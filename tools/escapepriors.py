#!/usr/bin/env python3
"""escapepriors.py -- which way humans LEAVE the stand, per map.

Enhancement 6, part A. Every other human prior in this toolbox is about
WHERE the roads are (humanbake/escapebake: per-link traffic tiers). This
one is about the first decision of a carry: a human who just took the flag
picks a direction out of the room, and on any given map that choice is not
uniform -- some exits are the ones people actually use, and different
people use different ones. The bot's descent, being an argmin, always
leaves the same way. This mines the human answer as a DISTRIBUTION so the
game can sample from it instead of copying one player's habit (the owner's
standing ruling: mine the best behavior from EACH human, never conform to
one).

The measurement, per steal:

  * the steal itself comes from the effects-bit carry state machine in
    film.py (carry_windows: EF_FLAG1/EF_FLAG2 transitions on the player
    entity). No print-stream text is used, so this reads the same in
    either demo shape.
  * the flag STAND is film.flag_stands()'s per-color estimate for the demo
    (median of that color's carry-start positions -- the flag is always
    picked up at its own stand), falling back to this window's own start
    point when the color was only stolen once.
  * the EXIT BEARING is the compass direction from that stand to where the
    carrier actually was ~3 seconds later, quantized to 8 buckets of 45
    degrees. Bucket 0 is +x (E) and buckets advance counter-clockwise:
    E NE N NW W SW S SE. Three seconds is long enough for the exit taken
    to be unambiguous and short enough that it is still the exit and not
    the route home.

Sample selection inside the 3s window matters because a HUMAN .dm2 is a
client recording: entity updates only arrive for players inside the
recorder's PVS, so a carrier who is not the recorder has holes in his
track (film.py's module docstring measures 11-42% frame coverage on this
corpus). The bearing therefore uses the LAST sample at or before t0+3.0s
that is at least MIN_ELAPSED_S after the grab and at least MIN_RUN_U from
the stand -- a real displacement, whenever in the window it was actually
observed -- and the steal is skipped outright when no such sample exists.

Only client (human) demos are counted. A serverrecord capture is our own
bots playing and would poison the prior with the very habit this is meant
to replace; walk_demo auto-detects the shape and those files are skipped
and reported.

Usage:
    escapepriors.py <demo.dm2|dir> [...] [--out tools/escape-priors.json]
                    [--min-events N] [--verbose]

Writes the per-map bucket counts as escape-priors.json. The game reads
that file with a hand parser (sg_arach.c Escape_Load, cvar
sg_escapeprior), so the shape is a contract: a "maps" object whose values
are flat 8-element integer arrays in bucket order. Nothing else in the
file is read by the game, and no map name may appear anywhere outside
that object.
"""
import argparse
import collections
import importlib.abc
import importlib.machinery
import json
import math
import os
import sys
import types

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


# film.py is the demo walker AND a matplotlib renderer; only the walker is
# wanted here, and none of the functions used below (walk_demo, anonymize,
# carry_windows, flag_stands) touch numpy or matplotlib. Rather than
# duplicate a second .dm2 parser -- the parsing quirks in
# parse_delta_entity_film cost real calibration to get right -- the plotting
# imports are satisfied with inert stand-ins when those packages are not
# installed. If they ARE installed the real ones load and this does nothing.
class _Inert:
    def __init__(self, *a, **k): pass
    def __call__(self, *a, **k): return _Inert()
    def __getattr__(self, n): return _Inert()
    def __iter__(self): return iter(())


class _InertFinder(importlib.abc.MetaPathFinder, importlib.abc.Loader):
    ROOTS = ('numpy', 'matplotlib')

    def find_spec(self, name, path=None, target=None):
        if name.split('.')[0] in self.ROOTS:
            return importlib.machinery.ModuleSpec(name, self, is_package=True)
        return None

    def create_module(self, spec):
        m = types.ModuleType(spec.name)
        m.__path__ = []
        m.__getattr__ = lambda n: _Inert()
        return m

    def exec_module(self, module):
        pass


def _import_film():
    try:
        import numpy            # noqa: F401
        import matplotlib       # noqa: F401
    except ImportError:
        sys.meta_path.insert(0, _InertFinder())
    import film
    return film


F = _import_film()

BUCKETS = ('E', 'NE', 'N', 'NW', 'W', 'SW', 'S', 'SE')
WINDOW_S = 3.0        # the exit window: how long after the grab we look
MIN_ELAPSED_S = 1.0   # a sample sooner than this is still inside the stand
MIN_RUN_U = 160.0     # displacement from the stand that makes a bearing real
MIN_CARRY_S = 1.0     # shorter "carries" are flag touches, not steals


def bearing_bucket(dx, dy):
    """Compass bucket of (dx, dy), 45 degrees per bucket, bucket 0 centred
    on +x. Mirrors sg_arach.c SG_Bearing8 exactly (fold the atan2 result
    into 0..2pi BEFORE scaling -- see sg_rune.c Heading_Quantize for why
    the fold is not optional)."""
    a = math.degrees(math.atan2(dy, dx)) + 22.5
    while a < 0.0:
        a += 360.0
    return int(a / 45.0) & 7


def exit_bearing(window, stand):
    """The bucket this steal left by, or None when the demo never showed
    the carrier far enough from the stand inside the window (PVS holes, or
    a carry that died in the room)."""
    t0 = window['t0']
    best = None
    for f, x, y, z in window['path']:
        t = f / F.FPS
        if t - t0 < MIN_ELAPSED_S:
            continue
        if t - t0 > WINDOW_S:
            break
        dx, dy = x - stand[0], y - stand[1]
        if math.hypot(dx, dy) < MIN_RUN_U:
            continue
        best = (dx, dy)
    if best is None:
        return None
    return bearing_bucket(best[0], best[1])


def mine_demo(path, verbose=False):
    """-> (mapname, [(flag_color, bucket), ...], stats dict). mapname is
    None when the file did not parse into anything usable."""
    st = {'steals': 0, 'used': 0, 'short': 0, 'nobearing': 0}
    try:
        d = F.walk_demo(path)
    except Exception as exc:
        st['error'] = repr(exc)
        return None, [], st
    st['svrecord'] = d['svrecord']
    # the configstring spells the map however the recording server's map
    # command spelled it (LMCTF35 and lmctf35 both appear in this corpus);
    # the game looks the prior up by level.mapname, which is lower case
    if d['map']:
        d['map'] = d['map'].lower()
    st['map'] = d['map']
    if d['svrecord'] or not d['map']:
        return None, [], st
    labels, teams = F.anonymize(d)
    st['players'] = len(labels)
    windows, _ = F.carry_windows(d['tracks'], labels)
    stands = F.flag_stands(windows)
    out = []
    for w in windows:
        st['steals'] += 1
        if w['t1'] - w['t0'] < MIN_CARRY_S:
            st['short'] += 1
            continue
        stand = stands.get(w['color']) or w['path'][0][1:3]
        b = exit_bearing(w, stand)
        if b is None:
            st['nobearing'] += 1
            continue
        out.append((w['color'], b))
        st['used'] += 1
    if verbose:
        print(f"  {os.path.basename(path)}: map={d['map']} "
              f"players={len(labels)} steals={st['steals']} "
              f"used={st['used']} short={st['short']} "
              f"nobearing={st['nobearing']}")
    return d['map'], out, st


def collect(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            files += [os.path.join(p, n) for n in sorted(os.listdir(p))
                      if n.lower().endswith('.dm2')]
        else:
            files.append(p)
    return files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('paths', nargs='+', help='.dm2 files or directories')
    ap.add_argument('--out', default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), 'escape-priors.json'))
    ap.add_argument('--min-events', type=int, default=4,
                    help='maps with fewer mined steals are not written '
                         '(one human leaving one way once is not a prior)')
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args()

    files = collect(args.paths)
    # keyed both pooled ("<map>") and per stolen flag ("<map>:red"/":blue").
    # A CTF map is usually a mirror of itself, so "leave the red stand
    # heading east" and "leave the blue stand heading west" are the SAME
    # human decision wearing opposite world bearings; pooling the two
    # colours smears one habit into a two-humped distribution. The game
    # knows which stand its carrier just robbed, so it asks for that
    # colour first and only falls back to the pooled entry.
    per_map = collections.defaultdict(lambda: [0] * 8)
    per_map_demos = collections.defaultdict(set)
    tally = collections.Counter()
    for path in files:
        mapname, buckets, st = mine_demo(path, args.verbose)
        tally['files'] += 1
        if 'error' in st:
            tally['unparsed'] += 1
            continue
        if st.get('svrecord'):
            tally['serverrecord_skipped'] += 1
            continue
        if not mapname:
            tally['no_mapname'] += 1
            continue
        tally['client_demos'] += 1
        tally['steals'] += st['steals']
        tally['short'] += st['short']
        tally['nobearing'] += st['nobearing']
        tally['used'] += st['used']
        if buckets:
            per_map_demos[mapname].add(os.path.basename(path))
            for color, b in buckets:
                per_map[mapname][b] += 1
                per_map[f'{mapname}:{color}'][b] += 1

    kept = {m: c for m, c in sorted(per_map.items())
            if sum(c) >= args.min_events}
    dropped = sorted(m for m in per_map if m not in kept)

    doc = {
        '_format': 'escape-priors v1',
        '_buckets': list(BUCKETS),
        '_note': ('per map, counts of human exit bearings from the flag '
                  'stand ~3s after a steal; bucket 0 is +x and buckets '
                  'advance counter-clockwise'),
        '_window_s': WINDOW_S,
        '_min_events': args.min_events,
        '_corpus': {'files': tally['files'],
                    'client_demos': tally['client_demos'],
                    'serverrecord_skipped': tally['serverrecord_skipped'],
                    'steals_seen': tally['steals'],
                    'steals_used': tally['used']},
        'maps': kept,
    }
    # hand-formatted rather than json.dump'd so every map's counts sit on
    # ONE line: this file is read by a hand parser in the game (sg_arach.c
    # Escape_Load) and by humans diffing two mining runs, and both are
    # better served by a line per map than by 8 lines per map.
    with open(args.out, 'w') as fh:
        fh.write('{\n')
        for k, v in doc.items():
            if k != 'maps':
                fh.write(f' {json.dumps(k)}: {json.dumps(v)},\n')
        fh.write(' "maps": {\n')
        fh.write(',\n'.join(f'  {json.dumps(m)}: {json.dumps(c)}'
                            for m, c in kept.items()))
        fh.write('\n }\n}\n')

    print(f"files={tally['files']} client={tally['client_demos']} "
          f"svrecord_skipped={tally['serverrecord_skipped']} "
          f"unparsed={tally['unparsed']} no_map={tally['no_mapname']}")
    print(f"steals seen={tally['steals']} used={tally['used']} "
          f"too_short={tally['short']} no_bearing={tally['nobearing']}")
    print(f"maps written={len(kept)} (dropped under --min-events "
          f"{args.min_events}: {', '.join(dropped) if dropped else 'none'})")
    print(f"{'map':<17} {'n':>4}  " + ' '.join(f'{b:>4}' for b in BUCKETS) +
          '   H   top  demos')
    for m, c in kept.items():
        n = sum(c)
        h = -sum((v / n) * math.log2(v / n) for v in c if v)
        print(f"{m:<17} {n:>4}  " +
              ' '.join(f'{100*v//n:>3}%' if n else '   -' for v in c) +
              f"  {h:.2f}  {BUCKETS[c.index(max(c))]:<3} "
              f"{len(per_map_demos[m.split(':')[0]])}")
    print(f"-> {args.out}")


if __name__ == '__main__':
    main()
