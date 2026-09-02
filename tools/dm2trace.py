#!/usr/bin/env python3
"""dm2trace: the recording player's movement out of a Quake II demo.

Reads the demo's server messages (protocol 34), follows the player state
(origin, velocity, pmove flags, view angles) frame by frame and the
grapple cable effect that marks a rope out, and prints one line per
server frame in the SGHUMAN form used by the live trace, or a summary.

usage: dm2trace.py DEMO.dm2 [--lines]
"""
import struct, sys

class R:
    def __init__(self, data):
        self.d = data; self.p = 0
    def byte(self):
        v = self.d[self.p]; self.p += 1; return v
    def char(self):
        v = struct.unpack_from('<b', self.d, self.p)[0]; self.p += 1; return v
    def short(self):
        v = struct.unpack_from('<h', self.d, self.p)[0]; self.p += 2; return v
    def long(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]; self.p += 4; return v
    def string(self):
        e = self.d.index(b'\0', self.p); s = self.d[self.p:e].decode('latin1'); self.p = e + 1; return s
    def pos(self):
        return (self.short() * 0.125, self.short() * 0.125, self.short() * 0.125)
    def done(self):
        return self.p >= len(self.d)

PMF_DUCKED, PMF_JUMP_HELD, PMF_ON_GROUND = 1, 2, 4

def parse_entity_delta(r):
    bits = r.byte()
    if bits & 128:
        bits |= r.byte() << 8
        if bits & 32768:
            bits |= r.byte() << 16
            if bits & (1 << 23):
                bits |= r.byte() << 24
    num = r.short() if bits & 256 else r.byte()
    if bits & 2048: r.byte()
    if bits & (1 << 20): r.byte()
    if bits & (1 << 21): r.byte()
    if bits & (1 << 22): r.byte()
    if bits & 16: r.byte()
    if bits & (1 << 17): r.short()
    if (bits & ((1 << 16) | (1 << 25))) == ((1 << 16) | (1 << 25)): r.long()
    elif bits & (1 << 16): r.byte()
    elif bits & (1 << 25): r.short()
    if (bits & (16384 | (1 << 19))) == (16384 | (1 << 19)): r.long()
    elif bits & 16384: r.byte()
    elif bits & (1 << 19): r.short()
    if (bits & (4096 | (1 << 18))) == (4096 | (1 << 18)): r.long()
    elif bits & 4096: r.byte()
    elif bits & (1 << 18): r.short()
    origin = [None, None, None]
    if bits & 1: origin[0] = r.short() * 0.125
    if bits & 2: origin[1] = r.short() * 0.125
    if bits & 512: origin[2] = r.short() * 0.125
    if bits & 1024: r.byte()
    if bits & 4: r.byte()
    if bits & 8: r.byte()
    if bits & (1 << 24): r.short(); r.short(); r.short()
    if bits & (1 << 26): r.byte()
    if bits & 32: r.byte()
    if bits & (1 << 27): r.short()
    return num, bits, origin

TE_POS = {5,6,7,8,17,18,20,21,22,28,33,43,45,46,47,49,50,51,52}
TE_POS_DIR = {0,1,2,4,9,12,13,14,26,27,30,40,41,42,44,53}
TE_POS_POS = {3,11,23,32,39}

def parse_temp_entity(r, state):
    t = r.byte()
    if t in TE_POS: r.pos()
    elif t in TE_POS_DIR: r.pos(); r.byte()
    elif t in TE_POS_POS: r.pos(); r.pos()
    elif t in (10, 15, 25, 29): r.byte(); r.pos(); r.byte(); r.byte()
    elif t in (16, 19, 36, 37): r.short(); r.pos(); r.pos()
    elif t == 24:
        ent = r.short(); a = r.pos(); b = r.pos(); r.pos()
        state['cables'].add(ent)
        if ent == state['playernum'] + 1:
            state['cable'] = True; state['bite'] = b
    elif t == 31: r.short(); r.short(); r.pos(); r.pos()
    elif t == 34: r.pos(); r.short()
    elif t == 35: r.pos(); r.pos(); r.byte()
    elif t == 38:
        nextid = r.short(); r.byte(); r.pos(); r.byte(); r.byte(); r.short()
        if nextid != -1: r.long()
    elif t == 48: r.short(); r.pos()
    else:
        raise ValueError('temp entity %d' % t)

def parse_sound(r):
    flags = r.byte(); r.byte()
    if flags & 1: r.byte()
    if flags & 2: r.byte()
    if flags & 16: r.byte()
    if flags & 8: r.short()
    if flags & 4: r.pos()

def parse_playerstate(r, ps):
    flags = r.short()
    if flags & 1: ps['pm_type'] = r.byte()
    if flags & 2: ps['origin'] = r.pos()
    if flags & 4: ps['velocity'] = r.pos()
    if flags & 8: r.byte()
    if flags & 16: ps['pm_flags'] = r.byte()
    if flags & 32: r.short()
    if flags & 64: r.short(); r.short(); r.short()
    if flags & 128: r.char(); r.char(); r.char()
    if flags & 256: ps['angles'] = (r.short() * 360 / 65536, r.short() * 360 / 65536, r.short() * 360 / 65536)
    if flags & 512: r.char(); r.char(); r.char()
    if flags & 4096: r.byte()
    if flags & 8192:
        r.byte(); r.char(); r.char(); r.char(); r.char(); r.char(); r.char()
    if flags & 1024: r.byte(); r.byte(); r.byte(); r.byte()
    if flags & 2048: r.byte()
    if flags & 16384: r.byte()
    stats = r.long()
    for i in range(32):
        if stats & (1 << i): r.short()

def read_demo(path):
    data = open(path, 'rb').read()
    p = 0
    state = {'playernum': 0, 'cable': False, 'bite': None}
    ps = {'origin': (0, 0, 0), 'velocity': (0, 0, 0), 'pm_flags': 0, 'pm_type': 0, 'angles': (0, 0, 0)}
    frames = []
    mapname = None
    ents = {}
    skins = {}
    state['cables'] = set()
    while p + 4 <= len(data):
        length = struct.unpack_from('<i', data, p)[0]; p += 4
        if length == -1: break
        r = R(data[p:p + length]); p += length
        state['cable'] = False
        state['cables'] = set()
        while not r.done():
            cmd = r.byte()
            if cmd == 12:
                r.long(); r.long(); r.byte(); r.string(); state['playernum'] = r.short(); r.string()
            elif cmd == 13:
                idx = r.short(); s = r.string()
                if idx == 0: mapname = s
                if 1312 <= idx < 1312 + 256: skins[idx - 1312] = s.split('\\')[0]
            elif cmd == 14:
                num, bits, origin = parse_entity_delta(r)
                if num not in ents: ents[num] = [0.0, 0.0, 0.0]
                for a in range(3):
                    if origin[a] is not None: ents[num][a] = origin[a]
            elif cmd == 20:
                r.long(); r.long(); r.byte(); n = r.byte(); r.p += n
            elif cmd == 17: parse_playerstate(r, ps)
            elif cmd in (18, 19):
                while True:
                    num, bits, origin = parse_entity_delta(r)
                    if num == 0: break
                    if bits & 64:
                        ents.pop(num, None); continue
                    if num not in ents: ents[num] = [0.0, 0.0, 0.0]
                    for a in range(3):
                        if origin[a] is not None: ents[num][a] = origin[a]
            elif cmd in (1, 2): r.short(); r.byte()
            elif cmd == 3: parse_temp_entity(r, state)
            elif cmd == 4: r.string()
            elif cmd == 5: r.p += 512
            elif cmd in (6, 7, 8): pass
            elif cmd == 9: parse_sound(r)
            elif cmd == 10: r.byte(); r.string()
            elif cmd in (11, 15): r.string()
            elif cmd == 16:
                size = r.short(); r.byte()
                if size > 0: r.p += size
            else:
                raise ValueError('svc %d at frame %d' % (cmd, len(frames)))
        frames.append({'origin': ps['origin'], 'velocity': ps['velocity'], 'flags': ps['pm_flags'],
                       'type': ps['pm_type'], 'angles': ps['angles'], 'cable': state['cable'], 'bite': state['bite'],
                       'players': {n + 1: tuple(ents[n + 1]) for n in skins if (n + 1) in ents},
                       'cables': set(state['cables'])})
    return mapname, frames, skins

def player_summary(mapname, frames, skins, name):
    import statistics as st
    num = None
    for n, s in skins.items():
        if s == name: num = n + 1
    if num is None: return '%s not in this game' % name
    track = [(i, f['players'][num], num in f['cables']) for i, f in enumerate(frames) if num in f['players']]
    if len(track) < 20: return '%s: too few frames' % name
    sp = []; vz = []; ropes = []
    for a, b in zip(track, track[1:]):
        if b[0] - a[0] != 1: continue
        dx, dy, dz = [(b[1][k] - a[1][k]) * 10 for k in range(3)]
        s = (dx * dx + dy * dy) ** 0.5
        if s > 1200: continue   # a respawn or a teleport, not a move
        sp.append(s); vz.append(dz); ropes.append(b[2])
    n = len(sp)
    if n < 20: return '%s: too few moves' % name
    air = sum(1 for z in vz if abs(z) > 30) / n
    eps = []; cur = None
    for i, c in enumerate(ropes):
        if c and cur is None: cur = {'start': i, 'speed0': sp[i]}
        if not c and cur is not None:
            cur['end'] = i; cur['speed1'] = sp[i - 1]; eps.append(cur); cur = None
    out = '%s on %s: %d moves (%.0f s); speed mean %.0f median %.0f p90 %.0f max %.0f; over 300 %.0f%%; vertical motion %.0f%%' % (
        name, mapname.split('\n')[0][:12], n, n / 10, st.mean(sp), st.median(sp), sorted(sp)[int(0.9 * n)], max(sp), 100 * sum(1 for s in sp if s > 300) / n, 100 * air)
    if eps:
        out += '\n  rope: %d episodes, %.1f per minute; out for mean %.2f s; speed at fire mean %.0f; at release mean %.0f' % (
            len(eps), len(eps) / (n / 600), st.mean((e['end'] - e['start']) / 10 for e in eps), st.mean(e['speed0'] for e in eps), st.mean(e['speed1'] for e in eps))
    return out

def summary(mapname, frames):
    import statistics as st
    live = [f for f in frames if f['type'] == 0]
    if len(live) < 20: return '%s: no frames' % mapname
    sp = [(f['velocity'][0] ** 2 + f['velocity'][1] ** 2) ** 0.5 for f in live]
    n = len(sp)
    air = sum(1 for f in live if not (f['flags'] & PMF_ON_GROUND)) / n
    duck = sum(1 for f in live if f['flags'] & PMF_DUCKED) / n
    eps = []; cur = None
    for i, f in enumerate(live):
        if f['cable'] and cur is None: cur = {'start': i, 'speed0': sp[i]}
        if not f['cable'] and cur is not None:
            cur['end'] = i; cur['speed1'] = sp[i - 1]; cur['air1'] = not (live[i - 1]['flags'] & PMF_ON_GROUND); eps.append(cur); cur = None
    out = '%s: %d frames (%.0f s); speed mean %.0f median %.0f p90 %.0f max %.0f; over 300 %.0f%%; airborne %.0f%%; ducked %.0f%%' % (
        mapname, n, n / 10, st.mean(sp), st.median(sp), sorted(sp)[int(0.9 * n)], max(sp), 100 * sum(1 for s in sp if s > 300) / n, 100 * air, 100 * duck)
    if eps:
        out += '\n  rope: %d episodes, %.1f per minute; out for mean %.2f s; speed at fire mean %.0f; speed at release mean %.0f; released in the air %.0f%%' % (
            len(eps), len(eps) / (n / 600), st.mean((e['end'] - e['start']) / 10 for e in eps), st.mean(e['speed0'] for e in eps),
            st.mean(e['speed1'] for e in eps), 100 * sum(1 for e in eps if e['air1']) / len(eps))
    return out

if __name__ == '__main__':
    mapname, frames, skins = read_demo(sys.argv[1])
    if '--player' in sys.argv:
        print(player_summary(mapname, frames, skins, sys.argv[sys.argv.index('--player') + 1]))
    elif '--players' in sys.argv:
        print(sorted(set(skins.values())))
    elif '--lines' in sys.argv:
        for i, f in enumerate(frames):
            o, v = f['origin'], f['velocity']
            print('SGDEMO t=%.1f at=(%.0f %.0f %.0f) v=(%.0f %.0f %.0f) speed=%.0f ground=%d duck=%d rope=%d yaw=%.0f' % (
                i / 10, o[0], o[1], o[2], v[0], v[1], v[2], (v[0] ** 2 + v[1] ** 2) ** 0.5,
                1 if f['flags'] & PMF_ON_GROUND else 0, 1 if f['flags'] & PMF_DUCKED else 0, 1 if f['cable'] else 0, f['angles'][1]))
    else:
        print(summary(mapname, frames))
