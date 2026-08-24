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
TRACE_FORMAT = TRACE_FORMAT_V1
EVIDENCE_FORMAT_V1 = "lmctf-human-replay-evidence-v1"
EVIDENCE_FORMAT_V2 = "lmctf-human-replay-evidence-v2"
EVIDENCE_FORMAT = EVIDENCE_FORMAT_V1
MAX_HOOK_EVENTS = 16384
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
            0, 0xFFFFFFFF),
        "entity_crc32": integer(
            value.get("entity_crc32"), f"line {line} entity_crc32",
            0, 0xFFFFFFFF),
        "physics_id": integer(
            value.get("physics_id"), f"line {line} physics_id",
            1, 0xFFFFFFFF),
        "module_revision": integer(
            value.get("module_revision"), f"line {line} module_revision",
            0, 0x7FFFFFFF),
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
                          1, 0x7FFFFFFF),
        "frame": integer(value.get("frame"), f"line {line} frame",
                         0, 0x7FFFFFFF),
        "snapinitial": integer(
            value.get("snapinitial"), f"line {line} snapinitial", 0, 1),
        "cmd": validate_command(value.get("cmd"), f"line {line} cmd"),
        "before": validate_state(
            value.get("before"), f"line {line} before"),
        "after": validate_state(value.get("after"), f"line {line} after"),
        "ground": integer(value.get("ground"), f"line {line} ground",
                          -1, 0x7FFFFFFF),
        "waterlevel": integer(
            value.get("waterlevel"), f"line {line} waterlevel", 0, 3),
        "watertype": integer(
            value.get("watertype"), f"line {line} watertype",
            -0x80000000, 0x7FFFFFFF),
        "touches": [integer(item, f"line {line} touches[{index}]",
                            -1, 0x7FFFFFFF)
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
                          1, 0x7FFFFFFF),
        "frame": integer(value.get("frame"), f"line {line} frame",
                         0, 0x7FFFFFFF),
        "hook": integer(value.get("hook"), f"line {line} hook",
                        1, 0x7FFFFFFF),
    }
    if kind == "hook-fire":
        result.update({
            "origin_q8": integer_vector(value.get("origin_q8"),
                                        f"line {line} origin_q8",
                                        -0x80000000, 0x7FFFFFFF),
            "velocity_q8": integer_vector(value.get("velocity_q8"),
                                          f"line {line} velocity_q8",
                                          -0x80000000, 0x7FFFFFFF),
            "view_short": integer_vector_2(value.get("view_short"),
                                            f"line {line} view_short",
                                            -32768, 32767),
            "hand": integer(value.get("hand"), f"line {line} hand", 0, 2),
        })
    elif kind == "hook-attach":
        result.update({
            "bite_q8": integer_vector(value.get("bite_q8"),
                                      f"line {line} bite_q8",
                                      -0x80000000, 0x7FFFFFFF),
            "target": integer(value.get("target"), f"line {line} target",
                              0, 0x7FFFFFFF),
            "world": integer(value.get("world"), f"line {line} world", 0, 1),
        })
        if bool(result["world"]) != (result["target"] == 0):
            raise ValueError(f"line {line}: hook world target mismatch")
    else:
        result.update({
            "origin_q8": integer_vector(value.get("origin_q8"),
                                        f"line {line} origin_q8",
                                        -0x80000000, 0x7FFFFFFF),
            "velocity_q8": integer_vector(value.get("velocity_q8"),
                                          f"line {line} velocity_q8",
                                          -0x80000000, 0x7FFFFFFF),
        })
    return result


def read_sessions(path: Path) -> list[dict[str, Any]]:
    sessions: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    with path.open("r", encoding="utf-8") as source:
        for line_number, raw in enumerate(source, 1):
            if not raw.strip():
                continue
            try:
                value = json.loads(raw)
            except json.JSONDecodeError as error:
                raise ValueError(f"line {line_number}: invalid JSON: {error}") \
                    from error
            if isinstance(value, dict) and value.get("kind") == "header":
                trace_format = value.get("format")
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
            else:
                if current is None:
                    raise ValueError(
                        f"line {line_number}: record precedes header")
                kind = value.get("kind") if isinstance(value, dict) else None
                if kind == "rune-bind":
                    if current["rune_bindings"]:
                        raise ValueError(
                            f"line {line_number}: duplicate rune binding")
                    current["rune_bindings"].append(validate_rune_bind(
                        value, line_number, current["identity"],
                        current["trace_format"]))
                elif kind == "step":
                    step = validate_step(
                        value, line_number, current["trace_format"])
                    current["steps"].append(step)
                    current["greatest_step"] = max(
                        current["greatest_step"], step["seq"])
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
                    if len(current["hook_events"]) >= MAX_HOOK_EVENTS:
                        raise ValueError("trace hook event capacity exceeded")
                    current["hook_events"].append(event)
                    current["last_hook_event"] = event["event"]
                    current["last_hook_after_step"] = event["after_step"]
                else:
                    raise ValueError(
                        f"line {line_number}: unknown trace record kind")
    if not sessions:
        raise ValueError("trace contains no sessions")
    return sessions


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


def build_evidence(path: Path, session: dict[str, Any], client: int | None,
                   first_frame: int | None,
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
