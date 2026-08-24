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
import stat
import subprocess
import sys
import time
from typing import Any, Mapping, Sequence


LANES = tuple(f"s{index:02d}" for index in range(1, 11))
OFFSETS = tuple(range(10))
TOPMAPS_PATH = Path(__file__).with_name("topmaps.txt")
MAX_JSON_BYTES = 64 * 1024 * 1024
ZERO_HASH = "0" * 64
FORMAT_OWNER = "lmctf-fleet-owner-v1"
FORMAT_RECEIPT = "lmctf-fleet-residence-v1"
FORMAT_LEDGER = "lmctf-fleet-ledger-entry-v1"
FORMAT_SPEC = "lmctf-fleet-run-spec-v1"
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
        elif arguments.command == "launch-check":
            identities = launch_persistent_engines(arguments.spec)
            print(_canonical({"engines": identities}).decode("ascii"), end="")
        else:
            import fleet_runner_live
            fleet_runner_live.run_fleet(
                sys.modules[__name__], arguments.spec,
                arguments.state_root, arguments.evidence_root,
            )
        return 0
    except (ValueError, OSError) as exc:
        print(f"fleet-runner: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
