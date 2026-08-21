#!/usr/bin/env python3
"""Stage, attest, install, and recover one RUNE/SNAG pair."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import tempfile
import time
import uuid
from typing import Mapping, Sequence

import snagrepair


PAIR_FORMAT = "lmctf-runegen-pair-v1"
PROVENANCE_FORMAT = "lmctf-runegen-provenance-v1"
TRANSACTION_FORMAT = "lmctf-runegen-transaction-v1"
BACKUP_FORMAT = "lmctf-runegen-backup-v1"
MAP_RE = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_-]{0,62}\Z")
SHA_RE = re.compile(r"[0-9a-f]{64}\Z")
MAX_FILE_BYTES = 512 * 1024 * 1024
SNAG_READY_RE = re.compile(
    r"slipgate: snag ready map=([A-Za-z0-9_][A-Za-z0-9_-]{0,62}) "
    r"repairs=([0-9]+) rune_sha256=([0-9a-f]{64}) "
    r"evidence_sha256=([0-9a-f]{64}) snag_sha256=([0-9a-f]{64})\Z"
)
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
class AcceptedPair:
    map_name: str
    rune: FileRecord
    snag: FileRecord
    evidence: FileRecord
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
        identity = lambda item: (
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


def _record_json(record: FileRecord) -> dict[str, object]:
    return {
        "mode": record.mode,
        "path": str(record.path),
        "sha256": record.sha256,
        "size": record.size,
    }


def _record_from_json(value: object, label: str) -> FileRecord:
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
    actual = _record(Path(path), label)
    expected = FileRecord(Path(path), digest, size, mode)
    if actual != expected:
        raise PairError(f"{label} bytes disagree with accepted record")
    return actual


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
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise


def _require_absent(path: Path, label: str) -> None:
    if path.exists() or path.is_symlink():
        raise PairError(f"{label} already exists")


def write_provenance(
    map_name: str,
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
    rune = _record(rune_path, "RUNE")
    q2ded = _record(q2ded_path, "q2ded")
    config = _record(config_path, "config")
    modules = tuple(_record(path, "game module") for path in module_paths)
    if {record.path.name for record in modules} != {"game.so", "gamex86_64.so"}:
        raise PairError("frozen modules must be game.so and gamex86_64.so")
    value = {
        "cold_load_command": ["maxclients", "sv sg add red", "quit"],
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
    path: Path, map_name: str, rune: FileRecord
) -> tuple[FileRecord, dict[str, int], tuple[FileRecord, ...], str]:
    value, payload = _load_canonical_json(path, "provenance")
    expected_keys = {
        "cold_load_command",
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
        or value["cold_load_command"]
        != ["maxclients", "sv sg add red", "quit"]
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
        _record_from_json(value["q2ded"], "q2ded"),
        _record_from_json(value["config"], "config"),
        *(
            _record_from_json(item, "game module")
            for item in modules_value
        ),
    )
    if {record.path.name for record in inputs[2:]} != {"game.so", "gamex86_64.so"}:
        raise PairError("provenance module names are invalid")
    return _record(path, "provenance"), counts, inputs, _sha256(payload)


def _validate_explicit_zero_snag(
    map_name: str,
    rune: FileRecord,
    evidence: FileRecord,
    snag: FileRecord,
) -> None:
    rune_value, rune_digest = snagrepair.read_rune_and_sha256(rune.path)
    expected = snagrepair.render(
        map_name, rune_value, [], evidence.sha256, rune_digest
    ).encode("ascii")
    if _read_regular(snag.path, "SNAG") != expected:
        raise PairError("SNAG is not the canonical explicit-zero declaration")


def stage_explicit_zero_pair(
    map_name: str,
    stage_maps: Path,
    evidence_path: Path,
    provenance_path: Path,
    manifest_path: Path,
) -> AcceptedPair:
    map_name = _map_name(map_name)
    stage_maps = Path(stage_maps)
    rune_path = stage_maps / f"{map_name}.rune"
    snag_path = stage_maps / f"{map_name}.snag"
    for path, label in (
        (snag_path, "SNAG"),
        (evidence_path, "evidence"),
        (manifest_path, "pair manifest"),
    ):
        _require_absent(Path(path), label)
    rune = _record(rune_path, "RUNE")
    provenance, counts, inputs, fingerprint = _validate_provenance(
        provenance_path, map_name, rune
    )
    evidence_value = {
        "artifact_sha256": rune.sha256,
        "classification": "NO_ACCEPTED_OBSERVATION",
        "fingerprint": fingerprint,
        "format": "lmctf-snag-bootstrap-v1",
        "map": map_name,
    }
    try:
        _atomic_write(Path(evidence_path), canonical_json(evidence_value), 0o444)
        evidence = _record(Path(evidence_path), "evidence")
        rune_value, rune_digest = snagrepair.read_rune_and_sha256(rune.path)
        snagrepair.atomic_write(
            snag_path,
            snagrepair.render(
                map_name, rune_value, [], evidence.sha256, rune_digest
            ),
        )
        snag_path.chmod(0o444)
        if _record(rune_path, "RUNE") != rune:
            raise PairError("RUNE changed while staging SNAG")
        snag = _record(snag_path, "SNAG")
        _validate_explicit_zero_snag(map_name, rune, evidence, snag)
        manifest_value = {
            "counts": dict(counts),
            "evidence": _record_json(evidence),
            "fingerprint": fingerprint,
            "format": PAIR_FORMAT,
            "frozen_inputs": [_record_json(record) for record in inputs],
            "map": map_name,
            "provenance": _record_json(provenance),
            "rune": _record_json(rune),
            "snag": _record_json(snag),
        }
        _atomic_write(Path(manifest_path), canonical_json(manifest_value), 0o444)
        return validate_pair(Path(manifest_path))
    except BaseException:
        for path in (Path(manifest_path), snag_path, Path(evidence_path)):
            try:
                path.unlink()
            except FileNotFoundError:
                pass
        raise


def validate_pair(manifest_path: Path) -> AcceptedPair:
    value, _payload = _load_canonical_json(Path(manifest_path), "pair manifest")
    if set(value) != {
        "counts",
        "evidence",
        "fingerprint",
        "format",
        "frozen_inputs",
        "map",
        "provenance",
        "rune",
        "snag",
    }:
        raise PairError("pair manifest has incorrect shape")
    map_name = value["map"]
    fingerprint = value["fingerprint"]
    if (
        value["format"] != PAIR_FORMAT
        or not isinstance(map_name, str)
        or MAP_RE.fullmatch(map_name) is None
        or not isinstance(fingerprint, str)
        or SHA_RE.fullmatch(fingerprint) is None
    ):
        raise PairError("pair manifest identity is invalid")
    rune = _record_from_json(value["rune"], "RUNE")
    snag = _record_from_json(value["snag"], "SNAG")
    evidence = _record_from_json(value["evidence"], "evidence")
    if not isinstance(value["provenance"], dict):
        raise PairError("provenance record has incorrect shape")
    provenance, counts, provenance_inputs, actual_fingerprint = _validate_provenance(
        Path(str(value["provenance"].get("path", ""))), map_name, rune
    )
    if provenance != _record_from_json(value["provenance"], "provenance"):
        raise PairError("provenance record disagrees with pair manifest")
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
        raise PairError("pair manifest disagrees with provenance")
    evidence_value, evidence_payload = _load_canonical_json(evidence.path, "evidence")
    expected_evidence = {
        "artifact_sha256": rune.sha256,
        "classification": "NO_ACCEPTED_OBSERVATION",
        "fingerprint": fingerprint,
        "format": "lmctf-snag-bootstrap-v1",
        "map": map_name,
    }
    if evidence_value != expected_evidence or _sha256(evidence_payload) != evidence.sha256:
        raise PairError("evidence identity is invalid")
    _validate_explicit_zero_snag(map_name, rune, evidence, snag)
    return AcceptedPair(
        map_name,
        rune,
        snag,
        evidence,
        provenance,
        Path(manifest_path).resolve(),
        fingerprint,
        counts,
        frozen,
    )


def verify_cold_load(manifest_path: Path, cold_log_path: Path) -> AcceptedPair:
    pair = validate_pair(manifest_path)
    try:
        text = _read_regular(Path(cold_log_path), "cold-load log").decode("utf-8")
    except UnicodeDecodeError as error:
        raise PairError("cold-load log is not UTF-8") from error
    lines = text.splitlines()
    failure = next((line for line in reversed(lines) if FAILURE_RE.fullmatch(line)), None)
    if failure is not None:
        raise PairError(f"cold load rejected pair: {failure}")
    if any(line.startswith("rune: wrote ") for line in lines):
        raise PairError("cold load unexpectedly generated a RUNE")
    snag_matches = [
        (index, match)
        for index, line in enumerate(lines)
        if (match := SNAG_READY_RE.fullmatch(line)) is not None
    ]
    rune_matches = [
        (index, match)
        for index, line in enumerate(lines)
        if (match := RUNE_READY_RE.fullmatch(line)) is not None
    ]
    if len(snag_matches) != 1 or len(rune_matches) != 1:
        raise PairError("cold load requires one SNAG-ready and one RUNE-ready line")
    snag_index, snag_match = snag_matches[0]
    rune_index, rune_match = rune_matches[0]
    if snag_index >= rune_index:
        raise PairError("cold-load readiness lines are reversed")
    if (
        snag_match.group(1) != pair.map_name
        or int(snag_match.group(2)) != 0
        or snag_match.group(3) != pair.rune.sha256
        or snag_match.group(4) != pair.evidence.sha256
        or snag_match.group(5) != pair.snag.sha256
    ):
        raise PairError("cold-load SNAG attestation disagrees with staged bytes")
    actual_counts = {
        "seeds": int(rune_match.group(2)),
        "links": int(rune_match.group(3)),
        "mechanism_nodes": int(rune_match.group(4)),
        "plans": int(rune_match.group(5)),
    }
    if rune_match.group(1) != pair.map_name or actual_counts != pair.counts:
        raise PairError("cold-load RUNE readiness disagrees with staged pair")
    return validate_pair(manifest_path)


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


def _safe_transaction_name(map_name: str, value: object) -> str:
    prefix = f".runegen-{map_name}."
    if (
        not isinstance(value, str)
        or value != Path(value).name
        or not value.startswith(prefix)
    ):
        raise PairError("invalid transaction path")
    return value


def _journal_path(map_name: str, live_maps: Path) -> Path:
    return Path(live_maps) / f".runegen-{map_name}.transaction.json"


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


def _cleanup_transaction(
    live_maps: Path, journal_path: Path, value: Mapping[str, object]
) -> None:
    for side in ("rune", "snag"):
        old = value["old"][side]
        new = value["new"][side]
        rollback = old["rollback"]
        if rollback is not None:
            _unlink_recorded(live_maps / _safe_transaction_name(value["map"], rollback))
        _unlink_recorded(live_maps / _safe_transaction_name(value["map"], new["temporary"]))
    journal_path.unlink(missing_ok=True)
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
    if value["state"] not in {"prepared", "snag-installed", "committed"}:
        raise PairError("transaction journal state is invalid")
    for inventory_name in ("old", "new"):
        inventory = value[inventory_name]
        if not isinstance(inventory, dict) or set(inventory) != {"rune", "snag"}:
            raise PairError("transaction inventory is invalid")
    for side in ("rune", "snag"):
        old = value["old"][side]
        new = value["new"][side]
        if not isinstance(old, dict) or set(old) != {
            "exists",
            "mode",
            "rollback",
            "sha256",
        }:
            raise PairError("old transaction record is invalid")
        if not isinstance(new, dict) or set(new) != {"mode", "sha256", "temporary"}:
            raise PairError("new transaction record is invalid")
        if type(old["exists"]) is not bool:
            raise PairError("old transaction existence is invalid")
        if old["exists"]:
            _safe_transaction_name(map_name, old["rollback"])
            if not isinstance(old["sha256"], str) or SHA_RE.fullmatch(old["sha256"]) is None:
                raise PairError("old transaction digest is invalid")
        elif old["rollback"] is not None or old["sha256"] is not None:
            raise PairError("absent old transaction record is invalid")
        if (
            type(old["mode"]) is not int
            or type(new["mode"]) is not int
            or not isinstance(new["sha256"], str)
            or SHA_RE.fullmatch(new["sha256"]) is None
        ):
            raise PairError("transaction file metadata is invalid")
        _safe_transaction_name(map_name, new["temporary"])
    return journal, value


def recover_interrupted_install(
    map_name: str, live_maps: Path, *, force_rollback: bool = False
) -> None:
    map_name = _map_name(map_name)
    live_maps = Path(live_maps).resolve()
    loaded = _load_journal(map_name, live_maps)
    if loaded is None:
        return
    journal, value = loaded
    destinations = {
        "rune": live_maps / f"{map_name}.rune",
        "snag": live_maps / f"{map_name}.snag",
    }
    if not force_rollback and all(
        _current_digest(destinations[side], f"live {side}")
        == value["new"][side]["sha256"]
        for side in ("rune", "snag")
    ):
        _cleanup_transaction(live_maps, journal, value)
        return
    for side in ("rune", "snag"):
        old = value["old"][side]
        new = value["new"][side]
        if old["rollback"] is not None:
            rollback = live_maps / _safe_transaction_name(map_name, old["rollback"])
            if _current_digest(rollback, f"{side} rollback") != old["sha256"]:
                raise PairError(f"{side} rollback bytes are unavailable")
        temporary = live_maps / _safe_transaction_name(map_name, new["temporary"])
        if temporary.exists() or temporary.is_symlink():
            _read_regular(temporary, f"new {side} temporary")
    restore_temporaries: list[Path] = []
    try:
        for side in ("snag", "rune"):
            old = value["old"][side]
            destination = destinations[side]
            if old["exists"]:
                rollback = live_maps / old["rollback"]
                payload = _read_regular(rollback, f"{side} rollback")
                restore = live_maps / f".runegen-{map_name}.restore-{side}-{uuid.uuid4().hex}"
                _copy_payload(restore, payload, old["mode"])
                restore_temporaries.append(restore)
                os.replace(restore, destination)
                restore_temporaries.remove(restore)
                _fsync_directory(live_maps)
            else:
                if destination.exists() or destination.is_symlink():
                    _read_regular(destination, f"live {side}")
                    destination.unlink()
                    _fsync_directory(live_maps)
        _cleanup_transaction(live_maps, journal, value)
    finally:
        for path in restore_temporaries:
            path.unlink(missing_ok=True)


def _backup_old_pair(
    map_name: str,
    backup_dir: Path,
    old: Mapping[str, tuple[bool, bytes | None, int, str | None]],
) -> BackupRecord:
    backup_dir.mkdir(parents=True, exist_ok=True)
    directory = backup_dir / (
        f"{map_name}-{time.strftime('%Y%m%d-%H%M%S')}-{uuid.uuid4().hex}"
    )
    directory.mkdir(mode=0o700)
    records: dict[str, object] = {}
    for side in ("rune", "snag"):
        exists, payload, mode, digest = old[side]
        records[side] = {
            "exists": exists,
            "mode": mode,
            "sha256": digest,
        }
        if exists:
            destination = directory / f"old.{side}"
            _copy_payload(destination, payload, mode)
    manifest = directory / "manifest.json"
    _atomic_write(
        manifest,
        canonical_json({"format": BACKUP_FORMAT, "map": map_name, "old": records}),
        0o600,
    )
    _fsync_directory(directory)
    _fsync_directory(backup_dir)
    return BackupRecord(directory, manifest)


def install_pair(
    pair: AcceptedPair, live_maps: Path, backup_dir: Path
) -> BackupRecord:
    pair = validate_pair(pair.manifest_path)
    live_maps = Path(live_maps).resolve()
    if not live_maps.is_dir():
        raise PairError("live maps directory is missing")
    recover_interrupted_install(pair.map_name, live_maps)
    destinations = {
        "rune": live_maps / f"{pair.map_name}.rune",
        "snag": live_maps / f"{pair.map_name}.snag",
    }
    old = {
        side: _inventory_live(path, f"live {side}")
        for side, path in destinations.items()
    }
    backup = _backup_old_pair(pair.map_name, Path(backup_dir), old)
    token = uuid.uuid4().hex
    new_records = {"rune": pair.rune, "snag": pair.snag}
    temporary_names: dict[str, str] = {}
    rollback_names: dict[str, str | None] = {}
    journal = _journal_path(pair.map_name, live_maps)
    try:
        for side in ("rune", "snag"):
            record = new_records[side]
            temporary_name = f".runegen-{pair.map_name}.new-{side}-{token}"
            temporary = live_maps / temporary_name
            _copy_payload(temporary, _read_regular(record.path, side), record.mode)
            if _current_digest(temporary, f"new {side}") != record.sha256:
                raise PairError(f"new {side} copy digest mismatch")
            temporary_names[side] = temporary_name
            exists, payload, mode, digest = old[side]
            if exists:
                rollback_name = f".runegen-{pair.map_name}.old-{side}-{token}"
                rollback = live_maps / rollback_name
                _copy_payload(rollback, payload, mode)
                if _current_digest(rollback, f"old {side}") != digest:
                    raise PairError(f"old {side} rollback digest mismatch")
                rollback_names[side] = rollback_name
            else:
                rollback_names[side] = None
        _fsync_directory(live_maps)
        value: dict[str, object] = {
            "format": TRANSACTION_FORMAT,
            "map": pair.map_name,
            "new": {
                side: {
                    "mode": new_records[side].mode,
                    "sha256": new_records[side].sha256,
                    "temporary": temporary_names[side],
                }
                for side in ("rune", "snag")
            },
            "old": {
                side: {
                    "exists": old[side][0],
                    "mode": old[side][2],
                    "rollback": rollback_names[side],
                    "sha256": old[side][3],
                }
                for side in ("rune", "snag")
            },
            "state": "prepared",
        }
        _write_journal(journal, value)
        _fault("before-snag-replace")
        os.replace(live_maps / temporary_names["snag"], destinations["snag"])
        _fsync_directory(live_maps)
        _fault("after-snag-replace")
        value["state"] = "snag-installed"
        _write_journal(journal, value)
        os.replace(live_maps / temporary_names["rune"], destinations["rune"])
        _fsync_directory(live_maps)
        _fault("after-rune-replace")
        value["state"] = "committed"
        _write_journal(journal, value)
        _cleanup_transaction(live_maps, journal, value)
        return backup
    except Exception as error:
        if journal.exists() or journal.is_symlink():
            try:
                recover_interrupted_install(
                    pair.map_name, live_maps, force_rollback=True
                )
            except Exception as recovery_error:
                raise PairError(
                    f"pair install failed and rollback failed: {recovery_error}"
                ) from error
        else:
            for name in (*temporary_names.values(), *(item for item in rollback_names.values() if item)):
                (live_maps / name).unlink(missing_ok=True)
        if isinstance(error, PairError):
            raise
        raise PairError(f"pair install failed: {error}") from error


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
    stage.add_argument("--evidence", required=True, type=Path)
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
                args.rune,
                args.q2ded,
                args.config,
                args.module,
                args.maxclients,
                _parse_counts(args.count),
                args.output,
            )
        elif args.command == "stage":
            stage_explicit_zero_pair(
                args.map,
                args.stage_maps,
                args.evidence,
                args.provenance,
                args.manifest,
            )
        elif args.command == "verify-cold-load":
            verify_cold_load(args.manifest, args.cold_log)
        elif args.command == "install":
            backup = install_pair(
                validate_pair(args.manifest), args.live_maps, args.backup_dir
            )
            print(backup.manifest)
    except PairError as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
