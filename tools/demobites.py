#!/usr/bin/env python3
"""demobites.py DEMO_DIR... -o MAPS_DIR [--players NAME,...]

Rope bites from demos into the maps' bite files.  Every player in every
demo (recorder or not) counts unless --players names a set.  For each rope:
the player's position when it was fired and where the cable ended.  Lines
are appended to MAPS_DIR/<map>.bites, deduplicated on a 16-unit grid, in the
form "fire_x fire_y fire_z bite_x bite_y bite_z".  The RUNE builder verifies
each line against the map when it generates.
"""
import sys, os, glob, zipfile, re, tempfile, collections, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dm2trace

def base(n):
    n = n.lower().strip()
    n = re.sub(r'^(\[[^\]]*\]|\{[^}]*\}|<[^>]*>|\^[a-z]+\^ ?|[a-z0-9]{1,6}[.!|-])', '', n)
    return re.sub(r'(\[\d\]|\.mf|-sub)$', '', n)

def demos_in(dirs, tmp):
    out = []
    for d in dirs:
        for f in sorted(glob.glob(os.path.join(d, '*'))):
            if f.lower().endswith('.dm2'):
                out.append(f)
            elif f.lower().endswith('.zip'):
                try:
                    z = zipfile.ZipFile(f)
                    for n in z.namelist():
                        if n.lower().endswith('.dm2'):
                            z.extract(n, tmp); out.append(os.path.join(tmp, n))
                except Exception as e:
                    print('zip failed', f, e, file=sys.stderr)
    return out

def merge(path, entries):
    have = set()
    if os.path.exists(path):
        for l in open(path):
            f = l.split()
            if len(f) == 6:
                have.add(tuple(int(float(x)) // 16 for x in f[3:6]))
    new = 0
    with open(path, 'a') as out:
        for k, v in entries.items():
            if k in have: continue
            out.write(' '.join(v) + '\n'); new += 1
    return new

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('demos', nargs='+'); ap.add_argument('-o', '--out', required=True)
    ap.add_argument('--players', default='')
    a = ap.parse_args()
    want = {p.strip().lower() for p in a.players.split(',') if p.strip()}
    tmp = tempfile.mkdtemp()
    per = collections.defaultdict(dict); read = 0
    for path in demos_in(a.demos, tmp):
        try:
            col = {}; mapname, frames, skins = dm2trace.read_demo(path, col)
        except Exception as e:
            print('failed', os.path.basename(path), e, file=sys.stderr); continue
        mp = os.path.splitext(os.path.basename(col.get('mapfile', '')))[0].lower()
        if not mp: continue
        for n, name in skins.items():
            if want and base(name) not in want: continue
            num = n + 1; cur = None
            for f in frames:
                ends = f['bites'].get(num)
                if ends and num in f['players']:
                    if cur is None: cur = {'fire': f['players'][num], 'end': ends[1], 'n': 1}
                    else: cur['end'] = ends[1]; cur['n'] += 1
                elif cur is not None:
                    if cur['n'] >= 3:
                        fx, fy, fz = cur['fire']; bx, by, bz = cur['end']
                        k = (int(bx) // 16, int(by) // 16, int(bz) // 16)
                        per[mp].setdefault(k, tuple('%.0f' % v for v in (fx, fy, fz, bx, by, bz)))
                    cur = None
        read += 1
    os.makedirs(a.out, exist_ok=True)
    for mp, d in sorted(per.items()):
        new = merge(os.path.join(a.out, mp + '.bites'), d)
        print('%s: %d bites, %d new' % (mp, len(d), new))
    print('demos read:', read)

if __name__ == '__main__':
    main()
