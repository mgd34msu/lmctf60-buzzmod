#!/usr/bin/env python3
"""Focused checks for the single RUNE contract and generated policy APIs."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def _load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


GENERATOR = _load_module("gen_rune_contracts", ROOT / "tools" / "gen_rune_contracts.py")
GENERATED = _load_module(
    "rune_contracts_generated", ROOT / "tools" / "rune_contracts_generated.py"
)


class RuneContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = GENERATOR.load_document(ROOT / "slipgate" / "rune_actions.json")
        cls.pins = json.loads(
            (ROOT / "tests/fixtures/rune_action_pins.json").read_text(
                encoding="utf-8"
            )
        )

    def test_contract_has_one_action_range_and_no_discriminators(self):
        contract = self.document["contract"]
        self.assertEqual(
            {"first_action", "last_action", "descriptor"},
            set(contract["action_range"]),
        )
        self.assertEqual(
            {"contract", "wire_diagnostics", "display"}, set(self.document)
        )
        self.assertEqual(
            {"descriptor_fragments", "controllers", "action_requirements"},
            set(contract["mechanism_contract"]),
        )
        for action in contract["actions"]:
            self.assertEqual(17, len(action))
        for requirement in contract["mechanism_contract"]["action_requirements"]:
            for plan in requirement["plans"]:
                self.assertEqual({"controller"}, set(plan))

    def test_descriptor_membership_matches_action_admission(self):
        contract = self.document["contract"]
        sections = {}
        for field in contract["action_range"]["descriptor"].split(";"):
            if "=" in field:
                name, values = field.split("=", 1)
                sections[name] = values

        def ids(name):
            values = sections[name]
            if not values:
                return set()
            return {int(value.rsplit(":", 1)[1]) for value in values.split(",")}

        supported = {
            action["id"] for action in contract["actions"] if action["runtime_supported"]
        }
        requirements = contract["mechanism_contract"]["action_requirements"]
        admitted = {entry["action"] for entry in requirements if entry["admitted"]}
        planless = {
            entry["action"]
            for entry in requirements
            if entry["admitted"] and not entry["plan_required"]
        }
        action_ids = {action["id"] for action in contract["actions"]}
        self.assertEqual(supported, admitted)
        self.assertEqual(supported, ids("runtime"))
        self.assertEqual(action_ids - supported, ids("disabled"))
        self.assertEqual(planless, ids("planless"))

    def test_pinned_contract_and_mechanism_digests(self):
        action_crc, action_sha = GENERATOR.rune_action_contract_digests(self.document)
        mechanism_crc, mechanism_sha = GENERATOR.mechanism_contract_digests(
            self.document
        )
        self.assertEqual(self.pins["action_contract_crc32"], f"{action_crc:08x}")
        self.assertEqual(self.pins["action_contract_sha256"], action_sha)
        self.assertEqual(self.pins["mechanism_contract_crc32"], f"{mechanism_crc:08x}")
        self.assertEqual(self.pins["mechanism_contract_sha256"], mechanism_sha)
        self.assertEqual(
            self.pins["action_contract_descriptor"],
            GENERATED.RUNE_ACTION_CONTRACT_DESCRIPTOR,
        )
        self.assertEqual(action_crc, GENERATED.RUNE_ACTION_CONTRACT_CRC32)
        self.assertEqual(action_sha, GENERATED.RUNE_ACTION_CONTRACT_SHA256)
        self.assertEqual(mechanism_crc, GENERATED.RUNE_MECHANISM_CONTRACT_CRC32)
        self.assertEqual(mechanism_sha, GENERATED.RUNE_MECHANISM_CONTRACT_SHA256)

    def test_generated_outputs_match_the_contract_document(self):
        self.assertEqual(
            (ROOT / "slipgate/sg_action_contract.generated.h").read_bytes(),
            GENERATOR.render_c(self.document),
        )
        self.assertEqual(
            (ROOT / "tools/rune_contracts_generated.py").read_bytes(),
            GENERATOR.render_python(self.document),
        )
        self.assertEqual(
            self.pins["action_symbols"],
            [entry["symbol"] for entry in self.document["contract"]["actions"]],
        )

    def test_controller_flags_and_action_plan_admission_fail_closed(self):
        expected_flags = dict(self.pins["controller_flags"])
        self.assertEqual(14, GENERATED.RL_TRAIN)
        self.assertEqual(8, GENERATED.SG_MECHANISM_CONTROLLER_TRAIN)
        self.assertEqual(9, GENERATED.SG_MECHANISM_CONTROLLER_TRAIN_SHOOT)
        self.assertEqual(11, GENERATED.SG_MECHANISM_CONTROLLER_TRAIN_STATION)
        self.assertEqual(28, GENERATED.mechanism_controller_plan_flags(
            GENERATED.SG_MECHANISM_CONTROLLER_TRAIN_SHOOT
        ))
        for controller, flags in expected_flags.items():
            self.assertEqual(flags, GENERATED.mechanism_controller_plan_flags(controller))
        self.assertIsNone(GENERATED.mechanism_controller_plan_flags(99))

        allowed = {
            (GENERATED.RL_LIFT, GENERATED.SG_MECHANISM_CONTROLLER_PLATFORM),
            (GENERATED.RL_TELEPORT, GENERATED.SG_MECHANISM_CONTROLLER_TELEPORT),
            (GENERATED.RL_DOOR, GENERATED.SG_MECHANISM_CONTROLLER_AUTO_DOOR),
            (GENERATED.RL_DOOR, GENERATED.SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR),
            (GENERATED.RL_BUTTON_DOOR, GENERATED.SG_MECHANISM_CONTROLLER_BUTTON_DOOR),
            (GENERATED.RL_BUTTON_DOOR, GENERATED.SG_MECHANISM_CONTROLLER_RELAY_DOOR),
            (GENERATED.RL_BUTTON_DOOR, GENERATED.SG_MECHANISM_CONTROLLER_TIMED_VAULT),
            (GENERATED.RL_PUSH, GENERATED.SG_MECHANISM_CONTROLLER_PUSH),
            (GENERATED.RL_TRAIN, GENERATED.SG_MECHANISM_CONTROLLER_TRAIN),
            (GENERATED.RL_TRAIN, GENERATED.SG_MECHANISM_CONTROLLER_TRAIN_SHOOT),
            (GENERATED.RL_TRAIN, GENERATED.SG_MECHANISM_CONTROLLER_TRAIN_STATION),
        }
        for action in range(GENERATED.ACTION_COUNT):
            for controller in expected_flags:
                self.assertEqual(
                    (action, controller) in allowed,
                    GENERATED.action_mechanism_plan_allowed(action, controller),
                )
        self.assertFalse(GENERATED.action_mechanism_plan_allowed("door", 1))
        self.assertFalse(GENERATED.action_mechanism_plan_allowed(GENERATED.RL_DOOR, 99))
        self.assertTrue(
            GENERATED.action_mechanism_admitted(GENERATED.RL_DOOR_HOOK)
        )
        self.assertFalse(
            GENERATED.action_mechanism_plan_required(GENERATED.RL_DOOR_HOOK)
        )

    def test_chain_hook_is_planless_wire_only_with_secondary_control(self):
        action = GENERATED.action_contract(GENERATED.RL_CHAIN_HOOK)
        self.assertTrue(GENERATED.is_runtime_supported(GENERATED.RL_CHAIN_HOOK))
        self.assertTrue(
            GENERATED.action_mechanism_admitted(GENERATED.RL_CHAIN_HOOK)
        )
        self.assertFalse(
            GENERATED.action_mechanism_plan_required(GENERATED.RL_CHAIN_HOOK)
        )
        self.assertEqual(GENERATED.RL_HOOK, action["effective_suffix"])
        self.assertEqual(
            GENERATED.RLSCP_HOOK_CONTROL,
            action["secondary_control_policy"],
        )
        self.assertEqual(GENERATED.RLMP_NONE, action["mechanism_policy"])

        for mutate in (
            lambda row: row.__setitem__("mechanism_policy", 1),
            lambda row: row.__setitem__("mode_mask", 3),
            lambda row: row.__setitem__("secondary_control_policy", 0),
        ):
            malformed = copy.deepcopy(self.document)
            mutate(malformed["contract"]["actions"][GENERATED.RL_CHAIN_HOOK])
            with self.assertRaises(GENERATOR.ContractError):
                GENERATOR.validate_document(malformed)

        malformed = copy.deepcopy(self.document)
        malformed["contract"]["actions"][GENERATED.RL_DOOR_HOOK][
            "secondary_control_policy"
        ] = GENERATED.RLSCP_HOOK_CONTROL
        with self.assertRaises(GENERATOR.ContractError):
            GENERATOR.validate_document(malformed)

        malformed = copy.deepcopy(self.document)
        malformed["contract"]["actions"][GENERATED.RL_RUN][
            "secondary_control_policy"
        ] = GENERATED.RLSCP_HOOK_CONTROL
        with self.assertRaises(GENERATOR.ContractError):
            GENERATOR.validate_document(malformed)

    def test_wire_diagnostics_are_current(self):
        symbols = [entry["symbol"] for entry in self.document["wire_diagnostics"]]
        self.assertEqual(self.pins["wire_diagnostic_symbols"], symbols)
        self.assertEqual(26, len(symbols))
        self.assertEqual(symbols, [entry["symbol"] for entry in GENERATED.WIRE_DIAGNOSTICS])

    def test_canonicalization_ignores_object_order_and_whitespace(self):
        rearranged = {
            "display": copy.deepcopy(self.document["display"]),
            "wire_diagnostics": copy.deepcopy(self.document["wire_diagnostics"]),
            "contract": dict(reversed(list(self.document["contract"].items()))),
        }
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "contract.json"
            path.write_text(json.dumps(rearranged, indent=2), encoding="utf-8")
            round_tripped = GENERATOR.load_document(path)
        self.assertEqual(
            GENERATOR.rune_action_contract_bytes(self.document),
            GENERATOR.rune_action_contract_bytes(round_tripped),
        )

    def test_contract_changes_fail_validation_or_change_the_digest(self):
        changed = copy.deepcopy(self.document)
        changed["contract"]["mechanism_contract"]["descriptor_fragments"][-1] += "x"
        self.assertNotEqual(
            GENERATOR.mechanism_contract_digests(self.document),
            GENERATOR.mechanism_contract_digests(changed),
        )

        malformed = copy.deepcopy(self.document)
        malformed["contract"]["action_range"]["first_action"] = 1
        with self.assertRaises(GENERATOR.ContractError):
            GENERATOR.validate_document(malformed)

    def test_generated_c_api_uses_neutral_contract_names(self):
        header = (ROOT / "slipgate/sg_action_contract.generated.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("SG_RUNE_ACTION_CONTRACT_CRC32", header)
        self.assertIn("SG_RUNE_MECHANISM_CONTRACT_CRC32", header)
        self.assertIn("SG_MechanismControllerPlanFlags", header)
        self.assertIn("SG_ActionMechanismPlanAllowed(int action, uint16_t controller)", header)


if __name__ == "__main__":
    unittest.main()
