#!/usr/bin/env python3
"""Turn map-local stall clusters into strict map-repair inputs.

The input is stallcensus JSON lines carrying the exact map identity that
produced each cluster.  Output deliberately has no format selector: it is one
deterministic, map-bound input for the runtime reader.
"""

import argparse
import json
import math
import struct
from pathlib import Path


IDENTITY_KEYS = (
    "bsp_checksum", "entity_crc", "physics_flags", "gravity",
    "airaccelerate", "maxvelocity", "pmove_ms", "frame_ms",
    "host_physics_id",
)
MAX_RECORDS = 64
COORD_LIMIT = 65536.0
EVIDENCE_MAX = 1000000
DURATION_MS_MAX = 86400000
SURCHARGE_MAX = 60000


def _finite_number(value, field):
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise ValueError(f"{field} must be numeric")
    if not math.isfinite(value):
        raise ValueError(f"{field} must be finite")
    return value


def load_rows(paths):
    rows = []
    for path in paths:
        for line in Path(path).read_text(encoding="utf-8").splitlines():
            if line.strip():
                rows.append(json.loads(line))
    return rows


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


def clusters_for_map(rows, map_name):
    clusters = []
    # Refuse to combine rows merely because their map names agree.
    identity_for_map(rows, map_name)
    for row in rows:
        if row.get("map") != map_name:
            continue
        for cluster in row.get("snag_clusters", []):
            x = _finite_number(cluster.get("x"), "x")
            y = _finite_number(cluster.get("y"), "y")
            z = _finite_number(cluster.get("z"), "z")
            count = cluster.get("evidence_count")
            duration = _finite_number(cluster.get("duration_s"), "duration_s")
            if any(abs(value) > COORD_LIMIT for value in (x, y, z)):
                raise ValueError("coordinate exceeds runtime limit")
            if (not isinstance(count, int) or isinstance(count, bool) or
                    not 0 < count <= EVIDENCE_MAX):
                raise ValueError("evidence_count exceeds runtime limit")
            duration_ms = round(duration * 1000.0)
            if not 0 < duration_ms <= DURATION_MS_MAX:
                raise ValueError("duration_s exceeds runtime limit")
            clusters.append((x, y, z, count, duration))
    return canonical_records(clusters)


def canonical_records(clusters):
    """Validate then collapse byte-identical coordinate evidence.

    The runtime rejects duplicate coordinates.  Keeping the canonicalization
    here, before the count limit, makes repeated identical census rows safe
    while refusing a claim that gives one coordinate different evidence.
    """
    records = {}
    for x, y, z, count, duration in clusters:
        for value, field in ((x, "x"), (y, "y"), (z, "z"),
                             (duration, "duration_s")):
            _finite_number(value, field)
        if any(abs(value) > COORD_LIMIT for value in (x, y, z)):
            raise ValueError("coordinate exceeds runtime limit")
        if (not isinstance(count, int) or isinstance(count, bool) or
                not 0 < count <= EVIDENCE_MAX):
            raise ValueError("evidence_count exceeds runtime limit")
        if not 0 < round(duration * 1000.0) <= DURATION_MS_MAX:
            raise ValueError("duration_s exceeds runtime limit")
        # The reader sees the emitted three-decimal coordinates, so this key
        # is also the runtime's duplicate coordinate identity.  Normalize
        # signed zero explicitly: the C reader correctly treats -0.0 and 0.0
        # as the same coordinate, and the producer must do so before writing.
        emitted = tuple(struct.unpack("<f", struct.pack("<f", float(f"{value:.3f}")))[0]
                        for value in (x, y, z))
        emitted = tuple(0.0 if value == 0.0 else value for value in emitted)
        key = emitted
        duration_ms = round(duration * 1000.0)
        record = (*emitted, count, duration_ms / 1000.0)
        if (key in records and
                (records[key][3], round(records[key][4] * 1000.0)) !=
                (count, duration_ms)):
            raise ValueError("conflicting repair coordinate")
        if key not in records or record < records[key]:
            records[key] = record
    if not records or len(records) > MAX_RECORDS:
        raise ValueError("repair record count exceeds runtime limit")
    return sorted(records.values())


def render(map_name, identity, clusters, radius, surcharge):
    if not clusters:
        raise ValueError(f"{map_name} has no snag clusters")
    if not (math.isfinite(radius) and 1.0 <= radius <= 512.0):
        raise ValueError("radius must be in [1, 512]")
    if not isinstance(surcharge, int) or surcharge < 0 or surcharge > SURCHARGE_MAX:
        raise ValueError("surcharge must be an integer in runtime range")
    clusters = canonical_records(clusters)
    identity = validate_identity(identity)
    lines = [f"map {map_name}"]
    for key in IDENTITY_KEYS:
        lines.append(f"{key} {identity[key]:.9g}" if isinstance(identity[key], float)
                     else f"{key} {identity[key]}")
    # Sorting makes equivalent census input order produce byte-identical
    # repairs.  A cluster's declared evidence and duration remain evidence,
    # not an identity for a player or entity.
    for x, y, z, count, duration in clusters:
        duration_ms = round(duration * 1000.0)
        lines.append("repair %.3f %.3f %.3f %.3f %d %d %d" % (
            x, y, z, radius, count, duration_ms, surcharge))
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("census", nargs="+")
    parser.add_argument("--map", required=True)
    parser.add_argument("--radius", type=float, default=64.0)
    parser.add_argument("--surcharge", type=int, default=1000)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    rows = load_rows(args.census)
    payload = render(args.map, identity_for_map(rows, args.map),
                     clusters_for_map(rows, args.map),
                     args.radius, args.surcharge)
    Path(args.output).write_text(payload, encoding="ascii")


if __name__ == "__main__":
    main()
