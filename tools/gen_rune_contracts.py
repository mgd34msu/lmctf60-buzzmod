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
DEFAULT_CONTRACT = ROOT / "slipgate" / "rune_actions.json"
DEFAULT_C_OUTPUT = ROOT / "slipgate" / "sg_action_contract.generated.h"
DEFAULT_PY_OUTPUT = ROOT / "tools" / "rune_contracts_generated.py"
_SYMBOL_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
_COLOR_RE = re.compile(r"^#[0-9a-fA-F]{6}$")

_PINNED_ENUMS = {
    "actions": {
        0: "RL_RUN", 1: "RL_JUMP", 2: "RL_DROP", 3: "RL_HOOK",
        4: "RL_SWIM", 5: "RL_LIFT", 6: "RL_TELEPORT",
        7: "RL_ROCKETJUMP", 8: "RL_DOOR", 9: "RL_DOOR_DROP",
        10: "RL_DOOR_SWIM", 11: "RL_DOOR_HOOK",
        12: "RL_BUTTON_DOOR",
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
_PINNED_WIRE_DIAGNOSTICS = {
    0: "RLW_OK",
    1: "RLW_INVALID_ARGUMENT",
    2: "RLW_IO_ERROR",
    3: "RLW_BAD_MAGIC",
    5: "RLW_BAD_HEADER_SIZE",
    6: "RLW_BAD_SEED_SIZE",
    7: "RLW_BAD_LINK_SIZE",
    8: "RLW_BAD_COUNTS",
    9: "RLW_BAD_FILE_SIZE",
    10: "RLW_BAD_HEADER_CRC",
    11: "RLW_BAD_PAYLOAD_CRC",
    12: "RLW_BAD_MAPNAME",
    13: "RLW_MAPNAME_MISMATCH",
    14: "RLW_BAD_ACTION_CONTRACT",
    15: "RLW_BAD_PHYSICS_LAW",
    16: "RLW_IDENTITY_UNAVAILABLE",
    17: "RLW_BSP_CHECKSUM_MISMATCH",
    18: "RLW_ENTITY_CRC_MISMATCH",
    19: "RLW_PHYSICS_ID_MISMATCH",
    20: "RLW_BAD_SEED_RECORD",
    21: "RLW_BAD_LINK_RECORD",
    22: "RLW_DUPLICATE_LINK",
    23: "RLW_BAD_ROUTE_OWNERSHIP",
    24: "RLW_BAD_OBJECTIVE_CORE",
    25: "RLW_ALLOCATION_FAILED",
    26: "RLW_BAD_SIDECAR",
}
_PINNED_WIRE = {
    "magic": 0x454E5552, "little_endian_required": 1,
    "header_bytes": 160,
    "seed_bytes": 16, "link_bytes": 48, "map_name_bytes": 64,
    "header_crc_offset": 60, "header_reserved_offset": 4,
    "noncompound_tail_offset": 28, "noncompound_tail_bytes": 16,
    "link_reserved_offset": 43,
    "max_seeds": 32768, "max_links": 262144, "min_cost_ms": 1,
    "max_cost_ms": 30000,
}
_PINNED_PROOF_LAW = {
    "physics_flags_supported": 0, "host_physics_id_min": 1,
    "gravity_min": 1, "gravity_max": 32767,
    "gravity_integral_required": 1, "airaccelerate_zero_required": 1,
    "maxvelocity_min": 800, "funky_gravity_required": 0,
    "pmove_substep_ms": 25, "server_frame_ms": 100,
    "drop_approach_ms": 2500, "drop_travel_ms": 2000,
    "drop_total_ms": 4500,
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
    8: 1, 9: 0, 10: 1, 11: 0, 12: 1,
}
_FROZEN_BASE_ACTION_COUNT = 12
_PINNED_MECHANISM_CONTROLLERS = {
    0: ("SG_MECHANISM_CONTROLLER_NONE", 0),
    1: ("SG_MECHANISM_CONTROLLER_AUTO_DOOR", 0xD),
    2: ("SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR", 0xD),
    3: ("SG_MECHANISM_CONTROLLER_BUTTON_DOOR", 0xD),
    4: ("SG_MECHANISM_CONTROLLER_RELAY_DOOR", 0),
    5: ("SG_MECHANISM_CONTROLLER_PLATFORM", 0xD),
    6: ("SG_MECHANISM_CONTROLLER_TELEPORT", 0x5),
}
_PINNED_ACTION_MECHANISM_REQUIREMENTS = {
    0: (1, 0, 0, ()),
    1: (1, 0, 1, ()),
    2: (1, 0, 2, ()),
    3: (1, 0, 3, ()),
    4: (1, 0, 4, ()),
    5: (1, 1, 5, ((5, 1),)),
    6: (1, 1, 6, ((6, 1),)),
    7: (0, 0, 7, ()),
    8: (1, 1, 8, ((1, 1), (2, 1))),
    9: (0, 0, 9, ()),
    10: (1, 0, 10, ()),
    11: (0, 0, 11, ()),
    12: (1, 1, 12, ((3, 2),)),
}


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


def _validate_wire_diagnostics(entries):
    entries = _require_list(entries, "wire_diagnostics")
    values = []
    symbols = []
    for index, entry in enumerate(entries):
        where = f"wire_diagnostics[{index}]"
        _require_keys(entry, ("id", "symbol", "message"), where)
        values.append(_require_int(entry["id"], f"{where}.id", 0, 255))
        symbol = _validate_symbol(entry["symbol"], f"{where}.symbol")
        if not symbol.startswith("RLW_"):
            _fail(f"{where}.symbol", "must use the RLW_ namespace")
        symbols.append(symbol)
        message = _require_string(entry["message"], f"{where}.message")
        if not message or "\x00" in message:
            _fail(f"{where}.message", "must be nonempty and contain no NUL")
    if values != sorted(values):
        _fail("wire_diagnostics", "must be sorted by explicit id")
    if len(values) != len(set(values)):
        _fail("wire_diagnostics", "contains duplicate id values")
    if len(symbols) != len(set(symbols)):
        _fail("wire_diagnostics", "contains duplicate symbols")
    actual = {entry["id"]: entry["symbol"] for entry in entries}
    if actual != _PINNED_WIRE_DIAGNOSTICS:
        _fail(
            "wire_diagnostics",
            "symbol/ID pins differ "
            f"(expected={_PINNED_WIRE_DIAGNOSTICS}, actual={actual})",
        )
    return values, symbols


def _validate_action_range(action_range, actions):
    where = "contract.action_range"
    action_range = _require_dict(action_range, where)
    _require_keys(action_range, ("first_action", "last_action", "descriptor"), where)
    first = _require_int(action_range["first_action"], f"{where}.first_action", 0, 255)
    last = _require_int(action_range["last_action"], f"{where}.last_action", 0, 255)
    descriptor = _require_string(action_range["descriptor"], f"{where}.descriptor")
    if first > last or not actions or (first, last) != (actions[0]["id"], actions[-1]["id"]):
        _fail(where, "must cover the complete action registry")
    try:
        encoded = descriptor.encode("ascii", "strict")
    except UnicodeEncodeError as exc:
        raise ContractError(f"{where}.descriptor: must be ASCII") from exc
    if not encoded or b"\x00" in encoded:
        _fail(f"{where}.descriptor", "must be nonempty and contain no NUL")
    return action_range


def _validate_pinned_integer_object(value, expected, where: str):
    _require_keys(value, expected, where)
    for key, pinned in expected.items():
        actual = _require_int(value[key], f"{where}.{key}")
        if actual != pinned:
            _fail(f"{where}.{key}", f"must be pinned to {pinned}")


def _mechanism_descriptor(mechanism_contract) -> str:
    """Join the registry-owned fragments into the exact wire descriptor."""

    fragments = _require_list(
        mechanism_contract["descriptor_fragments"],
        "contract.mechanism_contract.descriptor_fragments",
    )
    if not fragments:
        _fail(
            "contract.mechanism_contract.descriptor_fragments",
            "must not be empty",
        )
    checked = []
    for index, fragment in enumerate(fragments):
        where = f"contract.mechanism_contract.descriptor_fragments[{index}]"
        fragment = _require_string(fragment, where)
        try:
            fragment.encode("ascii", "strict")
        except UnicodeEncodeError as exc:
            raise ContractError(f"{where}: must be ASCII") from exc
        if not fragment or "\x00" in fragment:
            _fail(where, "must be nonempty and contain no NUL")
        checked.append(fragment)
    return "".join(checked)


def _validate_mechanism_contract(value, actions):
    """Validate the mechanism descriptor and action-plan registry."""

    where = "contract.mechanism_contract"
    value = _require_keys(
        value,
        ("descriptor_fragments", "controllers",
         "action_requirements"),
        where,
    )

    descriptor = _mechanism_descriptor(value)
    descriptor_crc = zlib.crc32(descriptor.encode("ascii")) & 0xFFFFFFFF

    controllers = _require_list(value["controllers"], f"{where}.controllers")
    actual_controllers = {}
    for index, entry in enumerate(controllers):
        entry_where = f"{where}.controllers[{index}]"
        _require_keys(entry, ("id", "symbol", "plan_flags"), entry_where)
        controller = _require_int(
            entry["id"], f"{entry_where}.id", 0, 0xFFFF
        )
        symbol = _validate_symbol(entry["symbol"], f"{entry_where}.symbol")
        flags = _require_int(
            entry["plan_flags"], f"{entry_where}.plan_flags", 0, 0xFFFF
        )
        if controller in actual_controllers:
            _fail(entry_where, "duplicate controller id")
        actual_controllers[controller] = (symbol, flags)
    if list(actual_controllers) != list(range(len(actual_controllers))):
        _fail(f"{where}.controllers", "ids must be sorted and dense from zero")
    if actual_controllers != _PINNED_MECHANISM_CONTROLLERS:
        _fail(
            f"{where}.controllers",
            "controller IDs, symbols, or plan flags differ from reviewed pins",
        )

    requirements = _require_list(
        value["action_requirements"], f"{where}.action_requirements"
    )
    actual_requirements = {}
    for index, entry in enumerate(requirements):
        entry_where = f"{where}.action_requirements[{index}]"
        _require_keys(
            entry,
            ("action", "admitted", "plan_required", "link_policy_action",
             "plans"),
            entry_where,
        )
        action = _require_int(entry["action"], f"{entry_where}.action", 0, 255)
        admitted = _require_int(
            entry["admitted"], f"{entry_where}.admitted", 0, 1
        )
        required = _require_int(
            entry["plan_required"], f"{entry_where}.plan_required", 0, 1
        )
        policy_action = _require_int(
            entry["link_policy_action"],
            f"{entry_where}.link_policy_action", 0, 255,
        )
        plans = _require_list(entry["plans"], f"{entry_where}.plans")
        pairs = []
        for plan_index, plan in enumerate(plans):
            plan_where = f"{entry_where}.plans[{plan_index}]"
            _require_keys(plan, ("controller",), plan_where)
            controller = _require_int(
                plan["controller"], f"{plan_where}.controller", 1, 0xFFFF
            )
            if (controller not in actual_controllers or
                    actual_controllers[controller][1] == 0):
                _fail(plan_where, "controller is not executable")
            pairs.append(controller)
        if pairs != sorted(set(pairs)):
            _fail(f"{entry_where}.plans", "pairs must be sorted and unique")
        if required != bool(pairs):
            _fail(entry_where, "plan_required must match whether plans exist")
        if not admitted and (required or pairs):
            _fail(entry_where, "unadmitted action cannot own a plan")
        if action in actual_requirements:
            _fail(entry_where, "duplicate action")
        actual_requirements[action] = (
            admitted, required, policy_action, tuple(pairs)
        )

    if list(actual_requirements) != list(range(len(actions))):
        _fail(
            f"{where}.action_requirements",
            "must be sorted and cover every action exactly once",
        )
    for action in actions:
        admitted = actual_requirements[action["id"]][0]
        if admitted != action["runtime_supported"]:
            _fail(
                f"{where}.action_requirements[{action['id']}].admitted",
                "must match the action runtime-supported gate",
            )
    return descriptor


def _validate_drop_timing_law(proof_law):
    where = "contract.proof_law"
    server_frame_ms = _require_int(
        proof_law["server_frame_ms"], f"{where}.server_frame_ms", 1
    )
    timings = {}
    for key in ("drop_approach_ms", "drop_travel_ms", "drop_total_ms"):
        value = _require_int(proof_law[key], f"{where}.{key}", 1)
        if value % server_frame_ms != 0:
            _fail(
                f"{where}.{key}",
                f"must be a multiple of server_frame_ms ({server_frame_ms})",
            )
        timings[key] = value
    if timings["drop_approach_ms"] + timings["drop_travel_ms"] != timings[
        "drop_total_ms"
    ]:
        _fail(
            where,
            "drop_approach_ms + drop_travel_ms must equal drop_total_ms",
        )


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
    """Validate the complete contract document; return None or raise ContractError."""
    _reject_floats_and_surrogates(document)
    _require_keys(
        document,
        ("contract", "wire_diagnostics", "display"),
        "root",
    )

    contract = _require_keys(
        document["contract"],
        (
            "action_range", "mechanism_contract", "wire", "proof_law", "provenances", "modes", "traits", "endpoint_policies",
            "anchor_policies", "control_policies", "mechanism_policies",
            "field_bias_policies", "actions", "reasons",
        ),
        "contract",
    )
    _validate_pinned_integer_object(contract["wire"], _PINNED_WIRE,
                                    "contract.wire")
    _validate_pinned_integer_object(contract["proof_law"], _PINNED_PROOF_LAW,
                                    "contract.proof_law")
    _validate_drop_timing_law(contract["proof_law"])
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
        if action["runtime_supported"] != _PINNED_RUNTIME_SUPPORT[action["id"]]:
            _fail(where, "runtime support differs from the contract gate")

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

    _validate_action_range(contract["action_range"], actions)
    _validate_mechanism_contract(contract["mechanism_contract"], actions)

    _, wire_diagnostic_symbols = _validate_wire_diagnostics(
        document["wire_diagnostics"]
    )
    for symbol in wire_diagnostic_symbols:
        if symbol in all_symbols:
            _fail("wire_diagnostics", f"globally duplicate symbol {symbol}")
        all_symbols.add(symbol)

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
    payload = {"contract": document["contract"]}
    return json.dumps(payload, ensure_ascii=True, allow_nan=False, sort_keys=True,
                      separators=(",", ":")).encode("ascii")


def canonical_contract_bytes(document) -> bytes:
    validate_document(document)
    return _canonical_semantic_bytes(document)


def contract_digests(document):
    canonical = canonical_contract_bytes(document)
    return zlib.crc32(canonical) & 0xFFFFFFFF, hashlib.sha256(canonical).hexdigest()


def rune_action_contract_bytes(document) -> bytes:
    validate_document(document)
    contract = document["contract"]
    payload = {
        "actions": contract["actions"],
        "action_range": contract["action_range"],
        "mechanism_requirements": contract["mechanism_contract"]["action_requirements"],
    }
    return json.dumps(payload, ensure_ascii=True, allow_nan=False, sort_keys=True,
                      separators=(",", ":")).encode("ascii")


def rune_action_contract_digests(document):
    canonical = rune_action_contract_bytes(document)
    return zlib.crc32(canonical) & 0xFFFFFFFF, hashlib.sha256(canonical).hexdigest()


def mechanism_contract_descriptor(document) -> str:
    """Return the validated exact mechanism descriptor."""

    validate_document(document)
    return _mechanism_descriptor(document["contract"]["mechanism_contract"])


def mechanism_contract_digests(document):
    descriptor = mechanism_contract_descriptor(document).encode("ascii")
    return (
        zlib.crc32(descriptor) & 0xFFFFFFFF,
        hashlib.sha256(descriptor).hexdigest(),
    )


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


def _c_mechanism_policy_lines(mechanism_contract, actions):
    controllers = mechanism_contract["controllers"]
    requirements = mechanism_contract["action_requirements"]
    controller_by_id = {entry["id"]: entry for entry in controllers}
    action_by_id = {entry["id"]: entry for entry in actions}
    lines = _enum_lines("sg_mechanism_controller_t", controllers, "id")
    requirement_rows = []
    plan_rows = []
    for requirement in requirements:
        action_symbol = action_by_id[requirement["action"]]["symbol"]
        policy_symbol = action_by_id[requirement["link_policy_action"]]["symbol"]
        requirement_rows.append(f"{action_symbol}, {requirement['admitted']}, {requirement['plan_required']}, {policy_symbol}")
        for plan in requirement["plans"]:
            plan_rows.append(f"{action_symbol}, {controller_by_id[plan['controller']]['symbol']}")
    lines.extend(("/* X(action, admitted, plan_required, link_policy_action) */",))
    lines.extend(_macro_lines("SG_ACTION_MECHANISM_REQUIREMENT_ROWS", requirement_rows))
    lines.extend(("/* X(action, controller) */",))
    lines.extend(_macro_lines("SG_ACTION_MECHANISM_PLAN_ROWS", plan_rows))
    admitted = " ".join(f"case {action_by_id[x['action']]['symbol']}:" for x in requirements if x['admitted'])
    required = " ".join(f"case {action_by_id[x['action']]['symbol']}:" for x in requirements if x['plan_required'])
    lines.extend(("static inline int SG_ActionMechanismAdmitted(int action)", "{", "\tswitch (action)", "\t{", f"\t{admitted} return 1;", "\tdefault: return 0;", "\t}", "}", "", "static inline int SG_ActionMechanismPlanRequired(int action)", "{", "\tswitch (action)", "\t{", f"\t{required} return 1;", "\tdefault: return 0;", "\t}", "}", "", "static inline int SG_ActionMechanismLinkPolicyAction(int action)", "{", "\tswitch (action)", "\t{"))
    for req in requirements:
        lines.append(f"\tcase {action_by_id[req['action']]['symbol']}: return {action_by_id[req['link_policy_action']]['symbol']};")
    lines.extend(("\tdefault: return -1;", "\t}", "}", "", "static inline int SG_MechanismControllerPlanFlags(uint16_t controller)", "{", "\tswitch (controller)", "\t{"))
    for controller in controllers:
        lines.append(f"\tcase {controller['symbol']}: return {controller['plan_flags']};")
    lines.extend(("\tdefault: return 0;", "\t}", "}", "", "static inline int SG_ActionMechanismPlanAllowed(int action, uint16_t controller)", "{", "\tswitch (action)", "\t{"))
    for req in requirements:
        if req['plans']:
            clauses = [f"controller == {controller_by_id[p['controller']]['symbol']}" for p in req['plans']]
            lines.append(f"\tcase {action_by_id[req['action']]['symbol']}: return " + " || ".join(clauses) + ";")
    lines.extend(("\tdefault: return 0;", "\t}", "}", ""))
    return lines


def _python_mechanism_policy_lines(mechanism_contract):
    controllers = mechanism_contract["controllers"]
    requirements = mechanism_contract["action_requirements"]
    lines = [f"{item['symbol']} = {item['id']}" for item in controllers]
    lines.extend(("", "MECHANISM_CONTROLLERS = ("))
    for item in controllers:
        lines.append(f"    {{'id': {item['id']}, 'symbol': {item['symbol']!r}, 'plan_flags': {item['plan_flags']}}},")
    lines.extend((")", "MECHANISM_CONTROLLER_BY_ID = {entry['id']: entry for entry in MECHANISM_CONTROLLERS}", "", "ACTION_MECHANISM_REQUIREMENTS = ("))
    for req in requirements:
        plans = tuple(plan['controller'] for plan in req['plans'])
        lines.append(f"    {{'action': {req['action']}, 'admitted': {req['admitted']}, 'plan_required': {req['plan_required']}, 'link_policy_action': {req['link_policy_action']}, 'plans': {plans!r}}},")
    lines.extend((")", "ACTION_MECHANISM_REQUIREMENT_BY_ID = {entry['action']: entry for entry in ACTION_MECHANISM_REQUIREMENTS}", "", "def action_mechanism_admitted(action):", "    entry = ACTION_MECHANISM_REQUIREMENT_BY_ID.get(action) if type(action) is int else None", "    return bool(entry and entry['admitted'])", "", "def action_mechanism_plan_required(action):", "    entry = ACTION_MECHANISM_REQUIREMENT_BY_ID.get(action) if type(action) is int else None", "    return bool(entry and entry['plan_required'])", "", "def action_mechanism_link_policy_action(action):", "    entry = ACTION_MECHANISM_REQUIREMENT_BY_ID.get(action) if type(action) is int else None", "    return None if entry is None else entry['link_policy_action']", "", "def mechanism_controller_plan_flags(controller):", "    entry = MECHANISM_CONTROLLER_BY_ID.get(controller) if type(controller) is int else None", "    return None if entry is None else entry['plan_flags']", "", "def action_mechanism_plan_allowed(action, controller):", "    if type(action) is not int or type(controller) is not int:", "        return False", "    entry = ACTION_MECHANISM_REQUIREMENT_BY_ID.get(action)", "    return bool(entry and controller in entry['plans'])", ""))
    return lines


def render_c(document, crc32_value=None, sha256_value=None) -> bytes:
    validate_document(document)
    if crc32_value is None or sha256_value is None:
        crc32_value, sha256_value = contract_digests(document)
    rune_crc32_value, rune_sha256_value = (
        rune_action_contract_digests(document)
    )
    mechanism_crc32, mechanism_sha256 = mechanism_contract_digests(document)
    contract = document["contract"]
    mechanism_contract = contract["mechanism_contract"]
    mechanism_descriptor = _mechanism_descriptor(mechanism_contract)
    wire_diagnostics = document["wire_diagnostics"]
    trait_all_mask = sum(entry["bit"] for entry in contract["traits"])
    display = {entry["id"]: entry for entry in document["display"]["actions"]}
    reason_display = {entry["id"]: entry for entry in document["display"]["reasons"]}
    lines = [
        "/* Generated by tools/gen_rune_contracts.py. DO NOT EDIT. */",
        "#ifndef SG_ACTION_CONTRACT_GENERATED_H",
        "#define SG_ACTION_CONTRACT_GENERATED_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define SG_RUNE_ACTION_CONTRACT_CRC32 0x{rune_crc32_value:08x}U",
        f"#define SG_RUNE_ACTION_CONTRACT_SHA256 "
        f"{_c_string(rune_sha256_value)}",
        "#define SG_RUNE_ACTION_CONTRACT_DESCRIPTOR "
        f"{_c_string(contract['action_range']['descriptor'])}",
                f"#define SG_MECHANISM_CONTRACT_CRC32 0x{mechanism_crc32:08x}U",
        f"#define SG_MECHANISM_CONTRACT_SHA256 {_c_string(mechanism_sha256)}",
        "#define SG_MECHANISM_CONTRACT_DESCRIPTOR "
        f"{_c_string(mechanism_descriptor)}",
        "#define SG_RUNE_MECHANISM_CONTRACT_CRC32 "
        "SG_MECHANISM_CONTRACT_CRC32",
        "#define SG_RUNE_MECHANISM_CONTRACT_SHA256 "
        "SG_MECHANISM_CONTRACT_SHA256",
        "#define SG_RUNE_MECHANISM_CONTRACT_DESCRIPTOR "
        "SG_MECHANISM_CONTRACT_DESCRIPTOR",
        f"#define SG_ACTION_COUNT {len(contract['actions'])}",
        f"#define SG_PROVENANCE_COUNT {len(contract['provenances'])}",
        f"#define SG_COMPOUND_MODE_COUNT {len(contract['modes'])}",
        f"#define SG_ACTION_TRAIT_COUNT {len(contract['traits'])}",
        f"#define SG_ACTION_TRAIT_ALL_MASK 0x{trait_all_mask:04x}U",
        f"#define SG_ENDPOINT_POLICY_COUNT {len(contract['endpoint_policies'])}",
        f"#define SG_RUNE_WIRE_DIAGNOSTIC_COUNT {len(wire_diagnostics)}",
        "",
    ]
    rune_range = contract["action_range"]
    rune_last = contract["actions"][rune_range["last_action"]]["symbol"]
    lines.extend((
        "#define SG_RUNE_HEADER_RESERVED_OFFSET 4U",
        f"#define SG_RUNE_WIRE_ACTION_FIRST {rune_range['first_action']}",
        f"#define SG_RUNE_WIRE_ACTION_MAX {rune_last}",
        f"#define SG_RUNE_WIRE_ACTION_COUNT "
        f"{rune_range['last_action'] - rune_range['first_action'] + 1}",
        "",
    ))
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
        ("rune_wire_diagnostic_t", wire_diagnostics, "id"),
    )
    for type_name, entries, key in enum_specs:
        lines.extend(_enum_lines(type_name, entries, key))

    lines.extend(_c_mechanism_policy_lines(
        mechanism_contract, contract["actions"]
    ))

    lines.extend((
        "/* X(symbol, id, runtime_supported, default_provenance, provenance_mask,",
        " *   mode_mask, trait_mask, endpoint_policy, suffix_anchor_policy,",
        " *   preopen_mechanism_anchor_policy, ride_mechanism_anchor_policy,",
        " *   control_policy, mechanism_policy,",
        " *   effective_suffix, field_bias_policy, field_bias_ms,",
        " *   name, short_name, color) */",
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
                str(action["field_bias_ms"]),
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
    wire_diagnostic_rows = [
        f"{entry['symbol']}, {entry['id']}, {_c_string(entry['message'])}"
        for entry in wire_diagnostics
    ]
    lines.extend(("/* X(symbol, id, message) */",))
    lines.extend(
        _macro_lines("SG_RUNE_WIRE_DIAGNOSTIC_ROWS", wire_diagnostic_rows)
    )
    lines.extend(("#endif /* SG_ACTION_CONTRACT_GENERATED_H */", ""))
    return "\n".join(lines).encode("utf-8")


def render_python(document, crc32_value=None, sha256_value=None) -> bytes:
    validate_document(document)
    if crc32_value is None or sha256_value is None:
        crc32_value, sha256_value = contract_digests(document)
    rune_crc32_value, rune_sha256_value = rune_action_contract_digests(
        document
    )
    mechanism_crc32, mechanism_sha256 = mechanism_contract_digests(document)
    contract = document["contract"]
    mechanism_contract = contract["mechanism_contract"]
    mechanism_descriptor = _mechanism_descriptor(mechanism_contract)
    wire_diagnostics = document["wire_diagnostics"]
    trait_all_mask = sum(entry["bit"] for entry in contract["traits"])
    display = {entry["id"]: entry for entry in document["display"]["actions"]}
    reason_display = {entry["id"]: entry for entry in document["display"]["reasons"]}
    lines = [
        '"""Generated rune contract metadata. DO NOT EDIT."""',
        "",
        f"RUNE_ACTION_CONTRACT_CRC32 = 0x{rune_crc32_value:08x}",
        f"RUNE_ACTION_CONTRACT_SHA256 = {rune_sha256_value!r}",
        "RUNE_ACTION_CONTRACT_CANONICAL = "
        f"{rune_action_contract_bytes(document).decode('ascii')!r}",
        "RUNE_ACTION_CONTRACT_DESCRIPTOR = "
        f"{contract['action_range']['descriptor']!r}",
        f"RUNE_MECHANISM_CONTRACT_CRC32 = 0x{mechanism_crc32:08x}",
        f"RUNE_MECHANISM_CONTRACT_SHA256 = {mechanism_sha256!r}",
        f"RUNE_MECHANISM_CONTRACT_DESCRIPTOR = {mechanism_descriptor!r}",
        f"ACTION_COUNT = {len(contract['actions'])}",
        f"PROVENANCE_COUNT = {len(contract['provenances'])}",
        f"COMPOUND_MODE_COUNT = {len(contract['modes'])}",
        f"ACTION_TRAIT_COUNT = {len(contract['traits'])}",
        f"ACTION_TRAIT_ALL_MASK = {trait_all_mask}",
        f"ENDPOINT_POLICY_COUNT = {len(contract['endpoint_policies'])}",
        f"WIRE_DIAGNOSTIC_COUNT = {len(wire_diagnostics)}",
        "",
    ]
    rune_range = contract["action_range"]
    lines.extend((
        "RUNE_HEADER_RESERVED_OFFSET = 4",
        "RUNE_WIRE_ACTION_RANGE = "
        f"({rune_range['first_action']}, {rune_range['last_action']})",
        "",
    ))
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

    for entry in wire_diagnostics:
        lines.append(f"{entry['symbol']} = {entry['id']}")
    lines.append("")

    lines.extend(_python_mechanism_policy_lines(mechanism_contract))

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
        "WIRE_DIAGNOSTICS = (",
    ))
    for entry in wire_diagnostics:
        body = ", ".join(
            f"{key!r}: {entry[key]!r}" for key in ("id", "symbol", "message")
        )
        lines.append(f"    {{{body}}},")
    lines.extend((
        ")",
        "WIRE_DIAGNOSTIC_BY_ID = {entry['id']: entry for entry in WIRE_DIAGNOSTICS}",
        "WIRE_DIAGNOSTIC_SYMBOLS = {entry['id']: entry['symbol'] for entry in WIRE_DIAGNOSTICS}",
        "WIRE_DIAGNOSTIC_MESSAGES = {entry['id']: entry['message'] for entry in WIRE_DIAGNOSTICS}",
        "",
        "def action_valid(action):",
        "    if type(action) is not int:",
        "        return False",
        "    return RUNE_WIRE_ACTION_RANGE[0] <= action <= RUNE_WIRE_ACTION_RANGE[1]",
        "",
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
        "            trait & (trait - 1) or trait & ~ACTION_TRAIT_ALL_MASK):",
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
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT)
    parser.add_argument("--c-output", type=Path, default=DEFAULT_C_OUTPUT)
    parser.add_argument("--python-output", type=Path, default=DEFAULT_PY_OUTPUT)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    return parser


def main(argv=None):
    args = _parser().parse_args(argv)
    try:
        document = load_document(args.contract)
        if args.write:
            crc32_value, sha256_value = write_outputs(
                document, args.c_output, args.python_output)
        else:
            stale, crc32_value, sha256_value = check_outputs(
                document, args.c_output, args.python_output)
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
