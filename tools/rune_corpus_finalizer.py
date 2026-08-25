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
from typing import Any, Callable, Mapping, Sequence


FORMAT = "lmctf-rune-final-corpus-v2"
SEAL_FORMAT = "lmctf-rune-final-source-seal-v1"
AUTHORITY_FILENAME = "corpus-authority.json"
SEAL_FILENAME = "final-corpus-seal.json"
RENAME_NOREPLACE = 1
FINAL_CORPUS_BINDING_FIELDS = frozenset({
    "controller", "finalizer", "snapshot", "run_root", "corpus_authority",
    "corpus_id", "results",
})


def _canonical_object(api: Any, path: Path) -> tuple[dict[str, Any], bytes]:
    value, raw = api._load_json_regular(path, require_unaliased=True)
    if not isinstance(value, dict) or raw != api.canonical_json(value):
        raise api.CorpusError(f"noncanonical JSON authority: {path}")
    return value, raw


def _identity(api: Any, snapshot: Path, run_root: Path) -> dict[str, Any]:
    api.reject_symlink_components(snapshot)
    api.reject_symlink_components(run_root)
    verified = api.verify_snapshot(snapshot)
    expected_adopted = api.EXPECTED_ADOPTED_RUNE_COUNT
    if len(verified.get("adopted_runes", {})) != expected_adopted:
        raise api.CorpusError(
            f"final corpus requires exactly {expected_adopted} adopted RUNEs"
        )
    runs = run_root / "runs"
    if runs.exists() and any(runs.glob("*/.attempt-*-*")):
        raise api.CorpusError("final corpus has an unresolved hidden intent attempt")
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


def _validate_history(
    api: Any, *, run_root: Path, map_name: str, fingerprint: str,
    stable_port: int, result: Mapping[str, Any],
    adopted_runes: Mapping[str, Mapping[str, Any]] | None = None,
) -> list[dict[str, Any]]:
    """Authenticate immutable attempt ordering and one-shot replacement use."""
    if not hasattr(api, "load_map_history"):
        return []
    history = api.load_map_history(
        run_root, map_name, fingerprint=fingerprint, stable_port=stable_port,
    )
    if not history or history[-1].get("result") != result:
        raise api.CorpusError(f"final history pointer is not terminal for {map_name}")
    if any(
        not isinstance(item.get("result"), dict) and not item.get("aborted")
        for item in history
    ):
        raise api.CorpusError(f"final history has an unfinished attempt for {map_name}")
    terminal_history = [item for item in history if not item.get("aborted")]
    accepted = [
        item for item in terminal_history
        if isinstance(item.get("result"), dict)
        and item["result"].get("classification") in api.SUCCESS_CLASSIFICATIONS
    ]
    if len(accepted) != 1 or accepted[0] is not history[-1]:
        raise api.CorpusError(f"final history has post-accept work for {map_name}")
    candidate = (adopted_runes or {}).get(map_name)
    for item in history if adopted_runes is not None else ():
        intent = item["intent"]
        kind = intent["kind"]
        if candidate is None:
            if kind != "generated_missing":
                raise api.CorpusError(f"missing map has an adopted attempt for {map_name}")
        else:
            if kind not in {"adopted_validation", "generated_replacement"}:
                raise api.CorpusError(f"adopted map has missing-generation history for {map_name}")
            expected_source = {
                key: candidate[key] for key in ("path", "mode", "size", "sha256", "role")
            }
            if intent["source_artifact"] != expected_source:
                raise api.CorpusError(f"adopted history source changed for {map_name}")
            result_source = item.get("result", {}).get("provenance", {}).get("source_artifact")
            if item.get("result") is not None and result_source != expected_source:
                raise api.CorpusError(f"adopted result source changed for {map_name}")
    replacements = [item for item in history if item["intent"]["kind"] == "generated_replacement"]
    if len(replacements) > 1:
        raise api.CorpusError(f"final history repeats replacement for {map_name}")
    if result.get("attempt_kind") == "generated_replacement":
        if len(replacements) != 1:
            raise api.CorpusError(f"final replacement history is incomplete for {map_name}")
        rejection = result["provenance"]["rejection_result"]
        rejected = [
            item for item in terminal_history
            if item["intent"]["kind"] == "adopted_validation"
            and isinstance(item.get("result"), dict)
            and item["result"].get("disposition") == "artifact_rejected"
            and str((item["path"] / "result.json").relative_to(run_root)) == rejection
        ]
        if len(rejected) != 1:
            raise api.CorpusError(f"replacement lacks its one rejected adoption for {map_name}")
    if adopted_runes is not None and candidate is not None:
        kinds = [item["intent"]["kind"] for item in terminal_history]
        dispositions = [item["result"].get("disposition") for item in terminal_history]
        if result.get("attempt_kind") == "adopted_validation":
            if any(value != "adopted_validation" for value in kinds) or any(
                value != "infra_failed" for value in dispositions[:-1]
            ):
                raise api.CorpusError(
                    f"adopted final history has an impossible transition for {map_name}"
                )
        else:
            replacement_index = kinds.index("generated_replacement")
            if (
                replacement_index != len(terminal_history) - 1
                or kinds[:replacement_index].count("adopted_validation") != replacement_index
                or dispositions[:replacement_index] != [
                    *(["infra_failed"] * (replacement_index - 1)),
                    "artifact_rejected",
                ]
            ):
                raise api.CorpusError(
                    f"replacement history has an impossible transition for {map_name}"
                )
    if adopted_runes is not None and candidate is None:
        dispositions = [item["result"].get("disposition") for item in terminal_history]
        if any(value != "infra_failed" for value in dispositions[:-1]) or (
            dispositions and dispositions[-1] != "accepted"
        ):
            raise api.CorpusError(f"missing history has an impossible transition for {map_name}")
    records: list[dict[str, Any]] = []
    for item in history:
        attempt_path = item["path"]
        record = {
            "attempt": item["attempt"],
            "intent": api.regular_file_record(
                attempt_path / "intent.json", require_unaliased=True
            ),
        }
        if item.get("aborted"):
            abort = api._attempt_abort_path(run_root, map_name, item["attempt"])
            record["abort"] = api.regular_file_record(abort, require_unaliased=True)
        else:
            record["result"] = api.regular_file_record(
                attempt_path / "result.json", require_unaliased=True
            )
        records.append(record)
    return records


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
    adopted_runes: Mapping[str, Mapping[str, Any]],
    heartbeat_check: Callable[[], None] | None = None,
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
        heartbeat_check=heartbeat_check,
    )
    if validated is None:
        raise api.CorpusError(f"final result is invalid for {map_name}")
    result, _raw = validated
    history_records = _validate_history(
        api, run_root=run_root, map_name=map_name, fingerprint=fingerprint,
        stable_port=stable_port, result=result, adopted_runes=adopted_runes,
    )
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
    kind = result.get("attempt_kind")
    provenance = result.get("provenance")
    if kind not in {"adopted_validation", "generated_missing", "generated_replacement"}:
        raise api.CorpusError(f"final attempt kind is invalid for {map_name}")
    if not isinstance(provenance, dict) or set(provenance) != {
        "source_artifact", "rejection_result"
    }:
        raise api.CorpusError(f"final attempt provenance is invalid for {map_name}")
    source = provenance["source_artifact"]
    if (kind == "generated_missing") != (map_name not in adopted_runes):
        raise api.CorpusError(f"attempt kind does not match adopted inventory for {map_name}")
    if kind == "generated_missing":
        if source is not None or provenance["rejection_result"] is not None:
            raise api.CorpusError(f"missing generation has provenance for {map_name}")
    else:
        expected = adopted_runes.get(map_name)
        if not isinstance(expected, dict) or source != {
            key: expected[key] for key in ("path", "mode", "size", "sha256", "role")
        }:
            raise api.CorpusError(f"adopted provenance changed for {map_name}")
        if kind == "adopted_validation" and provenance["rejection_result"] is not None:
            raise api.CorpusError(f"adopted validation has a rejection source for {map_name}")
        if kind == "generated_replacement" and not isinstance(provenance["rejection_result"], str):
            raise api.CorpusError(f"replacement has no rejected adoption for {map_name}")
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
        "attempt_kind": kind,
        "provenance": provenance,
        "history": history_records,
        "attempt_result": api.regular_file_record(
            attempt_result, require_unaliased=True
        ),
    }
    return entry, result


def _source_results(
    api: Any, snapshot: Path, run_root: Path, identity: Mapping[str, Any],
    heartbeat: Any = None,
) -> list[dict[str, Any]]:
    entries = []
    for index, map_name in enumerate(identity["maps"]):
        if heartbeat is not None:
            heartbeat.event("active", map_name, {"stage": "final_replay"})
        try:
            entry, _result = _accepted_result(
                api,
                snapshot=snapshot,
                run_root=run_root,
                map_name=map_name,
                stable_port=identity["port_base"] + index,
                fingerprint=identity["fingerprint"],
                document_bytes=identity["document_bytes"],
                result_path=run_root / "runs" / map_name / "result.json",
                adopted_runes=identity["snapshot_verified"].get("adopted_runes", {}),
                heartbeat_check=(
                    lambda map_name=map_name: heartbeat.event("beat", map_name)
                ) if heartbeat is not None else None,
            )
        except BaseException:
            if heartbeat is not None:
                heartbeat.event("inactive", map_name)
            raise
        if heartbeat is not None:
            heartbeat.event("terminal", map_name)
        entries.append(entry)
    return entries


def _authority_results(
    api: Any,
    snapshot: Path,
    run_root: Path,
    identity: Mapping[str, Any],
    expected: Any, heartbeat: Any = None,
) -> list[dict[str, Any]]:
    if not isinstance(expected, list) or len(expected) != len(identity["maps"]):
        raise api.CorpusError("final corpus result inventory is incomplete")
    entries = []
    for index, map_name in enumerate(identity["maps"]):
        item = expected[index]
        required = {
            "map", "stable_port", "classification", "route_contract", "attempt",
            "attempt_kind", "provenance", "history", "attempt_result",
        }
        if not isinstance(item, dict) or set(item) != required:
            raise api.CorpusError("final corpus result entry is invalid")
        attempt = item.get("attempt")
        if type(attempt) is not int or attempt <= 0:
            raise api.CorpusError(f"final attempt is invalid for {map_name}")
        path = run_root / "runs" / map_name / f"attempt-{attempt:04d}" / "result.json"
        result_path = _record_matches(api, item["attempt_result"], path, map_name)
        if heartbeat is not None:
            heartbeat.event("active", map_name, {"stage": "final_replay"})
        try:
            entry, _result = _accepted_result(
                api,
                snapshot=snapshot,
                run_root=run_root,
                map_name=map_name,
                stable_port=identity["port_base"] + index,
                fingerprint=identity["fingerprint"],
                document_bytes=identity["document_bytes"],
                result_path=result_path,
                adopted_runes=identity["snapshot_verified"].get("adopted_runes", {}),
                heartbeat_check=(
                    lambda map_name=map_name: heartbeat.event("beat", map_name)
                ) if heartbeat is not None else None,
            )
        except BaseException:
            if heartbeat is not None:
                heartbeat.event("inactive", map_name)
            raise
        if entry != item:
            raise api.CorpusError(f"final corpus result authority drifted for {map_name}")
        if heartbeat is not None:
            heartbeat.event("terminal", map_name)
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


def _start_review_heartbeat(api: Any, run_root: Path, identity: Mapping[str, Any]) -> Any:
    """Publish liveness during unbounded final gate replay when available."""
    publisher_type = getattr(api, "HeartbeatPublisher", None)
    if publisher_type is None:
        return None
    publisher = publisher_type(run_root, identity["fingerprint"], len(identity["maps"]))
    try:
        publisher.event("active", "__corpus_review__", {"stage": "final_review"})
    except BaseException:
        publisher.close()
        raise
    return publisher


def _finish_review_heartbeat(
    publisher: Any, maps: Sequence[str], complete: bool
) -> None:
    if publisher is not None:
        try:
            publisher.seed_terminals(maps if complete else ())
            publisher.finish(len(maps) if complete else 0, complete)
        finally:
            publisher.close()


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
    api: Any, *, snapshot: Path, corpus_root: Path, _heartbeat: Any = None
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
    owns_heartbeat = _heartbeat is None
    heartbeat = _heartbeat if _heartbeat is not None else _start_review_heartbeat(
        api, run_root, identity
    )
    try:
        api.preflight_python_runtime(
            snapshot,
            heartbeat_check=(
                lambda: heartbeat.event("beat", "__corpus_review__")
            ) if heartbeat is not None else None,
        )
        results = _authority_results(
            api, snapshot, run_root, identity, payload.get("results"), heartbeat
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
        result = {
            "corpus_id": corpus_id,
            "authority": api.regular_file_record(
                corpus_root / AUTHORITY_FILENAME, require_unaliased=True
            ),
            "run_root": str(run_root),
            "port_base": identity["port_base"],
            "results": results,
        }
    except BaseException:
        if owns_heartbeat:
            try:
                _finish_review_heartbeat(heartbeat, identity["maps"], False)
            except BaseException:
                pass
        raise
    if owns_heartbeat:
        _finish_review_heartbeat(heartbeat, identity["maps"], True)
    return result


def _binding_path(api: Any, value: Any, label: str) -> Path:
    if not isinstance(value, str) or not Path(value).is_absolute():
        raise api.CorpusError(f"final corpus binding {label} is invalid")
    return Path(value)


def _binding_role_record(
    api: Any, roles: Mapping[str, Mapping[str, Any]], role: str,
) -> Mapping[str, Any]:
    record = roles.get(role)
    if not isinstance(record, Mapping):
        raise api.CorpusError(f"final corpus binding lacks bundle role {role}")
    if (
        not isinstance(record.get("sha256"), str)
        or type(record.get("size")) is not int
        or record["size"] < 0
    ):
        raise api.CorpusError(f"final corpus binding has invalid bundle role {role}")
    return record


def _binding_matches_record(
    api: Any,
    *,
    record: Mapping[str, Any],
    expected: Mapping[str, Any],
    label: str,
) -> None:
    if (
        record.get("sha256") != expected.get("sha256")
        or record.get("size") != expected.get("size")
    ):
        raise api.CorpusError(f"final corpus binding {label} differs from bundle")


def build_verified_final_corpus_binding(
    api: Any,
    *,
    snapshot: Path,
    corpus_root: Path,
    controller_record: Mapping[str, Any],
    finalizer_record: Mapping[str, Any],
) -> dict[str, Any]:
    """Return the one reverified final-corpus reference embedded downstream."""
    if not isinstance(controller_record, Mapping) or not isinstance(finalizer_record, Mapping):
        raise api.CorpusError("final corpus binding verifier records are invalid")
    snapshot = Path(snapshot).resolve(strict=True)
    corpus_root = Path(corpus_root).resolve(strict=True)
    verified = verify_final_corpus(api, snapshot=snapshot, corpus_root=corpus_root)
    required = {"corpus_id", "authority", "run_root", "port_base", "results"}
    if not isinstance(verified, Mapping) or set(verified) != required:
        raise api.CorpusError("final corpus binding verifier result is invalid")
    return {
        "controller": dict(controller_record),
        "finalizer": dict(finalizer_record),
        "snapshot": str(snapshot),
        "run_root": verified["run_root"],
        "corpus_authority": verified["authority"],
        "corpus_id": verified["corpus_id"],
        "results": verified["results"],
    }


def validate_bundle_final_corpus_binding(
    api: Any,
    *,
    binding: Mapping[str, Any],
    controller_record: Mapping[str, Any],
    finalizer_record: Mapping[str, Any],
    bundle_roles: Mapping[str, Mapping[str, Any]],
    engine_record: Mapping[str, Any] | None = None,
) -> dict[str, dict[str, Any]]:
    """Bind every bundled RUNE, BSP, module, and present SNAG to one corpus."""
    if not isinstance(binding, Mapping) or set(binding) != FINAL_CORPUS_BINDING_FIELDS:
        raise api.CorpusError("final corpus binding is incomplete")
    if not isinstance(bundle_roles, Mapping):
        raise api.CorpusError("final corpus binding role inventory is invalid")
    snapshot = _binding_path(api, binding.get("snapshot"), "snapshot")
    authority = binding.get("corpus_authority")
    if not isinstance(authority, Mapping) or not isinstance(authority.get("path"), str):
        raise api.CorpusError("final corpus binding authority is invalid")
    corpus_root = Path(authority["path"]).parent
    rebuilt = build_verified_final_corpus_binding(
        api,
        snapshot=snapshot,
        corpus_root=corpus_root,
        controller_record=controller_record,
        finalizer_record=finalizer_record,
    )
    if rebuilt != dict(binding):
        raise api.CorpusError("final corpus binding drifted")
    try:
        run_root = _binding_path(api, rebuilt["run_root"], "run root").resolve(strict=True)
        roles = api.verify_snapshot(snapshot).get("by_role")
    except (AttributeError, OSError) as exc:
        raise api.CorpusError(f"final corpus binding snapshot is invalid: {exc}") from exc
    if not isinstance(roles, Mapping):
        raise api.CorpusError("final corpus binding snapshot roles are invalid")
    manifest = roles.get("map_manifest")
    if not isinstance(manifest, Mapping) or not isinstance(manifest.get("path"), str):
        raise api.CorpusError("final corpus binding map manifest is invalid")
    maps = tuple(api.validate_manifest(snapshot / manifest["path"]))
    results = rebuilt["results"]
    if not isinstance(results, list) or len(results) != len(maps):
        raise api.CorpusError("final corpus binding result inventory is incomplete")
    for bundle_role, snapshot_role in (
        ("module-primary", "module_primary"),
        ("module-secondary", "module_secondary"),
    ):
        source = roles.get(snapshot_role)
        if not isinstance(source, Mapping):
            raise api.CorpusError(f"final corpus binding lacks snapshot role {snapshot_role}")
        _binding_matches_record(
            api,
            record=source,
            expected=_binding_role_record(api, bundle_roles, bundle_role),
            label=bundle_role,
        )
    if engine_record is not None:
        source = roles.get("engine")
        if not isinstance(source, Mapping):
            raise api.CorpusError("final corpus binding lacks snapshot engine")
        _binding_matches_record(
            api,
            record=source,
            expected=engine_record,
            label="engine",
        )
    checked: dict[str, dict[str, Any]] = {}
    for index, map_name in enumerate(maps):
        item = results[index]
        if (
            not isinstance(item, Mapping)
            or item.get("map") != map_name
            or type(item.get("attempt")) is not int
            or item["attempt"] < 1
            or not isinstance(item.get("attempt_result"), Mapping)
            or not isinstance(item["attempt_result"].get("path"), str)
        ):
            raise api.CorpusError("final corpus binding result record is invalid")
        expected_result = (
            run_root / "runs" / map_name / f"attempt-{item['attempt']:04d}" / "result.json"
        )
        result_path = Path(item["attempt_result"]["path"])
        if result_path != expected_result:
            raise api.CorpusError(f"final corpus binding result path drifted for {map_name}")
        result, _raw = api._load_json_regular(result_path)
        if (
            not isinstance(result, Mapping)
            or result.get("map") != map_name
            or result.get("attempt") != item["attempt"]
            or result.get("artifact_sha256") != _binding_role_record(
                api, bundle_roles, f"rune:{map_name}"
            ).get("sha256")
        ):
            raise api.CorpusError(f"final corpus binding RUNE differs for {map_name}")
        artifact = result.get("artifact")
        if isinstance(artifact, Mapping):
            _binding_matches_record(
                api,
                record=artifact,
                expected=_binding_role_record(api, bundle_roles, f"rune:{map_name}"),
                label=f"rune:{map_name}",
            )
        asset = roles.get(f"asset:{map_name}")
        if not isinstance(asset, Mapping):
            raise api.CorpusError(f"final corpus binding lacks snapshot asset {map_name}")
        _binding_matches_record(
            api,
            record=asset,
            expected=_binding_role_record(api, bundle_roles, f"bsp:{map_name}"),
            label=f"bsp:{map_name}",
        )
        snag_role = bundle_roles.get(f"snag:{map_name}")
        if snag_role is not None:
            snag_relative = result.get("cold_load_snag_record")
            expected_snag = (
                run_root / "runs" / map_name / f"attempt-{item['attempt']:04d}"
                / "cold-load" / "private" / "game" / "maps" / f"{map_name}.snag"
            )
            if (
                not isinstance(snag_relative, str)
                or run_root / snag_relative != expected_snag
            ):
                raise api.CorpusError(f"final corpus binding SNAG record drifted for {map_name}")
            _binding_matches_record(
                api,
                record=api.regular_file_record(expected_snag, require_unaliased=True),
                expected=_binding_role_record(api, bundle_roles, f"snag:{map_name}"),
                label=f"snag:{map_name}",
            )
        checked[map_name] = dict(item)
    return checked


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
        heartbeat = _start_review_heartbeat(api, run_root, identity)
        stage: Path | None = None
        try:
            api.preflight_python_runtime(
                snapshot,
                heartbeat_check=lambda: heartbeat.event("beat", "__corpus_review__"),
            )
            results = _source_results(api, snapshot, run_root, identity, heartbeat)
            stage = Path(tempfile.mkdtemp(prefix=".rune-final-", dir=output_parent))
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
            published = verify_final_corpus(
                api, snapshot=snapshot, corpus_root=destination, _heartbeat=heartbeat
            )
            _finish_review_heartbeat(heartbeat, identity["maps"], True)
            return published
        except BaseException:
            if stage is not None and stage.exists():
                _thaw_and_remove(stage)
            try:
                _finish_review_heartbeat(heartbeat, identity["maps"], False)
            except BaseException:
                pass
            raise


__all__ = [
    "AUTHORITY_FILENAME",
    "FINAL_CORPUS_BINDING_FIELDS",
    "FORMAT",
    "SEAL_FILENAME",
    "build_verified_final_corpus_binding",
    "finalize_corpus",
    "validate_bundle_final_corpus_binding",
    "verify_final_corpus",
]
