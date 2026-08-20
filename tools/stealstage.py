#!/usr/bin/env python3
"""Frozen Stage-A time, approach, and conversion calculations.

This module deliberately has no plotting or third-party dependency.  The
production trial receipt generator imports it, while host tests exercise the
same functions with boundary cases.  Authoritative steals and captures remain
the timestamped StdLog score events; final database and host totals reconcile
the complete event stream separately.
"""

from __future__ import annotations

import argparse
from collections import Counter
from collections.abc import Mapping
from contextlib import contextmanager
from decimal import Decimal, InvalidOperation
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import secrets
import shutil
import sqlite3
import statistics
import stat
import struct
import subprocess
import sys
import tempfile


WINDOW_SECONDS = 600.0
DEMO_FPS = 10.0
APPROACH_UNITS = 384.0
CLOSE_SECONDS = 1.5
DROP_RETURN_SECONDS = 30.0
CTF_TEAMS = frozenset(("red", "blue"))
FLAG_EVENT_KINDS = frozenset(
    ("F Pickup", "F Capture", "F Return", "FC LostFlag"))
METRIC_VERSION = "steal-close-stage-a-v1"
RECEIPT_FORMAT = "lmctf-steal-stage-a-receipt-v1"
RESULT_FORMAT = "lmctf-steal-stage-a-result-v1"
SOURCE_TREE_FORMAT = "lmctf-steal-source-tree-v1"
BUILD_RECEIPT_FORMAT = "lmctf-steal-source-build-v1"
KNOWLEDGE_REPORT_FORMAT = "lmctf-steal-source-policy-report-v1"
MEASUREMENT_IMPLEMENTATION_FORMAT = "lmctf-steal-measurement-implementation-v1"
EMPTY_PATCH_SHA256 = hashlib.sha256(b"").hexdigest()
POLICY_PROBE_PATH = "tests/test_offense_flag_pickup_recovery.py"
POLICY_PROBE_SHA256 = (
    "1e915b9cde0890cfa2833943e359f9fa56242b9f2b5084517f4beb89cd0c5208")
POLICY_PROBE_IMPORT_SHADOWS = (
    "json.py", "unittest.py", "pathlib.py", "math.py", "re.py",
)
SOURCE_BUILD_RECIPE = "gnu-make-stage-a-v2"
SOURCE_BUILD_YEAR = "2026"
SOURCE_BUILD_INPUTS = ("GNUmakefile", "GitRevisionInfo.tmpl")
MEASUREMENT_IMPLEMENTATION_PATHS = (
    "tools/stealstage.py", "tools/runeio.py",
    "tools/rune_contracts_generated.py", "tools/bspmechanisms.py",
    "tools/mapflags.py", "tools/corpusgraph.py", "tools/dm2speed.py",
    "tools/demokin.py",
)
METRIC_CONTRACT_SHA256 = (
    "317d1bfccce5dc13dbe100bfce238ef1b62c76028f7e18926255ac89e2268104")
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
COMMIT_RE = re.compile(r"[0-9a-f]{40}\Z")
SAFE_ID_RE = re.compile(r"[a-z0-9][a-z0-9._-]{0,63}\Z")
MAX_TEXT_BYTES = 64 * 1024 * 1024
MAX_DATABASE_BYTES = 256 * 1024 * 1024
MAX_DEMO_BYTES = 2 * 1024 * 1024 * 1024
MAX_BINARY_BYTES = 512 * 1024 * 1024
REQUIRED_ARTIFACTS = (
    "engine", "module", "rune", "bsp", "recording_harness",
    "server_log", "stdlog", "stats_database", "serverrecord",
)
SOURCE_ARTIFACTS = (
    "source_patch", "source_manifest", "build_receipt", "knowledge_report",
)
BUILD_RECEIPT_FIELDS = (
    "format", "metric_version", "recipe", "source_commit",
    "source_patch_sha256", "source_tree_sha256", "module_sha256",
    "build_input_sha256", "revision_header_sha256",
)
RUNE_IDENTITY_FIELDS = (
    "map", "bsp_checksum", "entity_crc", "physics_flags", "gravity",
    "airaccelerate", "maxvelocity", "pmove_ms", "frame_ms",
    "host_physics_id", "rune_payload_crc", "rune_header_crc",
    "action_contract_crc", "mechanism_contract_crc", "num_seeds",
    "num_links", "num_activation_nodes", "num_activation_edges",
    "num_activation_plans", "num_inventory_edges", "string_bytes",
)
KNOWLEDGE_REPORT_FIELDS = (
    "format", "metric_version", "source_commit", "source_patch_sha256",
    "source_tree_sha256", "module_sha256", "measurement_tool_sha256",
    "measurement_implementation_sha256", "policy_probe_sha256", "tests_run",
    "failures", "errors", "skipped", "successful",
)
RESULT_ROUND_FIELDS = (
    "name", "arm", "round", "port", "root_identity", "map",
    "source_identity", "roster_and_team_assignment", "artifact_sha256",
    "configuration_sha256", "rune_identity", "stand_origins",
    "evaluation_window_server_seconds", "window_counts", "telemetry",
    "database_full_stream", "demo", "authority",
)
AGGREGATE_RAW_FIELDS = (
    "bot_seconds", "team_seconds", "distance", "samples", "moving",
    "engaged", "defenders", "defender_dwell", "defender_moving",
    "departures", "suicides", "frags", "fragged", "pickups", "captures",
    "approaches", "observed_stand_seconds", "timely", "forbidden",
    "mismatches",
)
AGGREGATE_RATE_FIELDS = (
    "horizontal_distance_per_active_bot_minute", "moving_sample_fraction",
    "world_or_hazard_suicides_per_active_bot_minute",
    "combat_kills_per_active_team_minute",
    "combat_deaths_per_active_team_minute",
    "visible_or_audible_engagements_per_active_team_minute",
    "authoritative_pickups_per_active_team_minute",
    "approaches_per_observed_stand_minute", "close_approach_conversion",
    "authoritative_captures_per_active_team_minute",
    "steal_to_capture_conversion", "defender_post_dwell_fraction",
    "defender_moving_sample_fraction",
    "defender_departures_per_active_defender_minute",
    "captures_conceded_per_active_team_minute",
)
AGGREGATE_TEAM_RAW_FIELDS = (
    "team_seconds", "distance", "samples", "moving", "engaged",
    "defenders", "defender_dwell", "defender_moving", "departures",
    "frags", "fragged", "pickups", "captures", "approaches",
    "observed_stand_seconds", "timely", "captures_conceded",
)
AGGREGATE_TEAM_RATE_FIELDS = (
    "combat_kills_per_active_team_minute",
    "combat_deaths_per_active_team_minute",
    "visible_or_audible_engagements_per_active_team_minute",
    "authoritative_pickups_per_active_team_minute",
    "approaches_per_observed_stand_minute", "close_approach_conversion",
    "authoritative_captures_per_active_team_minute",
    "defender_post_dwell_fraction", "defender_moving_sample_fraction",
    "defender_departures_per_active_defender_minute",
    "captures_conceded_per_active_team_minute",
)
RESULT_CHECK_FIELDS = frozenset((
    "movement.distance_ratio", "movement.moving_fraction_delta",
    "movement.suicide_rate_delta", "combat.kill_rate_ratio",
    "combat.death_rate_ratio", "perception.engagement_rate_ratio",
    "perception.forbidden_knowledge_events", "steal.pickup_rate_ratio",
    "steal.pickup_rate_delta", "steal.pickup_count_delta",
    "steal.approach_rate_ratio", "conversion.absolute_delta",
    "conversion.ratio", "conversion.timely_count_delta",
    "capture.rate_ratio", "capture.steal_to_capture_delta",
    "capture.reconciliation_mismatches",
    "defense.defender_post_dwell_fraction_ratio",
    "defense.defender_moving_sample_fraction_ratio",
    "defense.departure_rate_ratio", "defense.captures_conceded_ratio",
    "defense.captures_conceded_delta",
))
SG_REPORT_RE = re.compile(
    r'^SG (?P<name>\S+): role=(?P<role>\d+) seed=(?P<seed>-?\d+) '
    r'goal=(?P<goal>-?\d+) sgoal=(?P<sgoal>-?\d+) spd=(?P<speed>\d+) '
    r'org=\((?P<x>-?\d+) (?P<y>-?\d+) (?P<z>-?\d+)\) '
    r'link=(?P<link>-?\d+) act=(?P<action>-?\d+) '
    r'hp=(?P<hook_phase>\d+) dh=(?P<door_hold>\d+) '
    r'dl=(?P<drop_locked>\d+) '
    r'st=(?P<stuck>\d+\.\d) gnd=(?P<grounded>[01]) '
    r'eng=(?P<engaged>[01]) frm=(?P<frame>\d+)$')
SG_CENSUS_RE = re.compile(
    r'^SGCENSUS (?P<name>\S+): frm=(?P<frame>\d+) '
    r'alive=(?P<alive>[01])$')
SG_ROLE_VALUES = frozenset(range(5))
SG_RUNTIME_ACTION_VALUES = frozenset((-1, 0, 1, 2, 3, 4, 5, 6, 8, 12))
SG_INT_MAX = 2_147_483_647

# Quake II delta-entity wire flags used by the dependency-free authenticated
# serverrecord reader below.
U_ORIGIN1 = 1 << 0
U_ORIGIN2 = 1 << 1
U_ANGLE2 = 1 << 2
U_ANGLE3 = 1 << 3
U_FRAME8 = 1 << 4
U_EVENT = 1 << 5
U_REMOVE = 1 << 6
U_ORIGIN3 = 1 << 9
U_ANGLE1 = 1 << 10
U_MODEL = 1 << 11
U_RENDERFX8 = 1 << 12
U_EFFECTS8 = 1 << 14
U_SKIN8 = 1 << 16
U_FRAME16 = 1 << 17
U_RENDERFX16 = 1 << 18
U_EFFECTS16 = 1 << 19
U_MODEL2 = 1 << 20
U_MODEL3 = 1 << 21
U_MODEL4 = 1 << 22
U_OLDORIGIN = 1 << 24
U_SKIN16 = 1 << 25
U_SOUND = 1 << 26
U_SOLID = 1 << 27


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


def _decimal_number(value, label):
    """Return the exact base-10 value represented by a JSON number.

    Python binary floats are first rendered with their shortest round-tripping
    decimal spelling.  Contract boundaries are then compared in Decimal space;
    this admits an exact 0.7 -> 2.2 interval while still rejecting the next
    representable declared decimal unit outside the band.
    """
    number = _finite_number(value, label)
    try:
        result = Decimal(str(value))
    except (InvalidOperation, TypeError, ValueError) as error:
        raise ValueError(f"{label} must be an exact decimal number") from error
    if not result.is_finite():
        raise ValueError(f"{label} must be an exact finite decimal number")
    return result, number


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
    exact_delay, delay = _decimal_number(delay, "close-pickup delay")
    if exact_delay < 0:
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
            elapsed = (Decimal(str(pickup["time"])) -
                       Decimal(str(approach["time"])))
            if 0 <= elapsed <= exact_delay:
                candidates.append(approach)
        if not candidates:
            continue
        # Earliest feasible is the interval-scheduling greedy that preserves
        # later approaches for later pickups.  Choosing the nearest/latest
        # feasible approach can reduce cardinality (A@0,1; P@1.2,2.4).
        approach = min(candidates, key=lambda item: item["time"])
        approach["matched"] = True
        matches.append({"approach": approach, "pickup": pickup,
                        "delay": float(Decimal(str(pickup["time"])) -
                                       Decimal(str(approach["time"])))})
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


def _exact_object(value, fields, label):
    if not isinstance(value, Mapping):
        raise ValueError(f"{label} must be an object")
    expected = set(fields)
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise ValueError(
            f"{label} has incorrect schema; missing={missing}, extra={extra}")
    return value


def _strict_json(payload, label):
    try:
        text = bytes(payload).decode("utf-8")
    except (TypeError, UnicodeDecodeError) as error:
        raise ValueError(f"{label} must be UTF-8 JSON") from error

    def object_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"{label} contains duplicate key {key!r}")
            result[key] = value
        return result

    try:
        result = json.loads(text, object_pairs_hook=object_pairs,
                            parse_constant=lambda token: (_ for _ in ()).throw(
                                ValueError(f"non-finite JSON number {token}")))
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise ValueError(f"{label} is invalid JSON") from error
    return result


def canonical_json(value):
    """Canonical bytes used by result/report hashes (not file formatting)."""
    return (json.dumps(value, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")


def sha256_bytes(value):
    return hashlib.sha256(bytes(value)).hexdigest()


def measurement_implementation(repository=None):
    """Rehash the complete repo-local implementation used by Stage A."""
    repository = (Path(__file__).resolve().parents[1] if repository is None
                  else Path(repository))
    if not repository.is_absolute():
        raise ValueError("measurement implementation root must be absolute")
    files = {}
    for relative in MEASUREMENT_IMPLEMENTATION_PATHS:
        payload = _read_cli_file(repository / relative, MAX_TEXT_BYTES,
                                 f"measurement implementation {relative}")
        files[relative] = sha256_bytes(payload)
    manifest = {"format": MEASUREMENT_IMPLEMENTATION_FORMAT,
                "files": files}
    return manifest, sha256_bytes(canonical_json(manifest))


def _require_bound_helper(module, relative, implementation):
    expected = Path(__file__).resolve().parents[1] / relative
    actual = getattr(module, "__file__", None)
    if actual is None or Path(os.path.abspath(actual)) != expected:
        raise ValueError(f"measurement helper loaded from wrong path: {relative}")
    payload = _read_cli_file(expected, MAX_TEXT_BYTES,
                             f"loaded measurement helper {relative}")
    if sha256_bytes(payload) != implementation["files"][relative]:
        raise ValueError(f"loaded measurement helper hash drifted: {relative}")
    return module


def _sha256(value, label):
    if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
        raise ValueError(f"{label} must be 64 lowercase hexadecimal characters")
    return value


def _commit(value, label):
    if not isinstance(value, str) or COMMIT_RE.fullmatch(value) is None:
        raise ValueError(f"{label} must be a 40-character lowercase Git commit")
    return value


def _safe_id(value, label):
    if not isinstance(value, str) or SAFE_ID_RE.fullmatch(value) is None:
        raise ValueError(f"{label} is not a safe identifier")
    return value


def result_hash(document):
    """Hash a result with its top-level ``result_sha256`` key omitted."""
    if not isinstance(document, Mapping):
        raise ValueError("result must be an object")
    return sha256_bytes(canonical_json({
        key: value for key, value in document.items()
        if key != "result_sha256"
    }))


def bind_result_hashes(document):
    """Bind non-self-referential report and result digests.

    ``report_sha256`` hashes only the canonical ``report`` value.  Then
    ``result_sha256`` hashes the canonical complete result with that one field
    omitted.  A digest never includes itself.
    """
    if not isinstance(document, dict) or "report" not in document:
        raise ValueError("result must contain report")
    if "result_sha256" in document or "report_sha256" in document:
        raise ValueError("unbound result must not contain digest fields")
    bound = dict(document)
    bound["report_sha256"] = sha256_bytes(canonical_json(bound["report"]))
    bound["result_sha256"] = result_hash(bound)
    return bound


def _validate_optional_measurement(value, label):
    if value is not None:
        _finite_number(value, label)


def _validate_aggregate_result(value, label):
    _exact_object(value, (
        "raw", "rates", "team_strata", "diagnostic_cutoff_gap_seconds",
    ), label)
    raw = _exact_object(value["raw"], AGGREGATE_RAW_FIELDS, f"{label}.raw")
    for field, number in raw.items():
        if _finite_number(number, f"{label}.raw.{field}") < 0.0:
            raise ValueError(f"{label}.raw.{field} must be nonnegative")
    if (raw["bot_seconds"] != 12000.0 or
            raw["team_seconds"] != 2400.0):
        raise ValueError(f"{label} does not represent exactly two 10-bot rounds")
    rates = _exact_object(value["rates"], AGGREGATE_RATE_FIELDS,
                          f"{label}.rates")
    for field, number in rates.items():
        _validate_optional_measurement(number, f"{label}.rates.{field}")
    strata = _exact_object(value["team_strata"], CTF_TEAMS,
                           f"{label}.team_strata")
    for color in CTF_TEAMS:
        entry = _exact_object(strata[color], ("raw", "rates"),
                              f"{label}.team_strata.{color}")
        team_raw = _exact_object(
            entry["raw"], AGGREGATE_TEAM_RAW_FIELDS,
            f"{label}.team_strata.{color}.raw")
        for field, number in team_raw.items():
            if _finite_number(
                    number, f"{label}.team_strata.{color}.raw.{field}") < 0.0:
                raise ValueError("aggregate team raw value must be nonnegative")
        if team_raw["team_seconds"] != 1200.0:
            raise ValueError(f"{label}.{color} does not cover two exact rounds")
        team_rates = _exact_object(
            entry["rates"], AGGREGATE_TEAM_RATE_FIELDS,
            f"{label}.team_strata.{color}.rates")
        for field, number in team_rates.items():
            _validate_optional_measurement(
                number, f"{label}.team_strata.{color}.rates.{field}")
    gaps = _sequence(value["diagnostic_cutoff_gap_seconds"],
                     f"{label}.diagnostic_cutoff_gap_seconds")
    if len(gaps) != 2:
        raise ValueError(f"{label} must contain one cutoff gap per round")
    for index, gap in enumerate(gaps):
        number = _finite_number(gap, f"{label}.cutoff_gap[{index}]")
        if not 0.0 < number <= 15.0:
            raise ValueError(f"{label} cutoff gap exceeds the frozen bound")


def _validate_round_result(record, index):
    label = f"result.round_metrics[{index}]"
    _exact_object(record, RESULT_ROUND_FIELDS, label)
    identity = {
        "name": _safe_id(record["name"], f"{label}.name"),
        "arm": record["arm"],
        "round": _integer(record["round"], f"{label}.round", minimum=1),
        "port": _integer(record["port"], f"{label}.port", minimum=1),
    }
    if (identity["arm"] not in ("baseline", "candidate") or
            identity["round"] not in (1, 2) or identity["port"] > 65535):
        raise ValueError(f"{label} has invalid trial identity")
    root_identity = _sequence(record["root_identity"],
                              f"{label}.root_identity")
    if len(root_identity) != 2:
        raise ValueError(f"{label}.root_identity must contain device and inode")
    identity["root_identity"] = tuple(
        _integer(item, f"{label}.root_identity", minimum=0)
        for item in root_identity)
    identity["map"] = _safe_id(record["map"], f"{label}.map")
    source = _validate_source_identity(record["source_identity"],
                                       f"{label}.source_identity")
    assignment = record["roster_and_team_assignment"]
    if (not isinstance(assignment, Mapping) or len(assignment) != 10 or
            any(not isinstance(name, str) or not name for name in assignment) or
            Counter(assignment.values()) != Counter({"red": 5, "blue": 5})):
        raise ValueError(f"{label} roster assignment is not exact unique 5v5")
    artifacts = _exact_object(record["artifact_sha256"], REQUIRED_ARTIFACTS,
                              f"{label}.artifact_sha256")
    for name, digest in artifacts.items():
        _sha256(digest, f"{label}.artifact_sha256.{name}")
    if artifacts["module"] != source["module_sha256"]:
        raise ValueError(f"{label} module/source identity mismatch")
    _sha256(record["configuration_sha256"],
            f"{label}.configuration_sha256")
    _exact_object(record["rune_identity"], RUNE_IDENTITY_FIELDS,
                  f"{label}.rune_identity")
    if record["rune_identity"]["map"] != identity["map"]:
        raise ValueError(f"{label} map/RUNE identity mismatch")
    stands = _exact_object(record["stand_origins"], CTF_TEAMS,
                           f"{label}.stand_origins")
    for color, raw in stands.items():
        coordinates = _sequence(raw, f"{label}.stand_origins.{color}")
        if len(coordinates) != 3:
            raise ValueError(f"{label}.{color} stand must have three coordinates")
        for axis, coordinate in enumerate(coordinates):
            _finite_number(coordinate, f"{label}.{color}[{axis}]")
    window = _exact_object(record["evaluation_window_server_seconds"],
                           ("start", "end"), f"{label}.window")
    start = _finite_number(window["start"], f"{label}.window.start")
    end = _finite_number(window["end"], f"{label}.window.end")
    if end - start != WINDOW_SECONDS:
        raise ValueError(f"{label} window is not exactly 600 seconds")
    for field in ("window_counts", "telemetry", "database_full_stream",
                  "demo", "authority"):
        if not isinstance(record[field], Mapping):
            raise ValueError(f"{label}.{field} must be an object")
    return identity


def _validate_result_treatment_authority(value, arm):
    label = f"result.treatment_authority.{arm}"
    _exact_object(value, (
        "source_identity", "source_root_identity", "source_artifact_sha256",
        "build_input_sha256", "policy_probe",
    ), label)
    identity = _validate_source_identity(value["source_identity"],
                                         f"{label}.source_identity")
    root = _sequence(value["source_root_identity"],
                     f"{label}.source_root_identity")
    if len(root) != 2:
        raise ValueError(f"{label}.source_root_identity must contain dev/inode")
    root_identity = tuple(_integer(item, f"{label}.source_root_identity",
                                   minimum=0) for item in root)
    artifacts = _exact_object(value["source_artifact_sha256"],
                              SOURCE_ARTIFACTS,
                              f"{label}.source_artifact_sha256")
    for name, digest in artifacts.items():
        _sha256(digest, f"{label}.source_artifact_sha256.{name}")
    if artifacts["source_patch"] != identity["source_patch_sha256"] or \
            artifacts["source_manifest"] != identity["source_tree_sha256"]:
        raise ValueError(f"{label} source artifact/identity mismatch")
    build_inputs = _exact_object(
        value["build_input_sha256"], SOURCE_BUILD_INPUTS,
        f"{label}.build_input_sha256")
    for name, digest in build_inputs.items():
        _sha256(digest, f"{label}.build_input_sha256.{name}")
    probe = _exact_object(value["policy_probe"], (
        "tests_run", "failures", "errors", "skipped", "successful",
    ), f"{label}.policy_probe")
    for field in ("tests_run", "failures", "errors", "skipped"):
        _integer(probe[field], f"{label}.policy_probe.{field}", minimum=0)
    if (probe["tests_run"] <= 0 or probe["failures"] != 0 or
            probe["errors"] != 0 or probe["successful"] is not True):
        raise ValueError(f"{label} policy probe did not pass")
    return identity, root_identity


def validate_result_hashes(document):
    _exact_object(document, (
        "format", "metric_version", "metric_contract_sha256",
        "measurement_tool_sha256", "measurement_implementation_sha256",
        "measurement_implementation", "manifest_sha256",
        "source_parent_commit", "treatment_authority", "round_metrics",
        "aggregate", "report", "report_sha256", "result_sha256",
    ), "result")
    _sha256(document["report_sha256"], "result.report_sha256")
    _sha256(document["result_sha256"], "result.result_sha256")
    for field in ("metric_contract_sha256", "measurement_tool_sha256",
                  "measurement_implementation_sha256", "manifest_sha256"):
        _sha256(document[field], f"result.{field}")
    source_parent_commit = _commit(
        document["source_parent_commit"], "result.source_parent_commit")
    if document["metric_contract_sha256"] != METRIC_CONTRACT_SHA256:
        raise ValueError("result metric-contract hash is not current authority")
    current_tool_digest = sha256_bytes(_read_cli_file(
        Path(__file__), MAX_TEXT_BYTES, "measurement tool"))
    if document["measurement_tool_sha256"] != current_tool_digest:
        raise ValueError("result measurement-tool hash is not current authority")
    current_implementation, current_implementation_digest = \
        measurement_implementation()
    if (document["measurement_implementation_sha256"] !=
            current_implementation_digest or
            document["measurement_implementation"] != current_implementation):
        raise ValueError(
            "result measurement-implementation manifest is not current authority")
    if (document["format"] != RESULT_FORMAT or
            document["metric_version"] != METRIC_VERSION or
            not isinstance(document["round_metrics"], list) or
            not isinstance(document["aggregate"], Mapping) or
            not isinstance(document["report"], Mapping)):
        raise ValueError("result identity or typed payload is invalid")
    if document["report_sha256"] != sha256_bytes(
            canonical_json(document["report"])):
        raise ValueError("result report hash mismatch")
    if document["result_sha256"] != result_hash(document):
        raise ValueError("result hash mismatch")
    identities = [_validate_round_result(record, index)
                  for index, record in enumerate(document["round_metrics"])]
    authority = _exact_object(document["treatment_authority"],
                              ("baseline", "candidate"),
                              "result.treatment_authority")
    treatment_ids = {}
    source_roots = {}
    for arm in ("baseline", "candidate"):
        treatment_ids[arm], source_roots[arm] = \
            _validate_result_treatment_authority(authority[arm], arm)
    if source_roots["baseline"] == source_roots["candidate"]:
        raise ValueError("result treatment source roots alias")
    if authority["baseline"]["build_input_sha256"] != \
            authority["candidate"]["build_input_sha256"]:
        raise ValueError("result candidate changed parent build authority")
    if (treatment_ids["baseline"]["source_commit"] != source_parent_commit or
            treatment_ids["candidate"]["source_commit"] !=
            source_parent_commit or
            treatment_ids["baseline"]["source_patch_sha256"] !=
            EMPTY_PATCH_SHA256 or
            treatment_ids["candidate"]["source_patch_sha256"] ==
            EMPTY_PATCH_SHA256 or
            treatment_ids["baseline"] == treatment_ids["candidate"] or
            treatment_ids["baseline"]["module_sha256"] ==
            treatment_ids["candidate"]["module_sha256"]):
        raise ValueError("result treatment identities are not frozen baseline/candidate")
    if (len(identities) != 4 or
            {(item["round"], item["arm"]) for item in identities} != {
                (1, "baseline"), (1, "candidate"),
                (2, "baseline"), (2, "candidate"),
            } or len({item["name"] for item in identities}) != 4 or
            len({item["port"] for item in identities}) != 4 or
            len({item["root_identity"] for item in identities}) != 4):
        raise ValueError("result round metrics do not preserve the exact 2x2 trial")
    if set(source_roots.values()) & {item["root_identity"] for item in identities}:
        raise ValueError("result treatment and round roots alias")
    records_by_pair = {
        (record["round"], record["arm"]): record
        for record in document["round_metrics"]
    }
    for record in document["round_metrics"]:
        if record["source_identity"] != treatment_ids[record["arm"]]:
            raise ValueError("result round/treatment source identity mismatch")
    for round_number in (1, 2):
        if (records_by_pair[(round_number, "baseline")][
                "roster_and_team_assignment"] !=
                records_by_pair[(round_number, "candidate")][
                "roster_and_team_assignment"]):
            raise ValueError("result paired arm assignments differ")
    first_assignment = records_by_pair[(1, "baseline")][
        "roster_and_team_assignment"]
    second_assignment = records_by_pair[(2, "baseline")][
        "roster_and_team_assignment"]
    if set(first_assignment) != set(second_assignment) or any(
            first_assignment[name] == second_assignment[name]
            for name in first_assignment):
        raise ValueError("result round two is not the exact team swap")
    first_record = document["round_metrics"][0]
    for record in document["round_metrics"][1:]:
        for field in ("map", "configuration_sha256", "rune_identity",
                      "stand_origins"):
            if record[field] != first_record[field]:
                raise ValueError(f"result rounds differ in frozen {field}")
        for artifact in ("engine", "rune", "bsp", "recording_harness"):
            if record["artifact_sha256"][artifact] != \
                    first_record["artifact_sha256"][artifact]:
                raise ValueError(
                    f"result rounds differ in frozen {artifact} artifact")
    aggregate = _exact_object(document["aggregate"],
                              ("baseline", "candidate"), "result.aggregate")
    _validate_aggregate_result(aggregate["baseline"],
                               "result.aggregate.baseline")
    _validate_aggregate_result(aggregate["candidate"],
                               "result.aggregate.candidate")
    try:
        recomputed = {
            arm: aggregate_rounds(document["round_metrics"], arm)
            for arm in ("baseline", "candidate")
        }
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError("result round metrics cannot be aggregated") from error
    if recomputed != dict(aggregate):
        raise ValueError("result aggregate does not match round metrics")
    report = _exact_object(document["report"], (
        "valid_receipts", "round_count", "sufficient_events",
        "all_bands_pass", "decision", "checks",
    ), "result.report")
    if (report["valid_receipts"] is not True or
            _integer(report["round_count"], "result.report.round_count") != 4 or
            not isinstance(report["sufficient_events"], bool) or
            not isinstance(report["all_bands_pass"], bool) or
            report["decision"] not in ("adopt", "strike", "inconclusive")):
        raise ValueError("result report identity or types are invalid")
    checks = _exact_object(report["checks"], RESULT_CHECK_FIELDS,
                           "result.report.checks")
    for name, check in checks.items():
        _exact_object(check, ("pass", "actual", "requirement"),
                      f"result.report.checks.{name}")
        if (not isinstance(check["pass"], bool) or
                not isinstance(check["requirement"], str) or
                not check["requirement"]):
            raise ValueError(f"result check {name} has invalid types")
        _validate_optional_measurement(
            check["actual"], f"result.report.checks.{name}.actual")
    expected_pass = (report["sufficient_events"] and
                     all(check["pass"] for check in checks.values()))
    expected_decision = ("adopt" if expected_pass else
                         "strike" if report["sufficient_events"] else
                         "inconclusive")
    if (report["all_bands_pass"] != expected_pass or
            report["decision"] != expected_decision):
        raise ValueError("result report decision is internally inconsistent")
    contract_path = Path(__file__).with_name("steal-stage-a-contract.json")
    contract = load_contract(_read_cli_file(
        contract_path, MAX_TEXT_BYTES, "metric contract"))
    evaluated = evaluate_bands(
        recomputed["baseline"], recomputed["candidate"], contract)
    expected_report = {
        "valid_receipts": True, "round_count": 4,
        "sufficient_events": evaluated["sufficient_events"],
        "all_bands_pass": evaluated["all_bands_pass"],
        "decision": evaluated["decision"], "checks": evaluated["checks"],
    }
    if dict(report) != expected_report:
        raise ValueError("result report does not match executable band evaluation")
    return document


def _open_directory_nofollow(path):
    """Open every component of an absolute directory without symlink travel."""
    candidate = Path(path)
    if not candidate.is_absolute() or str(candidate) != os.path.normpath(str(candidate)):
        raise ValueError("retained root must be a normalized absolute path")
    flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0)
    nofollow = getattr(os, "O_NOFOLLOW", 0)
    fd = os.open("/", flags)
    try:
        for component in candidate.parts[1:]:
            next_fd = os.open(component, flags | nofollow, dir_fd=fd)
            os.close(fd)
            fd = next_fd
        current = os.fstat(fd)
        if not stat.S_ISDIR(current.st_mode):
            raise ValueError("retained root is not a directory")
        return fd
    except Exception:
        os.close(fd)
        raise


def _relative_artifact_path(value, label):
    if not isinstance(value, str) or not value:
        raise ValueError(f"{label} must be a nonempty relative POSIX path")
    path = PurePosixPath(value)
    if (path.is_absolute() or value != path.as_posix() or
            any(part in ("", ".", "..") for part in path.parts)):
        raise ValueError(f"{label} must be a normalized relative POSIX path")
    return path


class RetainedRoot:
    """Stable no-follow reader rooted at one pre-existing trial directory."""

    def __init__(self, path):
        self.path = Path(path)
        self.fd = _open_directory_nofollow(self.path)
        opened = os.fstat(self.fd)
        self.identity = (opened.st_dev, opened.st_ino)
        self._file_identities = {}

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def __enter__(self):
        return self

    def __exit__(self, _kind, _error, _traceback):
        self.close()

    def assert_current(self):
        check = _open_directory_nofollow(self.path)
        try:
            named = os.fstat(check)
            if (named.st_dev, named.st_ino) != self.identity:
                raise ValueError("retained root changed during validation")
        finally:
            os.close(check)

    def assert_files_current(self):
        """Re-stat every previously read name through the retained root fd."""
        for logical, expected in self._file_identities.items():
            path = _relative_artifact_path(logical, "retained artifact path")
            directory = os.dup(self.fd)
            try:
                for component in path.parts[:-1]:
                    next_fd = os.open(
                        component,
                        os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) |
                        getattr(os, "O_NOFOLLOW", 0), dir_fd=directory)
                    os.close(directory)
                    directory = next_fd
                named = os.stat(path.parts[-1], dir_fd=directory,
                                follow_symlinks=False)
                identity = (named.st_dev, named.st_ino, named.st_size,
                            named.st_mtime_ns, named.st_ctime_ns)
                if identity != expected or named.st_nlink != 1:
                    raise ValueError(
                        f"retained artifact changed after validation: {logical}")
            finally:
                os.close(directory)

    def read(self, relative, maximum, label, *, allow_empty=False):
        path = _relative_artifact_path(relative, f"{label}.path")
        directory = os.dup(self.fd)
        try:
            for component in path.parts[:-1]:
                next_fd = os.open(
                    component,
                    os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) |
                    getattr(os, "O_NOFOLLOW", 0), dir_fd=directory)
                os.close(directory)
                directory = next_fd
            fd = os.open(path.parts[-1], os.O_RDONLY |
                         getattr(os, "O_NOFOLLOW", 0), dir_fd=directory)
            try:
                before = os.fstat(fd)
                if (not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or
                        (before.st_size == 0 and not allow_empty) or
                        before.st_size < 0 or before.st_size > maximum):
                    raise ValueError(
                        f"{label} must be a bounded unaliased regular file")
                output = bytearray()
                while len(output) < before.st_size:
                    block = os.read(fd, min(1024 * 1024,
                                            before.st_size - len(output)))
                    if not block:
                        raise ValueError(f"{label} truncated while reading")
                    output.extend(block)
                after = os.fstat(fd)
                named = os.stat(path.parts[-1], dir_fd=directory,
                                follow_symlinks=False)
                identity = lambda item: (
                    item.st_dev, item.st_ino, item.st_size,
                    item.st_mtime_ns, item.st_ctime_ns,
                )
                if (identity(before) != identity(after) or
                        identity(after) != identity(named) or
                        after.st_nlink != 1 or named.st_nlink != 1):
                    raise ValueError(f"{label} changed while reading")
                logical = path.as_posix()
                previous = self._file_identities.get(logical)
                if previous is not None and previous != identity(after):
                    raise ValueError(f"{label} was replaced between validations")
                self._file_identities[logical] = identity(after)
                return bytes(output)
            finally:
                os.close(fd)
        finally:
            os.close(directory)


def _artifact_record(value, label):
    _exact_object(value, ("path", "sha256"), label)
    path = _relative_artifact_path(value["path"], f"{label}.path").as_posix()
    digest = _sha256(value["sha256"], f"{label}.sha256")
    return path, digest


def _artifact_limit(name):
    if name in ("server_log", "stdlog", "recording_harness",
                "knowledge_report"):
        return MAX_TEXT_BYTES
    if name == "stats_database":
        return MAX_DATABASE_BYTES
    if name == "serverrecord":
        return MAX_DEMO_BYTES
    return MAX_BINARY_BYTES


def load_contract(payload):
    contract = _strict_json(payload, "metric contract")
    _exact_object(contract, (
        "metric_version", "frozen_before_candidate_trial", "trial_design",
        "match_identity", "source_authority", "receipt_schema",
        "result_schema", "measurement_limits", "event_authority",
        "metric_calculation", "stratification", "bands", "decision",
    ), "metric contract")
    if contract["metric_version"] != METRIC_VERSION:
        raise ValueError("metric contract has wrong literal metric version")
    design = _exact_object(contract["trial_design"], (
        "arms", "rounds", "treatments", "roster_size", "team_size",
    ), "metric contract.trial_design")
    if (design["arms"] != ["baseline", "candidate"] or
            design["treatments"] != ["baseline", "candidate"] or
            design["rounds"] != [1, 2] or design["roster_size"] != 10 or
            design["team_size"] != 5):
        raise ValueError("metric contract trial design is not exact 2x2 5v5")
    identity = _exact_object(contract["match_identity"], (
        "minimum_duration_seconds_per_round",
        "minimum_arm_swapped_rounds",
        "minimum_authoritative_pickups_per_arm",
        "disposable_game_roots_required", "disjoint_ports_required",
        "recording_policy_literal", "source_patch_rule",
    ), "metric contract.match_identity")
    if (identity["minimum_duration_seconds_per_round"] != 600 or
            identity["minimum_arm_swapped_rounds"] != 2 or
            identity["minimum_authoritative_pickups_per_arm"] != 5 or
            identity["disposable_game_roots_required"] is not True or
            identity["disjoint_ports_required"] is not True or
            identity["recording_policy_literal"] !=
            "serverrecord before roster joins through removal after the full-roster allowance; evaluate the exact server-time crop" or
            identity["source_patch_rule"] !=
            "SHA-256 of exact git diff --binary --no-ext-diff --no-textconv "
            "--src-prefix=a/ --dst-prefix=b/ HEAD -- bytes; reject every "
            "untracked nonignored path"):
        raise ValueError("metric contract match identity changed")
    source_authority = _exact_object(contract["source_authority"], (
        "matched_parent_rule", "baseline_source_patch_sha256",
        "policy_probe_path", "policy_probe_sha256", "build_recipe",
        "build_year", "build_authority_paths",
        "measurement_implementation_paths", "source_tree_rule",
    ), "metric contract.source_authority")
    if dict(source_authority) != {
            "matched_parent_rule":
                "baseline and candidate use the same declared clean parent "
                "commit; baseline patch is empty and candidate patch is nonempty",
            "baseline_source_patch_sha256": EMPTY_PATCH_SHA256,
            "policy_probe_path": POLICY_PROBE_PATH,
            "policy_probe_sha256": POLICY_PROBE_SHA256,
            "build_recipe": SOURCE_BUILD_RECIPE,
            "build_year": SOURCE_BUILD_YEAR,
            "build_authority_paths": list(SOURCE_BUILD_INPUTS),
            "measurement_implementation_paths":
                list(MEASUREMENT_IMPLEMENTATION_PATHS),
            "source_tree_rule":
                "canonical JSON of every tracked commit-plus-patch path with "
                "normalized UTF-8 path, Git mode, byte size, and SHA-256; "
                "reconstruct and rehash in a disposable checkout",
            }:
        raise ValueError("metric contract source authority changed")
    schema = _exact_object(contract["receipt_schema"], (
        "format", "top_level_fields", "treatment_fields", "round_fields",
        "source_identity_fields", "artifact_fields", "source_artifacts",
        "required_artifacts", "rune_identity_fields",
        "build_receipt_fields", "knowledge_report_fields",
        "configuration_digest_rule",
    ), "metric contract.receipt_schema")
    expected_schema = {
        "format": RECEIPT_FORMAT,
        "top_level_fields": [
            "format", "metric_version", "metric_contract_sha256",
            "measurement_tool_sha256", "measurement_implementation_sha256",
            "source_parent_commit", "treatments", "rounds",
        ],
        "treatment_fields": [
            "source_commit", "source_patch_sha256", "source_tree_sha256",
            "module_sha256", "source_root", "source_artifacts",
        ],
        "round_fields": [
            "name", "metric_version", "arm", "round", "treatment",
            "source_identity", "root", "port", "map",
            "roster_and_team_assignment", "evaluation_window_server_seconds",
            "active_duration_seconds", "recording_policy", "artifacts",
            "configuration_artifacts", "configuration_sha256", "rune_identity",
        ],
        "source_identity_fields": [
            "source_commit", "source_patch_sha256", "source_tree_sha256",
            "module_sha256",
        ],
        "artifact_fields": ["path", "sha256"],
        "source_artifacts": list(SOURCE_ARTIFACTS),
        "required_artifacts": list(REQUIRED_ARTIFACTS),
        "rune_identity_fields": list(RUNE_IDENTITY_FIELDS),
        "build_receipt_fields": list(BUILD_RECEIPT_FIELDS),
        "knowledge_report_fields": [
            *KNOWLEDGE_REPORT_FIELDS,
        ],
        "configuration_digest_rule":
            "sort normalized relative paths by UTF-8 bytes; SHA-256 each "
            "sequence of unsigned 64-bit big-endian path length, path bytes, "
            "unsigned 64-bit big-endian payload length, and exact payload bytes",
    }
    if dict(schema) != expected_schema:
        raise ValueError("metric contract receipt schema is not the executable schema")
    result_schema = _exact_object(contract["result_schema"], (
        "format", "top_level_fields", "report_fields", "hash_rule",
    ), "metric contract.result_schema")
    if dict(result_schema) != {
            "format": RESULT_FORMAT,
            "top_level_fields": [
                "format", "metric_version", "metric_contract_sha256",
                "measurement_tool_sha256",
                "measurement_implementation_sha256",
                "measurement_implementation", "manifest_sha256",
                "source_parent_commit",
                "treatment_authority", "round_metrics", "aggregate",
                "report", "report_sha256", "result_sha256",
            ],
            "report_fields": [
                "valid_receipts", "round_count", "sufficient_events",
                "all_bands_pass", "decision", "checks",
            ],
            "hash_rule":
                "report_sha256 is SHA-256 of canonical JSON report; "
                "result_sha256 is SHA-256 of canonical JSON result with only "
                "result_sha256 omitted; canonical JSON is UTF-8, sorted keys, "
                "compact separators, no non-finite values, and one trailing newline",
            }:
        raise ValueError("metric contract result schema is not executable authority")
    limits = _exact_object(contract["measurement_limits"], (
        "exact_window_seconds", "demo_fps", "alignment_residual_seconds_max",
        "carry_reconciliation_seconds_max", "diagnostic_cutoff_gap_seconds_max",
        "minimum_diagnostic_rows_per_bot", "census_cadence_frames",
        "census_rows_per_bot",
    ), "metric contract.measurement_limits")
    exact_limits = (limits["exact_window_seconds"] == WINDOW_SECONDS and
                    limits["demo_fps"] == DEMO_FPS and
                    limits["alignment_residual_seconds_max"] == 0.11 and
                    limits["carry_reconciliation_seconds_max"] == 0.2 and
                    limits["diagnostic_cutoff_gap_seconds_max"] == 15.0 and
                    limits["minimum_diagnostic_rows_per_bot"] == 1 and
                    limits["census_cadence_frames"] == 10 and
                    limits["census_rows_per_bot"] == 600)
    if not exact_limits:
        raise ValueError("metric contract measurement limits changed")
    for family, values in contract["bands"].items():
        if not isinstance(family, str) or not isinstance(values, Mapping):
            raise ValueError("metric contract bands are malformed")
        for field, value in values.items():
            _finite_number(value, f"bands.{family}.{field}")
    expected_bands = {
        "movement": {
            "horizontal_distance_per_active_bot_minute_ratio_min",
            "moving_sample_fraction_absolute_delta_min",
            "world_or_hazard_suicides_per_active_bot_minute_delta_max"},
        "combat": {
            "combat_kills_per_active_team_minute_ratio_min",
            "combat_kills_per_active_team_minute_ratio_max",
            "combat_deaths_per_active_team_minute_ratio_max"},
        "perception": {
            "forbidden_knowledge_events_max",
            "visible_or_audible_engagements_per_active_team_minute_ratio_min",
            "visible_or_audible_engagements_per_active_team_minute_ratio_max"},
        "steal": {
            "authoritative_pickups_per_active_team_minute_ratio_min",
            "authoritative_pickups_per_active_team_minute_delta_min",
            "authoritative_pickup_count_delta_min",
            "approaches_per_observed_stand_minute_ratio_min"},
        "conversion": {
            "close_approach_conversion_absolute_delta_min",
            "close_approach_conversion_ratio_min",
            "timely_authoritative_pickup_count_delta_min"},
        "capture": {
            "authoritative_captures_per_active_team_minute_ratio_min",
            "steal_to_capture_conversion_absolute_delta_min",
            "authoritative_reconciliation_mismatches_max"},
        "defense": {
            "defender_post_dwell_fraction_ratio_min",
            "defender_moving_sample_fraction_ratio_min",
            "defender_departures_per_active_defender_minute_ratio_max",
            "captures_conceded_per_active_team_minute_ratio_max",
            "captures_conceded_per_active_team_minute_delta_max"},
    }
    if (set(contract["bands"]) != set(expected_bands) or any(
            set(contract["bands"][family]) != fields
            for family, fields in expected_bands.items())):
        raise ValueError("metric contract has an unevaluated or missing band")
    calculations = {
        "evaluation_window", "active_bot_minutes", "active_team_minutes",
        "active_defender_minutes", "horizontal_distance",
        "telemetry_time_alignment", "moving_sample_fraction",
        "world_or_hazard_suicides", "combat",
        "visible_or_audible_engagements", "forbidden_knowledge_events",
        "approach", "demo_time_alignment", "close_conversion", "steal",
        "capture", "steal_to_capture_conversion",
        "defender_post_dwell_fraction", "defender_moving_sample_fraction",
        "defender_departure", "captures_conceded", "strata_and_pooling",
    }
    if (not isinstance(contract["metric_calculation"], Mapping) or
            set(contract["metric_calculation"]) != calculations or
            any(not isinstance(value, str) or not value.strip()
                for value in contract["metric_calculation"].values())):
        raise ValueError("metric contract calculation authority is incomplete")
    if (not isinstance(contract["event_authority"], Mapping) or
            set(contract["event_authority"]) != {
                "steal", "capture", "approach", "close_conversion",
                "demo_capture", "forbidden_knowledge"} or
            any(not isinstance(value, str) or not value.strip()
                for value in contract["event_authority"].values())):
        raise ValueError("metric contract event authority is incomplete")
    if contract["stratification"] != [
            "map", "team", "roster_size", "configuration"]:
        raise ValueError("metric contract stratification changed")
    decision = _exact_object(contract["decision"], (
        "require_every_band", "zero_baseline_ratio_rule",
        "insufficient_event_rule", "failure_rule"),
        "metric contract.decision")
    if decision["require_every_band"] is not True:
        raise ValueError("metric contract must require every band")
    if sha256_bytes(payload) != METRIC_CONTRACT_SHA256:
        raise ValueError("metric contract is not the checked-in frozen authority")
    return contract


def _rune_identity(rune):
    header = rune.header
    return {
        "map": header.map_name,
        "bsp_checksum": header.bsp_checksum,
        "entity_crc": header.entity_crc32,
        "physics_flags": header.physics_flags,
        "gravity": header.gravity,
        "airaccelerate": header.airaccelerate,
        "maxvelocity": header.maxvelocity,
        "pmove_ms": header.pmove_substep_ms,
        "frame_ms": header.server_frame_ms,
        "host_physics_id": header.host_physics_id,
        "rune_payload_crc": header.payload_crc32,
        "rune_header_crc": header.header_crc32,
        "action_contract_crc": header.action_contract_crc32,
        "mechanism_contract_crc": header.mechanism_contract_crc32,
        "num_seeds": header.num_seeds,
        "num_links": header.num_links,
        "num_activation_nodes": header.num_activation_nodes,
        "num_activation_edges": header.num_activation_edges,
        "num_activation_plans": header.num_activation_plans,
        "num_inventory_edges": header.num_inventory_edges,
        "string_bytes": header.string_bytes,
    }


CONNECT_RE = re.compile(r"^\t\tPlayerConnect\t([^\t]+)\t\t([0-9]+(?:\.[0-9]+)?)$")
TEAM_RE = re.compile(
    r"^\t\tPlayerTeamChange\t([^\t]+)\t(red|blue)\t([0-9]+(?:\.[0-9]+)?)$")
LEFT_RE = re.compile(r"^\t\tPlayerLeft\t([^\t]+)\t\t([0-9]+(?:\.[0-9]+)?)$")
RENAME_RE = re.compile(r"^\t\tPlayerRename\t([^\t]+)\t([^\t]+)\t([0-9]+(?:\.[0-9]+)?)$")


def parse_stdlog(payload):
    """Parse the complete production event stream without collapsing events."""
    try:
        lines = bytes(payload).decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise ValueError("StdLog is not UTF-8") from error
    control = {"connect": [], "team": [], "left": [], "rename": []}
    flag_events = []
    combat_events = []
    recognized_times = []
    for log_order, raw in enumerate(lines):
        match = CONNECT_RE.fullmatch(raw)
        if match:
            event = {"name": match.group(1), "time": float(match.group(2)),
                     "log_order": log_order}
            control["connect"].append(event)
            recognized_times.append(event["time"])
            continue
        match = TEAM_RE.fullmatch(raw)
        if match:
            event = {"name": match.group(1), "team": match.group(2),
                     "time": float(match.group(3)), "log_order": log_order}
            control["team"].append(event)
            recognized_times.append(event["time"])
            continue
        match = LEFT_RE.fullmatch(raw)
        if match:
            event = {"name": match.group(1), "time": float(match.group(2)),
                     "log_order": log_order}
            control["left"].append(event)
            recognized_times.append(event["time"])
            continue
        match = RENAME_RE.fullmatch(raw)
        if match:
            event = {"old_name": match.group(1), "new_name": match.group(2),
                     "time": float(match.group(3)), "log_order": log_order}
            control["rename"].append(event)
            recognized_times.append(event["time"])
            continue
        if any(marker in raw for marker in (
                "\tPlayerConnect\t", "\tPlayerTeamChange\t",
                "\tPlayerLeft\t", "\tPlayerRename\t")):
            raise ValueError(f"malformed StdLog roster event at line {log_order + 1}")
        columns = raw.split("\t")
        score_kind = next((kind for kind in FLAG_EVENT_KINDS
                           if kind in columns), None)
        if score_kind is not None:
            if (len(columns) != 6 or columns[2] != score_kind or
                    not columns[0] or columns[1] or columns[3]):
                raise ValueError(
                    f"malformed StdLog flag event at line {log_order + 1}")
            try:
                moment = float(columns[-1])
                int(columns[-2])
            except ValueError as error:
                raise ValueError(f"malformed StdLog flag time at line {log_order + 1}") from error
            event = {"name": columns[0], "kind": score_kind,
                     "time": moment, "log_order": log_order}
            flag_events.append(event)
            recognized_times.append(moment)
            continue
        if "Kill" in columns:
            if (len(columns) != 6 or columns[2] != "Kill" or
                    not columns[0] or not columns[1]):
                raise ValueError(f"malformed StdLog kill at line {log_order + 1}")
            try:
                moment = float(columns[-1])
                int(columns[-2])
            except ValueError as error:
                raise ValueError(f"malformed StdLog kill time at line {log_order + 1}") from error
            event = {"kind": "Kill", "attacker": columns[0],
                     "victim": columns[1], "weapon": columns[3],
                     "time": moment, "log_order": log_order}
            combat_events.append(event)
            recognized_times.append(moment)
        elif "Suicide" in columns:
            if (len(columns) != 6 or columns[2] != "Suicide" or
                    not columns[0] or columns[1]):
                raise ValueError(
                    f"malformed StdLog suicide at line {log_order + 1}")
            try:
                moment = float(columns[-1])
                int(columns[-2])
            except ValueError as error:
                raise ValueError(f"malformed StdLog suicide time at line {log_order + 1}") from error
            event = {"kind": "Suicide", "name": columns[0],
                     "weapon": columns[3], "time": moment,
                     "log_order": log_order}
            combat_events.append(event)
            recognized_times.append(moment)
    for index, moment in enumerate(recognized_times):
        _finite_number(moment, f"StdLog recognized event {index} time")
    if any(right < left for left, right in
           zip(recognized_times, recognized_times[1:])):
        raise ValueError("StdLog recognized event times are nonmonotonic")
    return {"lines": lines, "control": control,
            "flag_events": flag_events, "combat_events": combat_events}


def validate_continuous_roster(stdlog, assignment, expected_size=10,
                               team_size=5, duration=WINDOW_SECONDS):
    if not isinstance(assignment, Mapping):
        raise ValueError("roster assignment must be an object")
    if (len(assignment) != expected_size or
            any(not isinstance(name, str) or not name for name in assignment) or
            any(team not in CTF_TEAMS for team in assignment.values()) or
            Counter(assignment.values()) != Counter({"red": team_size,
                                                      "blue": team_size})):
        raise ValueError("roster assignment must be exactly ten unique 5v5 bots")
    roster = tuple(assignment)
    control = stdlog["control"]
    if control["rename"]:
        raise ValueError("roster contains a PlayerRename event")
    for kind in ("connect", "team", "left"):
        events = control[kind]
        names = [event["name"] for event in events]
        if len(events) != expected_size or set(names) != set(roster) or \
                len(names) != len(set(names)):
            raise ValueError(f"roster requires exactly one {kind} event per bot")
    connects = {event["name"]: event["time"]
                for event in control["connect"]}
    joins = {event["name"]: event["time"] for event in control["team"]}
    teams = {event["name"]: event["team"] for event in control["team"]}
    leaves = {event["name"]: event["time"] for event in control["left"]}
    if teams != dict(assignment):
        raise ValueError("StdLog team assignment differs from receipt")
    for name in roster:
        if connects[name] > joins[name]:
            raise ValueError(f"{name} joined a team before connecting")
    window = exact_window(joins, leaves, roster, duration=duration)
    if any(connects[name] > window[0] for name in roster):
        raise ValueError("a roster client was not connected at window start")
    actors = set(roster)
    for event in stdlog["flag_events"]:
        if event["name"] not in actors:
            raise ValueError("StdLog flag event names a non-roster client")
    for event in stdlog["combat_events"]:
        names = ((event["attacker"], event["victim"])
                 if event["kind"] == "Kill" else (event["name"],))
        if any(name not in actors for name in names):
            raise ValueError("StdLog combat event names a non-roster client")
    return {"roster": roster, "connected": connects, "joined": joins,
            "left": leaves, "teams": teams, "window": window}


HOST_PICKUP_RE = re.compile(r"^(.+) stole the (red|blue) flag\.$")
HOST_CAPTURE_RE = re.compile(r"^(.+) captured the (red|blue) flag\.$")


def reconcile_host_outcomes(payload, stdlog, teams):
    teams = _validated_teams(teams)
    roster = set(teams)
    try:
        lines = bytes(payload).decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise ValueError("server log is not UTF-8") from error
    host = []
    for line_number, line in enumerate(lines):
        match = HOST_PICKUP_RE.fullmatch(line)
        if match:
            host.append({"name": match.group(1), "kind": "F Pickup",
                         "flag": match.group(2), "line": line_number})
            continue
        match = HOST_CAPTURE_RE.fullmatch(line)
        if match:
            host.append({"name": match.group(1), "kind": "F Capture",
                         "flag": match.group(2), "line": line_number})
            continue
        if " stole the " in line or " captured the " in line:
            raise ValueError(
                f"malformed host flag outcome at line {line_number + 1}")
    expected = [event for event in stdlog["flag_events"]
                if event["kind"] in ("F Pickup", "F Capture")]
    if [(item["name"], item["kind"], item["flag"]) for item in host] != [
            (item["name"], item["kind"],
             flag_color_for_thief(teams[item["name"]]))
            for item in expected]:
        raise ValueError("ordered host flag outcomes disagree with StdLog")
    for item in host:
        if item["name"] not in roster:
            raise ValueError("host outcome names a non-roster client")
    return {"lines": lines, "outcomes": host}


def parse_stats_database(payload, assignment):
    raw = bytes(payload)
    if not raw.startswith(b"SQLite format 3\0"):
        raise ValueError("stats database lacks SQLite format header")
    # `deserialize` cannot inspect a checkpointed WAL-mode header without a
    # filename.  A private exact-byte immutable copy supports both rollback-
    # and WAL-header databases while forbidding journal/WAL sidecar reads.
    with tempfile.NamedTemporaryFile(prefix="stealstage-stats-",
                                     suffix=".db") as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
        uri = Path(stream.name).as_uri() + "?immutable=1"
        connection = sqlite3.connect(uri, uri=True)
        try:
            integrity = connection.execute("PRAGMA integrity_check").fetchone()
            if integrity != ("ok",):
                raise ValueError(f"stats database integrity failed: {integrity}")
            rows = connection.execute("""
                SELECT u.playername, g.frags, g.fragged, g.deaths, g.suicides,
                       g.shots, g.shots_hit, c.flag_pickups, c.flag_captures,
                       c.flag_returns
                  FROM userdata AS u
                  JOIN game_stats AS g USING (char_idx)
                  JOIN ctf_stats AS c USING (char_idx)
            """).fetchall()
        except sqlite3.Error as error:
            raise ValueError(f"stats database query failed: {error}") from error
        finally:
            connection.close()
    if len(rows) != len(assignment) or {row[0] for row in rows} != set(assignment):
        raise ValueError("stats database roster is not exact")
    fields = ("frags", "fragged", "deaths", "suicides", "shots",
              "shots_hit", "pickups", "captures", "returns")
    totals = {field: 0 for field in fields}
    per_player = {}
    by_team = {color: {field: 0 for field in fields}
               for color in CTF_TEAMS}
    for row in rows:
        values = {}
        for field, value in zip(fields, row[1:]):
            if isinstance(value, bool) or not isinstance(value, int) or value < 0:
                raise ValueError(f"stats database has invalid {field} for {row[0]}")
            values[field] = value
            totals[field] += value
            by_team[assignment[row[0]]][field] += value
        per_player[row[0]] = values
    return {"total": totals, "by_team": by_team,
            "per_player": per_player}


def reconcile_stats(stdlog, stats, roster):
    expected = {name: {"pickups": 0, "captures": 0, "frags": 0,
                       "fragged": 0, "suicides": 0, "returns": 0}
                for name in roster}
    for event in stdlog["flag_events"]:
        if event["kind"] == "F Pickup":
            expected[event["name"]]["pickups"] += 1
        elif event["kind"] == "F Capture":
            expected[event["name"]]["captures"] += 1
        elif event["kind"] == "F Return":
            expected[event["name"]]["returns"] += 1
    for event in stdlog["combat_events"]:
        if event["kind"] == "Kill":
            expected[event["attacker"]]["frags"] += 1
            expected[event["victim"]]["fragged"] += 1
        else:
            expected[event["name"]]["suicides"] += 1
    mismatches = []
    for name in roster:
        for field, value in expected[name].items():
            actual = stats["per_player"][name][field]
            if actual != value:
                mismatches.append({"name": name, "field": field,
                                   "stdlog": value, "stats": actual})
    if mismatches:
        raise ValueError(f"stats authority disagrees with StdLog: {mismatches}")
    return expected


def validate_knowledge_report(payload, source_identity, *,
                              measurement_tool_sha256,
                              measurement_implementation_sha256,
                              policy_probe):
    report = _strict_json(payload, "knowledge report")
    _exact_object(report, KNOWLEDGE_REPORT_FIELDS, "knowledge report")
    expected_probe = _exact_object(policy_probe, (
        "tests_run", "failures", "errors", "skipped", "successful",
    ), "computed policy probe")
    if (report["format"] != KNOWLEDGE_REPORT_FORMAT or
            report["metric_version"] != METRIC_VERSION or
            report["source_commit"] != source_identity["source_commit"] or
            report["source_patch_sha256"] !=
            source_identity["source_patch_sha256"] or
            report["source_tree_sha256"] !=
            source_identity["source_tree_sha256"] or
            report["module_sha256"] != source_identity["module_sha256"] or
            report["measurement_tool_sha256"] != measurement_tool_sha256 or
            report["measurement_implementation_sha256"] !=
            measurement_implementation_sha256 or
            report["policy_probe_sha256"] != POLICY_PROBE_SHA256):
        raise ValueError("knowledge report identity mismatch")
    for field in ("tests_run", "failures", "errors", "skipped"):
        _integer(report[field], f"knowledge report.{field}", minimum=0)
    if not isinstance(report["successful"], bool):
        raise ValueError("knowledge report.successful must be boolean")
    if {field: report[field] for field in expected_probe} != dict(expected_probe):
        raise ValueError("knowledge report differs from reconstructed policy probe")
    if (report["tests_run"] <= 0 or report["failures"] != 0 or
            report["errors"] != 0 or report["successful"] is not True):
        raise ValueError("reconstructed forbidden-knowledge policy probe failed")
    return report


def _run_knowledge_probe(repository):
    """Run the frozen source-policy suite and return measured runner output."""
    probe = Path(repository) / POLICY_PROBE_PATH
    payload = _read_cli_file(probe, MAX_TEXT_BYTES,
                             "forbidden-knowledge policy probe")
    if sha256_bytes(payload) != POLICY_PROBE_SHA256:
        raise ValueError("reconstructed policy probe differs from frozen authority")
    for relative in POLICY_PROBE_IMPORT_SHADOWS:
        tracked = subprocess.run(
            ("git", "-C", os.fspath(repository), "ls-files",
             "--error-unmatch", "--", relative), stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, check=False)
        if tracked.returncode == 0:
            raise ValueError(
                f"reconstructed policy source shadows a probe import: {relative}")
    runner = (
        "import importlib.util,json,sys,unittest;"
        "p=sys.argv[1];"
        "q=importlib.util.spec_from_file_location('_stage_a_policy_probe',p);"
        "m=importlib.util.module_from_spec(q);q.loader.exec_module(m);"
        "s=unittest.defaultTestLoader.loadTestsFromModule(m);"
        "r=unittest.TextTestRunner(verbosity=0).run(s);"
        "print(json.dumps({'tests_run':r.testsRun,'failures':len(r.failures),"
        "'errors':len(r.errors),'skipped':len(r.skipped),"
        "'successful':r.wasSuccessful()},sort_keys=True,separators=(',',':')))"
    )
    environment = dict(os.environ)
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    process = subprocess.run(
        (sys.executable, "-I", "-S", "-c", runner, os.fspath(probe)),
        cwd=Path(repository).parent,
        env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        check=False)
    if process.returncode != 0:
        detail = process.stderr.decode("utf-8", errors="replace")[-4000:]
        raise ValueError(f"forbidden-knowledge policy probe failed: {detail}")
    lines = process.stdout.splitlines()
    if len(lines) != 1:
        raise ValueError("forbidden-knowledge policy probe output is ambiguous")
    result = _strict_json(lines[0], "policy probe output")
    _exact_object(result, (
        "tests_run", "failures", "errors", "skipped", "successful",
    ), "policy probe output")
    for field in ("tests_run", "failures", "errors", "skipped"):
        _integer(result[field], f"policy probe output.{field}", minimum=0)
    if not isinstance(result["successful"], bool):
        raise ValueError("policy probe success must be boolean")
    return result


def flag_stands_from_bsp(payload, implementation=None):
    """Derive the two stand origins from the exact authenticated BSP bytes."""
    try:
        import bspmechanisms
        import mapflags
    except ModuleNotFoundError:  # python -m tools.stealstage
        from tools import bspmechanisms, mapflags
    if implementation is not None:
        _require_bound_helper(
            bspmechanisms, "tools/bspmechanisms.py", implementation)
        _require_bound_helper(mapflags, "tools/mapflags.py", implementation)
        corpusgraph = (sys.modules.get("corpusgraph") or
                       sys.modules.get("tools.corpusgraph"))
        if corpusgraph is None:
            raise ValueError("measurement helper corpusgraph was not loaded")
        _require_bound_helper(
            corpusgraph, "tools/corpusgraph.py", implementation)
    text = mapflags.bsp_entities(bytes(payload), "authenticated BSP")
    parsed, issues = bspmechanisms._parse_entity_text(text)
    if issues:
        raise ValueError(f"BSP entity stream is ambiguous: {issues}")
    entities, _removed = bspmechanisms._postspawn_deathmatch_entities(parsed)
    stands = {}
    aliases = {"info_flag_red": "red", "info_flag_blue": "blue"}
    for entity in entities:
        if entity is None or entity.get("classname") not in aliases:
            continue
        color = aliases[entity["classname"]]
        raw = entity.get("origin")
        if color in stands or not isinstance(raw, str):
            raise ValueError(f"BSP does not have exactly one {color} flag stand")
        parts = raw.split()
        if len(parts) != 3:
            raise ValueError(f"BSP {color} flag stand origin is malformed")
        stands[color] = tuple(_finite_number(float(value),
                                               f"BSP {color} stand")
                              for value in parts)
    if set(stands) != CTF_TEAMS:
        raise ValueError("BSP must contain exactly one red and blue flag stand")
    return stands


def _host_kill_anchors(lines, combat_events):
    cursor = 0
    anchors = []
    for event in combat_events:
        if event["kind"] != "Kill":
            continue
        found = None
        victim_prefix = event["victim"] + " "
        for index in range(cursor, len(lines)):
            if (lines[index].startswith(victim_prefix) and
                    event["attacker"] in lines[index]):
                found = index
                break
        if found is None:
            raise ValueError(
                f"no ordered host obituary for {event['attacker']} -> "
                f"{event['victim']} at {event['time']}")
        anchors.append({"line": found, "time": event["time"]})
        cursor = found + 1
    return anchors


def _server_frame(value, label):
    """Convert an exact server-time decimal to the 100 ms production tick."""
    exact, _number = _decimal_number(value, label)
    ticks = exact * Decimal(10)
    integral = ticks.to_integral_value()
    if ticks != integral or integral < 0 or integral > SG_INT_MAX:
        raise ValueError(f"{label} is not an integral production frame")
    return int(integral)


def validate_census(lines, roster_info):
    """Validate the exact 1 Hz production SGCENSUS admission timeline.

    Production emits after frame zero on frames divisible by ten.  The roster
    admission instant is excluded and the full-roster allowance endpoint is
    included, yielding exactly 600 observations per bot for a 600-second
    Stage-A window.  SG route diagnostics remain independently sparse.
    """
    roster = tuple(_validated_roster(roster_info["roster"]))
    if len(roster) != 10:
        raise ValueError("SGCENSUS authority requires exactly ten clients")
    start, end = roster_info["window"]
    start_frame = _server_frame(start, "census window start")
    end_frame = _server_frame(end, "census window end")
    first_frame = ((start_frame // 10) + 1) * 10
    expected = tuple(range(first_frame, end_frame + 1, 10))
    if len(expected) != 600:
        raise ValueError("SGCENSUS window does not contain exactly 600 pulses")
    observed = {name: [] for name in roster}
    alive = {name: [] for name in roster}
    previous = {name: None for name in roster}
    context_before = 0
    context_after = 0
    for line_number, line in enumerate(lines):
        if not line.startswith("SGCENSUS "):
            continue
        match = SG_CENSUS_RE.fullmatch(line)
        if match is None:
            raise ValueError(f"malformed SGCENSUS row at host line {line_number + 1}")
        name = match.group("name")
        if name not in observed:
            raise ValueError("SGCENSUS row names a non-roster client")
        frame = int(match.group("frame"))
        if frame > SG_INT_MAX or frame <= 0 or frame % 10 != 0:
            raise ValueError("SGCENSUS frame is outside the production cadence")
        if previous[name] is not None and frame <= previous[name]:
            raise ValueError("SGCENSUS frames are duplicate, replayed, or nonmonotonic")
        previous[name] = frame
        if frame < first_frame:
            context_before += 1
        elif frame > end_frame:
            context_after += 1
        else:
            observed[name].append(frame)
            alive[name].append(int(match.group("alive")))
    for name in roster:
        if tuple(observed[name]) != expected:
            raise ValueError(
                f"SGCENSUS is not the exact gap-free 1 Hz timeline for {name}")
    by_bot = {
        name: {"coverage_seconds": len(observed[name]),
               "alive_seconds": sum(alive[name]),
               "dead_frames": [frame for frame, is_alive in
                               zip(observed[name], alive[name]) if not is_alive]}
        for name in sorted(roster)
    }
    by_team = {
        color: {
            "coverage_bot_seconds": sum(
                by_bot[name]["coverage_seconds"] for name in roster
                if roster_info["teams"][name] == color),
            "alive_bot_seconds": sum(
                by_bot[name]["alive_seconds"] for name in roster
                if roster_info["teams"][name] == color),
        }
        for color in CTF_TEAMS
    }
    return {
        "first_frame": first_frame, "last_frame": end_frame,
        "expected_frames_per_bot": len(expected), "by_bot": by_bot,
        "coverage_bot_seconds": sum(item["coverage_seconds"]
                                    for item in by_bot.values()),
        "alive_bot_seconds": sum(item["alive_seconds"]
                                 for item in by_bot.values()),
        "context_rows_before": context_before,
        "context_rows_after": context_after,
        "by_team": by_team,
    }


def diagnostic_metrics(host, stdlog, roster_info, rune, contract):
    lines = host["lines"]
    roster = roster_info["roster"]
    join_lines = {}
    for name in roster:
        exact = f"{name} entered the game"
        matches = [index for index, line in enumerate(lines) if line == exact]
        if len(matches) != 1:
            raise ValueError(f"expected one host roster join for {name}")
        join_lines[name] = matches[0]
    start_line = max(join_lines.values())
    score_events = [event for event in stdlog["flag_events"]
                    if event["kind"] in ("F Pickup", "F Capture")]
    if len(score_events) != len(host["outcomes"]):
        raise ValueError("host/StdLog outcome cardinality changed")
    anchors = _host_kill_anchors(lines, stdlog["combat_events"])
    anchors.extend({"line": outcome["line"], "time": event["time"]}
                   for outcome, event in zip(host["outcomes"], score_events))
    end = roster_info["window"][1]
    eligible = [item for item in anchors
                if item["line"] > start_line and item["time"] < end]
    if not eligible:
        raise ValueError("no authoritative outcome brackets SG telemetry")
    cutoff = max(eligible, key=lambda item: (item["time"], item["line"]))
    cutoff_gap = end - cutoff["time"]
    maximum_gap = contract["measurement_limits"][
        "diagnostic_cutoff_gap_seconds_max"]
    if not 0.0 < cutoff_gap <= maximum_gap:
        raise ValueError("SG telemetry cutoff gap exceeds frozen bound")

    census = validate_census(lines, roster_info)
    rows = []
    prior_frame = {name: None for name in roster}
    for line_number, line in enumerate(lines):
        if not line.startswith("SG "):
            continue
        match = SG_REPORT_RE.fullmatch(line)
        if match is None:
            if any(line.startswith(f"SG {name}:") for name in roster):
                raise ValueError(f"malformed SG row at host line {line_number + 1}")
            continue
        name = match.group("name")
        if name not in roster:
            raise ValueError("SG row names a non-roster client")
        fields = {key: int(match.group(key)) for key in (
            "role", "seed", "goal", "sgoal", "speed", "x", "y", "z",
            "link", "action", "hook_phase", "door_hold", "drop_locked",
            "grounded", "engaged", "frame",
        )}
        stuck = _finite_number(float(match.group("stuck")), "SG stuck")
        if stuck < 0.0 or stuck > 86400.0:
            raise ValueError("SG stuck must be nonnegative")
        if (fields["role"] not in SG_ROLE_VALUES or
                fields["action"] not in SG_RUNTIME_ACTION_VALUES):
            raise ValueError("SG role/action enum is invalid")
        if (fields["seed"] < -1 or fields["goal"] < -1 or
                fields["sgoal"] < -1 or fields["link"] < -1):
            raise ValueError("SG route index is invalid")
        if (fields["hook_phase"] not in range(4) or
                fields["door_hold"] not in range(4) or
                fields["drop_locked"] not in (0, 1)):
            raise ValueError("SG state enum is invalid")
        if any(abs(fields[field]) > SG_INT_MAX for field in (
                "seed", "goal", "sgoal", "speed", "x", "y", "z",
                "link", "frame")):
            raise ValueError("SG integer field exceeds production range")
        if fields["speed"] < 0:
            raise ValueError("SG speed must be nonnegative")
        frame = fields["frame"]
        if prior_frame[name] is not None and frame <= prior_frame[name]:
            raise ValueError(f"SG frames are nonmonotonic for {name}")
        prior_frame[name] = frame
        seed, link, action = (fields["seed"], fields["link"],
                              fields["action"])
        if seed >= len(rune.seeds):
            raise ValueError("SG seed exceeds authenticated RUNE")
        if link == -1:
            if action != -1:
                raise ValueError("SG link/action disagree")
        else:
            if link >= len(rune.links) or action == -1:
                raise ValueError("SG link/action exceed authenticated RUNE")
            bound = rune.links[link]
            if bound.source != seed or bound.action != action:
                raise ValueError("SG row disagrees with authenticated RUNE")
        rows.append({"line": line_number, "name": name, **fields,
                     "stuck": stuck})
    cropped = crop_diagnostic_rows(
        rows, start_line, cutoff["line"], roster)
    if any(not census["first_frame"] <= row["frame"] <=
           census["last_frame"] for row in cropped):
        raise ValueError("SG diagnostic row falls outside exact census authority")
    if any(row["frame"] in census["by_bot"][row["name"]]["dead_frames"]
           for row in cropped):
        raise ValueError("SG diagnostic row conflicts with dead census state")
    per_bot = Counter(row["name"] for row in cropped)
    minimum = contract["measurement_limits"][
        "minimum_diagnostic_rows_per_bot"]
    if any(per_bot[name] < minimum for name in roster):
        raise ValueError("SG diagnostic crop lacks an expected client")

    by_team = {color: {key: 0 for key in (
        "samples", "moving", "engaged", "defenders", "defender_dwell",
        "defender_moving", "departures")}
        for color in CTF_TEAMS}
    previous_post = {}
    for row in cropped:
        result = by_team[roster_info["teams"][row["name"]]]
        moving = row["speed"] > 50
        engaged = row["engaged"] == 1
        post = 0 <= row["sgoal"] < 1500
        result["samples"] += 1
        result["moving"] += moving
        result["engaged"] += engaged
        if row["role"] == 1:
            result["defenders"] += 1
            result["defender_dwell"] += post
            result["defender_moving"] += moving
            if previous_post.get(row["name"]) is True and not post:
                result["departures"] += 1
            previous_post[row["name"]] = post
        else:
            previous_post.pop(row["name"], None)
    total = {key: sum(team[key] for team in by_team.values())
             for key in next(iter(by_team.values()))}
    return {**total, "by_team": by_team,
            "by_bot_samples": dict(sorted(per_bot.items())),
            "census": census,
            "brackets": {"start_line": start_line,
                         "end_line": cutoff["line"],
                         "cutoff_server_time": cutoff["time"],
                         "cutoff_gap_seconds": cutoff_gap}}


def validate_demo_alignment(demo, roster_info, expected_map, window,
                            fps=DEMO_FPS, residual_max=0.11):
    if not isinstance(demo, Mapping) or not demo.get("svrecord"):
        raise ValueError("demo is not an authenticated serverrecord")
    if demo.get("map") != expected_map:
        raise ValueError("serverrecord map differs from receipt")
    frame_count = _integer(demo.get("frames"), "serverrecord frames", minimum=1)
    tracks = demo.get("tracks")
    if not isinstance(tracks, Mapping):
        raise ValueError("serverrecord tracks are missing")
    candidates = []
    for entity, raw_track in tracks.items():
        if isinstance(entity, bool) or not isinstance(entity, int):
            raise ValueError("serverrecord entity identifiers must be integers")
        if not (1 <= entity <= 16) or not raw_track:
            continue
        track = []
        prior = None
        for index, raw in enumerate(_sequence(raw_track, f"track {entity}")):
            values = _sequence(raw, f"track {entity}[{index}]")
            if len(values) != 5:
                raise ValueError("serverrecord track row must contain five values")
            frame = _integer(values[0], "serverrecord frame", minimum=0)
            if prior is not None and frame != prior + 1:
                raise ValueError("serverrecord client frames must be consecutive")
            row = (frame,) + tuple(
                _finite_number(values[column], "serverrecord coordinate")
                for column in range(1, 4)) + (
                _integer(values[4], "serverrecord effects", minimum=0),)
            track.append(row)
            prior = frame
        candidates.append((entity, track))
    roster = roster_info["roster"]
    if len(candidates) != len(roster):
        raise ValueError("serverrecord does not contain exactly all expected clients")
    connect_times = roster_info["connected"]
    if len(set(connect_times.values())) != len(connect_times):
        raise ValueError("client connection order is ambiguous")
    candidates.sort(key=lambda item: (item[1][0][0], item[0]))
    skins = demo.get("skin_epochs")
    if not isinstance(skins, Mapping):
        raise ValueError("serverrecord client identity epochs are missing")
    identified = []
    for entity, track in candidates:
        epochs = skins.get(entity - 1)
        if (not isinstance(epochs, (list, tuple)) or not epochs or
                not isinstance(epochs[0], (list, tuple)) or
                len(epochs[0]) != 2):
            raise ValueError("serverrecord lacks a client identity epoch")
        effective = _integer(epochs[0][0], "skin epoch frame", minimum=0)
        value = epochs[0][1]
        if not isinstance(value, str) or not value:
            raise ValueError("serverrecord client identity is empty")
        name = value.split("\\", 1)[0]
        if name not in roster:
            raise ValueError("serverrecord identifies a non-roster client")
        identified.append((name, entity, track, effective, epochs[1:]))
    if len({item[0] for item in identified}) != len(roster):
        raise ValueError("serverrecord client identities are duplicate or missing")
    mapping = {name: {"entity": entity, "track": track,
                      "identity_epoch": effective, "later_epochs": later}
               for name, entity, track, effective, later in identified}
    first = {name: record["track"][0][0]
             for name, record in mapping.items()}
    offset = demo_level_offset(connect_times, first, fps=fps)
    residuals = {name: connect_times[name] - first[name] / fps - offset
                 for name in roster}
    if max(abs(value) for value in residuals.values()) > residual_max:
        raise ValueError("serverrecord roster alignment residual exceeds bound")
    start, end = _validated_window(window)
    for name, record in mapping.items():
        track = record["track"]
        first_time = track[0][0] / fps + offset
        last_time = track[-1][0] / fps + offset
        if first_time > start or last_time < end:
            raise ValueError(f"serverrecord track coverage is short for {name}")
        if record["identity_epoch"] / fps + offset > start + residual_max:
            raise ValueError(f"serverrecord identity begins after window for {name}")
        for raw_epoch in record["later_epochs"]:
            values = _sequence(raw_epoch, "later skin epoch")
            if len(values) != 2:
                raise ValueError("serverrecord skin epoch is malformed")
            frame = _integer(values[0], "later skin epoch frame", minimum=0)
            value = values[1]
            if not isinstance(value, str):
                raise ValueError("serverrecord skin epoch name is malformed")
            epoch_name = value.split("\\", 1)[0]
            if frame / fps + offset < end and epoch_name != name:
                raise ValueError("serverrecord client rename/slot drift inside window")
    if frame_count / fps + offset < end:
        raise ValueError("serverrecord total coverage is short")
    return {"frames": frame_count, "mapping": mapping, "offset": offset,
            "residuals": residuals,
            "level_time_end": frame_count / fps + offset}


def observed_stand_seconds(name, team, track, level_offset, events, teams,
                           window, fps=DEMO_FPS):
    """One enemy stand's observed seconds, excluding gaps/non-home intervals."""
    validated_teams = _validated_teams(teams)
    if name not in validated_teams or validated_teams[name] != team:
        raise ValueError("stand observation actor/team mismatch")
    validated_events, validated_teams = _validated_flag_events(
        events, validated_teams)
    fps = _finite_number(fps, "demo fps")
    if fps <= 0:
        raise ValueError("demo fps must be positive")
    level_offset = _finite_number(level_offset, "demo level offset")
    start, end = _validated_window(window)
    rows = _sequence(track, "serverrecord track")
    enemy = flag_color_for_thief(team)
    seconds = 0.0
    prior = None
    for index, row in enumerate(rows):
        values = _sequence(row, f"serverrecord track[{index}]")
        if len(values) != 5:
            raise ValueError("serverrecord track row must contain five values")
        frame = _integer(values[0], "serverrecord frame", minimum=0)
        if prior is not None and frame == prior + 1:
            moment = frame / fps + level_offset
            if (start <= moment < end and _flag_home_before_validated(
                    enemy, moment, validated_events, validated_teams)):
                seconds += 1.0 / fps
        prior = frame
    return seconds


def carry_starts(name, team, track, level_offset, fps=DEMO_FPS):
    starts = []
    previous = 0
    for row in track:
        frame = _integer(row[0], "carry frame", minimum=0)
        effects = _integer(row[4], "carry effects", minimum=0)
        current = effects & 0x000c0000
        if previous == 0 and current != 0:
            starts.append({"name": name, "team": team,
                           "time": frame / fps + level_offset})
        previous = current
    return starts


def reconcile_carry_starts(starts, pickups, tolerance=0.2):
    """One-to-one maximum-cardinality same-player carry reconciliation."""
    exact_tolerance, tolerance = _decimal_number(tolerance, "carry tolerance")
    if exact_tolerance < 0:
        raise ValueError("carry tolerance must be nonnegative")
    starts = [dict(record, time=moment) for record, moment in
              _timed_records(starts, "carry starts", require_sorted=False)]
    pickups = [dict(record, time=moment) for record, moment in
               _timed_records(pickups, "carry pickups")]
    for record in starts:
        if not isinstance(record.get("name"), str):
            raise ValueError("carry start name is invalid")
    for record in pickups:
        if (not isinstance(record.get("name"), str) or
                record.get("kind") != "F Pickup"):
            raise ValueError("carry pickup is invalid")
    unused = sorted(starts, key=lambda item: (item["time"], item["name"]))
    matches = []
    for pickup in sorted(pickups, key=lambda item: (item["time"],
                                                    item.get("log_order", 0))):
        candidates = [item for item in unused
                      if item["name"] == pickup["name"] and
                      abs(Decimal(str(item["time"])) -
                          Decimal(str(pickup["time"]))) <= exact_tolerance]
        if not candidates:
            continue
        start = min(candidates, key=lambda item: item["time"])
        unused.remove(start)
        matches.append({"start": start, "pickup": pickup,
                        "residual": float(Decimal(str(start["time"])) -
                                          Decimal(str(pickup["time"])))})
    matched_pickup_ids = {id(match["pickup"]) for match in matches}
    # The sorted copies above are the objects in matches, so identity is stable.
    ordered_pickups = sorted(pickups, key=lambda item: (item["time"],
                                                       item.get("log_order", 0)))
    unmatched_pickups = [item for item in ordered_pickups
                         if id(item) not in matched_pickup_ids]
    if unused or unmatched_pickups:
        raise ValueError(
            f"carry effects/StdLog pickups do not reconcile one-to-one: "
            f"starts={unused}, pickups={unmatched_pickups}")
    return matches


def _window_counts(stdlog, roster_info):
    window = roster_info["window"]
    score = crop_events(stdlog["flag_events"], window)
    combat = crop_events(stdlog["combat_events"], window)
    keys = ("pickups", "captures", "frags", "fragged", "suicides")
    total = {key: 0 for key in keys}
    by_team = {color: {key: 0 for key in keys} for color in CTF_TEAMS}
    for event in score:
        if event["kind"] == "F Pickup":
            key = "pickups"
        elif event["kind"] == "F Capture":
            key = "captures"
        else:
            continue
        color = roster_info["teams"][event["name"]]
        total[key] += 1
        by_team[color][key] += 1
    for event in combat:
        if event["kind"] == "Kill":
            total["frags"] += 1
            total["fragged"] += 1
            by_team[roster_info["teams"][event["attacker"]]]["frags"] += 1
            by_team[roster_info["teams"][event["victim"]]]["fragged"] += 1
        else:
            total["suicides"] += 1
            by_team[roster_info["teams"][event["name"]]]["suicides"] += 1
    return {"total": total, "by_team": by_team}


def analyze_demo(demo, roster_info, stands, stdlog, expected_map, contract):
    limits = contract["measurement_limits"]
    aligned = validate_demo_alignment(
        demo, roster_info, expected_map, roster_info["window"],
        fps=limits["demo_fps"],
        residual_max=limits["alignment_residual_seconds_max"])
    by_team = {color: {"distance": 0.0, "approaches": 0,
                       "observed_stand_seconds": 0.0, "timely": 0}
               for color in CTF_TEAMS}
    approaches = []
    starts = []
    fps = limits["demo_fps"]
    for name, record in aligned["mapping"].items():
        team = roster_info["teams"][name]
        track = record["track"]
        for before, after in zip(track, track[1:]):
            f0, x0, y0, _z0, _effects0 = before
            f1, x1, y1, _z1, _effects1 = after
            moment = f1 / fps + aligned["offset"]
            if f1 == f0 + 1 and in_window(moment, roster_info["window"]):
                distance = math.hypot(x1 - x0, y1 - y0)
                if not math.isfinite(distance):
                    raise ValueError("serverrecord movement is non-finite")
                if distance <= 180.0:
                    by_team[team]["distance"] += distance
        records = qualifying_approaches(
            name, team, track, stands[flag_color_for_thief(team)],
            aligned["offset"], stdlog["flag_events"], roster_info["teams"],
            roster_info["window"], fps=fps)
        approaches.extend(records)
        by_team[team]["approaches"] += len(records)
        starts.extend(carry_starts(name, team, track, aligned["offset"], fps))
    # A stand-minute is a property of one target stand, not five duplicate
    # bot-minutes.  Every validated client track covers the exact window, so
    # one deterministic track per attacking team is the observation clock.
    for team in sorted(CTF_TEAMS):
        name = min(name for name in aligned["mapping"]
                   if roster_info["teams"][name] == team)
        by_team[team]["observed_stand_seconds"] = observed_stand_seconds(
            name, team, aligned["mapping"][name]["track"], aligned["offset"],
            stdlog["flag_events"], roster_info["teams"],
            roster_info["window"], fps=fps)
    window_pickups = [event for event in crop_events(
        stdlog["flag_events"], roster_info["window"])
        if event["kind"] == "F Pickup"]
    matches = match_close_pickups(approaches, window_pickups)
    for match in matches:
        by_team[match["approach"]["team"]]["timely"] += 1
    all_pickups = [event for event in stdlog["flag_events"]
                   if event["kind"] == "F Pickup"]
    starts.sort(key=lambda record: (record["time"], record["name"]))
    carry_matches = reconcile_carry_starts(
        starts, all_pickups,
        tolerance=limits["carry_reconciliation_seconds_max"])
    return {
        "frames": aligned["frames"],
        "level_time_offset_seconds": aligned["offset"],
        "level_time_end_seconds": aligned["level_time_end"],
        "roster_alignment_residual_seconds": aligned["residuals"],
        "distance": sum(item["distance"] for item in by_team.values()),
        "approaches": len(approaches),
        "observed_stand_seconds": sum(
            item["observed_stand_seconds"] for item in by_team.values()),
        "timely": len(matches),
        "effects_carry_starts": len(starts),
        "carry_reconciliation_count": len(carry_matches),
        "by_team": by_team,
    }


def _safe_div(numerator, denominator):
    if numerator is None or denominator is None or denominator == 0:
        return None
    return numerator / denominator


def aggregate_rounds(rounds, arm):
    selected = [record for record in rounds if record["arm"] == arm]
    if not selected:
        raise ValueError(f"no rounds for {arm}")
    raw_fields = (
        "bot_seconds", "team_seconds", "distance", "samples", "moving",
        "engaged", "defenders", "defender_dwell", "defender_moving",
        "departures", "suicides", "frags", "fragged", "pickups",
        "captures", "approaches", "observed_stand_seconds", "timely",
        "forbidden", "mismatches",
    )
    raw = {field: 0.0 for field in raw_fields}
    team_fields = (
        "team_seconds", "distance", "samples", "moving", "engaged",
        "defenders", "defender_dwell", "defender_moving", "departures",
        "frags", "fragged", "pickups", "captures", "approaches",
        "observed_stand_seconds", "timely", "captures_conceded",
    )
    team = {color: {field: 0.0 for field in team_fields}
            for color in CTF_TEAMS}
    gaps = []
    for record in selected:
        raw["bot_seconds"] += WINDOW_SECONDS * 10
        raw["team_seconds"] += WINDOW_SECONDS * 2
        for field in ("distance", "approaches", "observed_stand_seconds",
                      "timely"):
            raw[field] += record["demo"][field]
        for field in ("samples", "moving", "engaged", "defenders",
                      "defender_dwell", "defender_moving", "departures"):
            raw[field] += record["telemetry"][field]
        for field in ("suicides", "frags", "fragged", "pickups", "captures"):
            raw[field] += record["window_counts"]["total"][field]
        raw["forbidden"] += record["authority"]["forbidden_knowledge_events"]
        raw["mismatches"] += record["authority"]["reconciliation_mismatches"]
        gaps.append(record["telemetry"]["brackets"]["cutoff_gap_seconds"])
        for color in CTF_TEAMS:
            team[color]["team_seconds"] += WINDOW_SECONDS
            for field in ("distance", "approaches", "observed_stand_seconds",
                          "timely"):
                team[color][field] += record["demo"]["by_team"][color][field]
            for field in ("samples", "moving", "engaged", "defenders",
                          "defender_dwell", "defender_moving", "departures"):
                team[color][field] += record["telemetry"]["by_team"][color][field]
            for field in ("frags", "fragged", "pickups", "captures"):
                team[color][field] += record["window_counts"]["by_team"][color][field]
            opponent = "blue" if color == "red" else "red"
            team[color]["captures_conceded"] += record[
                "window_counts"]["by_team"][opponent]["captures"]
    bot_minutes = raw["bot_seconds"] / 60.0
    team_minutes = raw["team_seconds"] / 60.0
    defender_minutes = raw["defenders"] / 60.0
    rates = {
        "horizontal_distance_per_active_bot_minute":
            _safe_div(raw["distance"], bot_minutes),
        "moving_sample_fraction": _safe_div(raw["moving"], raw["samples"]),
        "world_or_hazard_suicides_per_active_bot_minute":
            _safe_div(raw["suicides"], bot_minutes),
        "combat_kills_per_active_team_minute":
            _safe_div(raw["frags"], team_minutes),
        "combat_deaths_per_active_team_minute":
            _safe_div(raw["fragged"], team_minutes),
        "visible_or_audible_engagements_per_active_team_minute":
            _safe_div(raw["engaged"], team_minutes),
        "authoritative_pickups_per_active_team_minute":
            _safe_div(raw["pickups"], team_minutes),
        "approaches_per_observed_stand_minute":
            _safe_div(raw["approaches"], raw["observed_stand_seconds"] / 60.0),
        "close_approach_conversion": _safe_div(raw["timely"], raw["approaches"]),
        "authoritative_captures_per_active_team_minute":
            _safe_div(raw["captures"], team_minutes),
        "steal_to_capture_conversion": _safe_div(raw["captures"], raw["pickups"]),
        "defender_post_dwell_fraction":
            _safe_div(raw["defender_dwell"], raw["defenders"]),
        "defender_moving_sample_fraction":
            _safe_div(raw["defender_moving"], raw["defenders"]),
        "defender_departures_per_active_defender_minute":
            _safe_div(raw["departures"], defender_minutes),
        "captures_conceded_per_active_team_minute":
            _safe_div(raw["captures"], team_minutes),
    }
    strata = {}
    for color, value in sorted(team.items()):
        minutes = value["team_seconds"] / 60.0
        defender_minutes = value["defenders"] / 60.0
        strata[color] = {"raw": value, "rates": {
            "combat_kills_per_active_team_minute": _safe_div(value["frags"], minutes),
            "combat_deaths_per_active_team_minute": _safe_div(value["fragged"], minutes),
            "visible_or_audible_engagements_per_active_team_minute": _safe_div(value["engaged"], minutes),
            "authoritative_pickups_per_active_team_minute": _safe_div(value["pickups"], minutes),
            "approaches_per_observed_stand_minute": _safe_div(
                value["approaches"], value["observed_stand_seconds"] / 60.0),
            "close_approach_conversion": _safe_div(value["timely"], value["approaches"]),
            "authoritative_captures_per_active_team_minute": _safe_div(value["captures"], minutes),
            "defender_post_dwell_fraction": _safe_div(value["defender_dwell"], value["defenders"]),
            "defender_moving_sample_fraction": _safe_div(value["defender_moving"], value["defenders"]),
            "defender_departures_per_active_defender_minute": _safe_div(value["departures"], defender_minutes),
            "captures_conceded_per_active_team_minute": _safe_div(value["captures_conceded"], minutes),
        }}
    return {"raw": raw, "rates": rates, "team_strata": strata,
            "diagnostic_cutoff_gap_seconds": gaps}


def evaluate_bands(baseline, candidate, contract):
    """Apply every frozen numeric band and the event sufficiency rule."""
    b, c, bands = baseline["rates"], candidate["rates"], contract["bands"]
    checks = {}

    def add(name, passed, actual, requirement):
        if actual is not None:
            _finite_number(actual, f"gate {name}")
        checks[name] = {"pass": bool(passed), "actual": actual,
                        "requirement": requirement}

    def ratio(name):
        return _safe_div(c[name], b[name])

    value = ratio("horizontal_distance_per_active_bot_minute")
    add("movement.distance_ratio", value is not None and value >=
        bands["movement"]["horizontal_distance_per_active_bot_minute_ratio_min"],
        value, ">= frozen minimum")
    value = c["moving_sample_fraction"] - b["moving_sample_fraction"]
    add("movement.moving_fraction_delta", value >=
        bands["movement"]["moving_sample_fraction_absolute_delta_min"],
        value, ">= frozen minimum")
    value = (c["world_or_hazard_suicides_per_active_bot_minute"] -
             b["world_or_hazard_suicides_per_active_bot_minute"])
    add("movement.suicide_rate_delta", value <=
        bands["movement"]["world_or_hazard_suicides_per_active_bot_minute_delta_max"],
        value, "<= frozen maximum")

    value = ratio("combat_kills_per_active_team_minute")
    add("combat.kill_rate_ratio", value is not None and
        bands["combat"]["combat_kills_per_active_team_minute_ratio_min"] <=
        value <= bands["combat"]["combat_kills_per_active_team_minute_ratio_max"],
        value, "inside frozen band")
    value = ratio("combat_deaths_per_active_team_minute")
    add("combat.death_rate_ratio", value is not None and value <=
        bands["combat"]["combat_deaths_per_active_team_minute_ratio_max"],
        value, "<= frozen maximum")

    value = ratio("visible_or_audible_engagements_per_active_team_minute")
    add("perception.engagement_rate_ratio", value is not None and
        bands["perception"]["visible_or_audible_engagements_per_active_team_minute_ratio_min"] <=
        value <= bands["perception"]["visible_or_audible_engagements_per_active_team_minute_ratio_max"],
        value, "inside frozen band")
    forbidden = baseline["raw"]["forbidden"] + candidate["raw"]["forbidden"]
    add("perception.forbidden_knowledge_events", forbidden <=
        bands["perception"]["forbidden_knowledge_events_max"], forbidden,
        "<= frozen maximum")

    value = ratio("authoritative_pickups_per_active_team_minute")
    add("steal.pickup_rate_ratio",
        b["authoritative_pickups_per_active_team_minute"] == 0 or
        (value is not None and value >= bands["steal"][
            "authoritative_pickups_per_active_team_minute_ratio_min"]),
        value, ">= frozen minimum unless zero baseline")
    value = (c["authoritative_pickups_per_active_team_minute"] -
             b["authoritative_pickups_per_active_team_minute"])
    add("steal.pickup_rate_delta", value >= bands["steal"][
        "authoritative_pickups_per_active_team_minute_delta_min"],
        value, ">= frozen minimum")
    value = candidate["raw"]["pickups"] - baseline["raw"]["pickups"]
    add("steal.pickup_count_delta", value >= bands["steal"][
        "authoritative_pickup_count_delta_min"], value, ">= frozen minimum")
    value = ratio("approaches_per_observed_stand_minute")
    add("steal.approach_rate_ratio",
        b["approaches_per_observed_stand_minute"] == 0 or
        (value is not None and value >= bands["steal"][
            "approaches_per_observed_stand_minute_ratio_min"]),
        value, ">= frozen minimum unless zero baseline")

    value = (None if c["close_approach_conversion"] is None or
             b["close_approach_conversion"] is None else
             c["close_approach_conversion"] - b["close_approach_conversion"])
    add("conversion.absolute_delta", value is not None and value >=
        bands["conversion"]["close_approach_conversion_absolute_delta_min"],
        value, ">= frozen minimum")
    value = ratio("close_approach_conversion")
    add("conversion.ratio", b["close_approach_conversion"] == 0 or
        (value is not None and value >=
         bands["conversion"]["close_approach_conversion_ratio_min"]),
        value, ">= frozen minimum unless zero baseline")
    value = candidate["raw"]["timely"] - baseline["raw"]["timely"]
    add("conversion.timely_count_delta", value >= bands["conversion"][
        "timely_authoritative_pickup_count_delta_min"],
        value, ">= frozen minimum")

    value = ratio("authoritative_captures_per_active_team_minute")
    add("capture.rate_ratio",
        b["authoritative_captures_per_active_team_minute"] == 0 or
        (value is not None and value >= bands["capture"][
            "authoritative_captures_per_active_team_minute_ratio_min"]),
        value, ">= frozen minimum unless zero baseline")
    value = (None if c["steal_to_capture_conversion"] is None or
             b["steal_to_capture_conversion"] is None else
             c["steal_to_capture_conversion"] -
             b["steal_to_capture_conversion"])
    add("capture.steal_to_capture_delta", value is not None and value >=
        bands["capture"]["steal_to_capture_conversion_absolute_delta_min"],
        value, ">= frozen minimum")
    mismatches = baseline["raw"]["mismatches"] + candidate["raw"]["mismatches"]
    add("capture.reconciliation_mismatches", mismatches <=
        bands["capture"]["authoritative_reconciliation_mismatches_max"],
        mismatches, "<= frozen maximum")

    for metric, key in (
        ("defender_post_dwell_fraction", "defender_post_dwell_fraction_ratio_min"),
        ("defender_moving_sample_fraction", "defender_moving_sample_fraction_ratio_min"),
    ):
        value = ratio(metric)
        passed = (c[metric] is not None and b[metric] == 0 or
                  value is not None and value >= bands["defense"][key])
        add(f"defense.{metric}_ratio", passed, value, ">= frozen minimum")
    value = ratio("defender_departures_per_active_defender_minute")
    passed = (b["defender_departures_per_active_defender_minute"] == 0 and
              c["defender_departures_per_active_defender_minute"] == 0) or (
                  value is not None and value <= bands["defense"][
                      "defender_departures_per_active_defender_minute_ratio_max"])
    add("defense.departure_rate_ratio", passed, value, "<= frozen maximum")
    value = ratio("captures_conceded_per_active_team_minute")
    add("defense.captures_conceded_ratio",
        b["captures_conceded_per_active_team_minute"] == 0 or
        (value is not None and value <= bands["defense"][
            "captures_conceded_per_active_team_minute_ratio_max"]),
        value, "<= frozen maximum unless zero baseline")
    value = (c["captures_conceded_per_active_team_minute"] -
             b["captures_conceded_per_active_team_minute"])
    add("defense.captures_conceded_delta", value <= bands["defense"][
        "captures_conceded_per_active_team_minute_delta_max"],
        value, "<= frozen maximum")

    minimum = contract["match_identity"][
        "minimum_authoritative_pickups_per_arm"]
    sufficient = (baseline["raw"]["pickups"] >= minimum and
                  candidate["raw"]["pickups"] >= minimum and
                  baseline["raw"]["approaches"] > 0 and
                  candidate["raw"]["approaches"] > 0 and
                  baseline["raw"]["observed_stand_seconds"] > 0 and
                  candidate["raw"]["observed_stand_seconds"] > 0)
    passed = sufficient and all(item["pass"] for item in checks.values())
    return {"checks": checks, "sufficient_events": sufficient,
            "all_bands_pass": passed,
            "decision": "adopt" if passed else
                        "strike" if sufficient else "inconclusive"}


def _validate_source_identity(value, label):
    _exact_object(value, ("source_commit", "source_patch_sha256",
                          "source_tree_sha256", "module_sha256"), label)
    return {
        "source_commit": _commit(value["source_commit"],
                                  f"{label}.source_commit"),
        "source_patch_sha256": _sha256(
            value["source_patch_sha256"], f"{label}.source_patch_sha256"),
        "source_tree_sha256": _sha256(
            value["source_tree_sha256"], f"{label}.source_tree_sha256"),
        "module_sha256": _sha256(value["module_sha256"],
                                  f"{label}.module_sha256"),
    }


def _require_commit(repository, commit):
    process = subprocess.run(
        ("git", "-C", os.fspath(repository), "cat-file", "-e",
         f"{commit}^{{commit}}"), stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False)
    if process.returncode != 0:
        raise ValueError(f"source commit is not present in repository: {commit}")


def _validate_candidate_delta(repository):
    """Permit production C/H deltas while excluding measurement authority."""
    raw = subprocess.run(
        ("git", "-C", os.fspath(repository), "diff", "--name-only", "-z",
         "HEAD", "--"), check=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE).stdout
    paths = [item.decode("utf-8") for item in raw.rstrip(b"\0").split(b"\0")
             if item]
    if not paths:
        raise ValueError("candidate source delta is empty")
    forbidden_exact = {
        *SOURCE_BUILD_INPUTS, POLICY_PROBE_PATH, "GitRevisionInfo.h",
    }
    invalid = [path for path in paths if (
        path in forbidden_exact or path.startswith(("tools/", "tests/")) or
        PurePosixPath(path).suffix not in (".c", ".h"))]
    if invalid:
        raise ValueError(
            "candidate delta touches measurement/build authority: " +
            ", ".join(sorted(invalid)))
    return tuple(sorted(paths, key=lambda value: value.encode("utf-8")))


def _validate_treatment_shape(value, label):
    _exact_object(value, (
        "source_commit", "source_patch_sha256", "source_tree_sha256",
        "module_sha256", "source_root", "source_artifacts",
    ), label)
    identity = _validate_source_identity({
        field: value[field] for field in (
            "source_commit", "source_patch_sha256", "source_tree_sha256",
            "module_sha256")
    }, f"{label}.identity")
    root = value["source_root"]
    if not isinstance(root, str) or not Path(root).is_absolute():
        raise ValueError(f"{label}.source_root must be an absolute path")
    artifacts = _exact_object(value["source_artifacts"], SOURCE_ARTIFACTS,
                              f"{label}.source_artifacts")
    records = {}
    paths = set()
    for name in SOURCE_ARTIFACTS:
        path, digest = _artifact_record(
            artifacts[name], f"{label}.source_artifacts.{name}")
        if path in paths:
            raise ValueError(f"{label} reuses a source-artifact path")
        paths.add(path)
        records[name] = (path, digest)
    if records["source_patch"][1] != identity["source_patch_sha256"]:
        raise ValueError(f"{label} patch artifact/identity mismatch")
    if records["source_manifest"][1] != identity["source_tree_sha256"]:
        raise ValueError(f"{label} source manifest/identity mismatch")
    return identity, root, records


def _load_treatment(value, arm, source_repository, tool_digest,
                    implementation_digest,
                    expected_build_inputs=None):
    label = f"manifest.treatments.{arm}"
    identity, root_text, records = _validate_treatment_shape(value, label)
    root = RetainedRoot(root_text)
    try:
        payloads = {}
        for name, (path, expected) in records.items():
            payload = root.read(
                path, MAX_TEXT_BYTES, f"{arm} source {name}",
                allow_empty=name == "source_patch")
            if sha256_bytes(payload) != expected:
                raise ValueError(f"{arm} source {name} SHA-256 mismatch")
            payloads[name] = payload
        if sha256_bytes(payloads["source_patch"]) != \
                identity["source_patch_sha256"]:
            raise ValueError(f"{arm} retained source patch identity mismatch")
        with _reconstructed_source(
                source_repository, identity["source_commit"],
                payloads["source_patch"]) as reconstructed:
            source_manifest = _source_tree_manifest(reconstructed)
            if source_manifest != payloads["source_manifest"]:
                raise ValueError(f"{arm} reconstructed source manifest mismatch")
            if sha256_bytes(source_manifest) != identity["source_tree_sha256"]:
                raise ValueError(f"{arm} reconstructed source-tree hash mismatch")
            if arm == "candidate":
                _validate_candidate_delta(reconstructed)
            probe = _run_knowledge_probe(reconstructed)
            rebuilt_module, revision_header, build_inputs = _rebuild_module(
                reconstructed, expected_build_inputs, source_manifest)
        if sha256_bytes(rebuilt_module) != identity["module_sha256"]:
            raise ValueError(
                f"{arm} module is not the frozen reconstructed source build")
        build = _strict_json(payloads["build_receipt"],
                             f"{arm} source build receipt")
        _exact_object(build, BUILD_RECEIPT_FIELDS,
                      f"{arm} source build receipt")
        expected_build = {
            "format": BUILD_RECEIPT_FORMAT, "metric_version": METRIC_VERSION,
            "recipe": SOURCE_BUILD_RECIPE, **identity,
            "build_input_sha256": build_inputs,
            "revision_header_sha256": sha256_bytes(revision_header),
        }
        if dict(build) != expected_build:
            raise ValueError(f"{arm} source build receipt mismatch")
        knowledge = validate_knowledge_report(
            payloads["knowledge_report"], identity,
            measurement_tool_sha256=tool_digest,
            measurement_implementation_sha256=implementation_digest,
            policy_probe=probe)
        for name, (path, expected) in records.items():
            current = root.read(
                path, MAX_TEXT_BYTES, f"{arm} final source {name}",
                allow_empty=name == "source_patch")
            if sha256_bytes(current) != expected:
                raise ValueError(f"{arm} source authority changed after analysis")
        root.assert_files_current()
        root.assert_current()
        return {
            "source_identity": identity,
            "source_root_identity": list(root.identity),
            "source_artifact_sha256": {
                name: records[name][1] for name in SOURCE_ARTIFACTS},
            "build_input_sha256": build_inputs,
            "policy_probe": {
                field: knowledge[field] for field in (
                    "tests_run", "failures", "errors", "skipped",
                    "successful")},
        }
    finally:
        root.close()


def _configuration_digest(records):
    """Hash a path-sorted configuration set with unambiguous length framing."""
    digest = hashlib.sha256()
    for path, payload in sorted(records, key=lambda item: item[0].encode("utf-8")):
        encoded_path = path.encode("utf-8")
        digest.update(struct.pack(">Q", len(encoded_path)))
        digest.update(encoded_path)
        digest.update(struct.pack(">Q", len(payload)))
        digest.update(payload)
    return digest.hexdigest()


def _parse_demo_entity(r, bits, state, *, zero_baseline):
    if zero_baseline:
        state[:] = [0.0, 0.0, 0.0, 0]
    if bits & U_MODEL:
        r.skip(1)
    if bits & U_MODEL2:
        r.skip(1)
    if bits & U_MODEL3:
        r.skip(1)
    if bits & U_MODEL4:
        r.skip(1)
    if bits & U_FRAME8:
        r.skip(1)
    if bits & U_FRAME16:
        r.skip(2)
    if bits & U_SKIN8 and bits & U_SKIN16:
        r.skip(4)
    elif bits & U_SKIN8:
        r.skip(1)
    elif bits & U_SKIN16:
        r.skip(2)
    if bits & U_EFFECTS8 and bits & U_EFFECTS16:
        state[3] = r.u32()
    elif bits & U_EFFECTS8:
        state[3] = r.u8()
    elif bits & U_EFFECTS16:
        state[3] = r.u16()
    if bits & U_RENDERFX8 and bits & U_RENDERFX16:
        r.skip(4)
    elif bits & U_RENDERFX8:
        r.skip(1)
    elif bits & U_RENDERFX16:
        r.skip(2)
    if bits & U_ORIGIN1:
        state[0] = r.s16() / 8.0
    if bits & U_ORIGIN2:
        state[1] = r.s16() / 8.0
    if bits & U_ORIGIN3:
        state[2] = r.s16() / 8.0
    if bits & U_ANGLE1:
        r.skip(1)
    if bits & U_ANGLE2:
        r.skip(1)
    if bits & U_ANGLE3:
        r.skip(1)
    if bits & U_OLDORIGIN:
        r.skip(6)
    if bits & U_SOUND:
        r.skip(1)
    if bits & U_EVENT:
        r.skip(1)
    if bits & U_SOLID:
        r.skip(2)


def _decode_serverrecord(payload, implementation=None):
    """Strict dependency-free port of the retained film serverrecord walk."""
    try:
        import dm2speed as demo_wire
        import demokin as demo_kin
    except ModuleNotFoundError:
        from tools import dm2speed as demo_wire
        from tools import demokin as demo_kin
    if implementation is not None:
        _require_bound_helper(demo_wire, "tools/dm2speed.py", implementation)
        _require_bound_helper(demo_kin, "tools/demokin.py", implementation)
        if getattr(demo_kin, "D", demo_wire) is not demo_wire:
            raise ValueError("demo helpers do not share one bound wire parser")
    parse_playerstate_full = demo_kin.parse_playerstate_full
    data = bytes(payload)
    offset = 0
    map_name = None
    skins = {}
    skin_epochs = {}
    entities = {}
    tracks = {}
    frame_index = 0
    serverrecord = None
    wire_frames = []
    messages = 0
    terminated = False

    def packet_entities(reader, *, zero_baseline):
        while True:
            bits, number = demo_wire.parse_entity_bits(reader)
            if number == 0:
                return
            if bits & U_REMOVE:
                entities.pop(number, None)
                continue
            state = entities.setdefault(number, [0.0, 0.0, 0.0, 0])
            _parse_demo_entity(reader, bits, state,
                               zero_baseline=zero_baseline)

    def snapshot():
        nonlocal frame_index
        frame_index += 1
        for number, state in entities.items():
            if 1 <= number <= 32:
                tracks.setdefault(number, []).append(
                    (frame_index, state[0], state[1], state[2], state[3]))

    while offset < len(data):
        if offset + 4 > len(data):
            raise ValueError("truncated demo message length")
        (length,) = struct.unpack_from("<i", data, offset)
        offset += 4
        if length == -1:
            if offset != len(data):
                raise ValueError("demo has bytes after terminal marker")
            terminated = True
            break
        if length < 0 or offset + length > len(data):
            raise ValueError("invalid or truncated demo message")
        reader = demo_wire.R(data[offset:offset + length])
        offset += length
        messages += 1
        try:
            while not reader.done():
                service = reader.u8()
                if service == 12:
                    reader.skip(9)
                    reader.str_()
                    player_number = reader.u16()
                    reader.str_()
                    serverrecord = player_number == 0xffff
                elif service == 13:
                    index = reader.u16()
                    value = reader.str_()
                    if 1312 <= index < 1312 + 256:
                        slot = index - 1312
                        if skins.get(slot) != value:
                            skins[slot] = value
                            skin_epochs.setdefault(slot, []).append(
                                (frame_index + 1, value))
                    elif index == 33:
                        match = re.fullmatch(r"maps/([A-Za-z0-9._-]+)\.bsp", value)
                        if match:
                            map_name = match.group(1)
                elif service == 14:
                    bits, number = demo_wire.parse_entity_bits(reader)
                    state = entities.setdefault(number, [0.0, 0.0, 0.0, 0])
                    _parse_demo_entity(reader, bits, state,
                                       zero_baseline=False)
                elif service == 20:
                    if serverrecord:
                        wire_frames.append(reader.s32())
                        if reader.u8() != 18:
                            raise ValueError(
                                "serverrecord frame lacks packetentities")
                        entities.clear()
                        packet_entities(reader, zero_baseline=True)
                        snapshot()
                    else:
                        reader.skip(9)
                        area_bytes = reader.u8()
                        reader.skip(area_bytes)
                elif service == 17:
                    parse_playerstate_full(reader, {})
                elif service == 18:
                    packet_entities(reader, zero_baseline=False)
                    snapshot()
                elif service == 9:
                    demo_wire.parse_sound(reader)
                elif service == 3:
                    demo_wire.parse_temp_entity(reader)
                elif service in (1, 2):
                    reader.skip(3)
                elif service == 10:
                    reader.skip(1)
                    reader.str_()
                elif service in (11, 15, 4):
                    reader.str_()
                elif service == 5:
                    reader.skip(512)
                elif service in (6, 7):
                    pass
                else:
                    raise ValueError(f"unknown demo service {service}")
            if reader.pos() != len(reader.b):
                raise ValueError("demo message was not consumed exactly")
        except Exception as error:
            raise ValueError(
                f"malformed demo message ending at byte {offset}") from error
    if messages == 0 or serverrecord is None or frame_index == 0:
        raise ValueError("strict demo lacks serverdata or frames")
    if not serverrecord:
        raise ValueError("Stage-A demo is not a serverrecord")
    if (len(wire_frames) != frame_index or any(
            right != left + 1 for left, right in
            zip(wire_frames, wire_frames[1:]))):
        raise ValueError("serverrecord wire frames are not consecutive")
    return {"map": map_name, "skins": skins,
            "skin_epochs": skin_epochs, "tracks": tracks,
            "frames": frame_index,
            "wire_framenums": wire_frames, "svrecord": True,
            # yquake2 serverrecord commonly ends at an exact message boundary
            # without writing the client-demo -1 marker when `serverrecord`
            # is stopped.  Clean EOF is complete; a partial message was
            # rejected above and the termination fact remains explicit.
            "parse_complete": True, "terminated": terminated}


def _validate_round_shape(round_receipt, contract, treatment_identity):
    schema = contract["receipt_schema"]
    _exact_object(round_receipt, schema["round_fields"], "round receipt")
    name = _safe_id(round_receipt["name"], "round receipt.name")
    if round_receipt["metric_version"] != METRIC_VERSION:
        raise ValueError(f"{name} has wrong literal metric version")
    arm = round_receipt["arm"]
    treatment = round_receipt["treatment"]
    if arm not in ("baseline", "candidate") or treatment != arm:
        raise ValueError(f"{name} has invalid arm/treatment identity")
    round_number = _integer(round_receipt["round"], f"{name}.round", minimum=1)
    if round_number not in (1, 2):
        raise ValueError(f"{name} has invalid round identity")
    source = _validate_source_identity(round_receipt["source_identity"],
                                       f"{name}.source_identity")
    if source != treatment_identity:
        raise ValueError(f"{name} source identity differs from treatment")
    port = _integer(round_receipt["port"], f"{name}.port", minimum=1)
    if port > 65535:
        raise ValueError(f"{name}.port exceeds TCP/UDP range")
    map_name = _safe_id(round_receipt["map"], f"{name}.map")
    assignment = round_receipt["roster_and_team_assignment"]
    if (not isinstance(assignment, Mapping) or len(assignment) != 10 or
            any(not isinstance(player, str) or not player
                for player in assignment) or
            Counter(assignment.values()) != Counter({"red": 5, "blue": 5})):
        raise ValueError(f"{name} roster assignment is not exact unique 5v5")
    window = _exact_object(round_receipt["evaluation_window_server_seconds"],
                           ("start", "end"), f"{name}.evaluation_window")
    window_pair = (_finite_number(window["start"], f"{name}.window.start"),
                   _finite_number(window["end"], f"{name}.window.end"))
    if window_pair[1] - window_pair[0] != WINDOW_SECONDS:
        raise ValueError(f"{name} does not have exact 600-second window")
    active = _exact_object(round_receipt["active_duration_seconds"],
                           ("per_bot", "red_team", "blue_team"),
                           f"{name}.active_duration_seconds")
    if any(_finite_number(active[field], f"{name}.active.{field}") !=
           WINDOW_SECONDS for field in active):
        raise ValueError(f"{name} active duration is not exactly 600 seconds")
    policy = contract["match_identity"]["recording_policy_literal"]
    if round_receipt["recording_policy"] != policy:
        raise ValueError(f"{name} recording policy is not frozen literal")
    artifacts = round_receipt["artifacts"]
    _exact_object(artifacts, REQUIRED_ARTIFACTS, f"{name}.artifacts")
    seen_paths = set()
    for artifact_name in REQUIRED_ARTIFACTS:
        path, _digest = _artifact_record(
            artifacts[artifact_name], f"{name}.artifacts.{artifact_name}")
        if path in seen_paths:
            raise ValueError(f"{name} reuses an artifact path")
        seen_paths.add(path)
    configurations = _sequence(round_receipt["configuration_artifacts"],
                               f"{name}.configuration_artifacts")
    if not configurations:
        raise ValueError(f"{name} has no configuration artifacts")
    for index, record in enumerate(configurations):
        path, _digest = _artifact_record(
            record, f"{name}.configuration_artifacts[{index}]")
        if path in seen_paths:
            raise ValueError(f"{name} reuses a configuration path")
        seen_paths.add(path)
    _sha256(round_receipt["configuration_sha256"],
            f"{name}.configuration_sha256")
    rune_identity = _exact_object(
        round_receipt["rune_identity"], RUNE_IDENTITY_FIELDS,
        f"{name}.rune_identity")
    if rune_identity["map"] != map_name:
        raise ValueError(f"{name} RUNE identity map differs from receipt")
    for field in ("gravity", "airaccelerate", "maxvelocity"):
        _finite_number(rune_identity[field], f"{name}.rune_identity.{field}")
    for field in set(RUNE_IDENTITY_FIELDS) - {
            "map", "gravity", "airaccelerate", "maxvelocity"}:
        value = _integer(rune_identity[field],
                         f"{name}.rune_identity.{field}", minimum=0)
        if value > 0xffffffff:
            raise ValueError(f"{name}.rune_identity.{field} exceeds uint32")
    return {"name": name, "arm": arm, "round": round_number,
            "treatment": treatment, "source_identity": source,
            "port": port, "map": map_name, "window": window_pair}


def _load_round(round_receipt, contract, treatment_identity,
                treatment_authority, implementation):
    identity = _validate_round_shape(round_receipt, contract,
                                     treatment_identity)
    name = identity["name"]
    root_text = round_receipt["root"]
    if not isinstance(root_text, str):
        raise ValueError(f"{name}.root must be a string")
    root = RetainedRoot(root_text)
    try:
        artifacts = round_receipt["artifacts"]
        _exact_object(artifacts, REQUIRED_ARTIFACTS, f"{name}.artifacts")
        payloads = {}
        artifact_records = {}
        paths = set()
        for artifact_name in REQUIRED_ARTIFACTS:
            path, expected = _artifact_record(
                artifacts[artifact_name], f"{name}.artifacts.{artifact_name}")
            if path in paths:
                raise ValueError(f"{name} reuses an artifact path")
            paths.add(path)
            payload = root.read(path, _artifact_limit(artifact_name),
                                f"{name} {artifact_name}")
            if sha256_bytes(payload) != expected:
                raise ValueError(f"{name} {artifact_name} SHA-256 mismatch")
            payloads[artifact_name] = payload
            artifact_records[artifact_name] = (path, expected)
        config_values = _sequence(round_receipt["configuration_artifacts"],
                                  f"{name}.configuration_artifacts")
        if not config_values:
            raise ValueError(f"{name} has no configuration artifacts")
        configurations = []
        configuration_records = []
        for index, record in enumerate(config_values):
            path, expected = _artifact_record(
                record, f"{name}.configuration_artifacts[{index}]")
            if path in paths:
                raise ValueError(f"{name} reuses a configuration path")
            paths.add(path)
            payload = root.read(path, MAX_TEXT_BYTES,
                                f"{name} configuration {index}")
            if sha256_bytes(payload) != expected:
                raise ValueError(f"{name} configuration SHA-256 mismatch")
            configurations.append((path, payload))
            configuration_records.append((path, expected))
        configurations.sort(key=lambda item: item[0].encode("utf-8"))
        if _configuration_digest(configurations) != round_receipt[
                "configuration_sha256"]:
            raise ValueError(f"{name} configuration aggregate mismatch")
        if sha256_bytes(payloads["module"]) != treatment_identity[
                "module_sha256"]:
            raise ValueError(f"{name} module differs from source identity")

        try:
            import runeio
        except ModuleNotFoundError:
            from tools import runeio
        _require_bound_helper(runeio, "tools/runeio.py", implementation)
        _require_bound_helper(
            runeio.contract, "tools/rune_contracts_generated.py",
            implementation)
        rune = runeio.decode(payloads["rune"])
        derived_rune_identity = _rune_identity(rune)
        if derived_rune_identity != round_receipt["rune_identity"]:
            raise ValueError(f"{name} complete RUNE identity mismatch")
        if rune.header.map_name != identity["map"]:
            raise ValueError(f"{name} RUNE map differs from receipt")
        stands = flag_stands_from_bsp(payloads["bsp"], implementation)
        stdlog = parse_stdlog(payloads["stdlog"])
        roster_info = validate_continuous_roster(
            stdlog, round_receipt["roster_and_team_assignment"])
        if roster_info["window"] != identity["window"]:
            raise ValueError(f"{name} derived roster window differs from receipt")
        host = reconcile_host_outcomes(payloads["server_log"], stdlog,
                                       roster_info["teams"])
        stats = parse_stats_database(payloads["stats_database"],
                                     roster_info["teams"])
        reconcile_stats(stdlog, stats, roster_info["roster"])
        telemetry = diagnostic_metrics(
            host, stdlog, roster_info, rune, contract)
        demo = _decode_serverrecord(
            payloads["serverrecord"], implementation)
        demo_metrics = analyze_demo(
            demo, roster_info, stands, stdlog, identity["map"], contract)
        counts = _window_counts(stdlog, roster_info)
        # Close the derive-to-publish interval: every retained authority is
        # reopened through the same directory fd, required to retain its
        # inode/timestamps, and rehashed after all metric derivation.
        for artifact_name, (path, expected) in artifact_records.items():
            current = root.read(path, _artifact_limit(artifact_name),
                                f"{name} final {artifact_name}")
            if sha256_bytes(current) != expected:
                raise ValueError(f"{name} {artifact_name} changed after analysis")
        final_configurations = []
        for index, (path, expected) in enumerate(configuration_records):
            current = root.read(path, MAX_TEXT_BYTES,
                                f"{name} final configuration {index}")
            if sha256_bytes(current) != expected:
                raise ValueError(f"{name} configuration changed after analysis")
            final_configurations.append((path, current))
        final_configurations.sort(key=lambda item: item[0].encode("utf-8"))
        if _configuration_digest(final_configurations) != round_receipt[
                "configuration_sha256"]:
            raise ValueError(f"{name} configuration aggregate changed")
        root.assert_current()
        return {
            "name": name, "arm": identity["arm"],
            "round": identity["round"], "port": identity["port"],
            "root_identity": list(root.identity), "map": identity["map"],
            "source_identity": identity["source_identity"],
            "roster_and_team_assignment": dict(
                round_receipt["roster_and_team_assignment"]),
            "artifact_sha256": {
                artifact_name: round_receipt["artifacts"][artifact_name]["sha256"]
                for artifact_name in REQUIRED_ARTIFACTS
            },
            "configuration_sha256": round_receipt["configuration_sha256"],
            "rune_identity": derived_rune_identity,
            "stand_origins": {color: list(origin)
                              for color, origin in sorted(stands.items())},
            "evaluation_window_server_seconds": {
                "start": identity["window"][0], "end": identity["window"][1]},
            "window_counts": counts, "telemetry": telemetry,
            "database_full_stream": stats, "demo": demo_metrics,
            "authority": {
                "ordered_host_outcomes": len(host["outcomes"]),
                "full_std_pickups": sum(event["kind"] == "F Pickup"
                                        for event in stdlog["flag_events"]),
                "full_std_captures": sum(event["kind"] == "F Capture"
                                         for event in stdlog["flag_events"]),
                "full_stats_pickups": stats["total"]["pickups"],
                "full_stats_captures": stats["total"]["captures"],
                "reconciliation_mismatches": 0,
                "forbidden_knowledge_events": (
                    treatment_authority["policy_probe"]["failures"] +
                    treatment_authority["policy_probe"]["errors"]),
            },
        }
    finally:
        root.close()


def _preflight_round_files(receipts):
    """Reject missing, aliased, or hash-drifted round inputs before builds."""
    guards = []
    try:
        for receipt in receipts:
            root = RetainedRoot(receipt["root"])
            guards.append((root, receipt))
        if len({root.identity for root, _receipt in guards}) != len(guards):
            raise ValueError("manifest retained roots alias the same directory")
        for root, receipt in guards:
            name = receipt["name"]
            paths = set()
            for artifact_name in REQUIRED_ARTIFACTS:
                path, expected = _artifact_record(
                    receipt["artifacts"][artifact_name],
                    f"{name}.artifacts.{artifact_name}")
                if path in paths:
                    raise ValueError(f"{name} reuses an artifact path")
                paths.add(path)
                payload = root.read(path, _artifact_limit(artifact_name),
                                    f"{name} preflight {artifact_name}")
                if sha256_bytes(payload) != expected:
                    raise ValueError(f"{name} {artifact_name} SHA-256 mismatch")
            configurations = []
            for index, record in enumerate(receipt["configuration_artifacts"]):
                path, expected = _artifact_record(
                    record, f"{name}.configuration_artifacts[{index}]")
                if path in paths:
                    raise ValueError(f"{name} reuses a configuration path")
                paths.add(path)
                payload = root.read(path, MAX_TEXT_BYTES,
                                    f"{name} preflight configuration {index}")
                if sha256_bytes(payload) != expected:
                    raise ValueError(f"{name} configuration SHA-256 mismatch")
                configurations.append((path, payload))
            if _configuration_digest(configurations) != receipt[
                    "configuration_sha256"]:
                raise ValueError(f"{name} configuration aggregate mismatch")
        for root, _receipt in guards:
            root.assert_files_current()
            root.assert_current()
    finally:
        for root, _receipt in guards:
            root.close()


def _revalidate_manifest_files(receipts, metrics, treatments=None,
                               treatment_authority=None):
    """Hold every trial/source root while rehashing retained authority."""
    metric_by_name = {item["name"]: item for item in metrics}
    guards = []
    source_guards = []
    try:
        if treatments is not None or treatment_authority is not None:
            if treatments is None or treatment_authority is None:
                raise ValueError("treatment revalidation inputs are incomplete")
            for arm in ("baseline", "candidate"):
                root = RetainedRoot(treatments[arm]["source_root"])
                authority = treatment_authority[arm]
                if tuple(authority["source_root_identity"]) != root.identity:
                    raise ValueError(
                        f"{arm} retained source root changed after derivation")
                source_guards.append((root, arm, treatments[arm], authority))
        for receipt in receipts:
            name = receipt["name"]
            metric = metric_by_name.get(name)
            if metric is None:
                raise ValueError(f"missing derived metrics for {name}")
            root = RetainedRoot(receipt["root"])
            guards.append((root, receipt, metric))
            if tuple(metric["root_identity"]) != root.identity:
                raise ValueError(f"{name} retained root changed after derivation")
        all_identities = [root.identity for root, _receipt, _metric in guards]
        all_identities.extend(root.identity for root, _arm, _value, _authority
                              in source_guards)
        if len(set(all_identities)) != len(all_identities):
            raise ValueError("retained trial/source roots alias")
        for root, arm, treatment, authority in source_guards:
            for artifact_name in SOURCE_ARTIFACTS:
                path, expected = _artifact_record(
                    treatment["source_artifacts"][artifact_name],
                    f"{arm}.source_artifacts.{artifact_name}")
                payload = root.read(
                    path, MAX_TEXT_BYTES,
                    f"{arm} publication source {artifact_name}",
                    allow_empty=artifact_name == "source_patch")
                if sha256_bytes(payload) != expected or expected != \
                        authority["source_artifact_sha256"][artifact_name]:
                    raise ValueError(
                        f"{arm} source {artifact_name} changed before publication")
        for root, receipt, _metric in guards:
            name = receipt["name"]
            for artifact_name in REQUIRED_ARTIFACTS:
                path, expected = _artifact_record(
                    receipt["artifacts"][artifact_name],
                    f"{name}.artifacts.{artifact_name}")
                payload = root.read(path, _artifact_limit(artifact_name),
                                    f"{name} publication {artifact_name}")
                if sha256_bytes(payload) != expected:
                    raise ValueError(
                        f"{name} {artifact_name} changed before publication")
            configurations = []
            for index, record in enumerate(receipt["configuration_artifacts"]):
                path, expected = _artifact_record(
                    record, f"{name}.configuration_artifacts[{index}]")
                payload = root.read(path, MAX_TEXT_BYTES,
                                    f"{name} publication configuration {index}")
                if sha256_bytes(payload) != expected:
                    raise ValueError(
                        f"{name} configuration changed before publication")
                configurations.append((path, payload))
            configurations.sort(key=lambda item: item[0].encode("utf-8"))
            if _configuration_digest(configurations) != receipt[
                    "configuration_sha256"]:
                raise ValueError(
                    f"{name} configuration aggregate changed before publication")
        for root, _receipt, _metric in guards:
            root.assert_files_current()
            root.assert_current()
        for root, _arm, _treatment, _authority in source_guards:
            root.assert_files_current()
            root.assert_current()
    finally:
        for root, _receipt, _metric in guards:
            root.close()
        for root, _arm, _treatment, _authority in source_guards:
            root.close()


def validate_manifest(contract_payload, manifest_payload, source_repository):
    contract = load_contract(contract_payload)
    manifest = _strict_json(manifest_payload, "Stage-A receipt manifest")
    schema = contract["receipt_schema"]
    _exact_object(manifest, schema["top_level_fields"],
                  "Stage-A receipt manifest")
    if manifest["format"] != RECEIPT_FORMAT:
        raise ValueError("receipt manifest format is not current")
    if manifest["metric_version"] != METRIC_VERSION:
        raise ValueError("receipt manifest has wrong literal metric version")
    contract_digest = sha256_bytes(contract_payload)
    if _sha256(manifest["metric_contract_sha256"],
               "manifest.metric_contract_sha256") != contract_digest:
        raise ValueError("receipt manifest metric-contract hash mismatch")
    measurement_tool_payload = _read_cli_file(
        Path(__file__), MAX_TEXT_BYTES, "measurement tool")
    tool_digest = sha256_bytes(measurement_tool_payload)
    if _sha256(manifest["measurement_tool_sha256"],
               "manifest.measurement_tool_sha256") != tool_digest:
        raise ValueError("receipt manifest measurement-tool hash mismatch")
    implementation, implementation_digest = measurement_implementation()
    if _sha256(
            manifest["measurement_implementation_sha256"],
            "manifest.measurement_implementation_sha256") != \
            implementation_digest:
        raise ValueError(
            "receipt manifest measurement-implementation hash mismatch")
    source_parent_commit = _commit(
        manifest["source_parent_commit"], "manifest.source_parent_commit")

    treatments = manifest["treatments"]
    _exact_object(treatments, ("baseline", "candidate"),
                  "manifest.treatments")
    source_repository = Path(source_repository)
    if not source_repository.is_absolute():
        raise ValueError("source repository must be an absolute path")
    treatment_ids = {
        arm: _validate_treatment_shape(
            treatments[arm], f"manifest.treatments.{arm}")[0]
        for arm in ("baseline", "candidate")}
    baseline = treatment_ids["baseline"]
    if (baseline["source_commit"] != source_parent_commit or
            treatment_ids["candidate"]["source_commit"] !=
            source_parent_commit):
        raise ValueError("treatment commits differ from the matched source parent")
    if (baseline["source_patch_sha256"] != EMPTY_PATCH_SHA256 or
            treatment_ids["candidate"]["source_patch_sha256"] ==
            EMPTY_PATCH_SHA256):
        raise ValueError("baseline must have an empty patch and candidate a delta")
    if treatment_ids["candidate"] == baseline:
        raise ValueError("candidate treatment source identity equals baseline")
    if treatment_ids["baseline"]["module_sha256"] == \
            treatment_ids["candidate"]["module_sha256"]:
        raise ValueError("baseline and candidate module identities are equal")
    source_root_names = [treatments[arm]["source_root"]
                         for arm in ("baseline", "candidate")]
    if len(set(source_root_names)) != 2:
        raise ValueError("treatment source roots must be distinct")
    receipts = _sequence(manifest["rounds"], "manifest.rounds")
    expected_pairs = {(round_number, arm) for round_number in (1, 2)
                      for arm in ("baseline", "candidate")}
    preflight = []
    for receipt in receipts:
        if not isinstance(receipt, Mapping):
            raise ValueError("manifest round must be an object")
        arm = receipt.get("arm")
        if arm not in treatment_ids:
            raise ValueError("manifest round has unknown arm")
        preflight.append(_validate_round_shape(
            receipt, contract, treatment_ids[arm]))
    if (len(preflight) != 4 or
            {(item["round"], item["arm"]) for item in preflight} !=
            expected_pairs):
        raise ValueError("manifest must contain exactly the paired 2x2 design")
    if len({item["name"] for item in preflight}) != 4:
        raise ValueError("manifest round names must be unique")
    if len({item["port"] for item in preflight}) != 4:
        raise ValueError("manifest ports must be distinct")
    root_names = [receipt["root"] for receipt in receipts]
    if len(set(root_names)) != 4:
        raise ValueError("manifest retained roots must be distinct")
    if set(root_names) & set(source_root_names):
        raise ValueError("round and treatment retained roots must be distinct")

    receipt_by_pair = {(receipt["round"], receipt["arm"]): receipt
                       for receipt in receipts}
    for round_number in (1, 2):
        baseline_receipt = receipt_by_pair[(round_number, "baseline")]
        candidate_receipt = receipt_by_pair[(round_number, "candidate")]
        if baseline_receipt["roster_and_team_assignment"] != \
                candidate_receipt["roster_and_team_assignment"]:
            raise ValueError(f"round {round_number} arm team assignments differ")
    first_assignment = receipt_by_pair[(1, "baseline")][
        "roster_and_team_assignment"]
    second_assignment = receipt_by_pair[(2, "baseline")][
        "roster_and_team_assignment"]
    if set(first_assignment) != set(second_assignment) or any(
            first_assignment[name] == second_assignment[name]
            for name in first_assignment):
        raise ValueError("round two is not the exact arm-swapped team assignment")
    _preflight_round_files(receipts)
    baseline_authority = _load_treatment(
        treatments["baseline"], "baseline", source_repository, tool_digest,
        implementation_digest)
    candidate_authority = _load_treatment(
        treatments["candidate"], "candidate", source_repository, tool_digest,
        implementation_digest,
        baseline_authority["build_input_sha256"])
    treatment_authority = {
        "baseline": baseline_authority, "candidate": candidate_authority}
    if len({tuple(treatment_authority[arm]["source_root_identity"])
            for arm in ("baseline", "candidate")}) != 2:
        raise ValueError("treatment source roots alias the same directory")
    metrics = [_load_round(
        receipt, contract, treatment_ids[receipt["arm"]],
        treatment_authority[receipt["arm"]], implementation)
        for receipt in receipts]
    if len({tuple(item["root_identity"]) for item in metrics}) != 4:
        raise ValueError("manifest retained roots alias the same directory")
    common_fields = ("map", "configuration_sha256", "rune_identity",
                     "stand_origins")
    first = metrics[0]
    for item in metrics[1:]:
        for field in common_fields:
            if item[field] != first[field]:
                raise ValueError(f"rounds differ in frozen {field}")
        for artifact in ("engine", "rune", "bsp", "recording_harness"):
            if item["artifact_sha256"][artifact] != \
                    first["artifact_sha256"][artifact]:
                raise ValueError(f"rounds differ in frozen {artifact} artifact")
    by_pair = {(item["round"], item["arm"]): item for item in metrics}
    for arm in ("baseline", "candidate"):
        modules = {by_pair[(round_number, arm)]["artifact_sha256"]["module"]
                   for round_number in (1, 2)}
        if modules != {treatment_ids[arm]["module_sha256"]}:
            raise ValueError(f"{arm} module identity drifts across rounds")

    baseline = aggregate_rounds(metrics, "baseline")
    candidate = aggregate_rounds(metrics, "candidate")
    gate = evaluate_bands(baseline, candidate, contract)
    report = {"valid_receipts": True, "round_count": 4,
              "sufficient_events": gate["sufficient_events"],
              "all_bands_pass": gate["all_bands_pass"],
              "decision": gate["decision"], "checks": gate["checks"]}
    result = bind_result_hashes({
        "format": RESULT_FORMAT, "metric_version": METRIC_VERSION,
        "metric_contract_sha256": contract_digest,
        "measurement_tool_sha256": tool_digest,
        "measurement_implementation_sha256": implementation_digest,
        "measurement_implementation": implementation,
        "manifest_sha256": sha256_bytes(manifest_payload),
        "source_parent_commit": source_parent_commit,
        "treatment_authority": treatment_authority,
        "round_metrics": metrics,
        "aggregate": {"baseline": baseline, "candidate": candidate},
        "report": report,
    })
    validate_result_hashes(result)
    _revalidate_manifest_files(
        receipts, metrics, treatments, treatment_authority)
    validate_result_hashes(result)
    return result


def _read_cli_file(path, maximum, label):
    path = Path(os.path.abspath(os.fspath(path)))
    with RetainedRoot(path.parent) as root:
        payload = root.read(path.name, maximum, label)
        root.assert_current()
        return payload


def _write_exclusive(path, payload):
    destination = Path(os.path.abspath(os.fspath(path)))
    if destination.name in ("", ".", ".."):
        raise ValueError("output path must name a file")
    parent_fd = _open_directory_nofollow(destination.parent)
    parent_stat = os.fstat(parent_fd)
    parent_identity = (parent_stat.st_dev, parent_stat.st_ino)
    temporary = f".{destination.name}.tmp-{secrets.token_hex(12)}"
    fd = None
    try:
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                     getattr(os, "O_NOFOLLOW", 0), 0o600, dir_fd=parent_fd)
        view = memoryview(payload)
        while view:
            count = os.write(fd, view)
            view = view[count:]
        os.fsync(fd)
        os.close(fd)
        fd = None
        # A hard-link publication is atomic and fails if the destination
        # already exists.  The temporary name is removed only after the final
        # name is durable in the retained, no-follow parent directory.
        os.link(temporary, destination.name, src_dir_fd=parent_fd,
                dst_dir_fd=parent_fd, follow_symlinks=False)
        os.unlink(temporary, dir_fd=parent_fd)
        os.fsync(parent_fd)
        published_fd = os.open(
            destination.name, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0),
            dir_fd=parent_fd)
        try:
            before = os.fstat(published_fd)
            if (not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or
                    before.st_size != len(payload)):
                raise ValueError("published output has invalid identity")
            observed = bytearray()
            while len(observed) < before.st_size:
                block = os.read(
                    published_fd, min(1024 * 1024,
                                      before.st_size - len(observed)))
                if not block:
                    raise ValueError("published output truncated")
                observed.extend(block)
            after = os.fstat(published_fd)
            named = os.stat(destination.name, dir_fd=parent_fd,
                            follow_symlinks=False)
            identity = lambda item: (
                item.st_dev, item.st_ino, item.st_size, item.st_mtime_ns,
                item.st_ctime_ns, item.st_nlink,
            )
            if (bytes(observed) != bytes(payload) or
                    identity(before) != identity(after) or
                    identity(after) != identity(named)):
                raise ValueError("published output changed during attestation")
        finally:
            os.close(published_fd)
        current_parent = _open_directory_nofollow(destination.parent)
        try:
            current_stat = os.fstat(current_parent)
            current_identity = (current_stat.st_dev, current_stat.st_ino)
            if current_identity != parent_identity:
                raise ValueError("output parent changed during publication")
        finally:
            os.close(current_parent)
    except Exception:
        if fd is not None:
            os.close(fd)
        try:
            os.unlink(temporary, dir_fd=parent_fd)
        except OSError:
            pass
        raise
    finally:
        os.close(parent_fd)


def _source_patch_payload(repository):
    """Return the frozen tracked patch, rejecting omitted untracked inputs."""
    untracked = subprocess.run(
        ("git", "-C", os.fspath(repository), "ls-files", "--others",
         "--exclude-standard", "-z"), check=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE).stdout
    if untracked:
        names = [item.decode("utf-8", errors="replace")
                 for item in untracked.rstrip(b"\0").split(b"\0")]
        raise ValueError(
            "source patch identity excludes untracked files: " +
            ", ".join(names[:10]))
    return subprocess.run(
        ("git", "-C", os.fspath(repository), "diff", "--binary",
         "--no-ext-diff", "--no-textconv", "--src-prefix=a/",
         "--dst-prefix=b/", "HEAD", "--"),
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def _source_tree_manifest(repository):
    """Return canonical tracked worktree bytes, including modes and symlinks."""
    repository = Path(repository)
    untracked = subprocess.run(
        ("git", "-C", os.fspath(repository), "ls-files", "--others",
         "--exclude-standard", "-z"), check=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE).stdout
    if untracked:
        raise ValueError("reconstructed source contains untracked inputs")
    staged = subprocess.run(
        ("git", "-C", os.fspath(repository), "ls-files", "--stage", "-z"),
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    raw_entries = staged.rstrip(b"\0").split(b"\0") if staged else ()
    tracked_modes = {}
    for raw in raw_entries:
        try:
            metadata, raw_path = raw.split(b"\t", 1)
            index_mode, _object_name, stage = metadata.split(b" ", 2)
            path = raw_path.decode("utf-8")
        except (UnicodeDecodeError, ValueError) as error:
            raise ValueError("source index entry is malformed or non-UTF-8") from error
        if stage != b"0":
            raise ValueError("source index contains an unmerged entry")
        normalized = _relative_artifact_path(path, "source path").as_posix()
        tracked_modes[normalized] = index_mode.decode("ascii")
    entries = []
    for raw in raw_entries:
        try:
            metadata, raw_path = raw.split(b"\t", 1)
            index_mode, _object_name, stage = metadata.split(b" ", 2)
            path = raw_path.decode("utf-8")
        except (UnicodeDecodeError, ValueError) as error:
            raise ValueError("source index entry is malformed or non-UTF-8") from error
        if stage != b"0":
            raise ValueError("source index contains an unmerged entry")
        normalized = _relative_artifact_path(path, "source path").as_posix()
        target = repository / normalized
        current = os.lstat(target)
        if stat.S_ISLNK(current.st_mode):
            mode = "120000"
            payload = os.readlink(os.fsencode(target))
            try:
                link_text = payload.decode("utf-8")
            except UnicodeDecodeError as error:
                raise ValueError(
                    f"source symlink target is non-UTF-8: {normalized}") from error
            link_path = PurePosixPath(link_text)
            if (link_path.is_absolute() or link_text != link_path.as_posix() or
                    any(part in ("", ".", "..") for part in link_path.parts)):
                raise ValueError(
                    f"source symlink target is not safe and normalized: {normalized}")
            resolved = (PurePosixPath(normalized).parent / link_path).as_posix()
            if tracked_modes.get(resolved) not in ("100644", "100755"):
                raise ValueError(
                    f"source symlink does not target a tracked regular file: "
                    f"{normalized}")
        elif stat.S_ISREG(current.st_mode):
            mode = "100755" if current.st_mode & stat.S_IXUSR else "100644"
            fd = os.open(target, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
            try:
                before = os.fstat(fd)
                payload = bytearray()
                while len(payload) < before.st_size:
                    block = os.read(fd, min(1024 * 1024,
                                            before.st_size - len(payload)))
                    if not block:
                        raise ValueError(f"source file truncated: {normalized}")
                    payload.extend(block)
                after = os.fstat(fd)
                identity = lambda value: (
                    value.st_dev, value.st_ino, value.st_size,
                    value.st_mtime_ns, value.st_ctime_ns,
                )
                if identity(before) != identity(after):
                    raise ValueError(f"source file changed while read: {normalized}")
                payload = bytes(payload)
            finally:
                os.close(fd)
        else:
            raise ValueError(f"source path has unsupported type: {normalized}")
        decoded_mode = index_mode.decode("ascii")
        if decoded_mode not in ("100644", "100755", "120000"):
            raise ValueError(f"source index mode is unsupported: {normalized}")
        if mode != decoded_mode:
            raise ValueError(f"source worktree mode differs from index: {normalized}")
        entries.append({
            "path": normalized, "mode": mode, "size": len(payload),
            "sha256": sha256_bytes(payload),
        })
    entries.sort(key=lambda item: item["path"].encode("utf-8"))
    return canonical_json({"format": SOURCE_TREE_FORMAT, "entries": entries})


@contextmanager
def _reconstructed_source(source_repository, commit, patch_payload):
    """Yield a disposable checkout of exactly ``commit + patch_payload``."""
    repository = Path(source_repository)
    _require_commit(repository, commit)
    with tempfile.TemporaryDirectory(prefix="steal-source-reconstruct-") as tmp:
        temporary = Path(tmp)
        checkout = temporary / "source"
        clone = subprocess.run(
            ("git", "clone", "-q", "--no-hardlinks", "--no-checkout", "--",
             os.fspath(repository), os.fspath(checkout)),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        if clone.returncode != 0:
            detail = clone.stderr.decode("utf-8", errors="replace")[-4000:]
            raise ValueError(f"cannot reconstruct source repository: {detail}")
        subprocess.run(
            ("git", "-C", os.fspath(checkout), "-c", "core.hooksPath=/dev/null",
             "checkout", "-q", "--detach", commit), check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if patch_payload:
            patch_path = temporary / "source.patch"
            patch_path.write_bytes(patch_payload)
            applied = subprocess.run(
                ("git", "-C", os.fspath(checkout),
                 "-c", "core.hooksPath=/dev/null", "apply", "--binary",
                 "--index", "--whitespace=nowarn", "--", os.fspath(patch_path)),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            if applied.returncode != 0:
                detail = applied.stderr.decode("utf-8", errors="replace")[-4000:]
                raise ValueError(f"retained source patch does not apply: {detail}")
        reconstructed_patch = _source_patch_payload(checkout)
        if reconstructed_patch != patch_payload:
            raise ValueError("reconstructed source patch bytes are not canonical")
        yield checkout


def _revision_header_payload(repository):
    template = _read_cli_file(
        Path(repository) / "GitRevisionInfo.tmpl", MAX_TEXT_BYTES,
        "Git revision template")
    return (template.replace(b"$", b"")
            .replace(b"WCLOGCOUNT+2", b"0")
            .replace(b"WCREV=7", b"stage-a-v1")
            .replace(b"WCNOW=%Y", SOURCE_BUILD_YEAR.encode("ascii")))


def _rebuild_module(repository, expected_build_inputs=None,
                    expected_source_manifest=None):
    """Execute the single frozen source-to-module recipe in a fresh checkout."""
    make = shutil.which("make")
    compiler = shutil.which("gcc")
    if make is None or compiler is None:
        raise ValueError("frozen source build requires make and gcc")
    build_inputs = {}
    for relative in SOURCE_BUILD_INPUTS:
        payload = _read_cli_file(Path(repository) / relative, MAX_TEXT_BYTES,
                                 f"frozen build input {relative}")
        build_inputs[relative] = sha256_bytes(payload)
    if (expected_build_inputs is not None and
            dict(expected_build_inputs) != build_inputs):
        raise ValueError("candidate changed matched-parent build authority")
    source_before = _source_tree_manifest(repository)
    if (expected_source_manifest is not None and
            source_before != bytes(expected_source_manifest)):
        raise ValueError("source tree changed before frozen module build")
    target = "stage-a-game.so"
    for generated in ("GitRevisionInfo.h", target):
        try:
            os.lstat(Path(repository) / generated)
        except FileNotFoundError:
            pass
        else:
            raise ValueError(
                f"frozen build output collides with source authority: {generated}")
    header = _revision_header_payload(repository)
    (Path(repository) / "GitRevisionInfo.h").write_bytes(header)
    environment = dict(os.environ)
    environment.update({"LC_ALL": "C", "TZ": "UTC",
                        "SOURCE_DATE_EPOCH": "0"})
    process = subprocess.run(
        (make, "-j4", "-f", "GNUmakefile", "REV=0", "VER=stage-a-v1",
         f"CC={compiler} -std=c11",
         "CFLAGS=-std=c11 -O3 -DARCH=\"$(ARCH)\" -DSTDC_HEADERS "
         "-DVER='\"$(VER)\"' -Wall -DLINUX",
         "REVISION_HEADER=", f"TARGET={target}",
         target), cwd=repository, env=environment, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False)
    if process.returncode != 0:
        detail = (process.stdout + process.stderr).decode(
            "utf-8", errors="replace")[-8000:]
        raise ValueError(f"frozen source build failed: {detail}")
    module = _read_cli_file(Path(repository) / target, MAX_BINARY_BYTES,
                            "rebuilt module")
    source_after = _source_tree_manifest(repository)
    if source_after != source_before:
        raise ValueError("frozen module build mutated tracked source authority")
    return module, header, build_inputs


def _knowledge_report(source_identity, measurement_tool_sha256,
                      measurement_implementation_sha256, probe):
    return {
        "format": KNOWLEDGE_REPORT_FORMAT,
        "metric_version": METRIC_VERSION,
        **dict(source_identity),
        "measurement_tool_sha256": measurement_tool_sha256,
        "measurement_implementation_sha256":
            measurement_implementation_sha256,
        "policy_probe_sha256": POLICY_PROBE_SHA256,
        **dict(probe),
    }


def generate_source_authority(source_repository, module_payload,
                              measurement_tool_payload):
    """Generate every retained per-arm authority from the current Git tree."""
    repository = Path(source_repository)
    if not repository.is_absolute():
        raise ValueError("source repository must be an absolute path")
    commit = subprocess.run(
        ("git", "-C", os.fspath(repository), "rev-parse", "--verify",
         "HEAD^{commit}"), check=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE).stdout.decode("ascii").strip()
    _commit(commit, "source authority commit")
    patch = _source_patch_payload(repository)
    with _reconstructed_source(repository, commit, patch) as reconstructed:
        source_manifest = _source_tree_manifest(reconstructed)
        probe = _run_knowledge_probe(reconstructed)
        rebuilt, header, build_inputs = _rebuild_module(
            reconstructed, expected_source_manifest=source_manifest)
    if rebuilt != bytes(module_payload):
        raise ValueError("supplied module is not the frozen reconstructed build")
    identity = {
        "source_commit": commit,
        "source_patch_sha256": sha256_bytes(patch),
        "source_tree_sha256": sha256_bytes(source_manifest),
        "module_sha256": sha256_bytes(rebuilt),
    }
    build_receipt = {
        "format": BUILD_RECEIPT_FORMAT, "metric_version": METRIC_VERSION,
        "recipe": SOURCE_BUILD_RECIPE, **identity,
        "build_input_sha256": build_inputs,
        "revision_header_sha256": sha256_bytes(header),
    }
    _implementation, implementation_digest = measurement_implementation()
    report = _knowledge_report(
        identity, sha256_bytes(measurement_tool_payload),
        implementation_digest, probe)
    return {"identity": identity, "source_patch": patch,
            "source_manifest": source_manifest,
            "build_receipt": canonical_json(build_receipt),
            "knowledge_report": canonical_json(report)}


def verify_result(contract_payload, manifest_payload, result_payload,
                  source_repository):
    """Rerun all retained authority and compare the canonical result exactly."""
    supplied = _strict_json(result_payload, "result")
    validate_result_hashes(supplied)
    expected = validate_manifest(
        contract_payload, manifest_payload, source_repository)
    if canonical_json(supplied) != canonical_json(expected):
        raise ValueError("result differs from complete retained-evidence evaluation")
    return supplied


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Validate and evaluate exact Stage-A steal receipts")
    subparsers = parser.add_subparsers(dest="command", required=True)
    evaluate = subparsers.add_parser("evaluate")
    evaluate.add_argument("--contract", required=True)
    evaluate.add_argument("--manifest", required=True)
    evaluate.add_argument("--source-repository", required=True)
    evaluate.add_argument("--output")
    verify = subparsers.add_parser("verify-result")
    verify.add_argument("--result", required=True)
    verify.add_argument("--contract", required=True)
    verify.add_argument("--manifest", required=True)
    verify.add_argument("--source-repository", required=True)
    knowledge = subparsers.add_parser("knowledge-report")
    knowledge.add_argument("--source-repository", required=True)
    knowledge.add_argument("--module", required=True)
    knowledge.add_argument("--output", required=True)
    knowledge.add_argument("--source-patch-output", required=True)
    knowledge.add_argument("--source-manifest-output", required=True)
    knowledge.add_argument("--build-receipt-output", required=True)
    args = parser.parse_args(argv)
    if args.command == "verify-result":
        result_payload = _read_cli_file(args.result, MAX_TEXT_BYTES, "result")
        contract_payload = _read_cli_file(
            args.contract, MAX_TEXT_BYTES, "metric contract")
        manifest_payload = _read_cli_file(
            args.manifest, MAX_TEXT_BYTES, "receipt manifest")
        verify_result(
            contract_payload, manifest_payload, result_payload,
            Path(args.source_repository).resolve())
        return 0
    if args.command == "knowledge-report":
        repository = Path(args.source_repository).resolve()
        module_payload = _read_cli_file(
            args.module, MAX_BINARY_BYTES, "policy-probed module")
        tool_payload = _read_cli_file(
            Path(__file__).resolve(), MAX_TEXT_BYTES, "measurement tool")
        authority = generate_source_authority(
            repository, module_payload, tool_payload)
        _write_exclusive(args.source_patch_output,
                         authority["source_patch"])
        _write_exclusive(args.source_manifest_output,
                         authority["source_manifest"])
        _write_exclusive(args.build_receipt_output,
                         authority["build_receipt"])
        _write_exclusive(args.output, authority["knowledge_report"])
        return 0
    contract_payload = _read_cli_file(
        args.contract, MAX_TEXT_BYTES, "metric contract")
    manifest_payload = _read_cli_file(
        args.manifest, MAX_TEXT_BYTES, "receipt manifest")
    result = validate_manifest(
        contract_payload, manifest_payload,
        Path(args.source_repository).resolve())
    output = json.dumps(result, indent=2, sort_keys=True,
                        allow_nan=False).encode("utf-8") + b"\n"
    if args.output:
        parsed_manifest = _strict_json(
            manifest_payload, "Stage-A receipt manifest")
        _revalidate_manifest_files(
            parsed_manifest["rounds"], result["round_metrics"],
            parsed_manifest["treatments"], result["treatment_authority"])
        _write_exclusive(args.output, output)
        _revalidate_manifest_files(
            parsed_manifest["rounds"], result["round_metrics"],
            parsed_manifest["treatments"], result["treatment_authority"])
    else:
        sys.stdout.buffer.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
