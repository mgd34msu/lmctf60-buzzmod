#!/usr/bin/env python3
"""mapflags.py -- inspect info_flag_red / info_flag_blue origins in map assets.

This is an offline inspection aid, not deployment authority: engine entity
override rules and collision settling can change the final spawned flag
positions.  Rune generation prints those authoritative post-spawn root seed
indices and runegen passes them directly to runelint's deployment gate.
Here we prefer a loose or packed maps/<name>.ent view, then read the entity
lump from a loose or packed BSP, and report approximate origins/seeds.

Usage: mapflags.py [--out PATH] <gamedir> [<map> ...]
       (no maps = every loose rune found; stdout-only unless --out is given)
"""
import argparse
import glob
import json
import math
import os
import re
import struct
import sys
from functools import lru_cache

from corpusgraph import (HEADER_SIZE, LINK_SIZE, SEED_FMT, SEED_SIZE,
                         atomic_write_json, read_rune, require_safe_mapname)

MAX_PAK_DIRECTORY = 64 * 1024 * 1024
MAX_GAME_FILE_BYTES = 512 * 1024 * 1024
BSP_HEADER_SIZE = 8 + 19 * 8
MAX_ENTITY_BYTES = 32 * 1024 * 1024


@lru_cache(maxsize=64)
def pak_index(pakpath):
    """{lowercase name: (offset, length)} for one Quake2 .pak"""
    out = {}
    with open(pakpath, 'rb') as f:
        head = f.read(12)
        if len(head) != 12 or head[:4] != b'PACK':
            raise ValueError(f'{pakpath}: malformed PAK header')
        dirofs, dirlen = struct.unpack_from('<ii', head, 4)
        f.seek(0, os.SEEK_END)
        file_size = f.tell()
        if (dirofs < 12 or dirlen < 0 or dirlen % 64 or
                dirofs > file_size or dirlen > file_size - dirofs):
            raise ValueError(f'{pakpath}: malformed PAK directory')
        if dirlen > MAX_PAK_DIRECTORY:
            raise ValueError(f'{pakpath}: PAK directory is unreasonably large')
        f.seek(dirofs)
        d = f.read(dirlen)
        for i in range(dirlen // 64):
            name, pos, ln = struct.unpack_from('<56sii', d, i * 64)
            name = name.split(b'\0')[0].decode('latin-1').lower()
            if pos < 0 or ln < 0 or pos > file_size or ln > file_size - pos:
                raise ValueError(f'{pakpath}: malformed entry {name!r}')
            out[name] = (pos, ln)
    return out


def read_game_file(gamedir, relpath):
    """loose file first, then every pak (later paks win, as the engine does)"""
    loose = os.path.join(gamedir, relpath)
    if os.path.exists(loose):
        with open(loose, 'rb') as f:
            size = os.fstat(f.fileno()).st_size
            if size > MAX_GAME_FILE_BYTES:
                raise ValueError(f'{loose}: map asset is unreasonably large')
            return f.read()
    found = None
    for pak in sorted(glob.glob(os.path.join(gamedir, '*.pak'))):
        idx = pak_index(pak)
        hit = idx.get(relpath.lower())
        if hit:
            found = (pak, hit)
    if not found:
        return None
    pak, (pos, ln) = found
    if ln > MAX_GAME_FILE_BYTES:
        raise ValueError(f'{pak}: packed map asset is unreasonably large')
    with open(pak, 'rb') as f:
        f.seek(pos)
        return f.read(ln)


def bsp_entities(data, source='<BSP>'):
    if data is None:
        raise FileNotFoundError(source)
    if len(data) < BSP_HEADER_SIZE or data[:4] != b'IBSP':
        raise ValueError(f'{source}: malformed BSP header')
    version = struct.unpack_from('<i', data, 4)[0]
    if version != 38:
        raise ValueError(f'{source}: unsupported BSP version {version}')
    ofs, ln = struct.unpack_from('<ii', data, 8)  # lump 0 = entities
    if (ofs < BSP_HEADER_SIZE or ln < 0 or ln > MAX_ENTITY_BYTES or
            ofs > len(data) or ln > len(data) - ofs):
        raise ValueError(f'{source}: entity lump is outside the file')
    return data[ofs:ofs + ln].split(b'\0', 1)[0].decode('latin-1')


def parse_ents(text):
    """list of dicts, one per { } block"""
    out = []
    for block in re.findall(r'\{(.*?)\}', text, re.S):
        d = {}
        for k, v in re.findall(r'"([^"]*)"\s+"([^"]*)"', block):
            d[k] = v
        out.append(d)
    return out


def entity_text(gamedir, mapname):
    """Return (entity_text, source), preferring an external ENT override."""
    require_safe_mapname(mapname)
    ent_rel = f'maps/{mapname}.ent'
    ent_data = read_game_file(gamedir, ent_rel)
    if ent_data is not None:
        if len(ent_data) > MAX_ENTITY_BYTES:
            raise ValueError(f'{ent_rel}: entity text is unreasonably large')
        try:
            return ent_data.split(b'\0', 1)[0].decode('latin-1'), ent_rel
        except UnicodeDecodeError as e:
            raise ValueError(f'{ent_rel}: cannot decode entity text') from e

    bsp_rel = f'maps/{mapname}.bsp'
    bsp_data = read_game_file(gamedir, bsp_rel)
    if bsp_data is None:
        raise FileNotFoundError(
            f'{gamedir}: no {ent_rel} or {bsp_rel} (loose or in a PAK)')
    return bsp_entities(bsp_data, bsp_rel), bsp_rel


@lru_cache(maxsize=64)
def flag_origins_with_source(gamedir, mapname):
    text, source = entity_text(gamedir, mapname)
    ents = parse_ents(text)
    res = {}
    for e in ents:
        cn = e.get('classname', '')
        if cn in ('info_flag_red', 'info_flag_blue', 'item_flag_team1',
                  'item_flag_team2'):
            key = 'red' if cn in ('info_flag_red', 'item_flag_team1') else 'blue'
            raw_origin = e.get('origin')
            if raw_origin is None:
                raise ValueError(f'{source}: {cn} has no origin')
            parts = raw_origin.split()
            if len(parts) != 3:
                raise ValueError(f'{source}: {cn} origin must have 3 values')
            try:
                o = [float(x) for x in parts]
            except ValueError as exc:
                raise ValueError(f'{source}: {cn} has malformed origin') from exc
            if not all(math.isfinite(value) for value in o):
                raise ValueError(f'{source}: {cn} has non-finite origin')
            if key in res:
                raise ValueError(f'{source}: multiple {key} flag entities')
            res[key] = o
    return res, source


@lru_cache(maxsize=64)
def flag_origins(gamedir, mapname):
    return flag_origins_with_source(gamedir, mapname)[0]


@lru_cache(maxsize=64)
def load_graph(path):
    rune = read_rune(path)
    data = rune['data']
    num_seeds = rune['num_seeds']
    num_links = rune['num_links']
    off = HEADER_SIZE
    seeds = []
    for _ in range(num_seeds):
        x, y, z, ah, fl = struct.unpack_from(SEED_FMT, data, off)
        off += SEED_SIZE
        seeds.append((x, y, z))
    linked = set()
    for _ in range(num_links):
        source = struct.unpack_from('<i', data, off)[0]
        if 0 <= source < num_seeds:
            linked.add(source)
        off += LINK_SIZE
    return seeds, frozenset(linked)


@lru_cache(maxsize=64)
def load_seeds(path):
    return load_graph(path)[0]


def nearest(seeds, p, linked=None):
    """Nearest runtime-eligible seed using Rune_NearestSeed's distance.

    The in-game lookup additionally rejects a seed when its chest-height
    world trace is blocked. Offline callers do not have the engine tracer,
    but matching its linked/z filters and weighted metric avoids the old
    plain-Euclidean disagreement on stacked geometry.
    """
    best, bd = -1, 1e18
    for i, s in enumerate(seeds):
        dz = s[2] - p[2]
        if dz > 96.0 or dz < -96.0:
            continue
        dx = s[0] - p[0]
        dy = s[1] - p[1]
        if dx * dx + dy * dy > 128.0 * 128.0:
            continue
        d = dx * dx + dy * dy + dz**2 * 0.25
        if d < bd:
            bd, best = d, i
    # Runtime chooses the nearest visible geometry owner first and only then
    # asks whether that owner participates in routing. Searching past an
    # unlinked/tombstone owner can assign a body across a one-way boundary to
    # a farther seed it cannot reach.
    if best >= 0 and linked is not None and best not in linked:
        return -1, float('inf')
    return best, bd ** 0.5 if best >= 0 else float('inf')


def main(argv=None):
    parser = argparse.ArgumentParser(
        description='Resolve CTF flags and their nearest rune seeds.')
    parser.add_argument(
        '--out', '-o', metavar='PATH',
        help='atomically write the aggregate JSON to PATH (default: no file)')
    parser.add_argument('gamedir')
    parser.add_argument('maps', nargs='*', metavar='MAP')
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)

    gamedir = args.gamedir
    maps = args.maps
    if not maps:
        maps = sorted(os.path.basename(p)[:-5]
                      for p in glob.glob(os.path.join(gamedir, 'maps', '*.rune')))
    if not maps:
        print(f'mapflags: no loose rune files found under {gamedir}/maps',
              file=sys.stderr)
        return 1
    out = {}
    for m in maps:
        require_safe_mapname(m)
        fo = flag_origins(gamedir, m)
        rp = os.path.join(gamedir, 'maps', f'{m}.rune')
        rec = {'map': m, 'flags': fo}
        if os.path.exists(rp) and fo:
            seeds, linked = load_graph(rp)
            for team in ('red', 'blue'):
                if team in fo:
                    si, dist = nearest(seeds, fo[team], linked)
                    rec[f'{team}_seed'] = si
                    rec[f'{team}_seed_dist'] = round(dist, 1)
        out[m] = rec
        print(m, json.dumps(rec))
    if args.out:
        atomic_write_json(args.out, out)
        print(f'WROTE {args.out}: maps={len(out)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
