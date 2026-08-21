#!/usr/bin/env python3
"""Summarize objective-role behavior from SG telemetry and flag events.

Defense and pressure count bot samples against stable route cost. Escort pairs
same-team samples in one-second server-frame buckets. Recovery cost is measured
only inside authoritative steal-to-return-or-capture windows.
"""

import collections
import math
import re
import statistics
import sys


ROLE_ATTACK = 0
ROLE_DEFEND = 1
ROLE_CARRY = 2
ROLE_RECOVER = 3
ROLE_ESCORT = 4
SERVER_FRAMES_PER_SECOND = 10
DEFENSE_NEAR_MS = 1500
PRESSURE_NEAR_MS = 8000
ESCORT_NEAR_UNITS = 700

ROW = re.compile(
    r"SG (?P<name>\S+): role=(?P<role>\d+) seed=-?\d+ "
    r"goal=(?P<goal>-?\d+)(?: sgoal=(?P<sgoal>-?\d+))? "
    r"spd=-?\d+ org=\((?P<org>[-0-9. ]+)\)"
)
FRAME = re.compile(r"(?:^| )frm=(?P<frame>\d+)(?: |$)")
TEAM = re.compile(r"^(?P<name>\S+) is now on the (?P<team>red|blue) team\.$")
STEAL = re.compile(r"^\S+ stole the (?P<team>red|blue) flag\.$")
RECOVERY_END = re.compile(
    r"^\S+ (?:returned|captured) the (?P<team>red|blue) flag\.$"
)


def trace_deltas(windows):
    deltas = []
    for window in windows:
        for goals in window.values():
            if len(goals) > 1:
                deltas.append(goals[-1] - goals[0])
    return deltas


def percent(good, count):
    return f"{100 * good // count}%" if count else "-"


def grade(path):
    frames = collections.defaultdict(dict)
    teams = {}
    rows = []
    active_recovery = {"red": None, "blue": None}
    closed_windows = []
    recognized = 0
    with open(path, errors="replace") as log:
        for line in log:
            stripped = line.rstrip("\n")
            team_match = TEAM.match(stripped)
            if team_match:
                teams[team_match.group("name")] = team_match.group("team")
                continue
            steal_match = STEAL.match(stripped)
            if steal_match:
                active_recovery[steal_match.group("team")] = collections.defaultdict(list)
                continue
            end_match = RECOVERY_END.match(stripped)
            if end_match:
                team = end_match.group("team")
                if active_recovery[team] is not None:
                    closed_windows.append(active_recovery[team])
                    active_recovery[team] = None
                continue
            match = ROW.search(line)
            if not match:
                continue
            recognized += 1
            role = int(match.group("role"))
            goal = int(match.group("goal"))
            stable_goal = match.group("sgoal")
            if stable_goal is None:
                stable_goal = goal
            position = tuple(float(value) for value in match.group("org").split())
            name = match.group("name")
            team = teams.get(name)
            rows.append((role, int(stable_goal)))
            frame_match = FRAME.search(line)
            if frame_match and team is not None:
                frame = int(frame_match.group("frame")) // SERVER_FRAMES_PER_SECOND
                frames[frame][name] = (team, role, position)
            if (role == ROLE_RECOVER and goal >= 0 and team is not None and
                    active_recovery[team] is not None):
                active_recovery[team][name].append(goal)
    if not recognized:
        raise ValueError(f"rolestat: no SG telemetry rows recognized: {path}")

    defense_all = sum(role == ROLE_DEFEND for role, _ in rows)
    defense_ok = sum(role == ROLE_DEFEND and 0 <= goal < DEFENSE_NEAR_MS
                     for role, goal in rows)
    pressure_all = sum(role == ROLE_ATTACK for role, _ in rows)
    pressure_ok = sum(role == ROLE_ATTACK and 0 <= goal < PRESSURE_NEAR_MS
                      for role, goal in rows)
    wander = sum(goal < 0 for _, goal in rows)

    escort_ok = escort_all = 0
    for frame in frames.values():
        for team in ("red", "blue"):
            carriers = [position for row_team, role, position in frame.values()
                        if row_team == team and role == ROLE_CARRY]
            if not carriers:
                continue
            escort_all += 1
            escorts = [position for row_team, role, position in frame.values()
                       if row_team == team and role == ROLE_ESCORT]
            if escorts and min(math.dist(carrier, escort)
                               for carrier in carriers for escort in escorts) < ESCORT_NEAR_UNITS:
                escort_ok += 1

    open_windows = [window for window in active_recovery.values()
                    if window is not None]
    closed_deltas = trace_deltas(closed_windows)
    open_deltas = trace_deltas(open_windows)
    recovery = (f"{statistics.median(closed_deltas):+.0f}ms median delta"
                if closed_deltas else "-")
    open_recovery = (f"{statistics.median(open_deltas):+.0f}ms median delta"
                     if open_deltas else "-")
    name = path.split("/")[-1]
    print(f"{name}: defense {percent(defense_ok, defense_all)} "
          f"({defense_all} bot-samples) | pressure {percent(pressure_ok, pressure_all)} "
          f"({pressure_all} bot-samples) | same-team escort-buckets "
          f"{percent(escort_ok, escort_all)} "
          f"({escort_all} one-second team-buckets) | recover-cost closed {recovery} "
          f"({len(closed_deltas)} bot-traces, {len(closed_windows)} windows) "
          f"open {open_recovery} ({len(open_deltas)} bot-traces, "
          f"{len(open_windows)} windows) | "
          f"wander {percent(wander, len(rows))}")


def main(argv):
    if not argv:
        print("usage: rolestat.py <wave.log> [<wave.log> ...]", file=sys.stderr)
        return 2
    try:
        for path in argv:
            grade(path)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
