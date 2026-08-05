#!/usr/bin/env python3
"""turnsplit.py -- the honest turn gauge, split three ways.

Reads SG telemetry (1Hz per bot: org= positions, eng= combat flag,
hp= hook phase) from wave logs and reports median 1Hz heading change
for three disjoint frame classes:

  ground travel   eng=0 hp=0   the walk itself -- the polish target
  hook travel     eng=0 hp>0   swing arcs; pendulums read as churn
                               and most of it is the technique working
  combat          eng=1        deliberate dodging; noise here is skill

Usage: turnsplit.py <iter-dir> [<iter-dir> ...]
"""
import re, sys, math, glob, statistics

RE = re.compile(r'^SG (\S+): role=\d .*?org=\((-?\d+) (-?\d+) -?\d+\)'
                r'.*?hp=(\d).*?eng=(\d)', re.M)


def split(path):
    out = {'ground': [], 'hook': [], 'combat': []}
    pos, hd = {}, {}
    for m in RE.finditer(open(path, errors='replace').read()):
        b = m.group(1)
        x, y = int(m.group(2)), int(m.group(3))
        hook = m.group(4) != '0'
        eng = m.group(5) == '1'
        cls = 'combat' if eng else ('hook' if hook else 'ground')
        if b in pos:
            dx, dy = x - pos[b][0], y - pos[b][1]
            if dx * dx + dy * dy > 400:
                h = math.atan2(dy, dx)
                if b in hd:
                    d = abs(h - hd[b][0]) * 180 / math.pi
                    if d > 180:
                        d = 360 - d
                    # a sample belongs to a class only if BOTH ends do
                    if hd[b][1] == cls:
                        out[cls].append(d)
                hd[b] = (h, cls)
        pos[b] = (x, y)
    return out


def main():
    tot = {'ground': [], 'hook': [], 'combat': []}
    for d in sys.argv[1:]:
        for p in glob.glob(d + '/s0[1-9]*.log') + glob.glob(d + '/s10*.log'):
            r = split(p)
            for k in tot:
                tot[k] += r[k]
    for k in ('ground', 'hook', 'combat'):
        v = tot[k]
        if v:
            print(f"{k:8s} n={len(v):6d} median={statistics.median(v):6.1f} deg"
                  f"  p75={sorted(v)[3*len(v)//4]:6.1f}")
        else:
            print(f"{k:8s} n=0")
    print("(human demo-gauge reference: 52 deg median -- different sampler,"
          " compare classes to each other, not to 52)")


if __name__ == '__main__':
    main()
