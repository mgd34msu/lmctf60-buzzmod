import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class StrategyCallerIntegrationTest(unittest.TestCase):
    def text(self, path):
        return (ROOT / path).read_text()

    def test_production_build_links_strategy_reducer_and_caller(self):
        for path in ("Makefile", "GNUmakefile"):
            source = self.text(path)
            self.assertIn("slipgate/sg_strategy.o", source)
            self.assertIn("slipgate/sg_strategy_caller.o", source)

    def test_bot_slot_owns_and_initializes_strategy_lifecycle(self):
        header = self.text("slipgate/sg_bot.h")
        client = self.text("sg_client.c")
        self.assertIn("sg_strategy_caller_t strategy;", header)
        self.assertIn("SG_StrategyCallerInit(&bot->strategy)", client)
        self.assertNotIn("tac_role", header)

    def test_reducer_commits_destination_before_tactical_and_link_choice(self):
        source = self.text("sg_arach.c")
        objective = source.index("Think_Objective(bot, &tc)")
        strategy = source.index("StrategyCommitFrame(bot, &tc", objective)
        tactical = source.index("Think_TacticalRoute(bot, &tc)", strategy)
        pick = source.index("Think_PickLink(bot, &tc)", tactical)
        commit = source.index("Think_CommitLink(bot, &tc)", pick)
        self.assertLess(objective, strategy)
        self.assertLess(strategy, tactical)
        self.assertLess(tactical, pick)
        self.assertLess(pick, commit)

    def test_production_lifecycle_pulses_tactical_and_life_interruptions(self):
        source = self.text("sg_arach.c")
        self.assertIn(
            "StrategyInterrupt(bot, false, SG_STRATEGY_BLOCK_CONTROLLER)",
            source,
        )
        self.assertIn(
            "StrategyInterrupt(bot, true, SG_STRATEGY_BLOCK_CONTROLLER)",
            source,
        )
        self.assertIn(
            "StrategyInterrupt(bot, true, "
            "SG_STRATEGY_BLOCK_HOOK_OPPORTUNITY)",
            source,
        )
        self.assertIn("SG_STRATEGY_BLOCK_COMBAT", source)
        self.assertIn("SG_STRATEGY_BLOCK_OBSTRUCTION", source)

    def test_role_objective_and_human_order_feed_typed_proposal(self):
        source = self.text("sg_arach.c")
        chat = self.text("sg_chat.c")
        for goal in (
            "SG_STRATEGY_GOAL_COLLECT_ITEM",
            "SG_STRATEGY_GOAL_ESCORT_CARRIER",
            "SG_STRATEGY_GOAL_INTERCEPT_CARRIER",
            "SG_STRATEGY_GOAL_CARRY_FLAG",
            "SG_STRATEGY_GOAL_RECOVER_FLAG",
            "SG_STRATEGY_GOAL_CAPTURE_FLAG",
        ):
            self.assertIn(goal, source)
        self.assertIn("SG_DESTINATION_WEAPON", source)
        self.assertIn("SG_DESTINATION_POWERUP", source)
        self.assertIn("SG_ChatOrderPrincipal(tc->e)", source)
        self.assertIn("SG_STRATEGY_AUTHORITY_HUMAN", source)
        self.assertIn("return chat_bot[cl].order_from;", chat)


if __name__ == "__main__":
    unittest.main()
