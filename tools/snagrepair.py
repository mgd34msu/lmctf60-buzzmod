#!/usr/bin/env python3
"""Build one strict, RUNE-seed-bound navigation repair file.

``stallcensus.py`` retains visible demo clusters for diagnosis, but coordinates
are not graph identity.  Production repairs come only from the exact ``seed=``
reported by the controller during the same authenticated residence.  The
output repeats that seed's exact RUNE origin; the runtime never performs a
radius search or silently accepts a missing file.
"""

import argparse
import hashlib
import math
import os
import stat
import tempfile
from pathlib import Path

import runeio


IDENTITY_KEYS = (
    "bsp_checksum", "entity_crc", "physics_flags", "gravity",
    "airaccelerate", "maxvelocity", "pmove_ms", "frame_ms",
    "host_physics_id",
)
MAX_RECORDS = 512
COORD_LIMIT = 65536.0
EVIDENCE_MAX = 1000000
DURATION_MS_MAX = 86400000
SURCHARGE_MAX = 60000
SNAG_FORMAT = 2
MAX_EVIDENCE_MANIFEST_BYTES = 16 * 1024 * 1024
MAX_SNAG_FILE_BYTES = (MAX_RECORDS + 32) * 256
SNAG_HEADER_KEYS = (
    "snag_format", "map", "bsp_checksum", "entity_crc", "physics_flags",
    "gravity", "airaccelerate", "maxvelocity", "pmove_ms", "frame_ms",
    "host_physics_id", "rune_payload_crc", "rune_header_crc",
    "rune_action_contract_crc", "rune_mechanism_contract_crc",
    "rune_num_seeds", "rune_num_links", "rune_sha256", "evidence_sha256",
    "repairs",
)


def _finite_number(value, field):
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise ValueError(f"{field} must be numeric")
    if not math.isfinite(value):
        raise ValueError(f"{field} must be finite")
    return value


def _sha256(value, field="evidence_sha256"):
    if (not isinstance(value, str) or len(value) != 64 or
            any(ch not in "0123456789abcdef" for ch in value)):
        raise ValueError(f"{field} must be 64 lowercase hexadecimal characters")
    return value


def _read_regular(path, maximum, label):
    """Read one stable, unaliased regular file without following a symlink."""
    path = Path(path)
    flags = os.O_RDONLY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(path, flags)
    except OSError as exc:
        raise ValueError(f"cannot open {label} {path}: {exc}") from exc
    try:
        before = os.fstat(fd)
        if (not stat.S_ISREG(before.st_mode) or before.st_size <= 0 or
                before.st_size > maximum or before.st_nlink != 1):
            raise ValueError(
                f"{label} is not a bounded unaliased nonempty regular file")
        payload = bytearray()
        while len(payload) < before.st_size:
            block = os.read(fd, min(1024 * 1024, before.st_size - len(payload)))
            if not block:
                raise ValueError(f"{label} truncated during read")
            payload.extend(block)
        after = os.fstat(fd)
        named = path.stat(follow_symlinks=False)
        if ((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns,
             before.st_ctime_ns) !=
                (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns,
                 after.st_ctime_ns) or
                (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns,
                 after.st_ctime_ns) !=
                (named.st_dev, named.st_ino, named.st_size, named.st_mtime_ns,
                 named.st_ctime_ns) or
                after.st_nlink != 1 or named.st_nlink != 1):
            raise ValueError(f"{label} changed while reading")
        return bytes(payload)
    finally:
        os.close(fd)


def hash_evidence_manifest(path):
    payload = _read_regular(
        path, MAX_EVIDENCE_MANIFEST_BYTES, "evidence manifest")
    return hashlib.sha256(payload).hexdigest()


def read_rune_and_sha256(path):
    payload = _read_regular(
        path, runeio.MAX_RUNE_FILE_BYTES, "RUNE artifact")
    return runeio.decode_rune(payload), hashlib.sha256(payload).hexdigest()


def validate_identity(identity):
    if not isinstance(identity, dict):
        raise ValueError("map_identity must be an object")
    missing = [key for key in IDENTITY_KEYS if key not in identity]
    if missing:
        raise ValueError(f"map_identity missing {missing[0]}")
    for key in IDENTITY_KEYS:
        _finite_number(identity[key], key)
    for key in ("bsp_checksum", "entity_crc", "physics_flags", "pmove_ms",
                "frame_ms", "host_physics_id"):
        value = identity[key]
        if (not isinstance(value, int) or isinstance(value, bool) or
                not 0 <= value <= 0xffffffff):
            raise ValueError(f"{key} exceeds runtime limit")
    for key in ("gravity", "airaccelerate"):
        if abs(identity[key]) > COORD_LIMIT:
            raise ValueError(f"{key} exceeds runtime limit")
    if not 0 <= identity["maxvelocity"] <= COORD_LIMIT:
        raise ValueError("maxvelocity exceeds runtime limit")
    return {key: identity[key] for key in IDENTITY_KEYS}


def identity_for_map(rows, map_name):
    identity = None
    matched = False
    for row in rows:
        if row.get("map") != map_name:
            continue
        matched = True
        candidate = validate_identity(row.get("map_identity"))
        if identity is None:
            identity = candidate
        elif candidate != identity:
            raise ValueError("mixed map incarnations")
    if not matched:
        raise ValueError(f"{map_name} has no census evidence")
    return identity


def _rune_identity(rune):
    identity = rune.header.identity
    return {
        "bsp_checksum": identity.bsp_checksum,
        "entity_crc": identity.entity_crc32,
        "physics_flags": identity.physics_flags,
        "gravity": identity.gravity,
        "airaccelerate": identity.airaccelerate,
        "maxvelocity": identity.maxvelocity,
        "pmove_ms": identity.pmove_substep_ms,
        "frame_ms": identity.server_frame_ms,
        "host_physics_id": identity.host_physics_id,
    }


def require_rune_identity(rune, map_name, census_identity):
    if rune.header.map_name != map_name:
        raise ValueError(
            f"RUNE map {rune.header.map_name!r} does not match {map_name!r}"
        )
    if validate_identity(census_identity) != validate_identity(_rune_identity(rune)):
        raise ValueError("census identity does not match authenticated RUNE")


def seed_evidence_for_map(rows, map_name):
    """Aggregate exact controller-selected seed evidence for one map."""
    totals = {}
    identity_for_map(rows, map_name)
    for row in rows:
        if row.get("map") != map_name:
            continue
        evidence = row.get("accepted_route_stall_evidence")
        if not isinstance(evidence, list):
            raise ValueError(
                "accepted_route_stall_evidence must be a joined evidence array")
        seen = set()
        for record in evidence:
            if not isinstance(record, dict) or set(record) != {
                    "seed", "evidence_count", "duration_ms"}:
                raise ValueError("route-stall evidence has incorrect shape")
            seed = record["seed"]
            if (not isinstance(seed, int) or isinstance(seed, bool) or seed < 0):
                raise ValueError("route-stall seed must be a nonnegative integer")
            if seed in seen:
                raise ValueError("duplicate route-stall seed in one census row")
            seen.add(seed)
            count = record["evidence_count"]
            duration_ms = record["duration_ms"]
            if (not isinstance(count, int) or isinstance(count, bool) or
                    not 0 < count <= EVIDENCE_MAX):
                raise ValueError("evidence_count exceeds runtime limit")
            if (not isinstance(duration_ms, int) or isinstance(duration_ms, bool) or
                    not 0 < duration_ms <= DURATION_MS_MAX):
                raise ValueError("duration_ms exceeds runtime limit")
            old_count, old_duration = totals.get(seed, (0, 0))
            if (old_count > EVIDENCE_MAX - count or
                    old_duration > DURATION_MS_MAX - duration_ms):
                raise ValueError(
                    f"aggregated evidence for seed {seed} exceeds runtime limit")
            totals[seed] = (old_count + count, old_duration + duration_ms)
    return totals


def correlate_stall_evidence(row):
    """Join visible demo episodes to exact SG/RUNE episodes one-to-one.

    A repair is authorized only when one visible push/jitter interval overlaps
    exactly one controller episode for the same admitted player.  Every visible
    cluster receives an explicit disposition; ambiguous many-to-one or
    one-to-many joins reject the residence rather than guessing geometrically.
    """
    if not isinstance(row, dict):
        raise ValueError("stall census row must be an object")
    visible = row.get("stall_episodes")
    route = row.get("route_stall_episodes")
    clusters = row.get("snag_clusters")
    if not isinstance(visible, list) or not isinstance(route, list):
        raise ValueError("stall census lacks episode-level join authority")
    if not isinstance(clusters, list):
        raise ValueError("stall census lacks visible cluster authority")

    visible_by_id = {}
    for episode in visible:
        keys = {
            "episode_id", "player", "frame_start", "frame_end_exclusive",
            "duration_s", "x", "y", "z",
        }
        if not isinstance(episode, dict) or set(episode) != keys:
            raise ValueError("visible stall episode has incorrect shape")
        identifier = episode["episode_id"]
        if (not isinstance(identifier, str) or not identifier or
                identifier in visible_by_id or
                not isinstance(episode["player"], str) or
                not episode["player"] or
                type(episode["frame_start"]) is not int or
                type(episode["frame_end_exclusive"]) is not int or
                episode["frame_start"] < 0 or
                episode["frame_end_exclusive"] <= episode["frame_start"] or
                any(not isinstance(episode[key], (int, float)) or
                    isinstance(episode[key], bool) or
                    not math.isfinite(episode[key])
                    for key in ("duration_s", "x", "y", "z")) or
                episode["duration_s"] <= 0):
            raise ValueError("visible stall episode is invalid")
        visible_by_id[identifier] = episode

    route_checked = []
    for index, episode in enumerate(route):
        keys = {
            "player", "seed", "frame_start", "frame_end_exclusive",
            "evidence_count", "duration_ms",
        }
        if not isinstance(episode, dict) or set(episode) != keys:
            raise ValueError("route stall episode has incorrect shape")
        if (not isinstance(episode["player"], str) or not episode["player"] or
                any(type(episode[key]) is not int for key in (
                    "seed", "frame_start", "frame_end_exclusive",
                    "evidence_count", "duration_ms")) or
                episode["seed"] < 0 or episode["frame_start"] < 0 or
                episode["frame_end_exclusive"] <= episode["frame_start"] or
                not 0 < episode["evidence_count"] <= EVIDENCE_MAX or
                not 0 < episode["duration_ms"] <= DURATION_MS_MAX):
            raise ValueError("route stall episode is invalid")
        route_checked.append((index, episode))

    cluster_episode_ids = []
    for cluster in clusters:
        if (not isinstance(cluster, dict) or set(cluster) != {
                "x", "y", "z", "evidence_count", "duration_s",
                "episode_ids"}):
            raise ValueError("visible cluster has incorrect shape")
        identifiers = cluster["episode_ids"]
        if (not isinstance(identifiers, list) or not identifiers or
                identifiers != sorted(set(identifiers)) or
                any(identifier not in visible_by_id for identifier in identifiers) or
                type(cluster["evidence_count"]) is not int or
                cluster["evidence_count"] != len(identifiers) or
                any(not isinstance(cluster[key], (int, float)) or
                    isinstance(cluster[key], bool) or
                    not math.isfinite(cluster[key])
                    for key in ("x", "y", "z", "duration_s")) or
                cluster["duration_s"] <= 0):
            raise ValueError("visible cluster episode inventory is invalid")
        cluster_episode_ids.extend(identifiers)
    if sorted(cluster_episode_ids) != sorted(visible_by_id):
        raise ValueError("visible clusters do not partition stall episodes")

    used_route = set()
    matched = {}
    for identifier, episode in sorted(visible_by_id.items()):
        candidates = [
            (index, candidate) for index, candidate in route_checked
            if candidate["player"] == episode["player"] and
            max(candidate["frame_start"], episode["frame_start"]) <
            min(candidate["frame_end_exclusive"],
                episode["frame_end_exclusive"])
        ]
        if len(candidates) > 1:
            raise ValueError(f"ambiguous route episodes for {identifier}")
        if not candidates:
            continue
        index, candidate = candidates[0]
        if index in used_route:
            raise ValueError("one route episode overlaps multiple visible stalls")
        used_route.add(index)
        matched[identifier] = candidate

    totals = {}
    for candidate in matched.values():
        seed = candidate["seed"]
        count, duration = totals.get(seed, (0, 0))
        if (count > EVIDENCE_MAX - candidate["evidence_count"] or
                duration > DURATION_MS_MAX - candidate["duration_ms"]):
            raise ValueError("joined stall evidence exceeds runtime limit")
        totals[seed] = (
            count + candidate["evidence_count"],
            duration + candidate["duration_ms"],
        )

    dispositions = []
    for index, cluster in enumerate(clusters):
        identifiers = cluster["episode_ids"]
        attributed = [identifier for identifier in identifiers
                      if identifier in matched]
        if len(attributed) == len(identifiers):
            classification = "ATTRIBUTED_ROUTE_STALLS"
        elif attributed:
            classification = "PARTIAL_ROUTE_STALL_ATTRIBUTION"
        else:
            classification = "VISIBLE_STALL_WITHOUT_ROUTE_AUTHORITY"
        dispositions.append({
            "classification": classification,
            "cluster": index,
            "episode_ids": identifiers,
            "attributed_episode_ids": attributed,
            "seeds": sorted({matched[item]["seed"] for item in attributed}),
        })

    accepted = [
        {"seed": seed, "evidence_count": count, "duration_ms": duration}
        for seed, (count, duration) in sorted(totals.items())
    ]
    return {
        "accepted_route_stall_evidence": accepted,
        "cluster_dispositions": dispositions,
        "unmatched_route_episodes": [
            episode for index, episode in route_checked if index not in used_route
        ],
    }


def repairs_from_seed_evidence(rune, evidence, surcharge=1000):
    """Bind controller-selected seeds to the exact authenticated RUNE."""
    if not isinstance(surcharge, int) or not 0 <= surcharge <= SURCHARGE_MAX:
        raise ValueError("surcharge must be an integer in runtime range")
    if not isinstance(evidence, dict):
        raise ValueError("seed evidence must be an object")
    if len(evidence) > MAX_RECORDS:
        raise ValueError("repair record count exceeds runtime limit")
    outgoing = {link.source for link in rune.links}
    repairs = []
    for index, values in sorted(evidence.items()):
        if (not isinstance(index, int) or isinstance(index, bool) or
                not 0 <= index < len(rune.seeds)):
            raise ValueError(f"route-stall seed {index} exceeds RUNE bounds")
        seed = rune.seeds[index]
        if seed.flags & (runeio.RSF_WATER | runeio.RSF_TOMBSTONE):
            raise ValueError(f"route-stall seed {index} is not live ground")
        if index not in outgoing:
            raise ValueError(f"route-stall seed {index} has no live route")
        count, duration_ms = values
        repairs.append(
            (index, *seed.origin, count, duration_ms, surcharge)
        )
    return repairs


def render(map_name, rune, repairs, evidence_sha256, rune_sha256):
    evidence_sha256 = _sha256(evidence_sha256)
    rune_sha256 = _sha256(rune_sha256, "rune_sha256")
    if rune.header.map_name != map_name:
        raise ValueError("map does not match authenticated RUNE")
    if len(repairs) > MAX_RECORDS:
        raise ValueError("repair record count exceeds runtime limit")

    seen = set()
    outgoing = {link.source for link in rune.links}
    checked = []
    for record in repairs:
        if not isinstance(record, (tuple, list)) or len(record) != 7:
            raise ValueError("repair record has incorrect shape")
        seed_index, x, y, z, count, duration_ms, surcharge = record
        if (not isinstance(seed_index, int) or isinstance(seed_index, bool) or
                not 0 <= seed_index < len(rune.seeds)):
            raise ValueError("repair seed exceeds RUNE bounds")
        if seed_index in seen:
            raise ValueError("duplicate repair seed")
        seen.add(seed_index)
        seed = rune.seeds[seed_index]
        if seed.flags & (runeio.RSF_WATER | runeio.RSF_TOMBSTONE):
            raise ValueError("repair seed is not live ground")
        if seed_index not in outgoing:
            raise ValueError("repair seed has no live route")
        if (tuple(seed.origin) != (x, y, z) or
                any(math.copysign(1.0, expected) != math.copysign(1.0, actual)
                    for expected, actual in zip(seed.origin, (x, y, z)))):
            raise ValueError("repair origin does not match exact RUNE seed")
        if (not isinstance(count, int) or isinstance(count, bool) or
                not 0 < count <= EVIDENCE_MAX):
            raise ValueError("evidence_count exceeds runtime limit")
        if (not isinstance(duration_ms, int) or isinstance(duration_ms, bool) or
                not 0 < duration_ms <= DURATION_MS_MAX):
            raise ValueError("duration_ms exceeds runtime limit")
        if (not isinstance(surcharge, int) or isinstance(surcharge, bool) or
                not 0 <= surcharge <= SURCHARGE_MAX):
            raise ValueError("surcharge exceeds runtime limit")
        checked.append((seed_index, x, y, z, count, duration_ms, surcharge))

    identity = rune.header.identity
    header = rune.header
    lines = [
        f"snag_format {SNAG_FORMAT}",
        f"map {map_name}",
        f"bsp_checksum {identity.bsp_checksum}",
        f"entity_crc {identity.entity_crc32}",
        f"physics_flags {identity.physics_flags}",
        f"gravity {identity.gravity:.9g}",
        f"airaccelerate {identity.airaccelerate:.9g}",
        f"maxvelocity {identity.maxvelocity:.9g}",
        f"pmove_ms {identity.pmove_substep_ms}",
        f"frame_ms {identity.server_frame_ms}",
        f"host_physics_id {identity.host_physics_id}",
        f"rune_payload_crc {header.payload_crc32}",
        f"rune_header_crc {header.header_crc32}",
        f"rune_action_contract_crc {header.action_contract_crc32}",
        f"rune_mechanism_contract_crc {header.mechanism_contract_crc32}",
        f"rune_num_seeds {header.num_seeds}",
        f"rune_num_links {header.num_links}",
        f"rune_sha256 {rune_sha256}",
        f"evidence_sha256 {evidence_sha256}",
        f"repairs {len(checked)}",
    ]
    for seed_index, x, y, z, count, duration_ms, surcharge in checked:
        lines.append(
            "repair %d %.3f %.3f %.3f %d %d %d" %
            (seed_index, x, y, z, count, duration_ms, surcharge)
        )
    return "\n".join(lines) + "\n"


def validate_file(path, rune, rune_sha256):
    payload = _read_regular(path, MAX_SNAG_FILE_BYTES, "SNAG artifact")
    try:
        text = payload.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ValueError("SNAG artifact is not ASCII") from exc
    lines = text.splitlines()
    if len(lines) < len(SNAG_HEADER_KEYS):
        raise ValueError("SNAG binding is incomplete")

    fields = {}
    for key, line in zip(SNAG_HEADER_KEYS, lines):
        name, separator, value = line.partition(" ")
        if name != key or separator != " " or not value or " " in value:
            raise ValueError(f"SNAG binding field {key} is malformed")
        fields[key] = value
    try:
        repair_count = int(fields["repairs"], 10)
    except ValueError as exc:
        raise ValueError("SNAG repair count is malformed") from exc
    if not 0 <= repair_count <= MAX_RECORDS:
        raise ValueError("SNAG repair count exceeds runtime limit")
    if len(lines) != len(SNAG_HEADER_KEYS) + repair_count:
        raise ValueError("SNAG repair count does not own the complete tail")

    repairs = []
    for line in lines[len(SNAG_HEADER_KEYS):]:
        values = line.split(" ")
        if len(values) != 8 or values[0] != "repair":
            raise ValueError("SNAG repair record is malformed")
        try:
            repairs.append((
                int(values[1], 10),
                float(values[2]),
                float(values[3]),
                float(values[4]),
                int(values[5], 10),
                int(values[6], 10),
                int(values[7], 10),
            ))
        except ValueError as exc:
            raise ValueError("SNAG repair record is malformed") from exc

    expected = render(
        rune.header.map_name,
        rune,
        repairs,
        fields["evidence_sha256"],
        rune_sha256,
    )
    if text != expected:
        raise ValueError("SNAG binding does not match the authenticated RUNE")
    return repair_count


def atomic_write(path, payload):
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(
        prefix=f".{destination.name}.", dir=destination.parent
    )
    try:
        with os.fdopen(fd, "w", encoding="ascii", newline="\n") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
        directory_fd = os.open(destination.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", required=True)
    parser.add_argument("--rune", required=True)
    parser.add_argument("--evidence-manifest", required=True)
    parser.add_argument(
        "--explicit-zero", action="store_true", required=True,
        help=("emit a RUNE-bound repairs=0 bootstrap without claiming that "
              "the map was observed clean"),
    )
    parser.add_argument("--output", required=True)
    args = parser.parse_args(argv)

    rune, rune_sha256 = read_rune_and_sha256(args.rune)
    if rune.header.map_name != args.map:
        parser.error("map does not match authenticated RUNE")
    repairs = []
    atomic_write(
        args.output,
        render(args.map, rune, repairs,
               hash_evidence_manifest(args.evidence_manifest),
               rune_sha256),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
