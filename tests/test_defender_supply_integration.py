#!/usr/bin/env python3
"""Executable and source-linked checks for the bounded defender sortie."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DefenderSupplyIntegrationTest(unittest.TestCase):
    def test_phase_and_exact_route_policy_executes(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sg-def-supply-") as tmp:
            binary = Path(tmp) / "sg_defense_supply_test"
            compile_cmd = [
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wpedantic", "-I.", "tests/sg_defense_supply_test.c",
                "slipgate/sg_defense_supply.c", "-o", str(binary),
            ]
            subprocess.run(compile_cmd, cwd=ROOT, check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           text=True)
            result = subprocess.run([str(binary)], cwd=ROOT, check=True,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True)
            self.assertIn("sg_defense_supply_test: ok", result.stdout)

    def test_production_uses_exact_live_pad_and_phase_authority(self) -> None:
        goal = (ROOT / "slipgate/sg_goal.c").read_text()
        supply = (ROOT / "slipgate/sg_defense_supply.c").read_text()
        header = (ROOT / "slipgate/sg_goal.h").read_text()
        supply_header = (ROOT / "slipgate/sg_defense_supply.h").read_text()
        descend = (ROOT / "slipgate/sg_descend.c").read_text()

        self.assertIn("sg_fields.item[SG_FC_WEAPON]", goal)
        self.assertIn("sg_defense_supply_target_field[SG_MAXBOTS]", goal)
        self.assertIn("sg_fields.action_topology_epoch", goal)
        self.assertIn("SG_DefenseSupplyRoute(", goal)
        self.assertIn("goal_field = weapon_field", goal)
        self.assertIn("SG_DefenseSupplyChooseNeighbor(", descend)
        self.assertIn("SG_DefenseSupplyActionAllowed(", descend)
        self.assertGreaterEqual(
            descend.count("SG_DefenseSupplyGenericRetryAllowed("), 2
        )
        self.assertIn("l->action == RL_RUN", descend)
        self.assertIn(
            "bot->def_supply_phase == SG_DEF_SUPPLY_OUTBOUND",
            descend,
        )
        self.assertIn("supply_neighbors[supply_neighbor_count]", descend)
        self.assertIn("SG_DEF_SUPPLY_OUTBOUND", header)
        self.assertIn("SG_DEF_SUPPLY_RETURN", header)
        self.assertIn("selected_target_field", supply)
        self.assertIn("#define SG_DEFENSE_SUPPLY_DEADLINE_SECONDS 5.0f",
                      supply_header)
        self.assertIn("#define SG_DEF_SUPPLY_DEADLINE     SG_DEFENSE_SUPPLY_DEADLINE_SECONDS",
                      header)
        self.assertNotIn("def_supply_weapon_sig", goal)
        self.assertNotIn("item_sig[SG_FC_WEAPON]", goal)

    def test_acquisition_and_edges_enter_return_without_rearming(self) -> None:
        goal = (ROOT / "slipgate/sg_goal.c").read_text()
        descend = (ROOT / "slipgate/sg_descend.c").read_text()
        supply_header = (ROOT / "slipgate/sg_defense_supply.h").read_text()

        self.assertIn("weapon_state.nonblaster_available", goal)
        self.assertIn("SG_DefenseSupplyBeginReturn(bot)", goal)
        self.assertIn("SG_DefenseSupplyFinish(bot)", descend)
        self.assertIn("bot->def_supply_phase == SG_DEF_SUPPLY_RETURN", descend)
        self.assertIn("step.deadline_pending = SG_TimerPending(bot->def_supply_until)",
                      goal)
        self.assertIn("DefenseSupplyTargetValid(bot)", goal)
        self.assertIn("DefenseSupplyOtherOwner(bot, true)", goal)
        self.assertIn("DefenseSupplyRetireRun(bot)", goal)
        self.assertIn("DefenseSupplyRetireRailRetry(bot)", goal)
        self.assertIn("bot->rail_link = -1", goal)
        self.assertIn("bot->rail_stage = 0", goal)
        self.assertIn("bot->rail_until = 0.0f", goal)
        self.assertIn("action != RL_RUN", goal)
        self.assertIn("bot->commit_link = -1", goal)
        self.assertIn("strict live-field descent", descend)
        self.assertIn(
            "Starting a jump, drop, hook, lift, or declared",
            supply_header,
        )
        self.assertIn("route_field[SG_Rune()->links[bestlink].to]", descend)

    def test_owned_stocked_weapon_flows_through_post_selector(self) -> None:
        combat = (ROOT / "slipgate/sg_combat.c").read_text()
        header = (ROOT / "slipgate/sg_combat.h").read_text()
        descend = (ROOT / "slipgate/sg_descend.c").read_text()

        self.assertIn("SG_CombatWeaponState", header)
        self.assertIn("SG_CombatBestPostWeapon", header)
        self.assertIn("Combat_WalkLadder(self, stocked, sightline, true)",
                      combat)
        idle = combat.index("if (st->post_sight >= 0.0f)")
        self.assertIn("SG_CombatBestPostWeapon(self, st->post_sight)",
                      combat[idle:])
        self.assertIn("Combat_Arbitrate(self, st", combat[idle:])
        self.assertIn("it->use(self, it)", combat)
        self.assertIn("st->post_defender", combat)
        self.assertIn("Combat_PostWeapon(self, st->post_sight, false)",
                      combat)
        self.assertIn("role == SG_ROLE_DEFEND && bot->def_stand", descend)


if __name__ == "__main__":
    unittest.main()
