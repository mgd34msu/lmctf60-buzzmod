#!/usr/bin/env python3
"""Compare seed-center steering with route lookahead on recorded paths.

The tool localizes serverrecord positions to a RUNE and reports retargeting,
body turns, route turns, and counterfactual lookahead-heading churn.
"""
import sys, os, math, collections
import statistics as st

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import botkin
from demorune import SeedGrid

try:
    import runeio
except ModuleNotFoundError:
    from tools import runeio

LOOK = float(os.environ.get('SS_LOOK', '250'))


def load_rune(path):
    artifact = runeio.read(path)
    return [seed.origin for seed in artifact.seeds], artifact.links


def ang_diff(a, b):
    return (a - b + 180.0) % 360.0 - 180.0


def churn_1hz(series):
    """botkin's 1Hz turn gauge on an arbitrary (tick_index, angle) series:
    take the first sample of each 10-tick second, diff consecutive."""
    by_sec = collections.defaultdict(list)
    for t, a in series:
        by_sec[t // 10].append(a)
    ks = sorted(by_sec)
    vals = [by_sec[k][0] for k in ks]
    turns = [abs(ang_diff(vals[i], vals[i - 1])) for i in range(1, len(vals))
             if ks[i] - ks[i - 1] == 1]
    return turns


def cut_depth(p, target, verts):
    """max perpendicular distance from the chord p->target to the polyline
    vertices lying between them -- how deep the pure-pursuit shortcut cuts
    inside the corner the seed chain actually walks."""
    ax, ay = p
    bx, by = target
    L2 = (bx - ax) ** 2 + (by - ay) ** 2
    if L2 < 1.0:
        return 0.0
    worst = 0.0
    for (vx, vy) in verts:
        t = ((vx - ax) * (bx - ax) + (vy - ay) * (by - ay)) / L2
        if t <= 0.0 or t >= 1.0:
            continue
        px, py = ax + (bx - ax) * t, ay + (by - ay) * t
        d = math.hypot(vx - px, vy - py)
        if d > worst:
            worst = d
    return worst


def pursuit_point(p, chain_pts, look):
    """Walk the polyline chain_pts (upcoming seed centers, in order) from the
    projection of p, and return the point `look` units of arc ahead."""
    if not chain_pts:
        return None
    rem = look
    cur = p
    for q in chain_pts:
        seg = math.hypot(q[0] - cur[0], q[1] - cur[1])
        if seg <= 1e-6:
            continue
        if seg >= rem:
            f = rem / seg
            return (cur[0] + (q[0] - cur[0]) * f, cur[1] + (q[1] - cur[1]) * f)
        rem -= seg
        cur = q
    return cur                      # chain ran out: aim at its end


def analyse(track, grid, seeds, out):
    rows = []
    for i in range(1, len(track)):
        f0, x0, y0, z0, _ = track[i - 1]
        f1, x1, y1, z1, _ = track[i]
        if f1 - f0 != 1:
            rows.append(None)
            continue
        vx, vy = (x1 - x0) / 0.1, (y1 - y0) / 0.1
        hs = math.hypot(vx, vy)
        if hs < 60.0:
            rows.append(None)
            continue
        sd = grid.nearest((x1, y1, z1), maxr=400.0)
        if sd < 0:
            rows.append(None)
            continue
        rows.append((f1, x1, y1, z1, math.degrees(math.atan2(vy, vx)), hs, sd))

    n = len(rows)
    cross = [False] * n
    prev = None
    seedseq = []
    for i, r in enumerate(rows):
        if r is None:
            prev = None
            continue
        if prev is not None and r[6] != prev:
            cross[i] = True
            seedseq.append((i, r[6]))
        prev = r[6]

    # ---- A) retarget rate ----------------------------------------------
    if seedseq:
        gaps = [seedseq[k][0] - seedseq[k - 1][0]
                for k in range(1, len(seedseq))]
        out['dwell'] += [g for g in gaps if g <= 60]
        for k in range(1, len(seedseq)):
            a, b = seeds[seedseq[k - 1][1]], seeds[seedseq[k][1]]
            if seedseq[k][0] - seedseq[k - 1][0] <= 20:
                out['leg'].append(math.hypot(b[0] - a[0], b[1] - a[1]))

    # ---- B) chain turn vs body turn at retargets ------------------------
    for k in range(1, len(seedseq) - 1):
        i0, s0 = seedseq[k - 1]
        i1, s1 = seedseq[k]
        i2, s2 = seedseq[k + 1]
        if i2 - i0 > 40:
            continue
        p0, p1, p2 = seeds[s0], seeds[s1], seeds[s2]
        a1 = math.degrees(math.atan2(p1[1] - p0[1], p1[0] - p0[0]))
        a2 = math.degrees(math.atan2(p2[1] - p1[1], p2[0] - p1[0]))
        chain = abs(ang_diff(a2, a1))
        leg = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
        ha = rows[i1 - 2] if i1 - 2 >= 0 and rows[i1 - 2] else None
        hb = rows[i1 + 2] if i1 + 2 < n and rows[i1 + 2] else None
        if ha and hb:
            out['chain'].append((chain, abs(ang_diff(hb[4], ha[4])), leg))

    # ---- C) counterfactual commanded headings ---------------------------
    # forward seed chain from each tick: the distinct seeds still to come
    nxt_idx = [None] * n            # index into seedseq of the next retarget
    ci = 0
    for i in range(n):
        while ci < len(seedseq) and seedseq[ci][0] <= i:
            ci += 1
        nxt_idx[i] = ci

    body, servo, pursuit, servo_dist = [], [], [], []
    servo2, servo3 = [], []
    pursuit_safe = []
    for i in range(n):
        r = rows[i]
        if r is None:
            continue
        j = nxt_idx[i]
        if j is None or j >= len(seedseq):
            continue
        # contiguity: the next few retargets must be within ~3s, else the
        # track is broken (death, teleport, out of PVS)
        if seedseq[j][0] - i > 30:
            continue
        p = (r[1], r[2])
        s_next = seeds[seedseq[j][1]]
        d = math.hypot(s_next[0] - p[0], s_next[1] - p[1])
        body.append((r[0], r[4]))
        servo.append((r[0], math.degrees(math.atan2(s_next[1] - p[1],
                                                    s_next[0] - p[0]))))
        servo_dist.append(d)
        # sg_lookahead's actual behaviour: snap the aim ONE more seed down
        # the chain (and, for reference, two more)
        for k, acc in ((1, servo2), (2, servo3)):
            if j + k < len(seedseq) and seedseq[j + k][0] - i <= 45:
                sk = seeds[seedseq[j + k][1]]
                acc.append((r[0], math.degrees(math.atan2(sk[1] - p[1],
                                                          sk[0] - p[0]))))
        chain_pts = []
        for k in range(j, min(j + 8, len(seedseq))):
            if seedseq[k][0] - i > 60:
                break
            sp = seeds[seedseq[k][1]]
            chain_pts.append((sp[0], sp[1]))
        pp = pursuit_point(p, chain_pts, LOOK)
        if pp:
            pursuit.append((r[0], math.degrees(math.atan2(pp[1] - p[1],
                                                          pp[0] - p[0]))))
            out['cut'].append(cut_depth(p, pp, chain_pts))
            # the SAFE variant: back the pursuit point down the polyline
            # until the chord stays inside a half-player-box corridor of it
            # (the geometric stand-in for the player-box trace the game
            # would run). This is the version that cannot cut a corner.
            LL = LOOK
            sp = pp
            while LL > 48.0 and cut_depth(p, sp, chain_pts) > 16.0:
                LL *= 0.75
                sp = pursuit_point(p, chain_pts, LL)
            out['safeL'].append(LL)
            pursuit_safe.append((r[0],
                                 math.degrees(math.atan2(sp[1] - p[1],
                                                         sp[0] - p[0]))))
    out['psafe1hz'] += churn_1hz(pursuit_safe)
    for i in range(1, len(pursuit_safe)):
        if pursuit_safe[i][0] - pursuit_safe[i - 1][0] == 1:
            out['psafe_tick'].append(abs(ang_diff(
                pursuit_safe[i][1], pursuit_safe[i - 1][1])))
    out['servo2_1hz'] += churn_1hz(servo2)
    out['servo3_1hz'] += churn_1hz(servo3)
    for nm, ser in (('servo2', servo2), ('servo3', servo3)):
        for i in range(1, len(ser)):
            if ser[i][0] - ser[i - 1][0] == 1:
                out[nm + '_tick'].append(abs(ang_diff(ser[i][1],
                                                      ser[i - 1][1])))
    out['body1hz'] += churn_1hz(body)
    out['servo1hz'] += churn_1hz(servo)
    out['pursuit1hz'] += churn_1hz(pursuit)
    out['servo_dist'] += servo_dist
    # per-tick churn too
    for name, ser in (('body', body), ('servo', servo), ('pursuit', pursuit)):
        for i in range(1, len(ser)):
            if ser[i][0] - ser[i - 1][0] == 1:
                out[name + '_tick'].append(abs(ang_diff(ser[i][1],
                                                        ser[i - 1][1])))


KEYS = ('dwell', 'leg', 'chain', 'body1hz', 'servo1hz', 'pursuit1hz',
        'servo_dist', 'body_tick', 'servo_tick', 'pursuit_tick',
        'servo2_1hz', 'servo3_1hz', 'servo2_tick', 'servo3_tick', 'cut', 'safeL', 'psafe1hz',
        'psafe_tick')


def main():
    rune_dir = sys.argv[1]
    demos = sys.argv[2:]
    per_map = collections.defaultdict(lambda: {k: [] for k in KEYS})
    runes = {}
    for path in demos:
        w = botkin.walk(path)
        mp = w['map']
        if not mp or not w['svrecord']:
            print(f"  {os.path.basename(path)}: skip (map={mp} "
                  f"svrec={w['svrecord']})", file=sys.stderr)
            continue
        rp = os.path.join(rune_dir, mp + '.rune')
        if not os.path.exists(rp):
            print(f"  {os.path.basename(path)}: no rune for {mp}",
                  file=sys.stderr)
            continue
        if mp not in runes:
            seeds, links = load_rune(rp)
            runes[mp] = (seeds, links, SeedGrid(seeds))
        seeds, links, grid = runes[mp]
        out = per_map[mp]
        names = {nn: w['skins'].get(nn - 1, '?').split('\\')[0]
                 for nn in w['tracks']}
        ntr = 0
        for num, tr in sorted(w['tracks'].items()):
            if names.get(num, '?') == '?' or len(tr) < 100:
                continue
            analyse(tr, grid, seeds, out)
            ntr += 1
        print(f"  {os.path.basename(path)}: map={mp} seeds={len(seeds)} "
              f"links={len(links)} named-tracks={ntr} frames={w['frames']}",
              file=sys.stderr)

    for mp, out in sorted(per_map.items()):
        print(f"\n===== {mp} =====")
        if out['dwell']:
            dw = out['dwell']
            print(f"  A) retarget rate: median dwell {st.median(dw):.0f} ticks "
                  f"({st.median(dw)/10:.2f}s), mean {st.mean(dw):.2f} ticks "
                  f"-> {10.0/st.mean(dw):.2f} aim retargets/sec")
            print(f"     median leg between consecutive seed centers: "
                  f"{st.median(out['leg']):.0f}u  (n={len(out['leg'])})")
            sd = sorted(out['servo_dist'])
            def pc(q):
                return sd[min(len(sd) - 1, int(q * len(sd)))]
            print(f"     distance to the servo target when steering: "
                  f"p10={pc(.10):.0f} p25={pc(.25):.0f} med={pc(.50):.0f} "
                  f"p75={pc(.75):.0f} p90={pc(.90):.0f} u   "
                  f"share under 96u (the fan's probe reach) = "
                  f"{100.0*sum(1 for x in sd if x < 96)/len(sd):.0f}%")
        ch = out['chain']
        if ch:
            print("\n  B) chain turn (seed polyline) vs body turn "
                  "(+-0.2s around the retarget):")
            for lo, hi in [(0, 5), (5, 15), (15, 30), (30, 60), (60, 180)]:
                v = [(c, b, l) for c, b, l in ch if lo <= c < hi]
                if len(v) < 15:
                    continue
                print(f"     chain {lo:3d}-{hi:3d} deg  n={len(v):6d}  "
                      f"med chain={st.median([x[0] for x in v]):5.1f}  "
                      f"med body={st.median([x[1] for x in v]):5.1f}  "
                      f"med leg={st.median([x[2] for x in v]):5.0f}u")
            straight = [b for c, b, l in ch if c < 15]
            if straight:
                print(f"     -> straight chain joints (<15 deg, "
                      f"n={len(straight)}): median body turn "
                      f"{st.median(straight):.1f} deg over 0.4s = "
                      f"{st.median(straight)/0.4:.0f} deg/s")
        print("\n  C) counterfactual commanded heading, same trajectories "
              f"(pure-pursuit L={LOOK:.0f}u):")
        for label, k1, kt in (('body (measured)', 'body1hz', 'body_tick'),
                              ('servo = next seed center', 'servo1hz',
                               'servo_tick'),
                              ('servo+1 (what sg_lookahead does)',
                               'servo2_1hz', 'servo2_tick'),
                              ('servo+2 seeds', 'servo3_1hz', 'servo3_tick'),
                              ('pursuit = L ahead on polyline', 'pursuit1hz',
                               'pursuit_tick'),
                              ('pursuit, corridor-guarded', 'psafe1hz',
                               'psafe_tick')):
            v = out[k1]
            t = out[kt]
            if not v:
                continue
            rev = 100.0 * sum(1 for x in v if x > 90) / len(v)
            print(f"     {label:32s} 1Hz med={st.median(v):6.1f} deg  "
                  f"mean={st.mean(v):6.1f}  reversal={rev:5.1f}%  "
                  f"| per-tick med={st.median(t):5.2f} deg "
                  f"({st.median(t)*10:5.1f} deg/s)  n={len(v)}")
        sl = out['safeL']
        if sl:
            sl = sorted(sl)
            print(f"     guarded lookahead actually used: med={sl[len(sl)//2]:.0f}u "
                  f"p25={sl[len(sl)//4]:.0f} p75={sl[3*len(sl)//4]:.0f} "
                  f"(cap {LOOK:.0f})")
        cu = sorted(out['cut'])
        if cu:
            def q(x):
                return cu[min(len(cu)-1, int(x*len(cu)))]
            print(f"     corner-cut depth of the pursuit chord vs the seed "
                  f"polyline: med={q(.5):.0f} p90={q(.9):.0f} "
                  f"p99={q(.99):.0f} max={cu[-1]:.0f} u   "
                  f"share over 16u (half a player box) = "
                  f"{100.0*sum(1 for x in cu if x > 16)/len(cu):.1f}%")


if __name__ == '__main__':
    main()
