#!/usr/bin/env python3
"""demorune.py -- extrapolate human routing from .dm2 demos onto rune graphs.

For each demo: decode the POV player's origin trace (reusing dm2speed's
protocol-34 walker), map every frame to the nearest rune seed of the map it
was recorded on, compress to a seed-visit sequence, and record every
seed-to-seed transition as one count of human traffic. Flag events from the
print stream (steals/captures/losses) are kept with timestamps so carry
segments can be cut later.

Aggregated output (one JSON per map beside the rune):
    { "map": ..., "demos": N, "frames": N,
      "transitions": {"a>b": count, ...},
      "seed_dwell": {seed: seconds, ...},
      "events": [[t, "text"], ...] }

Usage: demorune.py <rune_dir> <out_dir> <demo> [<demo> ...]
The map is read from configstring 33 ("maps/<name>.bsp"); demos whose map
has no rune in <rune_dir> are skipped with a note.
"""
import struct, sys, os, re, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dm2speed import R, parse_entity_bits, parse_delta_entity, \
    parse_packetentities, parse_playerstate, parse_sound, parse_temp_entity
from corpusgraph import (atomic_write_json, read_rune,
                         rune_identity_from_rune, rune_live_seed_indices,
                         rune_seed_origins, stamp_corpus_identity)


def load_seeds(path):
    rune = read_rune(path, versions=(1, 2, 3))
    return rune_seed_origins(rune)


def load_seed_graph(path, expected_map=None):
    """Decode one snapshot for localization and its matching corpus stamp."""
    rune = read_rune(path, expected_map, versions=(1, 2, 3))
    return (rune_seed_origins(rune), rune_live_seed_indices(rune),
            rune_identity_from_rune(rune))


class SeedGrid:
    """spatial hash: nearest seed without an O(n) scan per frame"""
    CELL = 256.0

    def __init__(self, seeds, eligible=None):
        self.seeds = seeds
        self.cells = collections.defaultdict(list)
        if eligible is None:
            self.eligible = None
        else:
            checked = set()
            for i in eligible:
                if (isinstance(i, bool) or not isinstance(i, int) or
                        not 0 <= i < len(seeds) or i in checked):
                    raise ValueError(f'invalid eligible seed {i!r}')
                checked.add(i)
            self.eligible = frozenset(checked)
        for i, s in enumerate(seeds):
            self.cells[self.key(s)].append(i)

    def key(self, p):
        return (int(p[0] // self.CELL), int(p[1] // self.CELL),
                int(p[2] // self.CELL))

    def nearest(self, p, maxr=340.0):
        kx, ky, kz = self.key(p)
        best, bd = -1, maxr * maxr
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for i in self.cells.get((kx+dx, ky+dy, kz+dz), ()):
                        s = self.seeds[i]
                        d = ((s[0]-p[0])**2 + (s[1]-p[1])**2 +
                             (s[2]-p[2])**2)
                        if d < bd:
                            bd, best = d, i
        if (best >= 0 and self.eligible is not None and
                best not in self.eligible):
            return -1
        return best


def walk_demo(path):
    """yield (frames, mapname, events): org per frame, flag prints"""
    data = open(path, 'rb').read()
    off = 0
    state = {'vel': (0, 0, 0), 'org': (0, 0, 0)}
    frames, events, mapname = [], [], None
    while off + 4 <= len(data):
        (mlen,) = struct.unpack_from('<i', data, off)
        off += 4
        if mlen == -1:
            break
        r = R(data[off:off+mlen])
        off += mlen
        try:
            while not r.done():
                svc = r.u8()
                if svc == 12:
                    r.skip(9); r.str_(); r.skip(2); r.str_()
                elif svc == 13:
                    idx = r.u16()
                    s = r.str_()
                    if idx == 33:
                        m = re.match(r'maps/(\w+)\.bsp', s)
                        if m:
                            mapname = m.group(1)
                elif svc == 14:
                    bits, num = parse_entity_bits(r)
                    parse_delta_entity(r, bits)
                elif svc == 20:
                    r.skip(8); r.skip(1)
                    ab = r.u8(); r.skip(ab)
                elif svc == 17:
                    parse_playerstate(r, state)
                elif svc == 18:
                    parse_packetentities(r)
                    frames.append(state.get('org', (0, 0, 0)))
                elif svc == 9:
                    parse_sound(r)
                elif svc == 3:
                    parse_temp_entity(r)
                elif svc in (1, 2):
                    r.skip(3)
                elif svc == 10:
                    r.skip(1)
                    s = r.str_()
                    if re.search(r'stole|captured|returned|lost the', s):
                        events.append([len(frames) / 10.0,
                                       s.strip()[:80]])
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
    return frames, mapname, events


def main():
    rune_dir, out_dir = sys.argv[1], sys.argv[2]
    agg = {}
    grids = {}
    identities = {}
    for demo in sys.argv[3:]:
        frames, mapname, events = walk_demo(demo)
        if not mapname or len(frames) < 100:
            print(f"skip {os.path.basename(demo)}: map={mapname} "
                  f"frames={len(frames)}")
            continue
        rp = None
        for cand in (f'{rune_dir}/{mapname}.rune',
                     f'{rune_dir}/maps/{mapname}.rune',
                     f'{rune_dir}/runes/{mapname}.rune'):
            if os.path.exists(cand):
                rp = cand
                break
        if not rp:
            print(f"skip {os.path.basename(demo)}: no rune for {mapname}")
            continue
        if mapname not in grids:
            seeds, eligible, identity = load_seed_graph(rp, mapname)
            grids[mapname] = SeedGrid(seeds, eligible)
            identities[mapname] = identity
        g = grids[mapname]
        a = agg.setdefault(mapname, {
            'map': mapname, 'demos': 0, 'frames': 0,
            'transitions': collections.Counter(),
            'seed_dwell': collections.Counter(), 'events': []})
        a['demos'] += 1
        prev = -1
        for org in frames:
            s = g.nearest(org)
            if s < 0:
                continue
            a['frames'] += 1
            a['seed_dwell'][s] += 1
            if prev >= 0 and s != prev:
                a['transitions'][f'{prev}>{s}'] += 1
            prev = s
        a['events'].extend(events)
        print(f"{os.path.basename(demo)}: {mapname} frames={len(frames)} "
              f"events={len(events)}")
    os.makedirs(out_dir, exist_ok=True)
    for mapname, a in agg.items():
        stamp_corpus_identity(a, identities[mapname])
        a['transitions'] = dict(a['transitions'])
        a['seed_dwell'] = {str(k): v/10.0 for k, v in a['seed_dwell'].items()}
        out = f'{out_dir}/{mapname}.human.json'
        atomic_write_json(out, a)
        print(f"WROTE {out}: demos={a['demos']} frames={a['frames']} "
              f"transitions={len(a['transitions'])}")


if __name__ == '__main__':
    main()
