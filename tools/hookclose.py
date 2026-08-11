#!/usr/bin/env python3
"""
hookclose.py -- did the rope actually PULL?

For every HOOKFIRE ... HOOKEND pair, compare the bot's distance to the anchor in
the SG telemetry sample immediately BEFORE the fire with the sample immediately
AFTER the end.  A rope that attached overwrites velocity with a flat 800 u/s
straight at the anchor (p_weapon.c:2088-2092), so a pull is unmistakable as
closure.  A rope that never attached leaves the bot walking.

'burst' / 'apex' / 'drop' are known-pulled controls (sg_arach.c takes those
branches only with a live rope).  'noattach' is the population under test.

Usage: hookclose.py <iter-dir> [...]
"""
import sys, os, re, math, glob, statistics
from collections import defaultdict, Counter

RE_SG = re.compile(
    r"^SG (\S+): role=(-?\d+) seed=(-?\d+) goal=(-?\d+) sgoal=(-?\d+) spd=(-?\d+) "
    r"org=\((-?\d+) (-?\d+) (-?\d+)\) link=(-?\d+) act=(-?\d+) hp=(-?\d+) .*gnd=(\d)")
RE_FIRE = re.compile(r"^HOOKFIRE (\S+) at \((-?\d+) (-?\d+) (-?\d+)\)")
RE_END = re.compile(r"^HOOKEND (\S+) (\w+)")


def parse(path):
    """Return list of dicts, one per fire that got an end and has SG samples
    on both sides."""
    out = []
    pend = {}        # bot -> record waiting for its post-end SG sample
    open_f = {}      # bot -> record between fire and end
    last_sg = {}
    armskies = defaultdict(int)
    with open(path, errors="replace") as fh:
        for ln, line in enumerate(fh, 1):
            line = line.rstrip("\n")
            if line.startswith("SG "):
                m = RE_SG.match(line)
                if not m:
                    continue
                bot = m.group(1).rstrip(":")
                org = (float(m.group(7)), float(m.group(8)), float(m.group(9)))
                spd = int(m.group(6))
                gnd = int(m.group(13))
                last_sg[bot] = (org, spd, gnd)
                r = pend.pop(bot, None)
                if r is not None:
                    r["after"] = org
                    r["spd_after"] = spd
                    r["gnd_after"] = gnd
                    r["d_after"] = math.dist(org, r["anchor"])
                    out.append(r)
                continue
            if line.startswith("HOOKSKYHOLD "):
                armskies[line.split()[1]] += 1
                continue
            if line.startswith("HOOKFIRE "):
                m = RE_FIRE.match(line)
                if not m:
                    continue
                bot = m.group(1)
                sg = last_sg.get(bot)
                if not sg:
                    continue
                anchor = (float(m.group(2)), float(m.group(3)), float(m.group(4)))
                open_f[bot] = dict(bot=bot, anchor=anchor, before=sg[0],
                                   spd_before=sg[1], gnd_before=sg[2],
                                   d_before=math.dist(sg[0], anchor),
                                   line=ln, armsky=armskies[bot])
                armskies[bot] = 0
                continue
            if line.startswith("HOOKEND "):
                m = RE_END.match(line)
                if not m:
                    continue
                bot, tag = m.group(1), m.group(2)
                r = open_f.pop(bot, None)
                if r is None:
                    continue
                r["tag"] = tag
                r["endline"] = ln
                pend[bot] = r
                continue
    return out


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
    for d in sys.argv[1:]:
        for p in sorted(glob.glob(os.path.join(d, "*.log"))):
            recs.extend(parse(p))
    report(recs, "closure toward the anchor, by HOOKEND tag")

    na = [r for r in recs if r["tag"] == "noattach"]
    pulled = [r for r in recs if r["tag"] in ("apex", "drop", "burst")]

    print("\n### closure histogram (units closed toward the anchor)")
    bins = [(-10000, -100), (-100, 0), (0, 50), (50, 100), (100, 200),
            (200, 300), (300, 500), (500, 800), (800, 100000)]
    print("  %-14s %10s %10s" % ("band", "noattach", "apex/drop/burst"))
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
    for t in ("noattach", "apex", "drop", "burst"):
        g = [r["armsky"] for r in recs if r["tag"] == t]
        if not g:
            continue
        print("  %-9s n=%5d  mean=%5.1f  p50=%3.0f  p90=%4.0f  frac>=5 = %.1f%%  frac>=10 = %.1f%%"
              % (t, len(g), sum(g) / len(g), statistics.median(g),
                 statistics.quantiles(g, n=10)[8] if len(g) > 9 else 0,
                 100.0 * sum(1 for x in g if x >= 5) / len(g),
                 100.0 * sum(1 for x in g if x >= 10) / len(g)))

    print("\n### reach: distance vs what 800 u/s can cover in the deadline left")
    for t in ("noattach", "apex", "drop", "burst"):
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


if __name__ == "__main__":
    main()
