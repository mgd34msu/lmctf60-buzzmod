#!/usr/bin/env python3
"""Seal one accepted generation run with a content-addressed authority."""

from __future__ import annotations

import ctypes
import errno
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile
from typing import Any, Mapping, Sequence


FORMAT = "lmctf-rune-final-corpus-v1"
SEAL_FORMAT = "lmctf-rune-final-source-seal-v1"
AUTHORITY_FILENAME = "corpus-authority.json"
SEAL_FILENAME = "final-corpus-seal.json"
RENAME_NOREPLACE = 1


def _canonical_object(api: Any, path: Path) -> tuple[dict[str, Any], bytes]:
    value, raw = api._load_json_regular(path, require_unaliased=True)
    if not isinstance(value, dict) or raw != api.canonical_json(value):
        raise api.CorpusError(f"noncanonical JSON authority: {path}")
    return value, raw


def _identity(api: Any, snapshot: Path, run_root: Path) -> dict[str, Any]:
    api.reject_symlink_components(snapshot)
    api.reject_symlink_components(run_root)
    verified = api.verify_snapshot(snapshot)
    roles = verified.get("by_role")
    if not isinstance(roles, dict) or not isinstance(roles.get("map_manifest"), dict):
        raise api.CorpusError("snapshot has no map manifest authority")
    maps = tuple(api.validate_manifest(snapshot / roles["map_manifest"]["path"]))
    if not set(api.APPROVED_ROUTE_ONLY_MAPS).issubset(maps):
        raise api.CorpusError("map manifest lacks an approved route-only candidate")
    document, document_bytes = _canonical_object(
        api, run_root / "fingerprint-document.json"
    )
    fingerprint = api.verify_fingerprint_document(snapshot, document)
    fingerprint_bytes, _record = api.read_regular_bytes(
        run_root / "fingerprint.txt", require_unaliased=True
    )
    if fingerprint_bytes != (fingerprint + "\n").encode("ascii"):
        raise api.CorpusError("stored fingerprint text differs from the document")
    port_base = document.get("port_base")
    if type(port_base) is not int:
        raise api.CorpusError("fingerprint has no stable port base")
    return {
        "document": document,
        "document_bytes": document_bytes,
        "fingerprint": fingerprint,
        "maps": maps,
        "port_base": port_base,
        "roles": roles,
        "snapshot_verified": verified,
    }


def _record_matches(api: Any, record: Any, expected: Path, label: str) -> Path:
    if not isinstance(record, dict):
        raise api.CorpusError(f"{label} record is invalid")
    path = Path(str(record.get("path", "")))
    try:
        resolved = path.resolve(strict=True)
        expected_resolved = expected.resolve(strict=True)
    except OSError as exc:
        raise api.CorpusError(f"{label} path is unavailable: {exc}") from exc
    current = api.regular_file_record(path, require_unaliased=True)
    if resolved != expected_resolved or current != record:
        raise api.CorpusError(f"{label} identity changed")
    return path


def _accepted_result(
    api: Any,
    *,
    snapshot: Path,
    run_root: Path,
    map_name: str,
    stable_port: int,
    fingerprint: str,
    document_bytes: bytes,
    result_path: Path,
) -> tuple[dict[str, Any], dict[str, Any]]:
    validated = api.validate_terminal_result(
        result_path,
        run_root=run_root,
        map_name=map_name,
        fingerprint=fingerprint,
        stable_port=stable_port,
        snapshot=snapshot,
        fingerprint_document_bytes=document_bytes,
        recheck_pass_gates=True,
        runtime_preflighted=True,
    )
    if validated is None:
        raise api.CorpusError(f"final result is invalid for {map_name}")
    result, _raw = validated
    classification = result.get("classification")
    route_contract = result.get("route_contract")
    if classification not in api.SUCCESS_CLASSIFICATIONS:
        raise api.CorpusError(f"final result is not accepted for {map_name}")
    if classification == "PASS" and route_contract != "complete":
        raise api.CorpusError(f"PASS route contract changed for {map_name}")
    if classification == "ROUTE_ONLY" and (
        route_contract != "local_only" or map_name not in api.APPROVED_ROUTE_ONLY_MAPS
    ):
        raise api.CorpusError(f"ROUTE_ONLY is not approved for {map_name}")
    attempt = result.get("attempt")
    if type(attempt) is not int or attempt <= 0:
        raise api.CorpusError(f"final attempt is invalid for {map_name}")
    attempt_result = (
        run_root / "runs" / map_name / f"attempt-{attempt:04d}" / "result.json"
    )
    if result_path.resolve(strict=True) != attempt_result.resolve(strict=True):
        pointer_result, pointer_raw = api._load_json_regular(result_path)
        attempt_value, attempt_raw = api._load_json_regular(attempt_result)
        if pointer_raw != attempt_raw or pointer_result != attempt_value:
            raise api.CorpusError(f"mutable result pointer drifted for {map_name}")
    entry = {
        "map": map_name,
        "stable_port": stable_port,
        "classification": classification,
        "route_contract": route_contract,
        "attempt": attempt,
        "attempt_result": api.regular_file_record(
            attempt_result, require_unaliased=True
        ),
    }
    return entry, result


def _source_results(
    api: Any, snapshot: Path, run_root: Path, identity: Mapping[str, Any]
) -> list[dict[str, Any]]:
    entries = []
    for index, map_name in enumerate(identity["maps"]):
        entry, _result = _accepted_result(
            api,
            snapshot=snapshot,
            run_root=run_root,
            map_name=map_name,
            stable_port=identity["port_base"] + index,
            fingerprint=identity["fingerprint"],
            document_bytes=identity["document_bytes"],
            result_path=run_root / "runs" / map_name / "result.json",
        )
        entries.append(entry)
    return entries


def _authority_results(
    api: Any,
    snapshot: Path,
    run_root: Path,
    identity: Mapping[str, Any],
    expected: Any,
) -> list[dict[str, Any]]:
    if not isinstance(expected, list) or len(expected) != len(identity["maps"]):
        raise api.CorpusError("final corpus result inventory is incomplete")
    entries = []
    for index, map_name in enumerate(identity["maps"]):
        item = expected[index]
        required = {
            "map", "stable_port", "classification", "route_contract", "attempt",
            "attempt_result",
        }
        if not isinstance(item, dict) or set(item) != required:
            raise api.CorpusError("final corpus result entry is invalid")
        attempt = item.get("attempt")
        if type(attempt) is not int or attempt <= 0:
            raise api.CorpusError(f"final attempt is invalid for {map_name}")
        path = run_root / "runs" / map_name / f"attempt-{attempt:04d}" / "result.json"
        result_path = _record_matches(api, item["attempt_result"], path, map_name)
        entry, _result = _accepted_result(
            api,
            snapshot=snapshot,
            run_root=run_root,
            map_name=map_name,
            stable_port=identity["port_base"] + index,
            fingerprint=identity["fingerprint"],
            document_bytes=identity["document_bytes"],
            result_path=result_path,
        )
        if entry != item:
            raise api.CorpusError(f"final corpus result authority drifted for {map_name}")
        entries.append(entry)
    return entries


def _counts(results: Sequence[Mapping[str, Any]]) -> dict[str, int]:
    counts = {"PASS": 0, "ROUTE_ONLY": 0}
    for item in results:
        counts[str(item["classification"])] += 1
    return counts


def _logical_record(api: Any, path: Path, logical: str) -> dict[str, Any]:
    record = api.regular_file_record(path, require_unaliased=True)
    return {"path": logical, "size": record["size"], "sha256": record["sha256"]}


def _summary(api: Any, fingerprint: str, results: Sequence[Mapping[str, Any]]) -> bytes:
    return api.canonical_json({
        "format": FORMAT,
        "fingerprint": fingerprint,
        "total": len(results),
        "counts": _counts(results),
        "route_only_maps": [
            item["map"] for item in results if item["classification"] == "ROUTE_ONLY"
        ],
        "results": list(results),
    })


def _payload(
    api: Any,
    *,
    snapshot: Path,
    run_root: Path,
    identity: Mapping[str, Any],
    results: Sequence[Mapping[str, Any]],
    summary_record: Mapping[str, Any],
) -> dict[str, Any]:
    document = identity["document"]
    roles = identity["roles"]
    return {
        "format": FORMAT,
        "snapshot": str(snapshot),
        "run_root": str(run_root),
        "fingerprint": identity["fingerprint"],
        "fingerprint_document": api.regular_file_record(
            run_root / "fingerprint-document.json", require_unaliased=True
        ),
        "input_manifest": api.regular_file_record(
            snapshot / "input-manifest.json", require_unaliased=True
        ),
        "map_manifest": api.regular_file_record(
            snapshot / roles["map_manifest"]["path"], require_unaliased=True
        ),
        "controller_sha256": document["controller_sha256"],
        "finalizer_sha256": document["finalizer_sha256"],
        "route_only_policy": {
            "version": api.POLICY_VERSION,
            "sha256": document["route_only_policy_sha256"],
            "approved": list(api.APPROVED_ROUTE_ONLY_MAPS),
        },
        "counts": _counts(results),
        "summary": dict(summary_record),
        "results": list(results),
    }


def _fsync_directory(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def _rename_noreplace(source: Path, destination: Path) -> None:
    libc = ctypes.CDLL(None, use_errno=True)
    renameat2 = getattr(libc, "renameat2", None)
    if renameat2 is None:
        raise OSError(errno.ENOSYS, "renameat2 is unavailable")
    renameat2.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int,
                          ctypes.c_char_p, ctypes.c_uint]
    renameat2.restype = ctypes.c_int
    rc = renameat2(-100, os.fsencode(source), -100, os.fsencode(destination),
                   RENAME_NOREPLACE)
    if rc != 0:
        number = ctypes.get_errno()
        raise OSError(number, os.strerror(number), str(destination))


def _thaw_and_remove(stage: Path) -> None:
    if not stage.exists() or stage.is_symlink():
        return
    for directory, names, files in os.walk(stage, topdown=False, followlinks=False):
        current = Path(directory)
        for name in names + files:
            path = current / name
            if path.is_symlink():
                path.unlink()
            else:
                path.chmod(stat.S_IMODE(path.lstat().st_mode) | 0o700)
        current.chmod(stat.S_IMODE(current.lstat().st_mode) | 0o700)
    shutil.rmtree(stage)


def _frozen_tree(api: Any, root: Path) -> None:
    for directory, names, files in os.walk(root, followlinks=False):
        current = Path(directory)
        if current.is_symlink() or stat.S_IMODE(current.lstat().st_mode) & 0o222:
            raise api.CorpusError("final corpus authority tree is mutable")
        for name in names + files:
            path = current / name
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode) or stat.S_IMODE(info.st_mode) & 0o222:
                raise api.CorpusError("final corpus authority tree is mutable")
            if not (stat.S_ISREG(info.st_mode) or stat.S_ISDIR(info.st_mode)):
                raise api.CorpusError("final corpus authority has an unsupported file")


def _seal(api: Any, run_root: Path, corpus_root: Path, corpus_id: str) -> None:
    path = run_root / SEAL_FILENAME
    value = {
        "format": SEAL_FORMAT,
        "corpus_id": corpus_id,
        "corpus_root": str(corpus_root),
    }
    payload = api.canonical_json(value)
    if path.exists() or path.is_symlink():
        stored, raw = _canonical_object(api, path)
        if stored != value or raw != payload:
            raise api.CorpusError("generation run is sealed to another final corpus")
        return
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags, 0o444)
    try:
        offset = 0
        while offset < len(payload):
            written = os.write(fd, payload[offset:])
            if written <= 0:
                raise OSError("short source-seal write")
            offset += written
        os.fsync(fd)
    finally:
        os.close(fd)
    _fsync_directory(run_root)


def verify_final_corpus(
    api: Any, *, snapshot: Path, corpus_root: Path
) -> dict[str, Any]:
    """Reopen a final authority and every immutable accepted attempt."""
    api.reject_symlink_components(snapshot)
    api.reject_symlink_components(corpus_root)
    snapshot = snapshot.resolve(strict=True)
    corpus_root = corpus_root.resolve(strict=True)
    authority, raw = _canonical_object(api, corpus_root / AUTHORITY_FILENAME)
    if set(authority) != {"format", "corpus_id", "payload"}:
        raise api.CorpusError("final corpus authority schema is invalid")
    payload = authority["payload"]
    corpus_id = api.sha256_bytes(api.canonical_json(payload))
    if (
        authority["format"] != FORMAT
        or authority["corpus_id"] != corpus_id
        or corpus_root.name != corpus_id
        or raw != api.canonical_json(authority)
    ):
        raise api.CorpusError("final corpus identity is invalid")
    if not isinstance(payload, dict) or payload.get("snapshot") != str(snapshot):
        raise api.CorpusError("final corpus snapshot authority drifted")
    payload_run_root = Path(str(payload.get("run_root", "")))
    api.reject_symlink_components(payload_run_root)
    run_root = payload_run_root.resolve(strict=True)
    identity = _identity(api, snapshot, run_root)
    api.preflight_python_runtime(snapshot)
    results = _authority_results(
        api, snapshot, run_root, identity, payload.get("results")
    )
    summary_path = corpus_root / "summary.json"
    summary_bytes, _record = api.read_regular_bytes(
        summary_path, require_unaliased=True
    )
    expected_summary = _summary(api, identity["fingerprint"], results)
    if summary_bytes != expected_summary:
        raise api.CorpusError("final corpus summary drifted")
    summary_record = _logical_record(api, summary_path, "summary.json")
    expected_payload = _payload(
        api,
        snapshot=snapshot,
        run_root=run_root,
        identity=identity,
        results=results,
        summary_record=summary_record,
    )
    if payload != expected_payload:
        raise api.CorpusError("final corpus payload drifted")
    seal, _seal_raw = _canonical_object(api, run_root / SEAL_FILENAME)
    if seal != {
        "format": SEAL_FORMAT,
        "corpus_id": corpus_id,
        "corpus_root": str(corpus_root),
    }:
        raise api.CorpusError("final corpus source seal drifted")
    _frozen_tree(api, corpus_root)
    return {
        "corpus_id": corpus_id,
        "authority": api.regular_file_record(
            corpus_root / AUTHORITY_FILENAME, require_unaliased=True
        ),
        "run_root": str(run_root),
        "port_base": identity["port_base"],
        "results": results,
    }


def finalize_corpus(
    api: Any, *, snapshot: Path, run_root: Path, output_parent: Path
) -> dict[str, Any]:
    """Seal a complete generation run and publish its immutable authority."""
    api.reject_symlink_components(snapshot)
    api.reject_symlink_components(run_root)
    snapshot = snapshot.resolve(strict=True)
    run_root = run_root.resolve(strict=True)
    api.reject_symlink_components(output_parent)
    output_parent = Path(os.path.abspath(output_parent))
    if output_parent == run_root or run_root in output_parent.parents:
        raise api.CorpusError("final corpus archive cannot be inside its source run")
    output_parent.mkdir(parents=True, mode=0o700, exist_ok=True)
    output_parent = output_parent.resolve(strict=True)
    identity = _identity(api, snapshot, run_root)
    with api.ControllerLock(
        run_root, identity["fingerprint"], publish_owner=False
    ):
        seal_path = run_root / SEAL_FILENAME
        if seal_path.exists() or seal_path.is_symlink():
            seal, _raw = _canonical_object(api, seal_path)
            if (
                set(seal) != {"format", "corpus_id", "corpus_root"}
                or seal.get("format") != SEAL_FORMAT
            ):
                raise api.CorpusError("generation run source seal is invalid")
            sealed_root = Path(str(seal["corpus_root"]))
            if sealed_root.parent != output_parent:
                raise api.CorpusError("generation run is sealed to another archive")
            if sealed_root.exists() or sealed_root.is_symlink():
                return verify_final_corpus(
                    api, snapshot=snapshot, corpus_root=sealed_root
                )
        api.preflight_python_runtime(snapshot)
        results = _source_results(api, snapshot, run_root, identity)
        stage = Path(tempfile.mkdtemp(prefix=".rune-final-", dir=output_parent))
        try:
            summary_path = stage / "summary.json"
            api.atomic_write_bytes(
                summary_path, _summary(api, identity["fingerprint"], results), mode=0o444
            )
            payload = _payload(
                api,
                snapshot=snapshot,
                run_root=run_root,
                identity=identity,
                results=results,
                summary_record=_logical_record(api, summary_path, "summary.json"),
            )
            corpus_id = api.sha256_bytes(api.canonical_json(payload))
            destination = output_parent / corpus_id
            authority = {"format": FORMAT, "corpus_id": corpus_id, "payload": payload}
            api.atomic_write_json(
                stage / AUTHORITY_FILENAME, authority, mode=0o444
            )
            api.freeze_tree(stage)
            api.fsync_tree(stage)
            _seal(api, run_root, destination, corpus_id)
            try:
                _rename_noreplace(stage, destination)
            except FileExistsError:
                _thaw_and_remove(stage)
            _fsync_directory(output_parent)
            return verify_final_corpus(
                api, snapshot=snapshot, corpus_root=destination
            )
        except BaseException:
            _thaw_and_remove(stage)
            raise


__all__ = [
    "AUTHORITY_FILENAME",
    "FORMAT",
    "SEAL_FILENAME",
    "finalize_corpus",
    "verify_final_corpus",
]
