#!/usr/bin/env python3
"""Build and verify the complete 181-map `.snag` sidecar corpus.

The bootstrap state does not claim that a map is clean.  It says that no
authenticated residence has yet supplied controller-seed stall evidence, and
emits an explicit RUNE-bound ``repairs 0`` file so cold loading never relies on
missing-input fallback.  The final pass consumes only the persistent fleet's
fully verified stopped residence authorities.  It analyzes exactly the first
twenty residences from every lane, retains residence 21 as the native wrap
proof, joins visible demo stalls to exact controller/RUNE episodes, and emits
either attributed repairs or an explicit no-accepted-observation sidecar for
every map.
"""

from __future__ import annotations

import argparse
import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import secrets
import stat
import sys
import tempfile
import types

import rune_corpus_controller
import runeio
import snagrepair


FORMAT = "lmctf-snag-corpus-v1"
EVIDENCE_FORMAT = "lmctf-snag-evidence-v1"
FINAL_FORMAT = "lmctf-snag-corpus-v2"
FINAL_EVIDENCE_FORMAT = "lmctf-snag-evidence-v2"
FINAL_CLASSIFICATION = "FINAL_AUTHENTICATED_RESIDENCE_ATTRIBUTION"
MAX_RUNE_BYTES = 512 * 1024 * 1024
MAX_FLEET_RUNNER_BYTES = 16 * 1024 * 1024
MAX_RESIDENCE_ARTIFACT_BYTES = 1024 * 1024 * 1024


def canonical_json(value) -> bytes:
    try:
        encoded = json.dumps(
            value, sort_keys=True, separators=(",", ":"), allow_nan=False
        )
        return (encoded + "\n").encode("ascii")
    except (TypeError, ValueError, UnicodeEncodeError) as exc:
        raise ValueError("snag corpus JSON is not finite canonical ASCII") from exc


def _read_regular(path: Path, maximum: int = MAX_RUNE_BYTES):
    """Read one stable named regular file without following a final symlink."""
    flags = os.O_RDONLY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(path, flags)
    except OSError as exc:
        raise ValueError(f"cannot open {path}: {exc}") from exc
    try:
        before = os.fstat(fd)
        if (not stat.S_ISREG(before.st_mode) or before.st_size <= 0 or
                before.st_size > maximum or before.st_nlink != 1):
            raise ValueError(
                f"{path} is not a bounded unaliased nonempty regular file")
        payload = bytearray()
        while len(payload) < before.st_size:
            block = os.read(fd, min(1024 * 1024, before.st_size - len(payload)))
            if not block:
                raise ValueError(f"{path} was truncated during read")
            payload.extend(block)
        after = os.fstat(fd)
        named = path.stat(follow_symlinks=False)
        before_identity = (
            before.st_dev, before.st_ino, before.st_size,
            before.st_mtime_ns, before.st_ctime_ns,
        )
        after_identity = (
            after.st_dev, after.st_ino, after.st_size,
            after.st_mtime_ns, after.st_ctime_ns,
        )
        named_identity = (
            named.st_dev, named.st_ino, named.st_size,
            named.st_mtime_ns, named.st_ctime_ns,
        )
        if (not stat.S_ISREG(named.st_mode) or after.st_nlink != 1 or
                named.st_nlink != 1 or before_identity != after_identity or
                after_identity != named_identity):
            raise ValueError(f"{path} changed while reading")
        data = bytes(payload)
        return data, {
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
    finally:
        os.close(fd)


def read_rune_regular(path: Path):
    payload, record = _read_regular(path)
    return runeio.decode_rune(payload), record


def _write_bytes(path: Path, payload: bytes):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        written = 0
        while written < len(payload):
            count = os.write(fd, payload[written:])
            if count <= 0:
                raise OSError("short corpus write")
            written += count
        os.fsync(fd)
    finally:
        os.close(fd)


def _fsync_directory(path: Path):
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def _identity(rune):
    identity = rune.header.identity
    return {
        "airaccelerate": identity.airaccelerate,
        "bsp_checksum": identity.bsp_checksum,
        "entity_crc": identity.entity_crc32,
        "frame_ms": identity.server_frame_ms,
        "gravity": identity.gravity,
        "host_physics_id": identity.host_physics_id,
        "maxvelocity": identity.maxvelocity,
        "physics_flags": identity.physics_flags,
        "pmove_ms": identity.pmove_substep_ms,
    }


def _load_fleet_verifier(path: Path):
    """Execute exactly the stable runner bytes whose digest receipts bind."""
    payload, record = _read_regular(path, maximum=MAX_FLEET_RUNNER_BYTES)
    module_name = f"_lmctf_fleet_runner_{record['sha256']}"
    module = types.ModuleType(module_name)
    module.__file__ = str(path.absolute())
    module.__package__ = ""
    sys.modules[module_name] = module
    try:
        exec(compile(payload, str(path), "exec"), module.__dict__)
    except BaseException:
        sys.modules.pop(module_name, None)
        raise
    verifier = getattr(module, "verify_stopped_residence_evidence", None)
    if not callable(verifier):
        raise ValueError(
            "fleet runner lacks verify_stopped_residence_evidence")
    return module, record


def _stable_record(path: Path, maximum: int = MAX_RESIDENCE_ARTIFACT_BYTES):
    payload, record = _read_regular(path, maximum=maximum)
    return payload, record


def _receipt_relative_path(receipt_path: Path, evidence_root: Path) -> str:
    try:
        relative = receipt_path.absolute().relative_to(evidence_root.absolute())
    except ValueError as exc:
        raise ValueError("fleet verifier returned an outside receipt path") from exc
    if not relative.parts or any(part in {"", ".", ".."}
                                 for part in relative.parts):
        raise ValueError("fleet verifier returned an unsafe receipt path")
    return relative.as_posix()


def _topmaps_authority():
    path = Path(__file__).with_name("topmaps.txt")
    payload, record = _read_regular(path, maximum=1024 * 1024)
    try:
        lines = payload.decode("ascii").splitlines()
    except UnicodeDecodeError as exc:
        raise ValueError("topmaps authority is not ASCII") from exc
    maps = tuple(line for line in lines if line and not line.startswith("#"))
    authoritative = set(rune_corpus_controller.validate_manifest())
    if (len(maps) != 20 or len(set(maps)) != 20 or
            any(name != name.strip() or name.lower() != name or
                name not in authoritative
                for name in maps)):
        raise ValueError("topmaps authority is not an exact 20-map subset")
    return maps, record


def _cycle_receipts(module, receipts, evidence_root: Path,
                    runner_sha256: str):
    """Select one unbiased 20-residence cycle and its native wrap proof."""
    lanes = tuple(getattr(module, "LANES", ()))
    topmaps, topmaps_record = _topmaps_authority()
    module_topmaps = tuple(getattr(module, "CANONICAL_TOPMAPS", ()))
    offsets = tuple(getattr(module, "OFFSETS", ()))
    if (lanes != tuple(f"s{index:02d}" for index in range(1, 11)) or
            module_topmaps != topmaps or
            getattr(module, "CANONICAL_TOPMAPS_SHA256", None) !=
                topmaps_record["sha256"] or
            offsets != tuple(range(10))):
        raise ValueError("fleet verifier has a noncanonical lane/map schedule")
    if (not isinstance(receipts, tuple) or
            any(not isinstance(item, tuple) or len(item) != 2
                for item in receipts)):
        raise ValueError("fleet verifier returned an invalid receipt inventory")

    by_lane = {lane: {} for lane in lanes}
    receipt_records = {}
    for path_value, receipt in receipts:
        path = Path(path_value)
        if not isinstance(receipt, dict):
            raise ValueError("fleet verifier returned a non-object receipt")
        lane = receipt.get("lane")
        sequence = receipt.get("sequence")
        if lane not in by_lane or type(sequence) is not int or sequence < 0:
            raise ValueError("fleet verifier returned an invalid lane sequence")
        if sequence in by_lane[lane]:
            raise ValueError("fleet verifier returned a duplicate lane sequence")
        if receipt.get("runner_sha256") != runner_sha256:
            raise ValueError("residence runner identity differs from verifier bytes")
        if receipt.get("topmaps_sha256") != topmaps_record["sha256"]:
            raise ValueError("residence topmaps identity differs from authority")
        relative = _receipt_relative_path(path, evidence_root)
        payload, record = _stable_record(path, maximum=64 * 1024 * 1024)
        if (record["sha256"] in receipt_records and
                receipt_records[record["sha256"]][0] != payload):
            raise ValueError("receipt SHA-256 collision")
        receipt_records[record["sha256"]] = (payload, record)
        by_lane[lane][sequence] = (path, receipt, relative, record)

    selected = []
    wraps = []
    for lane_index, lane in enumerate(lanes):
        inventory = by_lane[lane]
        if any(sequence not in inventory for sequence in range(21)):
            raise ValueError(
                f"fleet evidence lacks a complete cycle and wrap for {lane}")
        for sequence in range(21):
            path, receipt, relative, record = inventory[sequence]
            expected_map = topmaps[(lane_index + sequence) % len(topmaps)]
            if receipt.get("map") != expected_map:
                raise ValueError(
                    f"fleet receipt schedule disagrees for {lane} sequence {sequence}")
            item = (path, receipt, relative, record)
            if sequence < 20:
                selected.append(item)
            else:
                wraps.append({
                    "lane": lane,
                    "map": expected_map,
                    "receipt_hash": receipt.get("receipt_hash"),
                    "receipt_path": relative,
                    "receipt_sha256": record["sha256"],
                    "sequence": sequence,
                })
    return selected, wraps, receipt_records, topmaps, topmaps_record


def _public_analysis(row):
    if not isinstance(row, dict):
        raise ValueError("stall analyzer returned a non-object")
    return {key: value for key, value in row.items()
            if not str(key).startswith("_")}


def _analyze_residence(receipt_path: Path, receipt, rune, rune_record):
    """Analyze exact hash-bound copies of one canonical residence's inputs."""
    import stallcensus

    map_name = receipt["map"]
    if (receipt.get("rune_sha256") != rune_record["sha256"] or
            receipt.get("rune_file", {}).get("sha256") !=
            rune_record["sha256"]):
        raise ValueError(f"residence RUNE differs from final {map_name} artifact")

    demo_path = Path(receipt["serverrecord"]["demo_path"])
    demo_payload, demo_record = _stable_record(demo_path)
    if (demo_record["sha256"] != receipt["serverrecord"]["demo_sha256"] or
            demo_record["size"] != receipt["serverrecord"]["demo_size"]):
        raise ValueError(f"residence demo identity drift for {map_name}")
    segment_path = (
        receipt_path.parent / "segments" /
        receipt["console_segment"]["path"]
    )
    segment_payload, segment_record = _stable_record(
        segment_path, maximum=64 * 1024 * 1024)
    if (segment_record["sha256"] != receipt["console_segment"]["sha256"] or
            segment_record["size"] != receipt["console_segment"]["size"]):
        raise ValueError(f"residence console identity drift for {map_name}")
    try:
        report_lines = segment_payload.decode("utf-8").splitlines(keepends=True)
    except UnicodeDecodeError as exc:
        raise ValueError("residence console segment is not UTF-8") from exc

    team_names = {1: "red", 2: "blue"}
    players = {}
    player_records = []
    for player in receipt["sg_players"]:
        team = team_names.get(player["team"])
        if team is None:
            raise ValueError("residence contains an invalid CTF team")
        name = player["name"]
        players[name] = {"team": team, "entity": player["client"]}
        player_records.append({
            "client": player["client"],
            "instance": player["instance"],
            "name": name,
            "slot": player["slot"],
            "team": player["team"],
        })
    if len(players) != len(receipt["sg_players"]):
        raise ValueError("residence contains duplicate SG names")

    demo_range = receipt["serverrecord"]["demo_frame_range"]
    residence = receipt["residence"]
    stands = {
        map_name: {
            "red": residence["red_flag_origin"],
            "blue": residence["blue_flag_origin"],
        }
    }
    identities = {map_name: _identity(rune)}
    with tempfile.TemporaryDirectory(prefix=".snag-residence-") as temporary:
        stable_demo = Path(temporary) / f"{map_name}.dm2"
        _write_bytes(stable_demo, demo_payload)
        row = stallcensus.analyze(
            stable_demo, stands, identities,
            expected_map=map_name,
            expected_players=players,
            frame_range=(demo_range["start"], demo_range["end_exclusive"]),
            require_svrecord=True,
            cap_s=None,
            sg_report_lines=report_lines,
            server_frame_range=(
                residence["start_frame"], residence["end_frame"]),
            rune=rune,
        )
    joined = snagrepair.correlate_stall_evidence(row)
    row.update(joined)
    return {
        "analysis": _public_analysis(row),
        "demo_sha256": demo_record["sha256"],
        "players": sorted(player_records, key=lambda item: item["name"]),
        "segment_sha256": segment_record["sha256"],
    }


def _validate_authority(maps, manifest_sha256):
    if manifest_sha256 != rune_corpus_controller.EXPECTED_MANIFEST_SHA256:
        raise ValueError("bootstrap map-manifest hash is not authoritative")
    authoritative_maps = rune_corpus_controller.validate_manifest()
    if maps != authoritative_maps:
        raise ValueError("bootstrap map list is not the exact ordered 181 authority")


def _derive_final_corpus(
        maps, rune_dir: Path, state_root: Path, evidence_root: Path,
        fleet_runner_path: Path, manifest_sha256: str, surcharge: int):
    """Derive all 181 sidecars from one verified native top-20 cycle."""
    _validate_authority(maps, manifest_sha256)
    if (type(surcharge) is not int or
            not 0 <= surcharge <= snagrepair.SURCHARGE_MAX):
        raise ValueError("snag surcharge exceeds runtime range")
    rune_corpus_controller.reject_symlink_components(rune_dir)
    rune_corpus_controller.reject_symlink_components(state_root)
    rune_corpus_controller.reject_symlink_components(evidence_root)
    rune_corpus_controller.reject_symlink_components(fleet_runner_path)

    module, runner_record = _load_fleet_verifier(fleet_runner_path)
    first_receipts = module.verify_stopped_residence_evidence(
        state_root, evidence_root)
    selected, wraps, receipt_records, topmaps, topmaps_record = _cycle_receipts(
        module, first_receipts, evidence_root, runner_record["sha256"])

    owner_path = state_root / "fleet-owner.json"
    ledger_path = evidence_root / "evidence-ledger.jsonl"
    owner_payload, owner_record = _stable_record(
        owner_path, maximum=64 * 1024 * 1024)
    ledger_payload, ledger_record = _stable_record(
        ledger_path, maximum=512 * 1024 * 1024)

    runes = {}
    rune_records = {}
    for map_name in maps:
        rune, record = read_rune_regular(rune_dir / f"{map_name}.rune")
        if rune.header.map_name != map_name:
            raise ValueError(
                f"RUNE names {rune.header.map_name!r}, expected {map_name!r}")
        runes[map_name] = rune
        rune_records[map_name] = record

    map_residences = {map_name: [] for map_name in maps}
    map_rows = {map_name: [] for map_name in maps}
    selected_receipt_records = {}
    for receipt_path, receipt, relative, receipt_record in selected:
        map_name = receipt["map"]
        if map_name not in map_residences:
            raise ValueError(f"fleet receipt map {map_name!r} is not authoritative")
        analyzed = _analyze_residence(
            receipt_path, receipt, runes[map_name], rune_records[map_name])
        analysis = analyzed["analysis"]
        if (analysis.get("map") != map_name or
                analysis.get("map_identity") != _identity(runes[map_name])):
            raise ValueError("stall analysis lost exact map identity")
        map_rows[map_name].append(analysis)
        map_residences[map_name].append({
            **analyzed,
            "lane": receipt["lane"],
            "receipt_hash": receipt.get("receipt_hash"),
            "receipt_path": relative,
            "receipt_sha256": receipt_record["sha256"],
            "sequence": receipt["sequence"],
        })
        selected_receipt_records[relative] = receipt_record

    if (set(map_name for map_name, rows in map_residences.items() if rows) !=
            set(topmaps) or
            any(len(map_residences[map_name]) != 10 for map_name in topmaps) or
            sum(len(rows) for rows in map_residences.values()) != 200):
        raise ValueError("selected fleet cycle does not cover each top-20 map ten times")

    # Re-run the complete fleet authority after every demo/segment analysis,
    # then re-read every direct input.  A caller cannot substitute a receipt,
    # owner, ledger, runner, demo, or segment during derivation and restore it
    # only after the result was published.
    second_receipts = module.verify_stopped_residence_evidence(
        state_root, evidence_root)
    if first_receipts != second_receipts:
        raise ValueError("fleet residence authority changed during derivation")
    runner_payload_after, runner_after = _stable_record(
        fleet_runner_path, maximum=MAX_FLEET_RUNNER_BYTES)
    if (runner_after != runner_record or
            hashlib.sha256(runner_payload_after).hexdigest() !=
            runner_record["sha256"]):
        raise ValueError("fleet verifier changed during derivation")
    owner_after_payload, owner_after = _stable_record(
        owner_path, maximum=64 * 1024 * 1024)
    ledger_after_payload, ledger_after = _stable_record(
        ledger_path, maximum=512 * 1024 * 1024)
    if (owner_after != owner_record or owner_after_payload != owner_payload or
            ledger_after != ledger_record or ledger_after_payload != ledger_payload):
        raise ValueError("stopped fleet authority changed during derivation")
    for receipt_path, _receipt, relative, original in selected:
        _payload, current = _stable_record(
            receipt_path, maximum=64 * 1024 * 1024)
        if current != original or current != selected_receipt_records[relative]:
            raise ValueError("selected residence receipt changed during derivation")
    for map_name in maps:
        current_rune, current_record = read_rune_regular(
            rune_dir / f"{map_name}.rune")
        if (current_rune.header.map_name != map_name or
                current_record != rune_records[map_name]):
            raise ValueError(
                f"final RUNE corpus changed during derivation at {map_name}")

    entries = []
    evidence_payloads = {}
    snag_payloads = {}
    for position, map_name in enumerate(maps):
        rune = runes[map_name]
        rune_record = rune_records[map_name]
        rows = map_rows[map_name]
        if rows:
            joined = snagrepair.seed_evidence_for_map(rows, map_name)
            repairs = snagrepair.repairs_from_seed_evidence(
                rune, joined, surcharge=surcharge)
            classification = (
                "AUTHENTICATED_ROUTE_STALLS" if repairs else
                "AUTHENTICATED_OBSERVATION_NO_ROUTE_STALL")
        else:
            repairs = []
            classification = "NO_ACCEPTED_OBSERVATION"
        evidence = {
            "classification": classification,
            "fleet_runner_sha256": runner_record["sha256"],
            "format": FINAL_EVIDENCE_FORMAT,
            "map": map_name,
            "map_position": position,
            "residences": map_residences[map_name],
            "rune_identity": _identity(rune),
            "rune_sha256": rune_record["sha256"],
        }
        evidence_bytes = canonical_json(evidence)
        evidence_sha = hashlib.sha256(evidence_bytes).hexdigest()
        snag_bytes = snagrepair.render(
            map_name, rune, repairs, evidence_sha,
            rune_record["sha256"]).encode("ascii")
        snag_sha = hashlib.sha256(snag_bytes).hexdigest()
        evidence_rel = f"evidence/{map_name}.json"
        snag_rel = f"maps/{map_name}.snag"
        evidence_payloads[evidence_rel] = evidence_bytes
        snag_payloads[snag_rel] = snag_bytes
        entries.append({
            "classification": classification,
            "evidence_path": evidence_rel,
            "evidence_sha256": evidence_sha,
            "evidence_size": len(evidence_bytes),
            "map": map_name,
            "position": position,
            "repairs": len(repairs),
            "residences": len(rows),
            "rune_path": f"maps/{map_name}.rune",
            "rune_sha256": rune_record["sha256"],
            "rune_size": rune_record["size"],
            "snag_path": snag_rel,
            "snag_sha256": snag_sha,
            "snag_size": len(snag_bytes),
        })

    document = {
        "classification": FINAL_CLASSIFICATION,
        "fleet": {
            "analyzed_residences": 200,
            "evidence_ledger_sha256": ledger_record["sha256"],
            "owner_sha256": owner_record["sha256"],
            "runner_sha256": runner_record["sha256"],
            "selection": "first-20-residences-per-lane",
            "topmaps_sha256": topmaps_record["sha256"],
            "wrap_receipts": wraps,
        },
        "format": FINAL_FORMAT,
        "map_count": len(entries),
        "maps": entries,
        "ordered_map_manifest_sha256": manifest_sha256,
    }
    guard = {
        "evidence_ledger": ledger_record,
        "fleet_owner": owner_record,
        "receipts": first_receipts,
        "runes": rune_records,
        "runner": runner_record,
        "topmaps": topmaps_record,
    }
    return document, evidence_payloads, snag_payloads, guard


def _revalidate_final_authority(
        maps, rune_dir: Path, state_root: Path, evidence_root: Path,
        fleet_runner_path: Path, guard) -> None:
    """Recheck every named authority immediately around corpus publication."""
    if not isinstance(guard, dict) or set(guard) != {
            "evidence_ledger", "fleet_owner", "receipts", "runes", "runner",
            "topmaps"}:
        raise ValueError("final corpus authority guard is invalid")
    module, runner_record = _load_fleet_verifier(fleet_runner_path)
    if runner_record != guard["runner"]:
        raise ValueError("fleet verifier changed before corpus publication")
    _topmaps, topmaps_record = _topmaps_authority()
    if topmaps_record != guard["topmaps"]:
        raise ValueError("topmaps authority changed before corpus publication")
    receipts = module.verify_stopped_residence_evidence(
        state_root, evidence_root)
    if receipts != guard["receipts"]:
        raise ValueError("fleet receipts changed before corpus publication")
    _owner_payload, owner_record = _stable_record(
        state_root / "fleet-owner.json", maximum=64 * 1024 * 1024)
    _ledger_payload, ledger_record = _stable_record(
        evidence_root / "evidence-ledger.jsonl",
        maximum=512 * 1024 * 1024)
    if (owner_record != guard["fleet_owner"] or
            ledger_record != guard["evidence_ledger"]):
        raise ValueError("stopped fleet authority changed before publication")
    if (not isinstance(guard["runes"], dict) or
            set(guard["runes"]) != set(maps)):
        raise ValueError("final RUNE guard is incomplete")
    for map_name in maps:
        rune, record = read_rune_regular(rune_dir / f"{map_name}.rune")
        if (rune.header.map_name != map_name or
                record != guard["runes"][map_name]):
            raise ValueError(
                f"final RUNE changed before corpus publication: {map_name}")


def _verify_final_files(root: Path, document, evidence_payloads,
                        snag_payloads, *, root_fd: int | None = None):
    if root_fd is None:
        rune_corpus_controller.reject_symlink_components(root)
        root_info = root.lstat()
    else:
        root_info = os.fstat(root_fd)
    if not stat.S_ISDIR(root_info.st_mode) or root_info.st_mode & 0o222:
        raise ValueError("final snag corpus root is not an immutable directory")
    expected_paths = {
        "snag-corpus-manifest.json", *evidence_payloads, *snag_payloads}
    actual_paths = set()
    for directory, names, files in os.walk(root, followlinks=False):
        directory_path = Path(directory)
        directory_info = (
            os.fstat(root_fd)
            if root_fd is not None and directory_path == root
            else directory_path.lstat())
        if (not stat.S_ISDIR(directory_info.st_mode) or
                directory_info.st_mode & 0o222):
            raise ValueError("final snag corpus contains a mutable directory")
        for name in names:
            info = (directory_path / name).lstat()
            if not stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode):
                raise ValueError("final snag corpus contains a non-directory entry")
        for name in files:
            path = directory_path / name
            info = path.lstat()
            if (not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or
                    info.st_mode & 0o222):
                raise ValueError("final snag corpus contains a mutable or linked file")
            actual_paths.add(path.relative_to(root).as_posix())
    if actual_paths != expected_paths:
        raise ValueError("final snag corpus file inventory is incomplete or extra")
    manifest_bytes = canonical_json(document)
    actual_manifest, manifest_record = _stable_record(
        root / "snag-corpus-manifest.json", maximum=64 * 1024 * 1024)
    if actual_manifest != manifest_bytes:
        raise ValueError("final snag corpus manifest differs from derivation")
    for relative, expected in {**evidence_payloads, **snag_payloads}.items():
        actual, _record = _stable_record(root / relative)
        if actual != expected:
            raise ValueError(f"final snag corpus artifact drift: {relative}")
    return document, manifest_record["sha256"]


def verify_final_corpus(
        maps, rune_dir: Path, state_root: Path, evidence_root: Path,
        fleet_runner_path: Path, root: Path, manifest_sha256: str,
        surcharge: int = 1000):
    document, evidence_payloads, snag_payloads, guard = _derive_final_corpus(
        maps, rune_dir, state_root, evidence_root, fleet_runner_path,
        manifest_sha256, surcharge)
    verified = _verify_final_files(
        root, document, evidence_payloads, snag_payloads)
    _revalidate_final_authority(
        maps, rune_dir, state_root, evidence_root, fleet_runner_path, guard)
    return verified


def verify_corpus(
        maps, rune_dir: Path, root: Path, manifest_sha256: str, *,
        root_fd: int | None = None):
    """Re-read and authenticate one complete immutable bootstrap corpus."""
    _validate_authority(maps, manifest_sha256)
    rune_corpus_controller.reject_symlink_components(rune_dir)
    if root_fd is None:
        rune_corpus_controller.reject_symlink_components(root)
        root_info = root.lstat()
    else:
        root_info = os.fstat(root_fd)
    if not stat.S_ISDIR(root_info.st_mode) or root_info.st_mode & 0o222:
        raise ValueError("bootstrap corpus root is not an immutable directory")

    expected_paths = {"snag-corpus-manifest.json"}
    for map_name in maps:
        expected_paths.add(f"evidence/{map_name}.json")
        expected_paths.add(f"maps/{map_name}.snag")
    actual_paths = set()
    for directory, names, files in os.walk(root, followlinks=False):
        directory_path = Path(directory)
        directory_info = (
            os.fstat(root_fd)
            if root_fd is not None and directory_path == root
            else directory_path.lstat()
        )
        if (not stat.S_ISDIR(directory_info.st_mode) or
                directory_info.st_mode & 0o222):
            raise ValueError(
                f"bootstrap corpus contains a mutable directory: {directory_path}")
        for name in names:
            info = (directory_path / name).lstat()
            if not stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode):
                raise ValueError("bootstrap corpus contains a non-directory entry")
        for name in files:
            path = directory_path / name
            info = path.lstat()
            if (not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or
                    info.st_mode & 0o222):
                raise ValueError("bootstrap corpus contains a mutable or linked file")
            actual_paths.add(path.relative_to(root).as_posix())
    if actual_paths != expected_paths:
        raise ValueError("bootstrap corpus file inventory is incomplete or extra")

    manifest_bytes, manifest_record = _read_regular(
        root / "snag-corpus-manifest.json", maximum=64 * 1024 * 1024)
    try:
        document = json.loads(manifest_bytes)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError("bootstrap corpus manifest is invalid JSON") from exc
    if manifest_bytes != canonical_json(document):
        raise ValueError("bootstrap corpus manifest is not canonical JSON")
    if (not isinstance(document, dict) or set(document) != {
            "classification", "format", "map_count",
            "ordered_map_manifest_sha256", "maps"} or
            document["classification"] != "BOOTSTRAP_NO_ACCEPTED_OBSERVATIONS" or
            document["format"] != FORMAT or document["map_count"] != len(maps) or
            document["ordered_map_manifest_sha256"] != manifest_sha256 or
            not isinstance(document["maps"], list) or
            len(document["maps"]) != len(maps)):
        raise ValueError("bootstrap corpus manifest schema is invalid")

    entry_fields = {
        "classification", "evidence_path", "evidence_sha256",
        "evidence_size", "map", "position", "repairs", "rune_path",
        "rune_sha256", "rune_size", "snag_path", "snag_sha256",
        "snag_size",
    }
    if type(document["map_count"]) is not int:
        raise ValueError("bootstrap corpus map_count is not an integer")

    for position, map_name in enumerate(maps):
        entry = document["maps"][position]
        evidence_rel = f"evidence/{map_name}.json"
        snag_rel = f"maps/{map_name}.snag"
        rune_rel = f"maps/{map_name}.rune"
        if (not isinstance(entry, dict) or set(entry) != entry_fields or
                type(entry["position"]) is not int or
                type(entry["repairs"]) is not int or
                type(entry["evidence_size"]) is not int or
                type(entry["rune_size"]) is not int or
                type(entry["snag_size"]) is not int or
                entry["classification"] != "NO_ACCEPTED_OBSERVATION" or
                entry["map"] != map_name or entry["position"] != position or
                entry["repairs"] != 0 or entry["evidence_path"] != evidence_rel or
                entry["snag_path"] != snag_rel or entry["rune_path"] != rune_rel):
            raise ValueError(f"bootstrap corpus entry {position} is invalid")

        rune, rune_record = read_rune_regular(rune_dir / f"{map_name}.rune")
        if (rune.header.map_name != map_name or
                entry["rune_sha256"] != rune_record["sha256"] or
                entry["rune_size"] != rune_record["size"]):
            raise ValueError(f"bootstrap corpus RUNE identity drift for {map_name}")
        evidence = {
            "classification": "NO_ACCEPTED_OBSERVATION",
            "format": EVIDENCE_FORMAT,
            "map": map_name,
            "map_position": position,
            "rune_identity": _identity(rune),
            "rune_sha256": rune_record["sha256"],
        }
        evidence_bytes, evidence_record = _read_regular(
            root / evidence_rel, maximum=16 * 1024 * 1024)
        if (evidence_bytes != canonical_json(evidence) or
                entry["evidence_sha256"] != evidence_record["sha256"] or
                entry["evidence_size"] != evidence_record["size"]):
            raise ValueError(f"bootstrap corpus evidence drift for {map_name}")
        expected_snag = snagrepair.render(
            map_name, rune, [], evidence_record["sha256"],
            rune_record["sha256"]
        ).encode("ascii")
        snag_bytes, snag_record = _read_regular(root / snag_rel)
        if (snag_bytes != expected_snag or
                entry["snag_sha256"] != snag_record["sha256"] or
                entry["snag_size"] != snag_record["size"]):
            raise ValueError(f"bootstrap corpus snag drift for {map_name}")
    return document, manifest_record["sha256"]


def _rename_noreplace(
        source_dir_fd: int, source: str,
        destination_dir_fd: int, destination: str) -> None:
    """Linux atomic publish that cannot replace even an empty directory."""
    libc = ctypes.CDLL(None, use_errno=True)
    renameat2 = getattr(libc, "renameat2", None)
    if renameat2 is None:
        raise ValueError("renameat2 is required for no-replace corpus publish")
    renameat2.argtypes = (
        ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p,
        ctypes.c_uint,
    )
    renameat2.restype = ctypes.c_int
    if renameat2(
        source_dir_fd, os.fsencode(source), destination_dir_fd,
        os.fsencode(destination), 1,  # RENAME_NOREPLACE
    ) != 0:
        error = ctypes.get_errno()
        if error in (errno.EEXIST, errno.ENOTEMPTY):
            raise ValueError("output already exists")
        raise OSError(error, os.strerror(error))


def _open_directory_nofollow(path: Path) -> int:
    """Open an existing absolute directory one retained component at a time."""
    absolute = Path(os.path.abspath(path))
    flags = (os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) |
             getattr(os, "O_NOFOLLOW", 0))
    fd = os.open(absolute.anchor, flags)
    try:
        for part in absolute.parts[1:]:
            next_fd = os.open(part, flags, dir_fd=fd)
            os.close(fd)
            fd = next_fd
        info = os.fstat(fd)
        if not stat.S_ISDIR(info.st_mode):
            raise ValueError(f"output parent is not a directory: {path}")
        return fd
    except BaseException:
        os.close(fd)
        raise


def _make_stage_directory(parent_fd: int, prefix: str):
    for _attempt in range(128):
        name = f".{prefix}.{secrets.token_hex(12)}"
        try:
            os.mkdir(name, 0o700, dir_fd=parent_fd)
        except FileExistsError:
            continue
        try:
            stage_fd = os.open(
                name,
                os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) |
                getattr(os, "O_NOFOLLOW", 0),
                dir_fd=parent_fd,
            )
            named = os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
            retained = os.fstat(stage_fd)
            if ((named.st_dev, named.st_ino) !=
                    (retained.st_dev, retained.st_ino)):
                raise ValueError("staging directory changed while opening")
            return name, stage_fd, Path(f"/proc/self/fd/{stage_fd}")
        except BaseException:
            try:
                os.rmdir(name, dir_fd=parent_fd)
            except OSError:
                pass
            raise
    raise ValueError("cannot allocate a unique staging directory")


def _same_named_directory(path: Path, retained_fd: int) -> bool:
    try:
        current_fd = _open_directory_nofollow(path)
    except OSError:
        return False
    try:
        current = os.fstat(current_fd)
        retained = os.fstat(retained_fd)
        return (current.st_dev, current.st_ino) == (
            retained.st_dev, retained.st_ino)
    finally:
        os.close(current_fd)


def _remove_stage_contents(root: Path, root_fd: int) -> None:
    """Remove children through a retained `/proc/self/fd` directory handle."""
    os.fchmod(root_fd, 0o700)
    for directory, names, _files in os.walk(
            root, topdown=True, followlinks=False):
        current = Path(directory)
        if current != root:
            current.chmod(0o700)
        for name in names:
            (current / name).chmod(0o700)
    for directory, names, files in os.walk(root, topdown=False, followlinks=False):
        current = Path(directory)
        for name in files:
            path = current / name
            path.chmod(0o600)
            path.unlink()
        for name in names:
            path = current / name
            path.chmod(0o700)
            path.rmdir()


def build_bootstrap(maps, rune_dir: Path, output: Path,
                    manifest_sha256: str):
    """Create a new complete corpus directory; never overwrite an old one."""
    _validate_authority(maps, manifest_sha256)
    rune_corpus_controller.reject_symlink_components(rune_dir)
    output_parent = Path(os.path.abspath(output.parent))
    try:
        parent_fd = _open_directory_nofollow(output_parent)
    except (FileNotFoundError, NotADirectoryError, OSError) as exc:
        raise ValueError(
            "output parent must be an existing non-symlink directory") from exc
    try:
        try:
            os.stat(output.name, dir_fd=parent_fd, follow_symlinks=False)
        except FileNotFoundError:
            pass
        else:
            raise ValueError("output already exists")
        stage_name, stage_fd, stage = _make_stage_directory(
            parent_fd, output.name)
    except BaseException:
        os.close(parent_fd)
        raise
    published = False
    try:
        entries = []
        for position, map_name in enumerate(maps):
            rune_path = rune_dir / f"{map_name}.rune"
            rune, rune_record = read_rune_regular(rune_path)
            if rune.header.map_name != map_name:
                raise ValueError(
                    f"RUNE {rune_path} names {rune.header.map_name!r}, "
                    f"expected {map_name!r}")
            evidence = {
                "classification": "NO_ACCEPTED_OBSERVATION",
                "format": EVIDENCE_FORMAT,
                "map": map_name,
                "map_position": position,
                "rune_identity": _identity(rune),
                "rune_sha256": rune_record["sha256"],
            }
            evidence_bytes = canonical_json(evidence)
            evidence_sha = hashlib.sha256(evidence_bytes).hexdigest()
            snag_bytes = snagrepair.render(
                map_name, rune, [], evidence_sha,
                rune_record["sha256"]).encode("ascii")
            snag_sha = hashlib.sha256(snag_bytes).hexdigest()
            evidence_rel = Path("evidence") / f"{map_name}.json"
            snag_rel = Path("maps") / f"{map_name}.snag"
            _write_bytes(stage / evidence_rel, evidence_bytes)
            _write_bytes(stage / snag_rel, snag_bytes)
            entries.append({
                "classification": "NO_ACCEPTED_OBSERVATION",
                "evidence_path": evidence_rel.as_posix(),
                "evidence_sha256": evidence_sha,
                "map": map_name,
                "position": position,
                "repairs": 0,
                "rune_path": f"maps/{map_name}.rune",
                "rune_sha256": rune_record["sha256"],
                "rune_size": rune_record["size"],
                "snag_path": snag_rel.as_posix(),
                "snag_sha256": snag_sha,
                "snag_size": len(snag_bytes),
                "evidence_size": len(evidence_bytes),
            })
        document = {
            "classification": "BOOTSTRAP_NO_ACCEPTED_OBSERVATIONS",
            "format": FORMAT,
            "map_count": len(entries),
            "ordered_map_manifest_sha256": manifest_sha256,
            "maps": entries,
        }
        manifest_bytes = canonical_json(document)
        _write_bytes(stage / "snag-corpus-manifest.json", manifest_bytes)
        rune_corpus_controller.freeze_tree(stage)
        os.fchmod(
            stage_fd, stat.S_IMODE(os.fstat(stage_fd).st_mode) & ~0o222,
        )
        verified_document, verified_sha = verify_corpus(
            maps, rune_dir, stage, manifest_sha256, root_fd=stage_fd)
        if verified_document != document or verified_sha != hashlib.sha256(
                manifest_bytes).hexdigest():
            raise ValueError("bootstrap corpus verification disagrees with build")
        rune_corpus_controller.fsync_tree(stage)
        # Both names are resolved relative to the retained parent descriptor.
        # Replacing the lexical parent with a symlink cannot redirect publish.
        _rename_noreplace(parent_fd, stage_name, parent_fd, output.name)
        published = True
        os.fsync(parent_fd)
        published_document, published_sha = verify_corpus(
            maps, rune_dir, stage, manifest_sha256, root_fd=stage_fd)
        if (published_document != document or published_sha != verified_sha):
            raise ValueError("published bootstrap corpus differs from stage")
        if not _same_named_directory(output_parent, parent_fd):
            raise ValueError("output parent changed during corpus publish")
        return document, hashlib.sha256(manifest_bytes).hexdigest()
    except BaseException:
        try:
            _remove_stage_contents(stage, stage_fd)
            os.rmdir(output.name if published else stage_name, dir_fd=parent_fd)
            os.fsync(parent_fd)
        except FileNotFoundError:
            pass
        raise
    finally:
        os.close(stage_fd)
        os.close(parent_fd)


def build_final(
        maps, rune_dir: Path, state_root: Path, evidence_root: Path,
        fleet_runner_path: Path, output: Path, manifest_sha256: str,
        surcharge: int = 1000):
    """Publish one immutable final 181-map corpus without overwriting."""
    _validate_authority(maps, manifest_sha256)
    output_parent = Path(os.path.abspath(output.parent))
    try:
        parent_fd = _open_directory_nofollow(output_parent)
    except (FileNotFoundError, NotADirectoryError, OSError) as exc:
        raise ValueError(
            "output parent must be an existing non-symlink directory") from exc
    try:
        try:
            os.stat(output.name, dir_fd=parent_fd, follow_symlinks=False)
        except FileNotFoundError:
            pass
        else:
            raise ValueError("output already exists")
        stage_name, stage_fd, stage = _make_stage_directory(
            parent_fd, output.name)
    except BaseException:
        os.close(parent_fd)
        raise
    published = False
    try:
        document, evidence_payloads, snag_payloads, guard = _derive_final_corpus(
            maps, rune_dir, state_root, evidence_root, fleet_runner_path,
            manifest_sha256, surcharge)
        for relative, payload in sorted(evidence_payloads.items()):
            _write_bytes(stage / relative, payload)
        for relative, payload in sorted(snag_payloads.items()):
            _write_bytes(stage / relative, payload)
        manifest_bytes = canonical_json(document)
        _write_bytes(stage / "snag-corpus-manifest.json", manifest_bytes)
        rune_corpus_controller.freeze_tree(stage)
        os.fchmod(stage_fd, stat.S_IMODE(os.fstat(stage_fd).st_mode) & ~0o222)
        verified, verified_sha = _verify_final_files(
            stage, document, evidence_payloads, snag_payloads,
            root_fd=stage_fd)
        expected_sha = hashlib.sha256(manifest_bytes).hexdigest()
        if verified != document or verified_sha != expected_sha:
            raise ValueError("final snag corpus verification disagrees with build")
        rune_corpus_controller.fsync_tree(stage)
        _revalidate_final_authority(
            maps, rune_dir, state_root, evidence_root, fleet_runner_path,
            guard)
        _rename_noreplace(parent_fd, stage_name, parent_fd, output.name)
        published = True
        os.fsync(parent_fd)
        published_document, published_sha = _verify_final_files(
            stage, document, evidence_payloads, snag_payloads,
            root_fd=stage_fd)
        if published_document != document or published_sha != verified_sha:
            raise ValueError("published final snag corpus differs from stage")
        _revalidate_final_authority(
            maps, rune_dir, state_root, evidence_root, fleet_runner_path,
            guard)
        if not _same_named_directory(output_parent, parent_fd):
            raise ValueError("output parent changed during corpus publish")
        return document, expected_sha
    except BaseException:
        try:
            _remove_stage_contents(stage, stage_fd)
            os.rmdir(output.name if published else stage_name, dir_fd=parent_fd)
            os.fsync(parent_fd)
        except FileNotFoundError:
            pass
        raise
    finally:
        os.close(stage_fd)
        os.close(parent_fd)


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path,
                        default=rune_corpus_controller.DEFAULT_MANIFEST)
    parser.add_argument("--rune-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--state-root", type=Path)
    parser.add_argument("--evidence-root", type=Path)
    parser.add_argument(
        "--fleet-runner", type=Path,
        default=Path(__file__).with_name("fleet-runner.py"))
    parser.add_argument("--surcharge", type=int, default=1000)
    parser.add_argument("--verify-final", action="store_true")
    args = parser.parse_args(argv)

    maps = rune_corpus_controller.validate_manifest(args.manifest)
    manifest_payload, manifest_record = _read_regular(
        args.manifest, maximum=1024 * 1024)
    manifest_sha = hashlib.sha256(manifest_payload).hexdigest()
    if manifest_sha != rune_corpus_controller.EXPECTED_MANIFEST_SHA256:
        raise ValueError("ordered map manifest changed after validation")
    final_requested = (
        args.state_root is not None or args.evidence_root is not None or
        args.verify_final)
    if final_requested:
        if args.state_root is None or args.evidence_root is None:
            parser.error("final corpus requires --state-root and --evidence-root")
        operation = verify_final_corpus if args.verify_final else build_final
        if args.verify_final:
            _document, digest = operation(
                maps, args.rune_dir, args.state_root, args.evidence_root,
                args.fleet_runner, args.output, manifest_record["sha256"],
                args.surcharge)
        else:
            _document, digest = operation(
                maps, args.rune_dir, args.state_root, args.evidence_root,
                args.fleet_runner, args.output, manifest_record["sha256"],
                args.surcharge)
    else:
        if args.verify_final:
            parser.error("--verify-final requires final corpus inputs")
        _document, digest = build_bootstrap(
            maps, args.rune_dir, args.output, manifest_record["sha256"])
    print(digest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
