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
        latch = source.index("THE LINK LATCH", chooser)
        commitment = source.index("Commitment. The composed surface", latch)

        self.assertLess(chooser, latch)
        self.assertLess(latch, commitment)
        self.assertEqual(source.count("SG_DefenseShiftChoose(&request"), 1)
        self.assertIn(
            "if (!defense_shift_selected && sg_cv.linklatch->value > 0",
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
        self.assertIn('X(defshift, "sg_defshift", "0")',
                      (ROOT / "slipgate/sg_cvars.h").read_text())
        self.assertIn('X(patrol, "sg_patrol", "0.55")',
                      (ROOT / "slipgate/sg_cvars.h").read_text())

    def test_quiet_patrol_is_enabled_and_walk_paced(self) -> None:
        descend = (ROOT / "slipgate/sg_descend.c").read_text()
        move = (ROOT / "slipgate/sg_move.c").read_text()

        self.assertIn("SG_DefensePatrolChoose(cand, nc", descend)
        self.assertIn("tc->patrol_walk = true;", descend)
        self.assertIn("SG_DefensePatrolThrottle(sg_cv.patrol->value)", move)
        self.assertIn("role == SG_ROLE_DEFEND && bot->def_stand", move)

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
