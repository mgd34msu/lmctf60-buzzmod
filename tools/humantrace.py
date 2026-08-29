#!/usr/bin/env python3
"""Validate exact server-side human Pmove traces and publish replay evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import tempfile
from typing import Any


TRACE_FORMAT_V1 = "lmctf-human-trace-v1"
TRACE_FORMAT_V2 = "lmctf-human-trace-v2"
TRACE_FORMAT_V3 = "lmctf-human-trace-v3"
TRACE_FORMAT = TRACE_FORMAT_V1
EVIDENCE_FORMAT_V1 = "lmctf-human-replay-evidence-v1"
EVIDENCE_FORMAT_V2 = "lmctf-human-replay-evidence-v2"
EVIDENCE_FORMAT_V3 = "lmctf-human-replay-evidence-v3"
EVIDENCE_FORMAT = EVIDENCE_FORMAT_V1
SHA256_HEX_BYTES = 64
UINT32_MAX = 0xFFFFFFFF
UINT64_MAX = 0xFFFFFFFFFFFFFFFF
INT32_MAX = 0x7FFFFFFF
STATE_FIELDS = {
    "type", "origin", "velocity", "flags", "time", "gravity",
    "delta_angles",
}
COMMAND_FIELDS = {
    "msec", "buttons", "angles", "forward", "side", "up", "impulse",
    "light",
}
RUNE_BIND_FIELDS = {
    "format", "kind", "start_sequence", "frame", "map",
    "bsp_checksum", "entity_crc32", "physics_flags", "gravity",
    "airaccelerate", "maxvelocity", "pmove_substep_ms",
    "server_frame_ms", "host_physics_id", "route_contract",
    "payload_crc32", "header_crc32", "action_contract_crc32",
    "mechanism_contract_crc32", "num_seeds", "num_links",
    "num_mechanism_nodes", "num_mechanism_edges",
    "num_inventory_edges", "num_mechanism_plans", "string_bytes",
    "rune_sha256",
}
RUNE_BIND_FIELDS_V2 = RUNE_BIND_FIELDS | {"start_hook_event"}
HOOK_COMMON_FIELDS = {
    "format", "kind", "event", "after_step", "client", "frame", "hook",
}
HOOK_EVENT_FIELDS = {
    "hook-fire": HOOK_COMMON_FIELDS | {
        "origin_q8", "velocity_q8", "view_short", "hand",
    },
    "hook-attach": HOOK_COMMON_FIELDS | {
        "bite_q8", "target", "world",
    },
    "hook-release": HOOK_COMMON_FIELDS | {
        "origin_q8", "velocity_q8",
    },
}
V3_HEADER_FIELDS = {
    "format", "kind", "session", "segment", "continuation", "start_order",
    "start_command", "start_hook_event", "map", "bsp_checksum",
    "entity_crc32", "physics_id", "host_physics_id", "gravity_bits",
    "airaccelerate_bits", "maxvelocity_bits", "pmove_substep_ms",
    "server_frame_ms", "physics_flags", "module_revision", "module_version",
    "prev_sha256", "sha256",
}
V3_STEP_FIELDS = {
    "format", "kind", "order", "command", "client", "spawn_generation",
    "frame", "level_time_bits", "snapinitial", "cmd", "before", "after",
    "viewangles_bits", "viewheight_bits", "mins_bits", "maxs_bits", "ground",
    "waterlevel", "watertype", "numtouch", "touches", "prev_sha256",
    "sha256",
}
V3_HOOK_SNAPSHOT_FIELDS = {
    "origin_bits", "velocity_bits", "mins_bits", "maxs_bits",
    "viewangles_bits", "viewheight_bits", "hook_origin_bits",
    "hook_velocity_bits", "hook_offset_bits", "hookend_bits",
    "hookangle_bits", "hook_state", "hook_length", "hand", "hook_target",
}
V3_HOOK_COMMON_FIELDS = {
    "format", "kind", "order", "hook_event", "after_command", "client",
    "spawn_generation", "frame", "level_time_bits", "hook",
    "prev_sha256", "sha256",
} | V3_HOOK_SNAPSHOT_FIELDS
V3_HOOK_FIELDS = {
    "hook-fire": V3_HOOK_COMMON_FIELDS,
    "hook-attach": V3_HOOK_COMMON_FIELDS | {"target", "world"},
    "hook-release": V3_HOOK_COMMON_FIELDS,
    "hook-reset": V3_HOOK_COMMON_FIELDS,
}
V3_END_FIELDS = {
    "format", "kind", "order", "frame", "level_time_bits", "prev_sha256",
    "sha256",
}
SHA256 = re.compile(r"[0-9a-f]{64}\Z")


def integer(value: Any, name: str, low: int, high: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{name} must be an integer")
    if value < low or value > high:
        raise ValueError(f"{name} is outside {low}..{high}")
    return value


def integer_vector(value: Any, name: str, low: int, high: int) -> list[int]:
    if not isinstance(value, list) or len(value) != 3:
        raise ValueError(f"{name} must contain three integers")
    return [integer(item, f"{name}[{index}]", low, high)
            for index, item in enumerate(value)]


def integer_vector_2(value: Any, name: str, low: int,
                     high: int) -> list[int]:
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError(f"{name} must contain two integers")
    return [integer(item, f"{name}[{index}]", low, high)
            for index, item in enumerate(value)]


def number(value: Any, name: str, low: float, high: float) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name} must be a finite number")
    result = float(value)
    if not math.isfinite(result) or result < low or result > high:
        raise ValueError(f"{name} is outside {low}..{high}")
    return result


def sha256_hex(value: Any, name: str) -> str:
    if not isinstance(value, str) or SHA256.fullmatch(value) is None:
        raise ValueError(f"{name} must be a lowercase SHA-256")
    return value


def validate_state(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != STATE_FIELDS:
        raise ValueError(f"{name} has the wrong fields")
    return {
        "type": integer(value["type"], f"{name}.type", 0, 4),
        "origin": integer_vector(value["origin"], f"{name}.origin",
                                 -32768, 32767),
        "velocity": integer_vector(value["velocity"], f"{name}.velocity",
                                   -32768, 32767),
        "flags": integer(value["flags"], f"{name}.flags", 0, 255),
        "time": integer(value["time"], f"{name}.time", 0, 255),
        "gravity": integer(value["gravity"], f"{name}.gravity",
                           -32768, 32767),
        "delta_angles": integer_vector(
            value["delta_angles"], f"{name}.delta_angles", -32768, 32767),
    }


def validate_command(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != COMMAND_FIELDS:
        raise ValueError(f"{name} has the wrong fields")
    return {
        "msec": integer(value["msec"], f"{name}.msec", 0, 255),
        "buttons": integer(value["buttons"], f"{name}.buttons", 0, 255),
        "angles": integer_vector(value["angles"], f"{name}.angles",
                                 -32768, 32767),
        "forward": integer(value["forward"], f"{name}.forward",
                           -32768, 32767),
        "side": integer(value["side"], f"{name}.side", -32768, 32767),
        "up": integer(value["up"], f"{name}.up", -32768, 32767),
        "impulse": integer(value["impulse"], f"{name}.impulse", 0, 255),
        "light": integer(value["light"], f"{name}.light", 0, 255),
    }


def validate_header(value: Any, line: int) -> dict[str, Any]:
    if not isinstance(value, dict) or value.get("format") not in {
            TRACE_FORMAT_V1, TRACE_FORMAT_V2} or \
            value.get("kind") != "header":
        raise ValueError(f"line {line}: invalid trace header")
    map_name = value.get("map")
    version = value.get("module_version")
    if not isinstance(map_name, str) or not map_name or len(map_name) >= 64:
        raise ValueError(f"line {line}: invalid map name")
    if not isinstance(version, str) or not version:
        raise ValueError(f"line {line}: invalid module version")
    return {
        "map": map_name,
        "bsp_checksum": integer(
            value.get("bsp_checksum"), f"line {line} bsp_checksum",
            0, UINT32_MAX),
        "entity_crc32": integer(
            value.get("entity_crc32"), f"line {line} entity_crc32",
            0, UINT32_MAX),
        "physics_id": integer(
            value.get("physics_id"), f"line {line} physics_id",
            1, UINT32_MAX),
        "module_revision": integer(
            value.get("module_revision"), f"line {line} module_revision",
            0, INT32_MAX),
        "module_version": version,
    }


def validate_rune_bind(value: Any, line: int,
                       trace_identity: dict[str, Any],
                       trace_format: str = TRACE_FORMAT_V1) -> dict[str, Any]:
    fields = (RUNE_BIND_FIELDS_V2 if trace_format == TRACE_FORMAT_V2
              else RUNE_BIND_FIELDS)
    if not isinstance(value, dict) or set(value) != fields or \
            value.get("format") != trace_format or \
            value.get("kind") != "rune-bind":
        raise ValueError(f"line {line}: invalid rune binding fields")
    map_name = value.get("map")
    if not isinstance(map_name, str) or not map_name or len(map_name) >= 64:
        raise ValueError(f"line {line}: invalid rune binding map")
    result = {
        "start_sequence": integer(
            value.get("start_sequence"),
            f"line {line} start_sequence", 1, 2**63 - 1),
        "frame": integer(value.get("frame"), f"line {line} frame",
                         0, 0x7FFFFFFF),
        "map": map_name,
        "bsp_checksum": integer(
            value.get("bsp_checksum"), f"line {line} bsp_checksum",
            0, 0xFFFFFFFF),
        "entity_crc32": integer(
            value.get("entity_crc32"), f"line {line} entity_crc32",
            0, 0xFFFFFFFF),
        "physics_flags": integer(
            value.get("physics_flags"), f"line {line} physics_flags",
            0, 0xFFFFFFFF),
        "gravity": number(value.get("gravity"), f"line {line} gravity",
                          -65536.0, 65536.0),
        "airaccelerate": number(
            value.get("airaccelerate"), f"line {line} airaccelerate",
            -65536.0, 65536.0),
        "maxvelocity": number(
            value.get("maxvelocity"), f"line {line} maxvelocity",
            0.0, 65536.0),
        "pmove_substep_ms": integer(
            value.get("pmove_substep_ms"),
            f"line {line} pmove_substep_ms", 1, 0xFFFF),
        "server_frame_ms": integer(
            value.get("server_frame_ms"),
            f"line {line} server_frame_ms", 1, 0xFFFF),
        "host_physics_id": integer(
            value.get("host_physics_id"),
            f"line {line} host_physics_id", 1, 0xFFFFFFFF),
        "route_contract": integer(
            value.get("route_contract"), f"line {line} route_contract",
            1, 1),
        "payload_crc32": integer(
            value.get("payload_crc32"), f"line {line} payload_crc32",
            0, 0xFFFFFFFF),
        "header_crc32": integer(
            value.get("header_crc32"), f"line {line} header_crc32",
            0, 0xFFFFFFFF),
        "action_contract_crc32": integer(
            value.get("action_contract_crc32"),
            f"line {line} action_contract_crc32", 0, 0xFFFFFFFF),
        "mechanism_contract_crc32": integer(
            value.get("mechanism_contract_crc32"),
            f"line {line} mechanism_contract_crc32", 0, 0xFFFFFFFF),
        "num_seeds": integer(
            value.get("num_seeds"), f"line {line} num_seeds", 1, 32768),
        "num_links": integer(
            value.get("num_links"), f"line {line} num_links", 0, 262144),
        "num_mechanism_nodes": integer(
            value.get("num_mechanism_nodes"),
            f"line {line} num_mechanism_nodes", 0, 8192),
        "num_mechanism_edges": integer(
            value.get("num_mechanism_edges"),
            f"line {line} num_mechanism_edges", 0, 262144),
        "num_inventory_edges": integer(
            value.get("num_inventory_edges"),
            f"line {line} num_inventory_edges", 0, 262144),
        "num_mechanism_plans": integer(
            value.get("num_mechanism_plans"),
            f"line {line} num_mechanism_plans", 0, 262144),
        "string_bytes": integer(
            value.get("string_bytes"), f"line {line} string_bytes",
            1, 1048576),
        "rune_sha256": sha256_hex(
            value.get("rune_sha256"), f"line {line} rune_sha256"),
    }
    if trace_format == TRACE_FORMAT_V2:
        result["start_hook_event"] = integer(
            value.get("start_hook_event"),
            f"line {line} start_hook_event", 1, 2**63 - 1)
    if (result["map"] != trace_identity["map"] or
            result["bsp_checksum"] != trace_identity["bsp_checksum"] or
            result["entity_crc32"] != trace_identity["entity_crc32"] or
            result["host_physics_id"] != trace_identity["physics_id"]):
        raise ValueError(f"line {line}: rune binding identity mismatch")
    if result["num_inventory_edges"] > result["num_mechanism_edges"]:
        raise ValueError(f"line {line}: rune binding edge counts disagree")
    return result



def validate_step(value: Any, line: int,
                  trace_format: str = TRACE_FORMAT_V1) -> dict[str, Any]:
    if not isinstance(value, dict) or value.get("format") != trace_format or \
            value.get("kind") != "step":
        raise ValueError(f"line {line}: invalid trace step")
    touches = value.get("touches")
    if not isinstance(touches, list) or len(touches) > 32:
        raise ValueError(f"line {line}: touches must contain at most 32 keys")
    return {
        "seq": integer(value.get("seq"), f"line {line} seq", 1, 2**63 - 1),
        "client": integer(value.get("client"), f"line {line} client",
                          1, INT32_MAX),
        "frame": integer(value.get("frame"), f"line {line} frame",
                         0, INT32_MAX),
        "snapinitial": integer(
            value.get("snapinitial"), f"line {line} snapinitial", 0, 1),
        "cmd": validate_command(value.get("cmd"), f"line {line} cmd"),
        "before": validate_state(
            value.get("before"), f"line {line} before"),
        "after": validate_state(value.get("after"), f"line {line} after"),
        "ground": integer(value.get("ground"), f"line {line} ground",
                          -1, INT32_MAX),
        "waterlevel": integer(
            value.get("waterlevel"), f"line {line} waterlevel", 0, 3),
        "watertype": integer(
            value.get("watertype"), f"line {line} watertype",
            -0x80000000, INT32_MAX),
        "touches": [integer(item, f"line {line} touches[{index}]",
                            -1, INT32_MAX)
                    for index, item in enumerate(touches)],
    }


def validate_hook_event(value: Any, line: int) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"line {line}: invalid hook event")
    kind = value.get("kind")
    fields = HOOK_EVENT_FIELDS.get(kind)
    if fields is None or set(value) != fields or \
            value.get("format") != TRACE_FORMAT_V2:
        raise ValueError(f"line {line}: invalid {kind or 'hook'} fields")
    result = {
        "kind": kind,
        "event": integer(value.get("event"), f"line {line} event",
                         1, 2**63 - 1),
        "after_step": integer(value.get("after_step"),
                              f"line {line} after_step", 0, 2**63 - 1),
        "client": integer(value.get("client"), f"line {line} client",
                          1, INT32_MAX),
        "frame": integer(value.get("frame"), f"line {line} frame",
                         0, INT32_MAX),
        "hook": integer(value.get("hook"), f"line {line} hook",
                        1, INT32_MAX),
    }
    if kind == "hook-fire":
        result.update({
            "origin_q8": integer_vector(value.get("origin_q8"),
                                        f"line {line} origin_q8",
                                        -0x80000000, INT32_MAX),
            "velocity_q8": integer_vector(value.get("velocity_q8"),
                                          f"line {line} velocity_q8",
                                          -0x80000000, INT32_MAX),
            "view_short": integer_vector_2(value.get("view_short"),
                                            f"line {line} view_short",
                                            -32768, 32767),
            "hand": integer(value.get("hand"), f"line {line} hand", 0, 2),
        })
    elif kind == "hook-attach":
        result.update({
            "bite_q8": integer_vector(value.get("bite_q8"),
                                      f"line {line} bite_q8",
                                      -0x80000000, INT32_MAX),
            "target": integer(value.get("target"), f"line {line} target",
                              0, INT32_MAX),
            "world": integer(value.get("world"), f"line {line} world", 0, 1),
        })
        if bool(result["world"]) != (result["target"] == 0):
            raise ValueError(f"line {line}: hook world target mismatch")
    else:
        result.update({
            "origin_q8": integer_vector(value.get("origin_q8"),
                                        f"line {line} origin_q8",
                                        -0x80000000, INT32_MAX),
            "velocity_q8": integer_vector(value.get("velocity_q8"),
                                          f"line {line} velocity_q8",
                                          -0x80000000, INT32_MAX),
        })
    return result


def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON field {key!r}")
        result[key] = value
    return result


def read_json(raw: str, line: int) -> Any:
    try:
        return json.loads(raw, object_pairs_hook=unique_object)
    except (json.JSONDecodeError, ValueError) as error:
        raise ValueError(f"line {line}: invalid JSON: {error}") from error


def require_fields(value: Any, fields: set[str], line: int,
                   name: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != fields:
        raise ValueError(f"line {line}: invalid {name} fields")
    return value


def printable(value: Any, line: int, name: str, capacity: int,
              map_name: bool = False) -> str:
    if not isinstance(value, str) or not value or len(value) >= capacity:
        raise ValueError(f"line {line}: invalid {name}")
    for character in value:
        if ord(character) < 0x20 or ord(character) > 0x7E:
            raise ValueError(f"line {line}: invalid {name}")
        if map_name and not (
                character.isascii() and
                (character.isalnum() or character in "_.-")):
            raise ValueError(f"line {line}: invalid {name}")
    return value


def v3_sha256_hex(value: Any, line: int, name: str) -> str:
    if not isinstance(value, str) or len(value) != SHA256_HEX_BYTES or \
            any(character not in "0123456789abcdef" for character in value):
        raise ValueError(f"line {line}: invalid {name}")
    return value


def validate_v3_digest(raw: str, value: dict[str, Any], line: int,
                       previous: str) -> str:
    if not raw.endswith("\n") or raw.endswith("\r\n"):
        raise ValueError(f"line {line}: v3 record has noncanonical newline")
    line_text = raw[:-1]
    claimed_previous = v3_sha256_hex(value.get("prev_sha256"), line,
                                     "prev_sha256")
    digest = v3_sha256_hex(value.get("sha256"), line, "sha256")
    suffix = f',"prev_sha256":"{claimed_previous}","sha256":"{digest}"}}'
    if claimed_previous != previous or not line_text.endswith(suffix) or \
            line_text.count(',"prev_sha256":"') != 1:
        raise ValueError(f"line {line}: v3 hash-chain predecessor mismatch")
    payload = line_text[:-len(suffix)] + "}"
    expected = hashlib.sha256(
        previous.encode("ascii") + payload.encode("utf-8")).hexdigest()
    if digest != expected:
        raise ValueError(f"line {line}: v3 hash-chain digest mismatch")
    return digest


def validate_v3_header(value: Any, line: int, raw: str) -> tuple[
        dict[str, Any], str]:
    value = require_fields(value, V3_HEADER_FIELDS, line, "v3 trace header")
    if value.get("format") != TRACE_FORMAT_V3 or value.get("kind") != "header":
        raise ValueError(f"line {line}: invalid trace header")
    previous = v3_sha256_hex(value.get("prev_sha256"), line, "prev_sha256")
    digest = validate_v3_digest(raw, value, line, previous)
    continuation = integer(value.get("continuation"), f"line {line} continuation",
                           0, 1)
    if (continuation == 0 and previous != "0" * SHA256_HEX_BYTES) or \
            (continuation == 1 and previous == "0" * SHA256_HEX_BYTES):
        raise ValueError(f"line {line}: invalid v3 continuation anchor")
    header = {
        "map": printable(value.get("map"), line, "map name", 64, True),
        "bsp_checksum": integer(value.get("bsp_checksum"),
                                f"line {line} bsp_checksum", 0, UINT32_MAX),
        "entity_crc32": integer(value.get("entity_crc32"),
                                f"line {line} entity_crc32", 0, UINT32_MAX),
        "physics_id": integer(value.get("physics_id"),
                              f"line {line} physics_id", 0, 0),
        "host_physics_id": integer(value.get("host_physics_id"),
                                   f"line {line} host_physics_id", 1,
                                   UINT32_MAX),
        "gravity_bits": integer(value.get("gravity_bits"),
                                f"line {line} gravity_bits", 0, UINT32_MAX),
        "airaccelerate_bits": integer(value.get("airaccelerate_bits"),
                                      f"line {line} airaccelerate_bits", 0,
                                      UINT32_MAX),
        "maxvelocity_bits": integer(value.get("maxvelocity_bits"),
                                    f"line {line} maxvelocity_bits", 0,
                                    UINT32_MAX),
        "pmove_substep_ms": integer(value.get("pmove_substep_ms"),
                                    f"line {line} pmove_substep_ms", 1,
                                    0xFFFF),
        "server_frame_ms": integer(value.get("server_frame_ms"),
                                   f"line {line} server_frame_ms", 1,
                                   0xFFFF),
        "physics_flags": integer(value.get("physics_flags"),
                                 f"line {line} physics_flags", 0, UINT32_MAX),
        "module_revision": integer(value.get("module_revision"),
                                   f"line {line} module_revision", 0,
                                   UINT32_MAX),
        "module_version": printable(value.get("module_version"), line,
                                    "module version", 64),
        # UINT32_MAX is the recorder's exhaustion sentinel, never a valid
        # root or continuation segment identity.
        "session": integer(value.get("session"), f"line {line} session", 0,
                           UINT32_MAX - 1),
        "segment": integer(value.get("segment"), f"line {line} segment", 0,
                           UINT32_MAX - 1),
        "continuation": continuation,
        "start_order": integer(value.get("start_order"),
                               f"line {line} start_order", 1, UINT64_MAX - 1),
        "start_command": integer(value.get("start_command"),
                                 f"line {line} start_command", 1,
                                 UINT64_MAX - 1),
        "start_hook_event": integer(value.get("start_hook_event"),
                                    f"line {line} start_hook_event", 1,
                                    UINT64_MAX - 1),
        "prev_sha256": previous,
        "sha256": digest,
    }
    return header, digest


def validate_v3_step(value: Any, line: int) -> dict[str, Any]:
    value = require_fields(value, V3_STEP_FIELDS, line, "v3 trace step")
    if value.get("format") != TRACE_FORMAT_V3 or value.get("kind") != "step":
        raise ValueError(f"line {line}: invalid trace step")
    touches = value.get("touches")
    numtouch = integer(value.get("numtouch"), f"line {line} numtouch", 0, 32)
    if not isinstance(touches, list) or len(touches) != numtouch:
        raise ValueError(f"line {line}: v3 touches do not match numtouch")
    return {
        "kind": "step",
        "order": integer(value.get("order"), f"line {line} order", 1,
                         UINT64_MAX - 1),
        "command": integer(value.get("command"), f"line {line} command", 1,
                           UINT64_MAX - 1),
        "client": integer(value.get("client"), f"line {line} client", 1,
                          INT32_MAX),
        "spawn_generation": integer(value.get("spawn_generation"),
                                    f"line {line} spawn_generation", 1,
                                    UINT64_MAX - 1),
        "frame": integer(value.get("frame"), f"line {line} frame", 0,
                         INT32_MAX),
        "level_time_bits": integer(value.get("level_time_bits"),
                                   f"line {line} level_time_bits", 0,
                                   UINT32_MAX),
        "snapinitial": integer(value.get("snapinitial"),
                               f"line {line} snapinitial", 0, 1),
        "cmd": validate_command(value.get("cmd"), f"line {line}"),
        "before": validate_state(value.get("before"), f"line {line} before"),
        "after": validate_state(value.get("after"), f"line {line} after"),
        "viewangles_bits": integer_vector(value.get("viewangles_bits"),
                                          f"line {line} viewangles_bits", 0,
                                          UINT32_MAX),
        "viewheight_bits": integer(value.get("viewheight_bits"),
                                   f"line {line} viewheight_bits", 0,
                                   UINT32_MAX),
        "mins_bits": integer_vector(value.get("mins_bits"),
                                    f"line {line} mins_bits", 0, UINT32_MAX),
        "maxs_bits": integer_vector(value.get("maxs_bits"),
                                    f"line {line} maxs_bits", 0, UINT32_MAX),
        "ground": integer(value.get("ground"), f"line {line} ground", -1,
                          INT32_MAX),
        "waterlevel": integer(value.get("waterlevel"),
                              f"line {line} waterlevel", 0, 3),
        "watertype": integer(value.get("watertype"), f"line {line} watertype",
                             -0x80000000, INT32_MAX),
        "numtouch": numtouch,
        "touches": [integer(item, f"line {line} touches[{index}]", -1,
                            INT32_MAX)
                    for index, item in enumerate(touches)],
    }


def validate_v3_hook_snapshot(value: dict[str, Any],
                              line: int) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for field in (
            "origin_bits", "velocity_bits", "mins_bits", "maxs_bits",
            "viewangles_bits", "hook_origin_bits", "hook_velocity_bits",
            "hook_offset_bits", "hookend_bits", "hookangle_bits"):
        result[field] = integer_vector(value.get(field), f"line {line} {field}",
                                       0, UINT32_MAX)
    result["viewheight_bits"] = integer(value.get("viewheight_bits"),
                                        f"line {line} viewheight_bits", 0,
                                        UINT32_MAX)
    for field in ("hook_state", "hook_length", "hand", "hook_target"):
        result[field] = integer(value.get(field), f"line {line} {field}",
                                -0x80000000, INT32_MAX)
    return result


def validate_v3_hook(value: Any, line: int) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"line {line}: invalid v3 hook event")
    kind = value.get("kind")
    fields = V3_HOOK_FIELDS.get(kind)
    if fields is None:
        raise ValueError(f"line {line}: invalid v3 hook event")
    value = require_fields(value, fields, line, kind)
    if value.get("format") != TRACE_FORMAT_V3:
        raise ValueError(f"line {line}: invalid {kind} format")
    result = {
        "kind": kind,
        "order": integer(value.get("order"), f"line {line} order", 1,
                         UINT64_MAX - 1),
        "hook_event": integer(value.get("hook_event"),
                              f"line {line} hook_event", 1,
                              UINT64_MAX - 1),
        "after_command": integer(value.get("after_command"),
                                 f"line {line} after_command", 0,
                                 UINT64_MAX - 1),
        "client": integer(value.get("client"), f"line {line} client", 1,
                          INT32_MAX),
        "spawn_generation": integer(value.get("spawn_generation"),
                                    f"line {line} spawn_generation", 1,
                                    UINT64_MAX - 1),
        "frame": integer(value.get("frame"), f"line {line} frame", 0,
                         INT32_MAX),
        "level_time_bits": integer(value.get("level_time_bits"),
                                   f"line {line} level_time_bits", 0,
                                   UINT32_MAX),
        "hook": integer(value.get("hook"), f"line {line} hook", 1,
                        INT32_MAX),
    }
    result.update(validate_v3_hook_snapshot(value, line))
    if kind == "hook-attach":
        result["target"] = integer(value.get("target"), f"line {line} target",
                                   0, INT32_MAX)
        result["world"] = integer(value.get("world"), f"line {line} world",
                                  0, 1)
        if bool(result["world"]) != (result["target"] == 0):
            raise ValueError(f"line {line}: hook world target mismatch")
    return result


def validate_v3_end(value: Any, line: int) -> dict[str, Any]:
    value = require_fields(value, V3_END_FIELDS, line, "v3 trace end")
    if value.get("format") != TRACE_FORMAT_V3 or value.get("kind") != "end":
        raise ValueError(f"line {line}: invalid trace end")
    return {
        "kind": "end",
        "order": integer(value.get("order"), f"line {line} order", 1,
                         UINT64_MAX - 1),
        "frame": integer(value.get("frame"), f"line {line} frame", 0,
                         INT32_MAX),
        "level_time_bits": integer(value.get("level_time_bits"),
                                   f"line {line} level_time_bits", 0,
                                   UINT32_MAX),
    }


def require_v3_order(current: dict[str, Any], record: dict[str, Any],
                     line: int) -> None:
    if record["order"] < current["start_order"] or \
            record["order"] <= current["last_order"] or \
            record["frame"] < current["last_frame"]:
        raise ValueError(f"line {line}: v3 record order is not monotonic")
    current["last_order"] = record["order"]
    current["last_frame"] = record["frame"]


def _read_sessions_single(path: Path) -> list[dict[str, Any]]:
    sessions: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    with path.open("r", encoding="utf-8", newline="") as source:
        for line_number, raw in enumerate(source, 1):
            if not raw.strip():
                if current and current["trace_format"] == TRACE_FORMAT_V3:
                    raise ValueError(f"line {line_number}: blank v3 record")
                continue
            value = read_json(raw, line_number)
            kind = value.get("kind") if isinstance(value, dict) else None
            if kind == "header":
                if current and current["trace_format"] == TRACE_FORMAT_V3 and \
                        not current["ended"]:
                    raise ValueError(
                        f"line {line_number}: v3 session lacks terminal end")
                trace_format = value.get("format") if isinstance(value, dict) \
                    else None
                if trace_format == TRACE_FORMAT_V3:
                    header, digest = validate_v3_header(value, line_number, raw)
                    current = {
                        "identity": {
                            key: header[key] for key in (
                                "map", "bsp_checksum", "entity_crc32",
                                "physics_id", "host_physics_id",
                                "gravity_bits", "airaccelerate_bits",
                                "maxvelocity_bits", "pmove_substep_ms",
                                "server_frame_ms", "physics_flags",
                                "module_revision", "module_version")
                        },
                        "trace_header": header,
                        "trace_format": trace_format,
                        "steps": [],
                        "hook_events": [],
                        "last_hook_event": header["start_hook_event"] - 1,
                        "last_hook_command": header["start_command"] - 1,
                        "last_command": header["start_command"] - 1,
                        "start_order": header["start_order"],
                        "last_order": header["start_order"] - 1,
                        "last_frame": -1,
                        "chain_previous": digest,
                        "terminal_sha256": None,
                        "end": None,
                        "ended": False,
                        "ordinal": len(sessions) + 1,
                    }
                else:
                    current = {
                        "identity": validate_header(value, line_number),
                        "trace_format": trace_format,
                        "rune_bindings": [],
                        "steps": [],
                        "hook_events": [],
                        "last_hook_event": 0,
                        "last_hook_after_step": 0,
                        "greatest_step": 0,
                        "ordinal": len(sessions) + 1,
                    }
                sessions.append(current)
                continue
            if current is None:
                raise ValueError(f"line {line_number}: record precedes header")
            if current["trace_format"] == TRACE_FORMAT_V3:
                if current["ended"]:
                    raise ValueError(f"line {line_number}: record follows v3 end")
                if not isinstance(value, dict):
                    raise ValueError(f"line {line_number}: invalid v3 record")
                digest = validate_v3_digest(
                    raw, value, line_number, current["chain_previous"])
                if kind == "step":
                    record = validate_v3_step(value, line_number)
                    require_v3_order(current, record, line_number)
                    if record["command"] <= current["last_command"]:
                        raise ValueError(
                            f"line {line_number}: v3 command is not ordered")
                    current["last_command"] = record["command"]
                    current["steps"].append(record)
                elif kind in V3_HOOK_FIELDS:
                    record = validate_v3_hook(value, line_number)
                    require_v3_order(current, record, line_number)
                    if record["hook_event"] <= current["last_hook_event"] or \
                            record["after_command"] < \
                            current["last_hook_command"] or \
                            record["after_command"] > current["last_command"]:
                        raise ValueError(
                            f"line {line_number}: v3 hook order is invalid")
                    current["last_hook_event"] = record["hook_event"]
                    current["last_hook_command"] = record["after_command"]
                    current["hook_events"].append(record)
                elif kind == "end":
                    record = validate_v3_end(value, line_number)
                    require_v3_order(current, record, line_number)
                    current["end"] = record
                    current["terminal_sha256"] = digest
                    current["ended"] = True
                else:
                    raise ValueError(f"line {line_number}: unknown v3 record kind")
                current["chain_previous"] = digest
                continue
            if kind == "rune-bind":
                if current["rune_bindings"]:
                    raise ValueError(
                        f"line {line_number}: duplicate rune binding")
                current["rune_bindings"].append(validate_rune_bind(
                    value, line_number, current["identity"],
                    current["trace_format"]))
            elif kind == "step":
                step = validate_step(value, line_number, current["trace_format"])
                current["steps"].append(step)
                current["greatest_step"] = max(current["greatest_step"],
                                               step["seq"])
            elif kind in HOOK_EVENT_FIELDS:
                if current["trace_format"] != TRACE_FORMAT_V2:
                    raise ValueError(
                        f"line {line_number}: v1 trace contains hook event")
                event = validate_hook_event(value, line_number)
                if event["event"] <= current["last_hook_event"]:
                    raise ValueError(
                        f"line {line_number}: hook event order is not "
                        "strictly increasing")
                if event["after_step"] < current["last_hook_after_step"]:
                    raise ValueError(
                        f"line {line_number}: hook after_step decreased")
                if event["after_step"] > current["greatest_step"]:
                    raise ValueError(
                        f"line {line_number}: hook after_step exceeds "
                        "the preceding Pmove sequence")
                current["hook_events"].append(event)
                current["last_hook_event"] = event["event"]
                current["last_hook_after_step"] = event["after_step"]
            else:
                raise ValueError(f"line {line_number}: unknown trace record kind")
    if current and current["trace_format"] == TRACE_FORMAT_V3 and \
            not current["ended"]:
        raise ValueError("v3 trace lacks terminal end")
    if not sessions:
        raise ValueError("trace contains no sessions")
    return sessions


def _v3_segment_prefix(path: Path) -> tuple[Path, str] | None:
    """Return the deterministic sibling prefix for a recorder v3 segment."""
    match = re.fullmatch(r"(.+-)([0-9]{6}|[1-9][0-9]{6,})\.jsonl",
                         path.name)
    if match is None:
        return None
    return path.parent, match.group(1)


def _v3_filename_segment(path: Path) -> int | None:
    """Return a canonical recorder segment suffix, if this is one."""
    match = re.fullmatch(r".+-([0-9]{6}|[1-9][0-9]{6,})\.jsonl", path.name)
    if match is None:
        return None
    segment = int(match.group(1))
    return segment if segment < UINT32_MAX else None


def _first_v3_record(path: Path) -> bool:
    with path.open("r", encoding="utf-8", newline="") as source:
        for line_number, raw in enumerate(source, 1):
            if raw.strip():
                value = read_json(raw, line_number)
                return isinstance(value, dict) and \
                    value.get("format") == TRACE_FORMAT_V3
    raise ValueError("trace contains no records")


def _v3_header_only(path: Path) -> dict[str, Any]:
    """Read enough of a sibling to decide whether it belongs to this session."""
    with path.open("r", encoding="utf-8", newline="") as source:
        raw = source.readline()
    if not raw:
        raise ValueError(f"{path.name}: v3 trace contains no header")
    value = read_json(raw, 1)
    header, _ = validate_v3_header(value, 1, raw)
    return header


def _validate_v3_segment(path: Path) -> dict[str, Any]:
    """Validate one physical segment before linking it to its predecessors.

    A continuation header can only authenticate its own line at this stage. Its
    predecessor is deliberately checked by _assemble_v3_session, where the
    complete sibling set is available; accepting it here would make a copied
    continuation look like a complete trace.
    """
    header: dict[str, Any] | None = None
    current: dict[str, Any] | None = None
    steps: list[dict[str, Any]] = []
    hook_events: list[dict[str, Any]] = []
    end: dict[str, Any] | None = None
    digest = ""

    with path.open("r", encoding="utf-8", newline="") as source:
        for line_number, raw in enumerate(source, 1):
            if not raw.strip():
                raise ValueError(
                    f"{path.name}: line {line_number}: blank v3 record")
            value = read_json(raw, line_number)
            kind = value.get("kind") if isinstance(value, dict) else None
            if header is None:
                if kind != "header":
                    raise ValueError(
                        f"{path.name}: line {line_number}: record precedes header")
                header, digest = validate_v3_header(value, line_number, raw)
                current = {
                    "start_order": header["start_order"],
                    "last_order": header["start_order"] - 1,
                    "last_frame": -1,
                    "last_command": header["start_command"] - 1,
                    "last_hook_event": header["start_hook_event"] - 1,
                    "last_hook_command": header["start_command"] - 1,
                }
                continue
            if end is not None:
                raise ValueError(
                    f"{path.name}: line {line_number}: record follows v3 end")
            if not isinstance(value, dict):
                raise ValueError(f"{path.name}: line {line_number}: invalid v3 record")
            digest = validate_v3_digest(raw, value, line_number, digest)
            if kind == "step":
                record = validate_v3_step(value, line_number)
                require_v3_order(current, record, line_number)
                if record["command"] <= current["last_command"]:
                    raise ValueError(
                        f"{path.name}: line {line_number}: v3 command is not ordered")
                current["last_command"] = record["command"]
                steps.append(record)
            elif kind in V3_HOOK_FIELDS:
                record = validate_v3_hook(value, line_number)
                require_v3_order(current, record, line_number)
                if record["hook_event"] <= current["last_hook_event"] or \
                        record["after_command"] < current["last_hook_command"] or \
                        record["after_command"] > current["last_command"]:
                    raise ValueError(
                        f"{path.name}: line {line_number}: v3 hook order is invalid")
                current["last_hook_event"] = record["hook_event"]
                current["last_hook_command"] = record["after_command"]
                hook_events.append(record)
            elif kind == "end":
                end = validate_v3_end(value, line_number)
                require_v3_order(current, end, line_number)
            else:
                raise ValueError(
                    f"{path.name}: line {line_number}: unknown v3 record kind")
    if header is None or current is None:
        raise ValueError(f"{path.name}: v3 trace contains no header")
    return {
        "path": path,
        "header": header,
        "steps": steps,
        "hook_events": hook_events,
        "end": end,
        "last_digest": digest,
        "last_order": current["last_order"],
        "last_frame": current["last_frame"],
        "last_command": current["last_command"],
        "last_hook_event": current["last_hook_event"],
    }


def _v3_stable_identity(header: dict[str, Any]) -> tuple[Any, ...]:
    """Fields a rotating segment may not change within one recorder session."""
    return tuple(header[field] for field in (
        "map", "bsp_checksum", "entity_crc32", "physics_id",
        "host_physics_id", "module_revision", "module_version"))


def _assemble_v3_session(segments: list[dict[str, Any]], ordinal: int) -> dict[str, Any]:
    if not segments:
        raise ValueError("v3 session contains no segments")
    roots = [segment for segment in segments
             if segment["header"]["continuation"] == 0]
    if len(roots) != 1:
        raise ValueError("v3 session must have exactly one zero-rooted segment")
    root = roots[0]
    zero = "0" * SHA256_HEX_BYTES
    if root["header"]["session"] != root["header"]["segment"] or \
            root["header"]["prev_sha256"] != zero or \
            root["header"]["start_order"] != 1 or \
            root["header"]["start_command"] != 1 or \
            root["header"]["start_hook_event"] != 1:
        raise ValueError("v3 root segment has an invalid zero anchor")
    by_predecessor: dict[str, dict[str, Any]] = {}
    root_identity = _v3_stable_identity(root["header"])
    session = root["header"]["session"]
    for segment in segments:
        header = segment["header"]
        if header["session"] != session or \
                _v3_stable_identity(header) != root_identity:
            raise ValueError("v3 continuation changes the session identity")
        if header["continuation"] == 0:
            continue
        predecessor = header["prev_sha256"]
        if predecessor == zero or predecessor in by_predecessor:
            raise ValueError("v3 continuation predecessor is ambiguous")
        by_predecessor[predecessor] = segment

    ordered: list[dict[str, Any]] = []
    consumed: set[Path] = set()
    current = root
    while True:
        if current["path"] in consumed:
            raise ValueError("v3 continuation chain contains a cycle")
        consumed.add(current["path"])
        ordered.append(current)
        if current["end"] is not None:
            if current["last_digest"] in by_predecessor:
                raise ValueError("v3 terminal segment has a continuation")
            break
        next_segment = by_predecessor.get(current["last_digest"])
        if next_segment is None:
            raise ValueError("v3 segment chain ends before a terminal end")
        header = next_segment["header"]
        if header["segment"] <= current["header"]["segment"] or \
                header["start_order"] != current["last_order"] + 1 or \
                header["start_command"] != current["last_command"] + 1 or \
                header["start_hook_event"] != current["last_hook_event"] + 1:
            raise ValueError("v3 continuation range does not follow its predecessor")
        current = next_segment
    if len(consumed) != len(segments):
        raise ValueError("v3 session contains an unlinked continuation segment")

    terminal = ordered[-1]
    if terminal["end"] is None:
        raise ValueError("v3 trace lacks terminal end")
    header = terminal["header"]
    return {
        "identity": {
            key: header[key] for key in (
                "map", "bsp_checksum", "entity_crc32", "physics_id",
                "host_physics_id", "gravity_bits", "airaccelerate_bits",
                "maxvelocity_bits", "pmove_substep_ms", "server_frame_ms",
                "physics_flags", "module_revision", "module_version")
        },
        "trace_header": header,
        "initial_trace_header": root["header"],
        "segment_headers": [segment["header"] for segment in ordered],
        "trace_format": TRACE_FORMAT_V3,
        "steps": [step for segment in ordered for step in segment["steps"]],
        "hook_events": [event for segment in ordered
                        for event in segment["hook_events"]],
        "terminal_sha256": terminal["last_digest"],
        "end": terminal["end"],
        "ordinal": ordinal,
        "source_paths": [segment["path"] for segment in ordered],
    }


def _read_v3_segment_set(path: Path) -> list[dict[str, Any]]:
    selected_header = _v3_header_only(path)
    sibling = _v3_segment_prefix(path)
    by_segment: dict[int, Path] = {}
    if sibling is None:
        paths = [path]
    else:
        directory, prefix = sibling
        for candidate in directory.iterdir():
            if not candidate.is_file() or not candidate.name.startswith(prefix):
                continue
            if not re.fullmatch(
                    re.escape(prefix) +
                    r"(?:[0-9]{6}|[1-9][0-9]{6,})\.jsonl",
                    candidate.name):
                continue
            segment = _v3_filename_segment(candidate)
            if segment is None:
                raise ValueError("v3 sibling has a noncanonical segment name")
            if segment in by_segment:
                raise ValueError("v3 sibling segment filename is ambiguous")
            by_segment[segment] = candidate
        paths = [candidate for _, candidate in sorted(by_segment.items())]
    if path not in paths:
        selected_segment = _v3_filename_segment(path)
        if selected_segment is None:
            raise ValueError("selected v3 path has a noncanonical segment name")
        if selected_segment in by_segment:
            raise ValueError("selected v3 segment filename is ambiguous")
        by_segment[selected_segment] = path
        paths = [candidate for _, candidate in sorted(by_segment.items())]
    segments: list[dict[str, Any]] = []
    for candidate in paths:
        try:
            header = _v3_header_only(candidate)
        except ValueError:
            # An existing filename collision or interrupted unrelated capture
            # has no authenticated v3 header with which it could join this
            # selected session. A malformed selected path is never ignored.
            if candidate == path:
                raise
            continue
        if header["session"] != selected_header["session"]:
            continue
        segments.append(_validate_v3_segment(candidate))
    for segment in segments:
        filename_segment = _v3_filename_segment(segment["path"])
        if filename_segment is None or \
                filename_segment != segment["header"]["segment"]:
            raise ValueError("v3 header segment does not match its filename")
    if not segments:
        raise ValueError("trace contains no selected v3 session")
    return [_assemble_v3_session(segments, 1)]


def read_sessions(path: Path) -> list[dict[str, Any]]:
    """Read legacy streams or a complete, zero-rooted v3 segment set."""
    if path.is_dir():
        candidates = sorted(candidate for candidate in path.glob("*.jsonl")
                            if candidate.is_file())
        if not candidates:
            raise ValueError("trace directory contains no segments")
        # A collision can leave an arbitrary or v3-looking file at the lowest
        # name. Do not choose it from a shallow label: fully authenticate one
        # complete zero-rooted session before selecting it for import.
        for candidate in candidates:
            try:
                if _first_v3_record(candidate):
                    return _read_v3_segment_set(candidate)
            except ValueError:
                continue
        path = candidates[0]
    if _first_v3_record(path):
        return _read_v3_segment_set(path)
    return _read_sessions_single(path)


def select_session(sessions: list[dict[str, Any]], ordinal: str,
                   map_name: str | None) -> dict[str, Any]:
    candidates = [session for session in sessions
                  if map_name is None or session["identity"]["map"] == map_name]
    if not candidates:
        raise ValueError("no trace session matches the requested map")
    if ordinal == "latest":
        return candidates[-1]
    wanted = integer(int(ordinal), "session", 1, len(sessions))
    selected = sessions[wanted - 1]
    if map_name is not None and selected["identity"]["map"] != map_name:
        raise ValueError("selected session does not match the requested map")
    return selected


def replay_segments(steps: list[dict[str, Any]]) -> list[dict[str, Any]]:
    segments: list[dict[str, Any]] = []
    start = 0
    reason = "trace-start"
    for index, step in enumerate(steps):
        if index == 0:
            continue
        previous = steps[index - 1]
        boundary = None
        if step["frame"] < previous["frame"]:
            boundary = "frame-rewind"
        elif step["snapinitial"]:
            boundary = "snapinitial"
        elif previous["after"] != step["before"]:
            boundary = "authoritative-state-change"
        if boundary:
            segments.append({
                "start_index": start,
                "end_exclusive": index,
                "reason": reason,
            })
            start = index
            reason = boundary
    if steps:
        segments.append({
            "start_index": start,
            "end_exclusive": len(steps),
            "reason": reason,
        })
    return segments


def build_evidence_v3(path: Path, session: dict[str, Any],
                      client: int | None, first_frame: int | None,
                      last_frame: int | None) -> dict[str, Any]:
    clients = sorted({step["client"] for step in session["steps"]})
    if client is None:
        if len(clients) != 1:
            raise ValueError(f"select one client from {clients}")
        client = clients[0]
    steps = [step for step in session["steps"] if step["client"] == client]
    if first_frame is not None:
        steps = [step for step in steps if step["frame"] >= first_frame]
    if last_frame is not None:
        steps = [step for step in steps if step["frame"] <= last_frame]
    if not steps:
        raise ValueError("selected trace window contains no steps")
    orders = [step["order"] for step in steps]
    if len(orders) != len(set(orders)) or orders != sorted(orders):
        raise ValueError("selected trace window is not order ordered")
    generations = {step["spawn_generation"] for step in steps}
    if len(generations) != 1:
        raise ValueError("selected trace window spans multiple playthroughs")
    frame_window = (steps[0]["frame"], steps[-1]["frame"])
    spawn_generation = next(iter(generations))
    hook_events = [
        event for event in session["hook_events"]
        if event["client"] == client and
        event["spawn_generation"] == spawn_generation and
        frame_window[0] <= event["frame"] <= frame_window[1]
    ]
    header = session["trace_header"]
    end = session["end"]
    source_paths = session.get("source_paths", [path])
    if not isinstance(source_paths, list) or not source_paths:
        raise ValueError("v3 session has no authenticated segment sources")
    source_digest = hashlib.sha256()
    source_basenames: list[str] = []
    for source_path in source_paths:
        if not isinstance(source_path, Path):
            raise ValueError("v3 session has an invalid segment source")
        payload = source_path.read_bytes()
        source_digest.update(source_path.name.encode("utf-8"))
        source_digest.update(b"\0")
        source_digest.update(payload)
        source_basenames.append(source_path.name)
    trace = {
        "format": TRACE_FORMAT_V3,
        "terminal_sha256": session["terminal_sha256"],
        "session": header["session"],
        "segment": header["segment"],
        "continuation": header["continuation"],
        "map": header["map"],
        "bsp_checksum": header["bsp_checksum"],
        "entity_crc32": header["entity_crc32"],
        "host_physics_id": header["host_physics_id"],
        "gravity_bits": header["gravity_bits"],
        "airaccelerate_bits": header["airaccelerate_bits"],
        "maxvelocity_bits": header["maxvelocity_bits"],
        "pmove_substep_ms": header["pmove_substep_ms"],
        "server_frame_ms": header["server_frame_ms"],
        "physics_flags": header["physics_flags"],
        "module_revision": header["module_revision"],
        "module_version": header["module_version"],
        "end_order": end["order"],
        "end_frame": end["frame"],
        "end_level_time_bits": end["level_time_bits"],
    }
    return {
        "format": EVIDENCE_FORMAT_V3,
        "identity": session["identity"],
        "source": {
            "basename": source_basenames[0],
            "basenames": source_basenames,
            "sha256": source_digest.hexdigest(),
            "session": session["ordinal"],
        },
        "trace": trace,
        "authenticated": {
            "algorithm": "sha256(previous_digest || canonical_payload)",
            "complete": True,
            "terminal_sha256": session["terminal_sha256"],
        },
        "client": client,
        "spawn_generation": spawn_generation,
        "frame_window": list(frame_window),
        "order_window": [orders[0], orders[-1]],
        "segments": replay_segments(steps),
        "steps": steps,
        "hook_events": hook_events,
    }


def build_evidence(path: Path, session: dict[str, Any], client: int | None,
                   first_frame: int | None,
                   last_frame: int | None) -> dict[str, Any]:
    if session.get("trace_format") == TRACE_FORMAT_V3:
        return build_evidence_v3(path, session, client, first_frame,
                                 last_frame)
    clients = sorted({step["client"] for step in session["steps"]})
    if client is None:
        if len(clients) != 1:
            raise ValueError(f"select one client from {clients}")
        client = clients[0]
    steps = [step for step in session["steps"] if step["client"] == client]
    if first_frame is not None:
        steps = [step for step in steps if step["frame"] >= first_frame]
    if last_frame is not None:
        steps = [step for step in steps if step["frame"] <= last_frame]
    if not steps:
        raise ValueError("selected trace window contains no steps")
    sequences = [step["seq"] for step in steps]
    if len(sequences) != len(set(sequences)):
        raise ValueError("selected trace window contains duplicate sequences")
    if sequences != sorted(sequences):
        raise ValueError("selected trace window is not sequence ordered")
    payload = path.read_bytes()
    frame_window = (steps[0]["frame"], steps[-1]["frame"])
    binding = session.get("rune_bindings", [])
    start_hook_event = (binding[0].get("start_hook_event", 1)
                        if binding else 1)
    hook_events = [
        event for event in session.get("hook_events", [])
        if event["client"] == client and
        frame_window[0] <= event["frame"] <= frame_window[1] and
        event["event"] >= start_hook_event
    ]
    return {
        "format": (EVIDENCE_FORMAT_V2
                   if session.get("trace_format") == TRACE_FORMAT_V2
                   else EVIDENCE_FORMAT_V1),
        "identity": session["identity"],
        "source": {
            "basename": path.name,
            "sha256": hashlib.sha256(payload).hexdigest(),
            "session": session["ordinal"],
        },
        "client": client,
        "frame_window": list(frame_window),
        "rune_bindings": list(session.get("rune_bindings", [])),
        "segments": replay_segments(steps),
        "steps": steps,
        **({"hook_events": hook_events}
           if session.get("trace_format") == TRACE_FORMAT_V2 else {}),
    }


def atomic_write(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(value, output, indent=2, sort_keys=True)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def main() -> int:
    parser = argparse.ArgumentParser(
        description="import an exact LMCTF human Pmove trace")
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--session", default="latest")
    parser.add_argument("--map")
    parser.add_argument("--client", type=int)
    parser.add_argument("--from-frame", type=int)
    parser.add_argument("--through-frame", type=int)
    args = parser.parse_args()
    try:
        session = select_session(
            read_sessions(args.trace), args.session, args.map)
        evidence = build_evidence(
            args.trace, session, args.client,
            args.from_frame, args.through_frame)
        atomic_write(args.output, evidence)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(
        f"WROTE {args.output}: map={evidence['identity']['map']} "
        f"client={evidence['client']} steps={len(evidence['steps'])} "
        f"segments={len(evidence['segments'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
