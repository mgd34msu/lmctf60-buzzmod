#!/usr/bin/env python3
"""effstat.py <log> -- progress-efficiency grade for one game log.

The owner's metric (wave 167 era): distance walked vs route progress made.
Efficiency = ms of goal-cost reduction per unit of distance actually
travelled, from the 1Hz SG telemetry. A bot running its route cleanly at
~300 u/s converts ~3.3 ms/u; near zero is motion without progress. Printed
per role so offense's number stands next to the escort's natural control.
"""
import re, sys, math, collections

names = {0: 'attack', 1: 'defend', 2: 'carry', 3: 'recover', 4: 'escort'}
stats = collections.defaultdict(lambda: {'dist': 0.0, 'prog': 0.0, 'n': 0})
prev = {}
for line in open(sys.argv[1], errors='replace'):
    m = re.match(r'SG (\S+): role=(\d+) seed=-?\d+ goal=(-?\d+) spd=\d+ '
                 r'org=\((-?\d+) (-?\d+) (-?\d+)\)', line)
    if not m:
        continue
    who, role, goal = m.group(1), int(m.group(2)), int(m.group(3))
    pos = tuple(int(x) for x in m.groups()[3:6])
    if who in prev:
        prole, pgoal, ppos = prev[who]
        if prole == role and pgoal > 0 and goal > 0:
            d = math.dist(ppos, pos)
            if d < 800:  # same life
                s = stats[role]
                s['dist'] += d
                s['prog'] += max(0, pgoal - goal)
                s['n'] += 1
    prev[who] = (role, goal, pos)

out = []
for role in sorted(stats):
    s = stats[role]
    if s['n'] < 30 or s['dist'] == 0:
        continue
    out.append(f"{names.get(role, role)}={s['prog'] / s['dist']:.2f}")
print('EFF: ' + ' '.join(out) if out else 'EFF: no samples')
