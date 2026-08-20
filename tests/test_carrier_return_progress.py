"""Focused source-and-policy checks for carrier return progress."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
FIELD_INF = 0x3FFFFFFF
DESCEND = (ROOT / "slipgate" / "sg_descend.c").read_text()
MOVE = (ROOT / "slipgate" / "sg_move.c").read_text()
GOAL = (ROOT / "slipgate" / "sg_goal.c").read_text()


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def cycle_route(links, costs, recent, shelved, alternate_only):
    """Mirror the finite RUN selection required by Objective_CycleRoute."""
    here = costs[0]
    choices = []
    for link, target, action, traversal_ms in links:
        cost = costs[target] + traversal_ms
        if action != "RUN" or cost >= FIELD_INF:
            continue
        if not alternate_only:
            choices.append(link)
            continue
        if alternate_only and (cost >= here or link in shelved or target in recent):
            continue
        choices.append((cost, link))
    if not alternate_only:
        return choices[0] if len(choices) == 1 else -1
    return min(choices)[1] if choices else -1


def test_astray_without_cover_never_zeroes_carrier_movement():
    rally = section(MOVE, "/* rallying: get to cover first", "/* on post:")
    assert "qboolean at_cover = role != SG_ROLE_CARRY;" in rally
    assert "else if (role == SG_ROLE_CARRY)" in rally
    assert "tc->rally_hold = false;" in rally
    assert "if (rally_hold && at_cover)" in rally
    assert "else if (rally_hold)" in rally


def test_astray_selects_reachable_standoff_then_holds_only_at_cover():
    cover = section(DESCEND, "static int Carrier_RallyCover", "static qboolean Carrier_LinkShelved")
    assert "goal_field[seed] < 600 || goal_field[seed] >= 2500" in cover
    assert "SG_CanSee(e, SG_Rune()->seeds[seed].origin, 22.0f)" in cover
    assert "if (distance > 1200.0f)" in cover
    hold = section(DESCEND, "if (role == SG_ROLE_CARRY)",
                   "if (bot->railhold_since > level.time")
    assert "rally_hold = cover >= 0;" in hold
    assert "bot->rally_cover = cover;" in hold
    assert "at_cover = (VectorLength(cvd) < 48.0f);" in MOVE


def test_flag_return_clears_carrier_hold_and_resumes_homeward_route():
    hold = section(DESCEND, "if (role == SG_ROLE_CARRY)",
                   "if (bot->railhold_since > level.time")
    release = section(hold, "if (!ours_astray)", "else if (bot->seed")
    assert "bot->rally_cover = -1;" in release
    assert "rally_hold = false;" in release


def test_multiexit_cycle_prefers_nonrecent_unshelved_lower_cost_route():
    assert cycle_route(
        [(8, 1, "RUN", 1100), (9, 2, "RUN", 100)],
        [2000, 300, 1200], recent=set(), shelved=set(), alternate_only=True
    ) == 9
    assert cycle_route(
        [(10, 1, "RUN", 100), (11, 2, "RUN", 100),
         (12, 3, "RUN", 100)],
        [1000, 500, 400, 300], recent={1}, shelved={11}, alternate_only=True
    ) == 12
    select = section(DESCEND, "static int Objective_CycleRoute", "static void StrikeWeaponPurposeClear")
    assert "SG_RouteCandidateGoalMs(goal_field[candidate->to]" in select
    assert "cost >= here || Carrier_LinkShelved(bot, link) ||" in select
    assert "Objective_VisitedRecently(bot, candidate->to, goal_field)" in select


def test_one_exit_cycle_stays_mobile_without_erasing_shelf_evidence():
    links = [(10, 1, "RUN", 100)]
    costs = [1000, 1100]
    assert cycle_route(links, costs, recent={1}, shelved={10}, alternate_only=True) == -1
    assert cycle_route(links, costs, recent={1}, shelved={10}, alternate_only=False) == 10
    pick = section(DESCEND, "int Think_PickLink", "static int Carrier_RallyCover")
    assert "SG_RouteCandidateGoalMs(route_field[neighbor->to]" in pick
    assert "SG_RouteCandidateGoalMs(goal_field[l->to]" in pick
    cycle = section(DESCEND, "int cycle_link = bestlink;",
                    "bot->visit_seed[bot->visit_head]")
    assert "alternate = Objective_CycleRoute(bot, goal_field, true);" in cycle
    assert "alternate = Objective_CycleRoute(bot, goal_field, false);" in cycle
    assert "memset(bot->bl_until, 0, sizeof(bot->bl_until));" not in cycle


def test_exact_route_owners_require_complete_edge_progress():
    weapon = section(DESCEND, "static int StrikeWeaponFilterFreshCandidate",
                     "static void StrikeCommitFreshLink")
    assert "SG_RouteCandidateDescends(route_field[bot->seed]" in weapon


def test_multiexit_cycle_never_reuses_shelved_edge_as_fallback():
    links = [(10, 1, "RUN", 100), (11, 2, "RUN", 100)]
    costs = [1000, 900, 1200]
    assert cycle_route(links, costs, recent={1}, shelved={10}, alternate_only=True) == -1
    assert cycle_route(links, costs, recent={1}, shelved={10}, alternate_only=False) == -1
    assert cycle_route([(10, 1, "RUN", 100)], [1000, 900],
                       recent={1}, shelved={10}, alternate_only=False) == 10
    select = section(DESCEND, "static int Objective_CycleRoute", "static void StrikeWeaponPurposeClear")
    assert "return finite_count == 1 ? finite_link : -1;" in select
    assert "candidate->from != bot->seed" in select
    cycle = section(DESCEND, "int cycle_link = bestlink;",
                    "bot->visit_seed[bot->visit_head]")
    assert "A multi-exit fan with no safe alternate" in cycle
    assert "bestlink = -1;" in cycle


def test_carrier_screen_uses_the_accepted_moving_formation_by_default():
    cvars = (ROOT / "slipgate" / "sg_cvars.h").read_text()
    assert 'X(interpose, "sg_interpose", "3")' in cvars
    interpose = section(
        GOAL, "int interpose_mode = SG_InterposeMode",
        "if (ht && ht->inuse && ht->client && !ht->deadflag)")
    assert "SG_InterposeMode(sg_cv.interpose->value)" in interpose
    assert "if (interpose_mode == 3)" in interpose
    assert "else if (interpose_mode == 2)" in interpose
    assert "SG_InterposeLeadStation(cc," in interpose
    assert "cf[threat_seed]" in interpose
    assert "en11->seed >= SG_Rune()->hdr.num_seeds" in interpose
    assert "e->client - game.clients" not in interpose


def test_carrier_screen_terminal_preserves_the_selected_formation():
    terminal = section(
        MOVE,
        "else if (!have_aim && role == SG_ROLE_ESCORT &&",
        "if (!have_aim && !gf && tc->scoop_mission)",
    )
    assert "!tc->scoop_mission" in terminal
    assert "SG_TerminalFieldSeed(SG_Rune(), goal_field," in terminal
    assert "VectorCopy(SG_Rune()->seeds[terminal_seed].origin" in terminal
    assert "SG_FlagStand" not in terminal

    priority = section(
        MOVE,
        "if (!have_aim && ordered_escort)",
        "else if (!have_aim && role == SG_ROLE_CARRY)",
    )
    assert "VectorCopy(ordered_escort->s.origin, aim);" in priority


def test_immediate_flag_objectives_do_not_apply_route_ribbon():
    ribbon = section(MOVE, "if (sg_cv.ribbon->value > 0.0f", "have_aim = true;")
    assert "SG_RouteRibbonAllowed(role == SG_ROLE_CARRY" in ribbon
    assert "EnemyFlagTouchMissionActive(tc->strike_pressure" in ribbon


if __name__ == "__main__":
    tests = [
        value
        for name, value in sorted(globals().items())
        if name.startswith("test_") and callable(value)
    ]
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.TestSuite(unittest.FunctionTestCase(test) for test in tests)
    )
    raise SystemExit(0 if result.wasSuccessful() else 1)
