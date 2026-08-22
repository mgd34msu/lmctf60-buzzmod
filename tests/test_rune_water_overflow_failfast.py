#!/usr/bin/env python3
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "slipgate/sg_rune.c").read_text(encoding="utf-8")


class WaterOverflowFailFastTests(unittest.TestCase):
    def test_overflow_stops_before_exposure_and_base_links(self) -> None:
        water = SOURCE.index('Rune_TelemetryPhaseStart("water-seed")')
        exposure = SOURCE.index('Rune_TelemetryPhaseStart("exposure")', water)
        section = SOURCE[water:exposure]
        seed = section.index("Seed_Water();")
        overflow = section.index("if (gen_water_overflow)", seed)
        failure = section.index(
            '"rune: FAILED: water seed capacity exhausted; "', overflow
        )
        cleanup = section.index("goto cleanup;", failure)
        self.assertLess(seed, overflow)
        self.assertLess(overflow, failure)
        self.assertLess(failure, cleanup)


if __name__ == "__main__":
    unittest.main()
