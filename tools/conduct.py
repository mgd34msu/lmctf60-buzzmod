#!/usr/bin/env python3
"""Measure flag-stand approach and carry behavior from demos.

Approaches are entries into a fixed stand radius per observed stand-minute.
Close conversion is a qualifying carry start within the configured delay after
an approach. Results are diagnostic unless reconciled with authoritative CTF
events and exact build receipts.
"""

import argparse
import glob as globmod
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import film

DT = 0.1                     # server frame step in every demo shape we read

GRIND_WIN_S = 1.5            # window length
GRIND_SPEED_UPS = 90.0       # gross path speed floor: "working hard"
GRIND_NET_UNITS = 28.0       # net displacement ceiling: "going nowhere"
SPIN_DPS = 540.0             # sustained yaw rate floor
SPIN_WIN_S = 0.6
SPIN_STILL_UPS = 60.0        # spin only counts when near-stationary
STAND_R = 384.0              # approach / guard radius around a stand
STAND_OBS_R = 1024.0         # stand-second observed if any sample this near
STEAL_LOOKAHEAD_S = 3.0
MIN_SEG_S = 2.0              # ignore visibility slivers shorter than this


def contiguous_segments(track):
    """Split a track into runs of consecutive frames (client demos drop
    entities whenever they leave the PVS), then split again at respawn
    teleports so a death does not read as a 600-unit sprint."""
    if not track:
        return []
    segs = []
    cur = [track[0]]
    for prev, samp in zip(track, track[1:]):
        step = samp[0] - prev[0]
        jump = math.hypot(samp[1] - prev[1], samp[2] - prev[2])
        if step == 1 and jump <= film.TELEPORT_UNITS:
            cur.append(samp)
        else:
            segs.append(cur)
            cur = [samp]
    segs.append(cur)
    return [s for s in segs if (len(s) - 1) * DT >= MIN_SEG_S]


def grind_windows(seg):
    """Yield (start_idx, reversals) for every grind window in one
    contiguous segment: gross path speed above GRIND_SPEED_UPS while net
    displacement stays under GRIND_NET_UNITS.  THE single detector --
    every analysis that measures going-nowhere movement imports this
    rather than re-typing the loop (two scratch scripts once carried
    private copies; that is how instruments drift)."""
    n = len(seg)
    win = max(2, int(round(GRIND_WIN_S / DT)))
    dx = [(seg[i + 1][1] - seg[i][1], seg[i + 1][2] - seg[i][2])
          for i in range(n - 1)]
    step = [math.hypot(a, b) for a, b in dx]
    i = 0
    while i + win < n:
        gross = sum(step[i:i + win])
        net = math.hypot(seg[i + win][1] - seg[i][1],
                         seg[i + win][2] - seg[i][2])
        if gross / (win * DT) > GRIND_SPEED_UPS and net < GRIND_NET_UNITS:
            rev = 0
            for j in range(i + 1, i + win):
                ax, ay = dx[j - 1]
                bx, by = dx[j]
                if ax * bx + ay * by < 0:
                    rev += 1
            yield i, rev
            i += win              # windows do not overlap once triggered
        else:
            i += 1


def conduct_track(track, yaw_by_frame):
    """(observed_s, grind_s, grind_reversals, spin_s) for one entity."""
    obs_s = grind_s = spin_s = 0.0
    reversals = 0
    win = max(2, int(round(GRIND_WIN_S / DT)))
    spin_win = max(2, int(round(SPIN_WIN_S / DT)))
    for seg in contiguous_segments(track):
        n = len(seg)
        obs_s += (n - 1) * DT
        step = [math.hypot(seg[i + 1][1] - seg[i][1],
                           seg[i + 1][2] - seg[i][2]) for i in range(n - 1)]
        for _si, rev in grind_windows(seg):
            grind_s += win * DT
            reversals += rev
        # -------- spin: sustained yaw rate while near-stationary
        yaws = [yaw_by_frame.get(s[0]) for s in seg]
        run = 0
        for i in range(1, n):
            if yaws[i] is None or yaws[i - 1] is None:
                run = 0
                continue
            dyaw = abs((yaws[i] - yaws[i - 1] + 180.0) % 360.0 - 180.0)
            slow = step[i - 1] / DT < SPIN_STILL_UPS
            if dyaw / DT >= SPIN_DPS and slow:
                run += 1
                if run >= spin_win:
                    spin_s += DT
            else:
                run = 0
    return obs_s, grind_s, reversals, spin_s


def defense_card(d, labels, stands_xy):
    """approach/steal/guard numbers for one demo against fixture stands.
    labels: entnum -> 'red'/'blue' (team_of on skins)."""
    tracks = d['tracks']
    frames = d['frames']
    out = {}
    for team, sx in stands_xy.items():
        # observed stand-seconds: any sample within STAND_OBS_R that frame
        observed = set()
        for ent, tr in tracks.items():
            for f, x, y, z, _ in tr:
                if math.hypot(x - sx[0], y - sx[1]) <= STAND_OBS_R:
                    observed.add(f)
        obs_s = len(observed) * DT
        if obs_s < 30.0:
            continue                      # stand effectively unobserved
        approaches = 0
        guarded = 0
        guarded2 = 0
        inside_prev = {}
        entries = []                      # (frame, ent) of enemy entries
        by_frame = {ent: {s[0]: s for s in tr}
                    for ent, tr in tracks.items()}
        for f in sorted(observed):
            ndef = 0
            for ent, tr in tracks.items():
                samp = by_frame[ent].get(f)
                if samp is None:
                    inside_prev.pop((ent, team), None)
                    continue
                near = math.hypot(samp[1] - sx[0], samp[2] - sx[1]) <= STAND_R
                lab = labels.get(ent)
                if near and lab == team:
                    ndef += 1
                if lab is not None and lab != team:
                    was = inside_prev.get((ent, team), False)
                    if near and not was:
                        approaches += 1
                        entries.append((f, ent))
                    inside_prev[(ent, team)] = near
            if ndef >= 1:
                guarded += 1
            if ndef >= 2:
                guarded2 += 1
        # steal conversion: an entry followed by that same player starting
        # a carry of THIS stand's flag within the lookahead
        windows, _ = film.carry_windows(tracks, labels)
        steals = 0
        for f, ent in entries:
            for w in windows:
                if w['entnum'] == ent and w['color'] == team and \
                   0 <= w['t0'] - f * DT <= STEAL_LOOKAHEAD_S:
                    steals += 1
                    break
        out[team] = {
            'obs_min': obs_s / 60.0,
            'approach_pm': approaches / (obs_s / 60.0),
            'steal_conv': (steals / approaches) if approaches else None,
            'guard_frac': guarded / len(observed),
            'guard2_frac': guarded2 / len(observed),
        }
    return out


def analyze(path, stands_all):
    d = film.walk_demo(path)
    film.cap_tracks_to_duration(d)      # mutates in place; returns metadata
    labels = {ent: film.team_of(d['skins'].get(ent, ''))
              for ent in d['tracks']}
    labels = {ent: lab for ent, lab in labels.items() if lab is not None}
    tot_obs = tot_grind = tot_spin = 0.0
    tot_rev = 0
    for ent, tr in d['tracks'].items():
        if labels.get(ent) is None:
            continue                      # spectators / unknown skins
        o, g, r, s = conduct_track(tr, d['yaws'].get(ent, {}))
        tot_obs += o
        tot_grind += g
        tot_rev += r
        tot_spin += s
    row = {
        'demo': os.path.basename(path),
        'map': d['map'],
        'svrecord': d['svrecord'],
        'players_observed_min': tot_obs / 60.0,
        'grind_spm': (tot_grind / (tot_obs / 60.0)) if tot_obs else None,
        'revrate': (tot_rev / tot_grind) if tot_grind else 0.0,
        'spin_spm': (tot_spin / (tot_obs / 60.0)) if tot_obs else None,
    }
    stands = stands_all.get(d['map'])
    if stands is not None:
        row['defense'] = defense_card(
            d, labels, {t: v for t, v in stands.items()})
    return row


def pool(rows):
    """Weighted pool by observed minutes; defense pooled by stand-min."""
    w = sum(r['players_observed_min'] for r in rows) or 1.0
    def avg(k):
        xs = [(r[k], r['players_observed_min'])
              for r in rows if r.get(k) is not None]
        return sum(v * m for v, m in xs) / (sum(m for _, m in xs) or 1.0)
    out = {'demos': len(rows), 'observed_min': round(w, 1),
           'grind_spm': round(avg('grind_spm'), 3),
           'revrate': round(avg('revrate'), 3),
           'spin_spm': round(avg('spin_spm'), 4)}
    dm = 0.0
    acc = {'approach_pm': 0.0, 'guard_frac': 0.0, 'guard2_frac': 0.0}
    sc_n = sc_d = 0
    for r in rows:
        for team, c in (r.get('defense') or {}).items():
            m = c['obs_min']
            dm += m
            for k in acc:
                acc[k] += c[k] * m
            if c['steal_conv'] is not None:
                sc_n += c['steal_conv'] * c['approach_pm'] * m
                sc_d += c['approach_pm'] * m
    if dm > 0:
        out['defense'] = {
            'stand_obs_min': round(dm, 1),
            'approach_pm': round(acc['approach_pm'] / dm, 3),
            'steal_conv': round(sc_n / sc_d, 3) if sc_d else None,
            'guard_frac': round(acc['guard_frac'] / dm, 3),
            'guard2_frac': round(acc['guard2_frac'] / dm, 3),
        }
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('demos', nargs='*')
    ap.add_argument('--scalars', action='store_true')
    ap.add_argument('--compare', action='store_true')
    ap.add_argument('--human', action='append', default=[])
    ap.add_argument('--bot', action='append', default=[])
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, 'stands.json')) as f:
        stands_all = json.load(f)

    def run(globs_or_paths):
        rows = []
        for g in globs_or_paths:
            for p in sorted(globmod.glob(os.path.expanduser(g))) or [g]:
                try:
                    rows.append(analyze(p, stands_all))
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
        print(json.dumps(r))


if __name__ == '__main__':
    main()
