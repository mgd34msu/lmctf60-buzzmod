from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def compact(source: str) -> str:
    return " ".join(source.split())


class RuneUpdateIntegrationTest(unittest.TestCase):
    def test_command_keeps_generation_and_adds_explicit_update(self) -> None:
        command = (ROOT / "g_svcmds.c").read_text(encoding="utf-8")
        header = (ROOT / "slipgate" / "sg_rune.h").read_text(
            encoding="utf-8")

        self.assertIn("Rune_Update(const char *mapname)", header)
        self.assertIn("gi.argc() == 2", command)
        self.assertIn("gi.argc() == 3", command)
        self.assertIn('Q_stricmp(gi.argv(2), "update")', command)
        self.assertIn("Rune_Generate(level.mapname)", command)
        self.assertIn("Rune_Update(level.mapname)", command)

    def test_successful_rune_commands_set_up_or_defer_by_active_rune(self) -> None:
        command = compact((ROOT / "g_svcmds.c").read_text(encoding="utf-8"))
        rune_start = command.index(
            'else if (Q_stricmp (cmd, "rune") == 0)')
        rune = command[rune_start:]

        for write in ("Rune_Generate(level.mapname)",
                      "Rune_Update(level.mapname)"):
            self.assertIn(f"if ({write}) SG_LevelSetupAfterRuneWrite();",
                          rune)
        self.assertEqual(rune.count("SG_LevelSetupAfterRuneWrite();"), 2)

    def test_autoload_latch_and_ready_publication(self) -> None:
        source = (ROOT / "slipgate" / "sg_arach.c").read_text(
            encoding="utf-8")

        frame_start = source.index("void SG_RunFrame(void)")
        frame_marker = ("/* ---------------------------------------------------------------- "
                        "spawn */")
        frame_end = source.index(frame_marker, frame_start)
        frame = compact(source[frame_start:frame_end])
        attempt_start = frame.index("if (!sg_autoload_attempted)")
        attempt_end = frame.index("SG_CompoundGuardGameFrame();",
                                 attempt_start)
        attempt = frame[attempt_start:attempt_end]

        self.assertIn("sg_autoload_attempted = true;", attempt)
        self.assertEqual(attempt.count("(void)SG_LevelSetup();"), 1)
        self.assertNotIn("sg_bots", attempt)
        self.assertNotIn("Botfill_Frame", attempt)
        self.assertLess(
            attempt.index("sg_autoload_attempted = true;"),
            attempt.index("(void)SG_LevelSetup();"),
        )
        self.assertLess(
            frame.index("(void)SG_LevelSetup();"),
            frame.index("Botfill_Frame();"),
        )

        change_start = source.index("void SG_LevelChange(void)")
        change_end = source.index("#ifdef SG_STRIKE_TRANSITION_TEST_API",
                                 change_start)
        change = compact(source[change_start:change_end])
        self.assertIn("sg_autoload_attempted = false;", change)

        attempt_start = source.index("static qboolean SG_LevelSetupAttempt(void)")
        setup_start = source.index("static qboolean SG_LevelSetupWithSource")
        setup_end = source.index("void SG_LevelSetupAfterRuneWrite(void)",
                                setup_start)
        setup = compact(source[setup_start:setup_end])
        self.assertLess(attempt_start, setup_start)
        setup_attempt = setup.index("SG_LevelSetupAttempt();")
        setup_flush = setup.index("if (sg_host.flush)")
        setup_return = setup.index("return ready;")
        self.assertIn("qboolean ready = SG_LevelSetupAttempt();", setup)
        self.assertIn("slipgate: rune setup terminal", setup)
        self.assertEqual(setup.count("SG_LevelSetupAttempt();"), 1)
        self.assertEqual(setup.count("sg_host.flush();"), 1)
        self.assertLess(setup_attempt, setup_flush)
        self.assertLess(setup_flush, setup_return)

    def test_objective_root_receipt_is_success_only_and_authoritative(self) -> None:
        source = compact(
            (ROOT / "slipgate" / "sg_arach.c").read_text(encoding="utf-8"))
        start = source.index("static qboolean SG_LevelSetupAttempt(void)")
        end = source.index("qboolean SG_LevelSetup(void)", start)
        attempt = source[start:end]
        receipt = ("sg_host.dprint(\"slipgate: objective roots red=%d blue=%d\\n\", "
                   "sg_fields.red_flag_seed, sg_fields.blue_flag_seed);")
        receipt_pos = attempt.index(receipt)
        ready_pos = attempt.index("sg_host.dprint(\"slipgate: rune ready")
        success = attempt[:attempt.rindex("return true;")]
        failure = attempt[attempt.index("fail:"):]

        self.assertEqual(attempt.count(receipt), 1)
        self.assertIn(receipt, success)
        self.assertNotIn(receipt, failure)
        self.assertIn("sg_fields.red_flag_seed", success)
        self.assertIn("sg_fields.blue_flag_seed", success)
        self.assertLess(success.index("Danger_Publish"), receipt_pos)
        self.assertLess(success.index("Caco_Reset();"), receipt_pos)
        self.assertLess(receipt_pos, ready_pos)

    def test_live_mechanism_rebind_drift_is_infrastructure(self) -> None:
        source = compact(
            (ROOT / "slipgate" / "sg_arach.c").read_text(encoding="utf-8"))
        start = source.index("rune_t *Rune_Load(const char *mapname)")
        end = source.index(
            "/* --------------------------------------------------------------- fields */",
            start,
        )
        loader = source[start:end]

        status = "SG_RuneMechanismBindingsStatus(rune, &failure_index)"
        self.assertIn(status, loader)
        self.assertIn(
            "infrastructure = binding_status == SG_RUNE_MECHANISM_BINDINGS_INFRA;",
            loader,
        )
        self.assertIn(
            "failure = binding_status == SG_RUNE_MECHANISM_BINDINGS_ARTIFACT "
            '? "live mechanism binding rejected" '
            ': "live mechanism binding unavailable or drifted";',
            loader,
        )

    def test_rune_write_setup_preserves_active_rune_and_consumes_autoload(self) -> None:
        source = compact(
            (ROOT / "slipgate" / "sg_arach.c").read_text(encoding="utf-8"))
        start = source.index("void SG_LevelSetupAfterRuneWrite(void)")
        end = source.index(
            "/* ----------------------------------------------------------------- body */", start)
        write_setup = source[start:end]

        active_start = write_setup.index("if (sg_rune)")
        active_return = write_setup.index("return;", active_start)
        active = write_setup[active_start:active_return]
        self.assertIn("slipgate: rune written; active rune remains in effect",
                      active)
        self.assertIn("until the next map setup", active)
        self.assertNotIn("sg_setup_failed = false;", active)
        self.assertNotIn("SG_LevelSetup();", active)

        inactive = write_setup[active_return:]
        self.assertNotIn("sg_setup_failed = false;", inactive)
        consume = inactive.index("sg_autoload_attempted = true;")
        setup = inactive.index("(void)SG_LevelSetupWithSource(\"write\");")
        self.assertLess(consume, setup)

    def test_human_stage_order(self) -> None:
        source = (ROOT / "slipgate" / "sg_rune.c").read_text(
            encoding="utf-8")
        closure = source.index("Graph_PruneObjectiveCoreWithClosure")
        body = source[closure:source.index("/* ----------------", closure)]
        relay = body.index("Prove_RelayWallObjectiveClosure")
        core = body.index("Graph_PruneObjectiveCoreTry")
        swim = body.index("Prove_ObjectiveSwimClosure")
        late = body.index("Graph_ProveLatePath")
        prior = body.index("Graph_ApplyLearningEvidence")
        local = body.index("Graph_PruneLocalObjectiveUnion")

        self.assertEqual([relay, core, swim, late, prior, local],
                         sorted([relay, core, swim, late, prior, local]))
        self.assertLess(prior, local)
        self.assertNotIn("Prove_ObjectiveClosure", body)
        apply = source.index("Graph_ApplyLearningEvidence")
        apply_body = source[apply:source.index(
            "Graph_PruneObjectiveCoreWithClosure", apply)]
        self.assertIn("SG_RuneLearningGameUpdate", apply_body)
        adapter = (ROOT / "slipgate" / "sg_rune_learning_game.c").read_text(
            encoding="utf-8")
        update = adapter.index("SG_RuneLearningGameUpdate(")
        update_body = adapter[update:]
        phases = ["LearningSourceRuns", "LearningSourceHooks",
                  "LearningNewRuns", "LearningNewHooks"]
        positions = [update_body.index(phase) for phase in phases]
        self.assertEqual(positions, sorted(positions))

    def test_learning_adapter_cannot_publish_and_install_rechecks_source(self) -> None:
        adapter = (ROOT / "slipgate" / "sg_rune_learning_game.c").read_text(
            encoding="utf-8")
        generator = (ROOT / "slipgate" / "sg_rune.c").read_text(
            encoding="utf-8")
        install = (ROOT / "slipgate" / "sg_rune_install.c").read_text(
            encoding="utf-8")

        self.assertNotIn("Link_Add", adapter)
        self.assertIn("SG_RuneFileInspectExact", generator)
        self.assertIn("source_rune_sha256", generator)
        self.assertLess(
            install.index("revalidate(revalidate_context)"),
            install.index("ops->rename_replace("))

    def test_both_build_dialects_own_production_and_focused_test(self) -> None:
        for name in ("GNUmakefile", "Makefile"):
            source = (ROOT / name).read_text(encoding="utf-8")
            self.assertIn("slipgate/sg_rune_learning.o", source, name)
            self.assertIn("slipgate/sg_rune_learning_game.o", source, name)
            self.assertIn("slipgate/sg_rune_update_source.o", source, name)
            self.assertIn("sg_rune_update_source_test", source, name)
            self.assertIn("rune-update-test", source, name)

        rune = ROOT / "slipgate" / "sg_rune.c"
        self.assertLess(len(rune.read_text(encoding="utf-8").splitlines()),
                        10000)

    def test_update_borrows_or_owns_source_through_revalidation(self) -> None:
        generator = (ROOT / "slipgate" / "sg_rune.c").read_text(
            encoding="utf-8")
        authority = (ROOT / "slipgate" /
                     "sg_rune_authority_game.c").read_text(encoding="utf-8")

        self.assertIn("SG_RuneUpdateSourceAcquire(canonical_mapname,",
                      generator)
        self.assertIn("SG_RuneUpdateSourceRelease(&learning_source_scope)",
                      generator)
        self.assertNotIn("learning_source = SG_Rune();", generator)
        self.assertIn("recheck.source_rune = learning_source;", generator)
        self.assertIn("loaded_source = recheck->source_rune;", authority)

    def test_open_post_inventory_state_runs_fair_late_prover_before_fallback(self) -> None:
        source = (ROOT / "slipgate" / "sg_rune.c").read_text(
            encoding="utf-8")

        prune_begin = source.index("Graph_PruneObjectiveCoreWithClosure(")
        prune_end = source.index("/* ----------------", prune_begin)
        prune = source[prune_begin:prune_end]
        self.assertNotIn("Prove_ObjectiveClosure", prune)
        self.assertIn("Graph_ProveLatePath", prune)
        self.assertLess(prune.index("Graph_ProveLatePath"),
                        prune.index("Graph_ApplyLearningEvidence"))
        self.assertIn("Graph_PruneLocalObjectiveUnion", prune)
        self.assertIn("RUNE_ROUTE_CONTRACT_LOCAL_ONLY", prune)

    def test_late_fallback_does_not_revive_unserialized_actions(self) -> None:
        source = (ROOT / "slipgate" / "sg_rune.c").read_text(
            encoding="utf-8")
        begin = source.index("static int Rune_LateTryBridge")
        end = source.index("static sg_rune_late_completion_t", begin)
        fallback = source[begin:end]

        self.assertNotIn("RL_CHAIN_HOOK", fallback)
        self.assertNotIn("RL_ROCKETJUMP", fallback)


if __name__ == "__main__":
    unittest.main()
