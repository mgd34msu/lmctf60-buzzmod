#!/usr/bin/env python3
"""Read and summarize the one RUNE artifact layout."""
from __future__ import annotations
from dataclasses import dataclass
import argparse
import json

try:
    import runeio
except ModuleNotFoundError:
    from tools import runeio


@dataclass(frozen=True)
class Rune:
    path: str
    map_name: str
    seeds: tuple[runeio.RuneSeed, ...]
    links: tuple[runeio.RuneLink, ...]


def load_rune(path):
    artifact = runeio.read(path)
    return Rune(str(path), artifact.header.map_name, artifact.seeds, artifact.links)


def compute_stats(rune, *_unused):
    return {'map': rune.map_name, 'seeds': len(rune.seeds), 'links': len(rune.links)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rune_file')
    args = parser.parse_args(argv)
    print(json.dumps(compute_stats(load_rune(args.rune_file)), sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
