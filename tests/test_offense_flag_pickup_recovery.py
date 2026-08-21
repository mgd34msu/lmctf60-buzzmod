#!/usr/bin/env python3
"""Focused source/geometry checks for live enemy-flag pickup recovery."""

from pathlib import Path
import math
import os
import re
import subprocess
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def between(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def direct_touch_authorized(
    bot: tuple[float, float, float],
    flag: tuple[float, float, float],
    *,
    enemy_flag_available: bool = True,
    at_home: bool,
    perceivable: bool,
    body_clear: bool,
) -> bool:
    """The geometry/knowledge contract of the production authority seam."""
    return (
        enemy_flag_available
        and math.dist(bot[:2], flag[:2]) < 160.0
        and abs(flag[2] - bot[2]) <= 64.0
        and body_clear
        and (at_home or perceivable)
    )


def approach_prebreach_binding(
    goal_cost: int,
    *,
    direct_touch: bool,
    live_enemy: bool,
    alive: bool,
    current_enemy: bool,
    visible: bool,
    enemy_team: bool,
    target_slot: int,
    target_ctfid: int,
    target_origin: tuple[float, float, float],
) -> tuple[int, int, tuple[float, float, float]] | None:
    """Approach-band arm policy plus the shared live-target binding seam."""
    if not (
        2000 < goal_cost < 5000
        and not direct_touch
        and live_enemy
        and alive
        and current_enemy
        and visible
        and enemy_team
        and target_slot > 0
        and target_ctfid != 0
    ):
        return None
    return target_slot, target_ctfid, target_origin


def reset_nade_binding(
    binding: tuple[int, int, float, float]
) -> tuple[int, int, float, float]:
    """The four-field reset contract shared by every lifecycle exit."""
    del binding
    return 0, 0, 0.0, 0.0


def armed_target_origin(
    armed_slot: int,
    armed_ctfid: int,
    candidate_slot: int,
    candidate_ctfid: int,
    *,
    current_slot: int,
    current_ctfid: int,
    alive: bool,
    visible: bool,
    origin: tuple[float, float, float],
) -> tuple[float, float, float] | None:
    """The only target that a bound phase-2 grenade may refresh from."""
    if (
        armed_slot <= 0
        or armed_ctfid == 0
        or candidate_slot != armed_slot
        or candidate_ctfid != armed_ctfid
        or current_slot != armed_slot
        or current_ctfid != armed_ctfid
        or not alive
        or not visible
    ):
        return None
    return origin


class OffenseFlagPickupRecoveryTest(unittest.TestCase):

    def test_duel_route_surface_gate_is_shared(self) -> None:
        descend = source("slipgate/sg_descend.c")
        setup = between(
            descend,
            "qboolean duel_route_price = SG_DuelRoutePriceAllowed",
            "sg_defense_supply_neighbor_t supply_neighbors",
        )
        incumbent = between(
            descend,
            "bestval = Surface_At",
            "qboolean sink_ban = false;",
        )
        candidate = between(descend, "else if (duel_route_price)",
                            "if (sg_human_use &&")

        self.assertIn("enemy_pressure", setup)
        self.assertIn("sg_cv.press->value", setup)
        self.assertIn("role == SG_ROLE_CARRY", setup)
        self.assertIn("sg_cv.carrypress->value", setup)
        self.assertIn("if (duel_route_price)", incumbent)
        self.assertIn("else if (duel_route_price)", candidate)
        self.assertNotIn("if (duel)", incumbent)

    def test_teammate_pass_side_is_pair_symmetric_and_executable(self) -> None:
        program = textwrap.dedent(
            r"""
            #include <math.h>
            #include "slipgate/sg_crowd_pass.h"
            #include "slipgate/sg_team_collision.h"

            int main(void)
            {
                static const unsigned long pairs[][2] = {
                    {1UL, 2UL}, {1UL, 3UL}, {2UL, 4UL},
                    {17UL, 91UL}, {0x10001UL, 0x20003UL}
                };
                unsigned int i;

                if (SG_CrowdPassSide(0UL, 2UL) != 0 ||
                    SG_CrowdPassSide(2UL, 0UL) != 0 ||
                    SG_CrowdPassSide(7UL, 7UL) != 0)
                    return 1;
                if (!SG_TeammateBodyPassable(1, 1, 0, 1) ||
                    !SG_TeammateBodyPassable(2, 1, 0, 2) ||
                    SG_TeammateBodyPassable(1, 1, 0, 2) ||
                    SG_TeammateBodyPassable(2, 1, 0, 1) ||
                    SG_TeammateBodyPassable(1, 0, 0, 1) ||
                    SG_TeammateBodyPassable(1, 1, 1, 1) ||
                    SG_TeammateBodyPassable(0, 1, 0, 0) ||
                    SG_TeammateBodyPassable(1, 2, 0, 1))
                    return 4;

                for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
                    int ab = SG_CrowdPassSide(pairs[i][0], pairs[i][1]);
                    int ba = SG_CrowdPassSide(pairs[i][1], pairs[i][0]);
                    double a_lateral;
                    double b_lateral;

                    if ((ab != -1 && ab != 1) || ab != ba)
                        return 2;
                    a_lateral = sin((double)ab * 28.0 * 3.141592653589793 / 180.0);
                    b_lateral = sin((180.0 + (double)ba * 28.0) *
                                    3.141592653589793 / 180.0);
                    if (!(a_lateral * b_lateral < 0.0))
                        return 3;
                }
                return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory(prefix="sg-crowd-pass-") as temp:
            temp_path = Path(temp)
            source_path = temp_path / "crowd_pass_test.c"
            binary_path = temp_path / "crowd_pass_test"
            source_path.write_text(program, encoding="utf-8")
            compiler = os.environ.get("CC", "cc")
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wpedantic",
                    "-I",
                    str(ROOT),
                    str(source_path),
                    "-lm",
                    "-o",
                    str(binary_path),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(binary_path)], cwd=ROOT, check=True)

        move = source("slipgate/sg_move.c")
        fan = between(
            move,
            "Feelers: try the goal heading first",
            "if (!flag_touch_terminal && sg_cv.smooth->value",
        )
        self.assertIn("SG_FeelerProbe(", fan)
        self.assertIn("feeler.trace", fan)

        run_room = between(move, "static qboolean SG_RunRoom", "static void SG_MovePolicy")
        self.assertIn("SG_TeammateBodyPassable(", run_room)
        self.assertIn("e->client->ctf.teamnum", run_room)
        self.assertIn("tr.ent->client->ctf.teamnum", run_room)
        self.assertNotIn("tr.ent->client && !tr.ent->deadflag", run_room)

    def test_dodge_clock_is_bound_to_bot_life_not_client_slot(self) -> None:
        program = textwrap.dedent(
            r"""
            #include <math.h>
            #include "slipgate/sg_weave_policy.h"

            int main(void)
            {
                static const unsigned long long instances[] = {
                    1ULL, 2ULL, 17ULL, 0x100000003ULL
                };
                unsigned int i;
                int distinct = 0;

                for (i = 0; i < sizeof(instances) / sizeof(instances[0]); ++i) {
                    float period = SG_WeavePeriod(instances[i], 101UL + i);
                    int side = SG_WeaveSideAt(instances[i], 101UL + i, 12.5f);

                    if (period < 0.4f || period > 0.85f ||
                        (side != -1 && side != 1))
                        return 1;
                    if (side != SG_WeaveSideAt(instances[i], 101UL + i, 12.5f))
                        return 2;
                    if (i > 0 &&
                        (period != SG_WeavePeriod(instances[0], 101UL) ||
                         side != SG_WeaveSideAt(instances[0], 101UL, 12.5f)))
                        distinct = 1;
                }
                if (!distinct)
                    return 3;
                if (SG_WeaveIdentityMix(7ULL, 11UL) ==
                    SG_WeaveIdentityMix(7ULL, 12UL))
                    return 4;
                if (SG_AirStrafeInitialPhase(7ULL, 11UL) < 0.0f ||
                    SG_AirStrafeInitialPhase(7ULL, 11UL) >=
                        6.2831853071795864769f)
                    return 5;
                if (SG_AirStrafeInitialPhase(7ULL, 11UL) ==
                    SG_AirStrafeInitialPhase(8ULL, 11UL))
                    return 6;
                return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory(prefix="sg-weave-policy-") as temp:
            temp_path = Path(temp)
            source_path = temp_path / "weave_policy_test.c"
            binary_path = temp_path / "weave_policy_test"
            source_path.write_text(program, encoding="utf-8")
            compiler = os.environ.get("CC", "cc")
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wpedantic",
                    "-I",
                    str(ROOT),
                    str(source_path),
                    "-lm",
                    "-o",
                    str(binary_path),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(binary_path)], cwd=ROOT, check=True)

        move = source("slipgate/sg_move.c")
        weave = between(move, "if (duel && !hold_post",
                        "float dose = sg_cv.breather->value;")
        jink = between(move,
                       "if (role == SG_ROLE_CARRY && cmd->forwardmove != 0",
                       "if (SG_TimerPending(bot->beat_until)")
        for section in (weave, jink):
            self.assertIn("SG_WeaveSideAt(", section)
            self.assertIn("bot->instance_token", section)
            self.assertIn("e->client->ctf.ctfid", section)
            self.assertNotIn("e->client - game.clients", section)
        self.assertIn("SG_CarrierJinkThreat(", jink)
        self.assertIn("en9->heard_only", jink)
        self.assertIn("VectorLength(threat_delta)", jink)

        air = between(move, "float dose = sg_cv.airstrafe->value",
                      "/* A proved graph ride")
        self.assertIn("SG_AirStrafeInitialPhase(", air)
        self.assertIn("bot->instance_token", air)
        self.assertIn("e->client->ctf.ctfid", air)
        self.assertLess(
            air.index("SG_AirStrafeInitialPhase("),
            air.index("bot->as_phase += 2.0f"),
        )

    def test_team_formation_drift_uses_private_independent_sequences(self) -> None:
        arach = source("slipgate/sg_arach.c")
        skew = between(arach, "TEAM SKEW (sg_teamskew)",
                       "role-flap diagnostic")
        self.assertIn("SG_RoleSkewRandomNext(sg_role_skew_random[ts])", skew)
        self.assertIn("SG_RoleSkewRandomValue(sg_role_skew_random[ts])", skew)
        self.assertIn("SG_RoleSkewRandomInterval(sg_role_skew_random[ts])", skew)
        self.assertNotIn("rand()", skew)
        reset = between(arach, "static void Role_LevelReset(void)",
                        "rune_t *SG_Rune(void)")
        self.assertIn("SG_RoleSkewRandomInitial(0)", reset)
        self.assertIn("SG_RoleSkewRandomInitial(1)", reset)

    def test_carrier_belief_aging_revalidates_team_and_exact_flag(self) -> None:
        caco = source("slipgate/sg_caco.c")
        aging = between(caco, "static void Caco_Age", "static void Caco_Project")
        self.assertEqual(aging.count("SG_CarrierBeliefIdentityCurrent("), 2)
        self.assertIn("expected = i == 0 ? blueflag : redflag", aging)
        self.assertIn("expected = i == 0 ? redflag : blueflag", aging)
        self.assertEqual(aging.count("ClientHasFlag(p) == expected"), 2)

    def test_strike_frame_distinguishes_human_carrier_from_no_carrier(self) -> None:
        arach = source("slipgate/sg_arach.c")
        prepare = between(arach, "static void StrikePrepareFrame", "void SG_StrikeSlotReset")
        self.assertIn("enemy_carrier = SG_FlagCarrier(enemy_flag)", prepare)
        self.assertIn("enemy_flag_carried = enemy_carrier &&", prepare)
        adapter = source("slipgate/sg_strike_adapter.c")
        self.assertIn(
            "current->enemy_flag_carried && !previous->enemy_flag_carried",
            adapter,
        )
        self.assertIn(
            "previous->enemy_flag_carried && !current->enemy_flag_carried",
            adapter,
        )

    def test_non_escort_attackers_hold_enemy_base_during_our_carry(self) -> None:
        goal = source("slipgate/sg_goal.c")
        objective = between(goal, "void Think_Objective", "if (sg_cv.runetoss->value")
        carrier_gate = objective.index("SG_AttackObjectiveUsesFixedStand(")
        fixed_red = objective.index("sg_fields.to_red_flag", carrier_gate)
        fixed_blue = objective.index("sg_fields.to_blue_flag", fixed_red)
        moving = objective.index("sg_fields.to_flag_now", fixed_blue)
        self.assertLess(carrier_gate, fixed_red)
        self.assertLess(fixed_red, fixed_blue)
        self.assertLess(fixed_blue, moving)
        self.assertIn("sg_caco_team_belief.carrier[team_index].client", objective)

        arach = source("slipgate/sg_arach.c")
        strike = between(arach, "static const int *StrikeEnemyField",
                         "static const int *StrikeOwnField")
        fixed_gate = strike.index("SG_AttackObjectiveUsesFixedStand(")
        dynamic = strike.index("sg_fields.to_flag_now", fixed_gate)
        self.assertLess(fixed_gate, dynamic)
        self.assertIn("sg_caco_team_belief.carrier[ti].client", strike)

    def test_escort_selection_uses_belief_route_not_hidden_carrier_origin(self) -> None:
        arach = source("slipgate/sg_arach.c")
        start = arach.index("if (have_carrier && own->client != my_client)")
        end = arach.index("/* No carrier ends the carry epoch", start)
        escort = arach[start:end]
        self.assertIn("SG_EscortRouteCost(", escort)
        self.assertIn("SG_EscortAssignmentScore(", escort)
        self.assertIn("sg_fields.our_carrier_valid[ti]", escort)
        self.assertNotIn("car_ent->s.origin", escort)
        self.assertNotIn("VectorSubtract(sg_bots[k].ent->s.origin", escort)
        self.assertIn("SG_AutonomousEscortCandidate(", escort)
        self.assertIn("SG_ChatOrderedRole(sg_bots[k].ent)", escort)

        goal = source("slipgate/sg_goal.c")
        objective = between(goal, "else if (role == SG_ROLE_ESCORT)",
                            "if (sg_cv.runetoss->value")
        self.assertIn("sg_fields.our_carrier_valid", objective)
        self.assertIn("sg_fields.to_red_flag", objective)
        self.assertIn("sg_fields.to_blue_flag", objective)

    def test_human_order_cannot_be_phantom_autonomous_escort(self) -> None:
        arach = source("slipgate/sg_arach.c")
        start = arach.index("if (have_carrier && own->client != my_client)")
        end = arach.index("/* No carrier ends the carry epoch", start)
        escort = arach[start:end]
        candidate = escort.index("SG_AutonomousEscortCandidate(")
        score = escort.index("SG_EscortAssignmentScore(", candidate)
        self.assertLess(candidate, score)
        self.assertIn("SG_ChatOrderedRole(sg_bots[k].ent)",
                      escort[candidate:score])

    def test_disabled_exit_asymmetry_does_not_draw_randomness(self) -> None:
        goal = source("slipgate/sg_goal.c")
        carry = between(goal, "if (carrying && !bot->was_carrying)",
                        "else if (!carrying && bot->was_carrying)")

        disabled = carry.index("bot->exitasym_armed = false;")
        gate = carry.index("if (sg_cv.exitasym->value > 0.0f)", disabled)
        draw = carry.index("random() * 100.0f", gate)
        self.assertLess(disabled, gate)
        self.assertLess(gate, draw)

    def test_117_unit_attacker_runs_through_live_flag_not_home_seed(self) -> None:
        move = source("slipgate/sg_move.c")
        helper = between(
            move,
            "static qboolean SG_AttackFlagTerminalAim",
            "static void Hook_DisciplineRetire",
        )

        attacker = (229.0, -699.0)
        flag = (232.0, -816.0)
        distance = math.dist(attacker, flag)
        self.assertAlmostEqual(distance, 117.038455, places=5)
        self.assertLess(distance, 160.0)

        # The production vector projects 150u beyond the entity, so its
        # segment contains the exact live-flag point rather than a route seed.
        through = (
            attacker[0] + (flag[0] - attacker[0]) * (distance + 150.0) / distance,
            attacker[1] + (flag[1] - attacker[1]) * (distance + 150.0) / distance,
        )
        segment = (through[0] - attacker[0], through[1] - attacker[1])
        flag_vector = (flag[0] - attacker[0], flag[1] - attacker[1])
        cross = abs(segment[0] * flag_vector[1] - segment[1] * flag_vector[0])
        self.assertAlmostEqual(cross, 0.0, places=6)
        self.assertLess(
            flag_vector[0] * segment[0] + flag_vector[1] * segment[1],
            segment[0] * segment[0] + segment[1] * segment[1],
        )

        authority = between(
            move,
            "qboolean SG_AttackFlagDirectTouchAuthority",
            "static qboolean SG_AttackFlagTerminalAim",
        )
        for token in (
            "flag = SG_EnemyFlag(team);",
            "SG_FlagApproachAvailableTo(flag, e)",
            "SG_DistXY(flag->s.origin, e->s.origin) >= 160.0f",
            "fabsf(flag->s.origin[2] - e->s.origin[2]) > 64.0f",
            "ctf_flagathome(flag)",
            "SG_FlagPerceivable(e, flag)",
            "sg_host.trace(e->s.origin, e->mins, e->maxs, flag->s.origin",
            "body.startsolid || body.allsolid",
        ):
            self.assertIn(token, authority)
        for token in (
            "SG_AttackFlagDirectTouchAuthority(e, team, &flag)",
            "VectorScale(fd, (fl + 150.0f) / fl, fd);",
            "VectorAdd(e->s.origin, fd, wend);",
            "VectorCopy(wend, aim);",
        ):
            self.assertIn(token, helper)
        self.assertNotIn("SG_EnemyFlag(team)", helper)
        self.assertNotIn("Rune_NearestSeed", helper)
        self.assertEqual(move.count("SG_EnemyFlag(team)"), 1)

    def test_direct_touch_authority_rejects_wrong_topology_height_wall_and_unseen_drop(self) -> None:
        blue_flag = (232.0, -816.0, 192.125)

        # The witnessed Phase touch is same-space, live, and direct.
        self.assertTrue(
            direct_touch_authorized(
                (229.0, -699.0, 168.0),
                blue_flag,
                enemy_flag_available=True,
                at_home=True,
                perceivable=False,
                body_clear=True,
            )
        )
        # xmap28 seed 618: close in graph coordinates only, 11.3s away by
        # hook/drop topology.  It must retain the graph route.
        self.assertFalse(
            direct_touch_authorized(
                (-32.0, -560.0, 40.0),
                blue_flag,
                enemy_flag_available=True,
                at_home=True,
                perceivable=True,
                body_clear=True,
            )
        )
        # xmap28 seed 2 sits above the flag: XY proximity alone is invalid.
        self.assertFalse(
            direct_touch_authorized(
                (153.0, -848.0, 376.0),
                blue_flag,
                enemy_flag_available=True,
                at_home=True,
                perceivable=True,
                body_clear=True,
            )
        )
        self.assertFalse(
            direct_touch_authorized(
                (229.0, -699.0, 168.0),
                blue_flag,
                enemy_flag_available=True,
                at_home=True,
                perceivable=True,
                body_clear=False,
            )
        )
        self.assertFalse(
            direct_touch_authorized(
                (229.0, -699.0, 168.0),
                blue_flag,
                enemy_flag_available=True,
                at_home=False,
                perceivable=False,
                body_clear=True,
            )
        )
        self.assertTrue(
            direct_touch_authorized(
                (229.0, -699.0, 168.0),
                blue_flag,
                enemy_flag_available=True,
                at_home=False,
                perceivable=True,
                body_clear=True,
            )
        )

        # A carried flag edict can retain its home coordinates.
        self.assertFalse(
            direct_touch_authorized(
                (229.0, -699.0, 168.0),
                blue_flag,
                enemy_flag_available=False,
                at_home=True,
                perceivable=True,
                body_clear=True,
            )
        )

    def test_flag_touch_authority_controls_graph_exit(self) -> None:
        descend = source("slipgate/sg_descend.c")
        terminal = between(
            descend,
            "qboolean attack_touch = false;",
            "if (tc->strike_pressure)",
        )
        self.assertIn("SG_AttackFlagDirectTouchAuthority(e, team, NULL)", terminal)
        self.assertIn("if (tc->strike_pressure &&", terminal)
        self.assertIn("role == SG_ROLE_CARRY", terminal)
        self.assertIn("SG_OwnHomeFlagDirectTouchAuthority(e, team, NULL)", terminal)
        self.assertIn("if (attack_touch || capture_touch)", terminal)
        self.assertNotIn("goal_field[bot->seed]", terminal)
        self.assertNotIn("bot->seed", terminal)
        self.assertNotIn("role == SG_ROLE_ATTACK || role == SG_ROLE_CARRY", terminal)

        clean_grab = between(
            descend,
            "if (tc->strike_pressure)",
            "if (bot->lead_ent > 0",
        )
        self.assertIn("if (tc->strike_pressure)", clean_grab)
        self.assertIn("SG_StrikeEnemyPressureSnapshot(mb5)", clean_grab)
        self.assertIn("SG_StrikeEnemyPressureGoalSnapshot(mb5)", clean_grab)
        self.assertNotIn("mb5->last_goalcost", clean_grab)
        self.assertNotIn("mb5->last_role == (int)SG_ROLE_ATTACK", clean_grab)

        blue_flag = (232.0, -816.0, 192.125)
        seed_618_direct = direct_touch_authorized(
            (-32.0, -560.0, 40.0), blue_flag,
            at_home=True, perceivable=True, body_clear=True,
        )
        self.assertFalse(seed_618_direct)

        seed_2_direct = direct_touch_authorized(
            (153.0, -848.0, 376.0), blue_flag,
            at_home=True, perceivable=True, body_clear=True,
        )
        self.assertFalse(seed_2_direct)

    def test_recover_flag_touch_owns_terminal_movement(self) -> None:
        move = source("slipgate/sg_move.c")
        fallback_start = move.index("/* last resort: the goal itself, by belief */")
        fallback = move[fallback_start:move.index("if (have_aim)", fallback_start)]
        recover = between(
            fallback,
            "else if (!have_aim && role == SG_ROLE_RECOVER)",
            "else if (!have_aim && role == SG_ROLE_DEFEND)",
        )
        self.assertLess(
            recover.index("flag_touch_terminal ="),
            recover.index("SG_OwnDroppedFlagDirectTouchAuthority"),
        )
        self.assertLess(
            recover.index("SG_OwnDroppedFlagDirectTouchAuthority"),
            recover.index("if (!flag_touch_terminal)"),
        )
        texture = between(
            move,
            "if (!DefenseCombatApplyDuelWeave(hold_post, proved_control",
            "if (role == SG_ROLE_CARRY && cmd->forwardmove != 0",
        )
        self.assertIn("tc->flag_touch_terminal, weave_side, cmd", texture)
        self.assertIn("if (!bot->terminal && !tc->flag_touch_terminal)", texture)
        self.assertIn("FlagTouchClaimMovement(", fallback)
        for hold in ("hold_post", "rally_hold", "rail_hold"):
            self.assertIn(f"{hold} = tc->{hold};", fallback)

    def test_live_flag_priority_precedes_graph_and_clears_hold(self) -> None:
        move = source("slipgate/sg_move.c")
        priority = move.index(
            "SG_AttackFlagTerminalAim(e, team, aim, &terminal_flag)")
        graph = move.index("if (!have_aim && bestlink >= 0)", priority)
        fallback = move.index("/* last resort: the goal itself, by belief */", graph)
        self.assertLess(priority, graph)
        self.assertLess(graph, fallback)
        local = move[priority - 350:graph]
        for token in (
            "bestlink = -1;",
            "tc->bestlink = -1;",
            "rally_hold = false;",
            "tc->rally_hold = false;",
            "action == RL_RUN",
        ):
            self.assertIn(token, local)

        terminal_fallback = between(
            move, "if (gf)\n\t\t\t{", "FlagTouchClaimMovement")
        self.assertNotIn("home4", terminal_fallback)
        self.assertNotIn("Rune_NearestSeed", terminal_fallback)
        self.assertNotIn("role == SG_ROLE_ATTACK", terminal_fallback)

    def test_live_flag_touch_owns_terminal_heading(self) -> None:
        move = source("slipgate/sg_move.c")
        terminal = between(
            move,
            "SG_AttackFlagTerminalAim(e, team, aim, &terminal_flag)",
            "if (!have_aim && bestlink >= 0)",
        )
        self.assertIn("flag_touch_terminal = true;", terminal)
        smooth_start = "if (!flag_touch_terminal && sg_cv.smooth->value"
        fan = between(move, "Feelers: try the goal heading first", smooth_start)
        self.assertIn("if (!flag_touch_terminal)", fan)
        smooth = between(move, smooth_start, "at a drop lip")
        self.assertIn("if (!flag_touch_terminal &&", smooth)

    def test_scoop_mission_finishes_the_relay_touch(self) -> None:
        goal = source("slipgate/sg_goal.c")
        publish = between(
            goal,
            "SCOOP is an enemy-flag touch mission",
            "tc->goal_field = goal_field;",
        )
        for token in (
            "tc->scoop_mission = role == SG_ROLE_ESCORT",
            "carrier[SG_TeamIdx(team)].client < 0",
            "state == SG_FLAG_ASTRAY",
            "goal_field == sg_fields.to_flag_now",
        ):
            self.assertIn(token, publish)

        move = source("slipgate/sg_move.c")
        priority = between(
            move,
            "The terminal approach is a physical touch",
            "if (!have_aim && bestlink >= 0)",
        )
        self.assertIn(
            "EnemyFlagTouchMissionActive(\n"
            "\t\t        tc->strike_pressure, tc->scoop_mission)",
            priority,
        )
        fallback = between(
            move,
            "if (!have_aim && !gf && tc->scoop_mission)",
            "else if (!have_aim && !gf && tc->strike_pressure)",
        )
        self.assertIn("SG_AttackFlagDirectTouchAuthority", fallback)
        self.assertIn("SG_TerminalFieldSeed(SG_Rune(),", fallback)
        self.assertNotIn("SG_FlagStand", fallback)

        pressure = between(
            move,
            "else if (!have_aim && !gf && tc->strike_pressure)",
            "else if (!have_aim && !gf)",
        )
        self.assertIn("state == SG_FLAG_ASTRAY", pressure)
        self.assertIn("SG_TerminalFieldSeed(SG_Rune(), goal_field", pressure)
        astray = pressure[:pressure.index("else\n\t\t\t\t{")]
        self.assertNotIn("SG_FlagStand", astray)

        arach = source("slipgate/sg_arach.c")
        overlay = between(
            arach,
            "static qboolean StrikeApplyDutyRoute",
            "static void StrikeRetireGenericRail",
        )
        self.assertIn("tc->scoop_mission = false;", overlay)

    def test_home_flag_touch_owns_carrier_terminal_heading(self) -> None:
        move = source("slipgate/sg_move.c")
        fallback_start = move.index("/* last resort: the goal itself, by belief */")
        fallback = move[fallback_start:move.index(
            "\n\t\tif (have_aim)", fallback_start)]
        carrier = between(
            fallback,
            "else if (!have_aim && role == SG_ROLE_CARRY)",
            "else if (!have_aim && role == SG_ROLE_RECOVER)",
        )
        self.assertLess(
            carrier.index("SG_OwnHomeFlagDirectTouchAuthority"),
            carrier.index("flag_touch_terminal = true;"),
        )
        self.assertIn(
            "role == SG_ROLE_CARRY && flag_touch_terminal &&", fallback)
        self.assertIn(
            "role == SG_ROLE_CARRY && flag_touch_terminal &&\n"
            "\t\t\t\t    sg_cv.termbrake->value",
            fallback,
        )

    def test_empty_flag_room_neither_holds_nor_arms_belief_grenade(self) -> None:
        descend = source("slipgate/sg_descend.c")
        clean = between(
            descend,
            "if (tc->strike_pressure)",
            "if (bot->commit_link >= 0 && bot->commit_link <",
        )
        live_gate = "live_room_enemy = live_enemy &&"
        self.assertIn("live_enemy = SG_CombatLiveEnemy(e);", clean)
        self.assertIn(live_gate, clean)
        self.assertIn("SG_CanSee(e, live_enemy->s.origin, live_enemy->viewheight)", clean)
        self.assertIn("qboolean live_flag_terminal = attack_touch;", clean)
        self.assertIn("if (room >= 1 && live_room_enemy && !live_flag_terminal)", clean)

        grenade = clean[clean.index(
            "if (rally_hold && live_room_enemy && !live_flag_terminal"
        ):]
        self.assertIn("if (rally_hold && live_room_enemy && !live_flag_terminal", grenade)
        self.assertIn("SG_NadeArmPrebreachLiveEnemy(bot, e, team)", grenade)
        self.assertNotIn("FindItem(\"Grenades\")", grenade)
        self.assertNotIn("VectorCopy(live_enemy->s.origin, bot->nade_at);", grenade)
        self.assertNotIn("sg_caco_enemies", grenade)

        move = source("slipgate/sg_move.c")
        switch = between(
            move,
            "if (!proved_control && bot->nade_phase == 1)",
            "if (!proved_control && bot->nade_phase == 2)",
        )
        self.assertIn("SG_NadeTargetSwitching(bot)", switch)
        self.assertIn("SG_NadeBoundLiveTarget(e, bot)", switch)
        self.assertIn("SG_AttackFlagDirectTouchAuthority(e, team, NULL)", switch)
        self.assertIn("!tc->strike_pressure ||", switch)
        self.assertNotIn("role != SG_ROLE_ATTACK", switch)
        self.assertIn("SG_NadeTargetClear(bot);", switch)

        cook = between(
            move,
            "if (!proved_control && bot->nade_phase == 2)",
            "if (!proved_control && sg_cv.soundfire->value",
        )
        cancel = cook.index("if ((armed_target &&")
        release = cook.index("cmd->buttons &= ~BUTTON_ATTACK;   /* the release throws */")
        self.assertLess(cancel, release)
        self.assertIn("SG_CombatLiveEnemy(e)", cook)
        self.assertIn("!tc->strike_pressure ||", cook)
        self.assertIn("!armed_target && tc->strike_pressure &&", cook)
        self.assertNotIn("role != SG_ROLE_ATTACK", cook)
        self.assertIn("SG_AttackFlagDirectTouchAuthority(e, team, NULL)", cook)
        self.assertIn(
            "SG_AttackFlagTerminalAim(e, team, pickup_aim, NULL)", cook)
        self.assertIn("SG_CanSee(e, nade_enemy->s.origin, nade_enemy->viewheight)", cook)
        self.assertIn("bot->nade_phase = 0;", cook[:release])

    def test_approach_band_binds_only_a_live_visible_enemy_away_from_touch(self) -> None:
        goal = source("slipgate/sg_goal.c")
        approach = between(
            goal,
            "qboolean Think_ApproachBand",
            "void Think_InterceptField",
        )
        flight = between(
            approach,
            "if (pressure_approach && sg_cv.flycook->value",
            "\n\treturn hold;",
        )
        self.assertIn(
            "goal_field[bot->seed] > 2000 && goal_field[bot->seed] < 5000",
            approach,
        )
        direct = flight.index("!SG_AttackFlagDirectTouchAuthority(e, team, NULL)")
        arm = flight.index("SG_NadeArmPrebreachLiveEnemy(bot, e, team)")
        self.assertLess(direct, arm)
        self.assertNotIn("Danger_Field(team)", flight)
        self.assertNotIn("Rune_NearestSeed", flight)
        self.assertNotIn("SG_FlagStand(team, false)", flight)
        common = dict(
            live_enemy=True,
            alive=True,
            current_enemy=True,
            visible=True,
            enemy_team=True,
            target_slot=4,
            target_ctfid=9001,
            target_origin=(384.0, -676.0, 80.0),
        )
        # A stale expensive seed may still be physically in touch range: at
        # every approach-band cost, pickup authority wins before any arm.
        for cost in (2001, 3000, 4999):
            self.assertIsNone(
                approach_prebreach_binding(cost, direct_touch=True, **common)
            )
            self.assertIsNone(
                approach_prebreach_binding(
                    cost, direct_touch=False, live_enemy=False,
                    **{key: value for key, value in common.items() if key != "live_enemy"},
                )
            )
        self.assertIsNone(
            approach_prebreach_binding(2000, direct_touch=False, **common)
        )
        self.assertIsNone(
            approach_prebreach_binding(5000, direct_touch=False, **common)
        )
        self.assertEqual(
            approach_prebreach_binding(3000, direct_touch=False, **common),
            (4, 9001, (384.0, -676.0, 80.0)),
        )
        self.assertIsNone(
            approach_prebreach_binding(
                3000, direct_touch=False, current_enemy=False,
                **{key: value for key, value in common.items() if key != "current_enemy"},
            )
        )

    def test_nade_binding_is_cleared_on_each_lifecycle_and_abort_exit(self) -> None:
        move = source("slipgate/sg_move.c")
        arach = source("slipgate/sg_arach.c")
        client = source("slipgate/sg_client.c")
        clear = between(
            move,
            "void SG_NadeTargetClear",
            "static edict_t *SG_NadeBoundLiveTarget",
        )
        for token in (
            "bot->nade_target_slot = 0;",
            "bot->nade_target_ctfid = 0;",
            "bot->nade_target_switch_until = 0.0f;",
            "bot->nade_target_cook_until = 0.0f;",
        ):
            self.assertIn(token, clear)

        armed = (4, 9001, 12.5, 15.2)
        for reason in ("death", "slot reuse", "physics", "stale host", "abort", "release"):
            with self.subTest(reason=reason):
                self.assertEqual(reset_nade_binding(armed), (0, 0, 0.0, 0.0))

        # Every production phase-zero assignment in the affected lifecycle
        # and transaction code explicitly retires the four-field binding.
        for text in (arach, move):
            for match in re.finditer(r"bot->nade_phase\s*=\s*0;", text):
                self.assertIn("SG_NadeTargetClear(bot);", text[match.end():match.end() + 180])

        death = between(arach, "static void Bot_ResetLifeActions", "static qboolean Think_Dead")
        self.assertIn("bot->nade_phase = 0;\n\tSG_NadeTargetClear(bot);", death)
        physics = arach[arach.index("physics-incompatible"):]
        self.assertIn("bot->nade_phase = 0;\n\t\tSG_NadeTargetClear(bot);", physics[:1800])
        stale = arach[arach.index("stale-host-rope"):]
        self.assertIn("bot->nade_phase = 0;\n\t\tSG_NadeTargetClear(bot);", stale[:2200])
        slot = between(client, "static void BotSlot_Reset", "static const char *sg_names")
        self.assertLess(slot.index("SG_NadeTargetClear(bot);"), slot.index("memset(bot, 0, sizeof(*bot));"))

    def test_bound_grenade_refreshes_only_its_armed_live_target(self) -> None:
        armed_slot, armed_ctfid = 4, 9001
        first = armed_target_origin(
            armed_slot, armed_ctfid, 4, 9001,
            current_slot=4, current_ctfid=9001,
            alive=True, visible=True, origin=(320.0, -700.0, 80.0),
        )
        moved = armed_target_origin(
            armed_slot, armed_ctfid, 4, 9001,
            current_slot=4, current_ctfid=9001,
            alive=True, visible=True, origin=(384.0, -676.0, 80.0),
        )
        self.assertEqual(first, (320.0, -700.0, 80.0))
        self.assertEqual(moved, (384.0, -676.0, 80.0))
        self.assertIsNone(
            armed_target_origin(
                armed_slot, armed_ctfid, 4, 9002,
                current_slot=4, current_ctfid=9002,
                alive=True, visible=True, origin=moved,
            )
        )
        self.assertIsNone(
            armed_target_origin(
                armed_slot, armed_ctfid, 4, 9001,
                current_slot=4, current_ctfid=9001,
                alive=False, visible=True, origin=moved,
            )
        )
        self.assertIsNone(
            armed_target_origin(
                armed_slot, armed_ctfid, 4, 9001,
                current_slot=4, current_ctfid=9001,
                alive=True, visible=False, origin=moved,
            )
        )
        self.assertIsNone(
            armed_target_origin(
                armed_slot, armed_ctfid, 4, 9001,
                current_slot=7, current_ctfid=9010,
                alive=True, visible=True, origin=moved,
            )
        )

        move = source("slipgate/sg_move.c")
        descend = source("slipgate/sg_descend.c")
        goal = source("slipgate/sg_goal.c")
        bot = source("slipgate/sg_bot.h")
        for token in (
            "nade_target_slot",
            "nade_target_ctfid",
            "nade_target_switch_until",
            "nade_target_cook_until",
        ):
            self.assertIn(token, bot)
        self.assertIn("SG_NadeArmPrebreachLiveEnemy(bot, e, team)", descend)
        self.assertIn("SG_NadeArmPrebreachLiveEnemy(bot, e, team)", goal)
        arm = between(
            move,
            "qboolean SG_NadeArmPrebreachLiveEnemy",
            "static qboolean SG_NadeTargetSwitching",
        )
        for token in (
            "target = SG_CombatLiveEnemy(e);",
            "SG_AttackFlagDirectTouchAuthority(e, team, NULL)",
            "SG_CanSee(e, target->s.origin, target->viewheight)",
            "SG_StrikePrebreachGrenadeDistanceAllowed(distance)",
            "VectorCopy(target->s.origin, bot->nade_at);",
            "bot->nade_target_slot = slot;",
            "bot->nade_target_ctfid = target->client->ctf.ctfid;",
            "bot->nade_target_switch_until = bot->nade_until;",
        ):
            self.assertIn(token, arm)
        resolver = between(
            move,
            "static edict_t *SG_NadeBoundLiveTarget",
            "qboolean SG_NadeArmPrebreachLiveEnemy",
        )
        self.assertIn("target = g_edicts + bot->nade_target_slot;", resolver)
        self.assertIn("target->client->ctf.ctfid != bot->nade_target_ctfid", resolver)
        self.assertIn("target->deadflag", resolver)
        self.assertIn("SG_CombatLiveEnemy(e) != target", resolver)

        binding = between(move, "static qboolean SG_NadeTargetSwitching", "static edict_t *SG_NadeArmedTarget")
        self.assertIn("bot->nade_target_cook_until == bot->nade_until", binding)
        self.assertIn("bot->nade_target_switch_until == bot->nade_until", binding)

        cook = between(
            move,
            "if (!proved_control && bot->nade_phase == 2)",
            "if (!proved_control && sg_cv.soundfire->value",
        )
        refresh = cook.index("VectorCopy(nade_enemy->s.origin, bot->nade_at);")
        lead = cook.index("if (sg_cv.nadelead->value)", refresh)
        self.assertLess(refresh, lead)
        self.assertIn("edict_t *len9 = nade_enemy;", cook)
        self.assertIn("SG_NadeTargetClear(bot);", cook)

    def test_wedgekill_exempts_only_live_enemy_flag_terminal(self) -> None:
        descend = source("slipgate/sg_descend.c")
        wedge = between(
            descend,
            "VectorSubtract(e->s.origin, bot->wedge_org, d);",
            "\n\tVectorSubtract(e->s.origin, bot->stag_org, d);",
        )
        recovery = between(
            wedge,
            "enemy_pressure &&",
            "\n\telse if (SG_AgeOver(bot->wedge_since, 15.0f) &&",
        )
        kill = wedge.index('sg_host.dprint("WEDGEKILL')
        self.assertLess(wedge.index("SG_AttackFlagDirectTouchAuthority(e, team, NULL)"), kill)
        self.assertNotIn("SG_EnemyFlag(team)", recovery)
        self.assertIn("bot->terminal = true;", recovery)
        self.assertIn("bot->commit_link = -1;", recovery)
        self.assertIn("return -1;", recovery)
        self.assertEqual(wedge.count("Cmd_Kill_f(e);"), 1)


if __name__ == "__main__":
    unittest.main()
