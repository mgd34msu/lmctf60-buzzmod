#!/usr/bin/env python3
"""Focused equivalence and fail-closed tests for the rune action registry."""

from __future__ import annotations

import contextlib
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import tempfile
import unittest
import zlib


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
            (ROOT / "tests" / "fixtures" / "rune_action_pins.json").read_text(
                encoding="utf-8"
            )
        )

    def _round_trip_document(self, document, *, indent=None):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "contract.json"
            path.write_text(
                json.dumps(document, indent=indent, ensure_ascii=False),
                encoding="utf-8",
            )
            return GENERATOR.load_document(path)

    def _load_raw(self, raw):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "contract.json"
            path.write_bytes(raw)
            return GENERATOR.load_document(path)

    def test_legacy_and_appended_identifier_pins(self):
        contract = self.document["contract"]
        self.assertEqual(self.pins["schema_version"], self.document["schema_version"])
        self.assertEqual(
            self.pins["action_symbols"],
            [entry["symbol"] for entry in contract["actions"]],
        )
        self.assertEqual(
            self.pins["provenance_symbols"],
            [entry["symbol"] for entry in contract["provenances"]],
        )
        self.assertEqual(
            self.pins["mode_symbols"],
            [entry["symbol"] for entry in contract["modes"]],
        )
        for fixture_key, action_key in (
            ("runtime_supported", "runtime_supported"),
            ("controller_revisions", "controller_revision"),
            ("effective_suffixes", "effective_suffix"),
            ("provenance_masks", "provenance_mask"),
            ("mode_masks", "mode_mask"),
            ("trait_masks", "trait_mask"),
            ("preopen_anchor_policies", "preopen_mechanism_anchor_policy"),
            ("ride_anchor_policies", "ride_mechanism_anchor_policy"),
        ):
            self.assertEqual(
                self.pins[fixture_key],
                [entry[action_key] for entry in contract["actions"]],
            )
        self.assertEqual(
            self.pins["descriptor_rows"],
            [
                [entry[field] for field in self.pins["descriptor_fields"]]
                for entry in contract["actions"]
            ],
        )
        reasons = {entry["id"]: entry["symbol"] for entry in contract["reasons"]}
        for reason_id, symbol in self.pins["reason_pins"]:
            self.assertEqual(symbol, reasons[reason_id])

    def test_wire_and_proof_law_pins(self):
        wire = self.document["contract"]["wire"]
        fixture = self.pins["wire"]
        for key in (
            "magic", "version", "little_endian_required", "header_bytes",
            "seed_bytes", "link_bytes",
        ):
            self.assertEqual(fixture[key], wire[key])
        proof = self.document["contract"]["proof_law"]
        self.assertEqual(fixture["pmove_substep_ms"], proof["pmove_substep_ms"])
        self.assertEqual(fixture["server_frame_ms"], proof["server_frame_ms"])
        self.assertEqual(1, proof["host_physics_id_min"])
        self.assertEqual((1, 32767), (proof["gravity_min"], proof["gravity_max"]))
        self.assertEqual(800, proof["maxvelocity_min"])
        self.assertEqual(0, proof["funky_gravity_required"])
        for key, value in self.pins["proof_law"].items():
            self.assertEqual(value, proof[key])
            self.assertEqual(value, getattr(GENERATED, f"RUNE_PROOF_{key.upper()}"))

    def test_canonicalization_ignores_whitespace_and_object_key_order(self):
        reordered = {
            "display": copy.deepcopy(self.document["display"]),
            "wire_diagnostics": copy.deepcopy(self.document["wire_diagnostics"]),
            "contract": dict(reversed(list(self.document["contract"].items()))),
            "schema_version": self.document["schema_version"],
        }
        pretty = self._round_trip_document(reordered, indent=4)
        self.assertEqual(
            GENERATOR.canonical_contract_bytes(self.document),
            GENERATOR.canonical_contract_bytes(pretty),
        )
        self.assertEqual(
            GENERATOR.contract_digests(self.document),
            GENERATOR.contract_digests(pretty),
        )

    def test_display_and_wire_diagnostic_messages_are_outside_action_crc(self):
        display_change = copy.deepcopy(self.document)
        display_change["display"]["actions"][0]["color"] = "#010203"
        display_change["display"]["reasons"][0]["message"] = "success"
        self.assertEqual(
            GENERATOR.contract_digests(self.document),
            GENERATOR.contract_digests(display_change),
        )

        diagnostic_change = copy.deepcopy(self.document)
        diagnostic_change["wire_diagnostics"][2]["message"] = "read failed"
        self.assertEqual(
            GENERATOR.contract_digests(self.document),
            GENERATOR.contract_digests(diagnostic_change),
        )
        self.assertNotEqual(
            GENERATOR.render_c(self.document),
            GENERATOR.render_c(diagnostic_change),
        )
        self.assertNotEqual(
            GENERATOR.render_python(self.document),
            GENERATOR.render_python(diagnostic_change),
        )

        semantic_change = copy.deepcopy(self.document)
        semantic_change["contract"]["actions"][0]["controller_revision"] = 2
        with self.assertRaises(GENERATOR.ContractError):
            GENERATOR.contract_digests(semantic_change)
        before = GENERATOR._canonical_semantic_bytes(self.document)
        after = GENERATOR._canonical_semantic_bytes(semantic_change)
        self.assertNotEqual(zlib.crc32(before), zlib.crc32(after))
        self.assertNotEqual(hashlib.sha256(before).digest(), hashlib.sha256(after).digest())

    def test_wire_diagnostic_ids_and_symbols_are_append_only_pins(self):
        expected = self.pins["wire_diagnostics"]
        self.assertEqual(
            expected,
            [
                [entry["id"], entry["symbol"], entry["message"]]
                for entry in self.document["wire_diagnostics"]
            ],
        )

        reordered = copy.deepcopy(self.document)
        reordered["wire_diagnostics"][0:2] = reversed(
            reordered["wire_diagnostics"][0:2]
        )
        with self.assertRaises(GENERATOR.ContractError):
            GENERATOR.validate_document(reordered)

        swapped_symbols = copy.deepcopy(self.document)
        swapped_symbols["wire_diagnostics"][0]["symbol"], swapped_symbols[
            "wire_diagnostics"
        ][1]["symbol"] = (
            swapped_symbols["wire_diagnostics"][1]["symbol"],
            swapped_symbols["wire_diagnostics"][0]["symbol"],
        )
        with self.assertRaises(GENERATOR.ContractError):
            GENERATOR.validate_document(swapped_symbols)

        appended_without_reviewed_pin = copy.deepcopy(self.document)
        appended_without_reviewed_pin["wire_diagnostics"].append(
            {"id": 27, "symbol": "RLW_UNREVIEWED", "message": "unreviewed"}
        )
        with self.assertRaises(GENERATOR.ContractError):
            GENERATOR.validate_document(appended_without_reviewed_pin)

    def test_strict_json_rejects_duplicate_keys_floats_and_bad_utf8(self):
        malformed = (
            ("duplicate", b'{"schema_version":1,"schema_version":1}'),
            ("float", b'{"value":1.0}'),
            ("nan", b'{"value":NaN}'),
            ("utf8", b'\xff'),
            ("huge_integer", b'{"value":' + b"1" * 5000 + b'}'),
            ("deep", b"[" * 2000 + b"0" + b"]" * 2000),
        )
        for label, raw in malformed:
            with self.subTest(label=label), self.assertRaises(GENERATOR.ContractError):
                self._load_raw(raw)

        direct_float = copy.deepcopy(self.document)
        direct_float["contract"]["proof_law"]["pmove_substep_ms"] = 25.0
        with self.assertRaises(GENERATOR.ContractError):
            GENERATOR.validate_document(direct_float)

    def test_sorted_ids_symbols_masks_suffixes_and_mode_anchors_fail_closed(self):
        mutations = []

        reordered = copy.deepcopy(self.document)
        reordered["contract"]["actions"][0:2] = reversed(
            reordered["contract"]["actions"][0:2]
        )
        mutations.append(reordered)

        duplicate_id = copy.deepcopy(self.document)
        duplicate_id["contract"]["actions"][1]["id"] = 0
        mutations.append(duplicate_id)

        swapped_symbols = copy.deepcopy(self.document)
        swapped_symbols["contract"]["actions"][0]["symbol"], swapped_symbols[
            "contract"
        ]["actions"][1]["symbol"] = (
            swapped_symbols["contract"]["actions"][1]["symbol"],
            swapped_symbols["contract"]["actions"][0]["symbol"],
        )
        mutations.append(swapped_symbols)

        missing_id = copy.deepcopy(self.document)
        del missing_id["contract"]["actions"][4]
        del missing_id["display"]["actions"][4]
        mutations.append(missing_id)

        bad_mask = copy.deepcopy(self.document)
        bad_mask["contract"]["actions"][0]["provenance_mask"] |= 1 << 7
        mutations.append(bad_mask)

        hook_provenance_broadened = copy.deepcopy(self.document)
        hook_provenance_broadened["contract"]["actions"][3]["provenance_mask"] = 15
        mutations.append(hook_provenance_broadened)

        run_endpoint_broadened = copy.deepcopy(self.document)
        run_endpoint_broadened["contract"]["actions"][0]["endpoint_policy"] = 0
        mutations.append(run_endpoint_broadened)

        drop_price_changed = copy.deepcopy(self.document)
        drop_price_changed["contract"]["actions"][2]["field_bias_ms"] = 151
        mutations.append(drop_price_changed)

        suffix_cycle = copy.deepcopy(self.document)
        suffix_cycle["contract"]["actions"][9]["effective_suffix"] = 10
        suffix_cycle["contract"]["actions"][10]["effective_suffix"] = 9
        mutations.append(suffix_cycle)

        wrong_mode_anchor = copy.deepcopy(self.document)
        wrong_mode_anchor["contract"]["actions"][9][
            "preopen_mechanism_anchor_policy"
        ] = 6
        mutations.append(wrong_mode_anchor)

        compound_bias_not_inherited = copy.deepcopy(self.document)
        compound_bias_not_inherited["contract"]["actions"][11]["field_bias_policy"] = 0
        mutations.append(compound_bias_not_inherited)

        missing_required_symbol = copy.deepcopy(self.document)
        missing_required_symbol["contract"]["traits"][5]["symbol"] = "SG_ACTF_OTHER"
        mutations.append(missing_required_symbol)

        unauthorized_runtime = copy.deepcopy(self.document)
        unauthorized_runtime["contract"]["actions"][9]["runtime_supported"] = 1
        unauthorized_runtime["contract"]["actions"][9]["controller_revision"] = 1
        mutations.append(unauthorized_runtime)

        unknown_schema = copy.deepcopy(self.document)
        unknown_schema["schema_version"] = 2
        mutations.append(unknown_schema)

        for index, mutation in enumerate(mutations):
            with self.subTest(index=index), self.assertRaises(GENERATOR.ContractError):
                GENERATOR.validate_document(mutation)

    def test_generated_metadata_and_effective_suffix_policies(self):
        crc32_value, sha256_value = GENERATOR.contract_digests(self.document)
        self.assertEqual(self.pins["contract_crc32"], f"{crc32_value:08x}")
        self.assertEqual(self.pins["contract_sha256"], sha256_value)
        self.assertEqual(crc32_value, GENERATED.CONTRACT_CRC32)
        self.assertEqual(sha256_value, GENERATED.CONTRACT_SHA256)
        self.assertEqual(12, GENERATED.ACTION_COUNT)
        self.assertEqual(5, GENERATED.PROVENANCE_COUNT)
        self.assertEqual(3, GENERATED.COMPOUND_MODE_COUNT)
        self.assertEqual(7, GENERATED.ACTION_TRAIT_COUNT)
        self.assertEqual(0x7f, GENERATED.ACTION_TRAIT_ALL_MASK)
        self.assertEqual(7, GENERATED.ENDPOINT_POLICY_COUNT)
        self.assertEqual(27, GENERATED.WIRE_DIAGNOSTIC_COUNT)

        self.assertEqual(GENERATED.RL_DROP, GENERATED.effective_suffix(GENERATED.RL_DOOR_DROP))
        self.assertEqual(GENERATED.RL_SWIM, GENERATED.effective_suffix(GENERATED.RL_DOOR_SWIM))
        self.assertEqual(GENERATED.RL_HOOK, GENERATED.effective_suffix(GENERATED.RL_DOOR_HOOK))
        self.assertFalse(
            GENERATED.has_trait(GENERATED.RL_DOOR_DROP, GENERATED.SG_ACTF_BALLISTIC)
        )
        self.assertTrue(
            GENERATED.effective_has_trait(
                GENERATED.RL_DOOR_DROP, GENERATED.SG_ACTF_BALLISTIC
            )
        )
        self.assertTrue(GENERATED.uses_hook_policy(GENERATED.RL_DOOR_HOOK))
        self.assertFalse(GENERATED.uses_hook_policy(GENERATED.RL_DOOR_DROP))
        self.assertEqual(
            self.pins["bias_at_rope_1000"],
            [GENERATED.field_bias_ms(action, 1000) for action in range(12)],
        )

    def test_generated_c_rows_match_python_and_golden_descriptors(self):
        header = (ROOT / "slipgate" / "sg_action_contract.generated.h").read_text(
            encoding="utf-8"
        )
        crc_match = re.search(r"SG_ACTION_CONTRACT_CRC32 0x([0-9a-f]{8})U", header)
        sha_match = re.search(r'SG_ACTION_CONTRACT_SHA256 "([0-9a-f]{64})"', header)
        self.assertIsNotNone(crc_match)
        self.assertIsNotNone(sha_match)
        self.assertEqual(GENERATED.CONTRACT_CRC32, int(crc_match.group(1), 16))
        self.assertEqual(GENERATED.CONTRACT_SHA256, sha_match.group(1))

        rows = []
        for line in header.splitlines():
            stripped = line.strip()
            if not stripped.startswith("X(RL_") or stripped.startswith("X(RLR_"):
                continue
            body = stripped[stripped.index("(") + 1:stripped.rfind(")")]
            parts = body.split(", ")
            self.assertEqual(20, len(parts), stripped)

            def enum_value(token):
                return getattr(GENERATED, token)

            def integer(token):
                return int(token.rstrip("Uu"), 0)

            action_id = integer(parts[1])
            self.assertEqual(action_id, enum_value(parts[0]))
            rows.append([
                integer(parts[2]), enum_value(parts[3]), integer(parts[4]),
                integer(parts[5]), integer(parts[6]), enum_value(parts[7]),
                enum_value(parts[8]), enum_value(parts[9]), enum_value(parts[10]),
                enum_value(parts[11]), enum_value(parts[12]), enum_value(parts[13]),
                enum_value(parts[14]), integer(parts[15]), integer(parts[16]),
            ])
            python_row = GENERATED.action_contract(action_id)
            self.assertEqual(python_row["name"], json.loads(parts[17]))
            self.assertEqual(python_row["short_name"], json.loads(parts[18]))
            self.assertEqual(python_row["color"], json.loads(parts[19]))
        self.assertEqual(self.pins["descriptor_rows"], rows)

    def test_generated_wire_diagnostic_c_and_python_maps_match(self):
        expected = self.pins["wire_diagnostics"]
        header = (ROOT / "slipgate" / "sg_action_contract.generated.h").read_text(
            encoding="utf-8"
        )
        c_rows = []
        for line in header.splitlines():
            stripped = line.strip()
            if not stripped.startswith("X(RLW_"):
                continue
            if stripped.endswith("\\"):
                stripped = stripped[:-1].rstrip()
            symbol, diagnostic_id, message = stripped[2:-1].split(", ", 2)
            c_rows.append([int(diagnostic_id), symbol, json.loads(message)])
        self.assertEqual(expected, c_rows)

        python_rows = [
            [entry["id"], entry["symbol"], entry["message"]]
            for entry in GENERATED.WIRE_DIAGNOSTICS
        ]
        self.assertEqual(expected, python_rows)
        self.assertEqual(
            {entry[0]: entry[1] for entry in expected},
            GENERATED.WIRE_DIAGNOSTIC_SYMBOLS,
        )
        self.assertEqual(
            {entry[0]: entry[2] for entry in expected},
            GENERATED.WIRE_DIAGNOSTIC_MESSAGES,
        )
        for diagnostic_id, symbol, _ in expected:
            self.assertEqual(diagnostic_id, getattr(GENERATED, symbol))

    def test_provenance_modes_and_runtime_support_are_outer_action_policies(self):
        self.assertTrue(GENERATED.allows_provenance(GENERATED.RL_RUN, GENERATED.RL_DECLARED))
        self.assertFalse(GENERATED.allows_provenance(GENERATED.RL_HOOK, GENERATED.RL_DECLARED))
        self.assertTrue(GENERATED.allows_provenance(GENERATED.RL_SWIM, GENERATED.RL_ADJUSTED))
        self.assertTrue(
            GENERATED.allows_provenance(GENERATED.RL_DOOR_HOOK, GENERATED.RL_CONTRACTED)
        )
        self.assertTrue(GENERATED.allows_mode(GENERATED.RL_DOOR_DROP, GENERATED.RLCM_PREOPEN))
        self.assertTrue(GENERATED.allows_mode(GENERATED.RL_DOOR_DROP, GENERATED.RLCM_RIDE))
        self.assertFalse(GENERATED.allows_mode(GENERATED.RL_DOOR_SWIM, GENERATED.RLCM_RIDE))
        self.assertTrue(GENERATED.allows_mode(GENERATED.RL_RUN, GENERATED.RLCM_NONE))

        for action in (GENERATED.RL_ROCKETJUMP, GENERATED.RL_DOOR_DROP,
                       GENERATED.RL_DOOR_SWIM, GENERATED.RL_DOOR_HOOK):
            self.assertFalse(GENERATED.is_runtime_supported(action))
        self.assertTrue(GENERATED.is_runtime_supported(GENERATED.RL_DROP))
        self.assertTrue(
            GENERATED.is_runtime_supported(GENERATED.effective_suffix(GENERATED.RL_DOOR_DROP))
        )
        self.assertFalse(GENERATED.is_runtime_supported(GENERATED.RL_DOOR_DROP))

    def test_generated_python_rejects_bool_nonints_and_huge_enum_values(self):
        for action in (True, False, None, "0"):
            with self.subTest(action=action), self.assertRaises(TypeError):
                GENERATED.action_contract(action)
        with self.assertRaises(ValueError):
            GENERATED.action_contract(12)
        for trait in (True, 0, 3, 128, "1"):
            with self.subTest(trait=trait), self.assertRaises(ValueError):
                GENERATED.has_trait(GENERATED.RL_RUN, trait)

        huge = 10 ** 10000
        self.assertFalse(GENERATED.allows_provenance(GENERATED.RL_RUN, huge))
        self.assertFalse(GENERATED.allows_mode(GENERATED.RL_RUN, huge))
        self.assertFalse(GENERATED.allows_provenance(GENERATED.RL_RUN, True))
        self.assertFalse(GENERATED.allows_mode(GENERATED.RL_RUN, False))
        with self.assertRaises(TypeError):
            GENERATED.allows_mode(True, GENERATED.RLCM_NONE)
        with self.assertRaises(TypeError):
            GENERATED.field_bias_ms(GENERATED.RL_HOOK, True)

    def test_write_check_and_stale_output_detection_are_byte_exact(self):
        with tempfile.TemporaryDirectory() as temporary:
            c_output = Path(temporary) / "generated.h"
            python_output = Path(temporary) / "generated.py"
            common = [
                "--schema", str(ROOT / "slipgate" / "rune_actions.json"),
                "--c-output", str(c_output),
                "--python-output", str(python_output),
            ]
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(0, GENERATOR.main([*common, "--write"]))
                self.assertEqual(0, GENERATOR.main([*common, "--check"]))
            self.assertEqual(0o644, c_output.stat().st_mode & 0o777)
            self.assertEqual(0o644, python_output.stat().st_mode & 0o777)
            c_output.write_bytes(c_output.read_bytes() + b"/* stale */\n")
            errors = io.StringIO()
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
                self.assertEqual(1, GENERATOR.main([*common, "--check"]))
            self.assertIn("stale generated file", errors.getvalue())

    def test_checked_in_outputs_are_current(self):
        stale, _, _ = GENERATOR.check_outputs(
            self.document,
            ROOT / "slipgate" / "sg_action_contract.generated.h",
            ROOT / "tools" / "rune_contracts_generated.py",
        )
        self.assertEqual([], stale)

    def test_malformed_cli_input_is_diagnostic_without_traceback(self):
        with tempfile.TemporaryDirectory() as temporary:
            bad_schema = Path(temporary) / "bad.json"
            bad_schema.write_bytes(b'{"schema_version":1,"schema_version":1}')
            errors = io.StringIO()
            with contextlib.redirect_stderr(errors):
                result = GENERATOR.main([
                    "--schema", str(bad_schema),
                    "--c-output", str(Path(temporary) / "out.h"),
                    "--python-output", str(Path(temporary) / "out.py"),
                    "--check",
                ])
            self.assertEqual(2, result)
            self.assertIn("rune contract error:", errors.getvalue())
            self.assertNotIn("Traceback", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
