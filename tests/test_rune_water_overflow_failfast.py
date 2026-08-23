#!/usr/bin/env python3
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "slipgate/sg_rune.c").read_text(encoding="utf-8")


class WaterOverflowFailFastTests(unittest.TestCase):
    def test_water_shares_the_authoritative_graph_capacity(self) -> None:
        self.assertNotIn("SG_WATER_MAX", SOURCE)
        water_start = SOURCE.index("static void Seed_Water(void)")
        water_end = SOURCE.index("static void Link_Index_Build(void)", water_start)
        water = SOURCE[water_start:water_end]
        self.assertIn("i < gen_num_seeds && !capacity_exhausted", water)
        self.assertIn("capacity_exhausted = gen_seed_overflow;", water)
        self.assertIn("capacity_exhausted = !Seed_WaterNeighbours(here);", water)

    def test_only_distinct_admissible_seeds_can_overflow(self) -> None:
        add_start = SOURCE.index("static void Seed_Add(vec3_t origin)")
        add_end = SOURCE.index("static void Seed_Flood(void)", add_start)
        add = SOURCE[add_start:add_end]
        duplicate = add.index("if (Seed_Nearby(origin))")
        hazardous = add.index("if (submerged &&", duplicate)
        capacity = add.index("if (gen_num_seeds >= SEED_MAX)", hazardous)
        overflow = add.index("gen_seed_overflow = true;", capacity)
        append = add.index("VectorCopy(origin, gen_seeds[gen_num_seeds].origin)", overflow)
        self.assertLess(duplicate, hazardous)
        self.assertLess(hazardous, capacity)
        self.assertLess(capacity, overflow)
        self.assertLess(overflow, append)

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
