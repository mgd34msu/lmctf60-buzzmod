#!/usr/bin/env python3
"""effstat.py <log> -- progress-efficiency grades for one game log.

The owner's metric: distance walked vs route progress made, from the 1Hz SG
telemetry, measured against sgoal= (the static stand field -- a cost that
cannot change unless the body moves) with goal= as fallback on old logs.
A bot running its route cleanly at ~300 u/s converts ~3.3 ms/u; near zero
is motion without progress.

Output: one EFF line per role (aggregate, trend-comparable across waves)
and one BOT line per individual -- efficiency, mean/max speed, distance --
because a fleet average hides exactly the laggard or the star the owner
wants found.
"""
import re, sys, math, collections

names = {0: 'attack', 1: 'defend', 2: 'carry', 3: 'recover', 4: 'escort'}
roles = collections.defaultdict(lambda: {'dist': 0.0, 'prog': 0.0, 'n': 0})
bots = collections.defaultdict(lambda: {'dist': 0.0, 'prog': 0.0, 'n': 0,
                                        'spd_sum': 0.0, 'spd_max': 0.0,
                                        'role_n': collections.Counter()})
prev = {}
for line in open(sys.argv[1], errors='replace'):
    m = re.match(r'SG (\S+): role=(\d+) seed=-?\d+ goal=(-?\d+)'
                 r'(?: sgoal=(-?\d+))? spd=(\d+) '
                 r'org=\((-?\d+) (-?\d+) (-?\d+)\)', line)
    if not m:
        continue
    who, role = m.group(1), int(m.group(2))
    goal = int(m.group(4)) if m.group(4) is not None else int(m.group(3))
    spd = int(m.group(5))
    pos = tuple(int(x) for x in m.groups()[5:8])
    b = bots[who]
    b['spd_sum'] += spd
    b['spd_max'] = max(b['spd_max'], spd)
    b['role_n'][role] += 1
    if who in prev:
        prole, pgoal, ppos = prev[who]
        if prole == role and pgoal > 0 and goal > 0:
            d = math.dist(ppos, pos)
            if d < 800:  # same life
                for s in (roles[role], b):
                    s['dist'] += d
                    s['prog'] += max(0, pgoal - goal)
                    s['n'] += 1
    prev[who] = (role, goal, pos)

out = []
for role in sorted(roles):
    s = roles[role]
    if s['n'] < 30 or s['dist'] == 0:
        continue
    out.append(f"{names.get(role, role)}={s['prog'] / s['dist']:.2f}")
print('EFF: ' + (' '.join(out) if out else 'no samples'))
for who in sorted(bots, key=lambda w: -(bots[w]['prog'] / bots[w]['dist']
                                        if bots[w]['dist'] else 0)):
    b = bots[who]
    if b['n'] < 30:
        continue
    eff = b['prog'] / b['dist'] if b['dist'] else 0
    mainrole = names.get(b['role_n'].most_common(1)[0][0], '?')
    print(f"BOT: {who:12s} eff={eff:4.2f} avg_spd={b['spd_sum'] / max(sum(b['role_n'].values()), 1):5.0f} "
          f"max_spd={b['spd_max']:4.0f} dist={b['dist']:7.0f} role={mainrole}")
