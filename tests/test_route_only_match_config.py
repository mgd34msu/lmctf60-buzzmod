#!/usr/bin/env python3
"""Fixed ordinary-match contract for the non-topmap route-only evidence."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "tools" / "route-only-match.cfg"
MAPLIST = ROOT / "tools" / "route-only-maplist.txt"
RUNNER = ROOT / "tools" / "fleet-runner.py"

EXPECTED_CONFIG = (
    b"set dedicated 1\n"
    b"set deathmatch 1\n"
    b"set maxclients 16\n"
    b"set timelimit 10\n"
    b"set fraglimit 0\n"
    b"set capturelimit 0\n"
    b"set ctfflags 16\n"
    b"set minimumplayers 0\n"
    b"set maplist_file \"route-only-maplist.txt\"\n"
    b"set sv_botfill \"5:5\"\n"
    b"set ctf_statsdb 2\n"
    b"set sg_sessiondb 1\n"
    b"set sg_debug 1\n"
)

ROUTE_ONLY_MAPS = (
    "lmctf01", "lmctf06", "lmctf12", "lmctf15", "lmctf19",
    "lmctf25", "tomb05", "xmap13", "xmap18", "xmap26",
)


def _runner():
    spec = importlib.util.spec_from_file_location("route_only_runner_test", RUNNER)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load fleet runner")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class RouteOnlyMatchConfigTest(unittest.TestCase):
    def test_exact_ordinary_match_config(self):
        self.assertEqual(CONFIG.read_bytes(), EXPECTED_CONFIG)
        self.assertEqual(MAPLIST.read_bytes(), b"")
        self.assertIn(b'set maplist_file "route-only-maplist.txt"\n', EXPECTED_CONFIG)

    def test_fixed_route_only_inventory_is_not_a_topmap_authority(self):
        runner = _runner()
        manifest = {
            line for line in (ROOT / "tools" / "rune-corpus-maps.txt").read_text().splitlines()
            if line
        }
        topmaps = {
            line for line in (ROOT / "tools" / "topmaps.txt").read_text().splitlines()
            if line and not line.startswith("#")
        }
        self.assertEqual(runner.ROUTE_ONLY_MAPS, ROUTE_ONLY_MAPS)
        self.assertEqual(runner.ROUTE_ONLY_LANES, tuple(f"r{i:02d}" for i in range(1, 11)))
        self.assertEqual(len(set(runner.ROUTE_ONLY_MAPS)), 10)
        self.assertTrue(set(runner.ROUTE_ONLY_MAPS).issubset(manifest))
        self.assertFalse(set(runner.ROUTE_ONLY_MAPS) & topmaps)


if __name__ == "__main__":
    unittest.main()
