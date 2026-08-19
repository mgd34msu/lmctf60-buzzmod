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
            episodes.append({'dur': dur, 'x': sum(xs) / len(xs),
                             'y': sum(ys) / len(ys),
                             'z': sum(zs) / len(zs)})
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
        })
    out = [{'x': round(c['sx'] / c['n'], 1), 'y': round(c['sy'] / c['n'], 1),
            'z': round(c['sz'] / c['n'], 1), 'evidence_count': c['n'],
            'duration_s': round(c['dur'], 2)} for c in clusters]
    out.sort(key=lambda c: (-c['evidence_count'], -c['duration_s'],
                            c['x'], c['y'], c['z']))
    return out


def analyze(path, stands_all, map_identities=None):
    d = film.walk_demo(path)
    film.cap_tracks_to_duration(d)
    labels = {ent: film.team_of(d['skins'].get(ent, '')) for ent in d['tracks']}
    labels = {ent: lab for ent, lab in labels.items() if lab is not None}
    stands = stands_all.get(d['map'])

    tot_obs = 0.0
    stall_s = 0.0
    post_s = 0.0
    durations = []
    points = []
    for ent, tr in d['tracks'].items():
        team = labels.get(ent)
        if team is None:
            continue
        own_stand = stands.get(team) if stands else None
        yaw_map = d['yaws'].get(ent, {})
        for seg in conduct.contiguous_segments(tr):
            tot_obs += (len(seg) - 1) * DT
            s_s, p_s, episodes = stall_and_post_events(seg, yaw_map, own_stand)
            stall_s += s_s
            post_s += p_s
            for e in episodes:
                durations.append(e['dur'])
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
        # Never aggregate these over maps: their coordinates are meaningful
        # only under this row's map identity.
        'snag_clusters': cluster_points(points),
        'top_stall_locations': cluster_points(points)[:3],
        '_durations': durations,      # for --compare's exact pooled median;
        '_obs_min': obs_min,          # stripped before per-demo printing
    }
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
