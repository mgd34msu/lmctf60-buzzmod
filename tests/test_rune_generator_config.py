#!/usr/bin/env python3
"""Check the standalone generator configuration contract."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "tools" / "rune.cfg"
EXPECTED = (
    b"set deathmatch 1\n"
    b"set maxclients 16\n"
    b"set timelimit 0\n"
    b"set capturelimit 8\n"
    b"set ctfflags 16\n"
    b"set minimumplayers 0\n"
    b"set bot_grapple 0\n"
    b"set bot_groundhook 0\n"
    b"set maplist_file \"__none__\"\n"
    b"set sv_botfill 0\n"
)


class RuneGeneratorConfigTest(unittest.TestCase):
    def test_config_is_the_standalone_generator_contract(self) -> None:
        self.assertTrue(CONFIG.is_file(), CONFIG)
        self.assertEqual(EXPECTED, CONFIG.read_bytes())


if __name__ == "__main__":
    unittest.main()
