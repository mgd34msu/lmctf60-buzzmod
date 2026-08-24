#!/usr/bin/env python3
"""Own ten persistent LMCTF engines and verify their stopped evidence.

The runner is intentionally independent of the development fleet scripts.  A
future bundle transaction installs the immutable files and writes the run
specification consumed here; this tool never deploys or discovers a module.
"""

from __future__ import annotations

import argparse
import ctypes
from dataclasses import dataclass
import fcntl
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import select
import signal
import sqlite3
import stat
import subprocess
import sys
import time
from typing import Any, Mapping, Sequence


LANES = tuple(f"s{index:02d}" for index in range(1, 11))
OFFSETS = tuple(range(10))
ROUTE_ONLY_LANES = tuple(f"r{index:02d}" for index in range(1, 11))
ROUTE_ONLY_MAPS = (
    "lmctf01", "lmctf06", "lmctf12", "lmctf15", "lmctf19",
    "lmctf25", "tomb05", "xmap13", "xmap18", "xmap26",
)
TOPMAPS_PATH = Path(__file__).with_name("topmaps.txt")
MAX_JSON_BYTES = 64 * 1024 * 1024
ZERO_HASH = "0" * 64
FORMAT_OWNER = "lmctf-fleet-owner-v1"
FORMAT_RECEIPT = "lmctf-fleet-residence-v1"
FORMAT_LEDGER = "lmctf-fleet-ledger-entry-v1"
FORMAT_SPEC = "lmctf-fleet-run-spec-v1"
FORMAT_ROUTE_OWNER = "lmctf-route-only-owner-v2"
FORMAT_ROUTE_RECEIPT = "lmctf-route-only-receipt-v2"
FORMAT_ROUTE_LEDGER = "lmctf-route-only-ledger-entry-v2"
FORMAT_ROUTE_SPEC = "lmctf-route-only-run-spec-v2"
SAFE_ATOM = frozenset(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-"
)


class FleetError(ValueError):
    """A fail-closed production fleet contract violation."""


def _read_topmaps() -> tuple[tuple[str, ...], str]:
    payload = TOPMAPS_PATH.read_bytes()
    try:
        lines = payload.decode("ascii").splitlines()
    except UnicodeDecodeError as exc:
        raise FleetError("topmaps authority is not ASCII") from exc
    maps = tuple(line for line in lines if line and not line.startswith("#"))
    if (len(maps) != 20 or len(set(maps)) != 20 or
            any(name != name.lower() or not name or
                any(character not in SAFE_ATOM for character in name)
                for name in maps)):
        raise FleetError("topmaps authority is not an exact safe 20-map list")
    return maps, hashlib.sha256(payload).hexdigest()


CANONICAL_TOPMAPS, CANONICAL_TOPMAPS_SHA256 = _read_topmaps()


def _reject_development_controller_environment() -> None:
    names = ("LMCTF_WAVEWATCH_ACTIVE", "LMCTF_WAVELOOP_ACTIVE")
    active = [name for name in names if os.environ.get(name)]
    if active:
        raise FleetError(
            "wavewatch/waveloop development controller must be stopped before "
            "the production fleet runner starts"
        )


def expected_map(lane: str, sequence: int) -> str:
    if lane not in LANES or type(sequence) is not int or sequence < 0:
        raise FleetError("invalid lane sequence")
    lane_index = LANES.index(lane)
    return CANONICAL_TOPMAPS[(OFFSETS[lane_index] + sequence) % 20]


def _canonical(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
        allow_nan=False,
    ).encode("ascii") + b"\n"


def _hash(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _hash_argv(argv: Sequence[str]) -> str:
    return _hash(b"\0".join(item.encode("utf-8") for item in argv) + b"\0")


def receipt_hash(receipt: Mapping[str, Any]) -> str:
    value = dict(receipt)
    value.pop("receipt_hash", None)
    return _hash(_canonical(value))


def ledger_entry_hash(entry: Mapping[str, Any]) -> str:
    value = dict(entry)
    value.pop("entry_hash", None)
    return _hash(_canonical(value))


def _no_duplicate_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise FleetError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _read_regular(path: Path, maximum: int = MAX_JSON_BYTES) -> tuple[bytes, os.stat_result]:
    try:
        before = path.lstat()
    except OSError as exc:
        raise FleetError(f"cannot stat required file {path}: {exc}") from exc
    if (not stat.S_ISREG(before.st_mode) or stat.S_ISLNK(before.st_mode) or
            before.st_nlink != 1 or before.st_size < 0 or before.st_size > maximum):
        raise FleetError(f"required path is not one bounded regular file: {path}")
    flags = os.O_RDONLY | os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(path, flags)
    except OSError as exc:
        raise FleetError(f"cannot open required file {path}: {exc}") from exc
    try:
        opened = os.fstat(fd)
        chunks = []
        remaining = maximum + 1
        while remaining:
            chunk = os.read(fd, min(65536, remaining))
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        after = os.fstat(fd)
    finally:
        os.close(fd)
    payload = b"".join(chunks)
    identity = lambda info: (
        info.st_dev, info.st_ino, info.st_size, stat.S_IMODE(info.st_mode),
        info.st_uid, info.st_gid, info.st_nlink, info.st_mtime_ns,
        info.st_ctime_ns,
    )
    if (len(payload) > maximum or identity(before) != identity(opened) or
            identity(opened) != identity(after)):
        raise FleetError(f"required file changed while reading: {path}")
    return payload, after


def _read_json(path: Path) -> tuple[dict, bytes]:
    payload, _info = _read_regular(path)
    try:
        value = json.loads(
            payload.decode("ascii"), object_pairs_hook=_no_duplicate_object,
            parse_constant=lambda token: (_ for _ in ()).throw(
                FleetError(f"non-finite JSON number {token}")),
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise FleetError(f"invalid canonical JSON file {path}") from exc
    if not isinstance(value, dict) or payload != _canonical(value):
        raise FleetError(f"JSON file is not one canonical object: {path}")
    return value, payload


def _safe_root(path_value: os.PathLike[str] | str, label: str) -> Path:
    path = Path(path_value)
    if not path.is_absolute():
        path = path.absolute()
    current = Path(path.anchor)
    for part in path.parts[1:]:
        if part in {"", ".", ".."}:
            raise FleetError(f"unsafe {label} path")
        current /= part
        try:
            info = current.lstat()
        except OSError as exc:
            raise FleetError(f"missing {label} component {current}") from exc
        if stat.S_ISLNK(info.st_mode):
            raise FleetError(f"symlink in {label} path: {current}")
    if not path.is_dir():
        raise FleetError(f"{label} is not a directory")
    return path


def _require_frozen_tree(root: Path, label: str) -> None:
    for directory, names, files in os.walk(root, followlinks=False):
        directory_path = Path(directory)
        info = directory_path.lstat()
        if not stat.S_ISDIR(info.st_mode) or stat.S_IMODE(info.st_mode) & 0o222:
            raise FleetError(f"{label} contains a mutable directory")
        for name in names:
            child = (directory_path / name).lstat()
            if stat.S_ISLNK(child.st_mode) or not stat.S_ISDIR(child.st_mode):
                raise FleetError(f"{label} contains an unsafe directory entry")
        for name in files:
            child = (directory_path / name).lstat()
            if (not stat.S_ISREG(child.st_mode) or stat.S_ISLNK(child.st_mode) or
                    stat.S_IMODE(child.st_mode) & 0o222):
                raise FleetError(f"{label} contains a mutable file")


def _inside(path: Path, root: Path, label: str) -> Path:
    absolute = path if path.is_absolute() else root / path
    absolute = absolute.absolute()
    try:
        relative = absolute.relative_to(root)
    except ValueError as exc:
        raise FleetError(f"{label} escapes its evidence root") from exc
    if not relative.parts or any(part in {"", ".", ".."} for part in relative.parts):
        raise FleetError(f"unsafe {label} path")
    current = root
    for part in relative.parts:
        current /= part
        try:
            info = current.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode):
            raise FleetError(f"symlink in {label} path: {current}")
    return absolute


def _trees_overlap(left: Path, right: Path) -> bool:
    """Return whether two validated evidence trees share writable space."""
    try:
        left.relative_to(right)
        return True
    except ValueError:
        try:
            right.relative_to(left)
            return True
        except ValueError:
            return False


def _file_record(path: Path) -> dict:
    payload, info = _read_regular(path, maximum=2 * 1024 * 1024 * 1024)
    return {
        "path": str(path.resolve()),
        "size": len(payload),
        "sha256": _hash(payload),
        "device": info.st_dev,
        "inode": info.st_ino,
    }


def _verify_file_record(record: Any, label: str, *, evidence_root: Path | None = None) -> Path:
    if not isinstance(record, dict) or set(record) != {
            "path", "size", "sha256", "device", "inode"}:
        raise FleetError(f"invalid {label} file identity")
    if (type(record["size"]) is not int or record["size"] < 0 or
            type(record["device"]) is not int or type(record["inode"]) is not int or
            not _valid_hash(record["sha256"]) or not isinstance(record["path"], str)):
        raise FleetError(f"invalid {label} file identity fields")
    path = Path(record["path"])
    if evidence_root is not None:
        path = _inside(path, evidence_root, label)
    current = _file_record(path)
    if current != record:
        raise FleetError(f"{label} file identity drift")
    return path


def _verify_installed_bundle(record: Any) -> tuple[dict, dict]:
    state_path = _verify_file_record(record, "installed bundle state")
    verifier_path = Path(__file__).with_name("server_bundle.py")
    verifier_record = _file_record(verifier_path)
    module_spec = importlib.util.spec_from_file_location(
        f"_lmctf_server_bundle_{verifier_record['sha256']}", verifier_path
    )
    if module_spec is None or module_spec.loader is None:
        raise FleetError("cannot load the installed bundle verifier")
    module = importlib.util.module_from_spec(module_spec)
    try:
        module_spec.loader.exec_module(module)
        state = module.verify_state_file(state_path)
    except Exception as exc:
        raise FleetError(f"installed bundle authority is invalid: {exc}") from exc
    if state.get("state_file") != record or not isinstance(state.get("active"), dict):
        raise FleetError("installed bundle state identity drift")
    return state["active"], verifier_record


def _bundle_role_records(active: Mapping[str, Any]) -> dict[str, dict]:
    files = active.get("files")
    if not isinstance(files, list):
        raise FleetError("installed bundle file inventory is invalid")
    roles = {}
    for record in files:
        if not isinstance(record, dict) or not isinstance(record.get("role"), str):
            raise FleetError("installed bundle file record is invalid")
        role = record["role"]
        if role in roles:
            raise FleetError("installed bundle contains a duplicate role")
        roles[role] = record
    return roles


def _verify_bundle_copy(record: Any, roles: Mapping[str, dict],
                        role: str, label: str) -> Path:
    path = _verify_file_record(record, label)
    authority = roles.get(role)
    if (not isinstance(authority, dict) or record["size"] != authority.get("size") or
            record["sha256"] != authority.get("sha256")):
        raise FleetError(f"{label} differs from installed bundle role {role}")
    return path


def _valid_hash(value: Any) -> bool:
    return (isinstance(value, str) and len(value) == 64 and
            all(character in "0123456789abcdef" for character in value))


def _integer(value: Any, label: str, minimum: int = 0) -> int:
    if type(value) is not int or value < minimum:
        raise FleetError(f"invalid {label}")
    return value


def _process_current(process: Mapping[str, Any]) -> bool:
    pid = _integer(process.get("pid"), "process pid", 1)
    try:
        boot_id = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
        line = Path(f"/proc/{pid}/stat").read_text()
    except (FileNotFoundError, ProcessLookupError):
        return False
    except OSError as exc:
        raise FleetError(f"cannot authenticate stopped process {pid}: {exc}") from exc
    closing = line.rfind(")")
    fields = line[closing + 2:].split() if closing >= 0 else []
    if len(fields) <= 19:
        raise FleetError(f"malformed process identity for {pid}")
    return (boot_id == process.get("boot_id") and
            int(fields[19]) == process.get("start_ticks"))


def _verify_process(process: Any, lane: str, engine: Mapping[str, Any]) -> None:
    required = {
        "pid", "boot_id", "start_ticks", "executable", "argv",
        "command_sha256", "pidfd_captured",
    }
    if not isinstance(process, dict) or set(process) != required:
        raise FleetError(f"invalid engine generation for {lane}")
    if (not isinstance(process["boot_id"], str) or
            _integer(process["start_ticks"], "process start ticks", 1) < 1 or
            process["executable"] != engine or
            process["pidfd_captured"] is not True or
            not isinstance(process["argv"], list) or not process["argv"] or
            any(not isinstance(item, str) or not item or "\0" in item
                for item in process["argv"]) or
            process["command_sha256"] != _hash_argv(process["argv"])):
        raise FleetError(f"engine generation identity drift for {lane}")
    if _process_current(process):
        raise FleetError(f"engine generation for {lane} is still active")


def _verify_unheld_lock(path: Path) -> None:
    flags = os.O_RDONLY | os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(path, flags)
    except OSError as exc:
        raise FleetError(f"cannot open fleet lock: {exc}") from exc
    try:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise FleetError("fleet lock is still held") from exc
        finally:
            try:
                fcntl.flock(fd, fcntl.LOCK_UN)
            except OSError:
                pass
    finally:
        os.close(fd)


def _verify_players(players: Any) -> None:
    if not isinstance(players, list) or len(players) != 10:
        raise FleetError("residence does not contain the exact ten-player roster")
    names, clients, slots = set(), set(), set()
    teams = {1: 0, 2: 0}
    for player in players:
        if not isinstance(player, dict) or set(player) != {
                "client", "instance", "name", "slot", "team"}:
            raise FleetError("invalid residence player")
        name = player["name"]
        if (not isinstance(name, str) or not name or not name.startswith("[SG]") or
                not isinstance(player["instance"], str) or not player["instance"]):
            raise FleetError("residence contains a non-SG player identity")
        client = _integer(player["client"], "player client", 1)
        slot = _integer(player["slot"], "player slot")
        team = player["team"]
        if team not in teams:
            raise FleetError("residence contains an invalid CTF team")
        teams[team] += 1
        names.add(name)
        clients.add(client)
        slots.add(slot)
    if len(names) != 10 or len(clients) != 10 or len(slots) != 10 or teams != {1: 5, 2: 5}:
        raise FleetError("residence roster is duplicate, missing, or unbalanced")


def _vector(value: Any, label: str) -> None:
    if (not isinstance(value, list) or len(value) != 3 or
            any(type(item) not in (int, float) or not math.isfinite(item)
                for item in value)):
        raise FleetError(f"invalid {label}")


def _verify_receipt(receipt: dict, path: Path, evidence_root: Path,
                    owner: dict, bundle_roles: Mapping[str, dict],
                    lane: str, sequence: int) -> None:
    required = {
        "format", "fleet_id", "bundle_id", "lane", "offset", "sequence", "map",
        "runner_sha256", "topmaps_sha256", "engine_generation",
        "client_generation", "bsp_file", "rune_file", "rune_sha256",
        "snag_file", "sg_players", "residence", "serverrecord",
        "console_segment", "pov", "receipt_hash",
    }
    if set(receipt) != required:
        raise FleetError("residence receipt has unknown or missing fields")
    if receipt.get("format") != FORMAT_RECEIPT:
        raise FleetError("invalid residence receipt format")
    if (receipt.get("fleet_id") != owner["fleet_id"] or
            receipt.get("bundle_id") != owner["bundle_id"] or
            receipt.get("lane") != lane or receipt.get("offset") != OFFSETS[LANES.index(lane)] or
            receipt.get("sequence") != sequence or
            receipt.get("map") != expected_map(lane, sequence) or
            receipt.get("runner_sha256") != owner["runner_sha256"] or
            receipt.get("topmaps_sha256") != CANONICAL_TOPMAPS_SHA256 or
            receipt.get("engine_generation") != owner["processes"][lane] or
            receipt.get("client_generation") != owner["clients"][lane] or
            receipt.get("receipt_hash") != receipt_hash(receipt)):
        raise FleetError(f"residence authority drift for {lane}/{sequence}")
    name = receipt["map"]
    _verify_bundle_copy(receipt.get("bsp_file"), bundle_roles, f"bsp:{name}", "BSP")
    rune = receipt.get("rune_file")
    _verify_bundle_copy(rune, bundle_roles, f"rune:{name}", "RUNE")
    _verify_bundle_copy(receipt.get("snag_file"), bundle_roles, f"snag:{name}", "SNAG")
    if receipt.get("rune_sha256") != rune["sha256"]:
        raise FleetError("residence RUNE digest drift")
    if not Path(rune["path"]).name.lower().startswith(name.lower() + "."):
        raise FleetError("residence RUNE map name drift")
    _verify_players(receipt.get("sg_players"))
    residence = receipt.get("residence")
    if not isinstance(residence, dict):
        raise FleetError("missing residence frame authority")
    start = _integer(residence.get("start_frame"), "residence start frame")
    end = _integer(residence.get("end_frame"), "residence end frame", 1)
    if end <= start:
        raise FleetError("empty residence frame range")
    _vector(residence.get("red_flag_origin"), "red flag origin")
    _vector(residence.get("blue_flag_origin"), "blue flag origin")
    serverrecord = receipt.get("serverrecord")
    if not isinstance(serverrecord, dict):
        raise FleetError("missing serverrecord authority")
    if set(serverrecord) != {"demo_path", "demo_sha256", "demo_size",
                             "demo_frame_range"}:
        raise FleetError("invalid serverrecord authority fields")
    demo = _inside(Path(str(serverrecord["demo_path"])), evidence_root,
                   "serverrecord")
    demo_record = _file_record(demo)
    if (demo_record["sha256"] != serverrecord["demo_sha256"] or
            demo_record["size"] != serverrecord["demo_size"]):
        raise FleetError("serverrecord identity drift")
    frame_range = serverrecord.get("demo_frame_range")
    if (not isinstance(frame_range, dict) or set(frame_range) != {"start", "end_exclusive"} or
            _integer(frame_range["start"], "demo start frame", 1) < 1 or
            _integer(frame_range["end_exclusive"], "demo end frame", 2) <= frame_range["start"]):
        raise FleetError("invalid serverrecord frame range")
    segment = receipt.get("console_segment")
    if not isinstance(segment, dict) or set(segment) != {"path", "sha256", "size"}:
        raise FleetError("invalid console segment authority")
    segment_path = _inside(path.parent / "segments" / str(segment["path"]), evidence_root,
                           "console segment")
    segment_record = _file_record(segment_path)
    if segment_record["sha256"] != segment["sha256"] or segment_record["size"] != segment["size"]:
        raise FleetError("console segment identity drift")
    pov = receipt.get("pov")
    if (not isinstance(pov, dict) or set(pov) != {
            "demo_path", "demo_sha256", "demo_size", "spectator", "target",
            "start_confirmed", "stop_confirmed"} or
            pov.get("start_confirmed") is not True or
            pov.get("stop_confirmed") is not True or
            not isinstance(pov.get("spectator"), str) or not pov["spectator"] or
            not isinstance(pov.get("target"), str) or not pov["target"].startswith("[SG]")):
        raise FleetError("incomplete POV lifecycle")
    pov_path = _inside(Path(str(pov.get("demo_path"))), evidence_root, "POV demo")
    pov_record = _file_record(pov_path)
    if pov_record["sha256"] != pov.get("demo_sha256") or pov_record["size"] != pov.get("demo_size"):
        raise FleetError("POV demo identity drift")


def verify_stopped_residence_evidence(
        state_root: os.PathLike[str] | str,
        evidence_root: os.PathLike[str] | str,
        ) -> tuple[tuple[Path, dict], ...]:
    """Verify a complete stopped 10-lane native cycle and wrap authority."""
    _reject_development_controller_environment()
    state = _safe_root(state_root, "state root")
    evidence = _safe_root(evidence_root, "evidence root")
    _require_frozen_tree(state, "state root")
    _require_frozen_tree(evidence, "evidence root")
    owner, _owner_payload = _read_json(state / "fleet-owner.json")
    runner_sha = _hash(Path(__file__).read_bytes())
    if set(owner) != {"format", "state", "fleet_id", "bundle_id", "runner_sha256",
                      "topmaps_sha256", "release_monotonic_ns", "processes",
                      "clients", "inputs", "maplists", "ledger_entries",
                      "ledger_tail_hash", "lock_path"}:
        raise FleetError("stopped fleet owner has unknown or missing fields")
    if (owner.get("format") != FORMAT_OWNER or owner.get("state") != "SAFE_STOPPED" or
            not isinstance(owner.get("fleet_id"), str) or not owner["fleet_id"] or
            not _valid_hash(owner.get("bundle_id")) or
            owner.get("runner_sha256") != runner_sha or
            owner.get("topmaps_sha256") != CANONICAL_TOPMAPS_SHA256 or
            type(owner.get("release_monotonic_ns")) is not int or
            owner["release_monotonic_ns"] < 0 or
            owner.get("ledger_entries") != 210 or
            not _valid_hash(owner.get("ledger_tail_hash"))):
        raise FleetError("stopped fleet owner authority is invalid")
    lock_path = _inside(Path(str(owner.get("lock_path"))), state, "fleet lock")
    _verify_unheld_lock(lock_path)
    inputs = owner.get("inputs")
    if not isinstance(inputs, dict) or set(inputs) != {
            "engine", "client", "config", "film", "module_aliases", "runtime",
            "installed_bundle", "bundle_verifier"}:
        raise FleetError("stopped fleet input inventory is invalid")
    active_bundle, bundle_verifier = _verify_installed_bundle(inputs["installed_bundle"])
    if (active_bundle.get("bundle_id") != owner["bundle_id"] or
            inputs["bundle_verifier"] != bundle_verifier):
        raise FleetError("stopped fleet installed bundle identity drift")
    bundle_roles = _bundle_role_records(active_bundle)
    engine = inputs["engine"]
    _verify_file_record(engine, "engine")
    _verify_file_record(inputs["client"], "client")
    _verify_bundle_copy(inputs["config"], bundle_roles, "config", "config")
    _verify_file_record(inputs["film"], "film decoder")
    _verify_file_record(inputs["runtime"], "fleet runtime")
    current_runtime = _file_record(Path(__file__).with_name("fleet_runner_live.py"))
    if inputs["runtime"] != current_runtime:
        raise FleetError("stopped fleet runtime differs from verifier companion")
    aliases = inputs["module_aliases"]
    if not isinstance(aliases, list) or len(aliases) != 2:
        raise FleetError("fleet needs exactly two module aliases")
    for alias, role in zip(aliases, ("module-primary", "module-secondary"), strict=True):
        _verify_bundle_copy(alias, bundle_roles, role, "module alias")
    if aliases[0]["sha256"] != aliases[1]["sha256"]:
        raise FleetError("production module aliases differ")
    processes = owner.get("processes")
    if not isinstance(processes, dict) or set(processes) != set(LANES):
        raise FleetError("stopped fleet process inventory is incomplete")
    for lane in LANES:
        _verify_process(processes[lane], lane, engine)
    clients = owner.get("clients")
    if not isinstance(clients, dict) or set(clients) != set(LANES):
        raise FleetError("stopped fleet client inventory is incomplete")
    for lane in LANES:
        _verify_process(clients[lane], lane, inputs["client"])
    maplists = owner.get("maplists")
    if not isinstance(maplists, dict) or set(maplists) != set(LANES):
        raise FleetError("stopped fleet maplist inventory is incomplete")
    for lane in LANES:
        maplist = _verify_file_record(maplists[lane], f"{lane} maplist")
        _verify_bundle_copy(maplists[lane], bundle_roles, f"maplist:{lane}",
                            f"{lane} maplist")
        payload, _info = _read_regular(maplist, maximum=1024 * 1024)
        maps = tuple(line for line in payload.decode("ascii").splitlines() if line)
        expected = tuple(expected_map(lane, sequence) for sequence in range(20))
        if maps != expected:
            raise FleetError(f"{lane} stopped maplist rotation drift")
    ledger_payload, _ledger_info = _read_regular(
        evidence / "evidence-ledger.jsonl", maximum=512 * 1024 * 1024
    )
    lines = ledger_payload.splitlines(keepends=True)
    if len(lines) != 210 or any(not line.endswith(b"\n") for line in lines):
        raise FleetError("fleet ledger does not contain exactly 210 entries")
    receipts = []
    previous = ZERO_HASH
    for index, line in enumerate(lines):
        try:
            entry = json.loads(
                line.decode("ascii"), object_pairs_hook=_no_duplicate_object,
                parse_constant=lambda token: (_ for _ in ()).throw(
                    FleetError(f"non-finite JSON number {token}")),
            )
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise FleetError("invalid fleet ledger JSON") from exc
        if not isinstance(entry, dict) or line != _canonical(entry):
            raise FleetError("fleet ledger entry is not canonical")
        lane = LANES[index // 21]
        sequence = index % 21
        expected_relative = f"receipts/{lane}/{sequence:02d}/receipt.json"
        if (entry.get("format") != FORMAT_LEDGER or entry.get("index") != index or
                entry.get("previous_hash") != previous or
                entry.get("receipt_path") != expected_relative or
                not _valid_hash(entry.get("receipt_hash")) or
                entry.get("entry_hash") != ledger_entry_hash(entry)):
            raise FleetError(f"fleet ledger chain drift at entry {index}")
        receipt_path = _inside(Path(expected_relative), evidence, "residence receipt")
        receipt, _receipt_payload = _read_json(receipt_path)
        if receipt.get("receipt_hash") != entry["receipt_hash"]:
            raise FleetError("ledger receipt digest drift")
        _verify_receipt(
            receipt, receipt_path, evidence, owner, bundle_roles, lane, sequence
        )
        receipts.append((receipt_path, receipt))
        previous = entry["entry_hash"]
    if previous != owner["ledger_tail_hash"]:
        raise FleetError("fleet ledger tail differs from stopped owner")
    return tuple(receipts)


def expected_route_only_map(lane: str) -> str:
    """Return the one immutable ordinary-match map assigned to ``lane``."""
    if lane not in ROUTE_ONLY_LANES:
        raise FleetError("invalid route-only lane")
    return ROUTE_ONLY_MAPS[ROUTE_ONLY_LANES.index(lane)]


def _select_route_only_results(
        maps: Sequence[str], results: Mapping[str, tuple[str, dict]],
        ) -> dict[str, dict]:
    """Reduce one complete final corpus classification to its fixed exception set."""
    if len(maps) != len(set(maps)) or set(results) != set(maps):
        raise FleetError("route-only controller result inventory is incomplete")
    selected = {}
    for map_name in maps:
        value = results[map_name]
        if (not isinstance(value, tuple) or len(value) != 2 or
                value[0] not in {"PASS", "ROUTE_ONLY"} or not isinstance(value[1], dict)):
            raise FleetError("route-only controller classification is invalid")
        classification, item = value
        if classification == "ROUTE_ONLY":
            if map_name not in ROUTE_ONLY_MAPS:
                raise FleetError("route-only controller accepted a non-candidate")
            selected[map_name] = item
    return selected


def _selected_route_only_lanes(selected: Mapping[str, dict]) -> tuple[str, ...]:
    """Return the immutable lane order for the accepted exception subset."""
    expected = set(ROUTE_ONLY_MAPS)
    if not isinstance(selected, Mapping) or not set(selected).issubset(expected):
        raise FleetError("route-only selection has an unknown map")
    return tuple(lane for lane in ROUTE_ONLY_LANES
                 if expected_route_only_map(lane) in selected)


def _validate_route_only_lane_selection(
        lanes: Mapping[str, Mapping[str, Any]], selected: Mapping[str, dict],
        ) -> tuple[str, ...]:
    """Require the spec to name exactly the controller-selected candidate lanes."""
    lane_order = _selected_route_only_lanes(selected)
    expected = {lane: expected_route_only_map(lane) for lane in lane_order}
    actual = {
        lane: lane_spec.get("map") if isinstance(lane_spec, Mapping) else None
        for lane, lane_spec in lanes.items()
    }
    if actual != expected:
        raise FleetError("route-only lane selection differs from controller authority")
    return lane_order


def _load_route_helpers() -> tuple[Any, Any, Any]:
    """Load the pinned local RUNE decoder and telemetry reducers."""
    tools = str(Path(__file__).parent)
    if tools not in sys.path:
        sys.path.insert(0, tools)
    try:
        import runeio
        import rolestat
        import stallcensus
    except ImportError as exc:
        raise FleetError(f"cannot load route-only evidence helper: {exc}") from exc
    return runeio, rolestat, stallcensus


def _route_helper_records() -> dict[str, dict]:
    return {
        "runeio": _file_record(Path(__file__).with_name("runeio.py")),
        "rolestat": _file_record(Path(__file__).with_name("rolestat.py")),
        "stallcensus": _file_record(Path(__file__).with_name("stallcensus.py")),
    }


def _load_film_module(record: Any):
    path = _verify_file_record(record, "route-only film decoder")
    module_spec = importlib.util.spec_from_file_location(
        f"_lmctf_route_film_{record['sha256']}", path
    )
    if module_spec is None or module_spec.loader is None:
        raise FleetError("cannot load route-only film decoder")
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    if not callable(getattr(module, "walk_demo", None)):
        raise FleetError("route-only film decoder lacks walk_demo")
    return module


def _validate_route_controller_authority(
        authority: Any, bundle_roles: Mapping[str, dict], engine: Mapping[str, Any],
        aliases: Sequence[dict],
        ) -> dict[str, dict]:
    """Re-open every accepted ROUTE_ONLY controller result, fail closed.

    The controller owns RUNE acceptance. The ordinary-match receipt replays
    the whole final corpus authority, then retains only its fixed candidate
    maps classified local-only against the immutable snapshot and active bundle.
    """
    required = {
        "controller", "snapshot", "run_root", "fingerprint",
        "fingerprint_document", "results",
    }
    if not isinstance(authority, dict) or set(authority) != required:
        raise FleetError("route-only controller authority is incomplete")
    controller_path = Path(__file__).with_name("rune_corpus_controller.py")
    controller_record = _file_record(controller_path)
    if authority["controller"] != controller_record:
        raise FleetError("route-only controller bytes differ from authority")
    if (not isinstance(authority["snapshot"], str) or
            not isinstance(authority["run_root"], str) or
            not _valid_hash(authority["fingerprint"])):
        raise FleetError("route-only controller path or fingerprint is invalid")
    snapshot = _safe_root(authority["snapshot"], "controller snapshot")
    run_root = _safe_root(authority["run_root"], "controller run root")
    document = _verify_file_record(
        authority["fingerprint_document"], "controller fingerprint document"
    )
    if document != _inside(run_root / "fingerprint-document.json", run_root,
                           "controller fingerprint document"):
        raise FleetError("route-only controller fingerprint document path drift")
    document_bytes, _document_info = _read_regular(document)
    module_spec = importlib.util.spec_from_file_location(
        f"_lmctf_route_controller_{controller_record['sha256']}", controller_path
    )
    if module_spec is None or module_spec.loader is None:
        raise FleetError("cannot load route-only controller verifier")
    controller = importlib.util.module_from_spec(module_spec)
    try:
        module_spec.loader.exec_module(controller)
        verified_snapshot = controller.verify_snapshot(snapshot)
    except Exception as exc:
        raise FleetError(f"route-only controller snapshot is invalid: {exc}") from exc
    roles = verified_snapshot.get("by_role")
    if not isinstance(roles, dict):
        raise FleetError("route-only controller snapshot role inventory is invalid")
    snapshot_engine = roles.get("engine")
    if (not isinstance(snapshot_engine, dict) or
            engine.get("sha256") != snapshot_engine.get("sha256")):
        raise FleetError("route-only active engine differs from controller snapshot")
    for alias, role in zip(aliases, ("module-primary", "module-secondary"), strict=True):
        snapshot_role = roles.get(role.replace("-", "_"))
        if (not isinstance(snapshot_role, dict) or
                alias.get("sha256") != snapshot_role.get("sha256")):
            raise FleetError("route-only bundle module differs from controller snapshot")
    map_manifest = roles.get("map_manifest")
    if not isinstance(map_manifest, dict):
        raise FleetError("route-only controller lacks the map manifest")
    try:
        maps = tuple(controller.validate_manifest(snapshot / map_manifest["path"]))
    except Exception as exc:
        raise FleetError(f"route-only controller map manifest is invalid: {exc}") from exc
    if not set(ROUTE_ONLY_MAPS).issubset(maps):
        raise FleetError("route-only controller map manifest lacks a candidate")
    results = authority["results"]
    if not isinstance(results, list) or len(results) != len(maps):
        raise FleetError("route-only controller result inventory is incomplete")
    checked = {}
    for index, item in enumerate(results):
        if not isinstance(item, dict) or set(item) != {"map", "result", "stable_port"}:
            raise FleetError("route-only controller result record is invalid")
        map_name = maps[index]
        if (item["map"] != map_name or type(item["stable_port"]) is not int or
                item["stable_port"] != controller.DEFAULT_PORT_BASE + maps.index(map_name)):
            raise FleetError("route-only controller result map or port drift")
        result_path = _verify_file_record(item["result"], "controller result")
        if result_path != _inside(run_root / "runs" / map_name / "result.json", run_root,
                                  "controller result"):
            raise FleetError("route-only controller result path drift")
        try:
            accepted = controller.validate_resumable_pass(
                result_path, run_root=run_root, fingerprint=authority["fingerprint"],
                fingerprint_document_bytes=document_bytes,
                stable_port=item["stable_port"], snapshot=snapshot,
                runtime_preflighted=True,
            )
            result, _raw = controller._load_json_regular(result_path)
        except Exception as exc:
            raise FleetError(f"route-only controller result is invalid: {exc}") from exc
        classification = result.get("classification") if isinstance(result, dict) else None
        if (accepted is not True or not isinstance(result, dict) or
                result.get("map") != map_name or
                classification not in {"PASS", "ROUTE_ONLY"}):
            raise FleetError("route-only controller did not accept the final result")
        if classification == "ROUTE_ONLY" and map_name in ROUTE_ONLY_MAPS:
            asset_role = roles.get(f"asset:{map_name}")
            rune_role = bundle_roles.get(f"rune:{map_name}")
            bsp_role = bundle_roles.get(f"bsp:{map_name}")
            if (not isinstance(asset_role, dict) or not isinstance(rune_role, dict) or
                    not isinstance(bsp_role, dict) or
                    result.get("artifact_sha256") != rune_role.get("sha256") or
                    asset_role.get("sha256") != bsp_role.get("sha256")):
                raise FleetError("route-only controller artifact differs from active bundle")
        checked[map_name] = (classification, item)
    return _select_route_only_results(maps, checked)


def _verify_route_demo_positions(telemetry: Mapping[str, Any],
                                 decoded_server: Mapping[str, Any],
                                 players: Sequence[dict]) -> None:
    """Bind every integer SG origin to the matching serverrecord snapshot."""
    observations = telemetry.get("observations")
    wire = decoded_server.get("wire_framenums")
    tracks = decoded_server.get("tracks")
    if (not isinstance(observations, list) or not isinstance(wire, list) or
            not isinstance(tracks, dict) or
            any(type(frame) is not int for frame in wire) or
            len(set(wire)) != len(wire)):
        raise FleetError("route-only demo position authority is invalid")
    snapshots = {frame: index + 1 for index, frame in enumerate(wire)}
    clients = {player["name"]: player["client"] for player in players}
    if len(clients) != len(players):
        raise FleetError("route-only demo player authority is invalid")
    for observation in observations:
        if (not isinstance(observation, dict) or
                set(observation) != {"player", "frame", "seed", "link", "action", "origin"} or
                not isinstance(observation["player"], str) or
                type(observation["frame"]) is not int or
                not isinstance(observation["origin"], list) or
                len(observation["origin"]) != 3 or
                any(type(value) is not int for value in observation["origin"])):
            raise FleetError("route-only SG observation authority is invalid")
        client = clients.get(observation["player"])
        snapshot = snapshots.get(observation["frame"])
        samples = tracks.get(client)
        if client is None or snapshot is None or not isinstance(samples, list):
            raise FleetError("route-only SG observation lacks a serverrecord sample")
        by_snapshot = {}
        for sample in samples:
            if (not isinstance(sample, (tuple, list)) or len(sample) < 4 or
                    type(sample[0]) is not int or sample[0] in by_snapshot):
                raise FleetError("route-only serverrecord track is invalid")
            by_snapshot[sample[0]] = sample
        sample = by_snapshot.get(snapshot)
        if sample is None:
            raise FleetError("route-only SG observation lacks its serverrecord frame")
        try:
            rounded = [int(round(float(value))) for value in sample[1:4]]
        except (TypeError, ValueError, OverflowError) as exc:
            raise FleetError("route-only serverrecord coordinate is invalid") from exc
        if (any(not math.isfinite(float(value)) for value in sample[1:4]) or
                rounded != observation["origin"]):
            raise FleetError("route-only SG origin disagrees with authenticated demo frame")


def _route_runtime_files(lane: str, map_name: str, game_root: Path,
                         bundle_roles: Mapping[str, dict]) -> dict[str, dict]:
    """Record the exact private files the route-only engine will load."""
    paths = {
        "module_primary": game_root / "game.so",
        "module_secondary": game_root / "gamex86_64.so",
        "maplist": game_root / "route-only-maplist.txt",
        "bsp_file": game_root / "maps" / f"{map_name}.bsp",
        "rune_file": game_root / "maps" / f"{map_name}.rune",
        "snag_file": game_root / "maps" / f"{map_name}.snag",
    }
    records = {field: _file_record(_inside(path, game_root, f"{lane} {field}"))
               for field, path in paths.items()}
    for field, role, label in (
            ("module_primary", "module-primary", "primary module"),
            ("module_secondary", "module-secondary", "secondary module"),
            ("maplist", "route-only-maplist", "route-only maplist"),
            ("bsp_file", f"bsp:{map_name}", "BSP"),
            ("rune_file", f"rune:{map_name}", "RUNE"),
            ("snag_file", f"snag:{map_name}", "SNAG")):
        _verify_bundle_copy(records[field], bundle_roles, role, f"{lane} runtime {label}")
    return records


def _verify_route_runtime_files(lane: str, map_name: str, runtime_files: Any,
                                bundle_roles: Mapping[str, dict]) -> None:
    """Re-open the private runtime files recorded before route-only launch."""
    required = {
        "module_primary", "module_secondary", "maplist", "bsp_file", "rune_file", "snag_file",
    }
    if not isinstance(runtime_files, dict) or set(runtime_files) != required:
        raise FleetError("route-only runtime file inventory is invalid")
    module_root = _verify_file_record(runtime_files["module_primary"], "route-only module")
    game_root = module_root.parent
    expected = {
        "module_primary": game_root / "game.so",
        "module_secondary": game_root / "gamex86_64.so",
        "maplist": game_root / "route-only-maplist.txt",
        "bsp_file": game_root / "maps" / f"{map_name}.bsp",
        "rune_file": game_root / "maps" / f"{map_name}.rune",
        "snag_file": game_root / "maps" / f"{map_name}.snag",
    }
    for field, path in expected.items():
        if _verify_file_record(runtime_files[field], f"route-only runtime {field}") != path:
            raise FleetError("route-only runtime file path drift")
    for field, role, label in (
            ("module_primary", "module-primary", "primary module"),
            ("module_secondary", "module-secondary", "secondary module"),
            ("maplist", "route-only-maplist", "route-only maplist"),
            ("bsp_file", f"bsp:{map_name}", "BSP"),
            ("rune_file", f"rune:{map_name}", "RUNE"),
            ("snag_file", f"snag:{map_name}", "SNAG")):
        _verify_bundle_copy(runtime_files[field], bundle_roles, role,
                            f"{lane} runtime {label}")


def _route_telemetry(lines: Sequence[str], players: Sequence[dict],
                     measurement: Mapping[str, Any], rune_path: Path,
                     map_name: str, decoded_server: Mapping[str, Any]) -> dict:
    runeio, rolestat, stallcensus = _load_route_helpers()
    names = [str(player["name"]) for player in players]
    start = _integer(measurement.get("start_frame"), "route measurement start")
    end = _integer(measurement.get("end_frame"), "route measurement end", 1)
    if end <= start:
        raise FleetError("route measurement frame range is empty")
    try:
        rune = runeio.read_rune(rune_path)
    except Exception as exc:
        raise FleetError(f"route-only RUNE cannot be decoded: {exc}") from exc
    if getattr(getattr(rune, "header", None), "map_name", None) != map_name:
        raise FleetError("route-only RUNE map identity drift")
    try:
        telemetry = stallcensus.route_stall_evidence(
            lines, names, expected_frame_range=(start, end), rune=rune
        )
    except ValueError as exc:
        raise FleetError(f"route-only SG telemetry is invalid: {exc}") from exc
    _require_route_census_alive(names, telemetry.get("census_alive_counts"))
    _verify_route_demo_positions(telemetry, decoded_server, players)
    teams = {str(player["name"]): int(player["team"]) for player in players}
    role_counts = {"red": {"attack_near": 0, "defend_near": 0},
                   "blue": {"attack_near": 0, "defend_near": 0}}
    for line in lines:
        match = stallcensus.SG_REPORT_RE.fullmatch(line.rstrip("\n"))
        if match is None:
            continue
        name = match.group("name")
        role = int(match.group("role"))
        stable_goal = int(match.group("sgoal"))
        role_match = rolestat.ROW.search(line)
        if (role_match is None or role_match.group("name") != name or
                int(role_match.group("role")) != role or
                int(role_match.group("sgoal") or role_match.group("goal")) != stable_goal):
            raise FleetError("route-only SG role telemetry disagrees with rolestat")
        if name not in teams:
            raise FleetError("route-only SG report names a non-roster player")
        color = "red" if teams[name] == 1 else "blue"
        if role == rolestat.ROLE_ATTACK and 0 <= stable_goal < rolestat.PRESSURE_NEAR_MS:
            role_counts[color]["attack_near"] += 1
        if role == rolestat.ROLE_DEFEND and 0 <= stable_goal < rolestat.DEFENSE_NEAR_MS:
            role_counts[color]["defend_near"] += 1
    if any(count <= 0 for side in role_counts.values() for count in side.values()):
        raise FleetError("route-only evidence lacks per-team near ATTACK and DEFEND")
    if any(count <= 0 for count in telemetry["report_counts"].values()):
        raise FleetError("route-only evidence lacks a report for an admitted bot")
    return {
        "route_stall": {
            "report_count": telemetry["report_count"],
            "report_counts": telemetry["report_counts"],
            "census_count": telemetry["census_count"],
            "census_alive_counts": telemetry["census_alive_counts"],
            "players": telemetry["players"],
            "evidence": telemetry["evidence"],
        },
        "role_observations": role_counts,
    }


def _require_route_census_alive(names: Sequence[str], census_alive_counts: Any) -> None:
    """Require every admitted bot to be alive in at least one census sample."""
    expected = set(names)
    if (not expected or len(expected) != len(names) or not isinstance(census_alive_counts, dict) or
            set(census_alive_counts) != expected):
        raise FleetError("route-only census alive inventory is invalid")
    missing = sorted(
        name for name in names
        if type(census_alive_counts[name]) is not int or census_alive_counts[name] <= 0
    )
    if missing:
        raise FleetError(f"route-only census lacks an alive sample for {', '.join(missing)}")


def _route_session_database(path: Path, players: Sequence[dict], map_name: str) -> dict:
    payload, _info = _read_regular(path, maximum=2 * 1024 * 1024 * 1024)
    if not payload.startswith(b"SQLite format 3\0"):
        raise FleetError("route-only session database is not SQLite")
    uri = path.resolve().as_uri() + "?immutable=1"
    try:
        connection = sqlite3.connect(uri, uri=True)
        try:
            if connection.execute("PRAGMA integrity_check").fetchone() != ("ok",):
                raise FleetError("route-only session database integrity failed")
            matches = connection.execute(
                "SELECT match_id, mapname, red_caps, blue_caps FROM matches WHERE mapname=?",
                (map_name,),
            ).fetchall()
            total_matches = connection.execute("SELECT COUNT(*) FROM matches").fetchone()
            if len(matches) != 1:
                raise FleetError("route-only session database lacks one map match")
            match_id, recorded_map, red_caps, blue_caps = matches[0]
            rows = connection.execute(
                "SELECT client_name, is_bot, team, caps, steals, returns, kills, deaths "
                "FROM sg_session_events WHERE match_id=?", (match_id,),
            ).fetchall()
        finally:
            connection.close()
    except sqlite3.Error as exc:
        raise FleetError(f"route-only session database query failed: {exc}") from exc
    if (total_matches != (1,) or not isinstance(match_id, int) or recorded_map != map_name or
            any(type(value) is not int or value < 0 for value in (red_caps, blue_caps))):
        raise FleetError("route-only session database is not one fresh match")
    expected = {str(player["name"]): int(player["team"]) for player in players}
    totals = {
        "red": {"captures": 0, "steals": 0, "returns": 0, "kills": 0, "deaths": 0},
        "blue": {"captures": 0, "steals": 0, "returns": 0, "kills": 0, "deaths": 0},
    }
    seen = set()
    if len(rows) != 10:
        raise FleetError("route-only session database lacks exactly ten bot rows")
    for row in rows:
        if len(row) != 8:
            raise FleetError("route-only session database row shape is invalid")
        name, is_bot, team, caps, steals, returns, kills, deaths = row
        if (not isinstance(name, str) or name not in expected or name in seen or
                is_bot != 1 or team != expected[name] or
                any(type(value) is not int or value < 0
                    for value in (caps, steals, returns, kills, deaths))):
            raise FleetError("route-only session database roster or counter drift")
        seen.add(name)
        color = "red" if team == 1 else "blue"
        for field, value in (("captures", caps), ("steals", steals), ("returns", returns),
                             ("kills", kills), ("deaths", deaths)):
            totals[color][field] += value
    if seen != set(expected):
        raise FleetError("route-only session database roster is incomplete")
    if (totals["red"]["captures"] != red_caps or
            totals["blue"]["captures"] != blue_caps):
        raise FleetError("route-only session capture totals disagree with match record")
    if any(values["kills"] + values["deaths"] < 1 for values in totals.values()):
        raise FleetError("route-only session database lacks per-team combat")
    return {"match_id": match_id, "map": map_name,
            "teams": totals}


def _verify_route_receipt(receipt: dict, path: Path, evidence_root: Path,
                          owner: dict, bundle_roles: Mapping[str, dict], film: Any,
                          controller_results: Mapping[str, dict], lane: str) -> None:
    required = {
        "format", "campaign_id", "bundle_id", "lane", "map", "runner_sha256",
        "engine_generation", "client_generation", "bsp_file", "rune_file",
        "rune_sha256", "snag_file", "sg_players", "residence", "serverrecord",
        "console_segment", "runtime_files", "pov", "session_database", "runtime", "behavior",
        "controller_result", "receipt_hash",
    }
    if set(receipt) != required or receipt.get("format") != FORMAT_ROUTE_RECEIPT:
        raise FleetError("route-only receipt has unknown or missing fields")
    map_name = expected_route_only_map(lane)
    if (receipt.get("campaign_id") != owner["campaign_id"] or
            receipt.get("bundle_id") != owner["bundle_id"] or
            receipt.get("lane") != lane or receipt.get("map") != map_name or
            receipt.get("runner_sha256") != owner["runner_sha256"] or
            receipt.get("engine_generation") != owner["processes"][lane] or
            receipt.get("client_generation") != owner["clients"][lane] or
            receipt.get("controller_result") != controller_results[map_name] or
            receipt.get("receipt_hash") != receipt_hash(receipt)):
        raise FleetError(f"route-only receipt authority drift for {lane}")
    _verify_bundle_copy(receipt.get("bsp_file"), bundle_roles, f"bsp:{map_name}", "BSP")
    rune = receipt.get("rune_file")
    rune_path = _verify_bundle_copy(rune, bundle_roles, f"rune:{map_name}", "RUNE")
    _verify_bundle_copy(receipt.get("snag_file"), bundle_roles, f"snag:{map_name}", "SNAG")
    if receipt.get("rune_sha256") != rune["sha256"]:
        raise FleetError("route-only RUNE digest drift")
    if receipt.get("runtime_files") != owner["lane_runtime"][lane]:
        raise FleetError("route-only receipt runtime file authority drift")
    _verify_route_runtime_files(lane, map_name, receipt["runtime_files"], bundle_roles)
    _verify_players(receipt.get("sg_players"))
    residence = receipt.get("residence")
    if not isinstance(residence, dict) or set(residence) != {
            "start_frame", "end_frame", "measurement", "exit_frame", "exit_time_seconds"}:
        raise FleetError("route-only residence authority is invalid")
    start = _integer(residence["start_frame"], "route start frame")
    end = _integer(residence["end_frame"], "route end frame", 1)
    measurement = residence["measurement"]
    if (end <= start or end - start < 6000 or
            _integer(residence["exit_frame"], "route exit frame", 1) != end or
            not isinstance(measurement, dict) or
            set(measurement) != {"start_frame", "end_frame"} or
            _integer(measurement["start_frame"], "route measurement start") < start or
            _integer(measurement["end_frame"], "route measurement end", 1) > end or
            measurement["end_frame"] <= measurement["start_frame"] or
            type(residence["exit_time_seconds"]) not in (int, float) or
            not math.isfinite(residence["exit_time_seconds"]) or
            residence["exit_time_seconds"] < 600.0):
        raise FleetError("route-only residence does not prove the timed match")
    serverrecord = receipt.get("serverrecord")
    if not isinstance(serverrecord, dict) or set(serverrecord) != {
            "demo_path", "demo_sha256", "demo_size", "demo_frame_range"}:
        raise FleetError("route-only serverrecord authority is invalid")
    lane_root = path.parent
    demo = _inside(Path(str(serverrecord["demo_path"])), evidence_root, "serverrecord")
    if demo != lane_root / "serverrecord.dm2":
        raise FleetError("route-only serverrecord escapes its lane")
    demo_record = _file_record(demo)
    if (demo_record["sha256"] != serverrecord["demo_sha256"] or
            demo_record["size"] != serverrecord["demo_size"]):
        raise FleetError("route-only serverrecord identity drift")
    frame_range = serverrecord.get("demo_frame_range")
    if (not isinstance(frame_range, dict) or set(frame_range) != {"start", "end_exclusive"} or
            _integer(frame_range["start"], "route demo start", 1) < 1 or
            _integer(frame_range["end_exclusive"], "route demo end", 2) <= frame_range["start"]):
        raise FleetError("route-only serverrecord frame range is invalid")
    try:
        decoded_server = film.walk_demo(demo, strict=True)
    except Exception as exc:
        raise FleetError(f"route-only serverrecord cannot be decoded: {exc}") from exc
    wire = decoded_server.get("wire_framenums")
    expected_wire = list(range(start + 1, end + 1))
    if (decoded_server.get("map") != map_name or decoded_server.get("svrecord") is not True or
            decoded_server.get("parse_complete") is not True or
            decoded_server.get("terminated") is not True or not isinstance(wire, list) or
            wire != expected_wire or frame_range != {"start": 1,
                                                     "end_exclusive": len(wire) + 1}):
        raise FleetError("route-only serverrecord map or frame authority drift")
    demo_players = {}
    for client, epochs in decoded_server.get("skin_epochs", {}).items():
        names = {value.split("\\", 1)[0] for _frame, value in epochs}
        if len(names) == 1:
            demo_players[next(iter(names))] = client + 1
    if demo_players != {player["name"]: player["client"] for player in receipt["sg_players"]}:
        raise FleetError("route-only serverrecord roster differs from receipt")
    segment = receipt.get("console_segment")
    if not isinstance(segment, dict) or set(segment) != {"path", "sha256", "size"}:
        raise FleetError("route-only console segment authority is invalid")
    segment_path = _inside(path.parent / "segments" / str(segment["path"]), evidence_root,
                           "route-only console segment")
    segment_record = _file_record(segment_path)
    if segment_record["sha256"] != segment["sha256"] or segment_record["size"] != segment["size"]:
        raise FleetError("route-only console segment identity drift")
    try:
        lines = segment_path.read_text(encoding="ascii").splitlines()
    except UnicodeDecodeError as exc:
        raise FleetError("route-only console segment is not ASCII") from exc
    runtime = receipt.get("runtime")
    if runtime != {"identity_committed": True, "route_contract": "local-only",
                   "rune_ready": True}:
        raise FleetError("route-only runtime readiness is invalid")
    roster = {}
    roster_rows = 0
    for line in lines:
        parts = line.split()
        if (len(parts) >= 3 and parts[0].isdigit() and parts[1].startswith("[SG]") and
                parts[2] in {"red", "blue"}):
            if parts[1] in roster:
                raise FleetError("route-only console roster repeats a bot")
            roster[parts[1]] = (int(parts[0]), 1 if parts[2] == "red" else 2)
            roster_rows += 1
    if roster != {player["name"]: (player["slot"], player["team"])
                  for player in receipt["sg_players"]} or roster_rows != 10:
        raise FleetError("route-only console roster differs from receipt")
    if lines.count("Timelimit hit.") != 1:
        raise FleetError("route-only console lacks one native timelimit")
    timelimit = lines.index("Timelimit hit.")
    exits = [line for line in lines if line.startswith("EXITLEVEL frame=")]
    if len(exits) != 1:
        raise FleetError("route-only console lacks one native level exit")
    exit_fields = exits[0].split()
    try:
        if (len(exit_fields) != 4 or not exit_fields[1].startswith("frame=") or
                not exit_fields[2].startswith("time=") or
                not exit_fields[3].startswith("changemap=")):
            raise ValueError("not an exact native exit")
        exit_frame = int(exit_fields[1].removeprefix("frame="))
        exit_time = float(exit_fields[2].removeprefix("time="))
    except (IndexError, ValueError) as exc:
        raise FleetError("route-only console exit shape is invalid") from exc
    if (timelimit < 0 or lines.index(exits[0]) <= timelimit or exit_frame != end or
            not math.isfinite(exit_time) or exit_time < 600.0 or
            exit_time != residence["exit_time_seconds"]):
        raise FleetError("route-only console does not prove timelimit intermission")
    if (sum(line.startswith(f"slipgate: rune identity committed map={map_name} ")
            for line in lines) != 1 or
            lines.count("slipgate: route contract local-only") != 1 or
            sum(line.startswith(f"slipgate: rune ready {map_name},") for line in lines) != 1):
        raise FleetError("route-only console lacks accepted RUNE readiness")
    pov = receipt.get("pov")
    if (not isinstance(pov, dict) or set(pov) != {
            "demo_path", "demo_sha256", "demo_size", "spectator", "target",
            "start_confirmed", "stop_confirmed"} or
            pov.get("start_confirmed") is not True or pov.get("stop_confirmed") is not True or
            not isinstance(pov.get("spectator"), str) or not pov["spectator"] or
            not isinstance(pov.get("target"), str) or not pov["target"].startswith("[SG]")):
        raise FleetError("route-only POV lifecycle is incomplete")
    pov_path = _inside(Path(str(pov["demo_path"])), evidence_root, "route-only POV demo")
    if pov_path != lane_root / "pov.dm2":
        raise FleetError("route-only POV escapes its lane")
    pov_record = _file_record(pov_path)
    if pov_record["sha256"] != pov["demo_sha256"] or pov_record["size"] != pov["demo_size"]:
        raise FleetError("route-only POV identity drift")
    try:
        decoded_pov = film.walk_demo(pov_path, strict=True)
    except Exception as exc:
        raise FleetError(f"route-only POV cannot be decoded: {exc}") from exc
    if (decoded_pov.get("map") != map_name or decoded_pov.get("svrecord") is not False or
            decoded_pov.get("parse_complete") is not True or
            decoded_pov.get("terminated") is not True or
            type(decoded_pov.get("frames")) is not int or decoded_pov["frames"] < 1):
        raise FleetError("route-only POV map or lifecycle authority drift")
    database = _verify_file_record(receipt.get("session_database"), "route-only session database",
                                   evidence_root=evidence_root)
    if database != lane_root / "session.db":
        raise FleetError("route-only session database escapes its lane")
    computed = _route_telemetry(
        lines, receipt["sg_players"], measurement, rune_path, map_name, decoded_server
    )
    computed["session"] = _route_session_database(database, receipt["sg_players"], map_name)
    if receipt.get("behavior") != computed:
        raise FleetError("route-only behavior evidence drift")


def verify_stopped_route_only_evidence(
        state_root: os.PathLike[str] | str,
        evidence_root: os.PathLike[str] | str,
        ) -> tuple[tuple[Path, dict], ...]:
    """Verify the controller-selected ordinary-match exception proof."""
    _reject_development_controller_environment()
    state = _safe_root(state_root, "route-only state root")
    evidence = _safe_root(evidence_root, "route-only evidence root")
    _require_frozen_tree(state, "route-only state root")
    _require_frozen_tree(evidence, "route-only evidence root")
    owner, _owner_payload = _read_json(state / "route-only-owner.json")
    required = {
        "format", "state", "campaign_id", "bundle_id", "runner_sha256",
        "release_monotonic_ns", "graceful_quit", "processes", "clients", "inputs",
        "controller_authority", "matches", "lane_runtime", "ledger_entries", "ledger_tail_hash",
        "lock_path", "selected_lanes", "selected_count", "no_op",
    }
    runner_sha = _hash(Path(__file__).read_bytes())
    if (set(owner) != required or owner.get("format") != FORMAT_ROUTE_OWNER or
            owner.get("state") != "SAFE_STOPPED" or not isinstance(owner.get("campaign_id"), str) or
            not owner["campaign_id"] or not _valid_hash(owner.get("bundle_id")) or
            owner.get("runner_sha256") != runner_sha or
            type(owner.get("release_monotonic_ns")) is not int or
            owner["release_monotonic_ns"] < 0 or type(owner.get("ledger_entries")) is not int or
            owner["ledger_entries"] < 0 or type(owner.get("no_op")) is not bool or
            type(owner.get("graceful_quit")) is not bool or
            not _valid_hash(owner.get("ledger_tail_hash"))):
        raise FleetError("route-only stopped owner authority is invalid")
    lock_path = _inside(Path(str(owner.get("lock_path"))), state, "route-only lock")
    _verify_unheld_lock(lock_path)
    inputs = owner.get("inputs")
    if not isinstance(inputs, dict) or set(inputs) != {
            "engine", "client", "route_config", "film", "runtime", "module_aliases",
            "installed_bundle", "bundle_verifier", "runeio", "rolestat", "stallcensus"}:
        raise FleetError("route-only input inventory is invalid")
    active_bundle, bundle_verifier = _verify_installed_bundle(inputs["installed_bundle"])
    if (active_bundle.get("bundle_id") != owner["bundle_id"] or
            inputs["bundle_verifier"] != bundle_verifier):
        raise FleetError("route-only installed bundle identity drift")
    roles = _bundle_role_records(active_bundle)
    _verify_file_record(inputs["engine"], "route-only engine")
    _verify_file_record(inputs["client"], "route-only client")
    _verify_bundle_copy(inputs["route_config"], roles, "route-only-config", "route-only config")
    _verify_file_record(inputs["film"], "route-only film decoder")
    if inputs["runtime"] != _file_record(Path(__file__).with_name("fleet_runner_live.py")):
        raise FleetError("route-only runtime differs from verifier companion")
    helpers = _route_helper_records()
    if (inputs["runeio"] != helpers["runeio"] or
            inputs["rolestat"] != helpers["rolestat"] or
            inputs["stallcensus"] != helpers["stallcensus"]):
        raise FleetError("route-only telemetry helper differs from verifier authority")
    aliases = inputs["module_aliases"]
    if not isinstance(aliases, list) or len(aliases) != 2:
        raise FleetError("route-only module aliases are invalid")
    for alias, role in zip(aliases, ("module-primary", "module-secondary"), strict=True):
        _verify_bundle_copy(alias, roles, role, "route-only module alias")
    if aliases[0]["sha256"] != aliases[1]["sha256"]:
        raise FleetError("route-only module aliases differ")
    controller_results = _validate_route_controller_authority(
        owner["controller_authority"], roles, inputs["engine"], aliases
    )
    lane_order = _selected_route_only_lanes(controller_results)
    if (owner.get("selected_lanes") != list(lane_order) or
            owner.get("selected_count") != len(lane_order) or
            owner.get("no_op") != (not lane_order) or
            owner.get("ledger_entries") != len(lane_order)):
        raise FleetError("route-only owner selection differs from controller authority")
    if lane_order:
        if owner["release_monotonic_ns"] <= 0 or owner["graceful_quit"] is not True:
            raise FleetError("route-only selected run did not stop cleanly")
    elif owner["release_monotonic_ns"] != 0 or owner["graceful_quit"] is not False:
        raise FleetError("route-only no-op owner launched a process")
    film = None
    if lane_order:
        try:
            film = _load_film_module(inputs["film"])
        except Exception as exc:
            raise FleetError(f"route-only film decoder cannot load: {exc}") from exc
    matches = owner.get("matches")
    if not isinstance(matches, dict) or matches != {
            lane: expected_route_only_map(lane) for lane in lane_order}:
        raise FleetError("route-only selected map inventory drift")
    lane_runtime = owner.get("lane_runtime")
    if not isinstance(lane_runtime, dict) or set(lane_runtime) != set(lane_order):
        raise FleetError("route-only runtime lane inventory is invalid")
    for lane in lane_order:
        _verify_route_runtime_files(lane, expected_route_only_map(lane),
                                    lane_runtime[lane], roles)
    processes, clients = owner.get("processes"), owner.get("clients")
    if not isinstance(processes, dict) or set(processes) != set(lane_order):
        raise FleetError("route-only engine inventory is incomplete")
    if not isinstance(clients, dict) or set(clients) != set(lane_order):
        raise FleetError("route-only client inventory is incomplete")
    for lane in lane_order:
        _verify_process(processes[lane], lane, inputs["engine"])
        _verify_process(clients[lane], lane, inputs["client"])
    ledger_payload, _ledger_info = _read_regular(evidence / "route-only-ledger.jsonl")
    lines = ledger_payload.splitlines(keepends=True)
    if len(lines) != len(lane_order) or any(not line.endswith(b"\n") for line in lines):
        raise FleetError("route-only ledger does not match the selected lanes")
    receipts, previous = [], ZERO_HASH
    for index, line in enumerate(lines):
        try:
            entry = json.loads(line.decode("ascii"), object_pairs_hook=_no_duplicate_object)
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise FleetError("route-only ledger JSON is invalid") from exc
        lane = lane_order[index]
        relative = f"receipts/{lane}/receipt.json"
        if (not isinstance(entry, dict) or line != _canonical(entry) or
                entry.get("format") != FORMAT_ROUTE_LEDGER or entry.get("index") != index or
                entry.get("previous_hash") != previous or entry.get("receipt_path") != relative or
                not _valid_hash(entry.get("receipt_hash")) or
                entry.get("entry_hash") != ledger_entry_hash(entry)):
            raise FleetError(f"route-only ledger chain drift at entry {index}")
        receipt_path = _inside(Path(relative), evidence, "route-only receipt")
        receipt, _receipt_payload = _read_json(receipt_path)
        if receipt.get("receipt_hash") != entry["receipt_hash"]:
            raise FleetError("route-only ledger receipt digest drift")
        _verify_route_receipt(receipt, receipt_path, evidence, owner, roles, film,
                              controller_results, lane)
        receipts.append((receipt_path, receipt))
        previous = entry["entry_hash"]
    if previous != owner["ledger_tail_hash"]:
        raise FleetError("route-only ledger tail differs from stopped owner")
    return tuple(receipts)


@dataclass
class CapturedChild:
    lane: str
    process: subprocess.Popen[bytes]
    pidfd: int
    identity: dict | None
    release_write: int


class FleetCycle:
    """Reduce native map commits without allowing an engine generation swap."""

    def __init__(self, lane: str, process: Mapping[str, Any]):
        if lane not in LANES or not isinstance(process, Mapping):
            raise FleetError("invalid fleet cycle authority")
        self.lane = lane
        self.process = dict(process)
        self.sequence = -1
        self.pending_exit = False
        self._completed = []

    def _same_generation(self, process: Mapping[str, Any]) -> None:
        keys = ("pid", "boot_id", "start_ticks", "command_sha256")
        if any(process.get(key) != self.process.get(key) for key in keys):
            raise FleetError(f"{self.lane} engine generation changed inside cycle")

    def map_committed(self, map_name: str, process: Mapping[str, Any]) -> None:
        self._same_generation(process)
        next_sequence = self.sequence + 1
        if map_name != expected_map(self.lane, next_sequence):
            raise FleetError(f"{self.lane} native map schedule drift")
        if self.sequence >= 0:
            if not self.pending_exit:
                raise FleetError(f"{self.lane} map changed without an authenticated exit")
            if self.sequence <= 20:
                self._completed.append(self.sequence)
        self.sequence = next_sequence
        self.pending_exit = False

    def level_exited(self, process: Mapping[str, Any]) -> None:
        self._same_generation(process)
        if self.sequence < 0 or self.pending_exit or self.sequence > 20:
            raise FleetError(f"{self.lane} has an invalid native level exit")
        self.pending_exit = True

    @property
    def completed_sequences(self) -> tuple[int, ...]:
        return tuple(self._completed)

    @property
    def complete(self) -> bool:
        return self.completed_sequences == tuple(range(21)) and self.sequence == 21


class RouteOnlyCycle:
    """Reduce one timed map and its authenticated native exit."""

    def __init__(self, lane: str, process: Mapping[str, Any]):
        if lane not in ROUTE_ONLY_LANES or not isinstance(process, Mapping):
            raise FleetError("invalid route-only cycle authority")
        self.lane = lane
        self.process = dict(process)
        self.sequence = -1
        self.pending_exit = False
        self.complete = False

    def _same_generation(self, process: Mapping[str, Any]) -> None:
        keys = ("pid", "boot_id", "start_ticks", "command_sha256")
        if any(process.get(key) != self.process.get(key) for key in keys):
            raise FleetError(f"{self.lane} engine generation changed inside route-only match")

    def map_committed(self, map_name: str, process: Mapping[str, Any]) -> None:
        self._same_generation(process)
        if self.sequence < 0:
            if map_name != expected_route_only_map(self.lane):
                raise FleetError(f"{self.lane} did not commit its route-only map")
            self.sequence = 0
            return
        if self.complete or not self.pending_exit or not map_name:
            raise FleetError(f"{self.lane} map changed without a timed route-only exit")
        self.sequence = 1
        self.complete = True

    def level_exited(self, process: Mapping[str, Any]) -> None:
        self._same_generation(process)
        if self.sequence != 0 or self.pending_exit:
            raise FleetError(f"{self.lane} route-only exit is invalid")
        self.pending_exit = True


def _arm_parent_death(parent: int) -> None:
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(1, signal.SIGKILL, 0, 0, 0) != 0 or os.getppid() != parent:
        os._exit(125)


def _proc_start_ticks(pid: int) -> int:
    line = Path(f"/proc/{pid}/stat").read_text()
    closing = line.rfind(")")
    fields = line[closing + 2:].split() if closing >= 0 else []
    if len(fields) <= 19:
        raise FleetError("malformed child process stat")
    return int(fields[19])


def _capture_child(child: CapturedChild, engine: dict, argv: list[str]) -> None:
    process = child.process
    executable = Path(f"/proc/{process.pid}/exe").resolve(strict=True)
    if executable != Path(engine["path"]).resolve(strict=True):
        raise FleetError(f"{child.lane} executed an unpinned engine image")
    start_ticks = _proc_start_ticks(process.pid)
    if child.identity is not None and start_ticks != child.identity["start_ticks"]:
        raise FleetError(f"{child.lane} process generation changed across release")
    child.identity = {
        "pid": process.pid,
        "boot_id": Path("/proc/sys/kernel/random/boot_id").read_text().strip(),
        "start_ticks": start_ticks,
        "executable": engine,
        "argv": argv,
        "command_sha256": _hash_argv(argv),
        "pidfd_captured": True,
    }


def _spawn_held_engine(lane: str, engine: dict, argv: list[str], cwd: Path,
                       stdout) -> CapturedChild:
    release_read, release_write = os.pipe2(os.O_CLOEXEC)
    parent = os.getpid()

    def before_exec() -> None:
        _arm_parent_death(parent)

    barrier = (
        "fd=$1; shift; IFS= read -r release <&$fd; "
        "test \"$release\" = R || exit 125; exec \"$@\""
    )
    wrapper = ["/bin/sh", "-c", barrier, "fleet-barrier", str(release_read), *argv]
    process = subprocess.Popen(
        wrapper, cwd=cwd, stdin=subprocess.PIPE, stdout=stdout,
        stderr=subprocess.STDOUT, preexec_fn=before_exec, pass_fds=(release_read,),
    )
    os.close(release_read)
    try:
        pidfd = os.pidfd_open(process.pid)
        partial = {
            "pid": process.pid,
            "boot_id": Path("/proc/sys/kernel/random/boot_id").read_text().strip(),
            "start_ticks": _proc_start_ticks(process.pid),
        }
        return CapturedChild(lane, process, pidfd, partial, release_write)
    except BaseException:
        os.close(release_write)
        process.kill()
        process.wait()
        raise


def _validate_run_spec(path: Path) -> tuple[dict, dict]:
    spec, _payload = _read_json(path)
    if spec.get("format") != FORMAT_SPEC:
        raise FleetError("invalid fleet run specification format")
    if any(token in str(path).lower() for token in ("wavewatch", "waveloop", "iterate2")):
        raise FleetError("development launcher cannot be a production run input")
    engine = spec.get("engine")
    _verify_file_record(engine, "engine")
    lanes = spec.get("lanes")
    if not isinstance(lanes, list) or len(lanes) != 10:
        raise FleetError("run specification needs ten lanes")
    by_lane = {}
    for lane_spec in lanes:
        if not isinstance(lane_spec, dict) or lane_spec.get("lane") not in LANES:
            raise FleetError("invalid lane run specification")
        lane = lane_spec["lane"]
        if lane in by_lane or lane_spec.get("offset") != OFFSETS[LANES.index(lane)]:
            raise FleetError("duplicate or misrotated lane specification")
        root = _safe_root(lane_spec.get("root"), f"{lane} root")
        maplist = _verify_file_record(lane_spec.get("maplist"), f"{lane} maplist")
        maplist_payload, _info = _read_regular(maplist, maximum=1024 * 1024)
        maps = tuple(line for line in maplist_payload.decode("ascii").splitlines() if line)
        expected = tuple(CANONICAL_TOPMAPS[(LANES.index(lane) + step) % 20]
                         for step in range(20))
        if maps != expected:
            raise FleetError(f"{lane} maplist is not its exact canonical rotation")
        argv = lane_spec.get("argv")
        if (not isinstance(argv, list) or not argv or argv[0] != engine["path"] or
                any(not isinstance(item, str) or not item or "\0" in item for item in argv)):
            raise FleetError(f"invalid exact engine argv for {lane}")
        if any(any(token in item.lower() for token in
                   ("wavewatch", "waveloop", "iterate2")) for item in argv):
            raise FleetError("development launcher cannot enter production argv")
        by_lane[lane] = {"root": root, "argv": argv}
    if set(by_lane) != set(LANES):
        raise FleetError("run specification lane inventory is incomplete")
    return spec, by_lane


def _validate_route_only_run_spec(path: Path) -> tuple[dict, dict]:
    """Validate a proposed ordered subset of the fixed route-only candidates."""
    spec, _payload = _read_json(path)
    required = {
        "format", "campaign_id", "engine", "client", "route_config", "film",
        "runtime", "module_aliases", "spectator", "target", "timeout_seconds",
        "installed_bundle", "controller_authority", "lanes",
    }
    if set(spec) != required or spec.get("format") != FORMAT_ROUTE_SPEC:
        raise FleetError("invalid route-only run specification")
    if any(token in str(path).lower() for token in ("wavewatch", "waveloop", "iterate2")):
        raise FleetError("development launcher cannot be a route-only run input")
    _verify_file_record(spec["engine"], "route-only engine")
    if (not isinstance(spec["campaign_id"], str) or not spec["campaign_id"] or
            not isinstance(spec["spectator"], str) or not spec["spectator"] or
            not isinstance(spec["target"], str) or not spec["target"].startswith("[SG]") or
            type(spec["timeout_seconds"]) is not int or spec["timeout_seconds"] < 60):
        raise FleetError("route-only run identity or timeout is invalid")
    lanes = spec["lanes"]
    if not isinstance(lanes, list) or len(lanes) > len(ROUTE_ONLY_LANES):
        raise FleetError("route-only run has an invalid lane selection")
    expected_keys = {
        "lane", "map", "root", "argv", "client_argv", "serverrecord_dir",
        "pov_demo", "game_root", "statsdb_backup", "artifacts",
    }
    by_lane = {}
    previous_lane_index = -1
    for lane_spec in lanes:
        if not isinstance(lane_spec, dict) or set(lane_spec) != expected_keys:
            raise FleetError("route-only lane specification is incomplete")
        lane = lane_spec["lane"]
        if lane not in ROUTE_ONLY_LANES or lane_spec["map"] != expected_route_only_map(lane):
            raise FleetError("route-only lane mapping is not the fixed authority")
        lane_index = ROUTE_ONLY_LANES.index(lane)
        if lane_index <= previous_lane_index:
            raise FleetError("route-only lane selection is not an ordered subset")
        previous_lane_index = lane_index
        root = _safe_root(lane_spec["root"], f"{lane} route-only root")
        argv = lane_spec["argv"]
        if (not isinstance(argv, list) or not argv or argv[0] != spec["engine"]["path"] or
                any(not isinstance(item, str) or not item or "\0" in item for item in argv) or
                any(any(token in item.lower() for token in ("wavewatch", "waveloop", "iterate2"))
                    for item in argv)):
            raise FleetError(f"invalid exact route-only engine argv for {lane}")
        client_argv = lane_spec["client_argv"]
        if (not isinstance(client_argv, list) or not client_argv or
                client_argv[0] != spec["client"]["path"] or
                any(not isinstance(item, str) or not item or "\0" in item
                    for item in client_argv)):
            raise FleetError(f"invalid route-only client argv for {lane}")
        serverrecord_dir = _inside(
            Path(lane_spec["serverrecord_dir"]), root, f"{lane} serverrecord directory"
        )
        pov_demo = _inside(Path(lane_spec["pov_demo"]), root, f"{lane} POV demo")
        game_root = _inside(Path(lane_spec["game_root"]), root, f"{lane} game root")
        if not game_root.is_dir():
            raise FleetError(f"{lane} route-only game root is not a directory")
        expected_game = game_root.relative_to(root).as_posix()
        game_pairs = [argv[index + 2] for index, token in enumerate(argv[:-2])
                      if token == "+set" and argv[index + 1] == "game"]
        if game_pairs != [expected_game]:
            raise FleetError(f"{lane} route-only argv does not bind its private game root")
        backup = _inside(Path(lane_spec["statsdb_backup"]), root,
                         f"{lane} session backup")
        if (backup.parent != game_root or backup.name != "route-only-session.db" or
                backup.exists()):
            raise FleetError(f"invalid fresh route-only backup path for {lane}")
        artifacts = lane_spec["artifacts"]
        if not isinstance(artifacts, dict) or set(artifacts) != {
                "bsp_file", "rune_file", "snag_file"}:
            raise FleetError(f"route-only artifact inventory is invalid for {lane}")
        for label, record in artifacts.items():
            _verify_file_record(record, f"{lane} {label}")
        for prior_lane, prior in by_lane.items():
            if _trees_overlap(game_root, prior["game_root"]):
                raise FleetError(
                    f"{lane} route-only game root shares writable tree with {prior_lane}"
                )
            if _trees_overlap(root, prior["root"]):
                raise FleetError(
                    f"{lane} route-only root shares writable tree with {prior_lane}"
                )
        by_lane[lane] = {
            **lane_spec, "root": root, "serverrecord_dir": serverrecord_dir,
            "pov_demo": pov_demo, "game_root": game_root, "statsdb_backup": backup,
        }
    return spec, by_lane


def launch_persistent_engines(spec_path: os.PathLike[str] | str) -> tuple[dict, ...]:
    """Launch ten pinned engines behind one coordinated release barrier.

    This is the engine-ownership boundary used by the full residence reducer.
    Bundle installation is deliberately outside this operation.
    """
    _reject_development_controller_environment()
    spec, lanes = _validate_run_spec(Path(spec_path))
    engine = spec["engine"]
    children = []
    try:
        for lane in LANES:
            output = open(lanes[lane]["root"] / "fleet-engine.log", "xb", buffering=0)
            child = _spawn_held_engine(
                lane, engine, lanes[lane]["argv"], lanes[lane]["root"], output
            )
            output.close()
            children.append(child)
        release_ns = time.monotonic_ns()
        for child in children:
            os.write(child.release_write, b"R\n")
            os.close(child.release_write)
            child.release_write = -1
        deadline = time.monotonic() + 5.0
        for child in children:
            while time.monotonic() < deadline:
                if child.process.poll() is not None:
                    raise FleetError(f"{child.lane} engine exited during release")
                try:
                    if Path(f"/proc/{child.process.pid}/exe").resolve() == \
                            Path(engine["path"]).resolve():
                        break
                except OSError:
                    pass
                time.sleep(0.005)
            else:
                raise FleetError(f"{child.lane} did not exec its pinned engine")
            _capture_child(child, engine, lanes[child.lane]["argv"])
        return tuple({**child.identity, "lane": child.lane,
                      "release_monotonic_ns": release_ns}
                     for child in children)
    except BaseException:
        for child in children:
            if child.release_write >= 0:
                os.close(child.release_write)
            try:
                signal.pidfd_send_signal(child.pidfd, signal.SIGKILL)
            except (ProcessLookupError, OSError):
                pass
            child.process.wait()
            os.close(child.pidfd)
        raise
    finally:
        # The complete residence event loop retains these descriptors.  This
        # bounded API is also used by the spawn integration test and therefore
        # must not leak descriptor ownership to its caller.
        for child in children:
            if child.process.poll() is None:
                try:
                    signal.pidfd_send_signal(child.pidfd, signal.SIGTERM)
                    child.process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    signal.pidfd_send_signal(child.pidfd, signal.SIGKILL)
                    child.process.wait(timeout=2)
            if child.process.stdin is not None:
                child.process.stdin.close()
            try:
                os.close(child.pidfd)
            except OSError:
                pass


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    verify = subparsers.add_parser("verify", help="verify stopped fleet evidence")
    verify.add_argument("--state-root", type=Path, required=True)
    verify.add_argument("--evidence-root", type=Path, required=True)
    launch = subparsers.add_parser("launch-check", help="prove coordinated pinned launch")
    launch.add_argument("--spec", type=Path, required=True)
    run = subparsers.add_parser("run", help="collect one persistent native cycle")
    run.add_argument("--spec", type=Path, required=True)
    run.add_argument("--state-root", type=Path, required=True)
    run.add_argument("--evidence-root", type=Path, required=True)
    route_verify = subparsers.add_parser(
        "route-only-verify", help="verify stopped route-only ordinary-match evidence"
    )
    route_verify.add_argument("--state-root", type=Path, required=True)
    route_verify.add_argument("--evidence-root", type=Path, required=True)
    route_run = subparsers.add_parser(
        "route-only-run", help="collect the controller-selected route-only match proof"
    )
    route_run.add_argument("--spec", type=Path, required=True)
    route_run.add_argument("--state-root", type=Path, required=True)
    route_run.add_argument("--evidence-root", type=Path, required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    try:
        arguments = _parser().parse_args(argv)
        _reject_development_controller_environment()
        if arguments.command == "verify":
            receipts = verify_stopped_residence_evidence(
                arguments.state_root, arguments.evidence_root
            )
            print(f"fleet evidence verified: {len(receipts)} residences")
        elif arguments.command == "route-only-verify":
            receipts = verify_stopped_route_only_evidence(
                arguments.state_root, arguments.evidence_root
            )
            print(f"route-only evidence verified: {len(receipts)} matches")
        elif arguments.command == "launch-check":
            identities = launch_persistent_engines(arguments.spec)
            print(_canonical({"engines": identities}).decode("ascii"), end="")
        elif arguments.command == "run":
            import fleet_runner_live
            fleet_runner_live.run_fleet(
                sys.modules[__name__], arguments.spec,
                arguments.state_root, arguments.evidence_root,
            )
        elif arguments.command == "route-only-run":
            import fleet_runner_live
            fleet_runner_live.run_route_only(
                sys.modules[__name__], arguments.spec,
                arguments.state_root, arguments.evidence_root,
            )
        else:
            raise FleetError("unrecognized fleet-runner command")
        return 0
    except (ValueError, OSError) as exc:
        print(f"fleet-runner: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
