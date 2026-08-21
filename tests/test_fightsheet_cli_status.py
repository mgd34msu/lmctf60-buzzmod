import sys
from contextlib import redirect_stderr, redirect_stdout
import io
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fightsheet
import film
import demoents
import routesheet
import teamsheet


def message(payload):
    return struct.pack("<i", len(payload)) + payload


def sparse_serverrecord():
    serverdata = bytes([12]) + b"\0" * 9 + b"game\0\xff\xfflevel\0"
    map_name = bytes([13]) + struct.pack("<H", 33) + b"maps/sparse.bsp\0"
    skin = bytes([13]) + struct.pack("<H", 1312) + b"Arach\\male/rb-rm\0"
    snapshots = []
    for frame, present in ((1, True), (2, False), (3, True)):
        entity = bytes([1, 1]) + struct.pack("<h", frame * 8) if present else b""
        snapshots.append(
            message(bytes([20]) + struct.pack("<i", frame) + bytes([18]) + entity + b"\0\0")
        )
    return message(serverdata + map_name + skin) + b"".join(snapshots) + struct.pack("<i", -1)


class FightsheetCliStatusTest(unittest.TestCase):
    def run_main(self, arguments):
        with mock.patch.object(sys, "argv", ["fightsheet.py", *arguments]):
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                return fightsheet.main()

    def test_parser_verification_failure_returns_nonzero(self):
        with mock.patch.object(fightsheet, "verify_parser", return_value=False):
            self.assertEqual(self.run_main(["--verify-parser", "bad.dm2"]), 1)

    def test_scalar_failure_returns_nonzero(self):
        with mock.patch.object(fightsheet, "analyze_demo", side_effect=ValueError):
            self.assertEqual(self.run_main(["--scalars", "bad.dm2"]), 1)

    def test_render_failure_returns_nonzero(self):
        with mock.patch.object(fightsheet, "render_fight_sheet", side_effect=ValueError):
            self.assertEqual(self.run_main(["--out", "sheets", "bad.dm2"]), 1)

    def test_serverrecord_omission_removes_entity_for_that_frame(self):
        with tempfile.TemporaryDirectory() as directory:
            demo = Path(directory) / "sparse.dm2"
            demo.write_bytes(sparse_serverrecord())
            reference = film.walk_demo(demo)
            for capture_events in (False, True):
                decoded = fightsheet.walk_demo_events(
                    demo, capture_events=capture_events
                )
                self.assertEqual(reference["tracks"], decoded["tracks"])
                self.assertEqual([1, 3], [row[0] for row in decoded["tracks"][1]])
            entity_tracks = demoents.walk_entities(demo)["tracks"]
            self.assertEqual([1, 3], [row[0] for row in entity_tracks[1]])
            with redirect_stdout(io.StringIO()):
                self.assertTrue(fightsheet.verify_parser([demo]))


class OtherSheetCliStatusTest(unittest.TestCase):
    def run_main(self, module, arguments):
        with mock.patch.object(sys, "argv", [module.__name__, *arguments]):
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                return module.main()

    def test_film_render_failure_returns_nonzero(self):
        with mock.patch.object(film, "render_sheet", side_effect=ValueError):
            self.assertEqual(
                self.run_main(film, ["--out", "sheets", "bad.dm2"]), 1
            )

    def test_film_coverage_failure_returns_nonzero(self):
        with mock.patch.object(film, "walk_demo", side_effect=ValueError):
            self.assertEqual(
                self.run_main(
                    film, ["--coverage-report", "--out", "sheets", "bad.dm2"]
                ),
                1,
            )

    def test_film_pool_failure_returns_nonzero(self):
        with mock.patch.object(film, "walk_demo", side_effect=ValueError):
            self.assertEqual(
                self.run_main(film, ["--pool", "--out", "sheets", "bad.dm2"]),
                1,
            )

    def test_routesheet_render_failure_returns_nonzero(self):
        with mock.patch.object(
            routesheet, "render_routes_sheet", side_effect=ValueError
        ):
            self.assertEqual(
                self.run_main(routesheet, ["--out", "sheets", "bad.dm2"]), 1
            )

    def test_routesheet_scalar_failure_returns_nonzero(self):
        with mock.patch.object(routesheet, "analyze_demo", side_effect=ValueError):
            self.assertEqual(
                self.run_main(routesheet, ["--scalars", "bad.dm2"]), 1
            )

    def test_routesheet_build_nodes_failure_returns_nonzero(self):
        with mock.patch.object(routesheet.F, "walk_demo", side_effect=ValueError):
            self.assertEqual(
                self.run_main(routesheet, ["--build-nodes", "bad.dm2"]), 1
            )

    def test_teamsheet_render_failure_returns_nonzero(self):
        with mock.patch.object(teamsheet, "render_team_sheet", side_effect=ValueError):
            self.assertEqual(
                self.run_main(teamsheet, ["--out", "sheets", "bad.dm2"]), 1
            )

    def test_teamsheet_scalar_failure_returns_nonzero(self):
        with mock.patch.object(teamsheet, "_cache_key", return_value="bad"):
            with mock.patch.object(teamsheet.os.path, "exists", return_value=False):
                with mock.patch.object(
                    teamsheet, "analyze_demo", side_effect=ValueError
                ):
                    self.assertEqual(
                        self.run_main(
                            teamsheet,
                            ["--scalars", "--cache", "/dev/null", "bad.dm2"],
                        ),
                        1,
                    )

    def test_teamsheet_fills_missing_stands_from_the_installed_bsp(self):
        flags = {"red": [1.0, 2.0, 3.0], "blue": [4.0, 5.0, 6.0]}
        with mock.patch.object(
            teamsheet.MF, "flag_origins", return_value=flags
        ) as flag_origins:
            stands = teamsheet.resolve_stands(
                "sparse", [], None, "/games/lmctf"
            )
        self.assertEqual({"red": (1.0, 2.0), "blue": (4.0, 5.0)}, stands)
        flag_origins.assert_called_once_with("/games/lmctf", "sparse")

    def test_teamsheet_uses_fixture_when_the_bsp_is_unavailable(self):
        fixtures = {
            "sparse": {"red": [1.0, 2.0, 3.0], "blue": [4.0, 5.0, 6.0]}
        }
        with mock.patch.object(
            teamsheet.MF, "flag_origins", side_effect=FileNotFoundError
        ):
            stands = teamsheet.resolve_stands(
                "sparse", [], fixtures, "/missing/lmctf"
            )
        self.assertEqual({"red": (1.0, 2.0), "blue": (4.0, 5.0)}, stands)


if __name__ == "__main__":
    unittest.main()
