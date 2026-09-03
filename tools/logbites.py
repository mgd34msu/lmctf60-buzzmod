#!/usr/bin/env python3
"""logbites.py SERVER_LOG... -o MAPS_DIR

Rope bites of the human players from server logs written with sg_debug 1
(the SGHUMAN trace logs "bite=(x y z)" while a rope holds).  Appended to
MAPS_DIR/<map>.bites like demobites.py, deduplicated on a 16-unit grid.
"""
import sys, os, re, collections, argparse

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('logs', nargs='+'); ap.add_argument('-o', '--out', required=True)
    a = ap.parse_args()
    per = collections.defaultdict(dict)
    for path in a.logs:
        cur = None; last = {}
        for l in open(path, errors='replace'):
            m = re.search(r'rune ready (\w+)', l)
            if m: cur = m[1]; continue
            m = re.match(r'^SGHUMAN (\S+) at=\(([-\d]+) ([-\d]+) ([-\d]+)\)', l)
            if m: last[m[1]] = m.groups()[1:]; continue
            m = re.match(r'^SGHUMAN (\S+) bite=\(([-\d]+) ([-\d]+) ([-\d]+)\)', l)
            if m and cur and m[1] in last:
                k = (int(m[2]) // 16, int(m[3]) // 16, int(m[4]) // 16)
                per[cur].setdefault(k, last[m[1]] + (m[2], m[3], m[4]))
    os.makedirs(a.out, exist_ok=True)
    for mp, d in sorted(per.items()):
        p = os.path.join(a.out, mp + '.bites'); have = set()
        if os.path.exists(p):
            for l in open(p):
                f = l.split()
                if len(f) == 6: have.add(tuple(int(float(x)) // 16 for x in f[3:6]))
        new = 0
        with open(p, 'a') as out:
            for k, v in d.items():
                if k in have: continue
                out.write(' '.join(v) + '\n'); new += 1
        print('%s: %d bites, %d new' % (mp, len(d), new))

if __name__ == '__main__':
    main()
