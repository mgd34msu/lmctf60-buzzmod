#!/usr/bin/env python3
"""Executable and production-wiring checks for the strike frame adapter."""

from pathlib import Path
import json
import math
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import snagrepair  # noqa: E402
import stealstage  # noqa: E402


class StrikeIntegrationTest(unittest.TestCase):
    def _compile_function_section_probe(
            self, tmp: str, name: str, sources: list[str]) -> str:
        objects: list[Path] = []
        common = [
            "gcc", "-std=c11", "-I.", "-DSTDC_HEADERS",
            '-DARCH="x86_64"', '-DVER="strike-test"', "-DLINUX",
            "-DSG_STRIKE_TRANSITION_TEST_API", "-Wall", "-Wextra",
            "-Werror", "-Wpedantic", "-ffunction-sections",
            "-fdata-sections",
        ]
        for source in sources:
            obj = Path(tmp) / (Path(source).stem + ".o")
            subprocess.run(common + ["-c", source, "-o", str(obj)],
                           cwd=ROOT, check=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True)
            objects.append(obj)
        binary = Path(tmp) / name
        subprocess.run(
            ["gcc", "-Wl,--gc-sections", *map(str, objects), "-lm",
             "-o", str(binary)],
            cwd=ROOT, check=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True)
        result = subprocess.run([str(binary)], cwd=ROOT, check=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        return result.stdout

    def test_core_reducer_executable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sg-strike-core-") as tmp:
            binary = Path(tmp) / "sg_strike_test"
            compile_cmd = [
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wpedantic", "-I.", "tests/sg_strike_test.c",
                "slipgate/sg_strike.c", "-o", str(binary),
            ]
            subprocess.run(compile_cmd, cwd=ROOT, check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           text=True)
            result = subprocess.run([str(binary)], cwd=ROOT, check=True,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True)
            self.assertIn("sg_strike_test: ok", result.stdout)

    def test_production_adapter_executable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sg-strike-") as tmp:
            binary = Path(tmp) / "sg_strike_adapter_test"
            compile_cmd = [
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wpedantic", "-I.", "tests/sg_strike_adapter_test.c",
                "slipgate/sg_strike.c", "slipgate/sg_strike_adapter.c",
                "-o", str(binary),
            ]
            subprocess.run(compile_cmd, cwd=ROOT, check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           text=True)
            result = subprocess.run([str(binary)], cwd=ROOT, check=True,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True)
            self.assertIn("sg_strike_adapter_test: ok", result.stdout)

    def test_production_route_transitions_executable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sg-strike-transition-") as tmp:
            output = self._compile_function_section_probe(tmp,
                "sg_strike_transition_test", [
                    "tests/sg_strike_transition_test.c",
                    "slipgate/sg_descend.c",
                    "slipgate/sg_arach.c",
                    "slipgate/sg_strike.c",
                    "slipgate/sg_util.c",
                    "slipgate/sg_drop_live.c",
                    "slipgate/sg_swim_live.c",
                    "slipgate/sg_defense_supply.c",
                ])
            self.assertIn("sg_strike_transition_test: ok", output)

    def test_production_move_gates_executable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sg-strike-move-") as tmp:
            output = self._compile_function_section_probe(tmp,
                "sg_strike_move_gate_test", [
                    "tests/sg_strike_move_gate_test.c",
                    "slipgate/sg_move.c",
                    "slipgate/sg_strike.c",
                ])
            self.assertIn("sg_strike_move_gate_test: ok", output)

    def test_stage_a_exact_window_and_diagnostic_crop(self) -> None:
        roster = ("a", "b")
        joined = {"a": 10.0, "b": 18.0}
        left = {"a": 700.0, "b": 618.0}
        window = stealstage.exact_window(joined, left, roster)
        self.assertEqual(window, (18.0, 618.0))
        self.assertTrue(stealstage.in_window(18.0, window))
        self.assertTrue(stealstage.in_window(617.999, window))
        self.assertFalse(stealstage.in_window(618.0, window))
        with self.assertRaises(ValueError):
            stealstage.exact_window(joined, {**left, "b": 617.9}, roster)

        rows = [
            {"line": 9, "name": "a", "value": 0},
            {"line": 11, "name": "a", "value": 1},
            {"line": 14, "name": "b", "value": 2},
            {"line": 21, "name": "a", "value": 3},
        ]
        cropped = stealstage.crop_diagnostic_rows(rows, 10, 20, roster)
        self.assertEqual([row["value"] for row in cropped], [1, 2])
        with self.assertRaises(ValueError):
            stealstage.crop_diagnostic_rows(
                rows + [{"line": 14, "name": "b"}], 10, 20, roster)

    def test_stage_a_exact_window_rejects_ambiguous_numbers(self) -> None:
        joined = {"a": 10.0, "b": 18.0}
        left = {"a": 700.0, "b": 700.0}
        invalid = (True, "18.0", None, float("nan"), float("inf"),
                   float("-inf"))
        for value in invalid:
            with self.subTest(stream="joined", value=value):
                with self.assertRaises(ValueError):
                    stealstage.exact_window(
                        {**joined, "b": value}, left, ("a", "b"))
            with self.subTest(stream="left", value=value):
                with self.assertRaises(ValueError):
                    stealstage.exact_window(
                        joined, {**left, "b": value}, ("a", "b"))
            with self.subTest(stream="duration", value=value):
                with self.assertRaises(ValueError):
                    stealstage.exact_window(
                        joined, left, ("a", "b"), duration=value)
        for roster in ((), ("a", "a")):
            with self.subTest(roster=roster):
                with self.assertRaises(ValueError):
                    stealstage.exact_window(joined, left, roster)
        with self.assertRaises(ValueError):
            stealstage.exact_window(
                {"a": 1e308}, {"a": 1e308}, ("a",), duration=1e308)
        with self.assertRaises(ValueError):
            stealstage.exact_window(
                {"a": 1e308}, {"a": 1e308}, ("a",), duration=600.0)
        for duration in (0.0, -1.0):
            with self.subTest(duration=duration):
                with self.assertRaises(ValueError):
                    stealstage.exact_window(
                        joined, left, ("a", "b"), duration=duration)

    def test_stage_a_demo_alignment_rejects_invalid_evidence(self) -> None:
        connected = {"a": 20.0, "b": 21.0}
        frames = {"a": 100, "b": 110}
        invalid = (True, "20.0", None, float("nan"), float("inf"),
                   float("-inf"))
        for value in invalid:
            with self.subTest(stream="connected", value=value):
                with self.assertRaises(ValueError):
                    stealstage.demo_level_offset(
                        {**connected, "a": value}, frames)
            with self.subTest(stream="frames", value=value):
                with self.assertRaises(ValueError):
                    stealstage.demo_level_offset(
                        connected, {**frames, "a": value})
            with self.subTest(stream="fps", value=value):
                with self.assertRaises(ValueError):
                    stealstage.demo_level_offset(
                        connected, frames, fps=value)
        for fps in (0, -1.0):
            with self.subTest(fps=fps):
                with self.assertRaises(ValueError):
                    stealstage.demo_level_offset(connected, frames, fps=fps)

    def test_stage_a_time_alignment_and_flag_home_timeline(self) -> None:
        self.assertEqual(stealstage.demo_level_offset(
            {"a": 20.0, "b": 21.0, "c": 22.0},
            {"a": 100, "b": 105, "c": 110}), 10.5)
        teams = {"thief": "red", "returner": "blue"}
        events = [
            {"name": "thief", "kind": "F Pickup", "time": 20.0,
             "log_order": 10},
            {"name": "thief", "kind": "FC LostFlag", "time": 25.0,
             "log_order": 11},
        ]
        self.assertTrue(stealstage.flag_home_before(
            "blue", 20.0, events, teams))
        self.assertFalse(stealstage.flag_home_before(
            "blue", 20.1, events, teams))
        deadline = 55.0
        just_before = math.nextafter(deadline, -math.inf)
        just_after = math.nextafter(deadline, math.inf)
        self.assertFalse(stealstage.flag_home_before(
            "blue", just_before, events, teams))
        self.assertFalse(stealstage.flag_home_before(
            "blue", deadline, events, teams))
        self.assertTrue(stealstage.flag_home_before(
            "blue", just_after, events, teams))
        returned = events + [
            {"name": "returner", "kind": "F Return", "time": 27.0,
             "log_order": 12},
        ]
        self.assertTrue(stealstage.flag_home_before(
            "blue", 27.1, returned, teams))

    def test_stage_a_auto_return_rule_is_bound_to_production_source(self) -> None:
        source = (ROOT / "g_ctffunc.c").read_text()
        wave_start = source.index("void ctf_flagwave")
        wave_end = source.index("void ctf_TossEnt", wave_start)
        wave = source[wave_start:wave_end]
        self.assertIn(
            "(ent->droptime) && (level.time > ent->droptime + 30)", wave)
        self.assertNotIn("level.time >= ent->droptime + 30", wave)

        drop_start = source.index("void ctf_playerdropflag")
        drop_end = source.index("qboolean ctf_flagtouch", drop_start)
        drop = source[drop_start:drop_end]
        lost = drop.index('"FC LostFlag"')
        log_time = drop.index("level.time", lost)
        droptime = drop.index("whichflag->droptime = level.time", log_time)
        self.assertLess(lost, log_time)
        self.assertLess(log_time, droptime)

    def test_stage_a_flag_timeline_fails_closed_and_orders_ties(self) -> None:
        teams = {"thief": "red", "returner": "blue"}
        return_last = [
            {"name": "thief", "kind": "F Pickup", "time": 20.0,
             "log_order": 1},
            {"name": "thief", "kind": "FC LostFlag", "time": 25.0,
             "log_order": 2},
            {"name": "returner", "kind": "F Return", "time": 25.0,
             "log_order": 3},
        ]
        lost_last = [return_last[0], return_last[2] | {"log_order": 2},
                     return_last[1] | {"log_order": 3}]
        self.assertTrue(stealstage.flag_home_before(
            "blue", 25.1, return_last, teams))
        self.assertFalse(stealstage.flag_home_before(
            "blue", 25.1, lost_last, teams))

        malformed_streams = (
            [return_last[0] | {"time": float("nan")}],
            [return_last[0] | {"time": True}],
            [return_last[0] | {"name": "unknown"}],
            [return_last[0] | {"kind": "F Mystery"}],
            [return_last[0] | {"log_order": True}],
            [{key: value for key, value in return_last[0].items()
              if key != "log_order"}],
            [return_last[1],
             {key: value for key, value in return_last[2].items()
              if key != "log_order"}],
            [return_last[1], return_last[0]],
            [return_last[0], return_last[1] | {"log_order": 1}],
        )
        for events in malformed_streams:
            with self.subTest(events=events):
                with self.assertRaises(ValueError):
                    stealstage.flag_home_before("blue", 30.0, events, teams)
        for moment in (True, "30.0", float("nan"), float("inf")):
            with self.subTest(moment=moment):
                with self.assertRaises(ValueError):
                    stealstage.flag_home_before(
                        "blue", moment, return_last, teams)
        with self.assertRaises(ValueError):
            stealstage.flag_home_before(
                "blue", 30.0, return_last, {**teams, "thief": "green"})
        with self.assertRaises(ValueError):
            stealstage.flag_home_before(
                "blue", 21.0,
                return_last + [{"name": "nobody", "kind": "F Pickup",
                                "time": 100.0, "log_order": 4}], teams)
        with self.assertRaises(ValueError):
            stealstage.flag_home_before(
                "blue", 1.7e308,
                [{"name": "thief", "kind": "FC LostFlag",
                  "time": 1e308, "log_order": 1}], teams)

    def test_stage_a_approach_and_close_match_boundaries(self) -> None:
        teams = {"thief": "red"}
        track = [
            (99, 384.0, 0.0, 0.0, 0),
            (100, 383.9, 0.0, 0.0, 0),
            (101, 380.0, 0.0, 0.0, 0),
        ]
        approaches = stealstage.qualifying_approaches(
            "thief", "red", track, (0.0, 0.0, 0.0), 10.0,
            [], teams, (20.0, 620.0))
        self.assertEqual(approaches,
                         [{"name": "thief", "team": "red", "time": 20.0}])
        self.assertEqual(len(stealstage.match_close_pickups(
            approaches, [{"name": "thief", "kind": "F Pickup",
                          "time": 21.5}])), 1)
        self.assertEqual(stealstage.match_close_pickups(
            approaches, [{"name": "thief", "kind": "F Pickup",
                          "time": 21.5001}]), [])
        duplicate = approaches + [
            {"name": "thief", "team": "red", "time": 20.2}]
        matches = stealstage.match_close_pickups(duplicate, [
            {"name": "thief", "kind": "F Pickup", "time": 20.3},
            {"name": "thief", "kind": "F Pickup", "time": 20.4},
        ])
        self.assertEqual([match["approach"]["time"] for match in matches],
                         [20.2, 20.0])

    def test_stage_a_geometry_crop_and_matching_fail_closed(self) -> None:
        valid_track = [
            (99, 384.0, 0.0, 0.0, 0),
            (100, 383.9, 0.0, 0.0, 0),
        ]
        call = lambda **changes: stealstage.qualifying_approaches(
            changes.get("name", "thief"), changes.get("team", "red"),
            changes.get("track", valid_track),
            changes.get("stand", (0.0, 0.0, 0.0)),
            changes.get("level_offset", 10.0), changes.get("events", []),
            changes.get("teams", {"thief": "red"}),
            changes.get("window", (20.0, 620.0)),
            fps=changes.get("fps", 10.0))
        invalid_calls = (
            {"track": [(99, float("nan"), 0.0, 0.0, 0), valid_track[1]]},
            {"track": [valid_track[0], (100, 383.9, float("inf"), 0.0, 0)]},
            {"track": [valid_track[0], (99, 383.9, 0.0, 0.0, 0)]},
            {"track": [valid_track[0]]},
            {"stand": (float("nan"), 0.0, 0.0)},
            {"level_offset": float("nan")},
            {"window": (20.0, float("inf"))},
            {"fps": 0.0},
            {"team": "blue"},
        )
        for changes in invalid_calls:
            with self.subTest(approach=changes):
                with self.assertRaises(ValueError):
                    call(**changes)

        valid_events = [
            {"name": "thief", "kind": "F Pickup", "time": 20.0},
            {"name": "thief", "kind": "F Pickup", "time": 21.0},
        ]
        self.assertEqual(stealstage.crop_events(
            valid_events, (20.0, 21.0)), [valid_events[0]])
        malformed_events = (
            [{"name": "thief"}],
            [{"time": float("nan")}],
            [{"time": True}],
            [{"time": 21.0}, {"time": 20.0}],
        )
        for events in malformed_events:
            with self.subTest(crop=events):
                with self.assertRaises(ValueError):
                    stealstage.crop_events(events, (20.0, 30.0))
        for window in ((float("nan"), 30.0), (30.0, 20.0),
                       (True, 30.0)):
            with self.subTest(window=window):
                with self.assertRaises(ValueError):
                    stealstage.crop_events([], window)

        approach = {"name": "thief", "team": "red", "time": 20.0}
        pickup = {"name": "thief", "kind": "F Pickup", "time": 20.5}
        invalid_matches = (
            ([approach | {"time": float("nan")}], [pickup], 1.5),
            ([approach], [pickup | {"time": float("inf")}], 1.5),
            ([approach | {"name": ""}], [pickup], 1.5),
            ([approach], [pickup | {"kind": "F Return"}], 1.5),
            ([approach], [pickup], float("nan")),
            ([approach], [pickup], True),
            ([approach], [pickup], -1.0),
        )
        for approaches, pickups, delay in invalid_matches:
            with self.subTest(match=(approaches, pickups, delay)):
                with self.assertRaises(ValueError):
                    stealstage.match_close_pickups(
                        approaches, pickups, delay=delay)

    def test_steal_stage_a_contract_schema_is_frozen(self) -> None:
        contract = json.loads(
            (ROOT / "tools/steal-stage-a-contract.json").read_text())
        self.assertEqual(contract["metric_version"],
                         "steal-close-stage-a-v1")
        self.assertEqual(contract["frozen_before_candidate_trial"],
                         "2026-08-19")
        self.assertNotIn("frozen_before_candidate_change", contract)
        identity = contract["match_identity"]
        self.assertEqual(set(identity["required_equal_fields"]), {
            "metric_version", "map_bsp_sha256", "configuration_sha256",
            "roster_and_team_assignment", "active_duration_seconds",
            "recording_policy", "recording_harness_sha256",
            "engine_sha256", "rune_sha256", "rune_identity",
        })
        self.assertEqual(
            identity["minimum_duration_seconds_per_round"],
            int(stealstage.WINDOW_SECONDS))
        self.assertGreaterEqual(identity["minimum_arm_swapped_rounds"], 2)
        self.assertGreaterEqual(
            identity["minimum_authoritative_pickups_per_arm"], 5)
        self.assertIs(identity["disposable_game_roots_required"], True)
        self.assertIs(identity["disjoint_ports_required"], True)
        receipts = contract["receipt_schema"]
        self.assertEqual(set(receipts["required_per_arm_fields"]), {
            "metric_version", "source_commit", "source_patch_sha256",
            "module_sha256",
            "engine_sha256", "map_bsp_sha256", "configuration_sha256",
            "roster_and_team_assignment", "port",
            "evaluation_window_server_seconds", "active_duration_seconds",
            "recording_policy", "recording_harness_sha256",
            "server_log_sha256", "stdlog_sha256", "stats_database_sha256",
            "serverrecord_sha256", "serverrecord_frames",
            "serverrecord_level_time_end", "rune_sha256", "rune_identity",
        })
        rune_fields = receipts["required_rune_identity_fields"]
        self.assertEqual(set(rune_fields), {
            "map", *snagrepair.IDENTITY_KEYS, "rune_payload_crc",
        })
        self.assertEqual(len(identity["required_equal_fields"]),
                         len(set(identity["required_equal_fields"])))
        self.assertLessEqual(set(identity["required_equal_fields"]),
                             set(receipts["required_per_arm_fields"]))
        equal_fields = identity["required_equal_fields"]
        first_receipt = {
            field: f"same:{field}"
            for field in receipts["required_per_arm_fields"]
        }
        first_receipt["rune_identity"] = {
            field: f"same:{field}" for field in rune_fields
        }
        second_receipt = dict(first_receipt)
        self.assertEqual(stealstage.receipt_identity_mismatches(
            (first_receipt, second_receipt), equal_fields,
            receipts["required_per_arm_fields"], rune_fields), [])
        for field in equal_fields:
            changed = dict(second_receipt)
            if field == "rune_identity":
                changed[field] = dict(first_receipt[field])
                changed[field][rune_fields[0]] = "different:rune_identity"
            else:
                changed[field] = f"different:{field}"
            with self.subTest(equal_receipt_field=field):
                self.assertEqual(stealstage.receipt_identity_mismatches(
                    (first_receipt, changed), equal_fields,
                    receipts["required_per_arm_fields"], rune_fields), [field])
        missing = dict(second_receipt)
        missing.pop(equal_fields[0])
        with self.assertRaises(ValueError):
            stealstage.receipt_identity_mismatches(
                (first_receipt, missing), equal_fields,
                receipts["required_per_arm_fields"], rune_fields)
        missing_non_identity = dict(second_receipt)
        missing_non_identity.pop("server_log_sha256")
        with self.assertRaises(ValueError):
            stealstage.receipt_identity_mismatches(
                (first_receipt, missing_non_identity), equal_fields,
                receipts["required_per_arm_fields"], rune_fields)
        missing_rune_field = dict(second_receipt)
        missing_rune_field["rune_identity"] = dict(
            second_receipt["rune_identity"])
        missing_rune_field["rune_identity"].pop(rune_fields[0])
        with self.assertRaises(ValueError):
            stealstage.receipt_identity_mismatches(
                (first_receipt, missing_rune_field), equal_fields,
                receipts["required_per_arm_fields"], rune_fields)
        with self.assertRaises(ValueError):
            stealstage.receipt_identity_mismatches(
                (first_receipt, second_receipt),
                equal_fields + [equal_fields[0]],
                receipts["required_per_arm_fields"], rune_fields)
        with self.assertRaises(ValueError):
            stealstage.receipt_identity_mismatches(
                (first_receipt, second_receipt),
                equal_fields + ["not_in_receipt_schema"],
                receipts["required_per_arm_fields"], rune_fields)
        self.assertNotIn("source_commit", identity["required_equal_fields"])
        self.assertNotIn("module_sha256", identity["required_equal_fields"])
        self.assertNotIn("bsp_crc", json.dumps(contract))
        self.assertEqual(contract["stratification"],
                         ["map", "team", "roster_size", "configuration"])
        self.assertIn("F Pickup", contract["event_authority"]["steal"])
        self.assertIn("STATS_OFFENSE_FLAG",
                      contract["event_authority"]["steal"])
        self.assertIn("host outcome log",
                      contract["event_authority"]["capture"])
        self.assertIn("diagnostic only",
                      contract["event_authority"]["demo_capture"])
        calculations = contract["metric_calculation"]
        self.assertEqual(set(calculations), {
            "evaluation_window", "active_bot_minutes", "active_team_minutes",
            "active_defender_minutes", "horizontal_distance",
            "telemetry_time_alignment",
            "moving_sample_fraction", "world_or_hazard_suicides",
            "combat", "visible_or_audible_engagements",
            "forbidden_knowledge_events", "approach",
            "demo_time_alignment", "close_conversion", "steal",
            "capture", "steal_to_capture_conversion",
            "defender_post_dwell_fraction",
            "defender_moving_sample_fraction", "defender_departure",
            "captures_conceded", "strata_and_pooling",
        })
        for formula in calculations.values():
            self.assertIsInstance(formula, str)
            self.assertTrue(formula.strip())
        self.assertIn("film.TELEPORT_UNITS=180",
                      calculations["horizontal_distance"])
        self.assertIn("[t0,t0+600.0)",
                      calculations["evaluation_window"])
        self.assertIn("not a clock",
                      calculations["telemetry_time_alignment"])
        self.assertIn("duplicate",
                      calculations["telemetry_time_alignment"])
        self.assertIn("spd greater than 50",
                      calculations["moving_sample_fraction"])
        self.assertIn("0 <= sgoal < 1500",
                      calculations["defender_post_dwell_fraction"])
        self.assertIn("eng=1",
                      calculations["visible_or_audible_engagements"])
        self.assertIn("current visible target",
                      calculations["visible_or_audible_engagements"])
        self.assertIn("not itself an engagement",
                      calculations["visible_or_audible_engagements"])
        self.assertIn("through 1.5 seconds",
                      calculations["close_conversion"])
        self.assertIn("same-timestamp events execute in ascending production log order",
                      calculations["approach"])
        self.assertIn("level.time > droptime+30",
                      calculations["approach"])
        self.assertIn("remains dropped at droptime+30",
                      calculations["approach"])
        self.assertIn("within 0.2 seconds",
                      calculations["demo_time_alignment"])
        self.assertIn("complete timestamped stream",
                      calculations["steal"])

        expected_band_fields = {
            "movement": {
                "horizontal_distance_per_active_bot_minute_ratio_min",
                "moving_sample_fraction_absolute_delta_min",
                "world_or_hazard_suicides_per_active_bot_minute_delta_max",
            },
            "combat": {
                "combat_kills_per_active_team_minute_ratio_min",
                "combat_kills_per_active_team_minute_ratio_max",
                "combat_deaths_per_active_team_minute_ratio_max",
            },
            "perception": {
                "forbidden_knowledge_events_max",
                "visible_or_audible_engagements_per_active_team_minute_ratio_min",
                "visible_or_audible_engagements_per_active_team_minute_ratio_max",
            },
            "steal": {
                "authoritative_pickups_per_active_team_minute_ratio_min",
                "authoritative_pickups_per_active_team_minute_delta_min",
                "authoritative_pickup_count_delta_min",
            },
            "conversion": {
                "close_approach_conversion_absolute_delta_min",
                "close_approach_conversion_ratio_min",
                "timely_authoritative_pickup_count_delta_min",
            },
            "capture": {
                "authoritative_captures_per_active_team_minute_ratio_min",
                "steal_to_capture_conversion_absolute_delta_min",
                "authoritative_reconciliation_mismatches_max",
            },
            "defense": {
                "defender_post_dwell_fraction_ratio_min",
                "defender_moving_sample_fraction_ratio_min",
                "defender_departures_per_active_defender_minute_ratio_max",
                "captures_conceded_per_active_team_minute_ratio_max",
                "captures_conceded_per_active_team_minute_delta_max",
            },
        }
        self.assertEqual(set(contract["bands"]), set(expected_band_fields))
        for family, fields in expected_band_fields.items():
            self.assertEqual(set(contract["bands"][family]), fields)
            for value in contract["bands"][family].values():
                self.assertIsInstance(value, (int, float))
                self.assertNotIsInstance(value, bool)
                self.assertTrue(math.isfinite(value))
        self.assertIs(contract["decision"]["require_every_band"], True)
        self.assertIn("inconclusive",
                      contract["decision"]["insufficient_event_rule"])

    def test_frame_snapshot_precedes_serial_think_and_owns_live_inputs(self) -> None:
        arach = (ROOT / "slipgate/sg_arach.c").read_text()
        adapter = (ROOT / "slipgate/sg_strike_adapter.c").read_text()
        header = (ROOT / "slipgate/sg_strike_adapter.h").read_text()

        prepare = arach.index("StrikePrepareFrame();")
        serial = arach.index("for (i = 0; i < SG_MAXBOTS; i++)", prepare)
        think = arach.index("SG_BotThink(&sg_bots[i]);", serial)
        self.assertLess(prepare, serial)
        self.assertLess(serial, think)
        self.assertIn("SG_StrikeAdapterBeginFrame", arach)
        self.assertIn("SG_CombatWeaponState", arach)
        self.assertIn("ctfid", arach)
        self.assertIn("SG_AttackFlagDirectTouchAuthority", arach)
        self.assertIn("sg_fields.item[SG_FC_WEAPON]", arach)
        self.assertIn("SG_StrikeMemberNeedsWeapon", arach)
        self.assertIn("SG_StrikeParticipant", arach)
        self.assertIn("!tc.strike_rush && !carrying", arach)
        self.assertIn("strike_team->weapon_deadline[strike_slot] -",
                      arach)
        move = (ROOT / "slipgate/sg_move.c").read_text()
        self.assertIn("(role == SG_ROLE_ATTACK || tc->strike_rush)", move)
        descend = (ROOT / "slipgate/sg_descend.c").read_text()
        self.assertIn("StrikeWeaponPrepareCommit(bot, tc)", descend)
        self.assertIn("(role == SG_ROLE_ATTACK || tc->strike_rush)", descend)
        self.assertIn("SG_NadeTargetClear(bot)", descend)
        self.assertIn("if (tc->strike_rush)", descend)
        self.assertIn("StrikeWeaponPurposeReconcile(bot, tc)", descend)
        self.assertIn("bot->strike_weapon_link = bestlink", descend)
        self.assertIn("bot->strike_weapon_until = tc->strike_weapon_deadline",
                      descend)
        self.assertIn("SG_StrikeWeaponRouteVerdict", descend)
        self.assertIn("StrikeRailLateOverrideAllowed(bot, tc)", descend)
        self.assertIn("StrikeRailWatchdogAllowed(bot, tc)", descend)
        self.assertIn("StrikeRailMoveAllowed(tc)", move)
        self.assertIn("memcpy(next_frame, frames", adapter)
        self.assertIn("SG_StrikeStep(&next_team[team_index]", adapter)
        self.assertIn("exactly once", header)

    def test_strike_telemetry_is_debug_gated_and_edge_only(self) -> None:
        source = (ROOT / "slipgate/sg_arach.c").read_text()
        start = source.index("static void StrikeTelemetryEdge(int team_index)")
        end = source.index("static void StrikeFrameInit", start)
        telemetry = source[start:end]
        self.assertIn("STRIKE_EDGE team=%d epoch=%u phase=%s", telemetry)
        for field in (
            "members=0x%08x", "hold=0x%08x", "rush=0x%08x", "carrier=%d",
        ):
            self.assertIn(field, telemetry)
        self.assertIn("if (edge && sg_cv.debug && sg_cv.debug->value)",
                      telemetry)
        self.assertIn(
            "team->epoch != sg_strike_telemetry_epoch[team_index]", telemetry)
        self.assertIn(
            "team->phase != sg_strike_telemetry_phase[team_index]", telemetry)
        self.assertEqual(telemetry.count("sg_host.dprint("), 1)
        begin = source.index("sg_strike_frame_ready = SG_StrikeAdapterBeginFrame")
        edge_call = source.index("StrikeTelemetryEdge(team_index)", begin)
        runframe = source.index("void SG_RunFrame(void)")
        serial = source.index("SG_BotThink(&sg_bots[i]);", runframe)
        self.assertLess(begin, edge_call)
        self.assertLess(edge_call, serial)

    def test_strike_bypasses_legacy_periodic_rally_for_members(self) -> None:
        source = (ROOT / "slipgate/sg_arach.c").read_text()
        call = source.index(
            "if (!StrikeApplyRallyPolicy(bot, &tc, &rally_hold))")
        legacy = source.index("Think_ApproachBand(bot, &tc)", call)
        self.assertLess(call, legacy)

    def test_active_strike_retires_unbound_rail_before_route_selection(self) -> None:
        arach = (ROOT / "slipgate/sg_arach.c").read_text()
        marker = arach.index("Generic proof-line retry has no strike")
        clear = arach.index("StrikeRetireGenericRail(bot, &tc)", marker)
        pick = arach.index("bestlink = Think_PickLink(bot, &tc)", clear)
        self.assertLess(clear, pick)

    def test_all_production_dialects_list_both_sources(self) -> None:
        gnu = (ROOT / "GNUmakefile").read_text()
        make = (ROOT / "Makefile").read_text()
        vcx = (ROOT / "gravity.vcxproj").read_text()
        filters = (ROOT / "gravity.vcxproj.filters").read_text()
        for text in (gnu, make):
            self.assertIn("slipgate/sg_strike.o", text)
            self.assertIn("slipgate/sg_strike_adapter.o", text)
        for text in (vcx, filters):
            self.assertIn("slipgate\\sg_strike.c", text)
            self.assertIn("slipgate\\sg_strike_adapter.c", text)
            self.assertIn("slipgate\\sg_strike.h", text)
            self.assertIn("slipgate\\sg_strike_adapter.h", text)

    def test_level_and_slot_lifecycle_reset_strike_identity(self) -> None:
        arach = (ROOT / "slipgate/sg_arach.c").read_text()
        client = (ROOT / "slipgate/sg_client.c").read_text()
        adapter = (ROOT / "slipgate/sg_strike_adapter.c").read_text()
        self.assertIn("SG_StrikeAdapterReset(&sg_strike_adapter)", arach)
        self.assertIn("SG_StrikeSlotReset(slot)", client)
        self.assertIn("SG_StrikeAdapterForgetSlot", adapter)
        self.assertIn("member_life[slot] = 0u", adapter)
        self.assertIn("bot->strike_weapon_link = -1", client)
        self.assertIn("bot->strike_weapon_link = -1", arach)

    def test_weapon_door_retirement_uses_guard_release_and_sticky_pause(self) -> None:
        descend = (ROOT / "slipgate/sg_descend.c").read_text()
        arach = (ROOT / "slipgate/sg_arach.c").read_text()
        start = descend.index("static qboolean StrikeWeaponDoorLeaseHeld")
        end = descend.index("static qboolean StrikeWeaponPurposeReconcile",
                            start)
        boundary = descend[start:end]
        self.assertIn("SG_DeclaredDoorGuardReleaseProvedClear(bot)", boundary)
        self.assertIn("result == SG_COMPOUND_GUARD_OK", boundary)
        self.assertIn("SG_DeclaredDoorGuardHoldOpen(bot, 500)", boundary)
        self.assertIn("SG_StrikeWeaponDoorRetirement", boundary)
        self.assertIn("SG_DeclaredDoorGuardPause(bot)", boundary)
        self.assertIn("bot->strike_weapon_draining = true", boundary)
        self.assertIn("tc->think_over = true", boundary)
        restore = arach.index("static qboolean Bot_DeclaredDoorGuardRestore")
        think = arach.index("void SG_BotThink", restore)
        restore_body = arach[restore:think]
        self.assertIn("if (bot->strike_weapon_draining)", restore_body)
        self.assertIn("SG_DeclaredDoorGuardPause(bot)", restore_body)


if __name__ == "__main__":
    unittest.main()
