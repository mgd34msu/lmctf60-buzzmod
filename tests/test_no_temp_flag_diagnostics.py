#!/usr/bin/env python3
"""Keep the retired flag-reachability probe out of production frames."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NoTemporaryFlagDiagnosticsTest(unittest.TestCase):
    def test_flag_probe_is_absent_from_production_sources(self):
        for relative in ("g_main.c", "g_ctffunc.c", "g_ctffunc.h"):
            source = (ROOT / relative).read_text(encoding="utf-8")
            with self.subTest(path=relative):
                self.assertNotIn("BotFlagDiag", source)
                self.assertNotIn("FLAGDIAG", source)
                self.assertNotIn("BOTDEATH", source)
                self.assertNotIn("BOTKIT", source)


if __name__ == "__main__":
    unittest.main()
