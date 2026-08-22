#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "slipgate/sg_rune.c").read_text(encoding="utf-8")


def main() -> None:
    water = SOURCE.index('Rune_TelemetryPhaseStart("water-seed")')
    exposure = SOURCE.index('Rune_TelemetryPhaseStart("exposure")', water)
    section = SOURCE[water:exposure]
    seed = section.index("Seed_Water();")
    overflow = section.index("if (gen_water_overflow)", seed)
    failure = section.index(
        '"rune: FAILED: water seed capacity exhausted; "', overflow
    )
    cleanup = section.index("goto cleanup;", failure)
    assert seed < overflow < failure < cleanup


if __name__ == "__main__":
    main()
    print("test_rune_water_overflow_failfast: ok")
