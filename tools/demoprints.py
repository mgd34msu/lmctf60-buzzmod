#!/usr/bin/env python3
"""demoprints.py -- dump the svc_print stream and the playerskin table.

A scouting tool: shows exactly how a demo words its flag messages and how
skins encode team, so the defense extractor can key off real strings
instead of guesses.

Usage: demoprints.py <demo.dm2> [...]
"""
import struct, sys, os, re

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dm2speed as D
from demokin import parse_playerstate_full
from demoents import parse_delta_entity_track


def walk_prints(path):
    data = open(path, 'rb').read()
    off = 0
    prints, skins, mapname = [], {}, None
    frame_idx = 0
    ents = {}
    while off + 4 <= len(data):
        (mlen,) = struct.unpack_from('<i', data, off)
        off += 4
        if mlen == -1:
            break
        r = D.R(data[off:off + mlen])
        off += mlen
        try:
            while not r.done():
                svc = r.u8()
                if svc == 12:
                    r.skip(9); r.str_(); r.u16(); r.str_()
                elif svc == 13:
                    idx = r.u16(); s = r.str_()
                    if 1312 <= idx < 1312 + 256:
                        skins[idx - 1312] = s
                    elif idx == 33:
                        m = re.match(r'maps/(\w+)\.bsp', s)
                        if m:
                            mapname = m.group(1)
                elif svc == 14:
                    bits, num = D.parse_entity_bits(r)
                    parse_delta_entity_track(r, bits,
                                             ents.setdefault(num, [0.0]*3))
                elif svc == 20:
                    r.skip(9); ab = r.u8(); r.skip(ab)
                elif svc == 17:
                    parse_playerstate_full(r, {})
                elif svc == 18:
                    while True:
                        bits, num = D.parse_entity_bits(r)
                        if num == 0:
                            break
                        if bits & (1 << 6):
                            ents.pop(num, None)
                            continue
                        parse_delta_entity_track(
                            r, bits, ents.setdefault(num, [0.0]*3))
                    frame_idx += 1
                elif svc == 9: D.parse_sound(r)
                elif svc == 3: D.parse_temp_entity(r)
                elif svc in (1, 2): r.skip(3)
                elif svc == 10:
                    lvl = r.u8(); s = r.str_()
                    prints.append((frame_idx, lvl, s.rstrip('\n')))
                elif svc in (11, 15, 4): r.str_()
                elif svc == 5: r.skip(512)
                elif svc in (6, 7): pass
                else: raise ValueError(svc)
        except Exception:
            continue
    return mapname, frame_idx, prints, skins


if __name__ == '__main__':
    pat = re.compile(sys.argv[1]) if sys.argv[1].startswith('~') is False and \
        os.path.exists(sys.argv[1]) is False else None
    args = sys.argv[1:]
    for p in args:
        mapname, nf, prints, skins = walk_prints(p)
        print(f'=== {os.path.basename(p)} map={mapname} frames={nf} '
              f'prints={len(prints)}')
        print('  skins:', {k: v for k, v in sorted(skins.items())[:24]})
        for f, lvl, s in prints[:200]:
            print(f'  [{f/10.0:7.1f}s l{lvl}] {s}')
