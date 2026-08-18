#!/usr/bin/env python3
"""Executable and production-wiring checks for the strike frame adapter."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


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
