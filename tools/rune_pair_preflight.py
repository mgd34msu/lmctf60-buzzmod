#!/usr/bin/env python3
"""Authenticate installed RUNE/SNAG pairs before a fleet launch."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
from typing import Sequence

import snagrepair


MAP_RE = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_-]{0,62}\Z")


def validate_pairs(maps_dir: Path, map_names: Sequence[str]) -> None:
    if not map_names:
        raise ValueError("no maps selected")
    for map_name in map_names:
        if MAP_RE.fullmatch(map_name) is None:
            raise ValueError(f"invalid map name {map_name!r}")
        rune_path = maps_dir / f"{map_name}.rune"
        snag_path = maps_dir / f"{map_name}.snag"
        rune, rune_sha256 = snagrepair.read_rune_and_sha256(rune_path)
        if rune.header.map_name != map_name:
            raise ValueError(
                f"{map_name}.rune authenticates map {rune.header.map_name!r}"
            )
        snagrepair.validate_file(snag_path, rune, rune_sha256)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--maps-dir", type=Path, required=True)
    parser.add_argument("maps", nargs="+")
    args = parser.parse_args(argv)
    try:
        validate_pairs(args.maps_dir, args.maps)
    except ValueError as exc:
        parser.error(f"artifact preflight failed: {exc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
