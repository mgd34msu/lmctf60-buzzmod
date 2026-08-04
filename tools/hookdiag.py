#!/usr/bin/env python3
"""
hookdiag.py -- cross-reference HOOKFIRE / HOOKEND / HOOKABORT / HOOKBITE with the
1 Hz SG telemetry lines to bucket the noattach mass.

Usage: hookdiag.py <iter-dir> [<iter-dir> ...]

No game code is touched; this only reads logs.
"""
import sys, os, re, math, glob
from collections import defaultdict, Counter

RE_SG = re.compile(
    r"^SG (\S+): role=(-?\d+) seed=(-?\d+) goal=(-?\d+) sgoal=(-?\d+) spd=(-?\d+) "
    r"org=\((-?\d+) (-?\d+) (-?\d+)\) link=(-?\d+) act=(-?\d+) hp=(-?\d+) .*gnd=(\d)")
RE_FIRE = re.compile(r"^HOOKFIRE (\S+) at \((-?\d+) (-?\d+) (-?\d+)\)")
RE_END = re.compile(r"^HOOKEND (\S+) (\w+)")
RE_ABORT = re.compile(r"^HOOKABORT (\S+) (\S+)")
RE_BITE = re.compile(r"^HOOKBITE (\S+) off=(\d+) into=(\S+) org=\((-?\d+) (-?\d+) (-?\d+)\) "
                     r"want=\((-?\d+) (-?\d+) (-?\d+)\) got=\((-?\d+) (-?\d+) (-?\d+)\)")
RE_SKY = re.compile(r"^HOOKSKYHOLD (\S+)")
RE_LAND = re.compile(r"^HOOKLAND (\S+) dist=(-?\d+) dz=(-?\d+)")
RE_DEATH = re.compile(r"^BOTDEATH: (\S+) ")
RE_CMD = re.compile(r"^CMD (\S+): fwd=(-?\d+) side=(-?\d+) up=(-?\d+) btn=(\d+)")

HOOK_SPEED = 800.0   # GRAPPLE_FIRE_HOOK_SPEED, p_weapon.c:14


class Fire:
    __slots__ = ("bot", "anchor", "org", "spd", "gnd", "act", "role", "line",
                 "tick", "end", "endline", "endtick", "aborts", "bites",
                 "maxspd", "deaths", "nsg", "skyholds", "dist", "postorg")

    def __init__(self):
        self.aborts = []
        self.bites = []
        self.deaths = 0
        self.maxspd = -1
        self.nsg = 0
        self.skyholds = 0
        self.end = None
        self.postorg = None


def parse(path):
    fires = []
    open_fire = {}        # bot -> Fire
    last_sg = {}          # bot -> dict
    tick = defaultdict(int)
    with open(path, "r", errors="replace") as fh:
        for ln, raw in enumerate(fh, 1):
            line = raw.rstrip("\n")
            if line.startswith("SG "):
                m = RE_SG.match(line)
                if not m:
                    continue
                bot = m.group(1).rstrip(":")
                d = dict(role=int(m.group(2)), spd=int(m.group(6)),
                         org=(float(m.group(7)), float(m.group(8)), float(m.group(9))),
                         act=int(m.group(11)), gnd=int(m.group(13)))
                last_sg[bot] = d
                tick[bot] += 1
                f = open_fire.get(bot)
                if f is not None:
                    f.nsg += 1
                    if d["spd"] > f.maxspd:
                        f.maxspd = d["spd"]
                    if f.postorg is None:
                        f.postorg = d["org"]
                continue
            if line.startswith("HOOKFIRE "):
                m = RE_FIRE.match(line)
                if not m:
                    continue
                bot = m.group(1)
                f = Fire()
                f.bot = bot
                f.anchor = (float(m.group(2)), float(m.group(3)), float(m.group(4)))
                sg = last_sg.get(bot)
                f.org = sg["org"] if sg else None
                f.spd = sg["spd"] if sg else None
                f.gnd = sg["gnd"] if sg else None
                f.act = sg["act"] if sg else None
                f.role = sg["role"] if sg else None
                f.line = ln
                f.tick = tick[bot]
                if f.org:
                    f.dist = math.dist(f.org, f.anchor)
                else:
                    f.dist = None
                prev = open_fire.get(bot)
                if prev is not None:      # fire with no end: overwritten
                    prev.end = "NOEND"
                    prev.endline = ln
                    prev.endtick = tick[bot]
                    fires.append(prev)
                open_fire[bot] = f
                continue
            if line.startswith("HOOKEND "):
                m = RE_END.match(line)
                if not m:
                    continue
                bot, tag = m.group(1), m.group(2)
                f = open_fire.pop(bot, None)
                if f is None:
                    continue
                f.end = tag
                f.endline = ln
                f.endtick = tick[bot]
                fires.append(f)
                continue
            if line.startswith("HOOKABORT "):
                m = RE_ABORT.match(line)
                if m:
                    f = open_fire.get(m.group(1))
                    if f is not None:
                        f.aborts.append(m.group(2))
                continue
            if line.startswith("HOOKBITE "):
                m = RE_BITE.match(line)
                if m:
                    f = open_fire.get(m.group(1))
                    if f is not None:
                        f.bites.append((int(m.group(2)), m.group(3)))
                continue
            if line.startswith("HOOKSKYHOLD "):
                m = RE_SKY.match(line)
                if m:
                    f = open_fire.get(m.group(1))
                    if f is not None:
                        f.skyholds += 1
                continue
            if line.startswith("BOTDEATH: "):
                m = RE_DEATH.match(line)
                if m:
                    f = open_fire.get(m.group(1))
                    if f is not None:
                        f.deaths += 1
                continue
    for bot, f in open_fire.items():
        f.end = "NOEND"
        f.endline = ln
        f.endtick = tick[bot]
        fires.append(f)
    return fires


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
    for d in dirs:
        for p in sorted(glob.glob(os.path.join(d, "*.log"))):
            fs = parse(p)
            allfires.extend([(os.path.basename(p), f) for f in fs])

    fires = [f for _, f in allfires]
    n = len(fires)
    print("=" * 78)
    print("total HOOKFIRE records parsed: %d  (dirs: %s)" % (n, ", ".join(dirs)))
    endc = Counter(f.end for f in fires)
    for k, v in endc.most_common():
        print("  end=%-10s %6d  %5.1f%%" % (k, v, pct(v, n)))

    na = [f for f in fires if f.end == "noattach"]
    ok = [f for f in fires if f.end in ("apex", "drop", "burst")]
    noend = [f for f in fires if f.end == "NOEND"]

    print()
    print("--- distance from bot origin (last SG before fire) to anchor ---")
    for name, grp in (("noattach", na), ("apex/drop/burst", ok), ("NOEND", noend)):
        ds = [f.dist for f in grp if f.dist is not None]
        q = quantiles(ds)
        print("%-16s n=%5d  p10=%5.0f p25=%5.0f p50=%5.0f p75=%5.0f p90=%5.0f p99=%5.0f  mean=%5.0f"
              % (name, len(ds), *(q + [sum(ds) / len(ds)])) if ds else name)

    print()
    print("--- implied bolt flight time at 800 u/s (dist/800, seconds) ---")
    for name, grp in (("noattach", na), ("apex/drop/burst", ok)):
        ds = [f.dist / HOOK_SPEED for f in grp if f.dist is not None]
        q = quantiles(ds)
        print("%-16s p50=%.2f p75=%.2f p90=%.2f p99=%.2f  frac>1.0s=%.1f%%  frac>0.5s=%.1f%%"
              % (name, q[2], q[3], q[4], q[5],
                 pct(sum(1 for x in ds if x > 1.0), len(ds)),
                 pct(sum(1 for x in ds if x > 0.5), len(ds))))

    print()
    print("--- lifetime of the fire, in SG ticks (~1 s each) ---")
    for name, grp in (("noattach", na), ("apex/drop/burst", ok), ("NOEND", noend)):
        c = Counter(f.nsg for f in grp)
        tot = len(grp)
        print("%-16s n=%5d  0 ticks(<1s)=%5.1f%%  1 tick=%5.1f%%  2 ticks=%5.1f%%  >=3=%5.1f%%"
              % (name, tot, pct(c[0], tot), pct(c[1], tot), pct(c[2], tot),
                 pct(sum(v for k, v in c.items() if k >= 3), tot)))

    print()
    print("--- log-line gap between HOOKFIRE and its end (proxy for frames) ---")
    for name, grp in (("noattach", na), ("apex/drop/burst", ok)):
        gaps = [f.endline - f.line for f in grp]
        q = quantiles(gaps)
        print("%-16s p10=%4d p25=%4d p50=%4d p75=%4d p90=%4d p99=%5d  gap<=2 = %.1f%%"
              % (name, *q, pct(sum(1 for g in gaps if g <= 2), len(gaps))))

    print()
    print("--- max SG spd observed while the fire was open (pull is a flat 800) ---")
    for name, grp in (("noattach", na), ("apex/drop/burst", ok), ("NOEND", noend)):
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
    ac = Counter()
    for f in na:
        if f.aborts:
            for a in f.aborts:
                ac["HOOKABORT " + a] += 1
        if f.bites:
            ac["HOOKBITE (rope did attach)"] += 1
        if f.deaths:
            ac["BOTDEATH during window"] += 1
    print("  noattach fires with any of these: %d / %d (%.1f%%)"
          % (sum(1 for f in na if f.aborts or f.bites or f.deaths), len(na),
             pct(sum(1 for f in na if f.aborts or f.bites or f.deaths), len(na))))
    for k, v in ac.most_common():
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
    for name, grp in (("noattach", na), ("apex/drop/burst", ok)):
        v = [math.dist(f.org, f.postorg) for f in grp if f.org and f.postorg]
        if v:
            q = quantiles(v)
            print("%-16s n=%5d p25=%5.0f p50=%5.0f p75=%5.0f p90=%5.0f" % (name, len(v), q[1], q[2], q[3], q[4]))

    print()
    print("--- skyholds burned in phase 1 before this fire ---")
    for name, grp in (("noattach", na), ("apex/drop/burst", ok)):
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
        tot, bad = per[k]
        print("  %-28s fires=%5d noattach=%5d  %5.1f%%" % (k, tot, bad, pct(bad, tot)))


if __name__ == "__main__":
    main()
