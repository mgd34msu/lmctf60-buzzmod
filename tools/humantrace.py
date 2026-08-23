#!/usr/bin/env python3
"""Validate exact server-side human Pmove traces and publish replay evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
from typing import Any


TRACE_FORMAT = "lmctf-human-trace-v1"
EVIDENCE_FORMAT = "lmctf-human-replay-evidence-v1"
STATE_FIELDS = {
    "type", "origin", "velocity", "flags", "time", "gravity",
    "delta_angles",
}
COMMAND_FIELDS = {
    "msec", "buttons", "angles", "forward", "side", "up", "impulse",
    "light",
}


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
    if not isinstance(value, dict) or value.get("format") != TRACE_FORMAT or \
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


def validate_step(value: Any, line: int) -> dict[str, Any]:
    if not isinstance(value, dict) or value.get("format") != TRACE_FORMAT or \
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
                current = {
                    "identity": validate_header(value, line_number),
                    "steps": [],
                    "ordinal": len(sessions) + 1,
                }
                sessions.append(current)
            else:
                if current is None:
                    raise ValueError(f"line {line_number}: step precedes header")
                current["steps"].append(validate_step(value, line_number))
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
    return {
        "format": EVIDENCE_FORMAT,
        "identity": session["identity"],
        "source": {
            "basename": path.name,
            "sha256": hashlib.sha256(payload).hexdigest(),
            "session": session["ordinal"],
        },
        "client": client,
        "frame_window": [steps[0]["frame"], steps[-1]["frame"]],
        "segments": replay_segments(steps),
        "steps": steps,
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
