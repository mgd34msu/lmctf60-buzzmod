#!/usr/bin/env python3
"""Bake carrier-escape traffic into authenticated HME sidecars.

Transition counts are mapped to the exact RUNE link order and log-scaled into
one byte per link. The output binds the complete validated RUNE identity.
"""
import math
import os
import struct
import sys

from corpusgraph import (atomic_write_bytes, load_corpus, read_rune,
                         require_current_rune_binding,
                         require_corpus_identity, require_safe_mapname,
                         rune_identity_from_rune, rune_link_pairs,
                         validate_transition_counts)
import sidecario


def bake_map(rune_dir, human_dir, mapname):
    require_safe_mapname(mapname)
    rune_path = None
    for candidate in (os.path.join(rune_dir, 'maps', f'{mapname}.rune'),
                      os.path.join(rune_dir, f'{mapname}.rune')):
        if os.path.exists(candidate):
            rune_path = candidate
            break
    json_path = os.path.join(human_dir, f'{mapname}.escape.json')
    if not rune_path or not os.path.exists(json_path):
        raise FileNotFoundError(f'{mapname}: rune={bool(rune_path)} '
                                f'json={os.path.exists(json_path)}')

    rune = read_rune(rune_path, mapname)
    num_seeds = rune['num_seeds']
    num_links = rune['num_links']
    pairs = rune_link_pairs(rune)

    document = load_corpus(json_path)
    identity = rune_identity_from_rune(rune)
    require_corpus_identity(document, json_path, identity)
    transitions = validate_transition_counts(document, json_path, num_seeds)
    counts = [transitions.get(f'{source}>{destination}', 0)
              for source, destination in pairs]
    top = max(counts) if counts else 0
    tiers = bytes(
        0 if count == 0 else
        max(1, min(255, int(255 * math.log1p(count) / math.log1p(top))))
        for count in counts)

    output = os.path.join(os.path.dirname(rune_path), f'{mapname}.hme')
    binding = sidecario.binding_from_rune(rune)
    payload = sidecario.encode(sidecario.HME, binding, tiers)
    atomic_write_bytes(
        output, payload,
        precommit=lambda: require_current_rune_binding(
            rune_path, mapname, binding),
    )
    used = sum(1 for tier in tiers if tier)
    print(f'{mapname}: links={num_links} human-used={used} '
          f'({100 * used // max(num_links, 1)}%) top_count={top} -> {output}')


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if len(args) < 3:
        print('usage: escapebake.py <rune_dir> <human_json_dir> '
              '<map> [<map> ...]', file=sys.stderr)
        return 2

    rune_dir, human_dir = args[:2]
    failed = False
    for mapname in args[2:]:
        try:
            bake_map(rune_dir, human_dir, mapname)
        except (OSError, ValueError, KeyError, OverflowError,
                struct.error) as error:
            print(f'escapebake: {error}', file=sys.stderr)
            failed = True
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
