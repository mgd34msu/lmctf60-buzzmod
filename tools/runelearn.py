#!/usr/bin/env python3
"""Build bounded, source-RUNE-bound human route nominations."""

from __future__ import annotations

import argparse
import copy
from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import re
import tempfile
from typing import Any, Iterable

try:
    import humantrace
    import runeio
except ModuleNotFoundError:
    from tools import humantrace, runeio


RLEARN_FORMAT = 2
MAX_CANDIDATES = 4096
MAX_HOOK_CANDIDATES = 512
MAX_HOOKS_PER_PAIR = 8
MAX_REPLAY_BYTES = 64 * 1024 * 1024
MAX_SEEDS_PER_CELL = 256
LEGACY_PULL_SPEED_Q8 = 800 * 8
LEGACY_PULL_SPEED_TOLERANCE_Q8 = 2
LEGACY_PULL_MIN_SAMPLES = 3
LEGACY_PULL_RESIDUAL_Q8 = 4 * 8
LEGACY_HOOK_MAX_RAY_Q8 = 8192 * 8
PMF_ON_GROUND = 4
PMF_NO_PREDICTION = 64
DRY_RUN_WAYPOINT = 1
SEED_CELL_Q8 = 64 * 8
SEED_HORIZONTAL_Q8 = 576 * 8 // 10
SEED_VERTICAL_Q8 = 48 * 8
WAYPOINT_DEVIATION_Q8 = 48 * 8
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
STEP_FIELDS = {
    "seq", "client", "frame", "snapinitial", "cmd", "before", "after",
    "ground", "waterlevel", "watertype", "touches",
}


@dataclass(frozen=True)
class LearningCandidate:
    source_from: int
    source_to: int
    from_origin_q8: tuple[int, int, int]
    to_origin_q8: tuple[int, int, int]
    waypoint_q8: tuple[int, int, int]
    first_sequence: int
    last_sequence: int
    has_waypoint: bool
    hint: int = DRY_RUN_WAYPOINT


@dataclass(frozen=True)
class HookNomination:
    source_from: int
    source_to: int
    from_origin_q8: tuple[int, int, int]
    to_origin_q8: tuple[int, int, int]
    rope_count: int
    aim_short: tuple[tuple[int, int], tuple[int, int]]
    bite_q8: tuple[tuple[int, int, int], tuple[int, int, int]]


@dataclass(frozen=True)
class LegacyPullSample:
    step_index: int
    sequence: int
    frame: int
    origin_q8: tuple[int, int, int]
    velocity_q8: tuple[int, int, int]


@dataclass(frozen=True)
class LegacyPullGroup:
    first_index: int
    last_index: int
    bite_q8: tuple[int, int, int]
    samples: tuple[LegacyPullSample, ...]


def _sha256(value: Any, name: str) -> str:
    if not isinstance(value, str) or SHA256.fullmatch(value) is None:
        raise ValueError(f"{name} must be a lowercase SHA-256")
    return value


def _float_bits(value: Any) -> bytes:
    import struct
    return struct.pack("<f", float(value))


def _seed_q8(seed: Any) -> tuple[int, int, int]:
    result = tuple(int(round(float(component) * 8.0))
                   for component in seed.origin)
    if len(result) != 3 or any(
            _float_bits(component) != _float_bits(fixed * 0.125)
            for component, fixed in zip(seed.origin, result)):
        raise ValueError("source RUNE contains a non-q8 seed")
    return result


def _binding_for(rune: Any, source_sha256: str) -> dict[str, Any]:
    header = rune.header
    return {
        "map": header.map_name,
        "bsp_checksum": header.bsp_checksum,
        "entity_crc32": header.entity_crc32,
        "physics_flags": header.physics_flags,
        "gravity": header.gravity,
        "airaccelerate": header.airaccelerate,
        "maxvelocity": header.maxvelocity,
        "pmove_substep_ms": header.pmove_substep_ms,
        "server_frame_ms": header.server_frame_ms,
        "host_physics_id": header.host_physics_id,
        "route_contract": header.route_contract,
        "payload_crc32": header.payload_crc32,
        "header_crc32": header.header_crc32,
        "action_contract_crc32": header.action_contract_crc32,
        "mechanism_contract_crc32": header.mechanism_contract_crc32,
        "num_seeds": header.num_seeds,
        "num_links": header.num_links,
        "num_mechanism_nodes": header.num_activation_nodes,
        "num_mechanism_edges": header.num_activation_edges,
        "num_inventory_edges": header.num_inventory_edges,
        "num_mechanism_plans": header.num_activation_plans,
        "string_bytes": header.string_bytes,
        "rune_sha256": source_sha256,
    }


def _binding_matches(actual: dict[str, Any],
                     expected: dict[str, Any]) -> bool:
    for name, wanted in expected.items():
        if name not in actual:
            return False
        found = actual[name]
        if name in {"gravity", "airaccelerate", "maxvelocity"}:
            if _float_bits(found) != _float_bits(wanted):
                return False
        elif found != wanted:
            return False
    return True


def recover_replay(rune: Any, source_sha256: str,
                   replay: Any) -> dict[str, Any]:
    """Bind a legacy raw replay to an identity-exact LOCAL_ONLY RUNE.

    Recovery records weaker post-hoc provenance; it does not claim the RUNE
    was loaded during capture. The exact in-engine oracle still decides every
    nomination.
    """
    source_sha256 = _sha256(source_sha256, "source rune SHA-256")
    if not isinstance(replay, dict) or replay.get("format") not in {
            humantrace.EVIDENCE_FORMAT_V1,
            humantrace.EVIDENCE_FORMAT_V2}:
        raise ValueError("replay has the wrong format")
    if rune.header.route_contract != runeio.RUNE_ROUTE_CONTRACT_LOCAL_ONLY:
        raise ValueError("recovery requires a LOCAL_ONLY source RUNE")
    source = replay.get("source")
    identity = replay.get("identity")
    bindings = replay.get("rune_bindings")
    if not isinstance(source, dict) or not isinstance(identity, dict):
        raise ValueError("replay identity is missing")
    _sha256(source.get("sha256"), "trace_sha256")
    if (identity.get("map") != rune.header.map_name or
            identity.get("bsp_checksum") != rune.header.bsp_checksum or
            identity.get("entity_crc32") != rune.header.entity_crc32 or
            identity.get("physics_id") != rune.header.host_physics_id):
        raise ValueError("replay identity does not match source RUNE")
    if not isinstance(bindings, list) or bindings:
        raise ValueError("recovery requires exactly zero rune bindings")
    raw_steps = replay.get("steps")
    if not isinstance(raw_steps, list) or not raw_steps:
        raise ValueError("replay steps are missing")
    steps = [_validated_step(value, index)
             for index, value in enumerate(raw_steps)]
    sequences = [step["seq"] for step in steps]
    if sequences != sorted(sequences) or len(sequences) != len(set(sequences)):
        raise ValueError("replay steps are not uniquely sequence ordered")

    recovered = copy.deepcopy(replay)
    binding = _binding_for(rune, source_sha256)
    binding.update({
        "start_sequence": sequences[0],
        "frame": steps[0]["frame"],
        "provenance": "posthoc-identity-exact",
    })
    if replay["format"] == humantrace.EVIDENCE_FORMAT_V2:
        events = replay.get("hook_events")
        if not isinstance(events, list):
            raise ValueError("replay hook events are missing")
        event_ids = [humantrace.integer(
            event.get("event"), "hook event", 1, 2**63 - 1)
            for event in events if isinstance(event, dict)]
        binding["start_hook_event"] = min(event_ids, default=1)
    recovered["rune_bindings"] = [binding]
    return recovered


def _validated_step(value: Any, index: int) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != STEP_FIELDS:
        raise ValueError(f"replay step {index} has the wrong fields")
    touches = value["touches"]
    if not isinstance(touches, list) or len(touches) > 32:
        raise ValueError(
            f"replay step {index} touches must contain at most 32 keys")
    return {
        "seq": humantrace.integer(value["seq"], f"step {index} seq",
                                  1, 2**63 - 1),
        "client": humantrace.integer(value["client"],
                                     f"step {index} client", 1, 0x7FFFFFFF),
        "frame": humantrace.integer(value["frame"],
                                    f"step {index} frame", 0, 0x7FFFFFFF),
        "snapinitial": humantrace.integer(
            value["snapinitial"], f"step {index} snapinitial", 0, 1),
        "cmd": humantrace.validate_command(value["cmd"],
                                           f"step {index} cmd"),
        "before": humantrace.validate_state(value["before"],
                                             f"step {index} before"),
        "after": humantrace.validate_state(value["after"],
                                            f"step {index} after"),
        "ground": humantrace.integer(value["ground"],
                                     f"step {index} ground", -1, 0x7FFFFFFF),
        "waterlevel": humantrace.integer(
            value["waterlevel"], f"step {index} waterlevel", 0, 3),
        "watertype": humantrace.integer(
            value["watertype"], f"step {index} watertype",
            -0x80000000, 0x7FFFFFFF),
        "touches": [
            humantrace.integer(item, f"step {index} touch {ordinal}",
                               -1, 0x7FFFFFFF)
            for ordinal, item in enumerate(touches)
        ],
    }


def _validate_replay(rune: Any, source_sha256: str,
                     replay: Any) -> tuple[list[dict[str, Any]],
                                           list[dict[str, Any]],
                                           dict[str, Any], str]:
    if not isinstance(replay, dict) or replay.get("format") not in {
            humantrace.EVIDENCE_FORMAT_V1,
            humantrace.EVIDENCE_FORMAT_V2}:
        raise ValueError("replay has the wrong format")
    if rune.header.route_contract != runeio.RUNE_ROUTE_CONTRACT_LOCAL_ONLY:
        raise ValueError("learning requires a LOCAL_ONLY source RUNE")
    source = replay.get("source")
    identity = replay.get("identity")
    bindings = replay.get("rune_bindings")
    if not isinstance(source, dict) or not isinstance(identity, dict):
        raise ValueError("replay identity is missing")
    trace_sha256 = _sha256(source.get("sha256"), "trace_sha256")
    if (identity.get("map") != rune.header.map_name or
            identity.get("bsp_checksum") != rune.header.bsp_checksum or
            identity.get("entity_crc32") != rune.header.entity_crc32 or
            identity.get("physics_id") != rune.header.host_physics_id):
        raise ValueError("replay identity does not match source RUNE")
    if not isinstance(bindings, list) or len(bindings) != 1 or \
            not isinstance(bindings[0], dict):
        raise ValueError("replay has no unique LOCAL_ONLY rune binding")
    expected = _binding_for(rune, source_sha256)
    binding = bindings[0]
    if not _binding_matches(binding, expected):
        raise ValueError("rune binding does not match source RUNE")
    start_sequence = humantrace.integer(
        binding.get("start_sequence"), "binding start_sequence",
        1, 2**63 - 1)
    raw_steps = replay.get("steps")
    if not isinstance(raw_steps, list):
        raise ValueError("replay steps are missing")
    steps = [_validated_step(value, index)
             for index, value in enumerate(raw_steps)]
    sequences = [step["seq"] for step in steps]
    if sequences != sorted(sequences) or len(sequences) != len(set(sequences)):
        raise ValueError("replay steps are not uniquely sequence ordered")
    steps = [step for step in steps if step["seq"] >= start_sequence]
    hook_events: list[dict[str, Any]] = []
    if replay["format"] == humantrace.EVIDENCE_FORMAT_V2:
        raw_events = replay.get("hook_events")
        if not isinstance(raw_events, list):
            raise ValueError("replay hook events are missing")
        start_hook_event = humantrace.integer(
            binding.get("start_hook_event"), "binding start_hook_event",
            1, 2**63 - 1)
        last_event = 0
        last_after_step = 0
        greatest_step = max(sequences, default=0)
        frame_window = replay.get("frame_window")
        if (not isinstance(frame_window, list) or len(frame_window) != 2):
            raise ValueError("replay frame window is invalid")
        first_frame = humantrace.integer(
            frame_window[0], "frame window start", 0, 0x7FFFFFFF)
        last_frame = humantrace.integer(
            frame_window[1], "frame window end", first_frame, 0x7FFFFFFF)
        client = humantrace.integer(
            replay.get("client"), "replay client", 1, 0x7FFFFFFF)
        for index, value in enumerate(raw_events):
            if not isinstance(value, dict) or "kind" not in value:
                raise ValueError(f"hook event {index} is invalid")
            validated = humantrace.validate_hook_event(
                {"format": humantrace.TRACE_FORMAT_V2, **value}, index + 1)
            if validated["event"] <= last_event:
                raise ValueError("replay hook events are not ordered")
            if validated["after_step"] < last_after_step:
                raise ValueError("replay hook after_step decreased")
            if validated["event"] < start_hook_event:
                raise ValueError("replay contains a pre-binding hook event")
            if validated["after_step"] > greatest_step:
                raise ValueError("hook event refers past replay steps")
            if (validated["client"] != client or
                    not first_frame <= validated["frame"] <= last_frame):
                raise ValueError("hook event is outside the selected replay")
            hook_events.append(validated)
            last_event = validated["event"]
            last_after_step = validated["after_step"]
    elif "hook_events" in replay:
        raise ValueError("v1 replay cannot contain hook events")
    return steps, hook_events, binding, trace_sha256


class _SeedGrid:
    def __init__(self, rune: Any):
        self.seeds = tuple(_seed_q8(seed) for seed in rune.seeds)
        self.cells: dict[tuple[int, int, int], list[int]] = {}
        for index, seed in enumerate(self.seeds):
            bucket = self.cells.setdefault(self._key(seed), [])
            bucket.append(index)
            if len(bucket) > MAX_SEEDS_PER_CELL:
                raise ValueError("source RUNE seed-cell capacity exceeded")

    @staticmethod
    def _key(point: tuple[int, int, int]) -> tuple[int, int, int]:
        return tuple(component // SEED_CELL_Q8 for component in point)

    def nearest(self, point: tuple[int, int, int]) -> int:
        cell = self._key(point)
        best = -1
        best_distance = 0
        for dz in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    key = (cell[0] + dx, cell[1] + dy, cell[2] + dz)
                    for index in self.cells.get(key, ()):
                        seed = self.seeds[index]
                        delta = tuple(seed[axis] - point[axis]
                                      for axis in range(3))
                        horizontal = delta[0] ** 2 + delta[1] ** 2
                        distance = horizontal + delta[2] ** 2
                        if (not -SEED_VERTICAL_Q8 < delta[2] <
                                SEED_VERTICAL_Q8 or
                                horizontal >= SEED_HORIZONTAL_Q8 ** 2):
                            continue
                        if (best < 0 or distance < best_distance or
                                (distance == best_distance and index < best)):
                            best = index
                            best_distance = distance
        return best

    def nearest_unique(self, point: tuple[int, int, int]) -> int:
        cell = self._key(point)
        nearest: list[tuple[int, int]] = []
        for dz in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    key = (cell[0] + dx, cell[1] + dy, cell[2] + dz)
                    for index in self.cells.get(key, ()):
                        seed = self.seeds[index]
                        delta = tuple(seed[axis] - point[axis]
                                      for axis in range(3))
                        horizontal = delta[0] ** 2 + delta[1] ** 2
                        if (not -SEED_VERTICAL_Q8 < delta[2] <
                                SEED_VERTICAL_Q8 or
                                horizontal >= SEED_HORIZONTAL_Q8 ** 2):
                            continue
                        nearest.append((horizontal + delta[2] ** 2, index))
        if not nearest:
            return -1
        nearest.sort()
        if len(nearest) > 1 and nearest[0][0] == nearest[1][0]:
            return -1
        return nearest[0][1]


def _dry(step: dict[str, Any]) -> bool:
    before_flags = step["before"]["flags"]
    after_flags = step["after"]["flags"]
    return (
        step["waterlevel"] == 0 and
        step["ground"] == 0 and
        all(touch == 0 for touch in step["touches"]) and
        before_flags & PMF_ON_GROUND and after_flags & PMF_ON_GROUND and
        not before_flags & PMF_NO_PREDICTION and
        not after_flags & PMF_NO_PREDICTION
    )


def _grounded_after(step: dict[str, Any]) -> bool:
    return (
        step["waterlevel"] == 0 and step["ground"] == 0 and
        all(touch == 0 for touch in step["touches"]) and
        step["after"]["flags"] & PMF_ON_GROUND and
        not step["after"]["flags"] & PMF_NO_PREDICTION
    )


def _dry_runs(steps: list[dict[str, Any]]) -> Iterable[list[dict[str, Any]]]:
    for segment in humantrace.replay_segments(steps):
        run: list[dict[str, Any]] = []
        for step in steps[segment["start_index"]:segment["end_exclusive"]]:
            if _dry(step):
                run.append(step)
            else:
                if run:
                    yield run
                run = []
        if run:
            yield run


def _waypoint(points: list[tuple[int, int, int]],
              source: tuple[int, int, int],
              destination: tuple[int, int, int]) -> tuple[bool,
                                                          tuple[int, int, int]]:
    dx = destination[0] - source[0]
    dy = destination[1] - source[1]
    length2 = dx * dx + dy * dy
    if length2 == 0:
        return False, (0, 0, 0)
    best = (0, (0, 0, 0))
    for point in points:
        cross = abs((point[0] - source[0]) * dy -
                    (point[1] - source[1]) * dx)
        if cross > best[0]:
            best = (cross, point)
    if best[0] * best[0] <= WAYPOINT_DEVIATION_Q8 ** 2 * length2:
        return False, (0, 0, 0)
    return True, best[1]


def _candidate_better(candidate: LearningCandidate,
                      current: LearningCandidate) -> bool:
    candidate_span = candidate.last_sequence - candidate.first_sequence
    current_span = current.last_sequence - current.first_sequence
    return (
        candidate_span > current_span or
        (candidate_span == current_span and
         (candidate.first_sequence, candidate.waypoint_q8) <
         (current.first_sequence, current.waypoint_q8))
    )


def build_candidates(rune: Any, steps: list[dict[str, Any]]) -> tuple[
        LearningCandidate, ...]:
    grid = _SeedGrid(rune)
    present = {(link.source, link.destination) for link in rune.links}
    selected: dict[tuple[tuple[int, int, int], tuple[int, int, int]],
                   LearningCandidate] = {}
    for run in _dry_runs(steps):
        samples = [(tuple(run[0]["before"]["origin"]), run[0]["seq"])]
        samples.extend((tuple(step["after"]["origin"]), step["seq"])
                       for step in run)
        localized = [(grid.nearest(point), point, sequence, ordinal)
                     for ordinal, (point, sequence) in enumerate(samples)]
        localized = [value for value in localized if value[0] >= 0]
        previous = None
        for value in localized:
            if previous is None:
                previous = value
                continue
            if value[0] == previous[0]:
                continue
            source_index, _, first_sequence, first_ordinal = previous
            destination_index, _, last_sequence, last_ordinal = value
            previous = value
            if ((source_index, destination_index) in present or
                    rune.seeds[source_index].flags & runeio.RSF_WATER or
                    rune.seeds[destination_index].flags & runeio.RSF_WATER):
                continue
            source = grid.seeds[source_index]
            destination = grid.seeds[destination_index]
            between = [point for point, _ in
                       samples[first_ordinal:last_ordinal + 1]]
            has_waypoint, waypoint = _waypoint(
                between, source, destination)
            candidate = LearningCandidate(
                source_from=source_index,
                source_to=destination_index,
                from_origin_q8=source,
                to_origin_q8=destination,
                waypoint_q8=waypoint,
                first_sequence=first_sequence,
                last_sequence=last_sequence,
                has_waypoint=has_waypoint,
            )
            key = (source, destination)
            current = selected.get(key)
            if current is None or _candidate_better(candidate, current):
                selected[key] = candidate
    if len(selected) > MAX_CANDIDATES:
        raise ValueError("human route candidate capacity exceeded")
    return tuple(sorted(selected.values(), key=lambda candidate: (
        candidate.from_origin_q8, candidate.to_origin_q8,
        candidate.source_from, candidate.source_to,
        candidate.first_sequence, candidate.waypoint_q8,
    )))


def _safe_hook_interval(steps: list[dict[str, Any]], first: int,
                        last: int) -> bool:
    selected = steps[first:last + 1]
    for index, current in enumerate(selected):
        if (current["snapinitial"] or current["waterlevel"] or
                current["ground"] not in (-1, 0) or
                current["before"]["flags"] & PMF_NO_PREDICTION or
                current["after"]["flags"] & PMF_NO_PREDICTION):
            return False
        if index and selected[index - 1]["after"] != current["before"]:
            return False
    return True


def _stable_landing(steps: list[dict[str, Any]], after_sequence: int) -> int:
    for index, current in enumerate(steps[:-1]):
        following = steps[index + 1]
        if (current["seq"] <= after_sequence or not _grounded_after(current) or
                not _dry(following) or following["frame"] <= current["frame"] or
                current["after"] != following["before"]):
            continue
        return index
    return -1


def _previous_dry(steps: list[dict[str, Any]], sequence: int) -> int:
    result = -1
    for index, current in enumerate(steps):
        if current["seq"] > sequence:
            break
        if _dry(current):
            result = index
    return result


def _legacy_pull_sample(previous: dict[str, Any], current: dict[str, Any],
                        step_index: int) -> LegacyPullSample | None:
    origin = tuple(current["before"]["origin"])
    velocity = tuple(current["before"]["velocity"])
    speed = math.sqrt(sum(component * component for component in velocity))
    if (current["waterlevel"] or
            current["before"]["flags"] & PMF_NO_PREDICTION or
            current["after"]["flags"] & PMF_NO_PREDICTION or
            current["frame"] != previous["frame"] + 1 or
            tuple(previous["after"]["origin"]) != origin or
            tuple(previous["after"]["velocity"]) == velocity or
            abs(speed - LEGACY_PULL_SPEED_Q8) >
            LEGACY_PULL_SPEED_TOLERANCE_Q8):
        return None
    return LegacyPullSample(
        step_index=step_index,
        sequence=current["seq"],
        frame=current["frame"],
        origin_q8=origin,
        velocity_q8=velocity,
    )


def _solve_three(matrix: list[list[float]], values: list[float]
                 ) -> tuple[float, float, float] | None:
    rows = [matrix[index][:] + [values[index]] for index in range(3)]
    for column in range(3):
        pivot = max(range(column, 3),
                    key=lambda index: abs(rows[index][column]))
        if abs(rows[pivot][column]) < 1e-9:
            return None
        rows[column], rows[pivot] = rows[pivot], rows[column]
        divisor = rows[column][column]
        rows[column] = [value / divisor for value in rows[column]]
        for index in range(3):
            if index == column:
                continue
            factor = rows[index][column]
            rows[index] = [
                rows[index][item] - factor * rows[column][item]
                for item in range(4)
            ]
    return tuple(rows[index][3] for index in range(3))


def _legacy_fixed_bite(samples: list[LegacyPullSample]
                       ) -> tuple[int, int, int] | None:
    if len(samples) < LEGACY_PULL_MIN_SAMPLES:
        return None
    matrix = [[0.0] * 3 for _ in range(3)]
    values = [0.0] * 3
    directions: list[tuple[float, float, float]] = []
    for sample in samples:
        length = math.sqrt(sum(component * component
                               for component in sample.velocity_q8))
        if length == 0.0:
            return None
        direction = tuple(component / length
                          for component in sample.velocity_q8)
        directions.append(direction)
        projection = [[
            (1.0 if row == column else 0.0) -
            direction[row] * direction[column]
            for column in range(3)] for row in range(3)]
        for row in range(3):
            for column in range(3):
                matrix[row][column] += projection[row][column]
                values[row] += (projection[row][column] *
                                sample.origin_q8[column])
    bite = _solve_three(matrix, values)
    if bite is None:
        return None
    for sample, direction in zip(samples, directions):
        delta = tuple(bite[axis] - sample.origin_q8[axis]
                      for axis in range(3))
        forward = sum(delta[axis] * direction[axis] for axis in range(3))
        residual_squared = max(
            0.0, sum(component * component for component in delta) -
            forward * forward)
        if (forward <= 0.0 or forward > LEGACY_HOOK_MAX_RAY_Q8 or
                math.sqrt(residual_squared) > LEGACY_PULL_RESIDUAL_Q8):
            return None
    return tuple(int(round(component)) for component in bite)


def _legacy_pull_groups(steps: list[dict[str, Any]]) -> tuple[
        LegacyPullGroup, ...]:
    runs: list[list[LegacyPullSample]] = []
    for index in range(1, len(steps)):
        sample = _legacy_pull_sample(steps[index - 1], steps[index], index)
        if sample is None:
            continue
        if not runs or sample.frame != runs[-1][-1].frame + 1:
            runs.append([sample])
        else:
            runs[-1].append(sample)
    groups = []
    for samples in runs:
        bite = _legacy_fixed_bite(samples)
        if bite is not None:
            groups.append(LegacyPullGroup(
                first_index=samples[0].step_index,
                last_index=samples[-1].step_index,
                bite_q8=bite,
                samples=tuple(samples),
            ))
    return tuple(groups)


def _legacy_stable_source(steps: list[dict[str, Any]], before_index: int,
                          segment_start: int) -> int:
    for index in range(before_index, segment_start, -1):
        previous = steps[index - 1]
        current = steps[index]
        if (_dry(previous) and _dry(current) and
                current["frame"] > previous["frame"] and
                previous["after"] == current["before"]):
            return index
    return -1


def _legacy_stable_landing(steps: list[dict[str, Any]], after_index: int,
                           segment_end: int) -> int:
    for index in range(after_index + 1, segment_end - 1):
        current = steps[index]
        following = steps[index + 1]
        if (_grounded_after(current) and _dry(following) and
                following["frame"] > current["frame"] and
                current["after"] == following["before"]):
            return index
    return -1


def _legacy_aim_short(source_q8: tuple[int, int, int],
                      bite_q8: tuple[int, int, int]) -> tuple[int, int]:
    delta = tuple(bite_q8[axis] - source_q8[axis] for axis in range(3))
    horizontal = math.hypot(delta[0], delta[1])
    pitch = -math.degrees(math.atan2(delta[2], horizontal))
    yaw = math.degrees(math.atan2(delta[1], delta[0]))

    def angle_short(angle: float) -> int:
        value = int(angle * 65536.0 / 360.0) & 0xFFFF
        return value - 0x10000 if value >= 0x8000 else value

    return angle_short(pitch), angle_short(yaw)


def _legacy_segments(steps: list[dict[str, Any]]) -> list[tuple[int, int]]:
    segments = []
    start = 0
    for index in range(1, len(steps)):
        previous = steps[index - 1]
        current = steps[index]
        if (current["frame"] < previous["frame"] or
                previous["after"]["origin"] !=
                current["before"]["origin"]):
            segments.append((start, index))
            start = index
    if steps:
        segments.append((start, len(steps)))
    return segments


def build_legacy_hook_candidates(rune: Any, steps: list[dict[str, Any]],
                                 binding: dict[str, Any], replay_format: str
                                 ) -> tuple[HookNomination, ...]:
    if replay_format != humantrace.EVIDENCE_FORMAT_V1:
        raise ValueError("legacy hook recovery requires v1 evidence")
    if binding.get("provenance") != "posthoc-identity-exact":
        raise ValueError("legacy hook recovery requires posthoc identity")
    grid = _SeedGrid(rune)
    present = {(link.source, link.destination) for link in rune.links}
    groups = _legacy_pull_groups(steps)
    segments = _legacy_segments(steps)
    selected: dict[tuple[Any, ...], HookNomination] = {}
    handled: set[tuple[int, int]] = set()
    for first, last in segments:
        segment_groups = [group for group in groups
                          if first <= group.first_index < last]
        for group in segment_groups:
            source_step = _legacy_stable_source(
                steps, group.first_index - 1, first)
            landing_step = _legacy_stable_landing(
                steps, group.last_index, last)
            if source_step < 0 or landing_step < 0:
                continue
            journey = (source_step, landing_step)
            if journey in handled:
                continue
            handled.add(journey)
            ropes = [candidate for candidate in segment_groups
                     if source_step < candidate.first_index and
                     candidate.last_index < landing_step]
            if len(ropes) not in (1, 2):
                continue
            duration_ms = ((steps[landing_step]["frame"] -
                            steps[source_step]["frame"]) *
                           int(binding["server_frame_ms"]))
            if duration_ms < 0 or duration_ms > 32767:
                continue
            source_from = grid.nearest_unique(
                tuple(steps[source_step]["after"]["origin"]))
            source_to = grid.nearest_unique(
                tuple(steps[landing_step]["after"]["origin"]))
            if (source_from < 0 or source_to < 0 or
                    source_from == source_to or
                    (source_from, source_to) in present or
                    rune.seeds[source_from].flags & runeio.RSF_WATER or
                    rune.seeds[source_to].flags & runeio.RSF_WATER):
                continue
            bites = [rope.bite_q8 for rope in ropes]
            aims = [_legacy_aim_short(grid.seeds[source_from], bite)
                    for bite in bites]
            while len(bites) < 2:
                bites.append((0, 0, 0))
                aims.append((0, 0))
            nomination = HookNomination(
                source_from=source_from,
                source_to=source_to,
                from_origin_q8=grid.seeds[source_from],
                to_origin_q8=grid.seeds[source_to],
                rope_count=len(ropes),
                aim_short=(aims[0], aims[1]),
                bite_q8=(bites[0], bites[1]),
            )
            key = (nomination.from_origin_q8, nomination.to_origin_q8,
                   nomination.rope_count, nomination.bite_q8)
            selected[key] = nomination
    result = tuple(sorted(selected.values(), key=lambda value: (
        value.from_origin_q8, value.to_origin_q8, value.rope_count,
        value.bite_q8, value.aim_short, value.source_from, value.source_to,
    )))
    if len(result) > MAX_HOOK_CANDIDATES:
        raise ValueError("human hook candidate capacity exceeded")
    per_pair: dict[tuple[tuple[int, int, int], tuple[int, int, int]], int] = {}
    for nomination in result:
        pair = (nomination.from_origin_q8, nomination.to_origin_q8)
        per_pair[pair] = per_pair.get(pair, 0) + 1
        if per_pair[pair] > MAX_HOOKS_PER_PAIR:
            raise ValueError("human hook variants per pair exceeded")
    return result


def build_hook_candidates(rune: Any, steps: list[dict[str, Any]],
                          events: list[dict[str, Any]],
                          binding: dict[str, Any]) -> tuple[
                              HookNomination, ...]:
    grid = _SeedGrid(rune)
    present = {(link.source, link.destination) for link in rune.links}
    selected: dict[tuple[Any, ...], HookNomination] = {}
    event_index = 0
    while event_index < len(events):
        fire = events[event_index]
        event_index += 1
        if fire["kind"] != "hook-fire" or event_index >= len(events):
            continue
        attach = events[event_index]
        if (attach["kind"] != "hook-attach" or
                attach["hook"] != fire["hook"]):
            continue
        event_index += 1
        if not attach["world"] or event_index >= len(events):
            continue
        release = events[event_index]
        if (release["kind"] != "hook-release" or
                release["hook"] != fire["hook"]):
            continue
        event_index += 1
        landing_index = _stable_landing(steps, release["after_step"])
        if landing_index < 0:
            continue
        ropes = [(fire, attach)]
        if (event_index < len(events) and
                events[event_index]["kind"] == "hook-fire" and
                events[event_index]["after_step"] <
                steps[landing_index]["seq"]):
            second_fire = events[event_index]
            event_index += 1
            if event_index >= len(events):
                continue
            second_attach = events[event_index]
            if (second_attach["kind"] != "hook-attach" or
                    second_attach["hook"] != second_fire["hook"] or
                    not second_attach["world"]):
                continue
            event_index += 1
            ropes.append((second_fire, second_attach))
            landing_index = _stable_landing(
                steps, second_attach["after_step"])
            if landing_index < 0:
                continue
            if (event_index < len(events) and
                    events[event_index]["kind"] == "hook-release" and
                    events[event_index]["hook"] == second_fire["hook"] and
                    events[event_index]["after_step"] <
                    steps[landing_index]["seq"]):
                event_index += 1
            if (event_index < len(events) and
                    events[event_index]["kind"] == "hook-fire" and
                    events[event_index]["after_step"] <
                    steps[landing_index]["seq"]):
                event_index += 1
                continue
        source_index = _previous_dry(steps, fire["after_step"])
        if source_index < 0:
            continue
        if not _safe_hook_interval(steps, source_index, landing_index + 1):
            continue
        duration_ms = ((steps[landing_index]["frame"] -
                        steps[source_index]["frame"]) *
                       int(binding["server_frame_ms"]))
        if duration_ms < 0 or duration_ms > 32767:
            continue
        source_from = grid.nearest_unique(
            tuple(steps[source_index]["after"]["origin"]))
        source_to = grid.nearest_unique(
            tuple(steps[landing_index]["after"]["origin"]))
        if (source_from < 0 or source_to < 0 or source_from == source_to or
                (source_from, source_to) in present or
                rune.seeds[source_from].flags & runeio.RSF_WATER or
                rune.seeds[source_to].flags & runeio.RSF_WATER):
            continue
        aims = [tuple(rope[0]["view_short"]) for rope in ropes]
        bites = [tuple(rope[1]["bite_q8"]) for rope in ropes]
        while len(aims) < 2:
            aims.append((0, 0))
            bites.append((0, 0, 0))
        nomination = HookNomination(
            source_from=source_from,
            source_to=source_to,
            from_origin_q8=grid.seeds[source_from],
            to_origin_q8=grid.seeds[source_to],
            rope_count=len(ropes),
            aim_short=(aims[0], aims[1]),
            bite_q8=(bites[0], bites[1]),
        )
        key = (nomination.from_origin_q8, nomination.to_origin_q8,
               nomination.rope_count, nomination.bite_q8)
        current = selected.get(key)
        if current is None or nomination.aim_short < current.aim_short:
            selected[key] = nomination
    result = tuple(sorted(selected.values(), key=lambda value: (
        value.from_origin_q8, value.to_origin_q8, value.rope_count,
        value.bite_q8, value.aim_short, value.source_from, value.source_to,
    )))
    if len(result) > MAX_HOOK_CANDIDATES:
        raise ValueError("human hook candidate capacity exceeded")
    per_pair: dict[tuple[tuple[int, int, int], tuple[int, int, int]], int] = {}
    for nomination in result:
        pair = (nomination.from_origin_q8, nomination.to_origin_q8)
        per_pair[pair] = per_pair.get(pair, 0) + 1
        if per_pair[pair] > MAX_HOOKS_PER_PAIR:
            raise ValueError("human hook variants per pair exceeded")
    return result


def render_learning(rune: Any, source_sha256: str, trace_sha256: str,
                    replay_sha256: str,
                    candidates: tuple[LearningCandidate, ...],
                    hook_candidates: tuple[HookNomination, ...]) -> str:
    source_sha256 = _sha256(source_sha256, "source rune SHA-256")
    trace_sha256 = _sha256(trace_sha256, "trace SHA-256")
    replay_sha256 = _sha256(replay_sha256, "replay SHA-256")
    header = rune.header
    lines = [
        f"rlearn_format {RLEARN_FORMAT}",
        f"map {header.map_name}",
        f"bsp_checksum {header.bsp_checksum}",
        f"entity_crc {header.entity_crc32}",
        f"physics_flags {header.physics_flags}",
        f"gravity {header.gravity:.9g}",
        f"airaccelerate {header.airaccelerate:.9g}",
        f"maxvelocity {header.maxvelocity:.9g}",
        f"pmove_ms {header.pmove_substep_ms}",
        f"frame_ms {header.server_frame_ms}",
        f"host_physics_id {header.host_physics_id}",
        f"source_route_contract {header.route_contract}",
        f"rune_payload_crc {header.payload_crc32}",
        f"rune_header_crc {header.header_crc32}",
        f"rune_action_contract_crc {header.action_contract_crc32}",
        f"rune_mechanism_contract_crc {header.mechanism_contract_crc32}",
        f"rune_num_seeds {header.num_seeds}",
        f"rune_num_links {header.num_links}",
        f"rune_num_mechanism_nodes {header.num_activation_nodes}",
        f"rune_num_mechanism_edges {header.num_activation_edges}",
        f"rune_num_inventory_edges {header.num_inventory_edges}",
        f"rune_num_mechanism_plans {header.num_activation_plans}",
        f"rune_string_bytes {header.string_bytes}",
        f"rune_sha256 {source_sha256}",
        f"trace_sha256 {trace_sha256}",
        f"replay_sha256 {replay_sha256}",
        f"candidates {len(candidates)}",
    ]
    for candidate in candidates:
        values = (
            candidate.source_from, *candidate.from_origin_q8,
            candidate.source_to, *candidate.to_origin_q8,
            candidate.hint, int(candidate.has_waypoint),
            *candidate.waypoint_q8,
            candidate.first_sequence, candidate.last_sequence,
        )
        lines.append("candidate " + " ".join(map(str, values)))
    lines.append(f"hook_candidates {len(hook_candidates)}")
    for candidate in hook_candidates:
        values = (
            candidate.source_from, *candidate.from_origin_q8,
            candidate.source_to, *candidate.to_origin_q8,
            candidate.rope_count,
            *candidate.aim_short[0], *candidate.aim_short[1],
            *candidate.bite_q8[0], *candidate.bite_q8[1],
        )
        lines.append("hook_candidate " + " ".join(map(str, values)))
    return "\n".join(lines) + "\n"


def _learning_parts(rune: Any, source_sha256: str, replay: Any,
                    replay_sha256: str) -> tuple[
                        str, tuple[LearningCandidate, ...],
                        tuple[HookNomination, ...]]:
    source_sha256 = _sha256(source_sha256, "source rune SHA-256")
    replay_sha256 = _sha256(replay_sha256, "replay SHA-256")
    steps, hook_events, binding, trace_sha256 = _validate_replay(
        rune, source_sha256, replay)
    candidates = build_candidates(rune, steps)
    hook_candidates = build_hook_candidates(
        rune, steps, hook_events, binding)
    return (render_learning(rune, source_sha256, trace_sha256,
                            replay_sha256, candidates, hook_candidates),
            candidates, hook_candidates)


def build_learning(rune: Any, source_sha256: str, replay: Any,
                   replay_sha256: str) -> tuple[
                       str, tuple[LearningCandidate, ...]]:
    text, candidates, _ = _learning_parts(
        rune, source_sha256, replay, replay_sha256)
    return text, candidates


def build_legacy_learning(rune: Any, source_sha256: str, replay: Any,
                          replay_sha256: str) -> tuple[
                              str, tuple[HookNomination, ...]]:
    source_sha256 = _sha256(source_sha256, "source rune SHA-256")
    replay_sha256 = _sha256(replay_sha256, "replay SHA-256")
    steps, hook_events, binding, trace_sha256 = _validate_replay(
        rune, source_sha256, replay)
    if hook_events:
        raise ValueError("legacy hook recovery cannot consume hook events")
    hooks = build_legacy_hook_candidates(
        rune, steps, binding, replay["format"])
    return (render_learning(rune, source_sha256, trace_sha256,
                            replay_sha256, (), hooks), hooks)


def build_combined_legacy_learning(rune: Any, source_sha256: str,
                                   replay: Any, replay_sha256: str) -> tuple[
                                       str,
                                       tuple[LearningCandidate, ...],
                                       tuple[HookNomination, ...]]:
    if (not isinstance(replay, dict) or
            replay.get("format") != humantrace.EVIDENCE_FORMAT_V1):
        raise ValueError("combined legacy recovery requires v1 evidence")
    source_sha256 = _sha256(source_sha256, "source rune SHA-256")
    replay_sha256 = _sha256(replay_sha256, "replay SHA-256")
    steps, hook_events, binding, trace_sha256 = _validate_replay(
        rune, source_sha256, replay)
    if hook_events:
        raise ValueError("combined legacy recovery cannot consume hook events")
    hooks = build_legacy_hook_candidates(
        rune, steps, binding, replay["format"])
    candidates = build_candidates(rune, steps)
    return (render_learning(rune, source_sha256, trace_sha256,
                            replay_sha256, candidates, hooks),
            candidates, hooks)


def _json_no_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for name, value in pairs:
        if name in result:
            raise ValueError(f"duplicate replay field {name}")
        result[name] = value
    return result


def _read_replay(path: Path) -> tuple[dict[str, Any], str]:
    with path.open("rb") as stream:
        size = os.fstat(stream.fileno()).st_size
        if size > MAX_REPLAY_BYTES:
            raise ValueError("replay exceeds bounded input size")
        payload = stream.read(MAX_REPLAY_BYTES + 1)
    if len(payload) != size:
        raise ValueError("replay changed while reading")
    try:
        value = json.loads(payload, object_pairs_hook=_json_no_duplicates)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid replay JSON: {error}") from error
    if not isinstance(value, dict):
        raise ValueError("replay root must be an object")
    return value, hashlib.sha256(payload).hexdigest()


def _read_rune(path: Path) -> tuple[Any, str]:
    with path.open("rb") as stream:
        size = os.fstat(stream.fileno()).st_size
        if size > runeio.MAX_RUNE_FILE_BYTES:
            raise ValueError("source RUNE exceeds bounded input size")
        payload = stream.read(runeio.MAX_RUNE_FILE_BYTES + 1)
    if len(payload) != size:
        raise ValueError("source RUNE changed while reading")
    return runeio.decode_rune(payload), hashlib.sha256(payload).hexdigest()


def _atomic_write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="ascii", newline="\n") as out:
            out.write(text)
            out.flush()
            os.fsync(out.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def build_file(rune_path: Path, replay_path: Path, output_path: Path) -> int:
    rune, source_sha256 = _read_rune(rune_path)
    replay, replay_sha256 = _read_replay(replay_path)
    text, candidates, hook_candidates = _learning_parts(
        rune, source_sha256, replay, replay_sha256)
    _atomic_write(output_path, text)
    return len(candidates) + len(hook_candidates)


def recover_file(rune_path: Path, replay_path: Path,
                 output_path: Path) -> None:
    rune, source_sha256 = _read_rune(rune_path)
    replay, _ = _read_replay(replay_path)
    recovered = recover_replay(rune, source_sha256, replay)
    text = json.dumps(recovered, indent=2, sort_keys=True) + "\n"
    _atomic_write(output_path, text)


def build_legacy_file(rune_path: Path, replay_path: Path,
                      output_path: Path) -> int:
    rune, source_sha256 = _read_rune(rune_path)
    replay, replay_sha256 = _read_replay(replay_path)
    text, hooks = build_legacy_learning(
        rune, source_sha256, replay, replay_sha256)
    _atomic_write(output_path, text)
    return len(hooks)


def build_combined_legacy_file(rune_path: Path, replay_path: Path,
                               output_path: Path) -> int:
    rune, source_sha256 = _read_rune(rune_path)
    replay, replay_sha256 = _read_replay(replay_path)
    text, candidates, hooks = build_combined_legacy_learning(
        rune, source_sha256, replay, replay_sha256)
    _atomic_write(output_path, text)
    return len(candidates) + len(hooks)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    build = subparsers.add_parser("build")
    build.add_argument("--rune", required=True, type=Path)
    build.add_argument("--replay", required=True, type=Path)
    build.add_argument("--output", required=True, type=Path)
    legacy = subparsers.add_parser("legacy-build")
    legacy.add_argument("--rune", required=True, type=Path)
    legacy.add_argument("--replay", required=True, type=Path)
    legacy.add_argument("--output", required=True, type=Path)
    combined = subparsers.add_parser("legacy-combined-build")
    combined.add_argument("--rune", required=True, type=Path)
    combined.add_argument("--replay", required=True, type=Path)
    combined.add_argument("--output", required=True, type=Path)
    recover = subparsers.add_parser("recover")
    recover.add_argument("--rune", required=True, type=Path)
    recover.add_argument("--replay", required=True, type=Path)
    recover.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "recover":
            recover_file(args.rune, args.replay, args.output)
            count = None
        elif args.command == "legacy-build":
            count = build_legacy_file(
                args.rune, args.replay, args.output)
        elif args.command == "legacy-combined-build":
            count = build_combined_legacy_file(
                args.rune, args.replay, args.output)
        else:
            count = build_file(args.rune, args.replay, args.output)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if count is None:
        print(f"WROTE {args.output}: provenance=posthoc-identity-exact")
    else:
        print(f"WROTE {args.output}: nominations={count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
