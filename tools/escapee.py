#!/usr/bin/env python3
"""escapee.py -- flag-carrier escape trajectories, mapped onto rune seeds.

For every flag steal print in a .dm2 demo, resolves the stealer's name to
an entity number (via the playerskins configstrings) and cuts that
entity's origin track for the next 200 demo frames (10Hz ~= 20s) or until
a window-ender print names the same player -- a capture, a return, or a
death obituary -- whichever comes first. Each cut track is mapped onto
the nearest rune seed per frame (demorune's spatial-hash method) and the
seed-to-seed transitions are counted. Counts are aggregated across the
whole demo corpus, one output file per map.

Entity-layer data is used from every demo, including ref-cam recordings:
the owner's ruling is that game info from any camera is fair game (only
POV kinematics are restricted to player-body recordings).

Output: tools/human/<map>.escape.json =
    {"map": <name>, "windows": N, "transitions": {"a>b": count, ...}}

Usage: escapee.py <demo.dm2> [<demo.dm2> ...]
"""
import struct, sys, os, re, json, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dm2speed as D
import demoents as DE
from demokin import parse_playerstate_full
from demorune import load_seeds, SeedGrid

RUNE_DIR = '/home/buzzkill/Games/Quake2/lmctf-hooktest/maps'
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'human')
WINDOW_FRAMES = 200          # 10Hz * 20s
STEAL_PAT = ' stole the '
ENDER_PATS = (' captured the ', ' returned the ', ' was ')


def strip_color(s):
    """Q2 color coding sets the high bit per-char; mask it off, then drop
    anything left that isn't printable ASCII."""
    return ''.join(chr(ord(c) & 0x7f) for c in s)


def clean_name(s):
    s = strip_color(s)
    s = re.sub(r'[^\x20-\x7e]', '', s)
    return s.strip()


def rune_path_for(mapname):
    for cand in (f'{RUNE_DIR}/{mapname}.rune',
                 f'{RUNE_DIR}/maps/{mapname}.rune',
                 f'{RUNE_DIR}/runes/{mapname}.rune'):
        if os.path.exists(cand):
            return cand
    return None


def walk_demo(path, grids, agg):
    """Returns (mapname, steals_found, windows_cut)."""
    data = open(path, 'rb').read()
    off = 0
    mapname = None
    skins = {}          # 0-based client index -> raw "name\model/skin"
    ents = {}           # entnum -> [x, y, z] live state
    frame_idx = 0
    active = []          # list of {entity, name, positions, frames_elapsed}
    steals_found = 0
    windows_cut = 0

    def get_grid():
        if mapname is None:
            return None
        if mapname not in grids:
            rp = rune_path_for(mapname)
            grids[mapname] = SeedGrid(load_seeds(rp)) if rp else False
        g = grids[mapname]
        return g if g else None

    def resolve_entity(name_clean):
        low = name_clean.lower()
        for i, skin in skins.items():
            skin_name = clean_name(skin.split('\\')[0])
            if skin_name.lower() == low:
                return i + 1
        return None

    def close_window(w):
        nonlocal windows_cut
        windows_cut += 1
        g = get_grid()
        if g is None or mapname is None:
            return
        a = agg.setdefault(mapname, {'map': mapname, 'windows': 0,
                                      'transitions': collections.Counter()})
        a['windows'] += 1
        prev = -1
        for (fi, x, y, z) in w['positions']:
            s = g.nearest((x, y, z))
            if s < 0:
                continue
            if prev >= 0 and s != prev:
                a['transitions'][f'{prev}>{s}'] += 1
            prev = s

    while off + 4 <= len(data):
        (mlen,) = struct.unpack_from('<i', data, off)
        off += 4
        if mlen == -1:
            break
        r = D.R(data[off:off+mlen])
        off += mlen
        try:
            while not r.done():
                svc = r.u8()
                if svc == 12:
                    r.skip(9); r.str_(); r.skip(2); r.str_()
                elif svc == 13:
                    idx = r.u16()
                    sstr = r.str_()
                    if 1312 <= idx < 1312 + 256:
                        skins[idx - 1312] = sstr
                    elif idx == 33:
                        m = re.match(r'maps/(\w+)\.bsp', sstr)
                        if m:
                            mapname = m.group(1)
                elif svc == 14:
                    bits, num = D.parse_entity_bits(r)
                    o = ents.setdefault(num, [0.0, 0.0, 0.0])
                    DE.parse_delta_entity_track(r, bits, o)
                elif svc == 20:
                    r.skip(9)
                    ab = r.u8(); r.skip(ab)
                elif svc == 17:
                    parse_playerstate_full(r, {})
                elif svc == 18:
                    while True:
                        bits, num = D.parse_entity_bits(r)
                        if num == 0:
                            break
                        if bits & DE.U_REMOVE:
                            ents.pop(num, None)
                            continue
                        o = ents.setdefault(num, [0.0, 0.0, 0.0])
                        DE.parse_delta_entity_track(r, bits, o)
                    frame_idx += 1
                    still = []
                    for w in active:
                        o = ents.get(w['entity'])
                        if o is not None:
                            w['positions'].append((frame_idx, o[0], o[1], o[2]))
                        w['frames_elapsed'] += 1
                        if w['frames_elapsed'] >= WINDOW_FRAMES:
                            close_window(w)
                        else:
                            still.append(w)
                    active = still
                elif svc == 9:
                    D.parse_sound(r)
                elif svc == 3:
                    D.parse_temp_entity(r)
                elif svc in (1, 2):
                    r.skip(3)
                elif svc == 10:
                    lvl = r.u8()
                    s = r.str_()
                    cs = strip_color(s)
                    if STEAL_PAT in cs:
                        steals_found += 1
                        name = clean_name(cs.split(STEAL_PAT)[0])
                        ent = resolve_entity(name)
                        if ent is not None:
                            active.append({'entity': ent, 'name': name,
                                            'positions': [], 'frames_elapsed': 0})
                    else:
                        for pat in ENDER_PATS:
                            if pat in cs:
                                who = clean_name(cs.split(pat)[0]).lower()
                                keep = []
                                for w in active:
                                    if w['name'].lower() == who:
                                        close_window(w)
                                    else:
                                        keep.append(w)
                                active = keep
                                break
                elif svc == 7:
                    pass
                elif svc == 11:
                    r.str_()
                elif svc == 15:
                    r.str_()
                elif svc == 4:
                    r.str_()
                elif svc == 5:
                    r.skip(512)
                elif svc == 6:
                    pass
                else:
                    raise ValueError(f"svc {svc}")
        except Exception:
            continue

    for w in active:
        close_window(w)

    return mapname, steals_found, windows_cut


def main():
    grids = {}
    agg = {}
    failed = []
    for path in sys.argv[1:]:
        base = os.path.basename(path)
        try:
            mapname, steals_found, windows_cut = walk_demo(path, grids, agg)
            print(f"{base}: map={mapname} steals={steals_found} windows={windows_cut}")
        except Exception as e:
            failed.append((base, str(e)))
            print(f"{base}: FAILED ({e})")

    os.makedirs(OUT_DIR, exist_ok=True)
    print()
    for mapname, a in sorted(agg.items()):
        outpath = os.path.join(OUT_DIR, f'{mapname}.escape.json')
        windows = a['windows']
        transitions = collections.Counter(a['transitions'])
        # merge with any prior run's output -- lets the corpus be processed
        # in timeout-bounded chunks without losing earlier chunks' counts.
        if os.path.exists(outpath):
            try:
                prev = json.load(open(outpath))
                windows += prev.get('windows', 0)
                for k, v in prev.get('transitions', {}).items():
                    transitions[k] += v
            except Exception:
                pass
        out = {'map': mapname, 'windows': windows,
               'transitions': dict(transitions)}
        json.dump(out, open(outpath, 'w'))
        print(f"WROTE {outpath}: windows={windows} "
              f"transitions={len(transitions)}")

    if failed:
        print(f"\n{len(failed)} demos failed to parse:")
        for base, err in failed:
            print(f"  {base}: {err}")


if __name__ == '__main__':
    main()
