#!/usr/bin/env python3
"""tripcensus.py -- where attacker trips end.

Born 2026-08-12 for stage 2: the defense-regime card says bot games
generate 3.7 stand entries/min against the humans' 9.1, and conversion
explains only part of the steal gap.  This census decomposes the
pressure gap trip by trip: a TRIP starts when a player crosses from
outside APPROACH_START of the enemy stand to inside it while closing,
and ends in exactly one of three ways -- ARRIVED (inside STAND_R),
DIED (respawn-teleport before arriving), or TURNED (distance re-opens
by TURN_MARGIN without arriving).  Per demo it reports trips/min,
arrival fraction, the died/turned split, and the median distance at
which death and turn-back happen.  Same coverage-honest denominators as
conduct.py; same rank/ratio caveat across populations.

Usage: tripcensus.py --stands <stands.json> <demo.dm2> [...]
"""
import json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import film
import conduct

DT = 0.1
APPROACH_START = 2500.0
STAND_R = 384.0
TURN_MARGIN = 600.0

def trips_for_track(tr, sx):
    """One trip per genuine approach: after any trip ends (arrival,
    turn-back, or death) a NEW trip cannot start until the player has
    left the approach zone entirely -- without this hysteresis a bot
    loitering at the stand re-triggered an "arrival" every few frames
    and the counts were inflated bounds, not rates (the first published
    run of this census carried exactly that inflation)."""
    out = []
    for seg in conduct.contiguous_segments(tr):
        cur = None                       # (start_i, min_dist, min_i)
        armed = True                     # may a new trip begin?
        for i, s in enumerate(seg):
            d = math.hypot(s[1] - sx[0], s[2] - sx[1])
            if cur is None:
                if d >= APPROACH_START:
                    armed = True
                if armed and d < APPROACH_START:
                    prev = seg[i-1] if i else None
                    pd = math.hypot(prev[1]-sx[0], prev[2]-sx[1]) if prev else 1e9
                    if pd >= d:          # closing when crossing the line
                        cur = [i, d, i]
                continue
            if d < cur[1]:
                cur[1], cur[2] = d, i
            if d <= STAND_R:
                out.append(('arrived', cur[1], (i - cur[0]) * DT))
                cur = None
                armed = False
            elif d > cur[1] + TURN_MARGIN:
                out.append(('turned', cur[1], (i - cur[0]) * DT))
                cur = None
                armed = False
        if cur is not None:              # segment ended mid-trip = death/PVS loss
            out.append(('died', cur[1], (len(seg)-1 - cur[0]) * DT))
    return out

def analyze(path, stands_all):
    d = film.walk_demo(path)
    film.cap_tracks_to_duration(d)
    st = stands_all.get(d['map'])
    if not st:
        return None
    labels = {e: film.team_of(d['skins'].get(e, '')) for e in d['tracks']}
    per = {'arrived': [], 'turned': [], 'died': []}
    obs_min = 0.0
    for ent, tr in d['tracks'].items():
        team = labels.get(ent)
        if team is None:
            continue
        obs_min += sum((len(s)-1) * DT for s in conduct.contiguous_segments(tr)) / 60.0
        enemy_stand = st['blue'] if team == 'red' else st['red']
        for kind, mind, dur in trips_for_track(tr, enemy_stand):
            per[kind].append(mind)
    n = sum(len(v) for v in per.values())
    med = lambda v: round(sorted(v)[len(v)//2], 0) if v else None
    return {
        'demo': os.path.basename(path), 'map': d['map'],
        'svrecord': d['svrecord'], 'obs_min': round(obs_min, 1),
        'trips_pm': round(n / obs_min, 3) if obs_min else None,
        'arrive_frac': round(len(per['arrived']) / n, 3) if n else None,
        'died_frac': round(len(per['died']) / n, 3) if n else None,
        'turned_frac': round(len(per['turned']) / n, 3) if n else None,
        'died_at_median': med(per['died']),
        'turned_at_median': med(per['turned']),
    }

def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('demos', nargs='+')
    ap.add_argument('--stands', required=True)
    args = ap.parse_args()
    stands_all = json.load(open(args.stands))
    for p in args.demos:
        try:
            r = analyze(p, stands_all)
            if r:
                print(json.dumps(r))
        except Exception as e:
            print(f"# skip {os.path.basename(p)}: {e}", file=sys.stderr)

if __name__ == '__main__':
    main()
