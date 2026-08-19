#!/usr/bin/env python3
"""Frozen Stage-A time, approach, and conversion calculations.

This module deliberately has no plotting or third-party dependency.  The
production trial receipt generator imports it, while host tests exercise the
same functions with boundary cases.  Authoritative steals and captures remain
the timestamped StdLog score events; final database and host totals reconcile
the complete event stream separately.
"""

from __future__ import annotations

from collections.abc import Mapping
import math
import statistics


WINDOW_SECONDS = 600.0
DEMO_FPS = 10.0
APPROACH_UNITS = 384.0
CLOSE_SECONDS = 1.5
DROP_RETURN_SECONDS = 30.0
CTF_TEAMS = frozenset(("red", "blue"))
FLAG_EVENT_KINDS = frozenset(
    ("F Pickup", "F Capture", "F Return", "FC LostFlag"))


def _sequence(value, label):
    if isinstance(value, (str, bytes, Mapping)):
        raise ValueError(f"{label} must be a sequence")
    try:
        return tuple(value)
    except TypeError as error:
        raise ValueError(f"{label} must be a sequence") from error


def _finite_number(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label} must be a finite number")
    try:
        number = float(value)
    except (OverflowError, TypeError, ValueError) as error:
        raise ValueError(f"{label} must be a finite number") from error
    if not math.isfinite(number):
        raise ValueError(f"{label} must be a finite number")
    return number


def _integer(value, label, minimum=None):
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{label} must be an integer")
    try:
        finite = math.isfinite(float(value))
    except OverflowError as error:
        raise ValueError(f"{label} is outside the finite measurement range") \
            from error
    if not finite:
        raise ValueError(f"{label} is outside the finite measurement range")
    if minimum is not None and value < minimum:
        raise ValueError(f"{label} must be at least {minimum}")
    return value


def _validated_roster(roster):
    names = _sequence(roster, "roster")
    if not names:
        raise ValueError("roster must not be empty")
    if any(not isinstance(name, str) or not name for name in names):
        raise ValueError("roster names must be non-empty strings")
    if len(set(names)) != len(names):
        raise ValueError("roster must not contain duplicates")
    return names


def _validated_window(window):
    values = _sequence(window, "evaluation window")
    if len(values) != 2:
        raise ValueError("evaluation window must contain start and end")
    start = _finite_number(values[0], "evaluation window start")
    end = _finite_number(values[1], "evaluation window end")
    if start >= end:
        raise ValueError("evaluation window must be increasing")
    return start, end


def _timed_records(records, label, require_sorted=True):
    values = _sequence(records, label)
    validated = []
    previous = None
    for index, record in enumerate(values):
        if not isinstance(record, Mapping) or "time" not in record:
            raise ValueError(f"{label}[{index}] is missing a time")
        moment = _finite_number(record["time"], f"{label}[{index}].time")
        if require_sorted and previous is not None and moment < previous:
            raise ValueError(f"{label} must be sorted by time")
        validated.append((record, moment))
        previous = moment
    return validated


def _validated_teams(teams):
    if not isinstance(teams, Mapping):
        raise ValueError("teams must be a mapping")
    validated = {}
    for name, team in teams.items():
        if not isinstance(name, str) or not name:
            raise ValueError("team names must be non-empty strings")
        if not isinstance(team, str) or team not in CTF_TEAMS:
            raise ValueError(f"invalid CTF team for {name}: {team}")
        validated[name] = team
    return validated


def _validated_flag_events(events, teams):
    validated_teams = _validated_teams(teams)
    timed = _timed_records(events, "flag events")
    validated = []
    previous_order = None
    for index, (event, moment) in enumerate(timed):
        name = event.get("name")
        kind = event.get("kind")
        if not isinstance(name, str) or name not in validated_teams:
            raise ValueError(f"flag events[{index}] has unknown name: {name}")
        if not isinstance(kind, str) or kind not in FLAG_EVENT_KINDS:
            raise ValueError(f"flag events[{index}] has unknown kind: {kind}")
        if "log_order" not in event:
            raise ValueError(
                f"flag events[{index}] is missing production log_order")
        order = _integer(event["log_order"],
                         f"flag events[{index}].log_order", minimum=0)
        if previous_order is not None and order <= previous_order:
            raise ValueError("flag events must follow unique production log_order")
        validated.append((event, moment, order))
        previous_order = order
    return validated, validated_teams


def _flag_home_before_validated(color, moment, events, teams):
    state = {"red": "home", "blue": "home"}
    deadline = {"red": None, "blue": None}

    def expire(strictly_before):
        for flag_color in CTF_TEAMS:
            value = deadline[flag_color]
            if value is not None and value < strictly_before:
                state[flag_color] = "home"
                deadline[flag_color] = None

    for event, event_time, _order in events:
        expire(min(moment, event_time))
        if event_time >= moment:
            break
        team = teams[event["name"]]
        kind = event["kind"]
        if kind == "F Pickup":
            flag_color = flag_color_for_thief(team)
            state[flag_color] = "carried"
            deadline[flag_color] = None
        elif kind == "FC LostFlag":
            flag_color = flag_color_for_thief(team)
            state[flag_color] = "dropped"
            return_time = event_time + DROP_RETURN_SECONDS
            if not math.isfinite(return_time) or return_time <= event_time:
                raise ValueError("automatic flag-return time is not representable")
            deadline[flag_color] = return_time
        elif kind == "F Return":
            state[team] = "home"
            deadline[team] = None
        else:  # F Capture
            state = {"red": "home", "blue": "home"}
            deadline = {"red": None, "blue": None}
    expire(moment)
    return state[color] == "home"


def exact_window(joined, left, roster, duration=WINDOW_SECONDS):
    """Return the half-open full-roster server-time window ``[start, end)``.

    The start is the latest authoritative PlayerTeamChange time in the exact
    roster.  Every roster member must remain connected through ``end``.  This makes the
    evaluated duration independent of wall-clock/server scheduling drift.
    """
    roster = _validated_roster(roster)
    if not isinstance(joined, Mapping) or not isinstance(left, Mapping):
        raise ValueError("connect and leave streams must be mappings")
    expected = set(roster)
    if set(joined) != expected or set(left) != expected:
        raise ValueError("evaluation roster does not match connect/leave stream")
    duration = _finite_number(duration, "evaluation duration")
    if duration <= 0.0:
        raise ValueError("evaluation duration must be positive and finite")
    joined_times = {
        name: _finite_number(joined[name], f"joined[{name}]")
        for name in roster
    }
    left_times = {
        name: _finite_number(left[name], f"left[{name}]")
        for name in roster
    }
    start = max(joined_times.values())
    end = start + duration
    if not math.isfinite(end) or end <= start:
        raise ValueError("evaluation window end must be finite and increasing")
    early = [name for name in roster if left_times[name] < end]
    if early:
        raise ValueError("roster left before exact evaluation window ended: " +
                         ", ".join(sorted(early)))
    return start, end


def in_window(moment, window):
    """The frozen crop is start-inclusive and end-exclusive."""
    start, end = _validated_window(window)
    moment = _finite_number(moment, "event time")
    return start <= moment < end


def crop_events(events, window):
    """Return timestamped records in the exact half-open window."""
    window = _validated_window(window)
    timed = _timed_records(events, "events")
    return [event for event, moment in timed
            if window[0] <= moment < window[1]]


def demo_level_offset(connected, first_entity_frames, fps=DEMO_FPS):
    """Median PlayerConnect minus first serverrecord entity-frame seconds."""
    if (not isinstance(connected, Mapping) or
            not isinstance(first_entity_frames, Mapping) or
            not connected or set(connected) != set(first_entity_frames)):
        raise ValueError("demo alignment roster mismatch")
    fps = _finite_number(fps, "demo fps")
    if fps <= 0.0:
        raise ValueError("demo fps must be positive")
    offsets = []
    for name in connected:
        connected_time = _finite_number(
            connected[name], f"connected[{name}]")
        first_frame = _finite_number(
            first_entity_frames[name], f"first_entity_frames[{name}]")
        offset = connected_time - first_frame / fps
        if not math.isfinite(offset):
            raise ValueError("demo alignment offset must be finite")
        offsets.append(offset)
    return statistics.median(offsets)


def flag_color_for_thief(team):
    if team == "red":
        return "blue"
    if team == "blue":
        return "red"
    raise ValueError(f"invalid CTF team: {team}")


def flag_home_before(color, moment, events, teams):
    """Reconstruct public home/carried/dropped state strictly before moment.

    F Pickup carries the enemy flag, FC LostFlag drops it, F Return returns
    the actor's team flag, and F Capture homes both flags.  Production's
    ``ctf_flagwave`` returns a dropped flag only when ``level.time`` is
    strictly greater than ``droptime + 30``; equality remains dropped. Events
    must be timestamp-sorted with strictly increasing production ``log_order``;
    same-timestamp events execute in that log order.
    """
    if not isinstance(color, str) or color not in CTF_TEAMS:
        raise ValueError(f"invalid flag color: {color}")
    moment = _finite_number(moment, "flag query time")
    validated_events, validated_teams = _validated_flag_events(events, teams)
    return _flag_home_before_validated(
        color, moment, validated_events, validated_teams)


def qualifying_approaches(name, team, track, stand, level_offset, events,
                          teams, window, fps=DEMO_FPS):
    """Return outside-to-inside 384u entries while the enemy flag is home."""
    validated_teams = _validated_teams(teams)
    if not isinstance(name, str) or name not in validated_teams:
        raise ValueError(f"approach actor has unknown name: {name}")
    if (not isinstance(team, str) or team not in CTF_TEAMS or
            validated_teams[name] != team):
        raise ValueError(f"approach actor has invalid team: {team}")
    rows = _sequence(track, "serverrecord track")
    if len(rows) < 2:
        raise ValueError("serverrecord track needs at least two frames")
    validated_track = []
    previous_frame = None
    for index, row in enumerate(rows):
        values = _sequence(row, f"serverrecord track[{index}]")
        if len(values) != 5:
            raise ValueError(
                f"serverrecord track[{index}] must contain five values")
        frame = _integer(values[0], f"serverrecord track[{index}].frame",
                         minimum=0)
        if previous_frame is not None and frame <= previous_frame:
            raise ValueError("serverrecord track frames must be increasing")
        x = _finite_number(values[1], f"serverrecord track[{index}].x")
        y = _finite_number(values[2], f"serverrecord track[{index}].y")
        z = _finite_number(values[3], f"serverrecord track[{index}].z")
        effects = _integer(values[4],
                           f"serverrecord track[{index}].effects", minimum=0)
        validated_track.append((frame, x, y, z, effects))
        previous_frame = frame
    stand_values = _sequence(stand, "flag stand")
    if len(stand_values) != 3:
        raise ValueError("flag stand must contain three coordinates")
    validated_stand = tuple(
        _finite_number(value, f"flag stand[{index}]")
        for index, value in enumerate(stand_values))
    level_offset = _finite_number(level_offset, "demo level offset")
    fps = _finite_number(fps, "demo fps")
    if fps <= 0.0:
        raise ValueError("demo fps must be positive")
    window = _validated_window(window)
    validated_events, validated_teams = _validated_flag_events(
        events, validated_teams)
    approaches = []
    enemy_color = flag_color_for_thief(team)
    for before, after in zip(validated_track, validated_track[1:]):
        f0, x0, y0, _z0, _effects0 = before
        f1, x1, y1, _z1, _effects1 = after
        if f1 - f0 != 1:
            continue
        distance0 = math.hypot(
            x0 - validated_stand[0], y0 - validated_stand[1])
        distance1 = math.hypot(
            x1 - validated_stand[0], y1 - validated_stand[1])
        if not math.isfinite(distance0) or not math.isfinite(distance1):
            raise ValueError("approach distance must be finite")
        if distance0 < APPROACH_UNITS or distance1 >= APPROACH_UNITS:
            continue
        level_time = f1 / fps + level_offset
        if not math.isfinite(level_time):
            raise ValueError("approach server time must be finite")
        if not window[0] <= level_time < window[1]:
            continue
        if not _flag_home_before_validated(
                enemy_color, level_time, validated_events, validated_teams):
            continue
        approaches.append({"name": name, "team": team,
                           "time": level_time})
    return approaches


def match_close_pickups(approaches, pickups, delay=CLOSE_SECONDS):
    """One-to-one same-player matching in the inclusive [0.0, 1.5] band."""
    delay = _finite_number(delay, "close-pickup delay")
    if delay < 0.0:
        raise ValueError("close-pickup delay must not be negative")
    validated_approaches = _timed_records(
        approaches, "approaches", require_sorted=False)
    validated_pickups = _timed_records(pickups, "pickups")
    unused = []
    for index, (approach, moment) in enumerate(validated_approaches):
        name = approach.get("name")
        team = approach.get("team")
        if not isinstance(name, str) or not name:
            raise ValueError(f"approaches[{index}] has an invalid name")
        if not isinstance(team, str) or team not in CTF_TEAMS:
            raise ValueError(f"approaches[{index}] has an invalid team")
        unused.append({**approach, "time": moment, "matched": False})
    ordered_pickups = []
    for index, (pickup, moment) in enumerate(validated_pickups):
        name = pickup.get("name")
        if not isinstance(name, str) or not name:
            raise ValueError(f"pickups[{index}] has an invalid name")
        if pickup.get("kind") != "F Pickup":
            raise ValueError(f"pickups[{index}] is not an F Pickup event")
        ordered_pickups.append({**pickup, "time": moment})
    matches = []
    for pickup in ordered_pickups:
        candidates = []
        for approach in unused:
            if approach["matched"] or approach["name"] != pickup["name"]:
                continue
            elapsed = pickup["time"] - approach["time"]
            if not math.isfinite(elapsed):
                raise ValueError("pickup-to-approach delay must be finite")
            if 0.0 <= elapsed <= delay:
                candidates.append(approach)
        if not candidates:
            continue
        approach = max(candidates, key=lambda item: item["time"])
        approach["matched"] = True
        matches.append({"approach": approach, "pickup": pickup,
                        "delay": pickup["time"] - approach["time"]})
    return matches


def crop_diagnostic_rows(rows, start_line, end_line, roster):
    """Crop emitted SG rows by conservative authoritative log brackets.

    SG Think_Emit is absent while a bot is dead, so these rows are diagnostic
    observations, not a gap-free clock.  The receipt generator supplies the
    final roster-join line and the last host outcome line whose matched StdLog
    timestamp is strictly before the evaluation end.  This function retains
    only rows inside those brackets and rejects malformed, duplicate, or
    out-of-order emitted rows; it never invents death-time samples.
    """
    roster = set(_validated_roster(roster))
    start_line = _integer(start_line, "diagnostic start line", minimum=0)
    end_line = _integer(end_line, "diagnostic end line", minimum=0)
    if start_line >= end_line:
        raise ValueError("diagnostic brackets are empty or reversed")
    kept = []
    seen = set()
    previous = None
    for index, row in enumerate(_sequence(rows, "diagnostic rows")):
        if not isinstance(row, Mapping):
            raise ValueError(f"diagnostic rows[{index}] is malformed")
        line = row.get("line")
        name = row.get("name")
        if (isinstance(line, bool) or not isinstance(line, int) or line < 0 or
                not isinstance(name, str) or name not in roster):
            raise ValueError("malformed diagnostic row")
        if (previous is not None and line <= previous) or line in seen:
            raise ValueError("duplicate or out-of-order diagnostic row")
        seen.add(line)
        previous = line
        if start_line < line <= end_line:
            kept.append(row)
    if not kept:
        raise ValueError("no diagnostic SG rows inside authoritative brackets")
    return kept


def _validated_field_names(values, label):
    fields = _sequence(values, label)
    if not fields:
        raise ValueError(f"{label} must not be empty")
    if any(not isinstance(field, str) or not field for field in fields):
        raise ValueError(f"{label} must contain non-empty strings")
    if len(set(fields)) != len(fields):
        raise ValueError(f"{label} must not contain duplicates")
    return fields


def receipt_identity_mismatches(receipts, required_equal_fields,
                                 required_per_arm_fields,
                                 required_rune_identity_fields):
    """Return exact top-level receipt fields that differ from the first arm.

    All contract-required receipt and nested RUNE-identity keys are checked
    before the matched fields are compared. Missing keys, duplicate contract
    fields, or an equality field absent from the receipt schema are failures,
    never mismatches that can be ignored.
    """
    records = _sequence(receipts, "receipts")
    if len(records) < 2:
        raise ValueError("at least two receipts are required")
    fields = _validated_field_names(
        required_equal_fields, "required equal fields")
    receipt_fields = _validated_field_names(
        required_per_arm_fields, "required per-arm fields")
    rune_fields = _validated_field_names(
        required_rune_identity_fields, "required RUNE identity fields")
    if not set(fields) <= set(receipt_fields):
        raise ValueError("required equal fields are absent from receipt schema")
    if "rune_identity" not in receipt_fields:
        raise ValueError("receipt schema must capture rune_identity")
    for index, receipt in enumerate(records):
        if not isinstance(receipt, Mapping):
            raise ValueError(f"receipts[{index}] must be a mapping")
        missing = [field for field in receipt_fields if field not in receipt]
        if missing:
            raise ValueError(
                f"receipts[{index}] is missing: {', '.join(missing)}")
        rune_identity = receipt["rune_identity"]
        if not isinstance(rune_identity, Mapping):
            raise ValueError(f"receipts[{index}].rune_identity must be a mapping")
        missing = [field for field in rune_fields if field not in rune_identity]
        if missing:
            raise ValueError(
                f"receipts[{index}].rune_identity is missing: " +
                ", ".join(missing))
    first = records[0]
    return [field for field in fields
            if any(receipt[field] != first[field] for receipt in records[1:])]
