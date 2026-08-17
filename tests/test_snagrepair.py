import json
import pathlib
import sys
import tempfile
import types
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import snagrepair
# The focused geometry functions do not need demo decoding.  Keep their test
# host-free when the optional film analysis dependency is not installed.
sys.modules.setdefault("film", types.ModuleType("film"))
sys.modules.setdefault("conduct", types.ModuleType("conduct"))
import stallcensus


class SnagRepairToolTest(unittest.TestCase):
    identity = {
        "bsp_checksum": 1, "entity_crc": 2, "physics_flags": 0,
        "gravity": 800.0, "airaccelerate": 0.0, "maxvelocity": 2000.0,
        "pmove_ms": 25, "frame_ms": 100, "host_physics_id": 1,
    }

    def test_stall_episode_keeps_z_and_post_needs_no_snag_signal(self):
        # Twelve frames provide two overlapping one-second windows.  This bot
        # is near its own stand but visibly jitters, so it is a snag.
        jitter = [(i * 0.1, 10.0 if i % 2 else 0.0, 0.0, 320.0)
                  for i in range(12)]
        yaw = {frame[0]: index * 15.0 for index, frame in enumerate(jitter)}
        stalls, posts, episodes = stallcensus.stall_and_post_events(
            jitter, yaw, (0.0, 0.0))
        self.assertGreater(stalls, 0.0)
        self.assertEqual(posts, 0.0)
        self.assertEqual({episode["z"] for episode in episodes}, {320.0})

        hold = [(i * 0.1, 0.0, 0.0, 320.0) for i in range(12)]
        hold_yaw = {frame[0]: index * 15.0 for index, frame in enumerate(hold)}
        stalls, posts, episodes = stallcensus.stall_and_post_events(
            hold, hold_yaw, (0.0, 0.0))
        self.assertEqual(stalls, 0.0)
        self.assertGreater(posts, 0.0)
        self.assertEqual(episodes, [])

    def test_cluster_is_3d_and_deterministic(self):
        # The first three points are transitively connected in 3D: A--B--C,
        # even though A and C are farther apart than the cluster radius.
        points = [(0.0, 0.0, 0.0, 1.0), (60.0, 0.0, 0.0, 2.0),
                  (120.0, 0.0, 0.0, 3.0), (0.0, 0.0, 200.0, 4.0)]
        first = stallcensus.cluster_points(points, radius=64.0)
        second = stallcensus.cluster_points(list(reversed(points)), radius=64.0)
        self.assertEqual(first, second)
        self.assertEqual(len(first), 2)
        self.assertEqual(first[0]["evidence_count"], 3)
        self.assertEqual(first[0]["duration_s"], 6.0)

    def test_map_isolation_and_deterministic_input(self):
        rows = [
            {"map": "alpha", "map_identity": self.identity, "snag_clusters": [
                {"x": 20.0, "y": 3.0, "z": 7.0,
                 "evidence_count": 2, "duration_s": 1.2}]},
            {"map": "beta", "map_identity": self.identity, "snag_clusters": [
                {"x": 999.0, "y": 3.0, "z": 7.0,
                 "evidence_count": 9, "duration_s": 8.0}]},
        ]
        alpha = snagrepair.render("alpha", self.identity,
                                  snagrepair.clusters_for_map(rows, "alpha"),
                                  64.0, 1000)
        self.assertIn("repair 20.000 3.000 7.000", alpha)
        self.assertNotIn("999.000", alpha)
        self.assertEqual(alpha, snagrepair.render(
            "alpha", self.identity, snagrepair.clusters_for_map(list(reversed(rows)), "alpha"),
            64.0, 1000))
        forbidden = ("version", "current", "active", "legacy", "forensic")
        self.assertTrue(all(word not in alpha.lower() for word in forbidden))

    def test_identity_and_runtime_bounds_fail_closed(self):
        row = {"map": "alpha", "map_identity": self.identity,
               "snag_clusters": [{"x": 1.0, "y": 2.0, "z": 3.0,
                                  "evidence_count": 1, "duration_s": 1.0}]}
        mismatched = dict(self.identity, bsp_checksum=999)
        mixed = dict(row)
        mixed["map_identity"] = mismatched
        with self.assertRaisesRegex(ValueError, "mixed map incarnations"):
            snagrepair.clusters_for_map([row, mixed], "alpha")
        with self.assertRaisesRegex(ValueError, "map_identity"):
            snagrepair.clusters_for_map([{"map": "alpha", "snag_clusters": []}], "alpha")

        duplicate = dict(row)
        records = snagrepair.clusters_for_map([row, duplicate], "alpha")
        self.assertEqual(len(records), 1)
        conflicting = dict(row)
        conflicting["snag_clusters"] = [{"x": 1.0, "y": 2.0, "z": 3.0,
                                         "evidence_count": 2, "duration_s": 1.0}]
        with self.assertRaisesRegex(ValueError, "conflicting repair coordinate"):
            snagrepair.clusters_for_map([row, conflicting], "alpha")

        sign_zero = [(-0.0004, 0.0, 0.0, 1, 1.0),
                     (0.0004, -0.0004, 0.0, 1, 1.0)]
        payload = snagrepair.render("alpha", self.identity, sign_zero, 64.0, 0)
        self.assertEqual(payload.count("repair "), 1)
        self.assertIn("repair 0.000 0.000 0.000", payload)
        with self.assertRaisesRegex(ValueError, "conflicting repair coordinate"):
            snagrepair.render("alpha", self.identity,
                              sign_zero + [(0.0, 0.0, 0.0, 2, 1.0)],
                              64.0, 0)

        float32_edge = [(65535.9994, 0.0, 0.0, 1, 1.0),
                        (65536.0, 0.0, 0.0, 1, 1.0)]
        payload = snagrepair.render("alpha", self.identity,
                                    float32_edge, 64.0, 0)
        self.assertEqual(payload.count("repair "), 1)
        self.assertIn("repair 65536.000", payload)
        with self.assertRaisesRegex(ValueError, "conflicting repair coordinate"):
            snagrepair.render("alpha", self.identity,
                              float32_edge + [(65536.0, 0.0, 0.0, 2, 1.0)],
                              64.0, 0)

        records = [(float(i), 0.0, 0.0, 1, 1.0) for i in range(64)]
        self.assertIn("repair", snagrepair.render("alpha", self.identity,
                                                    records, 64.0, 0))
        self.assertIn("repair", snagrepair.render(
            "alpha", self.identity, records + [records[0]], 64.0, 0))
        with self.assertRaisesRegex(ValueError, "record count"):
            snagrepair.render("alpha", self.identity,
                              records + [(64.0, 0.0, 0.0, 1, 1.0)], 64.0, 0)
        for bad in ((float("nan"), 0.0, 0.0, 1, 1.0),
                    (65537.0, 0.0, 0.0, 1, 1.0),
                    (0.0, 0.0, 0.0, 1000001, 1.0),
                    (0.0, 0.0, 0.0, 1, 86400.001)):
            with self.assertRaises(ValueError):
                snagrepair.render("alpha", self.identity, [bad], 64.0, 0)


if __name__ == "__main__":
    unittest.main()
