#!/usr/bin/env python3
"""defbake.py -- bake human defensive occupancy into per-map .dpo sidecars.

The game loads this optional sidecar beside the rune.  Its layout mirrors
the .hmn/.hml/.hme sidecars, except that those are indexed by LINK and this
one is indexed by SEED, so the header validates against num_seeds:

    offset 0   int  magic     0x314F5044   ('DPO1')
    offset 4   int  version   matches the validated v2 input rune
    offset 8   int  num_seeds must equal rune->hdr.num_seeds
    offset 12  int  planes    4
    offset 16  int  graph_crc IEEE CRC32 of ordered rune seeds + links
    offset 20  u8[num_seeds]  post tier, red team
               u8[num_seeds]  post tier, blue team
               u8[num_seeds]  intercept tier, red team
               u8[num_seeds]  intercept tier, blue team

A tier is log-scaled 0..255 exactly as humanbake.py scales link traffic,
with 0 meaning "no human ever held this seed".  Post tiers come from
seconds of standing-still time inside defradius of the team's own flag
stand while that flag was home; intercept tiers come from where defenders
ended up ten seconds after an enemy steal.

Usage: defbake.py <rune_dir> <defense_json_dir> [<map> ...]
"""
import glob
import math
import os
import struct
import sys

from corpusgraph import (atomic_write_bytes, load_corpus, read_rune,
                         require_corpus_identity, require_safe_mapname,
                         validate_seed_weights)

DPO_MAGIC = 0x314F5044


def tiers(weights, ns):
    """{seed: weight} -> bytes(ns), log-scaled 0..255, 0 = never held"""
    top = max(weights.values()) if weights else 0
    out = bytearray(ns)
    if top <= 0:
        return bytes(out)
    lt = math.log1p(top)
    for s, w in weights.items():
        if w > 0:
            out[s] = max(1, min(255, int(255 * math.log1p(w) / lt)))
    return bytes(out)


def _weights(document, path, branch, team, num_seeds):
    group = document.get(branch)
    if not isinstance(group, dict):
        raise ValueError(f'{path}: {branch} must be an object')
    if team not in group:
        raise ValueError(f'{path}: {branch}.{team} is missing')
    return validate_seed_weights(group[team], path, f'{branch}.{team}',
                                 num_seeds)


def bake_map(rune_dir, json_dir, mapname):
    require_safe_mapname(mapname)
    rune_path = None
    for candidate in (os.path.join(rune_dir, 'maps', f'{mapname}.rune'),
                      os.path.join(rune_dir, f'{mapname}.rune')):
        if os.path.exists(candidate):
            rune_path = candidate
            break
    json_path = os.path.join(json_dir, f'{mapname}.defense.json')
    if not rune_path or not os.path.exists(json_path):
        raise FileNotFoundError(f'{mapname}: rune={bool(rune_path)} '
                                f'json={os.path.exists(json_path)}')

    document = load_corpus(json_path)
    rune = read_rune(rune_path, mapname, versions=(2,))
    version = rune['version']
    num_seeds = rune['num_seeds']
    graph_crc = rune['graph_crc32']
    seed_crc = rune['seed_crc32']
    identity = {'map': mapname, 'rune_num_seeds': num_seeds,
                'rune_seed_crc32': seed_crc}
    require_corpus_identity(document, json_path, identity)
    planes = [
        tiers(_weights(document, json_path, 'dwell_seed', 'red',
                       num_seeds), num_seeds),
        tiers(_weights(document, json_path, 'dwell_seed', 'blue',
                       num_seeds), num_seeds),
        tiers(_weights(document, json_path, 'intercept_seed', 'red',
                       num_seeds), num_seeds),
        tiers(_weights(document, json_path, 'intercept_seed', 'blue',
                       num_seeds), num_seeds),
    ]
    # Beside the rune, like every other sidecar -- unless DPO_OUT names
    # a staging directory (used to validate the format without dropping
    # files into a live game directory).
    output_dir = os.environ.get('DPO_OUT') or os.path.dirname(rune_path)
    os.makedirs(output_dir, exist_ok=True)
    output = os.path.join(output_dir, f'{mapname}.dpo')
    payload = struct.pack('<5I', DPO_MAGIC, version, num_seeds,
                          len(planes), graph_crc) + b''.join(planes)
    atomic_write_bytes(output, payload)
    used = [sum(1 for value in plane if value) for plane in planes]
    print(f'{mapname}: seeds={num_seeds} held red/blue={used[0]}/{used[1]} '
          f'intercept red/blue={used[2]}/{used[3]} -> {output}')


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if len(args) < 2:
        print('usage: defbake.py <rune_dir> <defense_json_dir> '
              '[<map> ...]', file=sys.stderr)
        return 2

    rune_dir, json_dir = args[:2]
    maps = args[2:]
    if not maps:
        maps = sorted(os.path.basename(p).split('.')[0]
                      for p in glob.glob(os.path.join(
                          json_dir, '*.defense.json')))
    if not maps:
        print(f'defbake: no defense corpora found under {json_dir}',
              file=sys.stderr)
        return 1

    failed = False
    for mapname in maps:
        try:
            bake_map(rune_dir, json_dir, mapname)
        except (OSError, ValueError, KeyError, OverflowError,
                struct.error) as error:
            print(f'defbake: {error}', file=sys.stderr)
            failed = True
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
