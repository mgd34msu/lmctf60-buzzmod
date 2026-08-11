#!/usr/bin/env python3
"""mapflags.py -- pull info_flag_red / info_flag_blue origins out of BSPs.

The game finds its flag seeds at runtime with Rune_NearestSeed() on the
flag entity origin (slipgate/sg_fields.c Fields_Setup).  Offline we need
the same two points, so this reads the BSP entity lump -- from a loose
maps/<name>.bsp if present, otherwise from inside any pak in the game
directory -- and prints/returns the two origins plus the nearest rune
seed for each.

Usage: mapflags.py <gamedir> [<map> ...]      (no maps = every rune found)
"""
import struct, sys, os, glob, re, json
from functools import lru_cache


@lru_cache(maxsize=64)
def pak_index(pakpath):
    """{lowercase name: (offset, length)} for one Quake2 .pak"""
    out = {}
    with open(pakpath, 'rb') as f:
        head = f.read(12)
        if head[:4] != b'PACK':
            return out
        dirofs, dirlen = struct.unpack_from('<ii', head, 4)
        f.seek(dirofs)
        d = f.read(dirlen)
        for i in range(dirlen // 64):
            name, pos, ln = struct.unpack_from('<56sii', d, i * 64)
            name = name.split(b'\0')[0].decode('latin-1').lower()
            out[name] = (pos, ln)
    return out


def read_game_file(gamedir, relpath):
    """loose file first, then every pak (later paks win, as the engine does)"""
    loose = os.path.join(gamedir, relpath)
    if os.path.exists(loose):
        return open(loose, 'rb').read()
    found = None
    for pak in sorted(glob.glob(os.path.join(gamedir, '*.pak'))):
        idx = pak_index(pak)
        hit = idx.get(relpath.lower())
        if hit:
            found = (pak, hit)
    if not found:
        return None
    pak, (pos, ln) = found
    with open(pak, 'rb') as f:
        f.seek(pos)
        return f.read(ln)


def bsp_entities(data):
    if not data or data[:4] != b'IBSP':
        return ''
    ofs, ln = struct.unpack_from('<ii', data, 8)  # lump 0 = entities
    return data[ofs:ofs + ln].split(b'\0')[0].decode('latin-1')


def parse_ents(text):
    """list of dicts, one per { } block"""
    out = []
    for block in re.findall(r'\{(.*?)\}', text, re.S):
        d = {}
        for k, v in re.findall(r'"([^"]*)"\s+"([^"]*)"', block):
            d[k] = v
        out.append(d)
    return out


@lru_cache(maxsize=64)
def flag_origins(gamedir, mapname):
    data = read_game_file(gamedir, f'maps/{mapname}.bsp')
    ents = parse_ents(bsp_entities(data))
    res = {}
    for e in ents:
        cn = e.get('classname', '')
        if cn in ('info_flag_red', 'info_flag_blue', 'item_flag_team1',
                  'item_flag_team2'):
            key = 'red' if cn in ('info_flag_red', 'item_flag_team1') else 'blue'
            o = [float(x) for x in e.get('origin', '0 0 0').split()]
            res[key] = o
    return res


HEADER_FMT = '<4i64s'
SEED_FMT = '<3f2h'


@lru_cache(maxsize=64)
def load_seeds(path):
    data = open(path, 'rb').read()
    magic, ver, ns, nl, name = struct.unpack_from(HEADER_FMT, data, 0)
    off = struct.calcsize(HEADER_FMT)
    ssz = struct.calcsize(SEED_FMT)
    seeds = []
    for i in range(ns):
        x, y, z, ah, fl = struct.unpack_from(SEED_FMT, data, off)
        off += ssz
        seeds.append((x, y, z))
    return seeds


def nearest(seeds, p):
    best, bd = -1, 1e18
    for i, s in enumerate(seeds):
        d = (s[0]-p[0])**2 + (s[1]-p[1])**2 + (s[2]-p[2])**2
        if d < bd:
            bd, best = d, i
    return best, bd ** 0.5


def main():
    gamedir = sys.argv[1]
    maps = sys.argv[2:]
    if not maps:
        maps = sorted(os.path.basename(p)[:-5]
                      for p in glob.glob(os.path.join(gamedir, 'maps', '*.rune')))
    out = {}
    for m in maps:
        fo = flag_origins(gamedir, m)
        rp = os.path.join(gamedir, 'maps', f'{m}.rune')
        rec = {'map': m, 'flags': fo}
        if os.path.exists(rp) and fo:
            seeds = load_seeds(rp)
            for team in ('red', 'blue'):
                if team in fo:
                    si, dist = nearest(seeds, fo[team])
                    rec[f'{team}_seed'] = si
                    rec[f'{team}_seed_dist'] = round(dist, 1)
        out[m] = rec
        print(m, json.dumps(rec))
    json.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     'human', 'mapflags.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
