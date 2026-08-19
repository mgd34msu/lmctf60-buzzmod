#!/usr/bin/env python3
"""escapee.py -- flag-carrier escape trajectories, mapped onto rune seeds.

For every flag steal print in a .dm2 demo, resolves the stealer's name to
an entity number (via the playerskins configstrings) and cuts that
entity's origin track for the next 200 demo frames (10Hz ~= 20s) or until
a window-ender print names the same player -- a capture, a return, or a
death obituary -- whichever comes first. Each cut track is mapped onto
the nearest rune seed per frame (demorune's spatial-hash method) and the
seed-to-seed transitions are counted. Counts are aggregated across the
whole demo corpus, one output file per map.

Entity-layer data is used from every demo, including ref-cam recordings:
the owner's ruling is that game info from any camera is fair game (only
POV kinematics are restricted to player-body recordings).

Output: tools/human/<map>.escape.json = an identity-stamped corpus containing
the map and active world/physics/seed identity, window count, and transition counts.

Usage: escapee.py [--rune-dir DIR] [--out DIR] [--replace]
                  <demo.dm2> [<demo.dm2> ...]
"""
import argparse
import collections
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dm2speed as D
import demoents as DE
from demokin import parse_playerstate_full
from demorune import SeedGrid, load_seed_graph
from corpusgraph import (MAX_CORPUS_COUNT, atomic_write_json, load_corpus,
                         require_corpus_identity, stamp_corpus_identity,
                         validate_transition_counts)

RUNE_DIR = '/home/buzzkill/Games/Quake2/lmctf-hooktest/maps'
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'human')
WINDOW_FRAMES = 200          # 10Hz * 20s
STEAL_PAT = ' stole the '
ENDER_PATS = (' captured the ', ' returned the ', ' was ')


def strip_color(s):
    """Q2 color coding sets the high bit per-char; mask it off, then drop
    anything left that isn't printable ASCII."""
    return ''.join(chr(ord(c) & 0x7f) for c in s)


def clean_name(s):
    s = strip_color(s)
    s = re.sub(r'[^\x20-\x7e]', '', s)
    return s.strip()


def rune_path_for(mapname, rune_dir):
    for cand in (f'{rune_dir}/{mapname}.rune',
                 f'{rune_dir}/maps/{mapname}.rune',
                 f'{rune_dir}/runes/{mapname}.rune'):
        if os.path.exists(cand):
            return cand
    return None


def walk_demo(path, grids, agg, rune_dir):
    """Returns (mapname, steals_found, windows_cut)."""
    with open(path, 'rb') as stream:
        data = stream.read()
    off = 0
    mapname = None
    skins = {}          # 0-based client index -> raw "name\model/skin"
    ents = {}           # entnum -> [x, y, z] live state
    frame_idx = 0
    active = []          # list of {entity, name, positions, frames_elapsed}
    grid_errors = {}
    steals_found = 0
    windows_cut = 0

    def get_grid():
        if mapname is None:
            return None
        if mapname not in grids:
            rp = rune_path_for(mapname, rune_dir)
            if rp:
                try:
                    seeds, eligible, identity = load_seed_graph(rp, mapname)
                    grids[mapname] = SeedGrid(seeds, eligible)
                    grids[mapname].rune_identity = identity
                except (OSError, ValueError, struct.error) as error:
                    grids[mapname] = False
                    grid_errors[mapname] = str(error)
            else:
                grids[mapname] = False
                grid_errors[mapname] = f'no rune under {rune_dir}'
        g = grids[mapname]
        if not g:
            grid_errors.setdefault(mapname, 'rune unavailable or invalid')
        return g if g else None

    def resolve_entity(name_clean):
        low = name_clean.lower()
        for i, skin in skins.items():
            skin_name = clean_name(skin.split('\\')[0])
            if skin_name.lower() == low:
                return i + 1
        return None

    def close_window(w):
        nonlocal windows_cut
        windows_cut += 1
        g = get_grid()
        if g is None or mapname is None:
            return
        if mapname not in agg:
            agg[mapname] = stamp_corpus_identity(
                {'map': mapname, 'windows': 0,
                 'transitions': collections.Counter()}, g.rune_identity)
        a = agg[mapname]
        a['windows'] += 1
        prev = -1
        for (fi, x, y, z) in w['positions']:
            s = g.nearest((x, y, z))
            if s < 0:
                continue
            if prev >= 0 and s != prev:
                a['transitions'][f'{prev}>{s}'] += 1
            prev = s

    while off + 4 <= len(data):
        (mlen,) = struct.unpack_from('<i', data, off)
        off += 4
        if mlen == -1:
            break
        if mlen < 0 or mlen > len(data) - off:
            raise ValueError(f'{path}: invalid demo message length {mlen}')
        r = D.R(data[off:off+mlen])
        off += mlen
        try:
            while not r.done():
                svc = r.u8()
                if svc == 12:
                    r.skip(9); r.str_(); r.skip(2); r.str_()
                elif svc == 13:
                    idx = r.u16()
                    sstr = r.str_()
                    if 1312 <= idx < 1312 + 256:
                        skins[idx - 1312] = sstr
                    elif idx == 33:
                        m = re.fullmatch(
                            r'maps/([A-Za-z0-9_]'
                            r'[A-Za-z0-9_-]{0,62})\.bsp', sstr)
                        if m:
                            mapname = m.group(1)
                elif svc == 14:
                    bits, num = D.parse_entity_bits(r)
                    o = ents.setdefault(num, [0.0, 0.0, 0.0])
                    DE.parse_delta_entity_track(r, bits, o)
                elif svc == 20:
                    r.skip(9)
                    ab = r.u8(); r.skip(ab)
                elif svc == 17:
                    parse_playerstate_full(r, {})
                elif svc == 18:
                    while True:
                        bits, num = D.parse_entity_bits(r)
                        if num == 0:
                            break
                        if bits & DE.U_REMOVE:
                            ents.pop(num, None)
                            continue
                        o = ents.setdefault(num, [0.0, 0.0, 0.0])
                        DE.parse_delta_entity_track(r, bits, o)
                    frame_idx += 1
                    still = []
                    for w in active:
                        o = ents.get(w['entity'])
                        if o is not None:
                            w['positions'].append((frame_idx, o[0], o[1], o[2]))
                        w['frames_elapsed'] += 1
                        if w['frames_elapsed'] >= WINDOW_FRAMES:
                            close_window(w)
                        else:
                            still.append(w)
                    active = still
                elif svc == 9:
                    D.parse_sound(r)
                elif svc == 3:
                    D.parse_temp_entity(r)
                elif svc in (1, 2):
                    r.skip(3)
                elif svc == 10:
                    lvl = r.u8()
                    s = r.str_()
                    cs = strip_color(s)
                    if STEAL_PAT in cs:
                        steals_found += 1
                        name = clean_name(cs.split(STEAL_PAT)[0])
                        ent = resolve_entity(name)
                        if ent is not None:
                            active.append({'entity': ent, 'name': name,
                                            'positions': [], 'frames_elapsed': 0})
                    else:
                        for pat in ENDER_PATS:
                            if pat in cs:
                                who = clean_name(cs.split(pat)[0]).lower()
                                keep = []
                                for w in active:
                                    if w['name'].lower() == who:
                                        close_window(w)
                                    else:
                                        keep.append(w)
                                active = keep
                                break
                elif svc == 7:
                    pass
                elif svc == 11:
                    r.str_()
                elif svc == 15:
                    r.str_()
                elif svc == 4:
                    r.str_()
                elif svc == 5:
                    r.skip(512)
                elif svc == 6:
                    pass
                else:
                    raise ValueError(f"svc {svc}")
        except Exception:
            continue

    for w in active:
        close_window(w)

    if mapname is not None and windows_cut and mapname in grid_errors:
        raise ValueError(f'{mapname}: {grid_errors[mapname]}; '
                         'no windows were written')
    return mapname, steals_found, windows_cut


def main(argv=None):
    parser = argparse.ArgumentParser(
        description='Mine post-steal carrier routes into rune-seed traffic.')
    parser.add_argument('--rune-dir', default=RUNE_DIR,
                        help=f'rune directory (default: {RUNE_DIR})')
    parser.add_argument('--out', default=OUT_DIR, metavar='DIR',
                        help=f'output directory (default: {OUT_DIR})')
    parser.add_argument(
        '--replace', action='store_true',
        help='replace each output corpus instead of merging prior counts')
    parser.add_argument('demos', nargs='+', metavar='DEMO')
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    if not os.path.isdir(args.rune_dir):
        print(f'escapee: rune directory does not exist: {args.rune_dir}',
              file=sys.stderr)
        return 2

    grids = {}
    agg = {}
    failed = []
    for path in args.demos:
        base = os.path.basename(path)
        try:
            mapname, steals_found, windows_cut = walk_demo(
                path, grids, agg, args.rune_dir)
            print(f"{base}: map={mapname} steals={steals_found} windows={windows_cut}")
        except Exception as e:
            failed.append((base, str(e)))
            print(f"{base}: FAILED ({e})")

    try:
        os.makedirs(args.out, exist_ok=True)
    except OSError as error:
        print(f'escapee: cannot create output directory {args.out}: {error}',
              file=sys.stderr)
        return 1
    print()
    for mapname, a in sorted(agg.items()):
        outpath = os.path.join(args.out, f'{mapname}.escape.json')
        try:
            windows = a['windows']
            transitions = collections.Counter(a['transitions'])
            # Merge by default so timeout-bounded chunks retain prior work.
            # --replace is the explicit opt-in to discard the prior corpus.
            if not args.replace and os.path.exists(outpath):
                previous = load_corpus(outpath)
                require_corpus_identity(previous, outpath, a)
                previous_windows = previous.get('windows')
                if (isinstance(previous_windows, bool) or
                        not isinstance(previous_windows, int) or
                        previous_windows < 0):
                    raise ValueError(f'{outpath}: windows must be a '
                                     'non-negative integer')
                windows += previous_windows
                previous_transitions = validate_transition_counts(
                    previous, outpath, a['rune_num_seeds'])
                for key, count in previous_transitions.items():
                    transitions[key] += count
            if (isinstance(windows, bool) or not isinstance(windows, int) or
                    windows < 0 or windows > MAX_CORPUS_COUNT):
                raise ValueError(f'{outpath}: merged windows count is invalid')
            output = stamp_corpus_identity(
                {'map': mapname, 'windows': windows,
                 'transitions': dict(transitions)}, a)
            validate_transition_counts(
                output, outpath, a['rune_num_seeds'])
            atomic_write_json(outpath, output)
            print(f'WROTE {outpath}: windows={windows} '
                  f'transitions={len(transitions)}')
        except (OSError, ValueError, KeyError, OverflowError) as error:
            failed.append((mapname, str(error)))
            print(f'{mapname}: FAILED TO WRITE ({error})')

    if failed:
        print(f"\n{len(failed)} demo or output operations failed:")
        for base, err in failed:
            print(f"  {base}: {err}")
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
