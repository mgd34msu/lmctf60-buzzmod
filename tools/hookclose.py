#!/usr/bin/env python3
"""Measure rope closure from paired hook diagnostics and SG telemetry.

Each completed fire/end pair compares the nearest telemetry samples before the
fire and after termination. Malformed or globally invalid input fails closed.
"""
import sys, os, math, glob, statistics
from collections import defaultdict, Counter
if os.path.dirname(__file__) not in sys.path:
    sys.path.insert(0, os.path.dirname(__file__))
from hookevents import AuxMarker, HookEvent, SGTelemetry, scan_file


def parse_with_result(path):
    """Return list of dicts, one per fire that got an end and has SG samples
    on both sides."""
    out = []
    pend = defaultdict(list)  # bot -> records waiting for post-end SG sample
    open_f = {}      # (id, bot) -> record between fire and end
    last_sg = {}
    armskies = defaultdict(int)
    timeline = []

    def collect(item):
        # scan_file's input ceiling makes this replay list bounded.  Retain
        # only validated typed records; anomalies remain in PairingResult.
        if isinstance(item, (HookEvent, SGTelemetry, AuxMarker)):
            timeline.append(item)

    pairing = scan_file(path, collect)
    if pairing.global_fatal:
        return [], pairing
    fire_events = {pair.fire.line: pair.fire for pair in pairing.pairs}
    end_events = {pair.end.line: pair.end for pair in pairing.pairs}
    for item in timeline:
        if isinstance(item, SGTelemetry):
            bot = item.bot
            org = item.org
            spd = item.spd
            gnd = item.gnd
            last_sg[bot] = (org, spd, gnd)
            for r in pend.pop(bot, []):
                r["after"] = org
                r["spd_after"] = spd
                r["gnd_after"] = gnd
                r["d_after"] = math.dist(org, r["anchor"])
                out.append(r)
            continue
        if isinstance(item, AuxMarker):
            if item.kind == "HOOKSKYHOLD":
                armskies[item.bot] += 1
            continue
        if isinstance(item, HookEvent):
            event = fire_events.get(item.line)
            if event is not None:
                anchor = tuple(value / 8.0 for value in event.anchor_q8)
                bot = event.bot
                sg = last_sg.get(bot)
                if not sg:
                    continue
                open_f[event.key] = dict(id=event.id, bot=bot,
                                   anchor=anchor, before=sg[0],
                                   spd_before=sg[1], gnd_before=sg[2],
                                   d_before=math.dist(sg[0], anchor),
                                   line=event.line, armsky=armskies[bot])
                armskies[bot] = 0
                continue
            event = end_events.get(item.line)
            if event is not None:
                bot, tag = event.bot, event.reason
                r = open_f.pop(event.key, None)
                if r is None:
                    continue
                r["tag"] = tag
                r["detail"] = event.detail
                r["endline"] = event.line
                pend[bot].append(r)
                continue
    # A fatal telemetry anomaly makes this stream unsuitable for analytics as
    # a whole.  Never present a partial closure population beside a malformed
    # sample; the shared policy is the same in hookdiag.
    if any(anomaly.fatal for anomaly in pairing.telemetry_anomalies):
        out = []
    return out, pairing


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


def report(recs, title):
    print("\n### %s   (n=%d)" % (title, len(recs)))
    if not recs:
        return
    tags = Counter(r["tag"] for r in recs)
    for t, c in tags.most_common():
        grp = [r for r in recs if r["tag"] == t]
        clos = [r["d_before"] - r["d_after"] for r in grp]
        fr = [(r["d_before"] - r["d_after"]) / r["d_before"]
              for r in grp if r["d_before"] > 1]
        spd = [r["spd_after"] for r in grp]
        print("  %-10s n=%5d  closure(u): p25=%6.0f p50=%6.0f p75=%6.0f  "
              "frac>=200u=%5.1f%%  frac>=60%%dist=%5.1f%%  spd_after p50=%4d"
              % (t, len(grp),
                 statistics.quantiles(clos, n=4)[0] if len(clos) > 3 else 0,
                 statistics.median(clos),
                 statistics.quantiles(clos, n=4)[2] if len(clos) > 3 else 0,
                 100.0 * sum(1 for c in clos if c >= 200) / len(clos),
                 100.0 * sum(1 for f in fr if f >= 0.6) / max(1, len(fr)),
                 statistics.median(spd)))


def main():
    recs = []
    bad = False
    global_fatal = False
    for d in sys.argv[1:]:
        for p in sorted(glob.glob(os.path.join(d, "*.log"))):
            records, pairing = parse_with_result(p)
            recs.extend(records)
            report_anomalies(p, pairing)
            bad = bad or bool(pairing.anomalies) or any(
                anomaly.fatal for anomaly in pairing.telemetry_anomalies)
            global_fatal = global_fatal or pairing.global_fatal
    if global_fatal:
        return 1
    report(recs, "closure toward the anchor, by HOOKEND tag")

    na = [r for r in recs if r["tag"] == "noattach"]
    pulled = [r for r in recs if r["tag"] in ("apex", "arrived", "landed", "burst")]

    print("\n### closure histogram (units closed toward the anchor)")
    bins = [(-10000, -100), (-100, 0), (0, 50), (50, 100), (100, 200),
            (200, 300), (300, 500), (500, 800), (800, 100000)]
    print("  %-14s %10s %10s" % ("band", "noattach", "successful controls"))
    for lo, hi in bins:
        a = sum(1 for r in na if lo <= r["d_before"] - r["d_after"] < hi)
        b = sum(1 for r in pulled if lo <= r["d_before"] - r["d_after"] < hi)
        print("  %-14s %6d %4.1f%% %6d %4.1f%%"
              % ("%d..%d" % (lo, hi), a, 100.0 * a / max(1, len(na)),
                 b, 100.0 * b / max(1, len(pulled))))

    print("\n### noattach split by whether the rope demonstrably pulled")
    thr = 200.0
    strong = [r for r in na if r["d_before"] - r["d_after"] >= thr]
    weak = [r for r in na if r["d_before"] - r["d_after"] < thr]
    print("  pulled  (closed >=%.0fu toward anchor): %5d  %5.1f%%"
          % (thr, len(strong), 100.0 * len(strong) / max(1, len(na))))
    print("  did not (closed  <%.0fu)              : %5d  %5.1f%%"
          % (thr, len(weak), 100.0 * len(weak) / max(1, len(na))))
    for name, grp in (("pulled", strong), ("not pulled", weak)):
        if not grp:
            continue
        print("    %-11s d_before p50=%5.0f  spd_before p50=%4d  spd_after p50=%4d  "
              "gnd_before=%.0f%%  armsky p50=%.0f"
              % (name, statistics.median([r["d_before"] for r in grp]),
                 statistics.median([r["spd_before"] for r in grp]),
                 statistics.median([r["spd_after"] for r in grp]),
                 100.0 * sum(1 for r in grp if r["gnd_before"]) / len(grp),
                 statistics.median([r["armsky"] for r in grp])))

    print("\n### phase-1 sky-hold frames burned while arming this fire "
          "(each frame ~0.1 s of the 1.0 s deadline)")
    for t in ("noattach", "apex", "arrived", "landed", "burst"):
        g = [r["armsky"] for r in recs if r["tag"] == t]
        if not g:
            continue
        print("  %-9s n=%5d  mean=%5.1f  p50=%3.0f  p90=%4.0f  frac>=5 = %.1f%%  frac>=10 = %.1f%%"
              % (t, len(g), sum(g) / len(g), statistics.median(g),
                 statistics.quantiles(g, n=10)[8] if len(g) > 9 else 0,
                 100.0 * sum(1 for x in g if x >= 5) / len(g),
                 100.0 * sum(1 for x in g if x >= 10) / len(g)))

    print("\n### reach: distance vs what 800 u/s can cover in the deadline left")
    for t in ("noattach", "apex", "arrived", "landed", "burst"):
        g = [r for r in recs if r["tag"] == t]
        if not g:
            continue
        d = [r["d_before"] for r in g]
        # deadline left ~ 1.0 s minus the arming frames we can see (sky holds)
        left = [max(0.0, 1.0 - 0.1 * r["armsky"]) for r in g]
        over = sum(1 for r, l in zip(g, left) if r["d_before"] / 800.0 > l)
        print("  %-9s n=%5d  dist p50=%5.0f  flight p50=%.2fs  "
              "fires whose flight exceeds the visible deadline remainder: %5.1f%%"
              % (t, len(g), statistics.median(d),
                 statistics.median(d) / 800.0, 100.0 * over / len(g)))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
