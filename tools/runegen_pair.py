#!/usr/bin/env python3
"""Stage, attest, install, and recover one RUNE artifact."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
from dataclasses import dataclass
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import tempfile
import time
import uuid
from typing import Mapping, Sequence

try:
    import runeio
except ModuleNotFoundError:
    from tools import runeio


MANIFEST_FORMAT = "lmctf-runegen-rune-v1"
PROVENANCE_FORMAT = "lmctf-runegen-provenance-v2"
TRANSACTION_FORMAT = "lmctf-runegen-transaction-v2"
BACKUP_FORMAT = "lmctf-runegen-backup-v2"
MAP_RE = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_-]{0,62}\Z")
SHA_RE = re.compile(r"[0-9a-f]{64}\Z")
MAX_FILE_BYTES = 512 * 1024 * 1024
RUNE_READY_RE = re.compile(
    r"slipgate: rune ready ([A-Za-z0-9_][A-Za-z0-9_-]{0,62}), "
    r"([0-9]+) seeds, ([0-9]+) links, ([0-9]+) mechanism nodes, "
    r"([0-9]+) plans, gravity -?[0-9]+, all fields up\Z"
)
FAILURE_RE = re.compile(
    r"rune: (?:rejected .+|FAILED(?::| |$).*|generation refused .+|"
    r"revalidation failed .+|install failed .+|"
    r"cleanup restored pending door scope;.*)\Z"
)


class PairError(ValueError):
    pass


class InjectedCrash(BaseException):
    pass


@dataclass(frozen=True)
class FileRecord:
    path: Path
    sha256: str
    size: int
    mode: int


@dataclass(frozen=True)
class AcceptedRune:
    map_name: str
    bsp: FileRecord
    rune: FileRecord
    provenance: FileRecord
    manifest_path: Path
    fingerprint: str
    counts: Mapping[str, int]
    frozen_inputs: tuple[FileRecord, ...]


@dataclass(frozen=True)
class BackupRecord:
    directory: Path
    manifest: Path


def canonical_json(value: object) -> bytes:
    return (
        json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
        + "\n"
    ).encode("ascii")


def _map_name(value: str) -> str:
    if MAP_RE.fullmatch(value) is None:
        raise PairError("invalid map name")
    return value


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _read_regular(path: Path, label: str, *, unaliased: bool = True) -> bytes:
    path = Path(path)
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as error:
        raise PairError(f"cannot open {label} {path}: {error}") from error
    try:
        before = os.fstat(descriptor)
        if (
            not stat.S_ISREG(before.st_mode)
            or before.st_size <= 0
            or before.st_size > MAX_FILE_BYTES
            or (unaliased and before.st_nlink != 1)
        ):
            alias = " unaliased" if unaliased else ""
            raise PairError(f"{label} is not a bounded{alias} regular file")
        chunks = bytearray()
        while len(chunks) < before.st_size:
            block = os.read(
                descriptor, min(1024 * 1024, before.st_size - len(chunks))
            )
            if not block:
                raise PairError(f"{label} truncated during read")
            chunks.extend(block)
        after = os.fstat(descriptor)
        named = path.stat(follow_symlinks=False)

        def identity(item: os.stat_result) -> tuple[int, int, int, int, int]:
            return (
                item.st_dev,
                item.st_ino,
                item.st_size,
                item.st_mtime_ns,
                item.st_ctime_ns,
            )

        if (
            identity(before) != identity(after)
            or identity(after) != identity(named)
            or (unaliased and (after.st_nlink != 1 or named.st_nlink != 1))
        ):
            raise PairError(f"{label} changed while reading")
        return bytes(chunks)
    finally:
        os.close(descriptor)


def _record(path: Path, label: str) -> FileRecord:
    payload = _read_regular(path, label)
    info = Path(path).stat(follow_symlinks=False)
    return FileRecord(
        Path(path).resolve(),
        _sha256(payload),
        len(payload),
        stat.S_IMODE(info.st_mode),
    )


def _rune_record(path: Path, map_name: str) -> tuple[FileRecord, runeio.RuneArtifact]:
    payload = _read_regular(path, "RUNE")
    info = Path(path).stat(follow_symlinks=False)
    record = FileRecord(
        Path(path).resolve(),
        _sha256(payload),
        len(payload),
        stat.S_IMODE(info.st_mode),
    )
    try:
        decoded = runeio.decode_rune(payload)
    except runeio.RuneWireError as error:
        raise PairError(f"RUNE wire validation failed: {error}") from error
    if decoded.header.map_name != map_name:
        raise PairError("RUNE embedded map identity disagrees with requested map")
    return record, decoded


def _record_json(record: FileRecord) -> dict[str, object]:
    return {
        "mode": record.mode,
        "path": str(record.path),
        "sha256": record.sha256,
        "size": record.size,
    }


def _expected_record_from_json(value: object, label: str) -> FileRecord:
    if not isinstance(value, dict) or set(value) != {"mode", "path", "sha256", "size"}:
        raise PairError(f"{label} record has incorrect shape")
    path = value["path"]
    digest = value["sha256"]
    size = value["size"]
    mode = value["mode"]
    if (
        not isinstance(path, str)
        or not Path(path).is_absolute()
        or not isinstance(digest, str)
        or SHA_RE.fullmatch(digest) is None
        or type(size) is not int
        or size <= 0
        or type(mode) is not int
        or not 0 <= mode <= 0o7777
    ):
        raise PairError(f"{label} record is invalid")
    return FileRecord(Path(path), digest, size, mode)


def _record_from_json(value: object, label: str) -> FileRecord:
    expected = _expected_record_from_json(value, label)
    actual = _record(expected.path, label)
    if actual != expected:
        raise PairError(f"{label} bytes disagree with accepted record")
    return actual


def _rune_record_from_json(value: object, map_name: str) -> tuple[FileRecord, runeio.RuneArtifact]:
    expected = _expected_record_from_json(value, "RUNE")
    actual, decoded = _rune_record(expected.path, map_name)
    if actual != expected:
        raise PairError("RUNE bytes disagree with accepted record")
    return actual, decoded


def _load_canonical_json(path: Path, label: str) -> tuple[dict[str, object], bytes]:
    payload = _read_regular(path, label)
    try:
        value = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PairError(f"{label} is not valid JSON") from error
    if not isinstance(value, dict) or payload != canonical_json(value):
        raise PairError(f"{label} is not canonical JSON")
    return value, payload


def _fsync_directory(path: Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _atomic_write(path: Path, payload: bytes, mode: int = 0o644) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_text = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_text)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fchmod(stream.fileno(), mode)
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        _fsync_directory(path.parent)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def _require_absent(path: Path, label: str) -> None:
    if path.exists() or path.is_symlink():
        raise PairError(f"{label} already exists")


def write_provenance(
    map_name: str,
    bsp_path: Path,
    rune_path: Path,
    q2ded_path: Path,
    config_path: Path,
    module_paths: Sequence[Path],
    maxclients: int,
    counts: Mapping[str, int],
    output_path: Path,
) -> FileRecord:
    map_name = _map_name(map_name)
    if type(maxclients) is not int or not 1 <= maxclients <= 256:
        raise PairError("maxclients must be in [1,256]")
    expected_count_names = {"seeds", "links", "mechanism_nodes", "plans"}
    if (
        set(counts) != expected_count_names
        or any(type(value) is not int or value < 0 for value in counts.values())
    ):
        raise PairError("generation counts are invalid")
    bsp = _record(bsp_path, "BSP")
    rune = _record(rune_path, "RUNE")
    q2ded = _record(q2ded_path, "q2ded")
    config = _record(config_path, "config")
    modules = tuple(_record(path, "game module") for path in module_paths)
    if {record.path.name for record in modules} != {"game.so", "gamex86_64.so"}:
        raise PairError("frozen modules must be game.so and gamex86_64.so")
    value = {
        "cold_load_command": ["maxclients", "sv sg add red", "quit"],
        "bsp": _record_json(bsp),
        "config": _record_json(config),
        "counts": dict(counts),
        "format": PROVENANCE_FORMAT,
        "generation_command": ["maxclients", "sv rune", "quit"],
        "map": map_name,
        "maxclients": maxclients,
        "modules": [_record_json(record) for record in modules],
        "q2ded": _record_json(q2ded),
        "rune_sha256": rune.sha256,
    }
    _atomic_write(output_path, canonical_json(value))
    return _record(output_path, "provenance")


def _validate_provenance(
    path: Path, map_name: str, bsp: FileRecord, rune: FileRecord
) -> tuple[FileRecord, dict[str, int], tuple[FileRecord, ...], str]:
    value, payload = _load_canonical_json(path, "provenance")
    expected_keys = {
        "cold_load_command",
        "bsp",
        "config",
        "counts",
        "format",
        "generation_command",
        "map",
        "maxclients",
        "modules",
        "q2ded",
        "rune_sha256",
    }
    if set(value) != expected_keys:
        raise PairError("provenance has incorrect shape")
    if (
        value["format"] != PROVENANCE_FORMAT
        or value["map"] != map_name
        or value["rune_sha256"] != rune.sha256
        or value["generation_command"] != ["maxclients", "sv rune", "quit"]
        or value["cold_load_command"] != ["maxclients", "sv sg add red", "quit"]
        or type(value["maxclients"]) is not int
        or not 1 <= value["maxclients"] <= 256
    ):
        raise PairError("provenance identity is invalid")
    counts = value["counts"]
    if (
        not isinstance(counts, dict)
        or set(counts) != {"seeds", "links", "mechanism_nodes", "plans"}
        or any(type(item) is not int or item < 0 for item in counts.values())
    ):
        raise PairError("provenance counts are invalid")
    modules_value = value["modules"]
    if not isinstance(modules_value, list) or len(modules_value) != 2:
        raise PairError("provenance module inventory is invalid")
    inputs = (
        _record_from_json(value["bsp"], "BSP"),
        _record_from_json(value["q2ded"], "q2ded"),
        _record_from_json(value["config"], "config"),
        *(_record_from_json(item, "game module") for item in modules_value),
    )
    if inputs[0] != bsp:
        raise PairError("provenance BSP disagrees with staged map")
    if {record.path.name for record in inputs[3:]} != {"game.so", "gamex86_64.so"}:
        raise PairError("provenance module names are invalid")
    return _record(path, "provenance"), counts, inputs, _sha256(payload)


def stage_rune(
    map_name: str,
    stage_maps: Path,
    provenance_path: Path,
    manifest_path: Path,
) -> AcceptedRune:
    map_name = _map_name(map_name)
    stage_maps = Path(stage_maps)
    bsp = _record(stage_maps / f"{map_name}.bsp", "BSP")
    rune, decoded = _rune_record(stage_maps / f"{map_name}.rune", map_name)
    _require_absent(Path(manifest_path), "RUNE manifest")
    provenance, counts, inputs, fingerprint = _validate_provenance(
        provenance_path, map_name, bsp, rune
    )
    decoded_counts = {
        "seeds": len(decoded.seeds),
        "links": len(decoded.links),
        "mechanism_nodes": len(decoded.activation_nodes),
        "plans": len(decoded.activation_plans),
    }
    if decoded_counts != counts:
        raise PairError("RUNE decoded counts disagree with generation provenance")
    manifest_value = {
        "counts": dict(counts),
        "bsp": _record_json(bsp),
        "fingerprint": fingerprint,
        "format": MANIFEST_FORMAT,
        "frozen_inputs": [_record_json(record) for record in inputs],
        "map": map_name,
        "provenance": _record_json(provenance),
        "rune": _record_json(rune),
    }
    _atomic_write(Path(manifest_path), canonical_json(manifest_value), 0o444)
    try:
        return validate_rune(Path(manifest_path))
    except BaseException:
        Path(manifest_path).unlink(missing_ok=True)
        raise


def validate_rune(manifest_path: Path) -> AcceptedRune:
    value, _payload = _load_canonical_json(Path(manifest_path), "RUNE manifest")
    if set(value) != {
        "counts",
        "bsp",
        "fingerprint",
        "format",
        "frozen_inputs",
        "map",
        "provenance",
        "rune",
    }:
        raise PairError("RUNE manifest has incorrect shape")
    map_name = value["map"]
    fingerprint = value["fingerprint"]
    if (
        value["format"] != MANIFEST_FORMAT
        or not isinstance(map_name, str)
        or MAP_RE.fullmatch(map_name) is None
        or not isinstance(fingerprint, str)
        or SHA_RE.fullmatch(fingerprint) is None
    ):
        raise PairError("RUNE manifest identity is invalid")
    bsp = _record_from_json(value["bsp"], "BSP")
    rune, decoded = _rune_record_from_json(value["rune"], map_name)
    if not isinstance(value["provenance"], dict):
        raise PairError("provenance record has incorrect shape")
    provenance, counts, provenance_inputs, actual_fingerprint = _validate_provenance(
        Path(str(value["provenance"].get("path", ""))), map_name, bsp, rune
    )
    if provenance != _record_from_json(value["provenance"], "provenance"):
        raise PairError("provenance record disagrees with RUNE manifest")
    frozen_values = value["frozen_inputs"]
    if not isinstance(frozen_values, list):
        raise PairError("frozen input inventory is invalid")
    frozen = tuple(
        _record_from_json(item, "frozen input") for item in frozen_values
    )
    if (
        actual_fingerprint != fingerprint
        or frozen != provenance_inputs
        or value["counts"] != counts
    ):
        raise PairError("RUNE manifest disagrees with provenance")
    if counts != {
        "seeds": len(decoded.seeds),
        "links": len(decoded.links),
        "mechanism_nodes": len(decoded.activation_nodes),
        "plans": len(decoded.activation_plans),
    }:
        raise PairError("RUNE decoded counts disagree with generation provenance")
    return AcceptedRune(
        map_name,
        bsp,
        rune,
        provenance,
        Path(manifest_path).resolve(),
        fingerprint,
        counts,
        frozen,
    )


def verify_cold_load(manifest_path: Path, cold_log_path: Path) -> AcceptedRune:
    accepted = validate_rune(manifest_path)
    try:
        text = _read_regular(Path(cold_log_path), "cold-load log").decode("utf-8")
    except UnicodeDecodeError as error:
        raise PairError("cold-load log is not UTF-8") from error
    lines = text.splitlines()
    failure = next((line for line in reversed(lines) if FAILURE_RE.fullmatch(line)), None)
    if failure is not None:
        raise PairError(f"cold load rejected RUNE: {failure}")
    if any(line.startswith("rune: wrote ") for line in lines):
        raise PairError("cold load unexpectedly generated a RUNE")
    matches = [
        match for line in lines if (match := RUNE_READY_RE.fullmatch(line)) is not None
    ]
    if len(matches) != 1:
        raise PairError("cold load requires one RUNE-ready line")
    match = matches[0]
    actual_counts = {
        "seeds": int(match.group(2)),
        "links": int(match.group(3)),
        "mechanism_nodes": int(match.group(4)),
        "plans": int(match.group(5)),
    }
    if match.group(1) != accepted.map_name or actual_counts != accepted.counts:
        raise PairError("cold-load RUNE readiness disagrees with staged artifact")
    return validate_rune(manifest_path)


def _fault(point: str) -> None:
    configured = os.environ.get("RUNEGEN_PAIR_FAULT", "")
    if configured == f"fail:{point}":
        raise PairError(f"injected failure at {point}")
    if configured == f"crash:{point}":
        raise InjectedCrash(point)


def _copy_payload(path: Path, payload: bytes, mode: int) -> None:
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
    try:
        with os.fdopen(descriptor, "wb", closefd=False) as stream:
            stream.write(payload)
            stream.flush()
            os.fchmod(stream.fileno(), mode)
            os.fsync(stream.fileno())
    finally:
        os.close(descriptor)


def _inventory_live(path: Path, label: str) -> tuple[bool, bytes | None, int, str | None]:
    if not path.exists() and not path.is_symlink():
        return False, None, 0, None
    payload = _read_regular(path, label)
    info = path.stat(follow_symlinks=False)
    return True, payload, stat.S_IMODE(info.st_mode), _sha256(payload)


def _transaction_name(map_name: str, value: object, role: str) -> tuple[str, str]:
    prefix = f".runegen-{map_name}.{role}-"
    if (
        not isinstance(value, str)
        or value != Path(value).name
        or not value.startswith(prefix)
        or re.fullmatch(r"[0-9a-f]{32}", value[len(prefix):]) is None
    ):
        raise PairError("invalid transaction path")
    return value, value[len(prefix):]


def _journal_path(map_name: str, live_maps: Path) -> Path:
    return Path(live_maps) / f".runegen-{map_name}.transaction.json"


@contextmanager
def _map_install_lock(map_name: str, live_maps: Path):
    lock_path = Path(live_maps) / f".runegen-{map_name}.lock"
    flags = os.O_RDWR | os.O_CREAT | getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(lock_path, flags, 0o600)
    except OSError as error:
        raise PairError(f"cannot open per-map install lock: {error}") from error
    try:
        opened = os.fstat(descriptor)
        named = lock_path.stat(follow_symlinks=False)
        if (
            not stat.S_ISREG(opened.st_mode)
            or opened.st_nlink != 1
            or (opened.st_dev, opened.st_ino) != (named.st_dev, named.st_ino)
        ):
            raise PairError("per-map install lock is not an unaliased regular file")
        fcntl.flock(descriptor, fcntl.LOCK_EX)
        locked = os.fstat(descriptor)
        named = lock_path.stat(follow_symlinks=False)
        if (
            locked.st_nlink != 1
            or (locked.st_dev, locked.st_ino) != (named.st_dev, named.st_ino)
        ):
            raise PairError("per-map install lock changed while waiting")
        yield
    finally:
        os.close(descriptor)


def _write_journal(path: Path, value: dict[str, object]) -> None:
    _atomic_write(path, canonical_json(value), 0o600)


def _unlink_recorded(path: Path) -> None:
    try:
        info = path.lstat()
    except FileNotFoundError:
        return
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        raise PairError("transaction file is not an unaliased regular file")
    path.unlink()


def _current_digest(path: Path, label: str) -> str | None:
    if not path.exists() and not path.is_symlink():
        return None
    return _sha256(_read_regular(path, label))


def _validate_old_record(map_name: str, value: object) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != {
        "exists",
        "mode",
        "rollback",
        "sha256",
    }:
        raise PairError("old transaction record is invalid")
    if type(value["exists"]) is not bool or type(value["mode"]) is not int:
        raise PairError("old transaction metadata is invalid")
    if value["exists"]:
        _transaction_name(map_name, value["rollback"], "old-rune")
        if not isinstance(value["sha256"], str) or SHA_RE.fullmatch(value["sha256"]) is None:
            raise PairError("old transaction digest is invalid")
    elif value["rollback"] is not None or value["sha256"] is not None:
        raise PairError("absent old transaction record is invalid")
    return value


def _validate_new_record(map_name: str, value: object) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != {"mode", "sha256", "temporary"}:
        raise PairError("new transaction record is invalid")
    if (
        type(value["mode"]) is not int
        or not isinstance(value["sha256"], str)
        or SHA_RE.fullmatch(value["sha256"]) is None
    ):
        raise PairError("new transaction metadata is invalid")
    _transaction_name(map_name, value["temporary"], "new-rune")
    return value


def _cleanup_transaction(
    live_maps: Path, journal_path: Path, value: Mapping[str, object]
) -> None:
    old = value["old"]
    new = value["new"]
    journal_path.unlink(missing_ok=True)
    _fsync_directory(live_maps)
    if old["rollback"] is not None:
        rollback_name, _token = _transaction_name(
            value["map"], old["rollback"], "old-rune"
        )
        _unlink_recorded(live_maps / rollback_name)
    temporary_name, _token = _transaction_name(
        value["map"], new["temporary"], "new-rune"
    )
    _unlink_recorded(live_maps / temporary_name)
    _fsync_directory(live_maps)


def _load_journal(map_name: str, live_maps: Path) -> tuple[Path, dict[str, object]] | None:
    journal = _journal_path(map_name, live_maps)
    if not journal.exists() and not journal.is_symlink():
        return None
    value, _payload = _load_canonical_json(journal, "transaction journal")
    if set(value) != {"format", "map", "new", "old", "state"}:
        raise PairError("transaction journal has incorrect shape")
    if value["format"] != TRANSACTION_FORMAT or value["map"] != map_name:
        raise PairError("transaction journal identity is invalid")
    if value["state"] not in {"prepared", "committed"}:
        raise PairError("transaction journal state is invalid")
    old = _validate_old_record(map_name, value["old"])
    new = _validate_new_record(map_name, value["new"])
    _temporary_name, token = _transaction_name(map_name, new["temporary"], "new-rune")
    if old["rollback"] is not None:
        _rollback_name, rollback_token = _transaction_name(
            map_name, old["rollback"], "old-rune"
        )
        if rollback_token != token:
            raise PairError("transaction paths do not share one owner")
    return journal, value


def _recover_interrupted_install_locked(
    map_name: str, live_maps: Path, *, force_rollback: bool = False
) -> None:
    loaded = _load_journal(map_name, live_maps)
    if loaded is None:
        return
    journal, value = loaded
    destination = live_maps / f"{map_name}.rune"
    old = value["old"]
    new = value["new"]
    if old["rollback"] is not None:
        rollback_name, _token = _transaction_name(
            map_name, old["rollback"], "old-rune"
        )
        rollback = live_maps / rollback_name
        if _current_digest(rollback, "RUNE rollback") != old["sha256"]:
            raise PairError("RUNE rollback bytes are unavailable")
    temporary_name, _token = _transaction_name(
        map_name, new["temporary"], "new-rune"
    )
    temporary = live_maps / temporary_name
    if temporary.exists() or temporary.is_symlink():
        if _current_digest(temporary, "new RUNE temporary") != new["sha256"]:
            raise PairError("new RUNE temporary digest mismatch")
    if not force_rollback and _current_digest(destination, "live RUNE") == new["sha256"]:
        _cleanup_transaction(live_maps, journal, value)
        return
    restore: Path | None = None
    try:
        if old["exists"]:
            payload = _read_regular(live_maps / old["rollback"], "RUNE rollback")
            restore = live_maps / f".runegen-{map_name}.restore-rune-{uuid.uuid4().hex}"
            _copy_payload(restore, payload, old["mode"])
            os.replace(restore, destination)
            restore = None
            _fsync_directory(live_maps)
        elif destination.exists() or destination.is_symlink():
            _read_regular(destination, "live RUNE")
            destination.unlink()
            _fsync_directory(live_maps)
        _cleanup_transaction(live_maps, journal, value)
    finally:
        if restore is not None:
            restore.unlink(missing_ok=True)


def recover_interrupted_install(
    map_name: str, live_maps: Path, *, force_rollback: bool = False
) -> None:
    map_name = _map_name(map_name)
    live_maps = Path(live_maps).resolve()
    if not live_maps.is_dir():
        raise PairError("live maps directory is missing")
    with _map_install_lock(map_name, live_maps):
        _recover_interrupted_install_locked(
            map_name, live_maps, force_rollback=force_rollback
        )


def _backup_old_rune(
    map_name: str,
    backup_dir: Path,
    old: tuple[bool, bytes | None, int, str | None],
) -> BackupRecord:
    backup_dir.mkdir(parents=True, exist_ok=True)
    directory = backup_dir / (
        f"{map_name}-{time.strftime('%Y%m%d-%H%M%S')}-{uuid.uuid4().hex}"
    )
    directory.mkdir(mode=0o700)
    exists, payload, mode, digest = old
    if exists:
        _copy_payload(directory / "old.rune", payload, mode)
    manifest = directory / "manifest.json"
    _atomic_write(
        manifest,
        canonical_json({
            "format": BACKUP_FORMAT,
            "map": map_name,
            "rune": {"exists": exists, "mode": mode, "sha256": digest},
        }),
        0o600,
    )
    _fsync_directory(directory)
    _fsync_directory(backup_dir)
    return BackupRecord(directory, manifest)


def install_rune(
    accepted: AcceptedRune, live_maps: Path, backup_dir: Path
) -> BackupRecord:
    accepted = validate_rune(accepted.manifest_path)
    live_maps = Path(live_maps).resolve()
    if not live_maps.is_dir():
        raise PairError("live maps directory is missing")
    with _map_install_lock(accepted.map_name, live_maps):
        return _install_rune_locked(accepted, live_maps, Path(backup_dir))


def _install_rune_locked(
    accepted: AcceptedRune, live_maps: Path, backup_dir: Path
) -> BackupRecord:
    _recover_interrupted_install_locked(accepted.map_name, live_maps)
    destination = live_maps / f"{accepted.map_name}.rune"
    old = _inventory_live(destination, "live RUNE")
    backup = _backup_old_rune(accepted.map_name, backup_dir, old)
    token = uuid.uuid4().hex
    temporary_name = f".runegen-{accepted.map_name}.new-rune-{token}"
    rollback_name = (
        f".runegen-{accepted.map_name}.old-rune-{token}" if old[0] else None
    )
    journal = _journal_path(accepted.map_name, live_maps)
    try:
        temporary = live_maps / temporary_name
        _copy_payload(
            temporary,
            _read_regular(accepted.rune.path, "RUNE"),
            accepted.rune.mode,
        )
        if _current_digest(temporary, "new RUNE") != accepted.rune.sha256:
            raise PairError("new RUNE copy digest mismatch")
        if rollback_name is not None:
            rollback = live_maps / rollback_name
            _copy_payload(rollback, old[1], old[2])
            if _current_digest(rollback, "old RUNE") != old[3]:
                raise PairError("old RUNE rollback digest mismatch")
        _fsync_directory(live_maps)
        value: dict[str, object] = {
            "format": TRANSACTION_FORMAT,
            "map": accepted.map_name,
            "new": {
                "mode": accepted.rune.mode,
                "sha256": accepted.rune.sha256,
                "temporary": temporary_name,
            },
            "old": {
                "exists": old[0],
                "mode": old[2],
                "rollback": rollback_name,
                "sha256": old[3],
            },
            "state": "prepared",
        }
        _write_journal(journal, value)
        _fault("before-rune-replace")
        os.replace(temporary, destination)
        _fsync_directory(live_maps)
        _fault("after-rune-replace")
        value["state"] = "committed"
        _write_journal(journal, value)
        _cleanup_transaction(live_maps, journal, value)
        return backup
    except Exception as error:
        if journal.exists() or journal.is_symlink():
            try:
                _recover_interrupted_install_locked(
                    accepted.map_name, live_maps, force_rollback=True
                )
            except Exception as recovery_error:
                raise PairError(
                    f"RUNE install failed and rollback failed: {recovery_error}"
                ) from error
        else:
            for name in (temporary_name, rollback_name):
                if name is not None:
                    (live_maps / name).unlink(missing_ok=True)
        if isinstance(error, PairError):
            raise
        raise PairError(f"RUNE install failed: {error}") from error


def _parse_counts(values: Sequence[str]) -> dict[str, int]:
    result: dict[str, int] = {}
    for value in values:
        name, separator, raw = value.partition("=")
        if not separator or name in result:
            raise PairError("count must be NAME=VALUE")
        try:
            result[name] = int(raw)
        except ValueError as error:
            raise PairError("count value must be an integer") from error
    return result


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)

    recover = commands.add_parser("recover")
    recover.add_argument("--map", required=True)
    recover.add_argument("--live-maps", required=True, type=Path)

    provenance = commands.add_parser("provenance")
    provenance.add_argument("--map", required=True)
    provenance.add_argument("--bsp", required=True, type=Path)
    provenance.add_argument("--rune", required=True, type=Path)
    provenance.add_argument("--q2ded", required=True, type=Path)
    provenance.add_argument("--config", required=True, type=Path)
    provenance.add_argument("--module", action="append", required=True, type=Path)
    provenance.add_argument("--maxclients", required=True, type=int)
    provenance.add_argument("--count", action="append", required=True)
    provenance.add_argument("--output", required=True, type=Path)

    stage = commands.add_parser("stage")
    stage.add_argument("--map", required=True)
    stage.add_argument("--stage-maps", required=True, type=Path)
    stage.add_argument("--provenance", required=True, type=Path)
    stage.add_argument("--manifest", required=True, type=Path)

    verify = commands.add_parser("verify-cold-load")
    verify.add_argument("--manifest", required=True, type=Path)
    verify.add_argument("--cold-log", required=True, type=Path)

    install = commands.add_parser("install")
    install.add_argument("--manifest", required=True, type=Path)
    install.add_argument("--live-maps", required=True, type=Path)
    install.add_argument("--backup-dir", required=True, type=Path)

    args = parser.parse_args(argv)
    try:
        if args.command == "recover":
            recover_interrupted_install(args.map, args.live_maps)
        elif args.command == "provenance":
            write_provenance(
                args.map,
                args.bsp,
                args.rune,
                args.q2ded,
                args.config,
                args.module,
                args.maxclients,
                _parse_counts(args.count),
                args.output,
            )
        elif args.command == "stage":
            stage_rune(
                args.map,
                args.stage_maps,
                args.provenance,
                args.manifest,
            )
        elif args.command == "verify-cold-load":
            verify_cold_load(args.manifest, args.cold_log)
        elif args.command == "install":
            backup = install_rune(
                validate_rune(args.manifest), args.live_maps, args.backup_dir
            )
            print(backup.manifest)
    except PairError as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
