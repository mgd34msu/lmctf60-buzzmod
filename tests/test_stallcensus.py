#!/usr/bin/env python3
import sys
import types
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.modules.setdefault("film", types.ModuleType("film"))
sys.modules.setdefault("conduct", types.ModuleType("conduct"))

import runeio
import stallcensus


class StallCensusTest(unittest.TestCase):
    identity = {
        "bsp_checksum": 1, "entity_crc": 2, "physics_flags": 0,
        "gravity": 800.0, "airaccelerate": 0.0, "maxvelocity": 2000.0,
        "pmove_ms": 25, "frame_ms": 100, "host_physics_id": 1,
    }

    @staticmethod
    def rune():
        identity = runeio.RuneIdentity(
            map_name="alpha", bsp_checksum=1, entity_crc32=2,
            gravity=800.0, airaccelerate=0.0, maxvelocity=2000.0,
            host_physics_id=1, physics_flags=0, pmove_substep_ms=25,
            server_frame_ms=100,
        )
        seeds = (
            runeio.RuneSeed((0.0, 0.0, 0.0)),
            runeio.RuneSeed((64.0, 0.0, 0.0)),
        )
        return types.SimpleNamespace(
            header=types.SimpleNamespace(identity=identity),
            seeds=seeds,
            links=(
                types.SimpleNamespace(source=0, action=0),
                types.SimpleNamespace(source=1, action=0),
            ),
        )

    @staticmethod
    def report(name, seed, stuck, *, role=0, action=0, grounded=1,
               engaged=0, link=0, hook_phase=0, door_hold=0,
               drop_locked=0, frame=10):
        return (
            f"SG {name}: role={role} seed={seed} goal=100 sgoal=100 "
            f"spd=0 org=(0 0 0) link={link} act={action} "
            f"hp={hook_phase} dh={door_hold} dl={drop_locked} "
            f"st={stuck:.1f} gnd={grounded} eng={engaged} frm={frame}\n"
        )

    @staticmethod
    def census(name, frame, alive=1):
        return f"SGCENSUS {name}: frm={frame} alive={alive}\n"

    def test_stall_episode_keeps_height_and_post_is_not_a_stall(self):
        jitter = [
            (index * 0.1, 10.0 if index % 2 else 0.0, 0.0, 320.0)
            for index in range(12)
        ]
        yaw = {frame[0]: index * 15.0
               for index, frame in enumerate(jitter)}
        stalls, posts, episodes = stallcensus.stall_and_post_events(
            jitter, yaw, (0.0, 0.0))
        self.assertGreater(stalls, 0.0)
        self.assertEqual(posts, 0.0)
        self.assertEqual({episode["z"] for episode in episodes}, {320.0})

        hold = [(index * 0.1, 0.0, 0.0, 320.0)
                for index in range(12)]
        hold_yaw = {frame[0]: index * 15.0
                    for index, frame in enumerate(hold)}
        stalls, posts, episodes = stallcensus.stall_and_post_events(
            hold, hold_yaw, (0.0, 0.0))
        self.assertEqual(stalls, 0.0)
        self.assertGreater(posts, 0.0)
        self.assertEqual(episodes, [])

    def test_clusters_are_three_dimensional_and_deterministic(self):
        points = [
            (0.0, 0.0, 0.0, 1.0),
            (60.0, 0.0, 0.0, 2.0),
            (120.0, 0.0, 0.0, 3.0),
            (0.0, 0.0, 200.0, 4.0),
        ]
        first = stallcensus.cluster_points(points, radius=64.0)
        second = stallcensus.cluster_points(
            list(reversed(points)), radius=64.0)
        self.assertEqual(first, second)
        self.assertEqual(len(first), 2)
        self.assertEqual(first[0]["evidence_count"], 3)

    def test_authenticated_analysis_binds_players_frames_and_route_evidence(self):
        track = [
            (frame, 10.0 if frame % 2 else 0.0, 0.0, 0.0, 0)
            for frame in range(10, 22)
        ]
        decoded = {
            "map": "alpha",
            "svrecord": True,
            "frames": 30,
            "skins": {0: "Arach\\male/rb-rm"},
            "skin_epochs": {0: [(1, "Arach\\male/rb-rm")]},
            "parse_complete": True,
            "terminated": True,
            "wire_framenums": list(range(1, 31)),
            "tracks": {1: track},
            "yaws": {
                1: {frame: float(frame * 15) for frame in range(10, 22)}
            },
        }
        old_film, old_conduct = stallcensus.film, stallcensus.conduct
        stallcensus.film = types.SimpleNamespace(
            DURATION_CAP_S=850.0,
            walk_demo=lambda _path, **_kwargs: decoded,
            cap_tracks_to_duration=lambda *_args, **_kwargs: (False, 3.0),
            team_of=lambda skin: "red" if "/rb-r" in skin else None,
        )
        stallcensus.conduct = types.SimpleNamespace(
            contiguous_segments=lambda samples: [samples] if samples else []
        )
        stands = {"alpha": {"red": [0.0, 0.0], "blue": [100.0, 0.0]}}
        try:
            row = stallcensus.analyze(
                "demo.dm2",
                stands,
                {"alpha": self.identity},
                expected_map="alpha",
                expected_players={"Arach": {"team": "red", "entity": 1}},
                frame_range=(10, 22),
                require_svrecord=True,
                cap_s=None,
                sg_report_lines=[
                    self.census("Arach", 10),
                    self.report("Arach", 0, 1.1, frame=10),
                    self.census("Arach", 20),
                    self.report("Arach", 0, 2.1, frame=20),
                ],
                server_frame_range=(9, 21),
                rune=self.rune(),
            )
            self.assertEqual(row["players"], ["Arach"])
            self.assertEqual(row["frame_range"], [10, 22])
            self.assertGreater(row["players_observed_min"], 0)
            self.assertEqual(row["route_stall_report_count"], 2)
            self.assertEqual(row["route_stall_report_counts"], {"Arach": 2})
            self.assertEqual(row["route_stall_players"], ["Arach"])
            self.assertEqual(row["route_stall_census_count"], 2)
            self.assertEqual(
                row["route_stall_census_alive_counts"], {"Arach": 2}
            )
            self.assertEqual(row["route_stall_evidence"], [
                {"seed": 0, "evidence_count": 1, "duration_ms": 1100}
            ])

            with self.assertRaisesRegex(ValueError, "player set mismatch"):
                stallcensus.analyze(
                    "demo.dm2",
                    stands,
                    expected_map="alpha",
                    expected_players={
                        "Rune": {"team": "red", "entity": 1}
                    },
                    frame_range=(10, 22),
                    require_svrecord=True,
                    cap_s=None,
                )
            with self.assertRaisesRegex(ValueError, "half-open integer pair"):
                stallcensus.analyze(
                    "demo.dm2",
                    stands,
                    expected_map="alpha",
                    expected_players={
                        "Arach": {"team": "red", "entity": 1}
                    },
                    frame_range=(0, 22),
                    require_svrecord=True,
                    cap_s=None,
                )

            decoded["skin_epochs"] = {
                0: [
                    (1, "Arach\\male/rb-rm"),
                    (15, "Intruder\\male/bb-bm"),
                ]
            }
            with self.assertRaisesRegex(ValueError, "changed identity"):
                stallcensus.analyze(
                    "demo.dm2",
                    stands,
                    expected_map="alpha",
                    expected_players={
                        "Arach": {"team": "red", "entity": 1}
                    },
                    frame_range=(10, 22),
                    require_svrecord=True,
                    cap_s=None,
                )
        finally:
            stallcensus.film, stallcensus.conduct = old_film, old_conduct

    def test_route_stall_evidence_counts_only_eligible_episodes(self):
        lines = [
            self.report("Arach", 0, 1.1, frame=10),
            self.report("Rune", 1, 0.0, frame=10, link=1),
            self.report("Arach", 0, 2.1, frame=20),
            self.report("Arach", 0, 4.0, action=1, frame=30),
            self.report("Arach", 0, 1.2, frame=40),
            self.report("Rune", 1, 1.5, grounded=0, frame=20, link=1),
        ]
        reduced = stallcensus.route_stall_evidence(
            lines, ["Arach", "Rune"])
        self.assertEqual(reduced["report_count"], 6)
        self.assertEqual(reduced["players"], ["Arach", "Rune"])
        self.assertEqual(reduced["evidence"], [
            {"seed": 0, "evidence_count": 2, "duration_ms": 3300}
        ])

    def test_route_stall_evidence_rejects_bad_rows(self):
        with self.assertRaisesRegex(ValueError, "missing admitted players"):
            stallcensus.route_stall_evidence(
                [self.report("Arach", 0, 1.1)], ["Arach", "Rune"])
        malformed = self.report("Arach", 0, 1.1).replace(" sgoal=100", "")
        with self.assertRaisesRegex(ValueError, "malformed SG report"):
            stallcensus.route_stall_evidence([malformed], ["Arach"])
        with self.assertRaisesRegex(ValueError, "unexpected SG telemetry"):
            stallcensus.route_stall_evidence(
                [self.report("Intruder", 0, 1.1)], ["Arach"])
        with self.assertRaisesRegex(ValueError, "invalid SG action"):
            stallcensus.route_stall_evidence(
                [self.report("Arach", 0, 2.0, action=999)], ["Arach"])
        with self.assertRaisesRegex(ValueError, "invalid SG role"):
            stallcensus.route_stall_evidence(
                [self.report("Arach", 0, 2.0, role=5)], ["Arach"])
        with self.assertRaisesRegex(ValueError, "invalid SG state"):
            stallcensus.route_stall_evidence(
                [self.report("Arach", 0, 2.0, hook_phase=4)], ["Arach"])

    def test_excluded_cumulative_episode_stays_poisoned_until_reset(self):
        for excluded in (
            self.report("Arach", 0, 2.1, action=1, frame=20),
            self.report("Arach", 0, 2.1, grounded=0, frame=20),
            self.report("Arach", 0, 2.1, engaged=1, frame=20),
        ):
            reduced = stallcensus.route_stall_evidence([
                self.report("Arach", 0, 1.1, frame=10),
                excluded,
                self.report("Arach", 0, 3.1, frame=30),
            ], ["Arach"])
            self.assertEqual(reduced["evidence"], [
                {"seed": 0, "evidence_count": 1, "duration_ms": 1100}
            ])

        reduced = stallcensus.route_stall_evidence([
            self.report("Arach", 0, 1.1, frame=10),
            self.report("Arach", 0, 2.1, action=1, frame=20),
            self.report("Arach", 0, 0.0, frame=30),
            self.report("Arach", 0, 1.2, frame=40),
        ], ["Arach"])
        self.assertEqual(reduced["evidence"], [
            {"seed": 0, "evidence_count": 2, "duration_ms": 2300}
        ])

        for final in (
            self.report("Arach", 1, 3.1, frame=30),
            self.report("Arach", 0, 2.1, frame=30),
        ):
            reduced = stallcensus.route_stall_evidence([
                self.report("Arach", 0, 1.1, frame=10),
                self.report("Arach", 0, 2.1, action=1, frame=20),
                final,
            ], ["Arach"])
            self.assertEqual(reduced["evidence"], [
                {"seed": 0, "evidence_count": 1, "duration_ms": 1100}
            ])

    def test_authenticated_evidence_requires_cadence_and_rune_binding(self):
        with self.assertRaisesRegex(ValueError, "census"):
            stallcensus.route_stall_evidence(
                [self.report("Arach", 0, 2.0)], ["Arach"],
                expected_frame_range=(0, 9000), rune=self.rune())

        lines = [self.census("Arach", frame)
                 for frame in range(10, 101, 10)]
        lines.extend([
            self.report("Arach", 0, 0.0, frame=10),
            self.report("Arach", 0, 0.0, frame=20),
        ])
        reduced = stallcensus.route_stall_evidence(
            lines, ["Arach"], expected_frame_range=(0, 100),
            rune=self.rune())
        self.assertEqual(reduced["report_counts"], {"Arach": 2})
        self.assertEqual(reduced["census_count"], 10)
        self.assertEqual(reduced["evidence"], [])


if __name__ == "__main__":
    unittest.main()
