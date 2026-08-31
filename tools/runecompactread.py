#!/usr/bin/env python3
"""Independent structural reader for canonical compact RUNE wire images.

It validates the wire envelope and structural records, but deliberately does
not reconstruct the complete model.  Callers may optionally bind the complete
canonical identity record.
"""
from __future__ import annotations

import argparse
import binascii
import json
from dataclasses import dataclass
import os
from pathlib import Path
import stat
import struct
import sys


MAGIC = b"SGRCW001"
WIRE_VERSION = MODEL_VERSION = ANALYTIC_VERSION = 1
MODEL_SCHEMA_TAG = 0x4D434E52
ALIGNMENT = 8
HEADER_FIXED_BYTES = 48
DESCRIPTOR_BYTES = 24
CHECKSUM_OFFSET = 24

# (stable name, encoded record size, maximum record count), in wire order.
SECTION_SPECS = (
    ("identity", 252, 1),
    ("cells", 80, 1_048_576),
    ("facets", 56, 4_194_304),
    ("incidences", 20, 8_388_608),
    ("cell_incidences", 4, 8_388_608),
    ("vertices", 12, 16_777_216),
    ("portals", 44, 2_097_152),
    ("movement_fields", 24, 4_194_304),
    ("weapon_regions", 20, 4_194_304),
    ("weapon_profiles", 8, 256),
    ("weapon_kernels", 20, 8_388_608),
    ("analytic_function_refs", 4, 33_554_432),
    ("analytic_functions", 20, 1_048_576),
    ("analytic_input_dimensions", 4, 16_777_216),
    ("analytic_constants", 4, 1_048_576),
    ("analytic_affines", 12, 1_048_576),
    ("analytic_affine_slopes", 4, 16_777_216),
    ("analytic_polynomials", 12, 1_048_576),
    ("analytic_polynomial_coefficients", 4, 33_554_432),
    ("analytic_ballistics", 12, 1_048_576),
    ("analytic_piecewise", 16, 1_048_576),
    ("analytic_piecewise_clauses", 16, 4_194_304),
    ("mechanisms", 100, 1_048_576),
    ("mechanism_edges", 16, 4_194_304),
    ("landmarks", 60, 4_194_304),
    ("landmark_cells", 4, 16_777_216),
    ("facet_annotations", 8, 4_194_304),
    ("portal_mechanisms", 15, 4_194_304),
)
HEADER_BYTES = HEADER_FIXED_BYTES + DESCRIPTOR_BYTES * len(SECTION_SPECS)
IDENTITY_BYTES = SECTION_SPECS[0][1]
WEAPON_RESPONSE_FAMILY_COUNT = 12
WEAPON_RESPONSE_FAMILIES_ALL = (1 << WEAPON_RESPONSE_FAMILY_COUNT) - 1


class RuneCompactError(ValueError):
    """A compact RUNE wire-format rejection."""


@dataclass(frozen=True)
class Section:
    name: str
    record_bytes: int
    count: int
    offset: int


@dataclass(frozen=True)
class SourceCounts:
    model_count: int
    leaf_count: int
    area_count: int
    plane_count: int
    brush_count: int
    brush_side_count: int
    entity_count: int


@dataclass(frozen=True)
class Hull:
    mins: tuple[int, int, int]
    maxs: tuple[int, int, int]


@dataclass(frozen=True)
class Physics:
    gravity_bits: int
    ground_acceleration_bits: int
    air_acceleration_bits: int
    water_acceleration_bits: int
    hook_acceleration_bits: int
    external_acceleration_bits: int
    water_drag_bits: int
    max_velocity_bits: int
    frame_ms: int
    substep_ms: int


@dataclass(frozen=True)
class Identity:
    bsp_sha256: bytes
    bsp_bytes: int
    bsp_checksum: int
    entity_crc32: int
    entity_semantics_id: int
    physics_abi_id: int
    collision_law_id: int
    pmove_law_id: int
    gravity_law_id: int
    hook_law_id: int
    mechanism_law_id: int
    weapon_law_id: int
    construction_id: int
    schema_id: int
    producer_identity: int
    source_counts: SourceCounts
    standing_hull: Hull
    crouching_hull: Hull
    physics: Physics

    def summary(self) -> dict[str, object]:
        return {
            "bsp_sha256": self.bsp_sha256.hex(),
            "bsp_bytes": self.bsp_bytes,
            "bsp_checksum": self.bsp_checksum,
            "entity_crc32": self.entity_crc32,
            "entity_semantics_id": self.entity_semantics_id,
            "physics_abi_id": self.physics_abi_id,
            "collision_law_id": self.collision_law_id,
            "pmove_law_id": self.pmove_law_id,
            "gravity_law_id": self.gravity_law_id,
            "hook_law_id": self.hook_law_id,
            "mechanism_law_id": self.mechanism_law_id,
            "weapon_law_id": self.weapon_law_id,
            "construction_id": self.construction_id,
            "schema_id": self.schema_id,
            "producer_identity": self.producer_identity,
            "source_counts": self.source_counts.__dict__,
            "standing_hull": self.standing_hull.__dict__,
            "crouching_hull": self.crouching_hull.__dict__,
            "physics": self.physics.__dict__,
        }


@dataclass(frozen=True)
class CompactRuneSummary:
    wire_version: int
    model_version: int
    analytic_version: int
    schema_tag: int
    image_bytes: int
    checksum: int
    identity: Identity
    sections: tuple[Section, ...]

    @property
    def counts(self) -> dict[str, int]:
        return {section.name: section.count for section in self.sections}

    def as_dict(self) -> dict[str, object]:
        return {
            "wire_version": self.wire_version,
            "model_version": self.model_version,
            "analytic_version": self.analytic_version,
            "schema_tag": self.schema_tag,
            "image_bytes": self.image_bytes,
            "checksum": self.checksum,
            "identity": self.identity.summary(),
            "counts": self.counts,
        }


def _reject(code: str) -> None:
    raise RuneCompactError(code)


def _u16(data: memoryview, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data: memoryview, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _u64(data: memoryview, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def _i32s(data: memoryview, offset: int) -> tuple[int, int, int]:
    return struct.unpack_from("<iii", data, offset)


def _aligned(value: int) -> int:
    return (value + ALIGNMENT - 1) & -ALIGNMENT


def _maximum_canonical_image_bytes() -> int:
    cursor = HEADER_BYTES
    for _, record_bytes, limit in SECTION_SPECS:
        cursor = _aligned(cursor)
        cursor += record_bytes * limit
    return _aligned(cursor)


MAX_CANONICAL_IMAGE_BYTES = _maximum_canonical_image_bytes()
READ_CHUNK_BYTES = 1 << 20


def _all_zero(data: memoryview, start: int, end: int) -> bool:
    return not any(data[start:end])


def _checksum(data: memoryview) -> int:
    crc = binascii.crc32(data[:CHECKSUM_OFFSET])
    crc = binascii.crc32(b"\0" * 4, crc)
    return binascii.crc32(data[CHECKSUM_OFFSET + 4:], crc) & 0xFFFFFFFF


def _identity(data: memoryview, offset: int) -> Identity:
    source_counts = SourceCounts(*struct.unpack_from("<7I", data, offset + 136))
    standing_hull = Hull(_i32s(data, offset + 164), _i32s(data, offset + 176))
    crouching_hull = Hull(_i32s(data, offset + 188), _i32s(data, offset + 200))
    return Identity(
        bytes(data[offset:offset + 32]), _u64(data, offset + 32),
        _u32(data, offset + 40), _u32(data, offset + 44),
        *struct.unpack_from("<11Q", data, offset + 48), source_counts,
        standing_hull, crouching_hull,
        Physics(*struct.unpack_from("<10I", data, offset + 212)),
    )


def _span(data: memoryview, offset: int, total: int) -> bool:
    first, count = _u32(data, offset), _u32(data, offset + 4)
    return first <= total and count <= total - first


def _ref(value: int, total: int, allow_none: bool = False) -> bool:
    return value < total or (allow_none and value == 0xFFFFFFFF)


def _source(data: memoryview, offset: int, facet_count: int) -> bool:
    kind = _u32(data, offset)
    if kind == 0:
        return _all_zero(data, offset + 12, offset + 20)
    if kind == 1:
        return _all_zero(data, offset + 16, offset + 20)
    if kind == 2:
        return True
    return kind == 3 and _ref(_u32(data, offset + 4), facet_count) and \
        _all_zero(data, offset + 12, offset + 20)


def _expected_identity(value: object | None) -> memoryview | None:
    if value is None:
        return None
    try:
        identity = memoryview(value).cast("B")
    except (TypeError, ValueError):
        _reject("invalid-argument")
    if len(identity) != IDENTITY_BYTES:
        _reject("invalid-argument")
    return identity


def _validate_records(data: memoryview, sections: tuple[Section, ...]) -> None:
    """Validate the codec's wire structure, not complete model semantics."""
    counts = tuple(section.count for section in sections)

    def records(section: int):
        descriptor = sections[section]
        return (descriptor.offset + record * descriptor.record_bytes
                for record in range(descriptor.count))

    for p in records(1):
        if not (_span(data, p + 44, counts[4]) and _span(data, p + 52, counts[7])
                and _span(data, p + 60, counts[8])):
            _reject("invalid-span")
        if not _all_zero(data, p + 77, p + 80):
            _reject("nonzero-reserved")
        if (_u32(data, p + 68) & ~0x1FFF or _u32(data, p + 72) & ~0x0F
                or data[p + 76] & ~0x03):
            _reject("invalid-format")
    for p in records(2):
        if not _source(data, p, counts[2]):
            _reject("invalid-reference")
        if not (_span(data, p + 36, counts[5]) and _span(data, p + 44, counts[3])):
            _reject("invalid-span")
        if not _ref(_u32(data, p + 52), counts[6], True):
            _reject("invalid-reference")
    for p in records(3):
        if not (_ref(_u32(data, p), counts[1]) and _ref(_u32(data, p + 4), counts[2])):
            _reject("invalid-reference")
        if _u32(data, p + 12) >= 2 or _u32(data, p + 16) >= 2:
            _reject("invalid-format")
    for p in records(4):
        if not _ref(_u32(data, p), counts[3]):
            _reject("invalid-reference")
    for p in records(6):
        if not (_source(data, p, counts[2]) and _ref(_u32(data, p + 20), counts[2])
                and _ref(_u32(data, p + 24), counts[3])
                and _ref(_u32(data, p + 28), counts[3])):
            _reject("invalid-reference")
        if _u32(data, p + 36) >= 3 or data[p + 40] & ~0x03:
            _reject("invalid-format")
        if not _all_zero(data, p + 41, p + 44):
            _reject("nonzero-reserved")
    for p in records(7):
        if not (_ref(_u32(data, p), counts[1])
                and _ref(_u32(data, p + 4), counts[6], True)):
            _reject("invalid-reference")
        if _u32(data, p + 8) >= 6 or data[p + 12] & ~0x03:
            _reject("invalid-format")
        if not _all_zero(data, p + 13, p + 16):
            _reject("nonzero-reserved")
        if not _span(data, p + 16, counts[11]):
            _reject("invalid-span")
    for p in records(8):
        if not _ref(_u32(data, p), counts[1]):
            _reject("invalid-reference")
        if not (_span(data, p + 4, counts[4]) and _span(data, p + 12, counts[10])):
            _reject("invalid-span")
    profile_masks = []
    for p in records(9):
        source_profile, family_mask = _u32(data, p), _u32(data, p + 4)
        if (source_profile == 0 or family_mask == 0
                or family_mask & ~WEAPON_RESPONSE_FAMILIES_ALL):
            _reject("invalid-format")
        profile_masks.append(family_mask)
    profile_masks = tuple(profile_masks)
    for p in records(10):
        profile = _u32(data, p + 4)
        if not (_ref(_u32(data, p), counts[8]) and _ref(profile, counts[9])):
            _reject("invalid-reference")
        family = _u32(data, p + 8)
        if family >= WEAPON_RESPONSE_FAMILY_COUNT:
            _reject("invalid-format")
        if not _span(data, p + 12, counts[11]):
            _reject("invalid-span")
        if not profile_masks[profile] & (1 << family):
            _reject("invalid-reference")
    for p in records(11):
        if not _ref(_u32(data, p), counts[12]):
            _reject("invalid-reference")
    definitions = (counts[14], counts[15], counts[17], counts[19], counts[20])
    for p in records(12):
        if not _span(data, p, counts[13]) or _u32(data, p + 4) > 16:
            _reject("invalid-span")
        form = _u32(data, p + 16)
        if _u32(data, p + 12) >= 20 or form >= len(definitions):
            _reject("invalid-format")
        if not _ref(_u32(data, p + 8), definitions[form]):
            _reject("invalid-reference")
    for p in records(13):
        if _u32(data, p) >= 16:
            _reject("invalid-format")
    for p in records(15):
        if not _span(data, p + 4, counts[16]):
            _reject("invalid-span")
    for p in records(17):
        if not _span(data, p, counts[18]):
            _reject("invalid-span")
        if not _all_zero(data, p + 9, p + 12):
            _reject("nonzero-reserved")
    for p in records(20):
        if not _span(data, p, counts[21]):
            _reject("invalid-span")
        if not _ref(_u32(data, p + 8), counts[12]):
            _reject("invalid-reference")
    for p in records(21):
        if not _ref(_u32(data, p + 8), counts[12]):
            _reject("invalid-reference")
        if _u32(data, p + 12) >= 4:
            _reject("invalid-format")
    for p in records(22):
        if not (_ref(_u32(data, p + 8), counts[1]) and _ref(_u32(data, p + 12), counts[1])
                and _ref(_u32(data, p + 16), counts[24], True)):
            _reject("invalid-reference")
        if not _span(data, p + 44, counts[23]):
            _reject("invalid-span")
        if any(_u32(data, p + offset) >= limit for offset, limit in
               ((72, 8), (76, 5), (80, 5), (84, 5), (88, 5), (92, 3))):
            _reject("invalid-format")
        if data[p + 96] & ~0x03:
            _reject("invalid-format")
        if not _all_zero(data, p + 97, p + 100):
            _reject("nonzero-reserved")
    for p in records(23):
        if _u32(data, p + 12) >= 5:
            _reject("invalid-format")
    for p in records(24):
        if not _span(data, p + 4, counts[25]):
            _reject("invalid-span")
        if not _ref(_u32(data, p + 12), counts[22], True):
            _reject("invalid-reference")
        if _u32(data, p + 52) >= 13:
            _reject("invalid-format")
        if _u16(data, p + 58):
            _reject("nonzero-reserved")
    for p in records(25):
        if not _ref(_u32(data, p), counts[1]):
            _reject("invalid-reference")
    for p in records(26):
        if not _ref(_u32(data, p), counts[2]):
            _reject("invalid-reference")
        if _u16(data, p + 4) & ~0xFF or data[p + 6] & ~0x03:
            _reject("invalid-format")
        if data[p + 7]:
            _reject("nonzero-reserved")
    for p in records(27):
        if not (_ref(_u32(data, p), counts[6]) and _ref(_u32(data, p + 4), counts[22])):
            _reject("invalid-reference")
        if _u32(data, p + 8) >= 4:
            _reject("invalid-format")
        if not _all_zero(data, p + 12, p + 15):
            _reject("nonzero-reserved")


def inspect(data: bytes | bytearray | memoryview, *,
            expected_identity: object | None = None) -> CompactRuneSummary:
    """Validate an image and optionally bind its complete identity record."""
    expected = _expected_identity(expected_identity)
    try:
        image = memoryview(data).cast("B")
    except TypeError as error:
        raise TypeError("compact RUNE image must be bytes-like") from error
    size = len(image)
    if size > MAX_CANONICAL_IMAGE_BYTES:
        _reject("limit-exceeded")
    if size < HEADER_FIXED_BYTES:
        _reject("truncated")
    if bytes(image[:8]) != MAGIC:
        _reject("invalid-format")
    if (_u16(image, 8) != WIRE_VERSION or _u16(image, 32) != MODEL_VERSION
            or _u32(image, 36) != MODEL_SCHEMA_TAG
            or _u16(image, 40) != ANALYTIC_VERSION):
        _reject("unsupported-version")
    if (_u16(image, 10) != HEADER_BYTES
            or _u32(image, 12) != len(SECTION_SPECS)):
        _reject("invalid-format")
    if size < HEADER_BYTES:
        _reject("truncated")
    total = _u64(image, 16)
    if total != size:
        _reject("truncated" if total > size else "invalid-format")
    if (_u32(image, 28) or _u16(image, 34) or _u16(image, 42)
            or _u32(image, 44)):
        _reject("nonzero-reserved")

    cursor = HEADER_BYTES
    sections = []
    for number, (name, record_bytes, limit) in enumerate(SECTION_SPECS):
        descriptor = HEADER_FIXED_BYTES + number * DESCRIPTOR_BYTES
        if (_u32(image, descriptor) != number
                or _u32(image, descriptor + 4) != record_bytes):
            _reject("invalid-section")
        if _u32(image, descriptor + 12):
            _reject("nonzero-reserved")
        count, offset = _u32(image, descriptor + 8), _u64(image, descriptor + 16)
        if count > limit or (number == 0 and count != 1):
            _reject("limit-exceeded")
        aligned = _aligned(cursor)
        if offset != aligned or offset > total:
            _reject("invalid-section")
        if not _all_zero(image, cursor, aligned):
            _reject("nonzero-reserved")
        end = offset + count * record_bytes
        if end > total:
            _reject("overflow")
        sections.append(Section(name, record_bytes, count, offset))
        cursor = end

    aligned = _aligned(cursor)
    if aligned != total or not _all_zero(image, cursor, total):
        _reject("nonzero-reserved")
    if _u32(image, CHECKSUM_OFFSET) != _checksum(image):
        _reject("checksum-mismatch")
    _validate_records(image, tuple(sections))
    identity_offset = sections[0].offset
    if (expected is not None
            and image[identity_offset:identity_offset + IDENTITY_BYTES] != expected):
        _reject("identity-mismatch")
    return CompactRuneSummary(
        _u16(image, 8), _u16(image, 32), _u16(image, 40), _u32(image, 36),
        total, _u32(image, CHECKSUM_OFFSET), _identity(image, identity_offset),
        tuple(sections),
    )


def read(path: str | Path, *, expected_identity: object | None = None) -> CompactRuneSummary:
    artifact = Path(path)
    flags = (os.O_RDONLY | getattr(os, "O_BINARY", 0) |
             getattr(os, "O_NONBLOCK", 0))
    descriptor = os.open(artifact, flags)
    try:
        opened = os.fstat(descriptor)
        if not stat.S_ISREG(opened.st_mode):
            _reject("unsupported-file")
        size = opened.st_size
        if size > MAX_CANONICAL_IMAGE_BYTES:
            _reject("limit-exceeded")
        data = bytearray()
        remaining = size + 1
        while remaining:
            block = os.read(descriptor, min(READ_CHUNK_BYTES, remaining))
            if not block:
                break
            data.extend(block)
            remaining -= len(block)
        after = os.fstat(descriptor)
    finally:
        os.close(descriptor)
    if (len(data) > MAX_CANONICAL_IMAGE_BYTES or
            after.st_size > MAX_CANONICAL_IMAGE_BYTES):
        _reject("limit-exceeded")
    if size != after.st_size or len(data) != size:
        _reject("changed")
    return inspect(data, expected_identity=expected_identity)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=Path)
    arguments = parser.parse_args(argv)
    try:
        summary = read(arguments.artifact)
    except OSError as error:
        print(f"runecompactread.py: {error}", file=sys.stderr)
        return 2
    except RuneCompactError as error:
        print(f"runecompactread.py: reject: {error}", file=sys.stderr)
        return 1
    print(json.dumps(summary.as_dict(), separators=(",", ":"), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
