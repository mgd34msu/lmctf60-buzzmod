#!/usr/bin/env python3
"""Executable and production-wiring checks for the strike frame adapter."""

from pathlib import Path
import hashlib
import json
import math
import os
import sqlite3
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
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

    def test_public_death_does_not_publish_hidden_origin(self) -> None:
        caco = (ROOT / "slipgate/sg_caco.c").read_text()
        goal = (ROOT / "slipgate/sg_goal.c").read_text()
        arach = (ROOT / "slipgate/sg_arach.c").read_text()
        self.assertIn("SG_DeathBeliefSeed", caco)
        self.assertNotIn("VectorCopy(victim->s.origin, sg_caco_death", caco)
        self.assertIn("SG_EnemyRoomDeathKnown", goal)
        self.assertIn("SG_EnemyRoomDeathKnown", arach)

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
        decimal_boundary = [
            {"name": "thief", "team": "red", "time": 0.7}]
        self.assertEqual(len(stealstage.match_close_pickups(
            decimal_boundary, [{"name": "thief", "kind": "F Pickup",
                                "time": 2.2}])), 1)
        self.assertEqual(stealstage.match_close_pickups(
            decimal_boundary, [{"name": "thief", "kind": "F Pickup",
                                "time": 2.200001}]), [])
        duplicate = approaches + [
            {"name": "thief", "team": "red", "time": 20.2}]
        matches = stealstage.match_close_pickups(duplicate, [
            {"name": "thief", "kind": "F Pickup", "time": 20.3},
            {"name": "thief", "kind": "F Pickup", "time": 20.4},
        ])
        self.assertEqual([match["approach"]["time"] for match in matches],
                         [20.0, 20.2])

        # Nearest/latest matching loses one feasible pair here.  The frozen
        # earliest-feasible greedy is maximum-cardinality.
        cardinality = stealstage.match_close_pickups([
            {"name": "thief", "team": "red", "time": 0.0},
            {"name": "thief", "team": "red", "time": 1.0},
        ], [
            {"name": "thief", "kind": "F Pickup", "time": 1.2},
            {"name": "thief", "kind": "F Pickup", "time": 2.4},
        ])
        self.assertEqual(len(cardinality), 2)
        self.assertEqual([item["approach"]["time"] for item in cardinality],
                         [0.0, 1.0])

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
        duty = arach.index("strike_duty = strike_team->duty[strike_slot]")
        lead_abort = arach.index(
            'Lead_Abort(bot, "strike duty")', duty)
        objective = arach.index("Think_Objective(bot, &tc)", lead_abort)
        route = arach.index("StrikeApplyDutyRoute(&tc, strike_duty, team)",
                            objective)
        self.assertLess(duty, lead_abort)
        self.assertLess(lead_abort, objective)
        self.assertLess(lead_abort, route)
        goal = (ROOT / "slipgate/sg_goal.c").read_text()
        lead = goal.index("const int *lead = Lead_Field")
        self.assertIn("if (!tc->strike_blocks_optional)",
                      goal[lead - 100:lead])
        self.assertIn(
            "tc->mega = tc->strike_blocks_optional ? 0.0f", goal)
        tactics = goal.index("sg_cv.tactics->value")
        self.assertIn("!tc->strike_blocks_optional", goal[tactics - 80:tactics])
        self.assertIn("tc.strike_rush, carrying", arach)
        self.assertIn("strike_team->weapon_deadline[strike_slot] -",
                      arach)
        move = (ROOT / "slipgate/sg_move.c").read_text()
        self.assertIn("tc->strike_pressure", move)
        descend = (ROOT / "slipgate/sg_descend.c").read_text()
        self.assertIn("StrikeWeaponPrepareCommit(bot, tc)", descend)
        self.assertIn("qboolean enemy_pressure = tc->strike_pressure", descend)
        self.assertGreaterEqual(descend.count("enemy_pressure"), 8)
        self.assertIn("SG_AttackDescentFallbackAllowed(enemy_pressure,",
                      descend)
        axis_start = descend.index("SPREAD THE AXES")
        axis_end = descend.index("else if (role == SG_ROLE_CARRY)",
                                 axis_start)
        axis = descend[axis_start:axis_end]
        self.assertIn("SG_StrikeEnemyPressureSnapshot(mb6)", axis)
        self.assertNotIn("mb6->last_role", axis)
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
        snapshot = arach.index("sg_strike_enemy_pressure_cache[i] =",
                               arach.index("SG_StrikeAdapterBeginFrame"))
        serial = arach.index("SG_BotThink(&sg_bots[i]);", snapshot)
        self.assertLess(snapshot, serial)
        pressure = arach[arach.index(
            "qboolean SG_StrikeEnemyPressureSnapshot"):]
        self.assertIn("sg_strike_enemy_pressure_cache[slot]", pressure)
        reset = arach[arach.index("void SG_StrikeSlotReset"):]
        self.assertIn("sg_strike_enemy_pressure_cache[slot] = false", reset)
        move = (ROOT / "slipgate/sg_move.c").read_text()
        self.assertIn("SG_StrikeEnemyPressureSnapshot(bot)", move)
        self.assertIn("tc.strike_pressure = SG_StrikeEnemyPressureActive(",
                      arach)
        self.assertIn("tc.combat_pursuit = SG_StrikeCombatPursuitActive(",
                      arach)
        self.assertIn("tc.rearguard = SG_StrikeRearguardActive(", arach)
        self.assertIn("tc.escort_mission = SG_StrikeEscortActive(", arach)
        self.assertIn("tc->combat_pursuit ||", descend)
        self.assertIn("!tc->strike_active &&", descend)
        self.assertIn("if (tc->rearguard &&", descend)
        self.assertIn("tc->strike_pressure ? 1500", descend)
        self.assertEqual(descend.count("!ThinkMissionHold(bot, tc, goal_field)"),
                         2)
        goal = (ROOT / "slipgate/sg_goal.c").read_text()
        rally = goal[goal.index("THE RALLY."):goal.index("rally_done:")]
        self.assertIn("SG_StrikeEnemyPressureSnapshot(mb)", rally)
        self.assertNotIn("mb->last_role != (int)SG_ROLE_ATTACK", rally)

    def test_effective_escort_mission_controls_carrier_spacing(self) -> None:
        descend = (ROOT / "slipgate/sg_descend.c").read_text()
        start = descend.index("ANTI-LINGER (sg_unlinger")
        end = descend.index("for (li = SG_Rune()->first_link", start)
        anti_linger = descend[start:end]
        self.assertIn(
            "SG_AntiLingerEligible(role, tc->escort_mission)", anti_linger)
        gate = anti_linger[:anti_linger.index("static gitem_t *lg_flag")]
        self.assertNotIn("role != SG_ROLE_ESCORT", gate)

    def test_effective_escort_mission_controls_support_pull(self) -> None:
        price = (ROOT / "slipgate/sg_price.c").read_text()
        start = price.index("LONE WOLF (sg_lonewolf)")
        end = price.index("v += csup *", start)
        support = price[start:end]
        self.assertIn(
            "!SG_EscortSupportFullStrength(tc->escort_mission)", support)
        self.assertNotIn("tc->role != SG_ROLE_ESCORT", support)

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
        first = source.index("Think_ApproachBand(bot, &tc)", call)
        second = source.index("Think_ApproachBand(bot, &tc)", first + 1)
        self.assertLess(call, first)
        self.assertLess(first, second)
        goal = (ROOT / "slipgate/sg_goal.c").read_text()
        approach = goal[goal.index("qboolean Think_ApproachBand"):
                        goal.index("THE INTERCEPT SURFACE")]
        self.assertIn("!tc->strike_active &&", approach)
        self.assertIn("SG_StrikePrebreachApproachAllowed(", approach)

    def test_attack_direct_touch_uses_bounded_terminal_throttle(self) -> None:
        move = (ROOT / "slipgate/sg_move.c").read_text()
        terminal = move[move.index("if (!have_aim && tc->strike_pressure"):
                        move.index("if (!have_aim && bestlink >= 0)")]
        self.assertIn("attack_flag_terminal = true;", terminal)
        self.assertIn("SG_FlagTouchBrake(bot, e, terminal_flag->s.origin, true)",
                      terminal)
        self.assertIn("SG_AttackFlagTerminalAim(e, team, aim, &terminal_flag)",
                      terminal)
        self.assertLess(terminal.index("SG_AttackFlagTerminalAim"),
                        terminal.index("SG_FlagTouchBrake"))
        helper = move[move.index("static void SG_FlagTouchBrake"):
                      move.index("void SG_NadeTargetClear")]
        self.assertIn("SG_StrikeFlagTouchThrottle(", helper)
        self.assertIn("DotProduct(velocity, delta)", helper)

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


class StageAMeasurementAuthorityTest(unittest.TestCase):
    @staticmethod
    def _contract_bytes() -> bytes:
        return (ROOT / "tools/steal-stage-a-contract.json").read_bytes()

    @staticmethod
    def _source_commit() -> str:
        return subprocess.run(
            ("git", "rev-parse", "HEAD"), cwd=ROOT, check=True,
            stdout=subprocess.PIPE, text=True).stdout.strip()

    @classmethod
    def _identities(cls):
        commit = cls._source_commit()
        return {
            "baseline": {
                         "source_commit": commit,
                         "source_patch_sha256":
                             stealstage.EMPTY_PATCH_SHA256,
                         "source_tree_sha256": "c" * 64,
                         "module_sha256": "a" * 64},
            "candidate": {"source_commit": commit,
                          "source_patch_sha256": "1" * 64,
                          "source_tree_sha256": "d" * 64,
                          "module_sha256": "b" * 64},
        }

    @classmethod
    def _treatments(cls):
        result = {}
        for arm, identity in cls._identities().items():
            artifacts = {
                name: {"path": f"authority/{name}",
                       "sha256": hashlib.sha256(name.encode()).hexdigest()}
                for name in stealstage.SOURCE_ARTIFACTS
            }
            artifacts["source_patch"]["sha256"] = identity[
                "source_patch_sha256"]
            artifacts["source_manifest"]["sha256"] = identity[
                "source_tree_sha256"]
            result[arm] = {
                **identity, "source_root": f"/tmp/stage-a-source-{arm}",
                "source_artifacts": artifacts,
            }
        return result

    @staticmethod
    def _assignment(swapped=False):
        return {f"bot{index}":
                ("blue" if (index < 5) == swapped else "red")
                for index in range(10)}

    @classmethod
    def _round(cls, arm, round_number, port, root):
        assignment = cls._assignment(swapped=round_number == 2)
        artifacts = {
            name: {"path": f"evidence/{name}", "sha256": "2" * 64}
            for name in stealstage.REQUIRED_ARTIFACTS
        }
        source = cls._identities()[arm]
        artifacts["module"]["sha256"] = source["module_sha256"]
        rune_identity = {field: 0 for field in stealstage.RUNE_IDENTITY_FIELDS}
        rune_identity["map"] = "lmctf22"
        return {
            "name": f"r{round_number}-{arm}",
            "metric_version": stealstage.METRIC_VERSION,
            "arm": arm, "round": round_number, "treatment": arm,
            "source_identity": source, "root": root, "port": port,
            "map": "lmctf22", "roster_and_team_assignment": assignment,
            "evaluation_window_server_seconds": {"start": 10.0,
                                                   "end": 610.0},
            "active_duration_seconds": {"per_bot": 600.0,
                                        "red_team": 600.0,
                                        "blue_team": 600.0},
            "recording_policy":
                "serverrecord before roster joins through removal after the full-roster allowance; evaluate the exact server-time crop",
            "artifacts": artifacts,
            "configuration_artifacts": [
                {"path": "config/stage-a.cfg", "sha256": "3" * 64}],
            "configuration_sha256": "4" * 64,
            "rune_identity": rune_identity,
        }

    @classmethod
    def _manifest(cls, tool=None):
        if tool is None:
            tool = Path(stealstage.__file__).read_bytes()
        contract = cls._contract_bytes()
        _implementation, implementation_digest = \
            stealstage.measurement_implementation()
        rounds = []
        for index, (round_number, arm) in enumerate((
                (1, "baseline"), (1, "candidate"),
                (2, "baseline"), (2, "candidate"))):
            rounds.append(cls._round(
                arm, round_number, 47000 + index,
                f"/tmp/stage-a-r{round_number}-{arm}"))
        return {
            "format": stealstage.RECEIPT_FORMAT,
            "metric_version": stealstage.METRIC_VERSION,
            "metric_contract_sha256": hashlib.sha256(contract).hexdigest(),
            "measurement_tool_sha256": hashlib.sha256(tool).hexdigest(),
            "measurement_implementation_sha256": implementation_digest,
            "source_parent_commit": cls._source_commit(),
            "treatments": cls._treatments(), "rounds": rounds,
        }

    @staticmethod
    def _fixture_bsp() -> bytes:
        import mapflags
        entities = (
            {"classname": "worldspawn"},
            {"classname": "info_flag_red", "origin": "0 0 0"},
            {"classname": "info_flag_blue", "origin": "100 0 0"},
        )
        text = "".join("{\n" + "".join(
            f'"{key}" "{value}"\n' for key, value in entity.items()) +
            "}\n" for entity in entities).encode("latin-1") + b"\0"
        header = bytearray(mapflags.BSP_HEADER_SIZE)
        struct.pack_into("<4si", header, 0, b"IBSP", 38)
        struct.pack_into("<ii", header, 8,
                         mapflags.BSP_HEADER_SIZE, len(text))
        return bytes(header) + text

    @staticmethod
    def _fixture_rune() -> bytes:
        import runeio
        import rune_contracts_generated as contract
        seed = runeio.SEED_STRUCT.pack(
            0.0, 0.0, 0.0, 0, runeio.RSF_TOMBSTONE)
        payload = seed + b"\0"
        map_name = b"lmctf22" + b"\0" * (runeio.MAP_NAME_BYTES - 7)
        prefix = runeio.HEADER_STRUCT.pack(
            runeio.RUNE_MAGIC, 0, runeio.RUNE_HEADER_BYTES,
            runeio.RUNE_SEED_BYTES, runeio.RUNE_LINK_BYTES,
            1, 0, zlib.crc32(payload) & 0xffffffff,
            1, 2, contract.RUNE_ACTION_CONTRACT_CRC32,
            contract.RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED,
            800.0, 0.0, 2000.0,
            contract.RUNE_PROOF_PMOVE_SUBSTEP_MS,
            contract.RUNE_PROOF_SERVER_FRAME_MS,
            1, 0, map_name)
        extension = runeio.RUNE_HEADER_EXTENSION_STRUCT.pack(
            runeio.RUNE_ACTIVATION_NODE_BYTES,
            runeio.RUNE_ACTIVATION_EDGE_BYTES,
            runeio.RUNE_ACTIVATION_PLAN_BYTES,
            0, 0, 0, 0, 1,
            contract.RUNE_MECHANISM_CONTRACT_CRC32, 0)
        header = bytearray(prefix + extension)
        struct.pack_into("<I", header, runeio.RUNE_HEADER_CRC_OFFSET,
                         runeio._rune_header_crc(header))
        result = bytes(header) + payload
        runeio.decode(result)
        return result

    @staticmethod
    def _fixture_serverrecord() -> bytes:
        setup = (bytes([12]) + b"\0" * 9 + b"game\0" +
                 struct.pack("<H", 0xffff) + b"level\0" +
                 bytes([13]) + struct.pack("<H", 33) +
                 b"maps/lmctf22.bsp\0")
        setup += b"".join(
            bytes([13]) + struct.pack("<H", 1312 + index) +
            f"bot{index}\\male/grunt".encode() + b"\0"
            for index in range(10))
        messages = [setup]
        for frame in range(1, 6101):
            active = range(1, min(10, frame // 10) + 1)
            entities = b"".join(bytes((0, number)) for number in active)
            messages.append(bytes([20]) + struct.pack("<i", frame) +
                            bytes([18]) + entities + b"\0\0")
        return b"".join(struct.pack("<i", len(message)) + message
                        for message in messages)

    @classmethod
    def _fixture_stdlog(cls, assignment) -> bytes:
        lines = ["\t\tStdLog\t1.2", "\t\tGameStart\t\t\t0.0"]
        for index in range(10):
            name = f"bot{index}"
            moment = float(index + 1)
            lines.extend((
                f"\t\tPlayerConnect\t{name}\t\t{moment:.1f}",
                f"\t\tPlayerTeamChange\t{name}\t{assignment[name]}\t"
                f"{moment:.1f}",
            ))
        lines.append("bot0\tbot1\tKill\tRailgun\t0\t609.0")
        lines.extend(f"\t\tPlayerLeft\tbot{index}\t\t610.0"
                     for index in range(10))
        return ("\n".join(lines) + "\n").encode()

    @staticmethod
    def _fixture_server_log() -> bytes:
        lines = [f"bot{index} entered the game" for index in range(10)]
        lines.extend(
            f"SGCENSUS bot{index}: frm={frame} alive=1"
            for frame in range(100, 6120, 10) for index in range(10))
        lines.extend(
            f"SG bot{index}: role=1 seed=0 goal=0 sgoal=0 spd=100 "
            f"org=(0 0 0) link=-1 act=-1 hp=0 dh=0 dl=0 "
            f"st=0.0 gnd=1 eng=1 frm={110 + index}"
            for index in range(10))
        lines.append("bot1 was railed by bot0")
        return ("\n".join(lines) + "\n").encode()

    @staticmethod
    def _fixture_stats(path: Path) -> bytes:
        connection = sqlite3.connect(path)
        connection.executescript("""
            CREATE TABLE userdata (char_idx INTEGER, playername TEXT);
            CREATE TABLE game_stats (
                char_idx INTEGER, frags INTEGER, fragged INTEGER,
                deaths INTEGER, suicides INTEGER, shots INTEGER,
                shots_hit INTEGER);
            CREATE TABLE ctf_stats (
                char_idx INTEGER, flag_pickups INTEGER,
                flag_captures INTEGER, flag_returns INTEGER);
        """)
        for index in range(10):
            connection.execute("INSERT INTO userdata VALUES (?, ?)",
                               (index, f"bot{index}"))
            connection.execute(
                "INSERT INTO game_stats VALUES (?,?,?,?,?,?,?)",
                (index, int(index == 0), int(index == 1), int(index == 1),
                 0, 0, 0))
            connection.execute("INSERT INTO ctf_stats VALUES (?,0,0,0)",
                               (index,))
        connection.commit()
        connection.close()
        return path.read_bytes()

    @classmethod
    def _positive_cli_fixture(cls, temporary: Path):
        parent = cls._source_commit()
        candidate_checkout = temporary / "candidate-checkout"
        subprocess.run(("git", "clone", "-q", "--no-hardlinks",
                        "--no-checkout", ROOT, candidate_checkout), check=True)
        subprocess.run(("git", "checkout", "-q", "--detach", parent),
                       cwd=candidate_checkout, check=True)
        candidate_source = candidate_checkout / "g_main.c"
        candidate_source.write_text(
            candidate_source.read_text() +
            '\nconst char stage_a_fixture_candidate_identity[] = '
            '"stage-a-candidate";\n')
        patches = {"baseline": b"", "candidate":
                   stealstage._source_patch_payload(candidate_checkout)}
        tool_payload = Path(stealstage.__file__).read_bytes()
        _implementation, implementation_digest = \
            stealstage.measurement_implementation()
        treatments = {}
        modules = {}
        for arm in ("baseline", "candidate"):
            with stealstage._reconstructed_source(
                    ROOT, parent, patches[arm]) as reconstructed:
                source_manifest = stealstage._source_tree_manifest(reconstructed)
                probe = stealstage._run_knowledge_probe(reconstructed)
                module, revision_header, build_inputs = \
                    stealstage._rebuild_module(reconstructed)
            modules[arm] = module
            identity = {
                "source_commit": parent,
                "source_patch_sha256": hashlib.sha256(
                    patches[arm]).hexdigest(),
                "source_tree_sha256": hashlib.sha256(
                    source_manifest).hexdigest(),
                "module_sha256": hashlib.sha256(module).hexdigest(),
            }
            build_receipt = {
                "format": stealstage.BUILD_RECEIPT_FORMAT,
                "metric_version": stealstage.METRIC_VERSION,
                "recipe": stealstage.SOURCE_BUILD_RECIPE,
                **identity, "build_input_sha256": build_inputs,
                "revision_header_sha256": hashlib.sha256(
                    revision_header).hexdigest(),
            }
            authority_payloads = {
                "source_patch": patches[arm],
                "source_manifest": source_manifest,
                "build_receipt": stealstage.canonical_json(build_receipt),
                "knowledge_report": stealstage.canonical_json(
                    stealstage._knowledge_report(
                        identity, hashlib.sha256(tool_payload).hexdigest(),
                        implementation_digest, probe)),
            }
            source_root = temporary / f"source-{arm}"
            (source_root / "authority").mkdir(parents=True)
            records = {}
            for name, payload in authority_payloads.items():
                relative = f"authority/{name}"
                (source_root / relative).write_bytes(payload)
                records[name] = {"path": relative,
                                 "sha256": hashlib.sha256(payload).hexdigest()}
            treatments[arm] = {
                **identity, "source_root": str(source_root),
                "source_artifacts": records,
            }

        rune = cls._fixture_rune()
        import runeio
        rune_identity = stealstage._rune_identity(runeio.decode(rune))
        shared = {
            "engine": b"engine fixture\n",
            "rune": rune,
            "bsp": cls._fixture_bsp(),
            "recording_harness": b"serverrecord fixture harness\n",
            "server_log": cls._fixture_server_log(),
            "serverrecord": cls._fixture_serverrecord(),
        }
        stats = cls._fixture_stats(temporary / "fixture-stats.db")
        config = b"set stage_a_fixture 1\n"
        rounds = []
        tamper_path = None
        for index, (round_number, arm) in enumerate((
                (1, "baseline"), (1, "candidate"),
                (2, "baseline"), (2, "candidate"))):
            assignment = cls._assignment(swapped=round_number == 2)
            payloads = {**shared, "module": modules[arm],
                        "stdlog": cls._fixture_stdlog(assignment),
                        "stats_database": stats}
            root = temporary / f"round-{round_number}-{arm}"
            (root / "evidence").mkdir(parents=True)
            (root / "config").mkdir()
            artifacts = {}
            for name in stealstage.REQUIRED_ARTIFACTS:
                relative = f"evidence/{name}"
                path = root / relative
                path.write_bytes(payloads[name])
                artifacts[name] = {
                    "path": relative,
                    "sha256": hashlib.sha256(payloads[name]).hexdigest()}
                if index == 0 and name == "server_log":
                    tamper_path = path
            config_path = "config/stage-a.cfg"
            (root / config_path).write_bytes(config)
            rounds.append({
                "name": f"r{round_number}-{arm}",
                "metric_version": stealstage.METRIC_VERSION,
                "arm": arm, "round": round_number, "treatment": arm,
                "source_identity": {
                    field: treatments[arm][field] for field in (
                        "source_commit", "source_patch_sha256",
                        "source_tree_sha256", "module_sha256")},
                "root": str(root), "port": 48000 + index,
                "map": "lmctf22",
                "roster_and_team_assignment": assignment,
                "evaluation_window_server_seconds": {
                    "start": 10.0, "end": 610.0},
                "active_duration_seconds": {
                    "per_bot": 600.0, "red_team": 600.0,
                    "blue_team": 600.0},
                "recording_policy":
                    "serverrecord before roster joins through removal after "
                    "the full-roster allowance; evaluate the exact server-time crop",
                "artifacts": artifacts,
                "configuration_artifacts": [{
                    "path": config_path,
                    "sha256": hashlib.sha256(config).hexdigest()}],
                "configuration_sha256": stealstage._configuration_digest(
                    [(config_path, config)]),
                "rune_identity": rune_identity,
            })
        contract_payload = cls._contract_bytes()
        manifest = {
            "format": stealstage.RECEIPT_FORMAT,
            "metric_version": stealstage.METRIC_VERSION,
            "metric_contract_sha256": hashlib.sha256(
                contract_payload).hexdigest(),
            "measurement_tool_sha256": hashlib.sha256(
                tool_payload).hexdigest(),
            "measurement_implementation_sha256": implementation_digest,
            "source_parent_commit": parent,
            "treatments": treatments, "rounds": rounds,
        }
        contract_path = temporary / "contract.json"
        manifest_path = temporary / "manifest.json"
        contract_path.write_bytes(contract_payload)
        manifest_path.write_bytes(stealstage.canonical_json(manifest))
        return contract_path, manifest_path, tamper_path

    @staticmethod
    def _stdlog_bytes(rename=False, drift=False, early_leave=False):
        lines = ["\t\tStdLog\t1.2", "\t\tGameStart\t\t\t0.0"]
        for index in range(10):
            name, moment = f"bot{index}", float(index + 1)
            team = "red" if index < 5 else "blue"
            lines.extend((
                f"\t\tPlayerConnect\t{name}\t\t{moment:.1f}",
                f"\t\tPlayerTeamChange\t{name}\t{team}\t{moment:.1f}",
            ))
        if rename:
            lines.append("\t\tPlayerRename\tbot0\tother\t10.1")
        if drift:
            lines.append("\t\tPlayerTeamChange\tbot0\tblue\t10.2")
        lines.extend((
            "bot0\t\tF Pickup\t\t0\t10.0",
            "bot0\t\tF Capture\t\t5\t609.0",
            "bot0\t\tF Pickup\t\t0\t610.0",
        ))
        for index in range(10):
            leave = 609.9 if early_leave and index == 0 else 610.0
            lines.append(f"\t\tPlayerLeft\tbot{index}\t\t{leave:.1f}")
        return ("\n".join(lines) + "\n").encode()

    @classmethod
    def _result_fixture(cls):
        identities = cls._identities()
        implementation, implementation_digest = \
            stealstage.measurement_implementation()
        rounds = []
        for index, (round_number, arm) in enumerate((
                (1, "baseline"), (1, "candidate"),
                (2, "baseline"), (2, "candidate"))):
            source = identities[arm]
            team_demo = {
                color: {"distance": 100.0, "approaches": 1,
                        "observed_stand_seconds": 300.0, "timely": 1}
                for color in ("red", "blue")}
            team_telemetry = {
                color: {"samples": 10, "moving": 5, "engaged": 2,
                        "defenders": 5, "defender_dwell": 4,
                        "defender_moving": 3, "departures": 1}
                for color in ("red", "blue")}
            team_counts = {
                color: {"suicides": 0, "frags": 2, "fragged": 2,
                        "pickups": 3, "captures": 1}
                for color in ("red", "blue")}
            total_counts = {field: sum(team_counts[color][field]
                                       for color in ("red", "blue"))
                            for field in team_counts["red"]}
            rune_identity = {field: 0
                             for field in stealstage.RUNE_IDENTITY_FIELDS}
            rune_identity["map"] = "lmctf22"
            artifacts = {name: hashlib.sha256(name.encode()).hexdigest()
                         for name in stealstage.REQUIRED_ARTIFACTS}
            artifacts["module"] = source["module_sha256"]
            rounds.append({
                "name": f"r{round_number}-{arm}", "arm": arm,
                "round": round_number, "port": 47000 + index,
                "root_identity": [1, index + 10], "map": "lmctf22",
                "source_identity": source, "artifact_sha256": artifacts,
                "roster_and_team_assignment": cls._assignment(
                    swapped=round_number == 2),
                "configuration_sha256": "4" * 64,
                "rune_identity": rune_identity,
                "stand_origins": {"red": [0.0, 0.0, 0.0],
                                  "blue": [100.0, 0.0, 0.0]},
                "evaluation_window_server_seconds": {
                    "start": 10.0, "end": 610.0},
                "window_counts": {"total": total_counts,
                                  "by_team": team_counts},
                "telemetry": {
                    **{field: sum(team_telemetry[color][field]
                                  for color in ("red", "blue"))
                       for field in team_telemetry["red"]},
                    "by_team": team_telemetry,
                    "brackets": {"cutoff_gap_seconds": 1.0}},
                "database_full_stream": {},
                "demo": {
                    "distance": 200.0, "approaches": 2,
                    "observed_stand_seconds": 600.0, "timely": 2,
                    "by_team": team_demo},
                "authority": {"forbidden_knowledge_events": 0,
                              "reconciliation_mismatches": 0},
            })
        baseline = stealstage.aggregate_rounds(rounds, "baseline")
        candidate = stealstage.aggregate_rounds(rounds, "candidate")
        contract = stealstage.load_contract(cls._contract_bytes())
        gate = stealstage.evaluate_bands(baseline, candidate, contract)
        return {
            "format": stealstage.RESULT_FORMAT,
            "metric_version": stealstage.METRIC_VERSION,
            "metric_contract_sha256": stealstage.METRIC_CONTRACT_SHA256,
            "measurement_tool_sha256": hashlib.sha256(
                Path(stealstage.__file__).read_bytes()).hexdigest(),
            "measurement_implementation_sha256": implementation_digest,
            "measurement_implementation": implementation,
            "manifest_sha256": "2" * 64,
            "source_parent_commit": cls._source_commit(),
            "treatment_authority": {
                arm: {
                    "source_identity": identities[arm],
                    "source_root_identity": [2, index + 20],
                    "source_artifact_sha256": {
                        name: (identities[arm]["source_patch_sha256"]
                               if name == "source_patch" else
                               identities[arm]["source_tree_sha256"]
                               if name == "source_manifest" else
                               hashlib.sha256(
                                   f"{arm}-{name}".encode()).hexdigest())
                        for name in stealstage.SOURCE_ARTIFACTS},
                    "build_input_sha256": {
                        name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                        for name in stealstage.SOURCE_BUILD_INPUTS},
                    "policy_probe": {
                        "tests_run": 9, "failures": 0, "errors": 0,
                        "skipped": 0, "successful": True},
                }
                for index, arm in enumerate(("baseline", "candidate"))},
            "round_metrics": rounds,
            "aggregate": {"baseline": baseline, "candidate": candidate},
            "report": {"valid_receipts": True, "round_count": 4,
                       "sufficient_events": gate["sufficient_events"],
                       "all_bands_pass": gate["all_bands_pass"],
                       "decision": gate["decision"],
                       "checks": gate["checks"]},
        }

    def test_contract_is_typed_exact_and_not_caller_weakenable(self) -> None:
        payload = self._contract_bytes()
        contract = stealstage.load_contract(payload)
        self.assertEqual(contract["receipt_schema"]["required_artifacts"],
                         list(stealstage.REQUIRED_ARTIFACTS))
        self.assertEqual(contract["receipt_schema"]["rune_identity_fields"],
                         list(stealstage.RUNE_IDENTITY_FIELDS))
        self.assertEqual(contract["trial_design"], {
            "arms": ["baseline", "candidate"], "rounds": [1, 2],
            "treatments": ["baseline", "candidate"],
            "roster_size": 10, "team_size": 5})
        self.assertIn("approaches_per_observed_stand_minute_ratio_min",
                      contract["bands"]["steal"])
        weakened = json.loads(payload)
        weakened["receipt_schema"]["required_artifacts"].remove("serverrecord")
        with self.assertRaisesRegex(ValueError, "executable schema"):
            stealstage.load_contract(json.dumps(weakened).encode())
        weakened = json.loads(payload)
        weakened["bands"]["steal"][
            "authoritative_pickup_count_delta_min"] = -100
        with self.assertRaisesRegex(ValueError, "checked-in frozen authority"):
            stealstage.load_contract(json.dumps(weakened).encode())
        self.assertNotEqual(
            stealstage._configuration_digest([
                ("a", b"x"), ("b", b"y")]),
            stealstage._configuration_digest([
                ("a", b"xb\0y")]))
        duplicate = b'{"metric_version":"x","metric_version":"y"}'
        with self.assertRaisesRegex(ValueError, "duplicate key"):
            stealstage._strict_json(duplicate, "fixture")
        with tempfile.TemporaryDirectory(prefix="steal-implementation-") as tmp:
            copied = Path(tmp).resolve()
            for relative in stealstage.MEASUREMENT_IMPLEMENTATION_PATHS:
                target = copied / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / relative).read_bytes())
            manifest_before, digest_before = \
                stealstage.measurement_implementation(copied)
            helper = copied / "tools/runeio.py"
            helper.write_bytes(helper.read_bytes() + b"\n# drift\n")
            manifest_after, digest_after = \
                stealstage.measurement_implementation(copied)
            self.assertNotEqual(digest_before, digest_after)
            self.assertNotEqual(manifest_before, manifest_after)
            parallel_helper = mock.Mock(
                __file__=str(copied / "tools/runeio.py"))
            with self.assertRaisesRegex(ValueError, "loaded from wrong path"):
                stealstage._require_bound_helper(
                    parallel_helper, "tools/runeio.py", manifest_before)

    def test_manifest_hashes_schema_design_and_source_identity_fail_closed(self) -> None:
        contract = self._contract_bytes()
        manifest = self._manifest()

        def validate(value):
            return stealstage.validate_manifest(
                contract, json.dumps(value).encode(), ROOT)

        wrong = dict(manifest)
        wrong["metric_contract_sha256"] = "f" * 64
        with self.assertRaisesRegex(ValueError, "metric-contract hash"):
            validate(wrong)
        wrong = dict(manifest)
        wrong["measurement_tool_sha256"] = "f" * 64
        with self.assertRaisesRegex(ValueError, "measurement-tool hash"):
            validate(wrong)
        wrong = dict(manifest)
        wrong["measurement_implementation_sha256"] = "f" * 64
        with self.assertRaisesRegex(ValueError, "measurement-implementation"):
            validate(wrong)
        wrong = dict(manifest)
        wrong["extra"] = 1
        with self.assertRaisesRegex(ValueError, "incorrect schema"):
            validate(wrong)
        wrong = json.loads(json.dumps(manifest))
        wrong["treatments"]["baseline"], wrong["treatments"]["candidate"] = (
            wrong["treatments"]["candidate"],
            wrong["treatments"]["baseline"])
        with self.assertRaisesRegex(ValueError, "baseline must have an empty"):
            validate(wrong)
        wrong = json.loads(json.dumps(manifest))
        wrong["treatments"]["candidate"]["source_commit"] = "f" * 40
        with self.assertRaisesRegex(ValueError, "matched source parent"):
            validate(wrong)
        wrong = json.loads(json.dumps(manifest))
        wrong["treatments"]["candidate"]["source_artifacts"][
            "source_patch"]["sha256"] = "e" * 64
        with self.assertRaisesRegex(ValueError, "patch artifact/identity"):
            validate(wrong)
        wrong = json.loads(json.dumps(manifest))
        wrong["rounds"][1]["port"] = wrong["rounds"][0]["port"]
        with self.assertRaisesRegex(ValueError, "ports must be distinct"):
            validate(wrong)
        wrong = json.loads(json.dumps(manifest))
        wrong["rounds"][1]["root"] = wrong["rounds"][0]["root"]
        with self.assertRaisesRegex(ValueError, "roots must be distinct"):
            validate(wrong)
        wrong = json.loads(json.dumps(manifest))
        wrong["rounds"][2]["roster_and_team_assignment"] = \
            dict(wrong["rounds"][0]["roster_and_team_assignment"])
        wrong["rounds"][3]["roster_and_team_assignment"] = \
            dict(wrong["rounds"][0]["roster_and_team_assignment"])
        with self.assertRaisesRegex(ValueError, "not the exact arm-swapped"):
            validate(wrong)
        wrong = json.loads(json.dumps(manifest))
        wrong["rounds"][0]["source_identity"]["source_commit"] = "A" * 40
        with self.assertRaisesRegex(ValueError, "lowercase Git commit"):
            validate(wrong)
        wrong = json.loads(json.dumps(manifest))
        wrong["rounds"][0]["evaluation_window_server_seconds"]["end"] = 609.9
        with self.assertRaisesRegex(ValueError, "exact 600-second"):
            validate(wrong)

    def test_result_and_report_hashes_have_explicit_nonself_reference_rule(self) -> None:
        unbound = self._result_fixture()
        result = stealstage.bind_result_hashes(unbound)
        self.assertIs(stealstage.validate_result_hashes(result), result)
        changed = json.loads(json.dumps(result))
        changed["report"]["decision"] = "adopt"
        with self.assertRaisesRegex(ValueError, "report hash"):
            stealstage.validate_result_hashes(changed)
        changed = json.loads(json.dumps(result))
        changed["manifest_sha256"] = "3" * 64
        with self.assertRaisesRegex(ValueError, "result hash"):
            stealstage.validate_result_hashes(changed)
        changed = json.loads(json.dumps(result))
        changed["aggregate"]["baseline"]["raw"]["pickups"] += 1
        changed["result_sha256"] = stealstage.result_hash(changed)
        with self.assertRaisesRegex(ValueError, "does not match round metrics"):
            stealstage.validate_result_hashes(changed)
        with self.assertRaisesRegex(ValueError, "digest fields"):
            stealstage.bind_result_hashes(result)

    def test_retained_reader_rejects_symlinks_hardlinks_and_midread_change(self) -> None:
        with tempfile.TemporaryDirectory(prefix="steal-reader-") as temporary:
            root_path = Path(temporary) / "root"
            root_path.mkdir()
            artifact = root_path / "artifact"
            artifact.write_bytes(b"original")
            with stealstage.RetainedRoot(root_path) as root:
                self.assertEqual(root.read("artifact", 100, "artifact"),
                                 b"original")
                (root_path / "link").symlink_to(artifact)
                with self.assertRaises((OSError, ValueError)):
                    root.read("link", 100, "link")
                with self.assertRaisesRegex(ValueError, "normalized relative"):
                    root.read("../artifact", 100, "escape")
                alias = root_path / "alias"
                os.link(artifact, alias)
                with self.assertRaisesRegex(ValueError, "unalias"):
                    root.read("artifact", 100, "hardlink")
                alias.unlink()
                real_read = os.read
                changed = False

                def mutate(fd, count):
                    nonlocal changed
                    result = real_read(fd, count)
                    if not changed:
                        artifact.write_bytes(b"mutated!")
                        changed = True
                    return result

                with mock.patch.object(stealstage.os, "read", side_effect=mutate):
                    with self.assertRaisesRegex(ValueError, "changed while reading"):
                        root.read("artifact", 100, "racy")
            symlink_root = Path(temporary) / "root-link"
            symlink_root.symlink_to(root_path, target_is_directory=True)
            with self.assertRaises(OSError):
                stealstage.RetainedRoot(symlink_root)

            guard_root = Path(temporary) / "guard-root"
            guard_root.mkdir()
            artifacts = {}
            for name in stealstage.REQUIRED_ARTIFACTS:
                payload = f"retained-{name}".encode()
                path = f"{name}.evidence"
                (guard_root / path).write_bytes(payload)
                artifacts[name] = {
                    "path": path,
                    "sha256": hashlib.sha256(payload).hexdigest(),
                }
            configuration = b"set stage_a 1\n"
            (guard_root / "trial.cfg").write_bytes(configuration)
            receipt = {
                "name": "r1-baseline", "root": str(guard_root),
                "artifacts": artifacts,
                "configuration_artifacts": [{
                    "path": "trial.cfg",
                    "sha256": hashlib.sha256(configuration).hexdigest(),
                }],
                "configuration_sha256": stealstage._configuration_digest(
                    [("trial.cfg", configuration)]),
            }
            root_stat = guard_root.stat()
            metrics = [{"name": "r1-baseline",
                        "root_identity": [root_stat.st_dev,
                                          root_stat.st_ino]}]
            stealstage._revalidate_manifest_files([receipt], metrics)
            (guard_root / "engine.evidence").write_bytes(b"changed-engine")
            with self.assertRaisesRegex(ValueError, "engine changed before publication"):
                stealstage._revalidate_manifest_files([receipt], metrics)

    def test_exact_roster_timeline_window_membership_and_drift_rejection(self) -> None:
        assignment = self._assignment()
        stdlog = stealstage.parse_stdlog(self._stdlog_bytes())
        roster = stealstage.validate_continuous_roster(stdlog, assignment)
        self.assertEqual(roster["window"], (10.0, 610.0))
        counts = stealstage._window_counts(stdlog, roster)
        self.assertEqual(counts["total"]["pickups"], 1)
        self.assertEqual(counts["total"]["captures"], 1)
        for payload, message in (
                (self._stdlog_bytes(rename=True), "PlayerRename"),
                (self._stdlog_bytes(drift=True), "exactly one team"),
                (self._stdlog_bytes(early_leave=True), "left before exact")):
            with self.subTest(message=message):
                with self.assertRaises(ValueError):
                    stealstage.validate_continuous_roster(
                        stealstage.parse_stdlog(payload), assignment)
        bad = dict(assignment)
        bad["bot9"] = "red"
        with self.assertRaisesRegex(ValueError, "exactly ten unique 5v5"):
            stealstage.validate_continuous_roster(stdlog, bad)

    def test_ordered_host_and_per_player_stats_authorities_must_agree(self) -> None:
        assignment = self._assignment()
        stdlog = stealstage.parse_stdlog(self._stdlog_bytes())
        roster = stealstage.validate_continuous_roster(stdlog, assignment)
        host = (b"bot0 stole the blue flag.\n"
                b"bot0 captured the blue flag.\n"
                b"bot0 stole the blue flag.\n")
        reconciled = stealstage.reconcile_host_outcomes(
            host, stdlog, roster["teams"])
        self.assertEqual(len(reconciled["outcomes"]), 3)
        with self.assertRaisesRegex(ValueError, "ordered host"):
            stealstage.reconcile_host_outcomes(
                b"bot0 captured the blue flag.\n", stdlog,
                roster["teams"])
        with self.assertRaisesRegex(ValueError, "ordered host"):
            stealstage.reconcile_host_outcomes(
                host.replace(b"blue flag", b"red flag", 1), stdlog,
                roster["teams"])
        with self.assertRaisesRegex(ValueError, "malformed host"):
            stealstage.reconcile_host_outcomes(
                host + b"bot0 stole the green flag.\n", stdlog,
                roster["teams"])

        with tempfile.TemporaryDirectory(prefix="steal-db-") as temporary:
            database = Path(temporary) / "players.db"
            connection = sqlite3.connect(database)
            connection.executescript("""
                CREATE TABLE userdata (char_idx INTEGER, playername TEXT);
                CREATE TABLE game_stats (
                    char_idx INTEGER, frags INTEGER, fragged INTEGER,
                    deaths INTEGER, suicides INTEGER, shots INTEGER,
                    shots_hit INTEGER);
                CREATE TABLE ctf_stats (
                    char_idx INTEGER, flag_pickups INTEGER,
                    flag_captures INTEGER, flag_returns INTEGER);
            """)
            for index, name in enumerate(assignment):
                pickups = 2 if name == "bot0" else 0
                captures = 1 if name == "bot0" else 0
                connection.execute("INSERT INTO userdata VALUES (?, ?)",
                                   (index, name))
                connection.execute(
                    "INSERT INTO game_stats VALUES (?,0,0,0,0,0,0)",
                    (index,))
                connection.execute(
                    "INSERT INTO ctf_stats VALUES (?,?,?,0)",
                    (index, pickups, captures))
            connection.commit()
            connection.close()
            payload = database.read_bytes()
        stats = stealstage.parse_stats_database(payload, assignment)
        stealstage.reconcile_stats(stdlog, stats, roster["roster"])
        stats["per_player"]["bot0"]["pickups"] = 1
        with self.assertRaisesRegex(ValueError, "disagrees with StdLog"):
            stealstage.reconcile_stats(stdlog, stats, roster["roster"])

    def test_policy_report_is_exactly_bound_to_reconstructed_output(self) -> None:
        identity = self._identities()["baseline"]
        probe = {"tests_run": 9, "failures": 0, "errors": 0,
                 "skipped": 0, "successful": True}
        report = stealstage._knowledge_report(
            identity, "2" * 64, "4" * 64, probe)
        payload = json.dumps(report).encode()
        self.assertEqual(
            stealstage.validate_knowledge_report(
                payload, identity, measurement_tool_sha256="2" * 64,
                measurement_implementation_sha256="4" * 64,
                policy_probe=probe), report)
        changed = dict(report)
        changed["tests_run"] = 0
        with self.assertRaisesRegex(ValueError, "differs from reconstructed"):
            stealstage.validate_knowledge_report(
                json.dumps(changed).encode(), identity,
                measurement_tool_sha256="2" * 64,
                measurement_implementation_sha256="4" * 64,
                policy_probe=probe)
        failed_probe = dict(probe, failures=1, successful=False)
        failed_report = stealstage._knowledge_report(
            identity, "2" * 64, "4" * 64, failed_probe)
        with self.assertRaisesRegex(ValueError, "policy probe failed"):
            stealstage.validate_knowledge_report(
                json.dumps(failed_report).encode(), identity,
                measurement_tool_sha256="2" * 64,
                measurement_implementation_sha256="4" * 64,
                policy_probe=failed_probe)
        changed = dict(report)
        changed["module_sha256"] = "f" * 64
        with self.assertRaisesRegex(ValueError, "identity mismatch"):
            stealstage.validate_knowledge_report(
                json.dumps(changed).encode(), identity,
                measurement_tool_sha256="2" * 64,
                measurement_implementation_sha256="4" * 64,
                policy_probe=probe)
        changed = dict(report)
        changed["extra"] = 0
        with self.assertRaisesRegex(ValueError, "incorrect schema"):
            stealstage.validate_knowledge_report(
                json.dumps(changed).encode(), identity,
                measurement_tool_sha256="2" * 64,
                measurement_implementation_sha256="4" * 64,
                policy_probe=probe)

    def test_source_patch_reconstruction_probe_and_import_shadow_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="steal-source-") as temporary:
            repository = Path(temporary).resolve()
            subprocess.run(("git", "clone", "-q", "--no-hardlinks", ROOT,
                            repository), check=True)
            subprocess.run(("git", "checkout", "-q", "--detach",
                            self._source_commit()),
                           cwd=repository, check=True)
            (repository / "untracked.c").write_text("omitted\n")
            with self.assertRaisesRegex(ValueError, "excludes untracked"):
                stealstage._source_patch_payload(repository)
            (repository / "untracked.c").unlink()
            self.assertEqual(stealstage._run_knowledge_probe(repository), {
                "tests_run": 13, "failures": 0, "errors": 0,
                "skipped": 0, "successful": True})
            build_inputs = {
                name: hashlib.sha256((repository / name).read_bytes()).hexdigest()
                for name in stealstage.SOURCE_BUILD_INPUTS}
            makefile = repository / "GNUmakefile"
            original_makefile = makefile.read_bytes()
            makefile.write_bytes(original_makefile + b"\n# candidate mutation\n")
            with self.assertRaisesRegex(ValueError, "build authority"):
                stealstage._rebuild_module(repository, build_inputs)
            makefile.write_bytes(original_makefile)
            helper = repository / "tools/runeio.py"
            original_helper = helper.read_bytes()
            helper.write_bytes(original_helper + b"\n# candidate mutation\n")
            with self.assertRaisesRegex(ValueError, "measurement/build"):
                stealstage._validate_candidate_delta(repository)
            helper.write_bytes(original_helper)
            for generated in ("GitRevisionInfo.h", "stage-a-game.so"):
                collision = repository / generated
                collision.symlink_to("g_main.c")
                subprocess.run(("git", "add", "-f", generated),
                               cwd=repository, check=True)
                collision_manifest = stealstage._source_tree_manifest(
                    repository)
                with self.subTest(generated=generated):
                    with self.assertRaisesRegex(ValueError, "output collides"):
                        stealstage._rebuild_module(
                            repository, build_inputs, collision_manifest)
                subprocess.run(("git", "reset", "-q", "--", generated),
                               cwd=repository, check=True)
                collision.unlink()

            source_manifest = stealstage._source_tree_manifest(repository)
            real_run = subprocess.run
            production_source = repository / "g_main.c"
            production_payload = production_source.read_bytes()

            def mutating_build(command, *args, **kwargs):
                if (isinstance(command, tuple) and
                        "stage-a-game.so" in command and
                        "GNUmakefile" in command):
                    (repository / "stage-a-game.so").write_bytes(b"module")
                    production_source.write_bytes(
                        production_payload + b"\n/* drift */\n")
                    return subprocess.CompletedProcess(command, 0, b"", b"")
                return real_run(command, *args, **kwargs)

            with mock.patch.object(
                    stealstage.subprocess, "run", side_effect=mutating_build):
                with self.assertRaisesRegex(ValueError, "mutated tracked"):
                    stealstage._rebuild_module(
                        repository, build_inputs, source_manifest)
            production_source.write_bytes(production_payload)
            (repository / "stage-a-game.so").unlink()
            (repository / "GitRevisionInfo.h").unlink()
            for shadow in ("json.py", "unittest.py"):
                (repository / shadow).write_text(
                    "raise SystemExit('must never be imported')\n")
                subprocess.run(("git", "add", shadow), cwd=repository,
                               check=True)
                with self.subTest(shadow=shadow):
                    with self.assertRaisesRegex(
                            ValueError, "shadows a probe import"):
                        stealstage._run_knowledge_probe(repository)
                subprocess.run(("git", "reset", "-q", "--", shadow),
                               cwd=repository, check=True)
                (repository / shadow).unlink()
            probe_path = repository / stealstage.POLICY_PROBE_PATH
            probe_path.write_text(probe_path.read_text() + "\n# mutation\n")
            with self.assertRaisesRegex(ValueError, "differs from frozen"):
                stealstage._run_knowledge_probe(repository)
            patch = stealstage._source_patch_payload(repository)
            tampered = patch.replace(b"@@", b"XX", 1)
            with self.assertRaises(ValueError):
                with stealstage._reconstructed_source(
                        ROOT, self._source_commit(), tampered):
                    pass
            # The evaluator reconstructs the declared arm and does not probe
            # an unrelated dirty checkout supplied only as the object source.
            self.assertNotEqual(
                stealstage._source_patch_payload(repository), b"")
            with stealstage._reconstructed_source(
                    repository, self._source_commit(), b"") as reconstructed:
                head = subprocess.run(("git", "rev-parse", "HEAD"),
                                      cwd=reconstructed, check=True,
                                      stdout=subprocess.PIPE,
                                      text=True).stdout.strip()
                self.assertEqual(head, self._source_commit())
                self.assertTrue(stealstage._run_knowledge_probe(
                    reconstructed)["successful"])

    def test_source_tree_rejects_absolute_and_escaping_symlinks(self) -> None:
        with tempfile.TemporaryDirectory(prefix="steal-symlink-") as temporary:
            repository = Path(temporary).resolve()
            subprocess.run(("git", "init", "-q"), cwd=repository, check=True)
            (repository / "dir").mkdir()
            (repository / "dir/target.c").write_text("tracked\n")
            (repository / "alias.c").symlink_to("dir/target.c")
            subprocess.run(("git", "add", "."), cwd=repository, check=True)
            stealstage._source_tree_manifest(repository)
            for target in ("/etc/passwd", "../../etc/passwd"):
                (repository / "alias.c").unlink()
                (repository / "alias.c").symlink_to(target)
                subprocess.run(("git", "add", "alias.c"), cwd=repository,
                               check=True)
                with self.subTest(target=target):
                    with self.assertRaisesRegex(ValueError, "not safe"):
                        stealstage._source_tree_manifest(repository)

    def test_bsp_authenticated_stands_are_derived_and_ambiguous_maps_rejected(self) -> None:
        import mapflags

        def bsp(entities):
            text = "".join("{\n" + "".join(
                f'"{key}" "{value}"\n' for key, value in entity.items()) +
                "}\n" for entity in entities).encode("latin-1") + b"\0"
            header = bytearray(mapflags.BSP_HEADER_SIZE)
            struct.pack_into("<4si", header, 0, b"IBSP", 38)
            struct.pack_into("<ii", header, 8,
                             mapflags.BSP_HEADER_SIZE, len(text))
            return bytes(header) + text

        payload = bsp((
            {"classname": "worldspawn"},
            {"classname": "info_flag_red", "origin": "1 2 3"},
            {"classname": "item_flag_team2", "origin": "4 5 6"},
        ))
        self.assertEqual(stealstage.flag_stands_from_bsp(payload),
                         {"red": (1.0, 2.0, 3.0),
                          "blue": (4.0, 5.0, 6.0)})
        duplicate = bsp((
            {"classname": "worldspawn"},
            {"classname": "info_flag_red", "origin": "1 2 3"},
            {"classname": "info_flag_red", "origin": "2 3 4"},
            {"classname": "info_flag_blue", "origin": "4 5 6"},
        ))
        with self.assertRaisesRegex(ValueError, "exactly one red"):
            stealstage.flag_stands_from_bsp(duplicate)

    def test_demo_requires_all_clients_integral_consecutive_coverage_and_alignment(self) -> None:
        assignment = self._assignment()
        connected = {f"bot{index}": float(index + 1)
                     for index in range(10)}
        roster = {"roster": tuple(assignment), "connected": connected,
                  "teams": assignment, "window": (10.0, 610.0)}
        tracks = {}
        for index in range(10):
            first = index + 1
            tracks[index + 1] = [
                (frame, float(frame), 0.0, 0.0, 0)
                for frame in range(first, 611)]
        demo = {"svrecord": True, "map": "lmctf22",
                "frames": 610, "tracks": tracks,
                "skin_epochs": {
                    index: [(0, f"bot{index}\\male/grunt")]
                    for index in range(10)}}
        aligned = stealstage.validate_demo_alignment(
            demo, roster, "lmctf22", roster["window"], fps=1.0)
        self.assertEqual(set(aligned["mapping"]), set(assignment))
        missing = {**demo, "tracks": dict(tracks)}
        missing["tracks"].pop(10)
        with self.assertRaisesRegex(ValueError, "exactly all expected"):
            stealstage.validate_demo_alignment(
                missing, roster, "lmctf22", roster["window"], fps=1.0)
        gap = {**demo, "tracks": dict(tracks)}
        gap["tracks"][1] = tracks[1][:100] + tracks[1][101:]
        with self.assertRaisesRegex(ValueError, "consecutive"):
            stealstage.validate_demo_alignment(
                gap, roster, "lmctf22", roster["window"], fps=1.0)
        fractional = {**demo, "tracks": dict(tracks)}
        fractional["tracks"][1] = list(tracks[1])
        fractional["tracks"][1][0] = (1.5, 0.0, 0.0, 0.0, 0)
        with self.assertRaisesRegex(ValueError, "integer"):
            stealstage.validate_demo_alignment(
                fractional, roster, "lmctf22", roster["window"], fps=1.0)
        drifted = dict(roster)
        drifted["connected"] = dict(connected)
        drifted["connected"]["bot9"] = 10.2
        with self.assertRaisesRegex(ValueError, "residual"):
            stealstage.validate_demo_alignment(
                demo, drifted, "lmctf22", roster["window"], fps=1.0)
        short = {**demo, "frames": 609}
        with self.assertRaisesRegex(ValueError, "total coverage"):
            stealstage.validate_demo_alignment(
                short, roster, "lmctf22", roster["window"], fps=1.0)
        renamed = {**demo, "skin_epochs": dict(demo["skin_epochs"])}
        renamed["skin_epochs"][0] = [
            (0, "bot0\\male/grunt"), (100, "other\\male/grunt")]
        with self.assertRaisesRegex(ValueError, "rename/slot drift"):
            stealstage.validate_demo_alignment(
                renamed, roster, "lmctf22", roster["window"], fps=1.0)

    def test_carry_reconciliation_is_one_to_one_same_player_and_bounded(self) -> None:
        starts = [
            {"name": "a", "team": "red", "time": 1.1},
            {"name": "a", "team": "red", "time": 2.1},
        ]
        pickups = [
            {"name": "a", "kind": "F Pickup", "time": 1.0},
            {"name": "a", "kind": "F Pickup", "time": 2.0},
        ]
        matches = stealstage.reconcile_carry_starts(starts, pickups)
        self.assertEqual(len(matches), 2)
        for changed in (
                starts[:1],
                starts + [{"name": "a", "team": "red", "time": 2.15}],
                [{**starts[0], "name": "b"}, starts[1]],
                [{**starts[0], "time": 1.200001}, starts[1]]):
            with self.subTest(starts=changed):
                with self.assertRaisesRegex(ValueError, "one-to-one"):
                    stealstage.reconcile_carry_starts(changed, pickups)
        boundary_pickup = [
            {"name": "a", "kind": "F Pickup", "time": 0.6}]
        self.assertEqual(len(stealstage.reconcile_carry_starts(
            [{"name": "a", "team": "red", "time": 0.8}],
            boundary_pickup)), 1)
        with self.assertRaisesRegex(ValueError, "one-to-one"):
            stealstage.reconcile_carry_starts(
                [{"name": "a", "team": "red", "time": 0.800001}],
                boundary_pickup)

    def test_approach_exposure_denominator_is_separate_from_conversion(self) -> None:
        track = [
            (0, 400.0, 0.0, 0.0, 0),
            (1, 383.0, 0.0, 0.0, 0),
            (2, 380.0, 0.0, 0.0, 0),
            (3, 380.0, 0.0, 0.0, 0),
        ]
        teams = {"a": "red"}
        approaches = stealstage.qualifying_approaches(
            "a", "red", track, (0.0, 0.0, 0.0), 0.0, [], teams,
            (1.0, 3.0), fps=1.0)
        exposure = stealstage.observed_stand_seconds(
            "a", "red", track, 0.0, [], teams, (1.0, 3.0), fps=1.0)
        self.assertEqual(len(approaches), 1)
        self.assertEqual(exposure, 2.0)
        self.assertEqual(stealstage.match_close_pickups(approaches, []), [])
        assignment = self._assignment()
        roster = {"roster": tuple(assignment), "teams": assignment,
                  "window": (1.0, 3.0)}
        common_track = [(frame, 0.0, 0.0, 0.0, 0)
                        for frame in range(32)]
        aligned = {
            "frames": 31, "offset": 0.0, "level_time_end": 3.1,
            "residuals": {name: 0.0 for name in assignment},
            "mapping": {name: {"track": list(common_track)}
                        for name in assignment},
        }
        with mock.patch.object(
                stealstage, "validate_demo_alignment", return_value=aligned):
            measured = stealstage.analyze_demo(
                {}, roster, {"red": (0.0, 0.0, 0.0),
                             "blue": (0.0, 0.0, 0.0)},
                {"flag_events": []}, "lmctf22",
                stealstage.load_contract(self._contract_bytes()))
        self.assertAlmostEqual(
            measured["by_team"]["red"]["observed_stand_seconds"], 2.0)
        self.assertAlmostEqual(
            measured["by_team"]["blue"]["observed_stand_seconds"], 2.0)
        self.assertAlmostEqual(measured["observed_stand_seconds"], 4.0)

    def test_sg_diagnostics_validate_fields_enums_rune_and_cutoff(self) -> None:
        assignment = self._assignment()
        roster = {"roster": tuple(assignment), "teams": assignment,
                  "window": (10.0, 610.0)}
        joins = [f"bot{index} entered the game" for index in range(10)]

        def census(*, dead=None, frames=None):
            dead = dead or set()
            frames = frames or range(100, 6120, 10)
            return [
                f"SGCENSUS bot{index}: frm={frame} "
                f"alive={0 if (index, frame) in dead else 1}"
                for frame in frames for index in range(10)]

        def sg(index, **changes):
            fields = {"role": 0, "seed": 0, "goal": 0, "sgoal": 0,
                      "speed": 1, "link": -1, "action": -1,
                      "stuck": "0.0", "frame": 100 + index}
            fields.update(changes)
            return (f"SG bot{index}: role={fields['role']} seed={fields['seed']} "
                    f"goal={fields['goal']} sgoal={fields['sgoal']} "
                    f"spd={fields['speed']} org=(0 0 0) link={fields['link']} "
                    f"act={fields['action']} hp=0 dh=0 dl=0 "
                    f"st={fields['stuck']} gnd=1 eng=0 frm={fields['frame']}")

        class Rune:
            seeds = [object()]
            links = []

        stdlog = {"flag_events": [], "combat_events": [
            {"kind": "Kill", "attacker": "bot0", "victim": "bot1",
             "weapon": "Railgun", "time": 609.0, "log_order": 1}]}
        host = {"lines": joins + census() +
                [sg(index, frame=110 + index) for index in range(10)] +
                ["bot1 was railed by bot0"], "outcomes": []}
        contract = stealstage.load_contract(self._contract_bytes())
        metrics = stealstage.diagnostic_metrics(
            host, stdlog, roster, Rune(), contract)
        self.assertEqual(metrics["samples"], 10)
        self.assertEqual(metrics["brackets"]["cutoff_gap_seconds"], 1.0)
        cases = (
            {"role": 9}, {"action": 7}, {"seed": 1},
            {"stuck": "nan"}, {"frame": 2147483648},
        )
        for change in cases:
            lines = joins + census() + [sg(0, **{"frame": 110, **change})] + [sg(
                index, frame=110 + index)
                                                for index in range(1, 10)] + \
                ["bot1 was railed by bot0"]
            with self.subTest(change=change):
                with self.assertRaises(ValueError):
                    stealstage.diagnostic_metrics(
                        {"lines": lines, "outcomes": []}, stdlog,
                        roster, Rune(), contract)
        late_stdlog = {**stdlog, "combat_events": [
            {**stdlog["combat_events"][0], "time": 590.0}]}
        with self.assertRaisesRegex(ValueError, "cutoff gap"):
            stealstage.diagnostic_metrics(
                host, late_stdlog, roster, Rune(), contract)

        exact = stealstage.validate_census(host["lines"], roster)
        self.assertEqual(exact["coverage_bot_seconds"], 6000)
        self.assertEqual(exact["context_rows_before"], 10)
        self.assertEqual(exact["context_rows_after"], 10)
        dead_host = {"lines": joins + census(dead={(0, 110)}) +
                     [sg(0, frame=110)] + [sg(index, frame=110 + index)
                                           for index in range(1, 10)] +
                     ["bot1 was railed by bot0"], "outcomes": []}
        with self.assertRaisesRegex(ValueError, "dead census"):
            stealstage.diagnostic_metrics(
                dead_host, stdlog, roster, Rune(), contract)
        every_other = joins + census(frames=range(100, 6120, 20))
        with self.assertRaisesRegex(ValueError, "gap-free"):
            stealstage.validate_census(every_other, roster)
        replay = joins + census() + ["SGCENSUS bot0: frm=6110 alive=1"]
        with self.assertRaisesRegex(ValueError, "replayed"):
            stealstage.validate_census(replay, roster)
        malformed = joins + census() + ["SGCENSUS bot0 frm=6120 alive=1"]
        with self.assertRaisesRegex(ValueError, "malformed"):
            stealstage.validate_census(malformed, roster)

    def test_full_rune_identity_and_numeric_evaluator_are_executable(self) -> None:
        header = type("Header", (), {
            "map_name": "lmctf22", "bsp_checksum": 1, "entity_crc32": 2,
            "physics_flags": 3, "gravity": 800.0, "airaccelerate": 0.0,
            "maxvelocity": 2000.0, "pmove_substep_ms": 10,
            "server_frame_ms": 100, "host_physics_id": 1,
            "payload_crc32": 4, "header_crc32": 5,
            "action_contract_crc32": 6, "mechanism_contract_crc32": 7,
            "num_seeds": 8, "num_links": 9,
            "num_activation_nodes": 10, "num_activation_edges": 11,
            "num_activation_plans": 12, "num_inventory_edges": 13,
            "string_bytes": 14,
        })()
        identity = stealstage._rune_identity(type("Rune", (), {
            "header": header})())
        self.assertEqual(tuple(identity), stealstage.RUNE_IDENTITY_FIELDS)

        rate_names = (
            "horizontal_distance_per_active_bot_minute",
            "moving_sample_fraction",
            "world_or_hazard_suicides_per_active_bot_minute",
            "combat_kills_per_active_team_minute",
            "combat_deaths_per_active_team_minute",
            "visible_or_audible_engagements_per_active_team_minute",
            "authoritative_pickups_per_active_team_minute",
            "approaches_per_observed_stand_minute",
            "close_approach_conversion",
            "authoritative_captures_per_active_team_minute",
            "steal_to_capture_conversion",
            "defender_post_dwell_fraction",
            "defender_moving_sample_fraction",
            "defender_departures_per_active_defender_minute",
            "captures_conceded_per_active_team_minute",
        )
        raw = {"pickups": 10, "approaches": 10,
               "observed_stand_seconds": 600.0, "timely": 5,
               "forbidden": 0, "mismatches": 0}
        baseline = {"rates": {name: 1.0 for name in rate_names},
                    "raw": dict(raw)}
        candidate = {"rates": {name: 1.0 for name in rate_names},
                     "raw": dict(raw)}
        contract = stealstage.load_contract(self._contract_bytes())
        gate = stealstage.evaluate_bands(baseline, candidate, contract)
        self.assertIn("steal.approach_rate_ratio", gate["checks"])
        self.assertTrue(gate["sufficient_events"])
        candidate["raw"]["forbidden"] = 1
        gate = stealstage.evaluate_bands(baseline, candidate, contract)
        self.assertFalse(gate["checks"][
            "perception.forbidden_knowledge_events"]["pass"])
        candidate["raw"]["forbidden"] = 0
        candidate["raw"]["pickups"] = 4
        baseline["raw"]["pickups"] = 4
        gate = stealstage.evaluate_bands(baseline, candidate, contract)
        self.assertFalse(gate["sufficient_events"])
        self.assertEqual(gate["decision"], "inconclusive")

    def test_measurement_tool_exposes_real_evaluate_and_verify_cli(self) -> None:
        result = subprocess.run(
            (sys.executable, "tools/stealstage.py", "--help"), cwd=ROOT,
            check=True, stdout=subprocess.PIPE, text=True)
        self.assertIn("evaluate", result.stdout)
        self.assertIn("verify-result", result.stdout)
        self.assertIn("knowledge-report", result.stdout)
        with tempfile.TemporaryDirectory(prefix="steal-cli-") as temporary:
            temporary = Path(temporary)
            contract = temporary / "contract.json"
            manifest = temporary / "manifest.json"
            result_path = temporary / "result.json"
            contract.write_bytes(self._contract_bytes())
            manifest.write_text("{}\n")
            process = subprocess.run((
                sys.executable, "tools/stealstage.py", "evaluate",
                "--contract", str(contract), "--manifest", str(manifest),
                "--source-repository", str(ROOT)), cwd=ROOT,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn("incorrect schema", process.stderr)
            self.assertNotIn("unexpected keyword", process.stderr)

            result_path.write_bytes(canonical := stealstage.canonical_json(
                stealstage.bind_result_hashes(self._result_fixture())))
            process = subprocess.run((
                sys.executable, "tools/stealstage.py", "verify-result",
                "--result", str(result_path), "--contract", str(contract),
                "--manifest", str(manifest), "--source-repository",
                str(ROOT)), cwd=ROOT, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn("incorrect schema", process.stderr)
            missing = subprocess.run((
                sys.executable, "tools/stealstage.py", "verify-result",
                "--result", str(result_path), "--contract", str(contract),
                "--source-repository", str(ROOT)), cwd=ROOT,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("--manifest", missing.stderr)

            supplied = stealstage._strict_json(canonical, "fixture result")
            different = json.loads(json.dumps(supplied))
            different["manifest_sha256"] = "3" * 64
            different.pop("result_sha256")
            different.pop("report_sha256")
            different = stealstage.bind_result_hashes(different)
            with mock.patch.object(
                    stealstage, "validate_manifest", return_value=different):
                with self.assertRaisesRegex(ValueError, "differs from complete"):
                    stealstage.verify_result(
                        self._contract_bytes(), b"{}", canonical, ROOT)

    def test_complete_evaluate_publish_verify_cli_and_postpublish_tamper(self) -> None:
        with tempfile.TemporaryDirectory(prefix="steal-cli-positive-") as tmp:
            temporary = Path(tmp).resolve()
            contract, manifest, tamper_path = self._positive_cli_fixture(
                temporary)
            output = temporary / "result.json"
            evaluated = subprocess.run((
                sys.executable, "tools/stealstage.py", "evaluate",
                "--contract", str(contract), "--manifest", str(manifest),
                "--source-repository", str(ROOT), "--output", str(output)),
                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True)
            self.assertEqual(evaluated.returncode, 0, evaluated.stderr[-8000:])
            published = output.read_bytes()
            parsed = stealstage._strict_json(published, "published result")
            self.assertIs(stealstage.validate_result_hashes(parsed), parsed)

            verified = subprocess.run((
                sys.executable, "tools/stealstage.py", "verify-result",
                "--result", str(output), "--contract", str(contract),
                "--manifest", str(manifest), "--source-repository",
                str(ROOT)), cwd=ROOT, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True)
            self.assertEqual(verified.returncode, 0, verified.stderr[-8000:])
            self.assertEqual(output.read_bytes(), published)

            shadow = temporary / "shadow-runner"
            for relative in stealstage.MEASUREMENT_IMPLEMENTATION_PATHS:
                destination = shadow / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes((ROOT / relative).read_bytes())
            helper = shadow / "tools/runeio.py"
            helper.write_bytes(helper.read_bytes() + b"\n# post-receipt drift\n")
            shadow_tool = shadow / "tools/stealstage.py"
            shadow_evaluate = subprocess.run((
                sys.executable, str(shadow_tool), "evaluate",
                "--contract", str(contract), "--manifest", str(manifest),
                "--source-repository", str(ROOT), "--output",
                str(temporary / "shadow-result.json")), cwd=shadow,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertNotEqual(shadow_evaluate.returncode, 0)
            self.assertIn(
                "measurement-implementation hash mismatch",
                shadow_evaluate.stderr)
            shadow_verify = subprocess.run((
                sys.executable, str(shadow_tool), "verify-result",
                "--result", str(output), "--contract", str(contract),
                "--manifest", str(manifest), "--source-repository",
                str(ROOT)), cwd=shadow, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True)
            self.assertNotEqual(shadow_verify.returncode, 0)
            self.assertIn(
                "measurement-implementation",
                shadow_verify.stderr)

            tamper_path.write_bytes(tamper_path.read_bytes() + b"tampered\n")
            rejected = subprocess.run((
                sys.executable, "tools/stealstage.py", "verify-result",
                "--result", str(output), "--contract", str(contract),
                "--manifest", str(manifest), "--source-repository",
                str(ROOT)), cwd=ROOT, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("SHA-256 mismatch", rejected.stderr)

    def test_dependency_free_serverrecord_reader_is_strict_and_tracks_effects(self) -> None:
        serverdata = (bytes([12]) + b"\0" * 9 + b"game\0" +
                      struct.pack("<H", 0xffff) + b"level\0")
        map_config = (bytes([13]) + struct.pack("<H", 33) +
                      b"maps/lmctf22.bsp\0")
        skin_config = (bytes([13]) + struct.pack("<H", 1312) +
                       b"bot0\\male/grunt\0")
        first = (bytes([20]) + struct.pack("<i", 1) + bytes([18]) +
                 bytes([1, 1]) + struct.pack("<h", 64) + bytes([0, 0]))
        second = (bytes([20]) + struct.pack("<i", 2) + bytes([18]) +
                  bytes([0, 1, 0, 0]))
        messages = b"".join(
            struct.pack("<i", len(message)) + message for message in
            (serverdata + map_config + skin_config + first, second))
        decoded = stealstage._decode_serverrecord(messages)
        self.assertEqual(decoded["map"], "lmctf22")
        self.assertEqual(decoded["wire_framenums"], [1, 2])
        self.assertFalse(decoded["terminated"])
        self.assertEqual([row[1] for row in decoded["tracks"][1]],
                         [8.0, 0.0])
        self.assertEqual(decoded["skin_epochs"][0],
                         [(1, "bot0\\male/grunt")])
        gap = messages.replace(struct.pack("<i", 2) + bytes([18]),
                               struct.pack("<i", 3) + bytes([18]), 1)
        with self.assertRaisesRegex(ValueError, "wire frames"):
            stealstage._decode_serverrecord(gap)
        with self.assertRaisesRegex(ValueError, "truncated"):
            stealstage._decode_serverrecord(messages[:-1])

    def test_result_publication_is_atomic_noreplace_and_parent_nofollow(self) -> None:
        with tempfile.TemporaryDirectory(prefix="steal-output-") as temporary:
            output = Path(temporary) / "result.json"
            stealstage._write_exclusive(output, b"one\n")
            self.assertEqual(output.read_bytes(), b"one\n")
            with self.assertRaises(FileExistsError):
                stealstage._write_exclusive(output, b"two\n")
            self.assertEqual(output.read_bytes(), b"one\n")
            real = Path(temporary) / "real"
            real.mkdir()
            alias = Path(temporary) / "alias"
            alias.symlink_to(real, target_is_directory=True)
            with self.assertRaises(OSError):
                stealstage._write_exclusive(alias / "result.json", b"x\n")
            parent = Path(temporary) / "replace-parent"
            parent.mkdir()
            moved = Path(temporary) / "opened-parent"
            real_link = os.link

            def replace_parent(*args, **kwargs):
                result = real_link(*args, **kwargs)
                parent.rename(moved)
                parent.mkdir()
                return result

            with mock.patch.object(
                    stealstage.os, "link", side_effect=replace_parent):
                with self.assertRaisesRegex(ValueError, "parent changed"):
                    stealstage._write_exclusive(
                        parent / "result.json", b"authority\n")


if __name__ == "__main__":
    unittest.main()
