from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class TrainStationGenerationIntegrationTest(unittest.TestCase):
    def test_generation_discovers_and_proves_two_way_station_links(self) -> None:
        rune = source("slipgate/sg_rune.c")
        game = source("slipgate/sg_train_station_candidate_game.c")
        candidate = source("slipgate/sg_train_station_candidate.c")
        generation = rune + game + candidate
        oracle = source("slipgate/sg_oracle.c")

        self.assertIn(
            '#include "slipgate/sg_train_station_candidate_game.h"', rune
        )
        self.assertIn("static void Link_TrainStations(void)", rune)
        self.assertIn("SG_TrainStationCandidateGameGenerate", rune)
        self.assertIn("SG_TrainStationCandidatesDiscover", game)
        self.assertIn("SG_OracleTrainStationBoard", generation)
        self.assertIn("SG_OracleTrainStationCarry", generation)
        self.assertIn("SG_OracleTrainRideEgress", generation)
        self.assertIn("SG_MECHANISM_CONTROLLER_TRAIN_STATION", generation)
        self.assertIn("SG_TRAIN_STATION_DWELL_MS", generation)
        self.assertIn("Link_TrainStations();", rune)
        self.assertIn("qboolean SG_OracleTrainStationBoard", oracle)
        self.assertIn("qboolean SG_OracleTrainStationCarry", oracle)

    def test_station_binding_keeps_both_authored_dwells(self) -> None:
        candidate = source("slipgate/sg_train_station_candidate.c")

        self.assertIn("source_dwell_ms", candidate)
        self.assertIn("destination_dwell_ms", candidate)
        self.assertIn("SG_TRAIN_STATION_DWELL_MS", candidate)


if __name__ == "__main__":
    unittest.main()
