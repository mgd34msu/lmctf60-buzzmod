#!/usr/bin/env python3
"""Independent validator for the little-endian RUNE v2 artifact format."""
from __future__ import annotations

import argparse
import binascii
import json
import math
from pathlib import Path
import struct
import sys
from dataclasses import dataclass


MAGIC = 0x324E5552
VERSION = 2
ENDIAN = 0x0102
SCHEMA_REVISION = 3
HEADER_BYTES = 64
ENTRY_BYTES = 32
SECTION_COUNT = 13
ALIGNMENT = 8
MAX_ARTIFACT_BYTES = 1 << 32
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
NONE_ID = (UINT64_MAX, UINT64_MAX, UINT64_MAX)

RECORD_BYTES = (0, 256, 64, 12, 136, 136, 164, 172, 132, 104, 332,
                188, 160, 64)
MAX_COUNTS = (0, 1, 4_194_304, 8_388_608, 262_144, 4_194_304,
              1_048_576, 2_097_152, 2_097_152, 2_097_152, 4_194_304,
              65_536, 65_536, 1)

DOMAIN_CELL = 1
DOMAIN_PORTAL = 2
DOMAIN_PLANE = 3
DOMAIN_PHASE = 4
DOMAIN_TRANSITION = 5
DOMAIN_SURFACE = 6
DOMAIN_AFFORDANCE = 7
DOMAIN_KERNEL = 8
DOMAIN_LANDMARK = 9
DOMAIN_MECHANISM = 10
DOMAIN_COUNT = 11

CONTENTS_KNOWN = 0x1FFF
CELL_SEMANTICS_KNOWN = 0x0F
SURFACE_SEMANTICS_KNOWN = 0x1F
PORTAL_FLAGS_KNOWN = 0x0F
KERNEL_FLAGS_KNOWN = 0x1F
MODEL_FLAGS_REQUIRED = 0x07


class RuneV2Error(ValueError):
    """A fail-closed wire or semantic rejection."""


@dataclass(frozen=True)
class Section:
    section_type: int
    element_bytes: int
    count: int
    offset: int
    byte_count: int


def _reject(code: str) -> None:
    raise RuneV2Error(code)


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def _f32(data: bytes, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def _crc32(data: bytes) -> int:
    return binascii.crc32(data) & UINT32_MAX


def _aligned(value: int) -> int:
    return (value + ALIGNMENT - 1) & ~(ALIGNMENT - 1)


def _record(data: bytes, section: Section, index: int) -> int:
    return section.offset + index * section.element_bytes


def _stable_id(data: bytes, offset: int) -> tuple[int, int, int]:
    return (_u64(data, offset), _u64(data, offset + 8),
            _u64(data, offset + 16))


def _order(data: bytes, offset: int) -> tuple[int, int, int, int, int]:
    return (_u64(data, offset), _u32(data, offset + 8),
            _u32(data, offset + 12), _u32(data, offset + 16),
            _u32(data, offset + 20))


def _id_from_order(order: tuple[int, int, int, int, int]) -> tuple[int, int, int]:
    source, domain, source_index, ordinal, variant = order
    return (source, (domain << 32) | source_index, (ordinal << 32) | variant)


def _id_valid(stable_id: tuple[int, int, int]) -> bool:
    source, high, low = stable_id
    domain = high >> 32
    return (source not in (0, UINT64_MAX) and DOMAIN_CELL <= domain < DOMAIN_COUNT
            and (high & UINT32_MAX) != UINT32_MAX
            and (low >> 32) != UINT32_MAX and (low & UINT32_MAX) != UINT32_MAX)


def _record_id(data: bytes, offset: int, domain: int, source: int,
               previous: tuple[int, int, int, int, int] | None) -> tuple[int, int, int, int, int]:
    stable_id = _stable_id(data, offset)
    order = _order(data, offset + 24)
    if (not _id_valid(stable_id) or order[0] != source or order[1] != domain
            or order[0] in (0, UINT64_MAX) or any(value == UINT32_MAX for value in order[2:])
            or stable_id != _id_from_order(order)):
        _reject("bad-reference")
    if previous is not None and order <= previous:
        _reject("bad-order")
    return order


def _finite_values(data: bytes, offset: int, count: int) -> tuple[float, ...]:
    values = tuple(_f32(data, offset + index * 4) for index in range(count))
    if not all(math.isfinite(value) for value in values):
        _reject("nonfinite")
    return values


def _interval(data: bytes, offset: int, nonnegative: bool) -> tuple[float, float]:
    minimum, maximum = _finite_values(data, offset, 2)
    if minimum > maximum or (nonnegative and minimum < 0.0):
        _reject("bad-domain")
    return minimum, maximum


def _interval3(data: bytes, offset: int, nonnegative: bool) -> tuple[tuple[float, float], ...]:
    return tuple(_interval(data, offset + axis * 8, nonnegative) for axis in range(3))


def _bounds(data: bytes, offset: int) -> tuple[tuple[float, ...], tuple[float, ...]]:
    minimum = _finite_values(data, offset, 3)
    maximum = _finite_values(data, offset + 12, 3)
    if any(minimum[axis] >= maximum[axis] for axis in range(3)):
        _reject("bad-domain")
    return minimum, maximum


def _span(data: bytes, offset: int, total: int, minimum: int = 0,
          maximum: int = UINT32_MAX) -> tuple[int, int]:
    first, count = _u32(data, offset), _u32(data, offset + 4)
    if count < minimum or count > maximum:
        _reject("hostile-count")
    if first > total or count > total - first:
        _reject("bad-reference")
    return first, count


def _find(records: dict[tuple[int, int, int], int], stable_id: tuple[int, int, int],
          allow_none: bool = False) -> int | None:
    if allow_none and stable_id == NONE_ID:
        return None
    try:
        return records[stable_id]
    except KeyError:
        _reject("bad-reference")
    return None


def _parse_header(data: bytes) -> tuple[int, list[Section]]:
    if len(data) < HEADER_BYTES:
        _reject("truncated")
    if (_u32(data, 0) != MAGIC or _u16(data, 8) != HEADER_BYTES
            or _u16(data, 10) != ENTRY_BYTES or _u32(data, 12) != SECTION_COUNT
            or _u32(data, 16) != 0 or _u32(data, 20) != SCHEMA_REVISION
            or any(data[48:64])):
        _reject("bad-header")
    if _u16(data, 4) != VERSION:
        _reject("bad-version")
    if _u16(data, 6) != ENDIAN:
        _reject("bad-endian")
    generation = _u64(data, 24)
    total_bytes = _u64(data, 32)
    if generation == 0 or total_bytes != len(data) or total_bytes > MAX_ARTIFACT_BYTES:
        _reject("bad-size")
    header = bytearray(data[:HEADER_BYTES])
    struct.pack_into("<I", header, 44, 0)
    if _crc32(header) != _u32(data, 44):
        _reject("bad-header-crc")
    directory_end = HEADER_BYTES + SECTION_COUNT * ENTRY_BYTES
    if directory_end > total_bytes:
        _reject("bad-size")
    sections: list[Section] = []
    previous_end = directory_end
    for index in range(SECTION_COUNT):
        entry = HEADER_BYTES + index * ENTRY_BYTES
        section_type = _u16(data, entry)
        flags = _u16(data, entry + 2)
        element_bytes = _u32(data, entry + 4)
        count = _u32(data, entry + 8)
        section_crc = _u32(data, entry + 12)
        offset = _u64(data, entry + 16)
        byte_count = _u64(data, entry + 24)
        expected_offset = _aligned(previous_end)
        if count > MAX_COUNTS[index + 1]:
            _reject("hostile-count")
        if (section_type != index + 1 or flags != 1
                or element_bytes != RECORD_BYTES[index + 1]
                or (section_type in (1, 13) and count != 1)):
            _reject("bad-section")
        if (byte_count != element_bytes * count or offset != expected_offset
                or offset > total_bytes or byte_count > total_bytes - offset):
            _reject("bad-section")
        if any(data[previous_end:offset]):
            _reject("bad-section")
        if _crc32(data[offset:offset + byte_count]) != section_crc:
            _reject("bad-section-crc")
        sections.append(Section(section_type, element_bytes, count, offset,
                                byte_count))
        previous_end = offset + byte_count
    expected_total = _aligned(previous_end)
    if total_bytes != expected_total or any(data[previous_end:total_bytes]):
        _reject("bad-size")
    if _crc32(data[HEADER_BYTES:]) != _u32(data, 40):
        _reject("bad-payload-crc")
    return generation, sections


def _validate_model(data: bytes, sections: list[Section]) -> tuple[int, int, int, int, tuple[float, ...]]:
    model = sections[0].offset
    cells = sections[5].count
    portals = sections[6].count
    if (_u16(data, model) != 2 or _u16(data, model + 2) != 0
            or _u32(data, model + 4) != 0x32554E52
            or _u32(data, model + 8) != MODEL_FLAGS_REQUIRED
            or _u32(data, model + 12) != 0 or _u32(data, model + 188) != 0
            or _u32(data, model + 252) != 0):
        _reject("bad-record")
    bsp_id = _u64(data, model + 16)
    entity_id = _u64(data, model + 24)
    physics_id = _u64(data, model + 32)
    source = _u64(data, model + 40)
    schema_id = _u64(data, model + 48)
    producer = _u64(data, model + 56)
    if (0 in (bsp_id, entity_id, physics_id, source, schema_id, producer)
            or source == UINT64_MAX):
        _reject("bad-identity")
    _bounds(data, model + 64)
    _bounds(data, model + 88)
    physics = _finite_values(data, model + 112, 8)
    if (any(value < 0.0 for value in physics[:7]) or physics[7] <= 0.0):
        _reject("bad-domain")
    frame_ms, substep_ms = _u32(data, model + 144), _u32(data, model + 148)
    if frame_ms == 0 or substep_ms == 0 or substep_ms > frame_ms:
        _reject("bad-domain")
    state, reason = _u32(data, model + 152), _u32(data, model + 156)
    expected_cells, expected_portals = _u32(data, model + 160), _u32(data, model + 164)
    covered_cells, covered_portals = _u32(data, model + 168), _u32(data, model + 172)
    if (state != 2 or reason != 0 or expected_cells != cells
            or expected_portals != portals or covered_cells != cells
            or covered_portals != portals or cells == 0
            or _u32(data, model + 176) != UINT32_MAX):
        _reject("incomplete")
    if (_u32(data, model + 180) != 1 or _u32(data, model + 184) != 0
            or _u64(data, model + 192) in (0, producer)
            or _u64(data, model + 200) != bsp_id
            or _u64(data, model + 208) != source
            or _u64(data, model + 216) == 0
            or _u32(data, model + 224) == 0
            or _u32(data, model + 228) != cells
            or _u32(data, model + 232) != portals
            or any(_u32(data, model + offset) != 0
                   for offset in (236, 240, 244, 248))):
        _reject("incomplete")
    return source, bsp_id, schema_id, physics_id, physics


def _ids_for(data: bytes, section: Section, domain: int,
             source: int) -> dict[tuple[int, int, int], int]:
    result: dict[tuple[int, int, int], int] = {}
    previous = None
    for index in range(section.count):
        offset = _record(data, section, index)
        previous = _record_id(data, offset, domain, source, previous)
        stable_id = _stable_id(data, offset)
        if stable_id in result:
            _reject("bad-order")
        result[stable_id] = index
    return result


def _validate_records(data: bytes, sections: list[Section], source: int,
                      physics_id: int, physics: tuple[float, ...]) -> None:
    planes = _ids_for(data, sections[1], DOMAIN_PLANE, source)
    phases = _ids_for(data, sections[3], DOMAIN_PHASE, source)
    transitions = _ids_for(data, sections[4], DOMAIN_TRANSITION, source)
    cells = _ids_for(data, sections[5], DOMAIN_CELL, source)
    portals = _ids_for(data, sections[6], DOMAIN_PORTAL, source)
    surfaces = _ids_for(data, sections[7], DOMAIN_SURFACE, source)
    affordances = _ids_for(data, sections[8], DOMAIN_AFFORDANCE, source)
    kernels = _ids_for(data, sections[9], DOMAIN_KERNEL, source)
    landmarks = _ids_for(data, sections[10], DOMAIN_LANDMARK, source)
    mechanisms = _ids_for(data, sections[11], DOMAIN_MECHANISM, source)
    del kernels

    for index in range(sections[1].count):
        record = _record(data, sections[1], index)
        normal = _finite_values(data, record + 48, 3)
        _finite_values(data, record + 60, 1)
        if sum(value * value for value in normal) <= 0.0:
            _reject("bad-domain")

    phase_values: list[dict[str, object]] = []
    for index in range(sections[3].count):
        record = _record(data, sections[3], index)
        stance, motion, support = (_u32(data, record + 48), _u32(data, record + 52),
                                   _u32(data, record + 56))
        medium, void_relation, frame = (_u32(data, record + 60),
                                        _u32(data, record + 64),
                                        _u32(data, record + 68))
        mover = _stable_id(data, record + 72)
        velocity = _interval3(data, record + 96, False)
        elapsed = _interval(data, record + 120, True)
        quantum, horizon = _u32(data, record + 128), _u32(data, record + 132)
        if (stance >= 2 or motion >= 3 or support >= 3 or medium >= 4
                or void_relation >= 2 or frame >= 2 or quantum == 0
                or horizon < quantum):
            _reject("bad-domain")
        if ((motion == 0 and support == 0) or (motion == 1 and support != 0)
                or (motion == 2 and (medium not in (1, 2, 3) or support != 0))
                or (support == 2 and frame != 1)):
            _reject("bad-domain")
        if frame == 0:
            if mover != NONE_ID:
                _reject("bad-reference")
        elif mover == NONE_ID or mover[1] >> 32 != DOMAIN_MECHANISM or support != 2:
            _reject("bad-reference")
        phase_values.append({"id": _stable_id(data, record), "stance": stance,
                             "motion": motion, "support": support,
                             "medium": medium, "void": void_relation,
                             "frame": frame, "mover": mover,
                             "velocity": velocity, "elapsed": elapsed,
                             "quantum": quantum, "horizon": horizon})

    cell_values: list[dict[str, object]] = []
    counts = [section.count for section in sections]
    for index in range(sections[5].count):
        record = _record(data, sections[5], index)
        if (_u64(data, record + 48) != source or _u32(data, record + 56) == UINT32_MAX
                or _u32(data, record + 60) == UINT32_MAX):
            _reject("bad-reference")
        bounds = _bounds(data, record + 64)
        spans = (
            _span(data, record + 88, counts[1], 4, 64),
            _span(data, record + 96, counts[3], 1, 32),
            _span(data, record + 104, counts[7], 0, 128),
            _span(data, record + 112, counts[8], 0, 128),
            _span(data, record + 120, counts[9], 0, 128),
            _span(data, record + 128, counts[10], 0, 64),
            _span(data, record + 136, counts[11], 0, 64),
        )
        if (any(_u32(data, record + offset) == UINT32_MAX for offset in (144, 148, 152))
                or _u32(data, record + 156) & ~CONTENTS_KNOWN
                or _u32(data, record + 160) & ~CELL_SEMANTICS_KNOWN):
            _reject("bad-domain")
        cell_values.append({"id": _stable_id(data, record), "bounds": bounds,
                            "spans": spans})

    for index in range(sections[4].count):
        record = _record(data, sections[4], index)
        cell_index = _find(cells, _stable_id(data, record + 48))
        source_phase = _find(phases, _stable_id(data, record + 72))
        destination_phase = _find(phases, _stable_id(data, record + 96))
        assert cell_index is not None and source_phase is not None and destination_phase is not None
        kind = _u32(data, record + 120)
        duration = _interval(data, record + 124, True)
        if source_phase == destination_phase or not 1 <= kind < 8 or duration[1] <= 0.0 \
                or _u32(data, record + 132) != 0:
            _reject("bad-domain")
        phase_span = cell_values[cell_index]["spans"][1]
        if not (phase_span[0] <= source_phase < phase_span[0] + phase_span[1]
                and phase_span[0] <= destination_phase < phase_span[0] + phase_span[1]):
            _reject("bad-reference")
        source_value = phase_values[source_phase]
        destination_value = phase_values[destination_phase]
        if source_value["medium"] != destination_value["medium"]:
            _reject("bad-domain")
        discrete_keys = ("stance", "motion", "support", "medium", "void",
                         "frame", "mover")
        same_discrete = all(source_value[key] == destination_value[key]
                            for key in discrete_keys)
        same_clock = (source_value["quantum"] == destination_value["quantum"]
                      and source_value["horizon"] == destination_value["horizon"])
        same_velocity = source_value["velocity"] == destination_value["velocity"]
        same_elapsed = source_value["elapsed"] == destination_value["elapsed"]
        same_except_stance = all(
            source_value[key] == destination_value[key]
            for key in discrete_keys if key != "stance"
        ) and same_clock and same_velocity and same_elapsed
        if kind == 1 and (not same_except_stance or
                          source_value["stance"] == destination_value["stance"]):
            _reject("bad-domain")
        if kind == 2 and (not same_discrete or not same_clock or same_velocity
                          or not same_elapsed):
            _reject("bad-domain")
        if kind == 3 and (not same_discrete or not same_clock or
                          not same_velocity or same_elapsed):
            _reject("bad-domain")
        if kind == 4 and (not same_discrete or not same_clock
                          or source_value["support"] != 2 or not same_velocity
                          or same_elapsed):
            _reject("bad-domain")
        if kind == 5 and (source_value["motion"] != 0
                          or source_value["support"] == 0
                          or destination_value["motion"] != 1
                          or destination_value["support"] != 0
                          or source_value["stance"] != destination_value["stance"]
                          or source_value["void"] != destination_value["void"]
                          or not same_clock or destination_value["frame"] != 0
                          or destination_value["mover"] != NONE_ID):
            _reject("bad-domain")
        if kind == 6 and (source_value["motion"] != 1
                          or destination_value["motion"] != 1
                          or not same_discrete or not same_clock
                          or (same_velocity and same_elapsed)):
            _reject("bad-domain")
        if kind == 7 and (source_value["motion"] != 1
                          or source_value["support"] != 0
                          or destination_value["motion"] != 0
                          or destination_value["support"] == 0
                          or source_value["stance"] != destination_value["stance"]
                          or source_value["void"] != destination_value["void"]
                          or not same_clock):
            _reject("bad-domain")

    portal_values: list[dict[str, object]] = []
    for index in range(sections[6].count):
        record = _record(data, sections[6], index)
        if (_u64(data, record + 48) != source or _u32(data, record + 56) == UINT32_MAX
                or _u32(data, record + 60) == UINT32_MAX):
            _reject("bad-reference")
        from_cell = _find(cells, _stable_id(data, record + 64))
        to_cell = _find(cells, _stable_id(data, record + 88))
        _find(planes, _stable_id(data, record + 112))
        if from_cell == to_cell:
            _reject("bad-reference")
        vertices = _span(data, record + 136, counts[2], 3, 64)
        phase_span = _span(data, record + 144, counts[3], 1, 16)
        for vertex in range(vertices[0], vertices[0] + vertices[1]):
            _finite_values(data, _record(data, sections[2], vertex), 3)
        direction = _u32(data, record + 152)
        clearance = _finite_values(data, record + 156, 1)[0]
        contents_from, contents_to = _u32(data, record + 160), _u32(data, record + 164)
        flags = _u32(data, record + 168)
        if (direction >= 3 or clearance < 0.0 or not flags & 1
                or flags & ~PORTAL_FLAGS_KNOWN or contents_from & ~CONTENTS_KNOWN
                or contents_to & ~CONTENTS_KNOWN
                or (not flags & 2 and contents_from != contents_to)):
            _reject("bad-domain")
        portal_values.append({"from": from_cell, "to": to_cell,
                              "direction": direction, "flags": flags,
                              "phases": phase_span})

    surface_values: list[dict[str, object]] = []
    for index in range(sections[7].count):
        record = _record(data, sections[7], index)
        if (_u64(data, record + 48) != source or _u32(data, record + 56) == UINT32_MAX
                or _u32(data, record + 60) == UINT32_MAX):
            _reject("bad-reference")
        owner = _find(cells, _stable_id(data, record + 64))
        _find(planes, _stable_id(data, record + 88))
        _finite_values(data, record + 112, 3)
        if (_u32(data, record + 124) & ~CONTENTS_KNOWN
                or _u32(data, record + 128) & ~SURFACE_SEMANTICS_KNOWN):
            _reject("bad-domain")
        surface_values.append({"owner": owner})

    affordance_values: list[dict[str, object]] = []
    for index in range(sections[8].count):
        record = _record(data, sections[8], index)
        owner = _find(cells, _stable_id(data, record + 48))
        surface_span = _span(data, record + 72, counts[7], 1, 64)
        phase_span = _span(data, record + 80, counts[3], 1, 32)
        if _u32(data, record + 88) >= 7:
            _reject("bad-domain")
        _interval(data, record + 92, True)
        affordance_values.append({"owner": owner, "surfaces": surface_span,
                                  "phases": phase_span})

    mechanism_values: list[dict[str, object]] = []
    for index in range(sections[11].count):
        record = _record(data, sections[11], index)
        kind = _u32(data, record + 48)
        entry = _find(cells, _stable_id(data, record + 52))
        exit_cell = _find(cells, _stable_id(data, record + 76))
        activation = _stable_id(data, record + 100)
        entity_index, entity_ordinal = _u32(data, record + 124), _u32(data, record + 128)
        if kind >= 8 or entry == exit_cell or ((entity_index == UINT32_MAX) !=
                                               (entity_ordinal == UINT32_MAX)):
            _reject("bad-domain")
        _interval(data, record + 132, True)
        _interval(data, record + 140, True)
        topology = _span(data, record + 148, counts[11], 0, 64)
        if activation == NONE_ID:
            if entity_index == UINT32_MAX:
                _reject("bad-reference")
        else:
            _find(landmarks, activation)
        mechanism_values.append({"entry": entry, "exit": exit_cell,
                                 "topology": topology})

    for phase_index, phase in enumerate(phase_values):
        if phase["frame"] != 1:
            continue
        mechanism_index = _find(mechanisms, phase["mover"])
        assert mechanism_index is not None
        mechanism_value = mechanism_values[mechanism_index]
        if not any(cell_values[cell_index]["spans"][1][0] <= phase_index <
                   cell_values[cell_index]["spans"][1][0] +
                   cell_values[cell_index]["spans"][1][1]
                   for cell_index in (mechanism_value["entry"],
                                      mechanism_value["exit"])):
            _reject("bad-reference")

    landmark_values: list[dict[str, object]] = []
    for index in range(sections[10].count):
        record = _record(data, sections[10], index)
        if (_u64(data, record + 48) != source or _u32(data, record + 56) == UINT32_MAX
                or _u32(data, record + 60) == UINT32_MAX):
            _reject("bad-reference")
        cell_index = _find(cells, _stable_id(data, record + 64))
        assert cell_index is not None
        entity_index, entity_ordinal = _u32(data, record + 88), _u32(data, record + 92)
        if ((_u32(data, record + 96) >= 9)
                or ((entity_index == UINT32_MAX) != (entity_ordinal == UINT32_MAX))):
            _reject("bad-domain")
        origin = _finite_values(data, record + 100, 3)
        bounds = _bounds(data, record + 112)
        for axis in range(3):
            if not bounds[0][axis] <= origin[axis] <= bounds[1][axis]:
                _reject("bad-domain")
            cell_bounds = cell_values[cell_index]["bounds"]
            if not cell_bounds[0][axis] <= origin[axis] <= cell_bounds[1][axis]:
                _reject("bad-domain")
        _find(mechanisms, _stable_id(data, record + 136), True)
        surface_index = _find(surfaces, _stable_id(data, record + 160), True)
        if surface_index is not None and surface_values[surface_index]["owner"] != cell_index:
            _reject("bad-reference")
        landmark_values.append({"owner": cell_index})

    family_limits = (physics[1], physics[2], physics[3], physics[4],
                     physics[5], physics[5])
    kernel_values: list[dict[str, object]] = []
    for index in range(sections[9].count):
        record = _record(data, sections[9], index)
        source_cell = _find(cells, _stable_id(data, record + 48))
        destination_cell = _find(cells, _stable_id(data, record + 72))
        boundary = _stable_id(data, record + 96)
        _find(affordances, _stable_id(data, record + 120), True)
        mechanism = _stable_id(data, record + 144)
        _find(mechanisms, mechanism, True)
        source_phase = _find(phases, _stable_id(data, record + 168))
        destination_phase = _find(phases, _stable_id(data, record + 192))
        transition = _stable_id(data, record + 216)
        assert source_cell is not None and destination_cell is not None
        assert source_phase is not None and destination_phase is not None
        for cell_index, phase_index in ((source_cell, source_phase),
                                        (destination_cell, destination_phase)):
            phase_span = cell_values[cell_index]["spans"][1]
            if not phase_span[0] <= phase_index < phase_span[0] + phase_span[1]:
                _reject("bad-reference")
        if source_cell == destination_cell:
            transition_index = _find(transitions, transition)
            if boundary != NONE_ID or transition_index is None:
                _reject("bad-reference")
            transition_record = _record(data, sections[4], transition_index)
            if (_stable_id(data, transition_record + 48) != source_cell
                    or _stable_id(data, transition_record + 72) !=
                    _stable_id(data, record + 168)
                    or _stable_id(data, transition_record + 96) !=
                    _stable_id(data, record + 192)):
                _reject("bad-reference")
        else:
            portal_index = _find(portals, boundary)
            if transition != NONE_ID or portal_index is None:
                _reject("bad-reference")
            portal = portal_values[portal_index]
            forward = portal["from"] == source_cell and portal["to"] == destination_cell
            reverse = portal["to"] == source_cell and portal["from"] == destination_cell
            if (not forward and not reverse) or (portal["direction"] == 1 and not forward) \
                    or (portal["direction"] == 2 and not reverse):
                _reject("bad-reference")
        family, cost_law = _u32(data, record + 240), _u32(data, record + 244)
        flags = _u32(data, record + 328)
        if family >= 6 or cost_law >= 6 or flags & ~KERNEL_FLAGS_KNOWN \
                or flags & 0x13 != 0x13:
            _reject("bad-domain")
        _interval3(data, record + 248, False)
        duration = _interval(data, record + 272, True)
        speed = _interval(data, record + 280, True)
        acceleration = _interval(data, record + 288, True)
        vertical = _interval(data, record + 296, True)
        gravity, drag = _finite_values(data, record + 304, 2)
        if duration[1] <= 0.0 or _u64(data, record + 312) != physics_id:
            _reject("bad-identity")
        if (gravity != physics[0] or speed[1] > physics[7]
                or acceleration[1] > family_limits[family]
                or vertical[1] > family_limits[family]):
            _reject("bad-domain")
        source_medium = phase_values[source_phase]["medium"]
        destination_medium = phase_values[destination_phase]["medium"]
        water = source_medium in (1, 2, 3) or destination_medium in (1, 2, 3)
        if drag != (physics[6] if water else 0.0):
            _reject("bad-domain")
        changes_medium = source_medium != destination_medium
        if source_cell != destination_cell and changes_medium \
                and not portal_values[portal_index]["flags"] & 2:
            _reject("bad-domain")
        if bool(flags & 4) != changes_medium or (flags & 8 and phase_values[source_phase]["support"] == 0):
            _reject("bad-domain")
        if family == 2 and not water or family == 4 and mechanism == NONE_ID:
            _reject("bad-domain")
        kernel_values.append({"owner": source_cell})

    for cell_index, cell in enumerate(cell_values):
        spans = cell["spans"]
        for section_index, values, key in (
                (2, surface_values, "owner"), (3, affordance_values, "owner"),
                (4, kernel_values, "owner"), (5, landmark_values, "owner")):
            first, count = spans[section_index]
            if any(values[item][key] != cell_index for item in range(first, first + count)):
                _reject("bad-reference")
        first, count = spans[6]
        if any(mechanism_values[item]["entry"] != cell_index
               and mechanism_values[item]["exit"] != cell_index
               for item in range(first, first + count)):
            _reject("bad-reference")
    for values, span_index in ((surface_values, 2), (affordance_values, 3),
                               (kernel_values, 4), (landmark_values, 5)):
        for record_index, value in enumerate(values):
            first, count = cell_values[value["owner"]]["spans"][span_index]
            if not first <= record_index < first + count:
                _reject("bad-reference")


def validate(data: bytes, *, expected_generation: int, expected_bsp: bytes,
             expected_schema: bytes, expected_artifact: bytes,
             exact_artifact: bytes) -> dict[str, object]:
    """Validate one complete artifact and return its stable semantic summary."""
    if len(expected_bsp) != 32 or len(expected_schema) != 32 \
            or len(expected_artifact) != 32 or len(exact_artifact) != 32:
        _reject("bad-expected-identity")
    generation, sections = _parse_header(data)
    source, _, _, physics_id, physics = _validate_model(data, sections)
    binding = sections[12].offset
    bsp, schema = data[binding:binding + 32], data[binding + 32:binding + 64]
    if not any(bsp) or not any(schema) or generation != expected_generation \
            or bsp != expected_bsp or schema != expected_schema \
            or not any(exact_artifact) \
            or expected_artifact != exact_artifact:
        _reject("bad-binding")
    _validate_records(data, sections, source, physics_id, physics)
    names = ("model", "planes", "portal_vertices", "phases", "transitions",
             "cells", "portals", "surfaces", "affordances", "kernels",
             "landmarks", "mechanisms", "binding")
    summary: dict[str, object] = {
        "bsp": bsp.hex(),
        "generation": generation,
        "schema": schema.hex(),
    }
    for name, section in zip(names, sections):
        if name not in ("model", "binding"):
            summary[name] = section.count
    return dict(sorted(summary.items()))


def _identity(value: str) -> bytes:
    try:
        decoded = bytes.fromhex(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("identity must be 64 hexadecimal digits") from error
    if len(decoded) != 32:
        raise argparse.ArgumentTypeError("identity must be 64 hexadecimal digits")
    return decoded


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generation", required=True, type=int)
    parser.add_argument("--bsp-id", required=True, type=_identity)
    parser.add_argument("--schema-id", required=True, type=_identity)
    parser.add_argument("--artifact-id", required=True, type=_identity)
    parser.add_argument("--exact-artifact-id", required=True, type=_identity)
    parser.add_argument("artifact", type=Path)
    arguments = parser.parse_args(argv)
    try:
        if arguments.artifact.stat().st_size > MAX_ARTIFACT_BYTES:
            _reject("bad-size")
        data = arguments.artifact.read_bytes()
        summary = validate(data, expected_generation=arguments.generation,
                           expected_bsp=arguments.bsp_id,
                           expected_schema=arguments.schema_id,
                           expected_artifact=arguments.artifact_id,
                           exact_artifact=arguments.exact_artifact_id)
    except OSError as error:
        print(f"runev2read.py: {error}", file=sys.stderr)
        return 2
    except RuneV2Error as error:
        print(f"runev2read.py: reject: {error}", file=sys.stderr)
        return 1
    print(json.dumps(summary, separators=(",", ":"), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
