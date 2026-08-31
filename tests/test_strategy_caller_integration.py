import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class StrategyCallerIntegrationTest(unittest.TestCase):
    def text(self, path):
        return (ROOT / path).read_text()

    def test_production_build_links_strategy_reducer_and_caller(self):
        for path in ("Makefile", "GNUmakefile"):
            source = self.text(path)
            self.assertIn("slipgate/sg_belief.o", source)
            self.assertIn("slipgate/sg_destination.o", source)
            self.assertIn("slipgate/sg_rune_dynamics_model.o", source)
            self.assertIn("slipgate/sg_rune_dynamics_geometry.o", source)
            self.assertIn("slipgate/sg_rune_field_contract.o", source)
            self.assertIn("slipgate/sg_strategy.o", source)
            self.assertIn("slipgate/sg_strategy_caller.o", source)
            self.assertIn("slipgate/sg_strategy_runtime_bridge.o", source)

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

    def test_role_objective_and_human_order_feed_authenticated_queued_plan(self):
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
        self.assertIn("SG_DESTINATION_ARMOR", source)
        self.assertIn("SG_DESTINATION_POWERUP", source)
        self.assertIn("SG_ChatOrderPrincipal(tc->e)", source)
        self.assertIn("SG_STRATEGY_AUTHORITY_HUMAN", source)
        self.assertIn("SG_STRATEGY_DEPENDENCY_SETTLED", source)
        self.assertIn("SG_StrategyRuntimePlanResolve", source)
        self.assertIn("SG_StrategyCallerSubmit", source)
        self.assertIn("SG_StrategyCallerAdvance", source)
        self.assertIn("SG_StrategyCallerSettle", source)
        self.assertIn("SG_STRATEGY_WEAPON_PREPARATION_GOAL_ID", source)
        self.assertIn("SG_STRATEGY_ARMOR_PREPARATION_GOAL_ID", source)
        self.assertIn("SG_STRATEGY_SUPPLY_PREPARATION_GOAL_ID", source)
        self.assertIn("SG_STRATEGY_LEAD_PREPARATION_GOAL_ID", source)
        self.assertIn("StrategyAppendPreparation", source)
        self.assertIn("StrategyActivePlanRequest", source)
        self.assertIn("StrategyFramePlanRequest", source)
        self.assertIn("StrategyRequestMatchesLivePlan", source)
        self.assertNotIn("SG_FieldRootSeed", source)
        self.assertNotIn("SG_Rune()->seeds[root]", source)
        self.assertIn("return chat_bot[cl].order_from;", chat)

    def test_provider_dependency_uses_explicit_execution_fallback(self):
        source = self.text("sg_arach.c")
        commit_start = source.index("static qboolean StrategyCommitFrame")
        commit_end = source.index("static void StrategyInterrupt", commit_start)
        commit = source[commit_start:commit_end]
        self.assertIn("SG_StrategyRuntimeTargetProviderAvailable", commit)
        self.assertIn("StrategyLegacyExecutionFallback", commit)
        self.assertIn("StrategyFramePlanRequest", commit)
        self.assertNotIn("StrategyPlanRequest(bot, tc", commit)
        self.assertLess(
            commit.index("SG_StrategyRuntimeTargetProviderAvailable"),
            commit.index("StrategyFramePlanRequest"),
        )
        self.assertIn(
            "SG_StrategyRuntimeTargetProviderSet(NULL, NULL, NULL, NULL, NULL, NULL)",
            source,
        )

    def test_provider_requires_destination_field_authority_and_order_lifecycle(self):
        bridge = self.text("slipgate/sg_strategy_runtime_bridge.c")
        bridge_header = self.text("slipgate/sg_strategy_runtime_bridge.h")
        caller = self.text("slipgate/sg_strategy_caller.c")
        client = self.text("slipgate/sg_client.c")
        chat = self.text("sg_chat.c")
        self.assertIn("RuntimeDestinationEqual", bridge)
        self.assertIn("sg_strategy_runtime_target_view_t", bridge_header)
        self.assertIn("sg_strategy_runtime_target_authority_fn", bridge_header)
        self.assertIn("!view.opaque", bridge)
        self.assertIn("sg_strategy_runtime_provider_registration_t", bridge)
        self.assertIn("registration.authority(", bridge)
        self.assertIn("registration.release_view", bridge)
        self.assertIn("RuntimeProviderRegistrationCurrent(&registration)", bridge)
        self.assertIn("binding->commitment_id != target->commitment_id", bridge)
        self.assertIn("RuntimeAuthorityEqual(&binding->authority", bridge)
        self.assertIn("binding->goal_id != target->goal_id", bridge)
        self.assertIn("binding->target_id != target->target_id", bridge)
        self.assertIn("binding->destination", bridge)
        self.assertIn("binding->role != target->role", bridge)
        self.assertIn("!binding->execution_field", bridge)
        self.assertIn("binding->accepted_view != view->opaque", bridge)
        self.assertIn("SG_DestinationTerminalValid(binding->terminal)", bridge)
        self.assertIn("SG_FieldHandleValid(binding->field_handle)", bridge)
        self.assertIn("SG_FieldGuidanceValid(binding->guidance)", bridge)
        self.assertIn("SG_LocalizedFieldStateValid(binding->localized)", bridge)
        execution_start = bridge_header.index(
            "typedef struct sg_strategy_runtime_execution_s"
        )
        execution_end = bridge_header.index(
            "typedef struct sg_strategy_runtime_plan_request_s", execution_start
        )
        self.assertNotIn("execution_field", bridge_header[execution_start:execution_end])
        self.assertIn("CallerDestinationEqual", caller)
        self.assertIn("binding->commitment_id != plan->commitment_id", caller)
        self.assertIn("CallerFieldHandleEqual", caller)
        self.assertIn("CallerGuidanceObservation", caller)
        self.assertNotIn("sg_destination_field_t", caller)
        self.assertNotIn("SG_DestinationFieldValid", caller)
        self.assertIn("SG_StrategyCallerCancel", chat)
        self.assertIn("SG_StrategyCallerRelease", chat)
        self.assertIn("SG_StrategyCallerDestroy(&bot->strategy)", client)
        self.assertIn(
            "authority.principal_id = (uint32_t)order_from + 1U;", chat
        )
        expiry_start = chat.index("static void Chat_ExpireOrders")
        expiry_end = chat.index("int SG_ChatOrderedRole", expiry_start)
        expiry = chat[expiry_start:expiry_end]
        self.assertIn("Chat_EndStrategyOrder", expiry)
        self.assertLess(
            expiry.index("Chat_EndStrategyOrder"),
            expiry.index("chat_bot[i].order_from = -1"),
        )

    def test_human_order_release_never_reconstructs_stale_authority(self):
        arach = self.text("slipgate/sg_arach.c")
        chat = self.text("slipgate/sg_chat.c")
        start = chat.index("static void Chat_EndStrategyOrder")
        end = chat.index("static void Chat_Order", start)
        order_end = chat[start:end]

        self.assertNotIn("StrategyReleaseMissingHumanOrder", arach)
        self.assertNotIn("SG_StrategyCallerRelease(", arach)
        self.assertIn("authority.principal_id = (uint32_t)order_from + 1U;",
                      order_end)
        self.assertIn("SG_StrategyCallerRelease(&strategy_bot->strategy",
                      order_end)

    def test_production_request_has_canonical_ordered_prerequisites(self):
        source = self.text("slipgate/sg_arach.c")
        start = source.index("static int StrategyPlanRequest")
        end = source.index("static int StrategyAuthorityEqual", start)
        request = source[start:end]
        self.assertIn("SG_CollectibleArmorTargetField", request)
        self.assertIn("SG_DESTINATION_WEAPON", request)
        self.assertIn("SG_DESTINATION_ARMOR", request)
        self.assertIn("SG_DESTINATION_POWERUP", request)
        self.assertIn("SG_STRATEGY_DEPENDENCY_SETTLED", request)
        self.assertLess(
            request.index("SG_STRATEGY_WEAPON_PREPARATION_GOAL_ID"),
            request.index("SG_STRATEGY_ARMOR_PREPARATION_GOAL_ID"),
        )
        self.assertLess(
            request.index("SG_STRATEGY_ARMOR_PREPARATION_GOAL_ID"),
            request.index("SG_STRATEGY_SUPPLY_PREPARATION_GOAL_ID"),
        )
        self.assertLess(
            request.index("SG_STRATEGY_SUPPLY_PREPARATION_GOAL_ID"),
            request.index("SG_STRATEGY_LEAD_PREPARATION_GOAL_ID"),
        )
        self.assertLess(
            request.index("SG_STRATEGY_LEAD_PREPARATION_GOAL_ID"),
            request.index("SG_STRATEGY_PRIMARY_GOAL_ID"),
        )

    def test_autonomous_reuse_compares_current_semantics_before_refresh(self):
        source = self.text("slipgate/sg_arach.c")
        reusable_start = source.index("static int StrategyPlanReusable")
        reusable_end = source.index("static int StrategyFramePlanRequest", reusable_start)
        reusable = source[reusable_start:reusable_end]
        self.assertIn("StrategyPlanRequest(bot, tc, strike_duty, &candidate)", reusable)
        self.assertIn("StrategyRequestMatchesLivePlan(&candidate", reusable)
        self.assertIn("StrategyGoalSemanticsEqual", source)
        self.assertIn("StrategyGoalRolesEqual", source)
        self.assertIn("StrategyGoalDependenciesMatchLivePlan", source)
        active_start = source.index("static int StrategyActivePlanRequest")
        active_end = source.index("static int StrategyPlanTerminal", active_start)
        active = source[active_start:active_end]
        self.assertIn("request->spec = caller->plan.spec", active)
        self.assertNotIn("tc->goal_field", active)
        self.assertNotIn("execution_field", active)

    def test_reducer_owns_the_post_commit_route(self):
        source = self.text("sg_arach.c")
        commit = source.index("StrategyCommitFrame(bot, &tc", source.index(
            "Think_Objective(bot, &tc)"
        ))
        self.assertNotIn("RouteLocalNormalize(bot, &tc)", source[commit:])

    def test_exact_armor_scratch_cache_is_retired_before_map_storage(self):
        goal = self.text("slipgate/sg_goal.c")
        header = self.text("slipgate/sg_goal.h")
        source = self.text("sg_arach.c")
        level_start = source.index("void SG_LevelChange(void)")
        level_end = source.index("sg_rune = NULL;", level_start)
        level = source[level_start:level_end]

        self.assertIn("SG_CollectibleArmorTargetLevelReset", header)
        self.assertIn("sg_armor_target_level_generation", goal)
        self.assertIn("sg_armor_target_generation[bi]", goal)
        self.assertIn("memset(sg_armor_target_field", goal)
        self.assertIn("SG_CollectibleArmorTargetLevelReset();", level)
        self.assertLess(level.index("SG_RemoveBots();"),
                        level.index("SG_CollectibleArmorTargetLevelReset();"))

    def test_strategy_views_retire_before_owner_storage(self):
        level_source = self.text("sg_arach.c")
        shutdown_source = self.text("g_main.c")
        level_start = level_source.index("void SG_LevelChange(void)")
        level = level_source[level_start:]
        level_clear = level.index(
            "SG_StrategyRuntimeTargetProviderSet(NULL, NULL, NULL, NULL, NULL, NULL)"
        )
        level_retire = level.index(
            "SG_StrategyCallerDestroy(&sg_bots[i].strategy)", level_clear
        )
        level_guard = level.index("SG_CompoundGuardGameLevelReset()", level_retire)
        level_roster = level.index("SG_RemoveBots();", level_clear)
        level_localization = level.index(
            "SG_BotLocalizationProviderSet(NULL)", level_retire
        )
        self.assertLess(level_clear, level_retire)
        self.assertLess(level_retire, level_localization)
        self.assertLess(level_guard, level_roster)

        shutdown_start = shutdown_source.index("void ShutdownGame (void)")
        shutdown = shutdown_source[shutdown_start:]
        shutdown_clear = shutdown.index(
            "SG_StrategyRuntimeTargetProviderSet(NULL, NULL, NULL, NULL, NULL, NULL)"
        )
        shutdown_roster = shutdown.index("SG_RosterStorageReset();", shutdown_clear)
        shutdown_localization = shutdown.index(
            "SG_BotLocalizationProviderSet(NULL)", shutdown_roster
        )
        shutdown_free = shutdown.index("gi.FreeTags (TAG_LEVEL)", shutdown_localization)
        self.assertLess(shutdown_clear, shutdown_roster)
        self.assertLess(shutdown_roster, shutdown_localization)
        self.assertLess(shutdown_localization, shutdown_free)

    def test_failed_strategy_submit_discards_resolved_views(self):
        source = self.text("sg_arach.c")
        start = source.index("static qboolean StrategyCommitFrame")
        end = source.index("static void StrategyInterrupt", start)
        commit = source[start:end]
        resolve = commit.index("SG_StrategyRuntimePlanResolve")
        submit = commit.index("SG_StrategyCallerSubmit", resolve)
        discard = commit.index("SG_StrategyCallerPlanDiscard", submit)
        self.assertLess(resolve, submit)
        self.assertLess(submit, discard)

    def test_late_level_detection_never_calls_lost_view_owner(self):
        source = self.text("sg_arach.c")
        start = source.index("void SG_RunFrame(void)")
        end = source.index("SG_CompoundGuardGameFrame();", start)
        prologue = source[start:end]
        clear = prologue.index(
            "SG_StrategyRuntimeTargetProviderSet(NULL, NULL, NULL, NULL, NULL, NULL)"
        )
        abandon = prologue.index("SG_StrategyCallerOwnerLost", clear)
        level_change = prologue.index("SG_LevelChange();", abandon)
        self.assertLess(clear, abandon)
        self.assertLess(abandon, level_change)


if __name__ == "__main__":
    unittest.main()
