#!/usr/bin/env python3
"""Measure POV kinematics from LMCTF demos.

The decoder reads velocity, view angles, movement flags, and weapon state at
10 Hz. Output covers air gain, hop cadence, view/velocity divergence, and
touchdown friction loss.
"""
import struct, sys, math
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import dm2speed as D


def parse_playerstate_full(r, state):
    (PS_M_TYPE, PS_M_ORIGIN, PS_M_VELOCITY, PS_M_TIME, PS_M_FLAGS,
     PS_M_GRAVITY, PS_M_DELTA_ANGLES) = 1, 2, 4, 8, 16, 32, 64
    (PS_VIEWOFFSET, PS_VIEWANGLES, PS_KICKANGLES, PS_BLEND, PS_FOV,
     PS_WEAPONINDEX, PS_WEAPONFRAME, PS_RDFLAGS) = (128, 256, 512, 1024,
                                                    2048, 4096, 8192, 16384)
    flags = r.u16()
    if flags & PS_M_TYPE: state['pm_type'] = r.u8()
    if flags & PS_M_ORIGIN:
        state['org'] = (r.s16()/8.0, r.s16()/8.0, r.s16()/8.0)
    if flags & PS_M_VELOCITY:
        state['vel'] = (r.s16()/8.0, r.s16()/8.0, r.s16()/8.0)
    if flags & PS_M_TIME: r.skip(1)
    if flags & PS_M_FLAGS: state['pmf'] = r.u8()
    if flags & PS_M_GRAVITY: r.skip(2)
    if flags & PS_M_DELTA_ANGLES: r.skip(6)
    if flags & PS_VIEWOFFSET: r.skip(3)
    if flags & PS_VIEWANGLES:
        state['vyaw'] = r.s16()*360.0/65536.0
        state['vpitch'] = r.s16()*360.0/65536.0
        r.skip(2)
    if flags & PS_KICKANGLES: r.skip(3)
    if flags & PS_WEAPONINDEX: state['gun'] = r.u8()
    if flags & PS_WEAPONFRAME: r.skip(7)
    if flags & PS_BLEND: r.skip(4)
    if flags & PS_FOV: r.skip(1)
    if flags & PS_RDFLAGS: r.skip(1)
    statbits = r.u32()
    for i in range(32):
        if statbits & (1 << i):
            r.skip(2)


def walk(path):
    data = open(path, 'rb').read()
    off = 0
    state = {}
    frames = []
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
                if svc == 12: r.skip(9); r.str_(); r.skip(2); r.str_()
                elif svc == 13: r.skip(2); r.str_()
                elif svc == 14:
                    bits, num = D.parse_entity_bits(r)
                    D.parse_delta_entity(r, bits)
                elif svc == 20:
                    r.skip(8); r.skip(1)
                    ab = r.u8(); r.skip(ab)
                elif svc == 17:
                    parse_playerstate_full(r, state)
                elif svc == 18:
                    D.parse_packetentities(r)
                    frames.append(dict(state))
                elif svc == 9: D.parse_sound(r)
                elif svc == 3: D.parse_temp_entity(r)
                elif svc in (1, 2): r.skip(3)
                elif svc == 10: r.skip(1); r.str_()
                elif svc == 7: pass
                elif svc == 11: r.str_()
                elif svc == 15: r.str_()
                elif svc == 4: r.str_()
                elif svc == 5: r.skip(512)
                elif svc == 6: pass
                else: raise ValueError(svc)
        except Exception:
            continue
    return frames


def grammar(frames):
    import statistics as st
    gain, preturn, relaunch, touchloss = [], [], 0, []
    for i in range(1, len(frames)):
        f, p = frames[i], frames[i-1]
        if 'vel' not in f or 'vel' not in p:
            continue
        v, pv = f['vel'], p['vel']
        hv, phv = math.hypot(v[0], v[1]), math.hypot(pv[0], pv[1])
        if abs(v[2]) > 40 and abs(pv[2]) > 40 and hv > 200:
            gain.append(hv - phv)
        if pv[2] < -100 and v[2] > 100:
            relaunch += 1
        if pv[2] < -100 and abs(v[2]) < 20 and phv > 100:
            touchloss.append(phv - hv)
        if 'vyaw' in f and abs(v[2]) > 40 and hv > 250:
            vh = math.degrees(math.atan2(v[1], v[0]))
            preturn.append(abs((f['vyaw'] - vh + 180) % 360 - 180))
    out = {}
    if gain: out['air_gain_med'] = st.median(gain)
    if preturn: out['view_div_med'] = st.median(preturn)
    out['relaunches'] = relaunch
    if touchloss: out['touch_loss_med'] = st.median(touchloss)
    return out


if __name__ == '__main__':
    for path in sys.argv[1:]:
        fr = walk(path)
        print(path.rsplit('/', 1)[-1], len(fr), grammar(fr))
