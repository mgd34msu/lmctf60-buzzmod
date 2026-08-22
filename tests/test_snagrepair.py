import json
import hashlib
import os
import pathlib
import sys
import tempfile
import types
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import snagrepair
import snag_corpus
import runeio

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

    @staticmethod
    def rune(seeds=None, link_sources=None, map_name="alpha"):
        identity = runeio.RuneIdentity(
            map_name=map_name, bsp_checksum=1, entity_crc32=2,
            gravity=800.0, airaccelerate=0.0, maxvelocity=2000.0,
            host_physics_id=1, physics_flags=0, pmove_substep_ms=25,
            server_frame_ms=100,
        )
        seeds = seeds or (
            runeio.RuneSeed((0.0, 0.0, 0.0)),
            runeio.RuneSeed((64.0, 0.0, 0.0)),
            runeio.RuneSeed((0.0, 0.0, 128.0)),
        )
        header = types.SimpleNamespace(
            identity=identity, map_name=map_name, payload_crc32=33,
            header_crc32=44, action_contract_crc32=55,
            mechanism_contract_crc32=66, num_seeds=len(seeds), num_links=7,
        )
        if link_sources is None:
            link_sources = range(len(seeds))
        links = tuple(types.SimpleNamespace(source=source, action=0)
                      for source in link_sources)
        return types.SimpleNamespace(
            header=header, seeds=tuple(seeds), links=links)

    @staticmethod
    def report(name, seed, stuck, *, role=0, action=0, grounded=1, engaged=0,
               goal=100, sgoal=100, speed=0, link=0, hook_phase=0,
               door_hold=0, drop_locked=0, frame=10):
        return (
            f"SG {name}: role={role} seed={seed} goal={goal} sgoal={sgoal} "
            f"spd={speed} org=(0 0 0) link={link} act={action} "
            f"hp={hook_phase} dh={door_hold} dl={drop_locked} "
            f"st={stuck:.1f} gnd={grounded} eng={engaged} frm={frame}\n"
        )

    @staticmethod
    def census(name, frame, alive=1):
        return f"SGCENSUS {name}: frm={frame} alive={alive}\n"

    def test_stall_episode_keeps_z_and_post_needs_no_snag_signal(self):
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
        points = [(0.0, 0.0, 0.0, 1.0), (60.0, 0.0, 0.0, 2.0),
                  (120.0, 0.0, 0.0, 3.0), (0.0, 0.0, 200.0, 4.0)]
        first = stallcensus.cluster_points(points, radius=64.0)
        second = stallcensus.cluster_points(list(reversed(points)), radius=64.0)
        self.assertEqual(first, second)
        self.assertEqual(len(first), 2)
        self.assertEqual(first[0]["evidence_count"], 3)

    def test_authenticated_census_uses_entity_minus_one_and_exact_window(self):
        track = [(frame, 10.0 if frame % 2 else 0.0, 0.0, 0.0, 0)
                 for frame in range(10, 22)]
        decoded = {
            "map": "alpha", "svrecord": True, "frames": 30,
            "skins": {0: "Arach\\male/rb-rm"},
            "skin_epochs": {0: [(1, "Arach\\male/rb-rm")]},
            "parse_complete": True,
            "terminated": True,
            "wire_framenums": list(range(1, 31)),
            "tracks": {1: track},
            "yaws": {1: {frame: float(frame * 15) for frame in range(10, 22)}},
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
        try:
            row = stallcensus.analyze(
                "demo.dm2", {"alpha": {"red": [0.0, 0.0],
                                          "blue": [100.0, 0.0]}},
                {"alpha": self.identity}, expected_map="alpha",
                expected_players={"Arach": {"team": "red", "entity": 1}},
                frame_range=(10, 22),
                require_svrecord=True, cap_s=None,
                sg_report_lines=[
                    self.census("Arach", 10),
                    self.report("Arach", 0, 1.1, frame=10),
                    self.census("Arach", 20),
                    self.report("Arach", 0, 2.1, frame=20),
                ],
                server_frame_range=(9, 21), rune=self.rune(),
            )
            self.assertEqual(row["players"], ["Arach"])
            self.assertEqual(row["frame_range"], [10, 22])
            self.assertGreater(row["players_observed_min"], 0)
            self.assertEqual(row["route_stall_report_count"], 2)
            self.assertEqual(row["route_stall_report_counts"], {"Arach": 2})
            self.assertEqual(row["route_stall_players"], ["Arach"])
            self.assertEqual(row["route_stall_census_count"], 2)
            self.assertEqual(
                row["route_stall_census_alive_counts"], {"Arach": 2})
            self.assertEqual(row["route_stall_evidence"], [
                {"seed": 0, "evidence_count": 1, "duration_ms": 1100}
            ])
            with self.assertRaisesRegex(ValueError, "player set mismatch"):
                stallcensus.analyze(
                    "demo.dm2", {"alpha": {"red": [0.0, 0.0],
                                              "blue": [100.0, 0.0]}},
                    expected_map="alpha",
                    expected_players={"Rune": {"team": "red", "entity": 1}},
                    frame_range=(10, 22), require_svrecord=True, cap_s=None,
                )
            with self.assertRaisesRegex(ValueError, "half-open integer pair"):
                stallcensus.analyze(
                    "demo.dm2", {"alpha": {"red": [0.0, 0.0],
                                              "blue": [100.0, 0.0]}},
                    expected_map="alpha",
                    expected_players={"Arach": {"team": "red", "entity": 1}},
                    frame_range=(0, 22), require_svrecord=True, cap_s=None,
                )

            decoded["skin_epochs"] = {
                0: [(1, "Arach\\male/rb-rm"),
                    (15, "Intruder\\male/bb-bm")]
            }
            with self.assertRaisesRegex(ValueError, "changed identity"):
                stallcensus.analyze(
                    "demo.dm2", {"alpha": {"red": [0.0, 0.0],
                                              "blue": [100.0, 0.0]}},
                    expected_map="alpha",
                    expected_players={"Arach": {"team": "red", "entity": 1}},
                    frame_range=(10, 22), require_svrecord=True, cap_s=None,
                )
        finally:
            stallcensus.film, stallcensus.conduct = old_film, old_conduct

    def test_map_identity_and_rows_fail_closed(self):
        rows = [
            {"map": "alpha", "map_identity": self.identity,
             "accepted_route_stall_evidence": [
                 {"seed": 0, "evidence_count": 2, "duration_ms": 1200}
             ]},
            {"map": "beta", "map_identity": self.identity,
             "accepted_route_stall_evidence": []},
        ]
        self.assertEqual(
            snagrepair.seed_evidence_for_map(rows, "alpha"), {0: (2, 1200)})
        snagrepair.require_rune_identity(
            self.rune(), "alpha", snagrepair.identity_for_map(rows, "alpha")
        )
        mixed = json.loads(json.dumps(rows[0]))
        mixed["map_identity"]["bsp_checksum"] = 999
        with self.assertRaisesRegex(ValueError, "mixed map incarnations"):
            snagrepair.seed_evidence_for_map([rows[0], mixed], "alpha")
        with self.assertRaisesRegex(ValueError, "does not match"):
            snagrepair.require_rune_identity(self.rune(), "alpha",
                                              mixed["map_identity"])
        with self.assertRaisesRegex(ValueError, "accepted_route_stall_evidence"):
            snagrepair.seed_evidence_for_map([
                {"map": "alpha", "map_identity": self.identity}
            ], "alpha")

    def test_controller_seed_evidence_aggregates_without_geometric_guessing(self):
        rows = [
            {"map": "alpha", "map_identity": self.identity,
             "accepted_route_stall_evidence": [
                 {"seed": 0, "evidence_count": 2, "duration_ms": 1200}
             ]},
            {"map": "alpha", "map_identity": self.identity,
             "accepted_route_stall_evidence": [
                 {"seed": 0, "evidence_count": 4, "duration_ms": 2500}
             ]},
        ]
        rune = self.rune()
        evidence = snagrepair.seed_evidence_for_map(rows, "alpha")
        repairs = snagrepair.repairs_from_seed_evidence(
            rune, evidence, surcharge=900)
        self.assertEqual(repairs, [(0, 0.0, 0.0, 0.0, 6, 3700, 900)])

        # The visible cluster may be exactly midway between seeds.  It remains
        # diagnostic; the controller-selected seed is the policy authority.
        midpoint = stallcensus.cluster_points(
            [(32.0, 0.0, 0.0, 1.0)], radius=64.0)
        self.assertEqual(midpoint[0]["x"], 32.0)
        self.assertEqual(repairs[0][0], 0)

    def test_visible_and_route_episodes_join_one_to_one_or_fail(self):
        row = {
            "stall_episodes": [
                {"episode_id": "Arach:10:21", "player": "Arach",
                 "frame_start": 10, "frame_end_exclusive": 21,
                 "duration_s": 1.0, "x": 0.0, "y": 0.0, "z": 0.0},
                {"episode_id": "Rune:30:41", "player": "Rune",
                 "frame_start": 30, "frame_end_exclusive": 41,
                 "duration_s": 1.0, "x": 64.0, "y": 0.0, "z": 0.0},
            ],
            "route_stall_episodes": [
                {"player": "Arach", "seed": 0, "frame_start": 9,
                 "frame_end_exclusive": 22, "evidence_count": 1,
                 "duration_ms": 1300},
                {"player": "Arach", "seed": 1, "frame_start": 50,
                 "frame_end_exclusive": 61, "evidence_count": 1,
                 "duration_ms": 1100},
            ],
            "snag_clusters": [
                {"x": 0.0, "y": 0.0, "z": 0.0,
                 "evidence_count": 1, "duration_s": 1.0,
                 "episode_ids": ["Arach:10:21"]},
                {"x": 64.0, "y": 0.0, "z": 0.0,
                 "evidence_count": 1, "duration_s": 1.0,
                 "episode_ids": ["Rune:30:41"]},
            ],
        }
        joined = snagrepair.correlate_stall_evidence(row)
        self.assertEqual(joined["accepted_route_stall_evidence"], [
            {"seed": 0, "evidence_count": 1, "duration_ms": 1300}
        ])
        self.assertEqual(
            [item["classification"] for item in joined["cluster_dispositions"]],
            ["ATTRIBUTED_ROUTE_STALLS",
             "VISIBLE_STALL_WITHOUT_ROUTE_AUTHORITY"],
        )
        self.assertEqual(
            [item["seed"] for item in joined["unmatched_route_episodes"]], [1]
        )

        ambiguous = json.loads(json.dumps(row))
        ambiguous["route_stall_episodes"].append({
            "player": "Arach", "seed": 2, "frame_start": 15,
            "frame_end_exclusive": 20, "evidence_count": 1,
            "duration_ms": 500,
        })
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            snagrepair.correlate_stall_evidence(ambiguous)

        missing_cluster = json.loads(json.dumps(row))
        missing_cluster["snag_clusters"].pop()
        with self.assertRaisesRegex(ValueError, "partition"):
            snagrepair.correlate_stall_evidence(missing_cluster)

    def test_route_stall_reducer_counts_episodes_and_rejects_bad_rows(self):
        lines = [
            self.report("Arach", 0, 1.1, frame=10),
            self.report("Rune", 1, 0.0, frame=10),
            self.report("Arach", 0, 2.1, frame=20),
            self.report("Arach", 0, 4.0, action=1, frame=30),
            self.report("Arach", 0, 1.2, frame=40),
            self.report("Rune", 1, 1.5, grounded=0, frame=20),
        ]
        reduced = stallcensus.route_stall_evidence(lines, ["Arach", "Rune"])
        self.assertEqual(reduced["report_count"], 6)
        self.assertEqual(reduced["players"], ["Arach", "Rune"])
        self.assertEqual(reduced["evidence"], [
            {"seed": 0, "evidence_count": 2, "duration_ms": 3300}
        ])

        with self.assertRaisesRegex(ValueError, "missing admitted players"):
            stallcensus.route_stall_evidence(
                [self.report("Arach", 0, 1.1)], ["Arach", "Rune"])
        malformed = self.report("Arach", 0, 1.1).replace(" sgoal=100", "")
        with self.assertRaisesRegex(ValueError, "malformed SG report"):
            stallcensus.route_stall_evidence([malformed], ["Arach"])
        with self.assertRaisesRegex(ValueError, "unexpected SG telemetry"):
            stallcensus.route_stall_evidence(
                [self.report("Intruder", 0, 1.1)], ["Arach"])

    def test_route_stall_excluded_cumulative_episode_is_poisoned(self):
        for excluded in (
                self.report("Arach", 0, 2.1, action=1, frame=20),
                self.report("Arach", 0, 2.1, grounded=0, frame=20),
                self.report("Arach", 0, 2.1, engaged=1, frame=20)):
            reduced = stallcensus.route_stall_evidence([
                self.report("Arach", 0, 1.1, frame=10),
                excluded,
                self.report("Arach", 0, 3.1, frame=30),
            ], ["Arach"])
            self.assertEqual(reduced["evidence"], [
                {"seed": 0, "evidence_count": 1, "duration_ms": 1100}
            ])

        # An observed cumulative reset starts a new admissible episode.
        reduced = stallcensus.route_stall_evidence([
            self.report("Arach", 0, 1.1, frame=10),
            self.report("Arach", 0, 2.1, action=1, frame=20),
            self.report("Arach", 0, 0.0, frame=30),
            self.report("Arach", 0, 1.2, frame=40),
        ], ["Arach"])
        self.assertEqual(reduced["evidence"], [
            {"seed": 0, "evidence_count": 2, "duration_ms": 2300}
        ])

        # Neither a nearest-seed boundary nor a duplicated cumulative sample
        # proves that production reset stuck_time.
        reduced = stallcensus.route_stall_evidence([
            self.report("Arach", 0, 1.1, frame=10),
            self.report("Arach", 0, 2.1, action=1, frame=20),
            self.report("Arach", 1, 3.1, frame=30),
        ], ["Arach"])
        self.assertEqual(reduced["evidence"], [
            {"seed": 0, "evidence_count": 1, "duration_ms": 1100}
        ])
        reduced = stallcensus.route_stall_evidence([
            self.report("Arach", 0, 1.1, frame=10),
            self.report("Arach", 0, 2.1, action=1, frame=20),
            self.report("Arach", 0, 2.1, frame=30),
        ], ["Arach"])
        self.assertEqual(reduced["evidence"], [
            {"seed": 0, "evidence_count": 1, "duration_ms": 1100}
        ])

    def test_route_stall_requires_valid_domain_and_cadence_coverage(self):
        with self.assertRaisesRegex(ValueError, "invalid SG action"):
            stallcensus.route_stall_evidence(
                [self.report("Arach", 0, 2.0, action=999)], ["Arach"])
        with self.assertRaisesRegex(ValueError, "invalid SG role"):
            stallcensus.route_stall_evidence(
                [self.report("Arach", 0, 2.0, role=5)], ["Arach"])
        with self.assertRaisesRegex(ValueError, "invalid SG state"):
            stallcensus.route_stall_evidence(
                [self.report("Arach", 0, 2.0, hook_phase=4)], ["Arach"])
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

    def test_water_tombstone_bounds_and_routeless_seed_reject(self):
        seeds = (
            runeio.RuneSeed((0.0, 0.0, 0.0), flags=runeio.RSF_WATER),
            runeio.RuneSeed((64.0, 0.0, 0.0), flags=runeio.RSF_TOMBSTONE),
        )
        with self.assertRaisesRegex(ValueError, "not live ground"):
            snagrepair.repairs_from_seed_evidence(
                self.rune(seeds), {0: (1, 1000)})
        with self.assertRaisesRegex(ValueError, "RUNE bounds"):
            snagrepair.repairs_from_seed_evidence(
                self.rune(), {999: (1, 1000)})
        with self.assertRaisesRegex(ValueError, "no live route"):
            snagrepair.repairs_from_seed_evidence(
                self.rune(link_sources=(0, 2)), {1: (1, 1000)})

    def test_render_binds_exact_artifact_and_supports_explicit_clean(self):
        rune = self.rune()
        digest = "a" * 64
        rune_digest = "b" * 64
        clean = snagrepair.render("alpha", rune, [], digest, rune_digest)
        self.assertIn("snag_format 2\n", clean)
        self.assertIn(f"rune_sha256 {rune_digest}\n", clean)
        self.assertIn("rune_payload_crc 33\n", clean)
        self.assertIn("rune_header_crc 44\n", clean)
        self.assertIn("rune_num_seeds 3\n", clean)
        self.assertTrue(clean.endswith("repairs 0\n"))

        repairs = snagrepair.repairs_from_seed_evidence(
            rune, {0: (2, 1200)})
        payload = snagrepair.render(
            "alpha", rune, repairs, digest, rune_digest)
        self.assertIn("repairs 1\nrepair 0 0.000 0.000 0.000 2 1200 1000\n",
                      payload)
        with self.assertRaisesRegex(ValueError, "64 lowercase"):
            snagrepair.render(
                "alpha", rune, repairs, "A" * 64, rune_digest)
        with self.assertRaisesRegex(ValueError, "rune_sha256"):
            snagrepair.render("alpha", rune, repairs, digest, "B" * 64)
        with self.assertRaisesRegex(ValueError, "map does not match"):
            snagrepair.render("beta", rune, repairs, digest, rune_digest)
        bad = list(repairs[0])
        bad[1] = 0.125
        with self.assertRaisesRegex(ValueError, "exact RUNE seed"):
            snagrepair.render("alpha", rune, [bad], digest, rune_digest)
        signed_zero = list(repairs[0])
        signed_zero[2] = -0.0
        with self.assertRaisesRegex(ValueError, "exact RUNE seed"):
            snagrepair.render(
                "alpha", rune, [signed_zero], digest, rune_digest)
        with self.assertRaisesRegex(ValueError, "duplicate repair seed"):
            snagrepair.render(
                "alpha", rune, repairs + repairs, digest, rune_digest)
        with self.assertRaisesRegex(ValueError, "no live route"):
            snagrepair.render(
                "alpha", self.rune(link_sources=(1, 2)), repairs, digest,
                rune_digest)

    def test_explicit_zero_cli_is_exact_and_rejects_conflicting_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            evidence = root / "evidence.json"
            evidence.write_text('{"accepted":false}\n', encoding="utf-8")
            rune_path = root / "alpha.rune"
            rune_path.write_bytes(b"authenticated-rune")
            output = root / "alpha.snag"
            with mock.patch.object(
                    snagrepair, "read_rune_and_sha256",
                    return_value=(self.rune(), "b" * 64)):
                self.assertEqual(0, snagrepair.main([
                    "--explicit-zero", "--map", "alpha",
                    "--rune", str(rune_path),
                    "--evidence-manifest", str(evidence),
                    "--output", str(output),
                ]))
                text = output.read_text(encoding="ascii")
                self.assertTrue(text.endswith("repairs 0\n"))
                self.assertIn(
                    f"evidence_sha256 {hashlib.sha256(evidence.read_bytes()).hexdigest()}\n",
                    text,
                )
                conflict = root / "conflict.snag"
                with self.assertRaises(SystemExit) as raised:
                    snagrepair.main([
                        str(root / "rows.jsonl"), "--explicit-zero",
                        "--map", "alpha", "--rune", str(rune_path),
                        "--evidence-manifest", str(evidence),
                        "--output", str(conflict),
                    ])
                self.assertEqual(2, raised.exception.code)
                self.assertFalse(conflict.exists())
                wrong = root / "wrong.snag"
                with self.assertRaises(SystemExit) as raised:
                    snagrepair.main([
                        "--explicit-zero", "--map", "wrong",
                        "--rune", str(rune_path),
                        "--evidence-manifest", str(evidence),
                        "--output", str(wrong),
                    ])
                self.assertEqual(2, raised.exception.code)
                self.assertFalse(wrong.exists())

    def test_atomic_write_replaces_complete_ascii_payload(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = pathlib.Path(temporary) / "maps" / "alpha.snag"
            snagrepair.atomic_write(path, "first\n")
            snagrepair.atomic_write(path, "second\n")
            self.assertEqual(path.read_bytes(), b"second\n")
            self.assertEqual(list(path.parent.glob(".alpha.snag.*")), [])

            evidence = pathlib.Path(temporary) / "evidence.json"
            evidence.write_bytes(b'{"format":"test"}\n')
            self.assertEqual(
                snagrepair.hash_evidence_manifest(evidence),
                hashlib.sha256(evidence.read_bytes()).hexdigest(),
            )
            symlink = pathlib.Path(temporary) / "evidence-link.json"
            symlink.symlink_to(evidence)
            with self.assertRaisesRegex(ValueError, "cannot open"):
                snagrepair.hash_evidence_manifest(symlink)

    def test_bootstrap_corpus_is_complete_explicit_and_non_overwriting(self):
        maps = snag_corpus.rune_corpus_controller.validate_manifest()
        positions = {name: index for index, name in enumerate(maps)}

        def fake_rune(path):
            map_name = pathlib.Path(path).stem
            return self.rune(map_name=map_name), {
                "size": 100 + positions[map_name],
                "sha256": hashlib.sha256(map_name.encode("ascii")).hexdigest(),
            }

        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            output = root / "corpus"
            with mock.patch.object(
                    snag_corpus, "read_rune_regular", side_effect=fake_rune):
                document, digest = snag_corpus.build_bootstrap(
                    maps, root / "runes", output,
                    snag_corpus.rune_corpus_controller.EXPECTED_MANIFEST_SHA256)
                verified, verified_digest = snag_corpus.verify_corpus(
                    maps, root / "runes", output,
                    snag_corpus.rune_corpus_controller.EXPECTED_MANIFEST_SHA256)
            self.assertEqual(document, verified)
            self.assertEqual(digest, verified_digest)
            self.assertEqual(document["map_count"], 175)
            self.assertEqual(len(document["maps"]), 175)
            self.assertEqual(len(list((output / "maps").glob("*.snag"))), 175)
            self.assertEqual(
                len(list((output / "evidence").glob("*.json"))), 175)
            first = (output / "maps" / f"{maps[0]}.snag").read_text("ascii")
            self.assertTrue(first.endswith("repairs 0\n"))
            self.assertIn(
                f"evidence_sha256 {document['maps'][0]['evidence_sha256']}\n",
                first,
            )
            manifest_bytes = (output / "snag-corpus-manifest.json").read_bytes()
            self.assertEqual(hashlib.sha256(manifest_bytes).hexdigest(), digest)
            self.assertEqual(0, output.stat().st_mode & 0o222)
            self.assertEqual(
                0, (output / "maps" / f"{maps[0]}.snag").stat().st_mode & 0o222)
            with self.assertRaisesRegex(ValueError, "already exists"):
                snag_corpus.build_bootstrap(
                    maps, root / "runes", output,
                    snag_corpus.rune_corpus_controller.EXPECTED_MANIFEST_SHA256)
            with self.assertRaisesRegex(ValueError, "not authoritative"):
                snag_corpus.build_bootstrap(
                    maps, root / "runes", root / "wrong", "f" * 64)
            first_path = output / "maps" / f"{maps[0]}.snag"
            first_path.chmod(0o644)
            with mock.patch.object(
                    snag_corpus, "read_rune_regular", side_effect=fake_rune):
                with self.assertRaisesRegex(
                        ValueError, "mutable or linked file"):
                    snag_corpus.verify_corpus(
                        maps, root / "runes", output,
                        snag_corpus.rune_corpus_controller.EXPECTED_MANIFEST_SHA256)
            first_path.chmod(0o444)

    def test_bootstrap_corpus_rejects_identity_drift_without_partial_output(self):
        maps = snag_corpus.rune_corpus_controller.validate_manifest()
        wrong_map = maps[90]

        def fake_rune(path):
            map_name = pathlib.Path(path).stem
            if map_name == wrong_map:
                map_name = "wrong"
            return self.rune(map_name=map_name), {
                "size": 1, "sha256": "0" * 64,
            }

        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            output = root / "corpus"
            with mock.patch.object(
                    snag_corpus, "read_rune_regular", side_effect=fake_rune):
                with self.assertRaisesRegex(
                        ValueError, f"expected '{wrong_map}'"):
                    snag_corpus.build_bootstrap(
                        maps, root / "runes", output,
                        snag_corpus.rune_corpus_controller.EXPECTED_MANIFEST_SHA256)
            self.assertFalse(output.exists())
            self.assertEqual(list(root.glob(".corpus.*")), [])

    def test_corpus_publish_requires_existing_component_safe_parent(self):
        maps = snag_corpus.rune_corpus_controller.validate_manifest()
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            missing = root / "missing" / "corpus"
            with self.assertRaisesRegex(ValueError, "output parent must"):
                snag_corpus.build_bootstrap(
                    maps, root / "runes", missing,
                    snag_corpus.rune_corpus_controller.EXPECTED_MANIFEST_SHA256)
            self.assertFalse(missing.parent.exists())

            actual = root / "actual"
            actual.mkdir()
            linked = root / "linked"
            linked.symlink_to(actual, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "output parent must"):
                snag_corpus.build_bootstrap(
                    maps, root / "runes", linked / "corpus",
                    snag_corpus.rune_corpus_controller.EXPECTED_MANIFEST_SHA256)
            self.assertEqual(list(actual.iterdir()), [])

    def test_corpus_reader_rejects_named_ctime_drift(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = pathlib.Path(temporary) / "authority.bin"
            path.write_bytes(b"authority\n")
            current = path.stat()
            changed = types.SimpleNamespace(
                st_mode=current.st_mode,
                st_dev=current.st_dev,
                st_ino=current.st_ino,
                st_size=current.st_size,
                st_mtime_ns=current.st_mtime_ns,
                st_ctime_ns=current.st_ctime_ns + 1,
                st_nlink=current.st_nlink,
            )
            with mock.patch.object(pathlib.Path, "stat", return_value=changed):
                with self.assertRaisesRegex(ValueError, "changed while reading"):
                    snag_corpus._read_regular(path)

    def test_bootstrap_publish_rejects_destination_and_parent_races(self):
        maps = snag_corpus.rune_corpus_controller.validate_manifest()

        def fake_rune(path):
            map_name = pathlib.Path(path).stem
            return self.rune(map_name=map_name), {
                "size": 1,
                "sha256": hashlib.sha256(map_name.encode("ascii")).hexdigest(),
            }

        with tempfile.TemporaryDirectory() as temporary:
            parent = pathlib.Path(temporary) / "parent"
            parent.mkdir()
            output = parent / "corpus"
            rename_noreplace = snag_corpus._rename_noreplace

            def collide(source_fd, source, destination_fd, destination):
                os.mkdir(destination, dir_fd=destination_fd)
                rename_noreplace(
                    source_fd, source, destination_fd, destination)

            with mock.patch.object(
                    snag_corpus, "read_rune_regular", side_effect=fake_rune), \
                    mock.patch.object(
                        snag_corpus, "_rename_noreplace", side_effect=collide):
                with self.assertRaisesRegex(ValueError, "already exists"):
                    snag_corpus.build_bootstrap(
                        maps, parent / "runes", output,
                        snag_corpus.rune_corpus_controller.EXPECTED_MANIFEST_SHA256)
            self.assertTrue(output.is_dir())
            self.assertEqual(list(output.iterdir()), [])
            self.assertEqual(list(parent.glob(".corpus.*")), [])

        with tempfile.TemporaryDirectory() as temporary:
            container = pathlib.Path(temporary)
            parent = container / "parent"
            moved = container / "retained-parent"
            attacker = container / "attacker"
            parent.mkdir()
            attacker.mkdir()
            output = parent / "corpus"
            write_bytes = snag_corpus._write_bytes
            replaced = False

            def replace_parent(path, payload):
                nonlocal replaced
                if not replaced:
                    replaced = True
                    parent.rename(moved)
                    parent.symlink_to(attacker, target_is_directory=True)
                return write_bytes(path, payload)

            with mock.patch.object(
                    snag_corpus, "read_rune_regular", side_effect=fake_rune), \
                    mock.patch.object(
                        snag_corpus, "_write_bytes", side_effect=replace_parent):
                with self.assertRaisesRegex(
                        (ValueError, snag_corpus.rune_corpus_controller.CorpusError),
                        "symlink path component|output parent changed"):
                    snag_corpus.build_bootstrap(
                        maps, parent / "runes", output,
                        snag_corpus.rune_corpus_controller.EXPECTED_MANIFEST_SHA256)
            self.assertEqual(list(attacker.iterdir()), [])
            self.assertFalse((moved / "corpus").exists())
            self.assertEqual(list(moved.glob(".corpus.*")), [])

    def test_final_corpus_consumes_exact_cycle_receipts_and_wrap(self):
        maps = snag_corpus.rune_corpus_controller.validate_manifest()
        topmaps = tuple(
            line for line in (ROOT / "tools/topmaps.txt").read_text(
                encoding="ascii").splitlines()
            if line and not line.startswith("#"))
        runner_bytes = b"# exact fleet verifier fixture\n"
        runner_sha = hashlib.sha256(runner_bytes).hexdigest()
        _topmaps, topmaps_record = snag_corpus._topmaps_authority()

        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            state_root = root / "state"
            evidence_root = root / "evidence-root"
            rune_dir = root / "runes"
            output = root / "final"
            state_root.mkdir()
            evidence_root.mkdir()
            rune_dir.mkdir()
            runner_path = root / "fleet-runner.py"
            runner_path.write_bytes(runner_bytes)
            (state_root / "fleet-owner.json").write_bytes(b'{"stopped":true}\n')
            (evidence_root / "evidence-ledger.jsonl").write_bytes(
                b'{"terminal":true}\n')

            receipt_items = []
            for lane_index in range(10):
                lane = f"s{lane_index + 1:02d}"
                directory = evidence_root / lane / "health"
                directory.mkdir(parents=True)
                for sequence in range(21):
                    map_name = topmaps[(lane_index + sequence) % 20]
                    receipt = {
                        "lane": lane,
                        "map": map_name,
                        "receipt_hash": hashlib.sha256(
                            f"content:{lane}:{sequence}".encode()).hexdigest(),
                        "runner_sha256": runner_sha,
                        "sequence": sequence,
                        "topmaps_sha256": topmaps_record["sha256"],
                    }
                    path = directory / f"health-{sequence:06d}-{map_name}.json"
                    path.write_bytes(snag_corpus.canonical_json(receipt))
                    receipt_items.append((path, receipt))
            receipts = tuple(receipt_items)
            calls = 0

            def verify(_state, _evidence):
                nonlocal calls
                calls += 1
                return receipts

            module = types.SimpleNamespace(
                LANES=tuple(f"s{index:02d}" for index in range(1, 11)),
                OFFSETS=tuple(range(10)),
                CANONICAL_TOPMAPS=topmaps,
                CANONICAL_TOPMAPS_SHA256=topmaps_record["sha256"],
                verify_stopped_residence_evidence=verify,
            )
            positions = {name: index for index, name in enumerate(maps)}

            def fake_rune(path):
                map_name = pathlib.Path(path).stem
                return self.rune(map_name=map_name), {
                    "size": 100 + positions[map_name],
                    "sha256": hashlib.sha256(
                        f"rune:{map_name}".encode()).hexdigest(),
                }

            def fake_analysis(_path, receipt, rune, _record):
                accepted = []
                if receipt["lane"] == "s01" and receipt["sequence"] == 0:
                    accepted = [
                        {"seed": 0, "evidence_count": 2,
                         "duration_ms": 1200}
                    ]
                return {
                    "analysis": {
                        "accepted_route_stall_evidence": accepted,
                        "map": receipt["map"],
                        "map_identity": snag_corpus._identity(rune),
                    },
                    "demo_sha256": "d" * 64,
                    "players": [],
                    "segment_sha256": "e" * 64,
                }

            runner_record = {
                "size": len(runner_bytes), "sha256": runner_sha}
            patches = (
                mock.patch.object(
                    snag_corpus, "_load_fleet_verifier",
                    return_value=(module, runner_record)),
                mock.patch.object(
                    snag_corpus, "read_rune_regular", side_effect=fake_rune),
                mock.patch.object(
                    snag_corpus, "_analyze_residence",
                    side_effect=fake_analysis),
            )
            with patches[0], patches[1], patches[2]:
                document, digest = snag_corpus.build_final(
                    maps, rune_dir, state_root, evidence_root, runner_path,
                    output,
                    snag_corpus.rune_corpus_controller.EXPECTED_MANIFEST_SHA256)
                verified, verified_digest = snag_corpus.verify_final_corpus(
                    maps, rune_dir, state_root, evidence_root, runner_path,
                    output,
                    snag_corpus.rune_corpus_controller.EXPECTED_MANIFEST_SHA256)
            self.assertEqual(document, verified)
            self.assertEqual(digest, verified_digest)
            self.assertGreaterEqual(calls, 4)
            self.assertEqual(document["format"], snag_corpus.FINAL_FORMAT)
            self.assertEqual(document["fleet"]["analyzed_residences"], 200)
            self.assertEqual(len(document["fleet"]["wrap_receipts"]), 10)
            self.assertEqual(len(document["maps"]), 175)
            first_top = maps.index(topmaps[0])
            self.assertEqual(document["maps"][first_top]["residences"], 10)
            self.assertEqual(document["maps"][first_top]["repairs"], 1)
            non_top = next(index for index, name in enumerate(maps)
                           if name not in topmaps)
            self.assertEqual(
                document["maps"][non_top]["classification"],
                "NO_ACCEPTED_OBSERVATION")
            self.assertTrue(
                (output / "maps" / f"{topmaps[0]}.snag").read_text(
                    encoding="ascii").endswith(
                        "repair 0 0.000 0.000 0.000 2 1200 1000\n"))

    def test_final_cycle_rejects_runner_drift_and_missing_wrap(self):
        topmaps = tuple(
            line for line in (ROOT / "tools/topmaps.txt").read_text(
                encoding="ascii").splitlines()
            if line and not line.startswith("#"))
        runner_sha = "a" * 64
        _topmaps, topmaps_record = snag_corpus._topmaps_authority()
        with tempfile.TemporaryDirectory() as temporary:
            evidence_root = pathlib.Path(temporary)
            receipts = []
            for lane_index in range(10):
                lane = f"s{lane_index + 1:02d}"
                directory = evidence_root / lane
                directory.mkdir()
                for sequence in range(21):
                    receipt = {
                        "lane": lane,
                        "map": topmaps[(lane_index + sequence) % 20],
                        "receipt_hash": "b" * 64,
                        "runner_sha256": runner_sha,
                        "sequence": sequence,
                        "topmaps_sha256": topmaps_record["sha256"],
                    }
                    path = directory / f"{sequence}.json"
                    path.write_bytes(snag_corpus.canonical_json(receipt))
                    receipts.append((path, receipt))
            module = types.SimpleNamespace(
                LANES=tuple(f"s{index:02d}" for index in range(1, 11)),
                OFFSETS=tuple(range(10)), CANONICAL_TOPMAPS=topmaps,
                CANONICAL_TOPMAPS_SHA256=topmaps_record["sha256"])
            bad = list(receipts)
            bad[0][1]["runner_sha256"] = "c" * 64
            with self.assertRaisesRegex(ValueError, "runner identity"):
                snag_corpus._cycle_receipts(
                    module, tuple(bad), evidence_root, runner_sha)
            bad[0][1]["runner_sha256"] = runner_sha
            with self.assertRaisesRegex(ValueError, "cycle and wrap"):
                snag_corpus._cycle_receipts(
                    module, tuple(receipts[:-1]), evidence_root, runner_sha)

    def test_final_authority_guard_rejects_prepublication_rune_drift(self):
        maps = snag_corpus.rune_corpus_controller.validate_manifest()
        runner_record = {"size": 1, "sha256": "a" * 64}
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            state = root / "state"
            evidence = root / "evidence"
            runes = root / "runes"
            state.mkdir(); evidence.mkdir(); runes.mkdir()
            (state / "fleet-owner.json").write_bytes(b"o")
            (evidence / "evidence-ledger.jsonl").write_bytes(b"l")
            runner = root / "runner.py"
            runner.write_bytes(b"x")
            _owner, owner_record = snag_corpus._stable_record(
                state / "fleet-owner.json")
            _ledger, ledger_record = snag_corpus._stable_record(
                evidence / "evidence-ledger.jsonl")
            rune_records = {
                name: {"size": 1, "sha256": hashlib.sha256(
                    name.encode()).hexdigest()}
                for name in maps
            }
            guard = {
                "evidence_ledger": ledger_record,
                "fleet_owner": owner_record,
                "receipts": tuple(),
                "runes": rune_records,
                "runner": runner_record,
                "topmaps": snag_corpus._topmaps_authority()[1],
            }
            module = types.SimpleNamespace(
                verify_stopped_residence_evidence=lambda *_args: tuple())

            def drifted(path):
                name = pathlib.Path(path).stem
                record = dict(rune_records[name])
                if name == maps[90]:
                    record["sha256"] = "f" * 64
                return self.rune(map_name=name), record

            with mock.patch.object(
                    snag_corpus, "_load_fleet_verifier",
                    return_value=(module, runner_record)), \
                    mock.patch.object(
                        snag_corpus, "read_rune_regular",
                        side_effect=drifted):
                with self.assertRaisesRegex(ValueError, "final RUNE changed"):
                    snag_corpus._revalidate_final_authority(
                        maps, runes, state, evidence, runner, guard)


if __name__ == "__main__":
    unittest.main()
