from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RuneUpdateIntegrationTest(unittest.TestCase):
    def test_command_keeps_generation_and_adds_explicit_update(self) -> None:
        command = (ROOT / "g_svcmds.c").read_text(encoding="utf-8")
        header = (ROOT / "slipgate" / "sg_rune.h").read_text(
            encoding="utf-8")

        self.assertIn("Rune_Update(const char *mapname)", header)
        self.assertIn("gi.argc() == 2", command)
        self.assertIn("gi.argc() == 3", command)
        self.assertIn('Q_stricmp(gi.argv(2), "update")', command)
        self.assertIn("Rune_Generate(level.mapname);", command)
        self.assertIn("Rune_Update(level.mapname);", command)

    def test_human_stage_is_after_exact_inventories_and_before_local_only(self) -> None:
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
