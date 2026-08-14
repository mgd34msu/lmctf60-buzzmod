#!/usr/bin/env python3
"""humanbake.py -- bake human demo traffic into per-map .hmn sidecars.

Reads <map>.rune (link table order is the contract) and tools/human/
<map>.human.json (transition counts from demorune.py), writes
maps/<map>.hmn: 20-byte header then one uint8 per link -- the link's
human-traffic tier, log-scaled to 0..255 with 0 = no human ever ran it.
The sidecar header carries the validated v2 input rune version. Its CRC32 binds the
sidecar to the exact ordered seed-and-link payload from which it was baked.

A transition a>b credits every rune link a->b (all actions: if a human
moved seam-to-seam, the road is real regardless of gait). The game loads
the sidecar beside the rune and prices highways cheaper under
sg_humanprior.

Usage: humanbake.py <rune_dir> <human_json_dir> <map> [<map> ...]
"""
import math
import os
import struct
import sys

from corpusgraph import (HEADER_SIZE, LINK_FMT, LINK_SIZE, SEED_SIZE,
                         atomic_write_bytes, load_corpus, read_rune,
                         require_corpus_identity, require_safe_mapname,
                         validate_transition_counts)

HMN_MAGIC = 0x484D4E31  # "1NMH"


def bake_map(rune_dir, human_dir, mapname):
    require_safe_mapname(mapname)
    rune_path = None
    for candidate in (os.path.join(rune_dir, 'maps', f'{mapname}.rune'),
                      os.path.join(rune_dir, f'{mapname}.rune')):
        if os.path.exists(candidate):
            rune_path = candidate
            break
    json_path = os.path.join(human_dir, f'{mapname}.human.json')
    if not rune_path or not os.path.exists(json_path):
        raise FileNotFoundError(f'{mapname}: rune={bool(rune_path)} '
                                f'json={os.path.exists(json_path)}')

    rune = read_rune(rune_path, mapname, versions=(2,))
    data = rune['data']
    version = rune['version']
    num_seeds = rune['num_seeds']
    num_links = rune['num_links']
    graph_crc = rune['graph_crc32']
    seed_crc = rune['seed_crc32']
    link_offset = HEADER_SIZE + num_seeds * SEED_SIZE
    pairs = []
    for i in range(num_links):
        link = struct.unpack_from(LINK_FMT, data,
                                  link_offset + i * LINK_SIZE)
        pairs.append((link[0], link[1]))

    document = load_corpus(json_path)
    identity = {'map': mapname, 'rune_num_seeds': num_seeds,
                'rune_seed_crc32': seed_crc}
    require_corpus_identity(document, json_path, identity)
    transitions = validate_transition_counts(document, json_path, num_seeds)
    counts = [transitions.get(f'{source}>{destination}', 0)
              for source, destination in pairs]
    top = max(counts) if counts else 0
    tiers = bytes(
        0 if count == 0 else
        max(1, min(255, int(255 * math.log1p(count) / math.log1p(top))))
        for count in counts)

    output = os.path.join(os.path.dirname(rune_path), f'{mapname}.hmn')
    payload = struct.pack('<5I', HMN_MAGIC, version, num_links, 0,
                          graph_crc) + tiers
    atomic_write_bytes(output, payload)
    used = sum(1 for tier in tiers if tier)
    print(f'{mapname}: links={num_links} human-used={used} '
          f'({100 * used // max(num_links, 1)}%) top_count={top} -> {output}')


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if len(args) < 3:
        print('usage: humanbake.py <rune_dir> <human_json_dir> '
              '<map> [<map> ...]', file=sys.stderr)
        return 2

    rune_dir, human_dir = args[:2]
    failed = False
    for mapname in args[2:]:
        try:
            bake_map(rune_dir, human_dir, mapname)
        except (OSError, ValueError, KeyError, OverflowError,
                struct.error) as error:
            print(f'humanbake: {error}', file=sys.stderr)
            failed = True
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
