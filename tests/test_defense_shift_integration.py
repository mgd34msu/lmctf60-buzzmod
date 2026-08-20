#!/usr/bin/env python3
"""Source-level ownership checks for threat-responsive defender movement."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DefenseShiftIntegrationTest(unittest.TestCase):
    def test_selection_precedes_latch_and_commitment(self) -> None:
        source = (ROOT / "slipgate/sg_descend.c").read_text()
        chooser = source.index("SG_DefenseShiftChoose(&request")
        latch = source.index(
            "if (!defense_shift_selected && !defense_patrol_selected &&",
            chooser,
        )
        commitment = source.index(
            "if (bot->commit_link >= 0 && bot->commit_link <",
            latch,
        )

        self.assertLess(chooser, latch)
        self.assertLess(latch, commitment)
        self.assertEqual(source.count("SG_DefenseShiftChoose(&request"), 1)
        self.assertIn(
            "if (!defense_shift_selected && !defense_patrol_selected &&",
            source,
        )

    def test_exact_run_owns_the_step_and_deadline(self) -> None:
        source = (ROOT / "slipgate/sg_descend.c").read_text()

        self.assertIn("link->action != RL_RUN", source)
        self.assertIn("link->from != from_seed", source)
        self.assertIn("link->to != to_seed", source)
        self.assertIn("bot->commit_link < 0", source)
        self.assertIn("bot->commit_link == bot->def_shift_link", source)
        self.assertRegex(
            source,
            re.compile(
                r"SG_TimerArm\(&bot->commit_until, hold\);.*?"
                r"bot->commit_until = bot->def_shift_until;",
                re.DOTALL,
            ),
        )

    def test_player_visible_gate_is_threat_not_idle_patrol(self) -> None:
        source = (ROOT / "slipgate/sg_descend.c").read_text()

        required = (
            "role == SG_ROLE_DEFEND",
            "bot->def_stand",
            "bot->lead_ent <= 0",
            "!defense_quiet",
			"!duel",
            "!bot->engaged_last",
            "bot->rail_stage == 0",
            "SG_FLAG_HOME",
            "request.max_distance = 144.0f",
            "SG_TimerArm(&bot->def_shift_next, 1.25f)",
        )
        for token in required:
            self.assertIn(token, source)
        self.assertIn('X(defshift, "sg_defshift", "1")',
                      (ROOT / "slipgate/sg_cvars.h").read_text())
        self.assertIn('X(patrol, "sg_patrol", "0.55")',
                      (ROOT / "slipgate/sg_cvars.h").read_text())

    def test_quiet_patrol_is_enabled_and_walk_paced(self) -> None:
        descend = (ROOT / "slipgate/sg_descend.c").read_text()
        move = (ROOT / "slipgate/sg_move.c").read_text()

        self.assertIn("SG_DefensePatrolChoose(candidates,", descend)
        self.assertIn("tc->patrol_walk = true;", descend)
        self.assertIn("!defense_patrol_selected &&", descend)
        self.assertIn("bot->patrol_link == bot->commit_link", descend)
        self.assertLess(descend.index("SG_DefensePatrolChoose(candidates,"),
                        descend.index(
                            "StrikeCommitFreshLink(bot, tc, bestlink)"))
        self.assertIn("SG_DefensePatrolThrottle(sg_cv.patrol->value)", move)
        self.assertIn("role == SG_ROLE_DEFEND && bot->def_stand", move)

    def test_post_steps_charge_their_run_edge(self) -> None:
        source = (ROOT / "slipgate/sg_descend.c").read_text()
        shift = source[source.index("sg_defense_shift_candidate_t candidates"):
                       source.index("request.threat_x", source.index(
                           "sg_defense_shift_candidate_t candidates"))]
        patrol = source[source.index(
                            "sg_defense_patrol_candidate_t candidates[64]"):
                        source.index("bot->patrol_random =", source.index(
                            "sg_defense_patrol_candidate_t candidates[64]"))]

        self.assertIn("SG_RouteCandidateGoalMs(", shift)
        self.assertIn("goal_field[link->to]", shift)
        self.assertIn("Fields_LinkTraversalCostMs(link)", shift)
        self.assertIn("SG_RouteCandidateGoalMs(goal_field[link->to]", patrol)
        self.assertIn("Fields_LinkTraversalCostMs(link)", patrol)

    def test_pure_routes_skip_organic_return_penalty(self) -> None:
        source = (ROOT / "slipgate/sg_descend.c").read_text()
        call = source[source.index("SG_RouteReturnPenaltyAllowed("):
                      source.index("sg_cv.nobacktrack->value", source.index(
                          "SG_RouteReturnPenaltyAllowed("))]

        self.assertIn("nonworsening_route_neighbors, route_pure", call)

    def test_near_goal_hook_skip_requires_a_descending_run(self) -> None:
        source = (ROOT / "slipgate/sg_descend.c").read_text()
        start = source.index("descends = goal_field[bot->seed]")
        end = source.index("/* life ticker", start)
        proof = source[start:end]
        call = source[source.index("SG_HookNearGoalSkipAllowed("):
                      source.index("continue;", source.index(
                          "SG_HookNearGoalSkipAllowed("))]

        self.assertIn("goal_field[neighbor->to]", proof)
        self.assertIn("shelved = Carrier_LinkShelved(bot, li)", proof)
        self.assertIn("SG_HookFootRouteAvailable", proof)
        self.assertIn("descending_run_available = true", proof)
        self.assertIn("descending_run_available", call)

    def test_post_facing_uses_an_incoming_scoring_route(self) -> None:
        source = (ROOT / "slipgate/sg_descend.c").read_text()

        self.assertIn("SG_DefenseFacingSeed(SG_Rune(), bot->seed", source)
        self.assertNotIn("int facev = 0x7fffffff", source)

    def test_defense_terminal_preserves_selected_field(self) -> None:
        move = (ROOT / "slipgate/sg_move.c").read_text()
        start = move.index("else if (!have_aim && role == SG_ROLE_DEFEND)")
        end = move.index("if (!have_aim && !gf && tc->scoop_mission)", start)
        terminal = move[start:end]

        self.assertIn(
            "SG_TerminalFieldSeed(SG_Rune(), goal_field,", terminal)
        self.assertIn("VectorCopy(SG_Rune()->seeds[terminal_seed].origin", terminal)
        self.assertNotIn("SG_FlagStand", terminal)

        goal = (ROOT / "slipgate/sg_goal.c").read_text()
        for field in (
            "sg_fields.to_post[SG_TeamIdx(team)]",
            "sg_fields.to_lane[SG_TeamIdx(team)]",
            "sg_fields.to_icept[SG_TeamIdx(team)]",
        ):
            self.assertIn(field, goal)

    def test_admission_loss_retires_patrol_before_latch(self) -> None:
        source = (ROOT / "slipgate/sg_descend.c").read_text()
        retire = source.rfind("int patrol_link = bot->patrol_link", 0,
                              source.index(
                                  "SG_DefensePatrolRetire(bot, "
                                  "patrol_allowed)"))
        latch = source.index(
            "if (!defense_shift_selected && !defense_patrol_selected &&",
            retire,
        )

        self.assertLess(retire, latch)
        self.assertIn("SG_DefensePatrolRetire(bot, patrol_allowed)",
                      source[retire:latch])
        self.assertIn(".own_flag_home = own_flag && ctf_flagathome(own_flag)",
                      source[retire:latch])
        self.assertIn(".quiet = defense_quiet", source[retire:latch])
        self.assertIn(".busy = duel || bot->engaged_last", source[retire:latch])
        self.assertIn("SG_DefensePatrolAllowed(&patrol_request)",
                      source[retire:latch])
        self.assertIn("tc->bestlink = bestlink", source[retire:latch])
        self.assertIn("SG_TimerArm(&bot->patrol_until, 5.0f)",
                      source[retire:latch])
        self.assertIn("bot->patrol_link = chosen_link", source)
        self.assertIn("DefenseLocalRunReady(bot, bot->patrol_link", source)

    def test_pending_patrol_retirement_is_checked_before_drop(self) -> None:
        source = (ROOT / "slipgate/sg_descend.c").read_text()
        pending = source.index("if (bot->commit_retirement_pending &&")
        drop = source.index("if (drop_commit)", pending)
        hold = source[pending:drop]

        self.assertIn("SG_TraversalControllerPhysical(", hold)
        self.assertIn("bot, cl->action", hold)
        self.assertIn("drop_commit = false", hold)
        self.assertIn("staging_timed_out = false", hold)
        self.assertIn("bestlink = bot->commit_link", hold)

    def test_late_shelf_retires_shift_before_post_or_movement(self) -> None:
        source = (ROOT / "slipgate/sg_descend.c").read_text()
        shelf = source.index("if (bot->deaddoor_ahead)")
        retire = source.index("DefenseShiftRetireInvalid(bot, &bestlink")
        hold = source.index("if (bot->lead_ent > 0", retire)

        self.assertLess(shelf, retire)
        self.assertLess(retire, hold)
        self.assertIn("SG_DefenseShiftRetireIfInvalid(shift_link,", source)
        self.assertIn("&bot->commit_link", source)
        self.assertIn("*bestlink = -1", source)
        self.assertIn("DefenseShiftReset(bot, false)", source)
        self.assertIn("SG_TimerArm(&bot->def_shift_next, 1.25f)", source)

    def test_current_duel_blocks_and_retires_shift_authority(self) -> None:
        source = (ROOT / "slipgate/sg_descend.c").read_text()
        gate = source.index("shift_allowed = isfinite(sg_cv.defshift->value)")
        disabled = source.index("if (!shift_allowed)", gate)
        current_duel = source.index("!duel", gate)

        self.assertLess(current_duel, disabled)
        self.assertIn("bot->commit_link == bot->def_shift_link", source[disabled:])
        self.assertIn("DefenseShiftReset(bot,", source[disabled:])

    def test_current_visible_combat_preview_reaches_commit_before_move(self) -> None:
        combat = (ROOT / "slipgate/sg_combat.c").read_text()
        arach = (ROOT / "slipgate/sg_arach.c").read_text()

        preview = combat.index("qboolean SG_CombatWouldEngage(edict_t *self)")
        preview_end = combat.index("/*\n * Is this edict the enemy flag carrier",
                                   preview)
        preview_body = combat[preview:preview_end]
        self.assertIn("Combat_Scan(self, eye, forward, NULL, -1, false)", preview_body)
        self.assertIn("SG_CombatPreviewCandidateEligible", combat)
        self.assertNotIn("sg_cbt_", preview_body)

        defense_preview = arach.index(
            "Combat execution remains after movement"
        )
        preview_call = arach.index("SG_CombatWouldEngage(e)", defense_preview)
        preview_guard = arach.rfind(
            "if (isfinite(sg_cv.defshift->value)", 0, preview_call
        )
        preview_guard_end = arach.index("duel = true;", preview_call)
        guard_body = arach[preview_guard:preview_guard_end]
        duel_to_context = arach.index("tc.duel = duel", preview_call)
        commit = arach.index("Think_CommitLink(bot, &tc)", duel_to_context)
        self.assertGreaterEqual(preview_guard, 0)
        self.assertLess(preview_call, duel_to_context)
        self.assertLess(duel_to_context, commit)
        self.assertIn("role == SG_ROLE_DEFEND", guard_body)
        self.assertIn("bot->def_stand", guard_body)
        self.assertIn("sg_cv.defshift->value > 0.0f", guard_body)
        self.assertIn("SG_CombatWouldEngage(e)", guard_body)
        self.assertIn("duel = true;", arach[preview_call:duel_to_context])

    def test_lifecycle_resets_all_authority(self) -> None:
        bot = (ROOT / "slipgate/sg_bot.h").read_text()
        client = (ROOT / "slipgate/sg_client.c").read_text()
        arach = (ROOT / "slipgate/sg_arach.c").read_text()

        for field in (
            "patrol_link",
            "def_shift_seed",
            "def_shift_link",
            "def_shift_from",
            "def_shift_until",
            "def_shift_next",
        ):
            self.assertIn(field, bot)
            self.assertIn(field, arach)
        for field in ("def_shift_seed", "def_shift_link", "def_shift_from"):
            self.assertIn(field, client)


if __name__ == "__main__":
    unittest.main()
