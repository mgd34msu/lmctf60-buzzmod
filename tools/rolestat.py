#!/usr/bin/env python3
"""rolestat.py <wave.log> -- grade a wave's ROLE DISCIPLINE from telemetry.

Roles (sg_local.h): 0 ATTACK, 1 DEFEND, 2 CARRY, 3 RECOVER, 4 ESCORT.
Grades, per game:
  defense   time defenders spent inside 1500ms of their own flag field
  pressure  time attackers spent under 8000ms of the enemy flag field
  escort    carry seconds with a live escort inside 700 units
  recover   when own flag astray, recoverers' median field cost trend
  wander    fraction of samples with goal=-1 (off any useful surface)
"""
import re, sys, math, collections

pat = re.compile(r'SG (\S+): role=(\d+) seed=-?\d+ goal=(-?\d+) spd=(\d+) '
                 r'org=\(([-0-9. ]+)\)')

def grade(path):
    frames = collections.defaultdict(dict)
    idx = 0
    for l in open(path, errors='replace'):
        idx += 1
        m = pat.search(l)
        if m:
            frames[idx // 12][m.group(1)] = (
                int(m.group(2)), int(m.group(3)),
                tuple(float(x) for x in m.group(5).split()))
    n = d_ok = d_all = a_ok = a_all = 0
    esc_ok = esc_all = wander = total = 0
    for fr in frames.values():
        car = [(g, p) for r, g, p in fr.values() if r == 2]
        esc = [p for r, g, p in fr.values() if r == 4]
        for r, g, p in fr.values():
            total += 1
            if g < 0:
                wander += 1
                continue
            if r == 1:
                d_all += 1
                d_ok += g < 1500
            elif r == 0:
                a_all += 1
                a_ok += g < 8000
        if car:
            esc_all += 1
            if esc and min(math.dist(car[0][1], e) for e in esc) < 700:
                esc_ok += 1
    name = path.split('/')[-1]
    pc = lambda a, b: f"{100*a//b}%" if b else "-"
    print(f"{name}: defense {pc(d_ok,d_all)} ({d_all}s) | "
          f"pressure {pc(a_ok,a_all)} ({a_all}s) | "
          f"escorted-carry {pc(esc_ok,esc_all)} ({esc_all}s) | "
          f"wander {pc(wander,total)}")

if __name__ == '__main__':
    for p in sys.argv[1:]:
        grade(p)
