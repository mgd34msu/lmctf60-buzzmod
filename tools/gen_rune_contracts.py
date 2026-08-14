#!/usr/bin/env python3
"""Validate rune_actions.json and generate its C and Python contracts."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import zlib


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SCHEMA = ROOT / "slipgate" / "rune_actions.json"
DEFAULT_C_OUTPUT = ROOT / "slipgate" / "sg_action_contract.generated.h"
DEFAULT_PY_OUTPUT = ROOT / "tools" / "rune_contracts_generated.py"

_SYMBOL_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
_COLOR_RE = re.compile(r"^#[0-9a-fA-F]{6}$")

_PINNED_SCHEMA_VERSION = 1
_PINNED_ENUMS = {
    "actions": {
        0: "RL_RUN", 1: "RL_JUMP", 2: "RL_DROP", 3: "RL_HOOK",
        4: "RL_SWIM", 5: "RL_LIFT", 6: "RL_TELEPORT",
        7: "RL_ROCKETJUMP", 8: "RL_DOOR", 9: "RL_DOOR_DROP",
        10: "RL_DOOR_SWIM", 11: "RL_DOOR_HOOK",
    },
    "provenances": {
        0: "RL_PROVEN", 1: "RL_OBSERVED", 2: "RL_ADJUSTED",
        3: "RL_DECLARED", 4: "RL_CONTRACTED",
    },
    "modes": {0: "RLCM_NONE", 1: "RLCM_PREOPEN", 2: "RLCM_RIDE"},
    "traits": {
        1: "SG_ACTF_OWNS_CONTROL", 2: "SG_ACTF_BALLISTIC",
        4: "SG_ACTF_MAP_MECHANISM", 8: "SG_ACTF_ATOMIC",
        16: "SG_ACTF_DOOR_LEASE", 32: "SG_ACTF_SUPPRESS_LOCALIZATION",
        64: "SG_ACTF_EFFECTIVE_SUFFIX",
    },
    "endpoint_policies": {
        0: "RLEP_ANY", 1: "RLEP_DRY_BOTH", 2: "RLEP_FROM_DRY",
        3: "RLEP_AT_LEAST_ONE_WATER", 4: "RLEP_NOT_BOTH_WATER",
        5: "RLEP_FROM_WATER", 6: "RLEP_WATER_TO_DRY",
    },
    "anchor_policies": {
        0: "RLAP_ZERO", 1: "RLAP_RUN_WAYPOINT", 2: "RLAP_DROP_LIP",
        3: "RLAP_HOOK_CONTROL", 4: "RLAP_WORLD",
        5: "RLAP_TELEPORT_PAD", 6: "RLAP_DOOR_WAIT",
        7: "RLAP_UNSUPPORTED", 8: "RLAP_DOOR_PREOPEN_CONTACT",
        9: "RLAP_DOOR_RIDE_INGRESS_LIP",
    },
    "control_policies": {
        0: "RLCP_RUN", 1: "RLCP_JUMP", 2: "RLCP_DROP",
        3: "RLCP_HOOK", 4: "RLCP_SWIM", 5: "RLCP_DECLARED",
        6: "RLCP_UNSUPPORTED",
    },
    "mechanism_policies": {
        0: "RLMP_NONE", 1: "RLMP_DOOR_WORLD_FIXED_1_8",
    },
    "field_bias_policies": {
        0: "RLFB_NONE", 1: "RLFB_FIXED", 2: "RLFB_ROPE_CVAR",
        3: "RLFB_INHERIT",
    },
    "reasons": {
        0: "RLR_OK", 1: "RLR_UNKNOWN_ACTION", 2: "RLR_ACTION_DISABLED",
        3: "RLR_UNKNOWN_PROVENANCE", 4: "RLR_PROVENANCE_FORBIDDEN",
        5: "RLR_BAD_INDEX", 6: "RLR_SELF_LINK",
        7: "RLR_TOMBSTONE_ENDPOINT", 8: "RLR_BAD_COST",
        9: "RLR_BAD_ENDPOINT_POLICY", 10: "RLR_NONFINITE_ANCHOR",
        11: "RLR_BAD_ANCHOR_POLICY", 12: "RLR_BAD_CONTROL_POLICY",
        13: "RLR_BAD_MODE", 14: "RLR_NONZERO_TAIL",
        15: "RLR_NONZERO_RESERVED", 32: "RLR_BAD_RUN_CONTROL",
        33: "RLR_BAD_JUMP_CONTROL", 34: "RLR_BAD_DROP_CONTROL",
        35: "RLR_BAD_HOOK_CONTROL", 36: "RLR_BAD_SWIM_CONTROL",
        37: "RLR_BAD_DECLARED_CONTROL", 38: "RLR_BAD_TELEPORT_REACH",
        39: "RLR_BAD_DOOR_REACH", 40: "RLR_BAD_MECHANISM_ANCHOR",
        41: "RLR_BAD_SWEEP_CLEAR", 64: "RLR_MECHANISM_UNRESOLVED",
        65: "RLR_MECHANISM_AMBIGUOUS", 66: "RLR_DOOR_TEAM_UNSAFE",
        67: "RLR_APPROACH_REPLAY_FAILED", 68: "RLR_RIDE_REPLAY_FAILED",
        69: "RLR_SUFFIX_REPLAY_FAILED", 70: "RLR_COST_MISMATCH",
        71: "RLR_CLEAR_MISMATCH", 72: "RLR_TOP_WINDOW_SHORT",
        73: "RLR_SUPPORT_MISMATCH", 74: "RLR_UNSUPPORTED_ACTIVATOR",
        96: "RLR_LIVE_SOURCE_MISMATCH", 97: "RLR_LIVE_TOUCH_MISMATCH",
        98: "RLR_LIVE_DOOR_SET_MISMATCH", 99: "RLR_LIVE_SUPPORT_MISMATCH",
        100: "RLR_LIVE_TIMING_MISMATCH", 101: "RLR_LIVE_PERTURBED",
        102: "RLR_RECOVERY_UNSAFE", 103: "RLR_ACTION_TIMEOUT",
    },
}
_PINNED_WIRE = {
    "magic": 0x454E5552, "version": 3, "little_endian_required": 1,
    "header_bytes": 128,
    "seed_bytes": 16, "link_bytes": 44, "map_name_bytes": 64,
    "header_crc_offset": 60, "noncompound_tail_offset": 28,
    "noncompound_tail_bytes": 16, "link_reserved_offset": 43,
    "max_seeds": 32768, "max_links": 262144, "min_cost_ms": 1,
    "max_cost_ms": 30000,
}
_PINNED_PROOF_LAW = {
    "physics_flags_supported": 0, "host_physics_id_min": 1,
    "gravity_min": 1, "gravity_max": 32767,
    "gravity_integral_required": 1, "airaccelerate_zero_required": 1,
    "maxvelocity_min": 800, "funky_gravity_required": 0,
    "pmove_substep_ms": 25, "server_frame_ms": 100,
    "top_window_margin_ms": 100, "door_anchor_scale": 8,
    "world_fixed_scale": 8, "world_fixed_min": -32768,
    "world_fixed_max": 32767, "angle_short_units": 65536,
    "angle_byte_units": 256, "full_turn_degrees": 360,
    "declared_control_marker": 254, "drop_control_marker": 254,
    "damaging_fall_delta": 30, "drop_recovery_radius": 96,
    "drop_recovery_z": 72, "drop_lip_horizontal_min": 2,
    "drop_lip_horizontal_max": 256, "drop_lip_z_fixed": 64,
    "drop_lip_z_tolerance_fixed": 2, "teleport_seed_reach": 128,
    "hook_bolt_speed": 800, "hook_frame_distance": 80,
    "hook_min_ray": 1, "hook_max_ray": 8192,
    "hook_max_abs_pitch_degrees": 89, "hook_control_slack": 24,
    "water_hook_control_marker": 253, "hook_dry_settle_ms": 1000,
    "hook_water_settle_ms": 1250,
    "door_approach_horizontal_max": 320,
    "door_approach_vertical_max": 48,
    "door_egress_horizontal_max": 768,
    "door_egress_vertical_max": 96, "door_team_members_max": 16,
}
_PINNED_RUNTIME_SUPPORT = {
    0: 1, 1: 1, 2: 1, 3: 1, 4: 1, 5: 1, 6: 1, 7: 0,
    8: 1, 9: 0, 10: 0, 11: 0,
}
_PINNED_ACTION_FIELDS = (
    "runtime_supported", "default_provenance", "provenance_mask", "mode_mask",
    "trait_mask", "endpoint_policy", "suffix_anchor_policy",
    "preopen_mechanism_anchor_policy", "ride_mechanism_anchor_policy",
    "control_policy", "mechanism_policy", "effective_suffix",
    "field_bias_policy", "field_bias_ms", "controller_revision",
)
_PINNED_ACTION_ROWS = (
    (1, 0, 15, 1, 0,   1, 1, 0, 0, 0, 0, 0, 0, 0,   1),
    (1, 0, 15, 1, 3,   1, 0, 0, 0, 1, 0, 1, 0, 0,   1),
    (1, 0, 15, 1, 3,   2, 2, 0, 0, 2, 0, 2, 1, 150, 1),
    (1, 0, 1,  1, 1,   4, 3, 0, 0, 3, 0, 3, 2, 0,   1),
    (1, 0, 5,  1, 33,  3, 0, 0, 0, 4, 0, 4, 0, 0,   1),
    (1, 3, 8,  1, 37,  0, 4, 0, 0, 5, 0, 5, 0, 0,   1),
    (1, 3, 8,  1, 37,  0, 5, 0, 0, 5, 0, 6, 0, 0,   1),
    (0, 0, 15, 1, 3,   1, 7, 0, 0, 6, 0, 7, 1, 900, 0),
    (1, 3, 8,  1, 37,  1, 6, 0, 0, 5, 0, 8, 0, 0,   1),
    (0, 4, 16, 6, 125, 2, 2, 8, 9, 2, 1, 2, 3, 0,   0),
    (0, 4, 16, 2, 125, 5, 0, 8, 0, 4, 1, 4, 3, 0,   0),
    (0, 4, 16, 2, 125, 6, 3, 8, 0, 3, 1, 3, 3, 0,   0),
)


class ContractError(ValueError):
    """The registry is malformed or semantically inconsistent."""


def _reject_float(token: str):
    raise ContractError(f"floating-point JSON value is forbidden: {token}")


def _reject_constant(token: str):
    raise ContractError(f"non-finite JSON value is forbidden: {token}")


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ContractError(f"duplicate JSON key: {key!r}")
        result[key] = value
    return result


def load_document(path: Path):
    """Load strict UTF-8 JSON, rejecting duplicate keys and all floats."""
    try:
        raw = path.read_bytes()
        text = raw.decode("utf-8", "strict")
    except (OSError, UnicodeDecodeError) as exc:
        raise ContractError(f"cannot read strict UTF-8 {path}: {exc}") from exc
    try:
        document = json.loads(
            text,
            object_pairs_hook=_unique_object,
            parse_float=_reject_float,
            parse_constant=_reject_constant,
        )
    except ContractError:
        raise
    except (ValueError, RecursionError) as exc:
        raise ContractError(f"invalid JSON in {path}: {exc}") from exc
    try:
        validate_document(document)
    except ContractError:
        raise
    except (KeyError, TypeError, OverflowError, RecursionError) as exc:
        raise ContractError(f"invalid contract structure in {path}: {exc}") from exc
    return document


def _fail(where: str, message: str):
    raise ContractError(f"{where}: {message}")


def _require_dict(value, where: str):
    if not isinstance(value, dict):
        _fail(where, "must be an object")
    return value


def _require_list(value, where: str):
    if not isinstance(value, list):
        _fail(where, "must be an array")
    return value


def _require_keys(value, keys, where: str):
    value = _require_dict(value, where)
    expected = set(keys)
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        _fail(where, f"keys differ (missing={missing}, extra={extra})")
    return value


def _require_int(value, where: str, minimum=None, maximum=None):
    if type(value) is not int:
        _fail(where, "must be an integer")
    if minimum is not None and value < minimum:
        _fail(where, f"must be >= {minimum}")
    if maximum is not None and value > maximum:
        _fail(where, f"must be <= {maximum}")
    return value


def _require_string(value, where: str):
    if not isinstance(value, str):
        _fail(where, "must be a string")
    if any(0xD800 <= ord(char) <= 0xDFFF for char in value):
        _fail(where, "contains an invalid Unicode surrogate")
    return value


def _reject_floats_and_surrogates(value, where="root"):
    if isinstance(value, float):
        _fail(where, "floating-point values are forbidden")
    if isinstance(value, str):
        _require_string(value, where)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            _reject_floats_and_surrogates(item, f"{where}[{index}]")
    elif isinstance(value, dict):
        for key, item in value.items():
            _require_string(key, f"{where} key")
            _reject_floats_and_surrogates(item, f"{where}.{key}")


def _validate_symbol(value, where: str):
    value = _require_string(value, where)
    if not _SYMBOL_RE.fullmatch(value):
        _fail(where, "must be an uppercase C/Python identifier")
    return value


def _validate_enum(entries, where: str, *, key="id", dense=True,
                   power_bits=False, maximum=255):
    entries = _require_list(entries, where)
    values = []
    symbols = []
    for index, entry in enumerate(entries):
        item_where = f"{where}[{index}]"
        _require_keys(entry, (key, "symbol"), item_where)
        values.append(_require_int(entry[key], f"{item_where}.{key}", 0,
                                   maximum))
        symbols.append(_validate_symbol(entry["symbol"],
                                        f"{item_where}.symbol"))
    if values != sorted(values):
        _fail(where, f"must be sorted by explicit {key}")
    if len(values) != len(set(values)):
        _fail(where, f"contains duplicate {key} values")
    if len(symbols) != len(set(symbols)):
        _fail(where, "contains duplicate symbols")
    if dense and values != list(range(len(values))):
        _fail(where, f"must contain every {key} from 0 through {len(values)-1}")
    if power_bits and values != [1 << index for index in range(len(values))]:
        _fail(where, "trait bits must be contiguous powers of two")
    return values, symbols


def _validate_pinned_enum(entries, category: str, key="id"):
    actual = {entry[key]: entry["symbol"] for entry in entries}
    expected = _PINNED_ENUMS[category]
    if actual != expected:
        _fail(
            f"contract.{category}",
            f"symbol/ID pins differ (expected={expected}, actual={actual})",
        )


def _validate_pinned_integer_object(value, expected, where: str):
    _require_keys(value, expected, where)
    for key, pinned in expected.items():
        actual = _require_int(value[key], f"{where}.{key}")
        if actual != pinned:
            _fail(f"{where}.{key}", f"must be pinned to {pinned}")


def _validate_action_cycles(actions):
    by_id = {action["id"]: action for action in actions}
    for action in actions:
        start = action["id"]
        current = start
        seen = set()
        while True:
            if current in seen:
                _fail(f"contract.actions[{start}]", "effective suffix cycle")
            seen.add(current)
            suffix = by_id[current]["effective_suffix"]
            if suffix == current:
                break
            current = suffix


def validate_document(document):
    """Validate the complete schema; return None or raise ContractError."""
    _reject_floats_and_surrogates(document)
    _require_keys(document, ("schema_version", "contract", "display"), "root")
    schema_version = _require_int(document["schema_version"], "schema_version", 1)
    if schema_version != _PINNED_SCHEMA_VERSION:
        _fail("schema_version", f"unsupported schema version {schema_version}")

    contract = _require_keys(
        document["contract"],
        (
            "wire", "proof_law", "provenances", "modes", "traits", "endpoint_policies",
            "anchor_policies", "control_policies", "mechanism_policies",
            "field_bias_policies", "actions", "reasons",
        ),
        "contract",
    )
    _validate_pinned_integer_object(contract["wire"], _PINNED_WIRE,
                                    "contract.wire")
    _validate_pinned_integer_object(contract["proof_law"], _PINNED_PROOF_LAW,
                                    "contract.proof_law")
    enum_names = (
        "provenances", "modes", "endpoint_policies", "anchor_policies",
        "control_policies", "mechanism_policies", "field_bias_policies",
    )
    enum_values = {}
    all_symbols = set()
    for name in enum_names:
        values, symbols = _validate_enum(contract[name], f"contract.{name}")
        _validate_pinned_enum(contract[name], name)
        enum_values[name] = set(values)
        for symbol in symbols:
            if symbol in all_symbols:
                _fail(f"contract.{name}", f"globally duplicate symbol {symbol}")
            all_symbols.add(symbol)

    trait_values, trait_symbols = _validate_enum(
        contract["traits"], "contract.traits", key="bit", dense=False,
        power_bits=True, maximum=0x8000,
    )
    _validate_pinned_enum(contract["traits"], "traits", key="bit")
    for symbol in trait_symbols:
        if symbol in all_symbols:
            _fail("contract.traits", f"globally duplicate symbol {symbol}")
        all_symbols.add(symbol)
    trait_mask = 0
    for value in trait_values:
        trait_mask |= value

    reasons = contract["reasons"]
    reason_values, reason_symbols = _validate_enum(
        reasons, "contract.reasons", dense=False,
    )
    _validate_pinned_enum(reasons, "reasons")
    for symbol in reason_symbols:
        if symbol in all_symbols:
            _fail("contract.reasons", f"globally duplicate symbol {symbol}")
        all_symbols.add(symbol)
    if not reason_values or reason_values[0] != 0:
        _fail("contract.reasons", "reason 0 must exist")

    actions = _require_list(contract["actions"], "contract.actions")
    action_keys = (
        "id", "symbol", "runtime_supported", "default_provenance",
        "provenance_mask", "mode_mask", "trait_mask", "endpoint_policy",
        "suffix_anchor_policy", "preopen_mechanism_anchor_policy",
        "ride_mechanism_anchor_policy", "control_policy", "mechanism_policy",
        "effective_suffix", "field_bias_policy", "field_bias_ms",
        "controller_revision",
    )
    action_ids = []
    for index, action in enumerate(actions):
        where = f"contract.actions[{index}]"
        _require_keys(action, action_keys, where)
        action_id = _require_int(action["id"], f"{where}.id", 0, 255)
        action_ids.append(action_id)
        symbol = _validate_symbol(action["symbol"], f"{where}.symbol")
        if symbol in all_symbols:
            _fail(where, f"globally duplicate symbol {symbol}")
        all_symbols.add(symbol)
        for field in action_keys:
            if field not in ("symbol",):
                _require_int(action[field], f"{where}.{field}")
        if action["runtime_supported"] not in (0, 1):
            _fail(f"{where}.runtime_supported", "must be 0 or 1")
    if action_ids != sorted(action_ids):
        _fail("contract.actions", "must be sorted by explicit id")
    if len(action_ids) != len(set(action_ids)):
        _fail("contract.actions", "contains duplicate ids")
    if action_ids != list(range(len(action_ids))):
        _fail("contract.actions", "must contain every id from 0 through max")
    _validate_pinned_enum(actions, "actions")
    for action, expected in zip(actions, _PINNED_ACTION_ROWS):
        actual = tuple(action[field] for field in _PINNED_ACTION_FIELDS)
        if actual != expected:
            _fail(
                f"contract.actions[{action['id']}]",
                f"descriptor differs from reviewed pin (expected={expected}, actual={actual})",
            )

    action_id_set = set(action_ids)
    prov_mask = sum(1 << value for value in enum_values["provenances"])
    mode_mask = sum(1 << value for value in enum_values["modes"])
    by_id = {action["id"]: action for action in actions}
    for index, action in enumerate(actions):
        where = f"contract.actions[{index}]"
        default_prov = action["default_provenance"]
        if default_prov not in enum_values["provenances"]:
            _fail(where, "unknown default provenance")
        if action["provenance_mask"] <= 0 or action["provenance_mask"] & ~prov_mask:
            _fail(where, "invalid provenance mask")
        if not action["provenance_mask"] & (1 << default_prov):
            _fail(where, "default provenance is not allowed")
        if action["mode_mask"] <= 0 or action["mode_mask"] & ~mode_mask:
            _fail(where, "invalid mode mask")
        if action["trait_mask"] < 0 or action["trait_mask"] & ~trait_mask:
            _fail(where, "invalid trait mask")
        references = (
            ("endpoint_policy", "endpoint_policies"),
            ("suffix_anchor_policy", "anchor_policies"),
            ("preopen_mechanism_anchor_policy", "anchor_policies"),
            ("ride_mechanism_anchor_policy", "anchor_policies"),
            ("control_policy", "control_policies"),
            ("mechanism_policy", "mechanism_policies"),
            ("field_bias_policy", "field_bias_policies"),
        )
        for field, enum_name in references:
            if action[field] not in enum_values[enum_name]:
                _fail(where, f"unknown {field}")
        if action["effective_suffix"] not in action_id_set:
            _fail(where, "unknown effective suffix")
        if action["field_bias_ms"] < 0 or action["field_bias_ms"] > 32767:
            _fail(where, "field bias is outside signed-short range")
        if action["controller_revision"] < 0 or action["controller_revision"] > 255:
            _fail(where, "controller revision is outside byte range")
        if bool(action["runtime_supported"]) != (action["controller_revision"] > 0):
            _fail(where, "runtime support and controller revision disagree")
        if action["runtime_supported"] != _PINNED_RUNTIME_SUPPORT[action["id"]]:
            _fail(where, "runtime support differs from the schema-version gate")

        bias = action["field_bias_policy"]
        if bias == 1 and action["field_bias_ms"] <= 0:
            _fail(where, "fixed field bias must be positive")
        if bias != 1 and action["field_bias_ms"] != 0:
            _fail(where, "only fixed field bias may carry milliseconds")
        if bias == 3 and action["effective_suffix"] == action["id"]:
            _fail(where, "inherited bias requires a distinct suffix")

        mechanism = action["mechanism_policy"]
        has_effective_suffix = action["effective_suffix"] != action["id"]
        if bool(action["trait_mask"] & 64) != has_effective_suffix:
            _fail(where, "effective-suffix trait and target disagree")
        if mechanism == 0:
            if (action["preopen_mechanism_anchor_policy"] != 0 or
                    action["ride_mechanism_anchor_policy"] != 0 or
                    action["mode_mask"] != 1):
                _fail(where, "ordinary actions require zero mechanism anchors and NONE mode")
        else:
            required = 1 | 4 | 8 | 16 | 32 | 64
            if action["mode_mask"] & 1 or not action["trait_mask"] & required == required:
                _fail(where, "mechanism action lacks atomic door-lease traits or uses NONE mode")
            preopen_enabled = bool(action["mode_mask"] & (1 << 1))
            ride_enabled = bool(action["mode_mask"] & (1 << 2))
            if (action["preopen_mechanism_anchor_policy"] != (8 if preopen_enabled else 0) or
                    action["ride_mechanism_anchor_policy"] != (9 if ride_enabled else 0)):
                _fail(where, "mode-specific mechanism anchor policy mismatch")

    _validate_action_cycles(actions)
    for action in actions:
        if action["effective_suffix"] == action["id"]:
            continue
        suffix = by_id[action["effective_suffix"]]
        if action["field_bias_policy"] != 3:
            _fail(f"contract.actions[{action['id']}]", "effective suffix must inherit field bias")
        if action["control_policy"] != suffix["control_policy"]:
            _fail(f"contract.actions[{action['id']}]", "does not inherit suffix control policy")
        if action["suffix_anchor_policy"] != suffix["suffix_anchor_policy"]:
            _fail(f"contract.actions[{action['id']}]", "does not inherit suffix anchor policy")

    display = _require_keys(document["display"], ("actions", "reasons"),
                            "display")
    display_actions = _require_list(display["actions"], "display.actions")
    display_ids = []
    for index, entry in enumerate(display_actions):
        where = f"display.actions[{index}]"
        _require_keys(entry, ("id", "name", "short_name", "color"), where)
        display_ids.append(_require_int(entry["id"], f"{where}.id", 0, 255))
        for field in ("name", "short_name", "color"):
            _require_string(entry[field], f"{where}.{field}")
        if not _COLOR_RE.fullmatch(entry["color"]):
            _fail(f"{where}.color", "must be # followed by six hex digits")
    if display_ids != action_ids:
        _fail("display.actions", "must be sorted and cover every action id")

    display_reasons = _require_list(display["reasons"], "display.reasons")
    display_reason_ids = []
    for index, entry in enumerate(display_reasons):
        where = f"display.reasons[{index}]"
        _require_keys(entry, ("id", "message"), where)
        display_reason_ids.append(_require_int(entry["id"], f"{where}.id", 0, 255))
        _require_string(entry["message"], f"{where}.message")
    if display_reason_ids != reason_values:
        _fail("display.reasons", "must be sorted and cover every reason id")


def _canonical_semantic_bytes(document) -> bytes:
    payload = {
        "contract": document["contract"],
        "schema_version": document["schema_version"],
    }
    return json.dumps(
        payload,
        ensure_ascii=True,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("ascii")


def canonical_contract_bytes(document) -> bytes:
    """Validated canonical semantic payload; display metadata is absent."""
    validate_document(document)
    return _canonical_semantic_bytes(document)


def contract_digests(document):
    canonical = canonical_contract_bytes(document)
    return zlib.crc32(canonical) & 0xFFFFFFFF, hashlib.sha256(canonical).hexdigest()


def _c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def _enum_lines(type_name, entries, value_key="id"):
    lines = ["typedef enum", "{"]
    for entry in entries:
        lines.append(f"\t{entry['symbol']} = {entry[value_key]},")
    lines.extend((f"}} {type_name};", ""))
    return lines


def _macro_lines(name, rows):
    lines = [f"#define {name}(X) \\"]
    for index, row in enumerate(rows):
        suffix = " \\" if index + 1 < len(rows) else ""
        lines.append(f"\tX({row}){suffix}")
    lines.append("")
    return lines


def render_c(document, crc32_value=None, sha256_value=None) -> bytes:
    validate_document(document)
    if crc32_value is None or sha256_value is None:
        crc32_value, sha256_value = contract_digests(document)
    contract = document["contract"]
    display = {entry["id"]: entry for entry in document["display"]["actions"]}
    reason_display = {entry["id"]: entry for entry in document["display"]["reasons"]}
    lines = [
        "/* Generated by tools/gen_rune_contracts.py. DO NOT EDIT. */",
        "#ifndef SG_ACTION_CONTRACT_GENERATED_H",
        "#define SG_ACTION_CONTRACT_GENERATED_H",
        "",
        f"#define SG_ACTION_CONTRACT_SCHEMA_VERSION {document['schema_version']}",
        f"#define SG_ACTION_CONTRACT_CRC32 0x{crc32_value:08x}U",
        f"#define SG_ACTION_CONTRACT_SHA256 {_c_string(sha256_value)}",
        f"#define SG_ACTION_COUNT {len(contract['actions'])}",
        f"#define SG_PROVENANCE_COUNT {len(contract['provenances'])}",
        f"#define SG_COMPOUND_MODE_COUNT {len(contract['modes'])}",
        "",
    ]
    for key, value in contract["wire"].items():
        if key == "magic":
            lines.append(f"#define SG_RUNE_V3_{key.upper()} 0x{value:08x}U")
        else:
            lines.append(f"#define SG_RUNE_V3_{key.upper()} {value}")
    lines.append("")
    for key, value in contract["proof_law"].items():
        lines.append(f"#define SG_RUNE_PROOF_{key.upper()} {value}")
    lines.append("")
    enum_specs = (
        ("rune_action_t", contract["actions"], "id"),
        ("rune_provenance_t", contract["provenances"], "id"),
        ("rune_compound_mode_t", contract["modes"], "id"),
        ("sg_action_trait_t", contract["traits"], "bit"),
        ("rune_endpoint_policy_t", contract["endpoint_policies"], "id"),
        ("rune_anchor_policy_t", contract["anchor_policies"], "id"),
        ("rune_control_policy_t", contract["control_policies"], "id"),
        ("rune_mechanism_policy_t", contract["mechanism_policies"], "id"),
        ("rune_field_bias_policy_t", contract["field_bias_policies"], "id"),
        ("rune_reject_reason_t", contract["reasons"], "id"),
    )
    for type_name, entries, key in enum_specs:
        lines.extend(_enum_lines(type_name, entries, key))

    lines.extend((
        "/* X(symbol, id, runtime_supported, default_provenance, provenance_mask,",
        " *   mode_mask, trait_mask, endpoint_policy, suffix_anchor_policy,",
        " *   preopen_mechanism_anchor_policy, ride_mechanism_anchor_policy,",
        " *   control_policy, mechanism_policy,",
        " *   effective_suffix, field_bias_policy, field_bias_ms,",
        " *   controller_revision, name, short_name, color) */",
    ))
    action_rows = []
    for action in contract["actions"]:
        shown = display[action["id"]]
        suffix = contract["actions"][action["effective_suffix"]]["symbol"]
        action_rows.append(
            ", ".join((
                action["symbol"], str(action["id"]), str(action["runtime_supported"]),
                contract["provenances"][action["default_provenance"]]["symbol"],
                f"0x{action['provenance_mask']:04x}U",
                f"0x{action['mode_mask']:02x}U",
                f"0x{action['trait_mask']:04x}U",
                contract["endpoint_policies"][action["endpoint_policy"]]["symbol"],
                contract["anchor_policies"][action["suffix_anchor_policy"]]["symbol"],
                contract["anchor_policies"][action["preopen_mechanism_anchor_policy"]]["symbol"],
                contract["anchor_policies"][action["ride_mechanism_anchor_policy"]]["symbol"],
                contract["control_policies"][action["control_policy"]]["symbol"],
                contract["mechanism_policies"][action["mechanism_policy"]]["symbol"],
                suffix,
                contract["field_bias_policies"][action["field_bias_policy"]]["symbol"],
                str(action["field_bias_ms"]), str(action["controller_revision"]),
                _c_string(shown["name"]), _c_string(shown["short_name"]),
                _c_string(shown["color"]),
            ))
        )
    lines.extend(_macro_lines("SG_ACTION_CONTRACT_ROWS", action_rows))
    reason_rows = [
        f"{reason['symbol']}, {reason['id']}, "
        f"{_c_string(reason_display[reason['id']]['message'])}"
        for reason in contract["reasons"]
    ]
    lines.extend(("/* X(symbol, id, message) */",))
    lines.extend(_macro_lines("SG_RUNE_REJECTION_REASON_ROWS", reason_rows))
    lines.extend(("#endif /* SG_ACTION_CONTRACT_GENERATED_H */", ""))
    return "\n".join(lines).encode("utf-8")


def render_python(document, crc32_value=None, sha256_value=None) -> bytes:
    validate_document(document)
    if crc32_value is None or sha256_value is None:
        crc32_value, sha256_value = contract_digests(document)
    contract = document["contract"]
    display = {entry["id"]: entry for entry in document["display"]["actions"]}
    reason_display = {entry["id"]: entry for entry in document["display"]["reasons"]}
    lines = [
        '"""Generated rune action metadata. DO NOT EDIT."""',
        "",
        f"CONTRACT_SCHEMA_VERSION = {document['schema_version']}",
        f"CONTRACT_CRC32 = 0x{crc32_value:08x}",
        f"CONTRACT_SHA256 = {sha256_value!r}",
        f"ACTION_COUNT = {len(contract['actions'])}",
        f"PROVENANCE_COUNT = {len(contract['provenances'])}",
        f"COMPOUND_MODE_COUNT = {len(contract['modes'])}",
        "",
    ]
    for key, value in contract["wire"].items():
        lines.append(f"RUNE_V3_{key.upper()} = {value}")
    lines.append("")
    for key, value in contract["proof_law"].items():
        lines.append(f"RUNE_PROOF_{key.upper()} = {value}")
    lines.append("")
    for category in (
        "provenances", "modes", "traits", "endpoint_policies",
        "anchor_policies", "control_policies", "mechanism_policies",
        "field_bias_policies", "actions", "reasons",
    ):
        key = "bit" if category == "traits" else "id"
        for entry in contract[category]:
            lines.append(f"{entry['symbol']} = {entry[key]}")
        lines.append("")

    lines.append("ACTIONS = (")
    for action in contract["actions"]:
        shown = display[action["id"]]
        fields = [
            ("id", action["id"]), ("symbol", action["symbol"]),
            ("name", shown["name"]), ("short_name", shown["short_name"]),
            ("color", shown["color"]),
            ("runtime_supported", action["runtime_supported"]),
            ("default_provenance", action["default_provenance"]),
            ("provenance_mask", action["provenance_mask"]),
            ("mode_mask", action["mode_mask"]),
            ("trait_mask", action["trait_mask"]),
            ("endpoint_policy", action["endpoint_policy"]),
            ("suffix_anchor_policy", action["suffix_anchor_policy"]),
            ("preopen_mechanism_anchor_policy",
             action["preopen_mechanism_anchor_policy"]),
            ("ride_mechanism_anchor_policy",
             action["ride_mechanism_anchor_policy"]),
            ("control_policy", action["control_policy"]),
            ("mechanism_policy", action["mechanism_policy"]),
            ("effective_suffix", action["effective_suffix"]),
            ("field_bias_policy", action["field_bias_policy"]),
            ("field_bias_ms", action["field_bias_ms"]),
            ("controller_revision", action["controller_revision"]),
        ]
        body = ", ".join(f"{key!r}: {value!r}" for key, value in fields)
        lines.append(f"    {{{body}}},")
    lines.extend((")", "ACTION_BY_ID = {entry['id']: entry for entry in ACTIONS}",
                  "ACTION_NAMES = {entry['id']: entry['name'] for entry in ACTIONS}",
                  "ACTION_SHORT_NAMES = {entry['id']: entry['short_name'] for entry in ACTIONS}",
                  "ACTION_COLORS = {entry['id']: entry['color'] for entry in ACTIONS}", ""))

    for variable, category in (
        ("PROVENANCE_NAMES", "provenances"),
        ("MODE_NAMES", "modes"),
    ):
        values = ", ".join(
            f"{entry['id']}: {entry['symbol'].split('_', 1)[1]!r}"
            for entry in contract[category]
        )
        lines.append(f"{variable} = {{{values}}}")
    reason_symbols = ", ".join(
        f"{entry['id']}: {entry['symbol']!r}" for entry in contract["reasons"]
    )
    reason_messages = ", ".join(
        f"{entry['id']}: {reason_display[entry['id']]['message']!r}"
        for entry in contract["reasons"]
    )
    lines.extend((
        f"REASON_SYMBOLS = {{{reason_symbols}}}",
        f"REASON_MESSAGES = {{{reason_messages}}}",
        "",
        "def action_contract(action):",
        "    if type(action) is not int:",
        "        raise TypeError('action must be an integer')",
        "    try:",
        "        return ACTION_BY_ID[action]",
        "    except KeyError as exc:",
        "        raise ValueError(f'unknown rune action: {action!r}') from exc",
        "",
        "",
        "def has_trait(action, trait):",
        "    if (type(trait) is not int or trait <= 0 or",
        "            trait & (trait - 1) or trait > SG_ACTF_EFFECTIVE_SUFFIX):",
        "        raise ValueError(f'unknown action trait: {trait!r}')",
        "    return bool(action_contract(action)['trait_mask'] & trait)",
        "",
        "",
        "def effective_has_trait(action, trait):",
        "    # Policy classification only; never dispatch execution through the suffix.",
        "    return has_trait(effective_suffix(action), trait)",
        "",
        "",
        "def is_runtime_supported(action):",
        "    # Support belongs to the outer record, never to its effective suffix.",
        "    return bool(action_contract(action)['runtime_supported'])",
        "",
        "",
        "def allows_provenance(action, provenance):",
        "    entry = action_contract(action)",
        "    return (type(provenance) is int and 0 <= provenance < PROVENANCE_COUNT and",
        "            bool(entry['provenance_mask'] & (1 << provenance)))",
        "",
        "",
        "def allows_mode(action, mode):",
        "    entry = action_contract(action)",
        "    return (type(mode) is int and 0 <= mode < COMPOUND_MODE_COUNT and",
        "            bool(entry['mode_mask'] & (1 << mode)))",
        "",
        "",
        "def effective_suffix(action):",
        "    seen = set()",
        "    current = action",
        "    while True:",
        "        if current in seen:",
        "            raise ValueError('effective suffix cycle')",
        "        seen.add(current)",
        "        suffix = action_contract(current)['effective_suffix']",
        "        if suffix == current:",
        "            return current",
        "        current = suffix",
        "",
        "",
        "def uses_hook_policy(action):",
        "    return effective_suffix(action) == RL_HOOK",
        "",
        "",
        "def field_bias_ms(action, rope_bias_ms):",
        "    if type(rope_bias_ms) is not int:",
        "        raise TypeError('rope_bias_ms must be an integer')",
        "    current = action",
        "    seen = set()",
        "    while True:",
        "        if current in seen:",
        "            raise ValueError('field bias inheritance cycle')",
        "        seen.add(current)",
        "        entry = action_contract(current)",
        "        policy = entry['field_bias_policy']",
        "        if policy == RLFB_NONE:",
        "            return 0",
        "        if policy == RLFB_FIXED:",
        "            return entry['field_bias_ms']",
        "        if policy == RLFB_ROPE_CVAR:",
        "            return max(0, rope_bias_ms)",
        "        if policy != RLFB_INHERIT:",
        "            raise ValueError(f'unknown field bias policy: {policy}')",
        "        current = entry['effective_suffix']",
        "",
    ))
    return "\n".join(lines).encode("utf-8")


def generated_outputs(document):
    crc32_value, sha256_value = contract_digests(document)
    return (
        render_c(document, crc32_value, sha256_value),
        render_python(document, crc32_value, sha256_value),
        crc32_value,
        sha256_value,
    )


def _atomic_write(path: Path, data: bytes):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        os.fchmod(descriptor, 0o644)
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(data)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def write_outputs(document, c_output: Path, python_output: Path):
    c_bytes, python_bytes, crc32_value, sha256_value = generated_outputs(document)
    _atomic_write(c_output, c_bytes)
    _atomic_write(python_output, python_bytes)
    return crc32_value, sha256_value


def check_outputs(document, c_output: Path, python_output: Path):
    c_bytes, python_bytes, crc32_value, sha256_value = generated_outputs(document)
    stale = []
    for path, expected in ((c_output, c_bytes), (python_output, python_bytes)):
        try:
            actual = path.read_bytes()
        except OSError:
            actual = None
        if actual != expected:
            stale.append(path)
    return stale, crc32_value, sha256_value


def _parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    parser.add_argument("--c-output", type=Path, default=DEFAULT_C_OUTPUT)
    parser.add_argument("--python-output", type=Path, default=DEFAULT_PY_OUTPUT)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="atomically replace generated files")
    mode.add_argument("--check", action="store_true", help="byte-compare generated files")
    return parser


def main(argv=None):
    args = _parser().parse_args(argv)
    try:
        document = load_document(args.schema)
        if args.write:
            crc32_value, sha256_value = write_outputs(
                document, args.c_output, args.python_output
            )
        else:
            stale, crc32_value, sha256_value = check_outputs(
                document, args.c_output, args.python_output
            )
            if stale:
                for path in stale:
                    print(f"stale generated file: {path}", file=sys.stderr)
                return 1
    except ContractError as exc:
        print(f"rune contract error: {exc}", file=sys.stderr)
        return 2
    print(f"rune contract crc32={crc32_value:08x} sha256={sha256_value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
