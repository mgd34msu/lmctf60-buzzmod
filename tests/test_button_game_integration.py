#!/usr/bin/env python3
"""Non-vacuous wiring checks for the live func_button transaction boundary."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function_body(path: str, name: str) -> str:
    text = (ROOT / path).read_text(encoding="utf-8")
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", text, re.S)
    if match is None:
        raise AssertionError(f"missing function {name} in {path}")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise AssertionError(f"unterminated function {name} in {path}")


class ButtonGameIntegrationTests(unittest.TestCase):
    def test_stock_callbacks_authorize_before_mutation(self) -> None:
        wait = function_body("g_func.c", "button_wait")
        self.assertLess(wait.index("SG_AuthorizeButtonTargets"),
                        wait.index("G_UseTargets"))
        touch = function_body("g_func.c", "button_touch")
        self.assertLess(touch.index("SG_AuthorizeButtonTouch"),
                        touch.index("self->activator"))
        use = function_body("g_func.c", "button_use")
        self.assertLess(use.index("SG_AuthorizeButtonUse"),
                        use.index("self->activator"))
        killed = function_body("g_func.c", "button_killed")
        self.assertLess(killed.index("SG_AuthorizeButtonShot"),
                        killed.index("self->activator"))

    def test_token_lifecycle_hooks_precede_recycle_and_publication(self) -> None:
        free = function_body("g_utils.c", "G_FreeEdict")
        reset = free.index("SG_ButtonExecutionEntityFreed")
        self.assertLess(reset, free.index("gi.unlinkentity"))
        self.assertLess(reset, free.index("SG_MechCatalogInvalidate"))
        self.assertLess(reset, free.index("memset"))
        level_change = function_body("slipgate/sg_arach.c", "SG_LevelChange")
        self.assertIn("SG_ButtonExecutionLevelReset", level_change)

    def test_live_movement_uses_exact_button_support_gate(self) -> None:
        move = (ROOT / "slipgate/sg_move.c").read_text(encoding="utf-8")
        self.assertEqual(1, len(re.findall(
            r"SG_ButtonExecutionSupportValid\s*\(\s*&mechanism_binding\s*,"
            r"\s*bot\s*,\s*e\s*\)", move)))
        self.assertEqual(1, len(re.findall(
            r"SG_ButtonExecutionAnchor\s*\(\s*&mechanism_binding\s*,",
            move)))
        support = function_body("slipgate/sg_move.c",
                                "SG_ButtonExecutionSupportValid")
        for requirement in (
            "SG_MechanismControllerUsesButton",
            "SG_RuneMechanismBindingCurrent",
            "DoorStep_ButtonTransactionCurrent",
            "RLCM_PREOPEN",
            "RLCM_RIDE",
            "SG_LiftRider",
        ):
            self.assertIn(requirement, support)
        controller = function_body(
            "slipgate/sg_rune_mechanism_catalog.h",
            "SG_MechanismControllerUsesButton",
        )
        self.assertIn("SG_MECHANISM_CONTROLLER_BUTTON_DOOR", controller)
        self.assertIn("SG_MECHANISM_CONTROLLER_TIMED_VAULT", controller)

    def test_oracle_uses_controller_specific_declared_action(self) -> None:
        oracle = (ROOT / "slipgate/sg_oracle.c").read_text(encoding="utf-8")
        conditional = re.compile(
            r"button_controller\s*\?\s*RL_BUTTON_DOOR\s*:\s*RL_DOOR"
        )
        self.assertGreaterEqual(len(conditional.findall(oracle)), 2)
        self.assertGreaterEqual(
            oracle.count("SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR"),
            2,
        )

    def test_loader_button_traces_ignore_only_transient_population(self) -> None:
        contact = function_body("slipgate/sg_oracle.c",
                                "SG_DeclaredButtonDoorContactStatus")
        approach = function_body("slipgate/sg_oracle.c",
                                 "SG_OracleDoorApproach")
        carry = function_body("slipgate/sg_oracle.c",
                              "SG_OracleButtonCarryClear")
        for body in (contact, approach, carry):
            self.assertIn("SG_OracleStablePopulationTrace", body)
        self.assertIn("sg_oracle_loader_replay", contact)
        self.assertIn("sg_oracle_loader_replay", approach)
        stable = function_body("slipgate/sg_oracle.c",
                               "SG_OracleStablePopulationTrace")
        self.assertIn("MASK_PLAYERSOLID", stable)
        self.assertNotIn("~CONTENTS_MONSTER", stable)
        stable_mask = function_body("slipgate/sg_oracle.c",
                                    "SG_OracleStablePopulationTraceMask")
        self.assertIn("SG_OraclePopulationTransientBBox", stable_mask)
        self.assertIn("SOLID_NOT", stable_mask)
        self.assertIn("sg_oracle_population_trace_active", stable_mask)
        phantom = function_body("slipgate/sg_oracle.c", "SG_PhantomTrace")
        self.assertIn("SG_OracleStablePopulationTrace", phantom)
        self.assertIn("sg_oracle_contaminated = true", phantom)


if __name__ == "__main__":
    unittest.main()
