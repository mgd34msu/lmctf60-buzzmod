#!/usr/bin/env python3
"""stallcensus.py -- navigation stalls: bots pushing on geometry instead of
moving through it.

An eyewitness watching a live game reported bots getting physically stuck on
map objects while trying to cross them -- pressed against a ledge, jittering
in a doorway, hunting for a path that isn't there. Every other instrument in
this toolset reads outcomes (steals, carries, conduct.py's grind/spin) off
the film; none of them measures whether the underlying navigation is fluent.
This one does, directly from the same per-0.1s entity tracks film.py already
parses.

WHAT COUNTS AS A STALL: a window of >= STALL_WIN_S seconds where an entity's
net displacement stays under STALL_NET_UNITS while there is direct evidence
it kept trying to move:

  jitter    gross path length inside the window (sum of frame-to-frame step
            lengths) is >= STALL_GROSS_UNITS -- at least double the net
            ceiling, so real back-and-forth pushing against a surface is
            what trips this, not quantization noise on an entity that is
            simply standing still.
  yaw_turn  sustained yaw rate >= STALL_YAW_DPS averaged across the window
            while there is a translational push signal -- the body is turning,
            hunting for a way through, without the ceiling of conduct.py's spinbot-caliber
            SPIN_DPS (540): a stuck bot reorienting only needs to show
            visible, sustained turning, not a full spin.

A low-mobility window that shows NEITHER signal is ordinary standing still
(camping, holding a corridor) and is not counted as anything -- this
instrument only flags navigation failure, not stillness by itself.
Overlapping/adjacent triggering windows are merged into one stall episode so
the reported duration reflects how long the entity actually stayed stuck,
not the fixed probe length.

EXCLUDED:
  dead/respawn ticks   reuses conduct.py's contiguous_segments, which is
                        itself built on film.TELEPORT_UNITS: a respawn
                        teleport reads as a segment break, never as a giant
                        one-tick "displacement", and slivers shorter than
                        conduct.MIN_SEG_S are dropped outright.
  intentional posts    a low-mobility window whose midpoint lies within
                        POST_R of the entity's OWN team's flag stand (from
                        stands.json, same fixture conduct.py/tripcensus.py
                        use) is guarding, not stuck. It is never counted as
                        a stall; its time is tallied separately as
                        "held post time" so the two are never conflated. If
                        a window's stall evidence (jitter/yaw) and its post
                        proximity disagree with each other on the same
                        frame, stall wins -- a bot that is visibly fighting
                        geometry two steps from its own stand is still
                        stuck, not posted.

PER-DEMO OUTPUT:
  stalls_per_min       stall episodes / players_observed_min (coverage-
                        honest: same denominator convention as conduct.py --
                        a player-second counts only where that player has a
                        contiguous track sample that second).
  stall_dur_median_s   median stall episode duration.
  stall_time_frac      total stall seconds / total observed seconds.
  post_hold_frac       total held-post seconds / total observed seconds --
                        printed for contrast, not as a stall metric.
  top_stall_locations  up to 3 clusters (stall episode midpoints grouped
                        within CLUSTER_R in 3D) as {x, y, z,
                        evidence_count, duration_s},
                        sorted by count -- the map's snag spots. Map-
                        specific by construction, so --compare does not
                        pool this field (see below).
  stands_known         false if the demo's map has no stands.json fixture:
                        with no own-stand coordinate, post detection cannot
                        run and every qualifying low-mobility window on that
                        map is counted as a stall even if it was a post.
                        Read stall numbers for such demos as an upper bound.

COVERAGE HONESTY: human film is client POV -- entities exist in the track
dict only while inside the recorder's PVS, so a human demo's
players_observed_min is real observed time, not wall time; bot serverrecord
film sees everything, so its observed time ~= wall time. Rates here are
each population's own rate over its own denominator -- rank/ratio evidence
across populations, not calibrated absolutes, same caveat conduct.py states
for grind_spm/approach_pm.

--compare pools stalls_per_min, stall_dur_median_s (true median of the
pooled episode durations, not an average of per-demo medians),
stall_time_frac and post_hold_frac, weighted by players_observed_min exactly
like conduct.py's pool(). top_stall_locations is left out of the pooled
card: clustering stall midpoints from different maps together would produce
meaningless coordinates.

Usage:
  stallcensus.py <demo.dm2> [...]                     per-demo JSON lines
  stallcensus.py --compare --human <glob> --bot <glob>  pooled two-column card
"""

import argparse
import glob as globmod
import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import film
import conduct

DT = 0.1                     # server frame step, same as conduct.py/film.py

STALL_WIN_S = 1.0            # minimum stall window length
STALL_NET_UNITS = 24.0       # net displacement ceiling inside the window
STALL_GROSS_UNITS = 48.0     # gross path length floor for "jitter" -- double
                              # the net ceiling so genuine push-and-cancel
                              # motion trips it, not position quantization
STALL_YAW_DPS = 120.0        # sustained yaw-rate floor for "yaw_turn"
STALL_PUSH_UNITS = 8.0       # yaw alone is a deliberate scan, not a snag
POST_R = 200.0                # own-stand radius counted as holding a post
CLUSTER_R = 64.0              # stall-midpoint clustering radius

# Exact production SG report emitted by slipgate/sg_move.c.  Coordinates are
# useful visible-motion evidence, but the controller's seed is the only
# authoritative attribution of that body to the RUNE graph.
SG_REPORT_RE = re.compile(
    r'^SG (?P<name>\S+): role=(?P<role>\d+) seed=(?P<seed>-?\d+) '
    r'goal=(?P<goal>-?\d+) sgoal=(?P<sgoal>-?\d+) spd=(?P<speed>\d+) '
    r'org=\((?P<x>-?\d+) (?P<y>-?\d+) (?P<z>-?\d+)\) '
    r'link=(?P<link>-?\d+) act=(?P<action>-?\d+) '
    r'hp=(?P<hook_phase>\d+) dh=(?P<door_hold>\d+) '
    r'dl=(?P<drop_locked>\d+) '
    r'st=(?P<stuck>\d+\.\d) gnd=(?P<grounded>[01]) '
    r'eng=(?P<engaged>[01]) frm=(?P<frame>\d+)$'
)
SG_CENSUS_RE = re.compile(
    r'^SGCENSUS (?P<name>\S+): frm=(?P<frame>\d+) '
    r'alive=(?P<alive>[01])$'
)
ROUTE_STALL_THRESHOLD_S = 1.0
ROUTE_STALL_EVIDENCE_MAX = 1_000_000
ROUTE_STALL_DURATION_MS_MAX = 86_400_000
SG_ROLE_VALUES = frozenset(range(5))
# -1 is emitted when there is no selected link.  Disabled wire actions 7 and
# 9..11 cannot occur in an accepted, published graph.
SG_RUNTIME_ACTION_VALUES = frozenset((-1, 0, 1, 2, 3, 4, 5, 6, 8, 12))
SG_INT_MAX = 2_147_483_647


def route_stall_evidence(
        lines, expected_players, *, expected_frame_range=None, rune=None):
    """Reduce exact SG reports to RUNE-seed stall episodes.

    The visible demo establishes that a body was pushing/jittering.  It cannot
    choose between adjacent 64-unit lattice seeds.  The production controller
    already reports the exact seed it was driving, so this reducer uses only
    that value for routing policy.  ``st`` is cumulative within an episode.
    Only ordinary grounded, non-combat ``RL_RUN`` rows contribute.  An
    excluded row poisons its cumulative episode until an observed cumulative
    decrease; a nearest-seed change alone is not a production reset.
    """
    if (not isinstance(expected_players, (tuple, list, set)) or
            not expected_players or
            any(not isinstance(name, str) or not name
                for name in expected_players)):
        raise ValueError("expected_players must be a nonempty name set")
    expected = set(expected_players)
    if len(expected) != len(expected_players):
        raise ValueError("expected_players contains duplicates")
    if expected_frame_range is not None:
        if (not isinstance(expected_frame_range, (tuple, list)) or
                len(expected_frame_range) != 2 or
                any(type(value) is not int for value in expected_frame_range) or
                expected_frame_range[0] < 0 or
                expected_frame_range[1] <= expected_frame_range[0] or
                expected_frame_range[1] - expected_frame_range[0] >
                    ROUTE_STALL_DURATION_MS_MAX // 100):
            raise ValueError(
                "expected_frame_range must be a bounded half-open frame pair")
        if (rune is None or not hasattr(rune, "seeds") or
                not hasattr(rune, "links")):
            raise ValueError(
                "authenticated SG route evidence requires exact RUNE")

    seen = set()
    # player -> (seed, last cumulative milliseconds, poisoned, counted_active)
    previous = {}
    totals = {}
    player_report_counts = {name: 0 for name in expected}
    player_report_frames = {name: set() for name in expected}
    player_last_report_frame = {name: None for name in expected}
    census_frames = {name: [] for name in expected}
    census_alive = {name: 0 for name in expected}
    active_episode = {name: None for name in expected}
    episodes = []
    observations = []
    report_count = 0
    for line_number, raw_line in enumerate(lines, 1):
        line = raw_line.rstrip("\n")
        census = SG_CENSUS_RE.fullmatch(line)
        if census is not None:
            name = census.group("name")
            if name not in expected:
                raise ValueError(f"unexpected SG census player {name}")
            seen.add(name)
            frame = int(census.group("frame"))
            if frame > SG_INT_MAX:
                raise ValueError(f"SG census frame exceeds C int at line {line_number}")
            census_frames[name].append(frame)
            census_alive[name] += int(census.group("alive"))
            continue
        if line.startswith("SGCENSUS ") and any(
                line.startswith(f"SGCENSUS {name}:") for name in expected):
            raise ValueError(
                f"malformed SG census for admitted player at line {line_number}")
        match = SG_REPORT_RE.fullmatch(line)
        if match is None:
            if line.startswith("SG ") and any(
                    line.startswith(f"SG {name}:") for name in expected):
                raise ValueError(
                    f"malformed SG report for admitted player at line {line_number}")
            continue
        name = match.group("name")
        if name not in expected:
            raise ValueError(f"unexpected SG telemetry player {name}")
        seen.add(name)
        report_count += 1
        player_report_counts[name] += 1
        fields = {
            key: int(match.group(key))
            for key in (
                "role", "seed", "goal", "sgoal", "speed", "x", "y", "z",
                "link", "action", "hook_phase", "door_hold", "drop_locked",
                "grounded", "engaged", "frame",
            )
        }
        if fields["role"] not in SG_ROLE_VALUES:
            raise ValueError(f"invalid SG role at line {line_number}")
        if fields["action"] not in SG_RUNTIME_ACTION_VALUES:
            raise ValueError(f"invalid SG action at line {line_number}")
        if fields["seed"] < -1 or fields["goal"] < -1 or fields["sgoal"] < -1:
            raise ValueError(f"invalid SG route index at line {line_number}")
        if fields["link"] < -1:
            raise ValueError(f"invalid SG link at line {line_number}")
        if (fields["hook_phase"] not in range(4) or
                fields["door_hold"] not in range(4) or
                fields["drop_locked"] not in (0, 1)):
            raise ValueError(f"invalid SG state at line {line_number}")
        if any(abs(fields[key]) > SG_INT_MAX
               for key in ("seed", "goal", "sgoal", "speed", "x", "y", "z",
                           "link", "frame")):
            raise ValueError(f"SG numeric field exceeds C int at line {line_number}")
        if expected_frame_range is not None and not (
                expected_frame_range[0] < fields["frame"] <=
                expected_frame_range[1]):
            raise ValueError(f"SG route frame outside residence at line {line_number}")
        if (expected_frame_range is not None and
                fields["frame"] in player_report_frames[name]):
            raise ValueError(
                f"duplicate SG route frame for {name} at line {line_number}")
        if (player_last_report_frame[name] is not None and
                fields["frame"] <= player_last_report_frame[name]):
            raise ValueError(
                f"nonmonotonic SG route frame for {name} at line {line_number}")
        player_report_frames[name].add(fields["frame"])
        player_last_report_frame[name] = fields["frame"]

        if expected_frame_range is not None:
            seed = fields["seed"]
            link = fields["link"]
            action = fields["action"]
            if seed >= len(rune.seeds):
                raise ValueError(f"SG seed exceeds authenticated RUNE at line {line_number}")
            if link == -1:
                if action != -1:
                    raise ValueError(
                        f"SG link/action disagree at line {line_number}")
            else:
                if link >= len(rune.links) or action == -1:
                    raise ValueError(
                        f"SG link/action exceed authenticated RUNE at line {line_number}")
                bound = rune.links[link]
                if bound.source != seed or bound.action != action:
                    raise ValueError(
                        f"SG route row disagrees with authenticated RUNE at line {line_number}")

        observations.append({
            "player": name,
            "frame": fields["frame"],
            "seed": fields["seed"],
            "link": fields["link"],
            "action": fields["action"],
            "origin": [fields["x"], fields["y"], fields["z"]],
        })

        seed = fields["seed"]
        action = fields["action"]
        stuck_ms = round(float(match.group("stuck")) * 1000.0)
        if stuck_ms > ROUTE_STALL_DURATION_MS_MAX:
            raise ValueError(f"invalid SG stuck duration at line {line_number}")

        prior = previous.get(name)
        decreased = prior is not None and stuck_ms < prior[1]
        reset = prior is None or decreased
        seed_changed_without_reset = (
            prior is not None and prior[0] != seed and not decreased)
        poisoned = (False if reset else
                    prior[2] or seed_changed_without_reset)
        counted_active = (False if reset or seed_changed_without_reset
                          else prior[3])
        ordinary = (
            seed >= 0 and action == 0 and fields["grounded"] == 1 and
            fields["engaged"] == 0
        )
        if not ordinary:
            previous[name] = (seed, stuck_ms, True, counted_active)
            active_episode[name] = None
            continue
        if stuck_ms <= round(ROUTE_STALL_THRESHOLD_S * 1000.0):
            previous[name] = (seed, stuck_ms, False, False)
            active_episode[name] = None
            continue
        if poisoned:
            previous[name] = (seed, stuck_ms, True, counted_active)
            active_episode[name] = None
            continue

        count, duration_ms = totals.get(seed, (0, 0))
        if reset or not counted_active:
            inferred_frames = stuck_ms // 100
            frame_start = fields["frame"] - inferred_frames + 1
            if expected_frame_range is not None:
                frame_start = max(frame_start, expected_frame_range[0] + 1)
            if frame_start > fields["frame"]:
                raise ValueError(
                    f"SG stuck interval is invalid at line {line_number}")
            accepted_duration_ms = (
                fields["frame"] - frame_start + 1
            ) * 100
            count += 1
            duration_ms += accepted_duration_ms
            active_episode[name] = len(episodes)
            episodes.append({
                "player": name,
                "seed": seed,
                "frame_start": frame_start,
                "frame_end_exclusive": fields["frame"] + 1,
                "evidence_count": 1,
                "duration_ms": accepted_duration_ms,
            })
        else:
            duration_ms += stuck_ms - prior[1]
            index = active_episode[name]
            if index is None:
                raise ValueError(
                    f"SG active episode lost authority at line {line_number}")
            episodes[index]["frame_end_exclusive"] = fields["frame"] + 1
            episodes[index]["duration_ms"] += stuck_ms - prior[1]
        if (count > ROUTE_STALL_EVIDENCE_MAX or
                duration_ms > ROUTE_STALL_DURATION_MS_MAX):
            raise ValueError(f"route-stall evidence for seed {seed} exceeds limit")
        totals[seed] = (count, duration_ms)
        previous[name] = (seed, stuck_ms, False, True)

    if seen != expected:
        missing = sorted(expected - seen)
        raise ValueError(f"SG telemetry missing admitted players {missing}")
    if expected_frame_range is not None:
        first = ((expected_frame_range[0] // 10) + 1) * 10
        expected_census = list(range(first, expected_frame_range[1] + 1, 10))
        if not expected_census:
            raise ValueError("residence is too short for SG census authority")
        bad = {
            name: frames for name, frames in sorted(census_frames.items())
            if frames != expected_census
        }
        if bad:
            raise ValueError(
                "SG census does not exactly cover residence frames: "
                f"{bad}")
    return {
        "report_count": report_count,
        "report_counts": dict(sorted(player_report_counts.items())),
        "census_count": sum(len(frames) for frames in census_frames.values()),
        "census_alive_counts": dict(sorted(census_alive.items())),
        "players": sorted(seen),
        "evidence": [
            {"seed": seed, "evidence_count": count,
             "duration_ms": duration_ms}
            for seed, (count, duration_ms) in sorted(totals.items())
        ],
        "episodes": episodes,
        "observations": observations,
    }


def stall_and_post_events(seg, yaw_for_frame, own_stand):
    """Scan one contiguous, teleport-free segment for STALL and POST
episodes. Returns (stall_seconds, post_seconds, [stall episode dicts
    with 'dur','x','y','z']). See module docstring for the exact trigger
    rule."""
    n = len(seg)
    win = max(2, int(round(STALL_WIN_S / DT)))
    if n <= win:
        return 0.0, 0.0, []
    step = [math.hypot(seg[i + 1][1] - seg[i][1], seg[i + 1][2] - seg[i][2])
            for i in range(n - 1)]
    state = [None] * n           # per-frame: None / 'post' / 'stall'
    for i in range(0, n - win):
        net = math.dist(seg[i + win][1:4], seg[i][1:4])
        if net >= STALL_NET_UNITS:
            continue
        mid = seg[i + win // 2]
        near_own = (own_stand is not None and
                    math.hypot(mid[1] - own_stand[0],
                               mid[2] - own_stand[1]) <= POST_R)
        gross = sum(step[i:i + win])
        jitter = gross >= STALL_GROSS_UNITS
        push = gross >= STALL_PUSH_UNITS
        yaw_turn = False
        if not jitter:
            dyaw_tot = 0.0
            ok = True
            for j in range(i, i + win):
                a = yaw_for_frame.get(seg[j][0])
                b = yaw_for_frame.get(seg[j + 1][0])
                if a is None or b is None:
                    ok = False
                    break
                dyaw_tot += abs((b - a + 180.0) % 360.0 - 180.0)
            yaw_turn = (push and ok and
                        dyaw_tot / (win * DT) >= STALL_YAW_DPS)
        # A post is deliberate only when it lacks navigation-failure
        # evidence.  A bot visibly pushing or hunting at its own stand is a
        # snag, not a successful guard hold.
        kind = 'stall' if jitter or yaw_turn else ('post' if near_own else None)
        if kind is None:
            continue
        for j in range(i, i + win + 1):
            if state[j] != 'stall':      # stall always wins a tie with post
                state[j] = kind
    post_s = 0.0
    stall_s = 0.0
    episodes = []
    j = 0
    while j < n:
        if state[j] is None:
            j += 1
            continue
        cur = state[j]
        k = j
        while k < n and state[k] == cur:
            k += 1
        dur = (k - 1 - j) * DT
        if cur == 'post':
            post_s += dur
        else:
            xs = [seg[t][1] for t in range(j, k)]
            ys = [seg[t][2] for t in range(j, k)]
            zs = [seg[t][3] for t in range(j, k)]
            episodes.append({
                'dur': dur, 'x': sum(xs) / len(xs),
                'y': sum(ys) / len(ys), 'z': sum(zs) / len(zs),
                'frame_start': seg[j][0],
                'frame_end_exclusive': seg[k - 1][0] + 1,
            })
            stall_s += dur
        j = k
    return stall_s, post_s, episodes


def cluster_points(points, radius=CLUSTER_R):
    """Connected 3D radius clusters, independent of observation order.

    Edges join episode midpoints no farther than ``radius`` apart.  Taking
    connected components (rather than greedily comparing a changing centroid)
    makes transitive runs A--B--C one cluster even when A and C are farther
    apart, and gives a deterministic result for any input permutation.
    """
    points = sorted(points)
    seen = [False] * len(points)
    clusters = []
    for start in range(len(points)):
        if seen[start]:
            continue
        seen[start] = True
        pending = [start]
        members = []
        while pending:
            index = pending.pop()
            point = points[index]
            members.append(point)
            for other in range(len(points)):
                if not seen[other] and math.dist(point[:3], points[other][:3]) <= radius:
                    seen[other] = True
                    pending.append(other)
        clusters.append({
            'sx': sum(point[0] for point in members),
            'sy': sum(point[1] for point in members),
            'sz': sum(point[2] for point in members),
            'dur': sum(point[3] for point in members),
            'n': len(members),
            'episode_ids': sorted(
                point[4] for point in members if len(point) > 4),
        })
    out = [{'x': round(c['sx'] / c['n'], 1), 'y': round(c['sy'] / c['n'], 1),
            'z': round(c['sz'] / c['n'], 1), 'evidence_count': c['n'],
            'duration_s': round(c['dur'], 2),
            **({'episode_ids': c['episode_ids']} if c['episode_ids'] else {})}
           for c in clusters]
    out.sort(key=lambda c: (-c['evidence_count'], -c['duration_s'],
                            c['x'], c['y'], c['z']))
    return out


def analyze(path, stands_all, map_identities=None, *, expected_map=None,
            expected_players=None, frame_range=None, require_svrecord=False,
            cap_s=850.0, sg_report_lines=None, server_frame_range=None,
            rune=None):
    """Analyze one demo, optionally under an authenticated residence window.

    ``frame_range`` is a half-open pair of ``film.walk_demo`` snapshot indices
    (the first snapshot is 1).  It is not the wire ``svc_frame`` number.  When
    supplied, only those exact snapshots contribute and ``expected_players``
    is an exact ``name -> {team, entity}`` authority, not a best-effort filter.
    Production corpus builds use ``cap_s=None`` so the general film-report
    duration cap cannot silently shorten the residence window.
    """
    authenticated = (frame_range is not None or require_svrecord or
                     expected_players is not None or sg_report_lines is not None)
    d = film.walk_demo(path, strict=authenticated)
    if (authenticated and
            (d.get('parse_complete') is not True or
             d.get('terminated') is not True)):
        raise ValueError("authenticated snag census requires a complete decode")
    if cap_s is not None:
        film.cap_tracks_to_duration(d, cap_s=cap_s)
    if expected_map is not None and d['map'] != expected_map:
        raise ValueError(
            f"demo map {d['map']!r} does not match {expected_map!r}")
    if require_svrecord and not d['svrecord']:
        raise ValueError("authenticated snag census requires serverrecord")
    if require_svrecord:
        wire_frames = d.get('wire_framenums')
        if (not isinstance(wire_frames, list) or
                len(wire_frames) != d['frames'] or not wire_frames or
                any(type(value) is not int for value in wire_frames) or
                any(right != left + 1
                    for left, right in zip(wire_frames, wire_frames[1:]))):
            raise ValueError(
                "authenticated snag census requires consecutive wire frames")
    if frame_range is not None:
        if (not isinstance(frame_range, (tuple, list)) or
                len(frame_range) != 2 or
                any(not isinstance(value, int) or isinstance(value, bool)
                    for value in frame_range) or
                frame_range[0] < 1 or frame_range[1] <= frame_range[0]):
            raise ValueError("frame_range must be a nonempty half-open integer pair")
        if d['frames'] < frame_range[1] - 1:
            raise ValueError("demo ends before authenticated frame window")
        if server_frame_range is not None and require_svrecord:
            if (not isinstance(server_frame_range, (tuple, list)) or
                    len(server_frame_range) != 2 or
                    any(type(value) is not int for value in server_frame_range) or
                    server_frame_range[0] < 0 or
                    server_frame_range[1] <= server_frame_range[0]):
                raise ValueError("server_frame_range is not a valid frame pair")
            selected_wire = d['wire_framenums'][
                frame_range[0] - 1:frame_range[1] - 1]
            expected_count = server_frame_range[1] - server_frame_range[0]
            if (len(selected_wire) != expected_count or
                    selected_wire[0] != server_frame_range[0] + 1 or
                    selected_wire[-1] != server_frame_range[1]):
                raise ValueError(
                    "demo frame window does not equal residence frame window")
    if frame_range is not None:
        epochs = d.get('skin_epochs')
        if not isinstance(epochs, dict):
            raise ValueError("authenticated snag census requires skin epochs")
        raw_skins = {}
        for ent in d['tracks']:
            slot_epochs = epochs.get(ent - 1, [])
            if not isinstance(slot_epochs, list):
                raise ValueError("invalid skin epoch inventory")
            active = None
            previous_effective = 0
            for item in slot_epochs:
                if (not isinstance(item, (tuple, list)) or len(item) != 2 or
                        type(item[0]) is not int or item[0] < 1 or
                        item[0] < previous_effective or
                        not isinstance(item[1], str)):
                    raise ValueError("invalid skin epoch")
                effective, value = item
                previous_effective = effective
                if effective <= frame_range[0]:
                    active = value
                elif effective < frame_range[1]:
                    raise ValueError(
                        f"client slot {ent - 1} changed identity in frame window")
            raw_skins[ent] = active or ''
    else:
        raw_skins = {ent: d['skins'].get(ent - 1, '') for ent in d['tracks']}
    labels = {ent: film.team_of(raw_skins[ent]) for ent in d['tracks']}
    labels = {ent: lab for ent, lab in labels.items() if lab is not None}
    stands = stands_all.get(d['map'])
    if frame_range is not None:
        if not isinstance(stands, dict) or set(stands) != {'red', 'blue'}:
            raise ValueError("authenticated snag census requires both flag stands")
        for team, origin in stands.items():
            if (not isinstance(origin, (tuple, list)) or len(origin) < 2 or
                    any(not isinstance(value, (int, float)) or
                        isinstance(value, bool) or not math.isfinite(value)
                        for value in origin[:2])):
                raise ValueError(f"invalid {team} flag stand")

    expected = None
    if expected_players is not None:
        if not isinstance(expected_players, dict) or not expected_players:
            raise ValueError(
                "expected_players must be an exact nonempty binding object")
        expected = set(expected_players)
        expected_entities = set()
        for name, binding in expected_players.items():
            if (not isinstance(name, str) or not name or
                    not isinstance(binding, dict) or
                    set(binding) != {"team", "entity"} or
                    binding["team"] not in {"red", "blue"} or
                    type(binding["entity"]) is not int or
                    not 1 <= binding["entity"] <= 32 or
                    binding["entity"] in expected_entities):
                raise ValueError("expected player binding is invalid")
            expected_entities.add(binding["entity"])
        present = {}
        for ent, team in labels.items():
            name = raw_skins[ent].split('\\', 1)[0]
            if name in present:
                raise ValueError(f"duplicate admitted player {name}")
            present[name] = (ent, team)
        if set(present) != expected:
            missing = sorted(expected - set(present))
            extra = sorted(set(present) - expected)
            raise ValueError(
                f"admitted player set mismatch missing={missing} extra={extra}")
        for name, (entity, team) in present.items():
            binding = expected_players[name]
            if entity != binding["entity"] or team != binding["team"]:
                raise ValueError(
                    f"admitted player binding mismatch for {name}")
        labels = {ent: team for name, (ent, team) in present.items()
                  if name in expected}

    tot_obs = 0.0
    stall_s = 0.0
    post_s = 0.0
    durations = []
    points = []
    visible_episodes = []
    for ent, tr in d['tracks'].items():
        team = labels.get(ent)
        if team is None:
            continue
        if frame_range is not None:
            tr = [sample for sample in tr
                  if frame_range[0] <= sample[0] < frame_range[1]]
            expected_frames = list(range(frame_range[0], frame_range[1]))
            actual_frames = [sample[0] for sample in tr]
            if actual_frames != expected_frames:
                raise ValueError(
                    "admitted player does not cover every authenticated snapshot")
        own_stand = stands.get(team) if stands else None
        yaw_map = d['yaws'].get(ent, {})
        for seg in conduct.contiguous_segments(tr):
            tot_obs += (len(seg) - 1) * DT
            s_s, p_s, episodes = stall_and_post_events(seg, yaw_map, own_stand)
            stall_s += s_s
            post_s += p_s
            for e in episodes:
                durations.append(e['dur'])
                if frame_range is not None:
                    name = raw_skins[ent].split('\\', 1)[0]
                    start_snapshot = e['frame_start']
                    end_snapshot = e['frame_end_exclusive']
                    if (type(start_snapshot) is not int or
                            type(end_snapshot) is not int or
                            not frame_range[0] <= start_snapshot <
                                end_snapshot <= frame_range[1]):
                        raise ValueError(
                            "visible stall interval is outside authenticated window")
                    server_start = d['wire_framenums'][start_snapshot - 1]
                    server_end = d['wire_framenums'][end_snapshot - 2] + 1
                    episode_id = f"{name}:{server_start}:{server_end}"
                    if any(item['episode_id'] == episode_id
                           for item in visible_episodes):
                        raise ValueError("duplicate visible stall episode identity")
                    visible_episodes.append({
                        'episode_id': episode_id,
                        'player': name,
                        'frame_start': server_start,
                        'frame_end_exclusive': server_end,
                        'duration_s': round(e['dur'], 2),
                        'x': round(e['x'], 3),
                        'y': round(e['y'], 3),
                        'z': round(e['z'], 3),
                    })
                    points.append((e['x'], e['y'], e['z'], e['dur'], episode_id))
                else:
                    points.append((e['x'], e['y'], e['z'], e['dur']))

    obs_min = tot_obs / 60.0
    med = round(sorted(durations)[len(durations) // 2], 2) if durations else None
    row = {
        'demo': os.path.basename(path),
        'map': d['map'],
        'map_identity': (map_identities or {}).get(d['map']),
        'svrecord': d['svrecord'],
        'players_observed_min': round(obs_min, 2),
        'stalls_per_min': round(len(durations) / obs_min, 3) if obs_min else None,
        'stall_dur_median_s': med,
        'stall_time_frac': round(stall_s / tot_obs, 4) if tot_obs else None,
        'post_hold_frac': round(post_s / tot_obs, 4) if tot_obs else None,
        'stands_known': stands is not None,
        'frame_range': list(frame_range) if frame_range is not None else None,
        'wire_framenum_first': (d['wire_framenums'][0]
                                if require_svrecord else None),
        'wire_framenum_last': (d['wire_framenums'][-1]
                               if require_svrecord else None),
        'players': sorted(expected) if expected is not None else None,
        'stall_episodes': visible_episodes if frame_range is not None else None,
        # Never aggregate these over maps: their coordinates are meaningful
        # only under this row's map identity.
        'snag_clusters': cluster_points(points),
        'top_stall_locations': cluster_points(points)[:3],
        '_durations': durations,      # for --compare's exact pooled median;
        '_obs_min': obs_min,          # stripped before per-demo printing
    }
    if sg_report_lines is not None:
        if expected is None:
            raise ValueError(
                "SG route-stall evidence requires exact expected_players")
        if server_frame_range is None:
            raise ValueError(
                "SG route-stall evidence requires server_frame_range")
        route = route_stall_evidence(
            sg_report_lines, sorted(expected),
            expected_frame_range=server_frame_range, rune=rune)
        row['route_stall_evidence'] = route['evidence']
        row['route_stall_report_count'] = route['report_count']
        row['route_stall_report_counts'] = route['report_counts']
        row['route_stall_players'] = route['players']
        row['route_stall_census_count'] = route['census_count']
        row['route_stall_census_alive_counts'] = route['census_alive_counts']
        row['route_stall_episodes'] = route['episodes']

        snapshot_by_wire = {
            wire: index + 1 for index, wire in enumerate(d['wire_framenums'])
        }
        tracks_by_frame = {
            entity: {sample[0]: sample for sample in samples}
            for entity, samples in d['tracks'].items()
        }
        for observation in route['observations']:
            binding = expected_players[observation['player']]
            snapshot = snapshot_by_wire.get(observation['frame'])
            sample = tracks_by_frame[binding['entity']].get(snapshot)
            if (snapshot is None or sample is None or
                    math.dist(sample[1:4], observation['origin']) > 1.0):
                raise ValueError(
                    "SG route origin disagrees with authenticated demo frame")
    return row


def pool(rows):
    """Weighted pool by observed minutes, true pooled median from the
    concatenated raw episode durations -- not an average of per-demo
    medians. top_stall_locations is intentionally not pooled (see module
    docstring)."""
    w = sum(r['_obs_min'] for r in rows) or 1.0

    def avg(k):
        xs = [(r[k], r['_obs_min']) for r in rows if r.get(k) is not None]
        return sum(v * m for v, m in xs) / (sum(m for _, m in xs) or 1.0)

    all_durs = sorted(x for r in rows for x in r['_durations'])
    out = {
        'demos': len(rows),
        'stands_known_demos': sum(1 for r in rows if r['stands_known']),
        'observed_min': round(w, 1),
        'stalls_per_min': round(avg('stalls_per_min'), 3),
        'stall_dur_median_s': (round(all_durs[len(all_durs) // 2], 2)
                                if all_durs else None),
        'stall_time_frac': round(avg('stall_time_frac'), 4),
        'post_hold_frac': round(avg('post_hold_frac'), 4),
    }
    return out


def _public(row):
    return {k: v for k, v in row.items() if not k.startswith('_')}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('demos', nargs='*')
    ap.add_argument('--scalars', action='store_true')
    ap.add_argument('--compare', action='store_true')
    ap.add_argument('--human', action='append', default=[])
    ap.add_argument('--bot', action='append', default=[])
    ap.add_argument('--map-identity', help='JSON mapping from map name to exact map identity')
    ap.add_argument('--strict', action='store_true',
                    help='fail on an unreadable or invalid demo instead of skipping it')
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, 'stands.json')) as f:
        stands_all = json.load(f)
    map_identities = {}
    if args.map_identity:
        with open(args.map_identity) as f:
            map_identities = json.load(f)

    def run(globs_or_paths):
        rows = []
        for g in globs_or_paths:
            for p in sorted(globmod.glob(os.path.expanduser(g))) or [g]:
                try:
                    rows.append(analyze(p, stands_all, map_identities))
                except Exception as e:
                    if args.strict:
                        raise
                    print(f"# skip {os.path.basename(p)}: {e}",
                          file=sys.stderr)
        return rows

    if args.compare:
        hu = run(args.human)
        bo = run(args.bot)
        print(json.dumps({'human': pool(hu), 'bot': pool(bo)}, indent=2))
        return
    for r in run(args.demos):
        print(json.dumps(_public(r)))


if __name__ == '__main__':
    main()
