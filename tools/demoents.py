#!/usr/bin/env python3
"""demoents.py -- the entity layer: every visible player's trajectory.

Decodes what every other tool skips: the delta-entity stream. Maintains
per-entity origin state across frames and emits 10Hz trajectories for
every player entity in view (entity numbers 1..maxclients), not just the
POV. This is the corpus multiplier: a single POV demo carries the partial
trajectories of everyone the recorder ever saw -- including players who
never recorded a demo of their own.

Output per demo: {entnum: [(frame, x, y, z), ...]} plus the POV entity
number, map name, and the playerskin table for entity->name resolution.

The POV exclusion rules do NOT apply here: a ref cam's view of players is
player data (the owner's ruling -- game info from any camera, POV
kinematics only from player bodies).
"""
import struct, sys, math
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import dm2speed as D
from demokin import parse_playerstate_full

U_ORIGIN1 = 1 << 0
U_ORIGIN2 = 1 << 1
U_ANGLE2 = 1 << 2
U_ANGLE3 = 1 << 3
U_FRAME8 = 1 << 4
U_EVENT = 1 << 5
U_REMOVE = 1 << 6
U_MOREBITS1 = 1 << 7
U_NUMBER16 = 1 << 8
U_ORIGIN3 = 1 << 9
U_ANGLE1 = 1 << 10
U_MODEL = 1 << 11
U_RENDERFX8 = 1 << 12
U_EFFECTS8 = 1 << 14
U_MOREBITS2 = 1 << 15
U_SKIN8 = 1 << 16
U_FRAME16 = 1 << 17
U_RENDERFX16 = 1 << 18
U_EFFECTS16 = 1 << 19
U_MODEL2 = 1 << 20
U_MODEL3 = 1 << 21
U_MODEL4 = 1 << 22
U_MOREBITS3 = 1 << 23
U_OLDORIGIN = 1 << 24
U_SKIN16 = 1 << 25
U_SOUND = 1 << 26
U_SOLID = 1 << 27


def parse_delta_entity_track(r, bits, org):
    """like dm2speed's skip version, but reads origins into org (dict)"""
    if bits & U_MODEL: r.skip(1)
    if bits & U_MODEL2: r.skip(1)
    if bits & U_MODEL3: r.skip(1)
    if bits & U_MODEL4: r.skip(1)
    if bits & U_FRAME8: r.skip(1)
    if bits & U_FRAME16: r.skip(2)
    if (bits & U_SKIN8) and (bits & U_SKIN16): r.skip(4)
    elif bits & U_SKIN8: r.skip(1)
    elif bits & U_SKIN16: r.skip(2)
    if (bits & U_EFFECTS8) and (bits & U_EFFECTS16): r.skip(4)
    elif bits & U_EFFECTS8: r.skip(1)
    elif bits & U_EFFECTS16: r.skip(2)
    if (bits & U_RENDERFX8) and (bits & U_RENDERFX16): r.skip(4)
    elif bits & U_RENDERFX8: r.skip(1)
    elif bits & U_RENDERFX16: r.skip(2)
    if bits & U_ORIGIN1: org[0] = r.s16() / 8.0
    if bits & U_ORIGIN2: org[1] = r.s16() / 8.0
    if bits & U_ORIGIN3: org[2] = r.s16() / 8.0
    if bits & U_ANGLE1: r.skip(1)
    if bits & U_ANGLE2: r.skip(1)
    if bits & U_ANGLE3: r.skip(1)
    if bits & U_OLDORIGIN: r.skip(6)
    if bits & U_SOUND: r.skip(1)
    if bits & U_EVENT: r.skip(1)
    if bits & U_SOLID: r.skip(2)


def walk_entities(path, maxplayers=32):
    data = open(path, 'rb').read()
    off = 0
    playernum = None
    mapname = None
    skins = {}
    ents = {}          # entnum -> [x, y, z] live state
    tracks = {}        # entnum -> list of (frame_idx, x, y, z)
    frame_idx = 0
    import re as _re
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
                    r.skip(9); r.str_(); playernum = r.u16(); r.str_()
                elif svc == 13:
                    idx = r.u16()
                    sstr = r.str_()
                    if 1312 <= idx < 1312 + 256:
                        skins[idx - 1312] = sstr
                    elif idx == 33:
                        m = _re.match(r'maps/(\w+)\.bsp', sstr)
                        if m:
                            mapname = m.group(1)
                elif svc == 14:
                    bits, num = D.parse_entity_bits(r)
                    o = ents.setdefault(num, [0.0, 0.0, 0.0])
                    parse_delta_entity_track(r, bits, o)
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
                        if bits & U_REMOVE:
                            ents.pop(num, None)
                            continue
                        o = ents.setdefault(num, [0.0, 0.0, 0.0])
                        parse_delta_entity_track(r, bits, o)
                    frame_idx += 1
                    for num, o in ents.items():
                        if 1 <= num <= maxplayers:
                            tracks.setdefault(num, []).append(
                                (frame_idx, o[0], o[1], o[2]))
                elif svc == 9: D.parse_sound(r)
                elif svc == 3: D.parse_temp_entity(r)
                elif svc in (1, 2): r.skip(3)
                elif svc == 10: r.skip(1); r.str_()
                elif svc in (11, 15, 4): r.str_()
                elif svc == 5: r.skip(512)
                elif svc in (6, 7): pass
                else: raise ValueError(svc)
        except Exception:
            continue
    return {'map': mapname, 'pov': playernum, 'skins': skins,
            'tracks': tracks, 'frames': frame_idx}


if __name__ == '__main__':
    for path in sys.argv[1:]:
        d = walk_entities(path)
        names = {n: d['skins'].get(n - 1, '?').split('\\')[0]
                 for n in d['tracks']}
        print(f"{path.rsplit('/', 1)[-1]}: map={d['map']} frames={d['frames']} "
              f"players_tracked={len(d['tracks'])}")
        for n, t in sorted(d['tracks'].items(), key=lambda kv: -len(kv[1])):
            print(f"   ent{n} {names.get(n, '?'):24s} {len(t)} samples")
