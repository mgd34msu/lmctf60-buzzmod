#!/usr/bin/env python3
"""
hookdiag.py -- cross-reference HOOKFIRE / HOOKEND / HOOKABORT / HOOKBITE with the
1 Hz SG telemetry lines to bucket the noattach mass.

Usage: hookdiag.py <iter-dir> [<iter-dir> ...]

No game code is touched; this only reads logs.
"""
import sys, os, math, glob
from bisect import bisect_left, bisect_right
from collections import defaultdict, Counter
if os.path.dirname(__file__) not in sys.path:
    sys.path.insert(0, os.path.dirname(__file__))
from hookevents import scan_file

HOOK_SPEED = 800.0   # GRAPPLE_FIRE_HOOK_SPEED, p_weapon.c:14
PULLED_TERMINALS = ("apex", "arrived", "landed", "burst")


class Fire:
    __slots__ = ("id", "bot", "kind", "link", "map", "detail", "anchor",
                 "org", "spd", "gnd", "act", "role", "line", "tick",
                 "end", "endline", "endtick", "abort_count", "bite_count",
                 "maxspd", "deaths", "nsg", "skyholds", "dist", "postorg",
                 "any_cause", "_abort_window", "_bite_window",
                 "_death_window")

    def __init__(self):
        self.id = None
        self.bot = None
        self.kind = None
        self.link = None
        self.map = None
        self.detail = None
        self.anchor = None
        self.org = None
        self.spd = None
        self.gnd = None
        self.act = None
        self.role = None
        self.line = None
        self.tick = 0
        self.end = None
        self.endline = None
        self.endtick = 0
        self.abort_count = 0
        self.bite_count = 0
        self.deaths = 0
        self.maxspd = -1
        self.nsg = 0
        self.skyholds = 0
        self.dist = None
        self.postorg = None
        self.any_cause = False
        self._abort_window = None
        self._bite_window = None
        self._death_window = None


class _BotTimeline:
    """One bot's SG and auxiliary positions, finalized with bounded indexes."""

    __slots__ = ("sg_positions", "sg_samples", "sg_prefix", "_speeds",
                 "_max_tree", "_tree_base", "marker_positions",
                 "marker_values", "marker_prefix")

    def __init__(self):
        self.sg_positions = []
        self.sg_samples = []
        self.sg_prefix = [0]
        self._speeds = []
        self._max_tree = []
        self._tree_base = 0
        self.marker_positions = defaultdict(list)
        self.marker_values = defaultdict(list)
        self.marker_prefix = defaultdict(list)

    def add_sg(self, sample):
        self.sg_positions.append(sample.line)
        self.sg_samples.append(sample)
        self.sg_prefix.append(len(self.sg_samples))
        self._speeds.append(sample.spd)

    def add_marker(self, marker):
        self.marker_positions[marker.kind].append(marker.line)
        self.marker_values[marker.kind].append(marker)
        self.marker_prefix[marker.kind].append(
            len(self.marker_values[marker.kind]))

    def build_indexes(self):
        """Build an O(log N) range maximum index without per-fire scans."""
        count = len(self._speeds)
        if not count:
            self._tree_base = 0
            self._max_tree = []
            return
        base = 1
        while base < count:
            base <<= 1
        tree = [-1] * (base * 2)
        tree[base:base + count] = self._speeds
        for index in range(base - 1, 0, -1):
            tree[index] = max(tree[index << 1], tree[(index << 1) | 1])
        self._tree_base = base
        self._max_tree = tree

    def _sg_bounds(self, start, end):
        # The report interval is exactly [FIRE position, END position), but
        # observations at the FIRE line itself are excluded as well: a line
        # contains one record, never both a FIRE and SG sample.
        return (bisect_right(self.sg_positions, start),
                bisect_left(self.sg_positions, end))

    def sample_before(self, position):
        index = bisect_left(self.sg_positions, position) - 1
        return self.sg_samples[index] if index >= 0 else None

    def tick_before(self, position):
        return self.sg_prefix[bisect_left(self.sg_positions, position)]

    def range_count(self, start, end):
        left, right = self._sg_bounds(start, end)
        return self.sg_prefix[right] - self.sg_prefix[left]

    def first_in_range(self, start, end):
        left, right = self._sg_bounds(start, end)
        return self.sg_samples[left] if left < right else None

    def range_max(self, start, end):
        left, right = self._sg_bounds(start, end)
        if left >= right:
            return -1
        left += self._tree_base
        right += self._tree_base
        result = -1
        while left < right:
            if left & 1:
                result = max(result, self._max_tree[left])
                left += 1
            if right & 1:
                right -= 1
                result = max(result, self._max_tree[right])
            left >>= 1
            right >>= 1
        return result

    def marker_bounds(self, kind, start, end):
        positions = self.marker_positions.get(kind, ())
        return (bisect_right(positions, start),
                bisect_left(positions, end))

    def marker_count(self, kind, start, end):
        left, right = self.marker_bounds(kind, start, end)
        prefix = self.marker_prefix.get(kind, ())
        if not prefix or left >= right:
            return 0
        return prefix[right - 1] - (prefix[left - 1] if left else 0)

class _DiagObserver:
    """Collect one-pass scan observations into lazy per-bot timelines."""

    def __init__(self):
        self.events = {}
        self.timelines = {}
        self.anomalies = []
        self.eof_line = 0

    def timeline(self, bot):
        timeline = self.timelines.get(bot)
        if timeline is None:
            timeline = _BotTimeline()
            self.timelines[bot] = timeline
        return timeline

    def on_event(self, event):
        self.events[event.line] = event

    def on_telemetry(self, sample):
        self.timeline(sample.bot).add_sg(sample)

    def on_aux(self, marker):
        self.timeline(marker.bot).add_marker(marker)

    def on_anomaly(self, anomaly):
        self.anomalies.append(anomaly)

    def on_eof(self, marker):
        self.eof_line = marker.line

    def finalize(self, event, end_event=None):
        timeline = self.timeline(event.bot)
        anchor = tuple(value / 8.0 for value in event.anchor_q8)
        f = Fire()
        f.id = event.id
        f.bot = event.bot
        f.kind = event.kind
        f.link = event.link
        f.map = event.map
        f.anchor = anchor
        f.line = event.line
        before = timeline.sample_before(event.line)
        if before is not None:
            f.org = before.org
            f.spd = before.spd
            f.gnd = before.gnd
            f.act = before.act
            f.role = before.role
            f.dist = math.dist(f.org, anchor)
        f.tick = timeline.tick_before(event.line)

        if end_event is None:
            f.end = "NOEND"
            f.endline = self.eof_line
            # EOF is a cursor after the last consumed line.  This includes a
            # final SG record, matching the old streaming consumer's result,
            # while still exposing the historical last-line endline field.
            end_position = self.eof_line + 1
            f.detail = None
        else:
            f.end = end_event.reason
            f.detail = end_event.detail
            f.endline = end_event.line
            end_position = end_event.line
        f.endtick = timeline.tick_before(end_position)
        f.nsg = timeline.range_count(event.line, end_position)
        f.maxspd = timeline.range_max(event.line, end_position)
        first = timeline.first_in_range(event.line, end_position)
        f.postorg = first.org if first is not None else None

        abort_left, abort_right = timeline.marker_bounds(
            "HOOKABORT", event.line, end_position)
        bite_left, bite_right = timeline.marker_bounds(
            "HOOKBITE", event.line, end_position)
        death_left, death_right = timeline.marker_bounds(
            "BOTDEATH", event.line, end_position)
        f._abort_window = (timeline, abort_left, abort_right)
        f._bite_window = (timeline, bite_left, bite_right)
        f._death_window = (timeline, death_left, death_right)
        f.abort_count = abort_right - abort_left
        f.bite_count = bite_right - bite_left
        f.skyholds = timeline.marker_count("HOOKSKYHOLD", event.line,
                                           end_position)
        f.deaths = death_right - death_left
        f.any_cause = bool(f.abort_count or f.bite_count or f.deaths)
        return f


def parse_with_result(path):
    observer = _DiagObserver()
    pairing = scan_file(path, observer)
    if pairing.global_fatal:
        return [], pairing
    # A malformed SG record invalidates the entire analytics population.  The
    # scan still completes so anomaly reporting and the pairing result remain
    # deterministic, but no partial records escape this boundary.
    if any(anomaly.fatal for anomaly in pairing.telemetry_anomalies):
        return [], pairing
    for timeline in observer.timelines.values():
        timeline.build_indexes()

    # Pairing order is deliberate: completed pairs retain pair_file's END
    # insertion order, followed by incomplete FIREs in their FIRE order.  No
    # event observed outside this result can become an analytics record.
    fires = [observer.finalize(pair.fire, pair.end)
             for pair in pairing.pairs]
    fires.extend(observer.finalize(event) for event in pairing.incomplete)
    return fires, pairing


class CauseSummary:
    """Bounded aggregate of engine-visible causes for noattach fires."""

    __slots__ = ("abort_counts", "bite_fires", "death_fires",
                 "any_cause_fires")

    def __init__(self, abort_counts=None, bite_fires=0, death_fires=0,
                 any_cause_fires=0):
        self.abort_counts = Counter(abort_counts or {})
        self.bite_fires = bite_fires
        self.death_fires = death_fires
        self.any_cause_fires = any_cause_fires

    # Short aliases make the summary convenient to inspect without changing
    # the explicit names used by the report.
    @property
    def aborts(self):
        return self.abort_counts

    @property
    def bites(self):
        return self.bite_fires

    @property
    def deaths(self):
        return self.death_fires

    @property
    def any_cause(self):
        return self.any_cause_fires


def aggregate_noattach_causes(fires):
    """Aggregate abort reasons with one difference pass per timeline.

    Each Fire stores precomputed marker indices for its exact interval.  For
    each shared timeline, difference-array coverage turns overlapping abort
    windows into one pass over the timeline's HOOKABORT marker array.  A
    marker contributes once per covering fire, preserving multiplicity while
    avoiding per-fire marker iteration.  Bite/death/any-cause metrics are
    deliberately fire booleans, matching the report's historical semantics.
    """
    summary = CauseSummary()
    windows = defaultdict(list)
    for fire in fires:
        if fire.end != "noattach":
            continue
        summary.bite_fires += int(bool(fire.bite_count))
        summary.death_fires += int(bool(fire.deaths))
        summary.any_cause_fires += int(bool(fire.any_cause))
        window = fire._abort_window
        if window is not None:
            timeline, left, right = window
            windows[timeline].append((left, right))

    for timeline, ranges in windows.items():
        markers = timeline.marker_values.get("HOOKABORT", ())
        if not markers:
            continue
        diff = [0] * (len(markers) + 1)
        for left, right in ranges:
            if left < right:
                diff[left] += 1
                diff[right] -= 1
        coverage = 0
        for index, marker in enumerate(markers):
            coverage += diff[index]
            if coverage:
                summary.abort_counts[marker.reason] += coverage
    return summary


def parse(path):
    return parse_with_result(path)[0]


def report_anomalies(path, pairing):
    for anomaly in pairing.anomalies:
        scope = " global-fatal" if anomaly.global_fatal else ""
        print("%s:%d: hook protocol %s%s: %s" %
              (path, anomaly.line, anomaly.code, scope, anomaly.message), file=sys.stderr)
    for anomaly in pairing.telemetry_anomalies:
        scope = " fatal" if anomaly.fatal else ""
        print("%s:%d: SG telemetry %s%s: %s" %
              (path, anomaly.line, anomaly.code, scope, anomaly.message),
              file=sys.stderr)


def pct(n, d):
    return (100.0 * n / d) if d else 0.0


def quantiles(vals, qs=(0.10, 0.25, 0.50, 0.75, 0.90, 0.99)):
    if not vals:
        return []
    s = sorted(vals)
    return [s[min(len(s) - 1, int(q * len(s)))] for q in qs]


def main():
    dirs = sys.argv[1:]
    allfires = []
    bad = False
    global_fatal = False
    for d in dirs:
        for p in sorted(glob.glob(os.path.join(d, "*.log"))):
            fs, pairing = parse_with_result(p)
            allfires.extend([(os.path.basename(p), f) for f in fs])
            report_anomalies(p, pairing)
            bad = bad or bool(pairing.anomalies) or any(
                anomaly.fatal for anomaly in pairing.telemetry_anomalies)
            global_fatal = global_fatal or pairing.global_fatal
    if global_fatal:
        return 1

    fires = [f for _, f in allfires]
    n = len(fires)
    print("=" * 78)
    print("total HOOKFIRE records parsed: %d  (dirs: %s)" % (n, ", ".join(dirs)))
    endc = Counter(f.end for f in fires)
    for k, v in endc.most_common():
        print("  end=%-10s %6d  %5.1f%%" % (k, v, pct(v, n)))

    na = [f for f in fires if f.end == "noattach"]
    ok = [f for f in fires if f.end in PULLED_TERMINALS]
    noend = [f for f in fires if f.end == "NOEND"]

    print()
    print("--- distance from bot origin (last SG before fire) to anchor ---")
    for name, grp in (("noattach", na), ("successful controls", ok), ("NOEND", noend)):
        ds = [f.dist for f in grp if f.dist is not None]
        q = quantiles(ds)
        if not ds:
            print("%-16s (no samples)" % name)
            continue
        print("%-16s n=%5d  p10=%5.0f p25=%5.0f p50=%5.0f p75=%5.0f p90=%5.0f p99=%5.0f  mean=%5.0f"
              % (name, len(ds), *(q + [sum(ds) / len(ds)])))

    print()
    print("--- implied bolt flight time at 800 u/s (dist/800, seconds) ---")
    for name, grp in (("noattach", na), ("successful controls", ok)):
        ds = [f.dist / HOOK_SPEED for f in grp if f.dist is not None]
        if not ds:
            print("%-16s (no samples)" % name)
            continue
        q = quantiles(ds)
        print("%-16s p50=%.2f p75=%.2f p90=%.2f p99=%.2f  frac>1.0s=%.1f%%  frac>0.5s=%.1f%%"
              % (name, q[2], q[3], q[4], q[5],
                 pct(sum(1 for x in ds if x > 1.0), len(ds)),
                 pct(sum(1 for x in ds if x > 0.5), len(ds))))

    print()
    print("--- lifetime of the fire, in SG ticks (~1 s each) ---")
    for name, grp in (("noattach", na), ("successful controls", ok), ("NOEND", noend)):
        c = Counter(f.nsg for f in grp)
        tot = len(grp)
        print("%-16s n=%5d  0 ticks(<1s)=%5.1f%%  1 tick=%5.1f%%  2 ticks=%5.1f%%  >=3=%5.1f%%"
              % (name, tot, pct(c[0], tot), pct(c[1], tot), pct(c[2], tot),
                 pct(sum(v for k, v in c.items() if k >= 3), tot)))

    print()
    print("--- log-line gap between HOOKFIRE and its end (proxy for frames) ---")
    for name, grp in (("noattach", na), ("successful controls", ok)):
        gaps = [f.endline - f.line for f in grp]
        if not gaps:
            print("%-16s (no samples)" % name)
            continue
        q = quantiles(gaps)
        print("%-16s p10=%4d p25=%4d p50=%4d p75=%4d p90=%4d p99=%5d  gap<=2 = %.1f%%"
              % (name, *q, pct(sum(1 for g in gaps if g <= 2), len(gaps))))

    print()
    print("--- max SG spd observed while the fire was open (pull is a flat 800) ---")
    for name, grp in (("noattach", na), ("successful controls", ok), ("NOEND", noend)):
        v = [f.maxspd for f in grp if f.maxspd >= 0]
        if not v:
            print("%-16s (no samples)" % name)
            continue
        q = quantiles(v)
        print("%-16s n=%5d p50=%4d p75=%4d p90=%4d p99=%4d  frac>=600=%.1f%%  frac>=400=%.1f%%"
              % (name, len(v), q[2], q[3], q[4], q[5],
                 pct(sum(1 for x in v if x >= 600), len(v)),
                 pct(sum(1 for x in v if x >= 400), len(v))))

    print()
    print("--- engine-visible causes recorded inside the fire window ---")
    cause_summary = aggregate_noattach_causes(na)
    ac = Counter({"HOOKABORT " + reason: count
                 for reason, count in cause_summary.abort_counts.items()})
    if cause_summary.bite_fires:
        ac["HOOKBITE (rope did attach)"] = cause_summary.bite_fires
    if cause_summary.death_fires:
        ac["BOTDEATH during window"] = cause_summary.death_fires
    print("  noattach fires with any of these: %d / %d (%.1f%%)"
          % (cause_summary.any_cause_fires, len(na),
             pct(cause_summary.any_cause_fires, len(na))))
    for k, v in sorted(ac.items(), key=lambda item: (-item[1], item[0])):
        print("    %-32s %5d  (%.1f%% of noattach)" % (k, v, pct(v, len(na))))

    print()
    print("--- noattach vs bot state at fire ---")
    for label, key in (("gnd", "gnd"), ("role", "role"), ("act", "act")):
        cn = Counter(getattr(f, key) for f in na)
        co = Counter(getattr(f, key) for f in ok)
        keys = sorted(set(cn) | set(co), key=lambda x: (x is None, x))
        print("  %s:" % label)
        for k in keys:
            tot = cn[k] + co[k]
            print("     %-6s noattach=%5d  ok=%5d  noattach share=%5.1f%%"
                  % (k, cn[k], co[k], pct(cn[k], tot)))

    print()
    print("--- noattach rate as a function of distance to anchor ---")
    bins = [(0, 100), (100, 200), (200, 300), (300, 400), (400, 600), (600, 800),
            (800, 1200), (1200, 2000), (2000, 100000)]
    print("  %-12s %7s %7s %7s %8s" % ("dist band", "fires", "noatt", "ok", "noatt%"))
    for lo, hi in bins:
        a = sum(1 for f in na if f.dist is not None and lo <= f.dist < hi)
        b = sum(1 for f in ok if f.dist is not None and lo <= f.dist < hi)
        print("  %-12s %7d %7d %7d %7.1f%%" % ("%d-%d" % (lo, hi), a + b, a, b, pct(a, a + b)))

    print()
    print("--- speed at fire vs outcome ---")
    sbins = [(0, 50), (50, 150), (150, 250), (250, 350), (350, 500), (500, 100000)]
    print("  %-12s %7s %7s %7s %8s" % ("spd band", "fires", "noatt", "ok", "noatt%"))
    for lo, hi in sbins:
        a = sum(1 for f in na if f.spd is not None and lo <= f.spd < hi)
        b = sum(1 for f in ok if f.spd is not None and lo <= f.spd < hi)
        print("  %-12s %7d %7d %7d %7.1f%%" % ("%d-%d" % (lo, hi), a + b, a, b, pct(a, a + b)))

    print()
    print("--- how far the bot moved during the fire window (first SG after fire) ---")
    for name, grp in (("noattach", na), ("successful controls", ok)):
        v = [math.dist(f.org, f.postorg) for f in grp if f.org and f.postorg]
        if not v:
            print("%-16s (no samples)" % name)
            continue
        q = quantiles(v)
        print("%-16s n=%5d p25=%5.0f p50=%5.0f p75=%5.0f p90=%5.0f" %
              (name, len(v), q[1], q[2], q[3], q[4]))

    print()
    print("--- skyholds burned in phase 1 before this fire ---")
    for name, grp in (("noattach", na), ("successful controls", ok)):
        v = [f.skyholds for f in grp]
        print("%-16s mean=%.1f  frac with >0 = %.1f%%" %
              (name, sum(v) / len(v) if v else 0, pct(sum(1 for x in v if x), len(v))))

    print()
    print("--- per-map noattach share ---")
    per = defaultdict(lambda: [0, 0])
    for src, f in allfires:
        per[src][0] += 1
        if f.end == "noattach":
            per[src][1] += 1
    for k in sorted(per):
        tot, noattach_count = per[k]
        print("  %-28s fires=%5d noattach=%5d  %5.1f%%" %
              (k, tot, noattach_count, pct(noattach_count, tot)))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
