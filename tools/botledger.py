#!/usr/bin/env python3
"""botledger.py <wave> <log> [<log> ...] -- append per-bot per-wave rows.

Individual measurement, kept individual: one row per bot per game, written
to tools/botledger.csv. Columns: wave, server, bot, main_role, eff,
avg_spd, max_spd, dist, prog_ms, steals, caps, kills, deaths.

eff measures against sgoal= (static stand field; goal= fallback on old
logs). Kill feed parsed for the combat columns. The CSV is the era's
longitudinal record: any bot's line can be traced across every wave it
ever played.
"""
import re, sys, os, math, collections, csv

names = {0: 'attack', 1: 'defend', 2: 'carry', 3: 'recover', 4: 'escort'}
wave = sys.argv[1]
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'botledger.csv')
newfile = not os.path.exists(out)
rows = []

for path in sys.argv[2:]:
    server = os.path.basename(path).rsplit('.', 1)[0]
    bots = collections.defaultdict(lambda: {
        'dist': 0.0, 'prog': 0.0, 'n': 0, 'spd_sum': 0.0, 'spd_n': 0,
        'spd_max': 0.0, 'roles': collections.Counter(),
        'steals': 0, 'caps': 0, 'kills': 0, 'deaths': 0})
    prev = {}
    for line in open(path, errors='replace'):
        m = re.match(r'SG (\S+): role=(\d+) seed=-?\d+ goal=(-?\d+)'
                     r'(?: sgoal=(-?\d+))? spd=(\d+) '
                     r'org=\((-?\d+) (-?\d+) (-?\d+)\)', line)
        if m:
            who, role = m.group(1), int(m.group(2))
            goal = int(m.group(4)) if m.group(4) is not None else int(m.group(3))
            spd = int(m.group(5))
            pos = tuple(int(x) for x in m.groups()[5:8])
            b = bots[who]
            b['spd_sum'] += spd; b['spd_n'] += 1
            b['spd_max'] = max(b['spd_max'], spd)
            b['roles'][role] += 1
            if who in prev:
                prole, pgoal, ppos = prev[who]
                if prole == role and pgoal > 0 and goal > 0:
                    d = math.dist(ppos, pos)
                    if d < 800:
                        b['dist'] += d
                        b['prog'] += max(0, pgoal - goal)
                        b['n'] += 1
            prev[who] = (role, goal, pos)
            continue
        m = re.match(r'(\S+) stole the (?:red|blue) flag', line)
        if m:
            bots[m.group(1)]['steals'] += 1
            continue
        m = re.match(r'(\S+) captured the (?:red|blue) flag', line)
        if m:
            bots[m.group(1)]['caps'] += 1
            continue
        m = re.match(r'(\S+) was \w+(?: \w+)? by (\S+)', line)
        if m:
            bots[m.group(1)]['deaths'] += 1
            bots[m.group(2)]['kills'] += 1

    for who, b in bots.items():
        if b['spd_n'] < 30:
            continue
        eff = b['prog'] / b['dist'] if b['dist'] else 0.0
        mainrole = names.get(b['roles'].most_common(1)[0][0], '?') \
            if b['roles'] else '?'
        rows.append([wave, server, who, mainrole, f"{eff:.3f}",
                     f"{b['spd_sum'] / b['spd_n']:.0f}",
                     f"{b['spd_max']:.0f}", f"{b['dist']:.0f}",
                     f"{b['prog']:.0f}", b['steals'], b['caps'],
                     b['kills'], b['deaths']])

with open(out, 'a', newline='') as f:
    w = csv.writer(f)
    if newfile:
        w.writerow(['wave', 'server', 'bot', 'main_role', 'eff', 'avg_spd',
                    'max_spd', 'dist', 'prog_ms', 'steals', 'caps',
                    'kills', 'deaths'])
    w.writerows(rows)
print(f"botledger: {len(rows)} rows appended for wave {wave}")
