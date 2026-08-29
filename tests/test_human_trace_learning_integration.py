"""Structural and link-level regression checks for v3 human learning."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class HumanTraceLearningIntegrationTest(unittest.TestCase):
    def source(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_01_contract_uses_canonical_dynamics_costs(self) -> None:
        contract = self.source("slipgate/sg_human_trace_learning_contract.h")

        self.assertIn('#include "sg_rune_dynamics_model.h"', contract)
        self.assertIn("SG_RUNE_FIELD_COST_INFINITE", contract)
        self.assertIn("sg_rune_control_fiber_ref_t", contract)
        self.assertIn("sg_rune_kernel_ref_t", contract)
        self.assertIn("uint64_t effective_cost_us", contract)
        deleted_field = "sg_destination" + "_field"
        deleted_infinity = "SG_DESTINATION" + "_FIELD_INF"
        for forbidden in (
                deleted_field, deleted_infinity,
                "SG_HUMAN_TRACE_LEARNING_MAX", "MAX_REGIONS",
                "MAX_CAPABILITIES", "30000", "int32_t delta_ms"):
            self.assertNotIn(forbidden, contract)

    def test_02_contract_coincludes_canonical_learning_headers(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            probe = directory / "coinclude.c"
            probe.write_text(
                '#include "slipgate/sg_learning_contract.h"\n'
                '#include "slipgate/sg_human_trace_learning_contract.h"\n'
                'int coinclude(void) { return 0; }\n',
                encoding="utf-8")
            result = subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Wpedantic", "-Wconversion", "-Wsign-conversion",
                 "-Wshadow", "-Wstrict-prototypes", "-I", str(ROOT),
                 "-c", str(probe), "-o", str(directory / "coinclude.o")],
                cwd=ROOT, text=True, capture_output=True, check=False)

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_03_raw_application_is_not_a_public_or_exported_authority(self) -> None:
        game = self.source("slipgate/sg_human_trace_learning_game.h")
        host = self.source("slipgate/sg_human_trace_learning_host_game.h")
        trace = self.source("slipgate/sg_human_trace.h")
        spool_private = self.source(
            "slipgate/sg_human_trace_learning_spool_private.h"
        )

        for forbidden in ("ApplyPostMatch", "RuntimeInit", "TestApply",
                          "TestRuntime"):
            self.assertNotIn(forbidden, game)
            self.assertNotIn(forbidden, host)
        self.assertIn("SG_HumanTraceLearningHostGamePublishRuntime", host)
        self.assertIn("SG_HumanTraceLearningHostGameWithdrawRuntime", host)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            direct = directory / "direct_apply.c"
            direct.write_text(
                '#include "slipgate/sg_human_trace_learning_game.h"\n'
                'int direct_apply(void) {\n'
                '    return SG_HumanTraceLearningApplyPostMatch(0, 0, 0);\n'
                '}\n', encoding="utf-8")
            rejected = subprocess.run(
                ["cc", "-std=c11", "-Werror",
                 "-Werror=implicit-function-declaration", "-I", str(ROOT),
                 "-c", str(direct), "-o", str(directory / "direct_apply.o")],
                cwd=ROOT, text=True, capture_output=True, check=False)
            library = directory / "libhuman_trace_learning.so"
            subprocess.run(
                ["cc", "-std=c11", "-fvisibility=hidden", "-fPIC", "-shared",
                 "-I", str(ROOT),
                 "-o", str(library),
                 "slipgate/sg_human_trace_learning_contract.c",
                 "slipgate/sg_human_trace_learning.c",
                "slipgate/sg_human_trace_learning_game.c",
                "slipgate/sg_human_trace_learning_consumer.c"],
                cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
            direct_receipt = directory / "direct_receipt.c"
            direct_receipt.write_text(
                '#include "slipgate/sg_human_trace.h"\n'
                'int direct_receipt(void) {\n'
                '    return SG_HumanTraceMarkStoredV3ScopeConsumed(0, 0, 0);\n'
                '}\n', encoding="utf-8")
            receipt_rejected = subprocess.run(
                ["cc", "-std=c11", "-Werror",
                 "-Werror=implicit-function-declaration", "-I", str(ROOT),
                 "-c", str(direct_receipt),
                 "-o", str(directory / "direct_receipt.o")],
                cwd=ROOT, text=True, capture_output=True, check=False)
            spool_library = directory / "libhuman_trace_spool.so"
            subprocess.run(
                ["cc", "-std=c11", "-fPIC", "-shared",
                 "-fvisibility=default", "-I", str(ROOT),
                 "-o", str(spool_library), "slipgate/sg_human_trace.c"],
                cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
            exports = subprocess.run(
                ["nm", "-D", "--defined-only", str(library)], cwd=ROOT,
                text=True, capture_output=True, check=True).stdout
            spool_exports = subprocess.run(
                ["nm", "-D", "--defined-only", str(spool_library)],
                cwd=ROOT, text=True, capture_output=True, check=True).stdout

        self.assertNotEqual(rejected.returncode, 0, rejected.stderr)
        self.assertNotEqual(receipt_rejected.returncode, 0,
                            receipt_rejected.stderr)
        self.assertNotIn("ApplyPostMatch", exports)
        self.assertNotIn("TestApply", exports)
        self.assertNotIn("TestRuntime", exports)
        self.assertIn("SG_HumanTraceLearningConsumerEffectiveKernelCost", exports)
        self.assertNotIn("MarkStoredV3ScopeConsumed", trace)
        self.assertNotIn("SG_HumanTraceMarkStoredV3ScopeConsumed", spool_private)
        self.assertNotIn("SG_HumanTraceStoredV3ScopeConsumed", spool_private)
        self.assertIn("sg_human_trace_v3_scope_acceptance_t", spool_private)
        self.assertIn("SG_HumanTraceVisitAcceptedV3Collection", spool_private)
        self.assertNotIn("uint32_t client_id,", spool_private)
        self.assertNotIn("ScopeConsumed", spool_private)
        self.assertNotIn("MarkAccepted", spool_private)
        self.assertNotIn("MarkStoredV3ScopeConsumed", spool_exports)
        self.assertNotIn("StoredV3ScopeConsumed", spool_exports)
        self.assertNotIn("StoredV3EventsAccepted", spool_exports)
        self.assertNotIn("AcceptedV3ScopeConsumed", spool_exports)
        self.assertNotIn("AcceptedV3SpoolValid", spool_exports)
        self.assertNotIn("AcceptedV3Collection", spool_exports)
        self.assertNotIn("AcceptedV3ScopeView", spool_exports)

    def test_04_host_derives_complete_chronological_v3_evidence(self) -> None:
        host = self.source("slipgate/sg_human_trace_learning_host_game.c")
        hud = self.source("p_hud.c")
        spawn = self.source("g_spawn.c")

        for required in (
                "SG_HumanTraceCompleted",
                "SG_HumanTraceVisitAcceptedV3Collection",
                "LearningHostVisitStored", "LearningHostStageSourceRecord",
                "LearningHostCompleteTraversal",
                "LearningHostTrajectoryElapsedUs",
                "LearningHostIssueAcceptedV3Capability",
                "LearningHostApplyAcceptedV3Capability",
                "LearningHostPublishedForTrace"):
            self.assertIn(required, host)
        self.assertNotIn("SG_HumanTraceVisitStoredV3EventsAccepted", host)
        self.assertIn("!LearningHostSameScope(scope, &source->hook_fire)", host)
        for forbidden in ("command_msec", "SG_HumanTraceLearningTest",
                          "SG_HumanTraceLearningApplyPostMatch"):
            self.assertNotIn(forbidden, host)
        self.assertIn("SG_HumanTraceLearningHostGamePostMatch(NULL)", hud)
        self.assertIn("SG_HumanTraceLearningHostGameReset()", spawn)
        recorder_test = self.source("tests/sg_human_trace_hook_test.c")
        self.assertIn("RunLearningHostIntegration", recorder_test)
        self.assertIn("derived_records != 1U", recorder_test)
        self.assertIn("fixture.costs[0] !=", recorder_test)
        self.assertIn("UINT64_C(300000)", recorder_test)
        self.assertIn("SG_HumanTraceLearningHostGameReset();", recorder_test)
        self.assertIn("RunLearningHostLinearity", recorder_test)
        self.assertIn("RunLearningHostReceiptFailure", recorder_test)
        self.assertIn("fixture.playthroughs[0].used != 0U", recorder_test)

    def test_05_consumer_has_an_exact_neutral_fallback(self) -> None:
        contract = self.source("slipgate/sg_human_trace_learning_contract.c")
        consumer = self.source("slipgate/sg_human_trace_learning_consumer.c")
        regression = self.source("tests/sg_human_trace_learning_test.c")

        self.assertIn("if (!parameters)", contract)
        self.assertIn("*effective_cost_us_out = static_cost_us", contract)
        self.assertIn("SG_HumanTraceLearningEffectiveKernelCost", consumer)
        self.assertIn("NULL, &record.update.key", regression)
        self.assertIn("SG_HumanTraceLearningUpdateTouchesGeometry", regression)
        self.assertIn("TestForgedRangeAndTraceAreRejected", regression)
        self.assertNotIn("sg_destination" + "_field", consumer)

    def test_06_every_supported_build_owns_the_new_objects(self) -> None:
        objects = (
            "slipgate/sg_human_trace_learning_contract.o",
            "slipgate/sg_human_trace_learning.o",
            "slipgate/sg_human_trace_learning_game.o",
            "slipgate/sg_human_trace_learning_consumer.o",
            "slipgate/sg_human_trace_learning_host_game.o",
            "slipgate/sg_human_trace_learning_store.o",
        )
        for makefile in ("GNUmakefile", "Makefile"):
            contents = self.source(makefile)
            for object_name in objects:
                self.assertIn(object_name, contents)
            self.assertIn("human-trace-learning-test", contents)
            self.assertIn("-DSG_HUMAN_TRACE_LEARNING_TEST", contents)
            self.assertNotIn("sg_destination" + "_field", contents)
            self.assertNotIn("sg_rune_learning" + "_consumer", contents)
        for project in ("gravity.vcxproj", "gravity.vcxproj.filters"):
            contents = self.source(project)
            for source_name in (
                    r"slipgate\sg_human_trace_learning_contract.c",
                    r"slipgate\sg_human_trace_learning.c",
                    r"slipgate\sg_human_trace_learning_game.c",
                    r"slipgate\sg_human_trace_learning_consumer.c",
                    r"slipgate\sg_human_trace_learning_host_game.c",
                    r"slipgate\sg_human_trace_learning_store.c"):
                self.assertIn(source_name, contents)

    def test_07_v3_importer_and_learner_share_the_terminal_identity(self) -> None:
        importer = self.source("tools/humantrace.py")
        trace = self.source("slipgate/sg_human_trace.c")
        learner = self.source("slipgate/sg_human_trace_learning.c")

        self.assertIn("lmctf-human-trace-v3", importer)
        self.assertIn("derive_v3_learning_observations", importer)
        self.assertIn("terminal_sha256", importer)
        self.assertIn("SG_HumanTraceVisitAcceptedV3Events", trace)
        self.assertIn("terminal_sha256", learner)
        self.assertNotIn("sg_destination" + "_field", importer)


if __name__ == "__main__":
    unittest.main()
