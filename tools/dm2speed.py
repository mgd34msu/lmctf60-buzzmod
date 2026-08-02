#!/usr/bin/env python3
"""dm2speed.py -- extract the POV player's velocity trace from a Quake II
protocol-34 demo, and describe every high-speed episode's anatomy.

The playerstate in each svc_frame carries pmove origin/velocity in 1/8th
units.  Everything else in the stream is parsed only far enough to stay in
byte-sync.  Output: one line per frame (t, speed, hspeed, vz) plus an
episode digest of every run above --hi (default 700)."""
import struct, sys, math

class R:
    def __init__(self, b):
        self.b = b; self.i = 0
    def u8(self):  v = self.b[self.i]; self.i += 1; return v
    def s8(self):  v = struct.unpack_from('<b', self.b, self.i)[0]; self.i += 1; return v
    def u16(self): v = struct.unpack_from('<H', self.b, self.i)[0]; self.i += 2; return v
    def s16(self): v = struct.unpack_from('<h', self.b, self.i)[0]; self.i += 2; return v
    def u32(self): v = struct.unpack_from('<I', self.b, self.i)[0]; self.i += 4; return v
    def s32(self): v = struct.unpack_from('<i', self.b, self.i)[0]; self.i += 4; return v
    def str_(self):
        j = self.b.index(0, self.i); s = self.b[self.i:j]; self.i = j + 1
        return s.decode('latin1')
    def skip(self, n): self.i += n
    def pos(self, n=None): return self.i
    def done(self): return self.i >= len(self.b)

# ---------------------------------------------------------------- entities
U_ORIGIN1=1<<0; U_ORIGIN2=1<<1; U_ANGLE2=1<<2; U_ANGLE3=1<<3
U_FRAME8=1<<4; U_EVENT=1<<5; U_REMOVE=1<<6; U_MOREBITS1=1<<7
U_NUMBER16=1<<8; U_ORIGIN3=1<<9; U_ANGLE1=1<<10; U_MODEL=1<<11
U_RENDERFX8=1<<12; U_EFFECTS8=1<<14; U_MOREBITS2=1<<15
U_SKIN8=1<<16; U_FRAME16=1<<17; U_RENDERFX16=1<<18; U_EFFECTS16=1<<19
U_MODEL2=1<<20; U_MODEL3=1<<21; U_MODEL4=1<<22; U_MOREBITS3=1<<23
U_OLDORIGIN=1<<24; U_SKIN16=1<<25; U_SOUND=1<<26; U_SOLID=1<<27

def parse_entity_bits(r):
    total = r.u8()
    if total & U_MOREBITS1: total |= r.u8() << 8
    if total & U_MOREBITS2: total |= r.u8() << 16
    if total & U_MOREBITS3: total |= r.u8() << 24
    num = r.u16() if total & U_NUMBER16 else r.u8()
    return total, num

def parse_delta_entity(r, bits):
    if bits & U_MODEL:   r.skip(1)
    if bits & U_MODEL2:  r.skip(1)
    if bits & U_MODEL3:  r.skip(1)
    if bits & U_MODEL4:  r.skip(1)
    if bits & U_FRAME8:  r.skip(1)
    if bits & U_FRAME16: r.skip(2)
    if (bits & U_SKIN8) and (bits & U_SKIN16): r.skip(4)
    elif bits & U_SKIN8:  r.skip(1)
    elif bits & U_SKIN16: r.skip(2)
    if (bits & U_EFFECTS8) and (bits & U_EFFECTS16): r.skip(4)
    elif bits & U_EFFECTS8:  r.skip(1)
    elif bits & U_EFFECTS16: r.skip(2)
    if (bits & U_RENDERFX8) and (bits & U_RENDERFX16): r.skip(4)
    elif bits & U_RENDERFX8:  r.skip(1)
    elif bits & U_RENDERFX16: r.skip(2)
    if bits & U_ORIGIN1: r.skip(2)
    if bits & U_ORIGIN2: r.skip(2)
    if bits & U_ORIGIN3: r.skip(2)
    if bits & U_ANGLE1: r.skip(1)
    if bits & U_ANGLE2: r.skip(1)
    if bits & U_ANGLE3: r.skip(1)
    if bits & U_OLDORIGIN: r.skip(6)
    if bits & U_SOUND: r.skip(1)
    if bits & U_EVENT: r.skip(1)
    if bits & U_SOLID: r.skip(2)

def parse_packetentities(r):
    while True:
        bits, num = parse_entity_bits(r)
        if num == 0:
            break
        parse_delta_entity(r, bits)

# --------------------------------------------------------------- playerstate
PS_M_TYPE=1<<0; PS_M_ORIGIN=1<<1; PS_M_VELOCITY=1<<2; PS_M_TIME=1<<3
PS_M_FLAGS=1<<4; PS_M_GRAVITY=1<<5; PS_M_DELTA_ANGLES=1<<6
PS_VIEWOFFSET=1<<7; PS_VIEWANGLES=1<<8; PS_KICKANGLES=1<<9
PS_BLEND=1<<10; PS_FOV=1<<11; PS_WEAPONINDEX=1<<12
PS_WEAPONFRAME=1<<13; PS_RDFLAGS=1<<14

def parse_playerstate(r, state):
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
    if flags & PS_VIEWANGLES: r.skip(6)
    if flags & PS_KICKANGLES: r.skip(3)
    if flags & PS_WEAPONINDEX: state['gun'] = r.u8()
    if flags & PS_WEAPONFRAME: r.skip(1 + 3 + 3)
    if flags & PS_BLEND: r.skip(4)
    if flags & PS_FOV: r.skip(1)
    if flags & PS_RDFLAGS: r.skip(1)
    statbits = r.u32()
    for i in range(32):
        if statbits & (1 << i):
            r.skip(2)

# ------------------------------------------------------------------ sounds
def parse_sound(r):
    flags = r.u8()
    r.skip(1)                       # soundnum
    if flags & 1: r.skip(1)         # volume
    if flags & 2: r.skip(1)         # attenuation
    if flags & 16: r.skip(1)        # offset
    if flags & 8: r.skip(2)         # channel+ent
    if flags & 4: r.skip(6)         # position

# ------------------------------------------------------------- temp entity
# payload shapes by TE_ type (protocol 34, vanilla + lmctf usage)
def parse_temp_entity(r):
    t = r.u8()
    POS = 6; DIR = 1; ENT = 2
    shapes = {
        0: POS+DIR, 1: POS+DIR, 2: POS+DIR, 3: POS+DIR, 4: POS,
        5: POS, 6: POS, 7: POS, 8: POS, 9: POS, 10: POS+DIR, 11: POS+DIR,
        12: POS+DIR, 13: POS+POS, 14: POS, 15: ENT+POS+POS, 16: ENT+POS+POS+POS,
        17: POS+POS, 18: POS+DIR, 19: POS, 20: POS+1, 21: POS+POS,
        22: POS+POS, 23: POS+POS, 24: ENT+2+POS+POS, 25: POS, 26: POS+DIR,
        27: POS+DIR, 28: POS, 29: POS, 30: POS+DIR, 31: POS, 32: POS,
        33: POS,
    }
    if t not in shapes:
        raise ValueError(f"unknown TE {t}")
    r.skip(shapes[t])

# --------------------------------------------------------------------- main
def main(path, hi=700.0):
    data = open(path, 'rb').read()
    off = 0
    state = {'vel': (0,0,0), 'org': (0,0,0)}
    frames = []
    while off + 4 <= len(data):
        (mlen,) = struct.unpack_from('<i', data, off); off += 4
        if mlen == -1:
            break
        r = R(data[off:off+mlen]); off += mlen
        try:
            while not r.done():
                svc = r.u8()
                if svc == 12:      # serverdata
                    r.skip(9); r.str_(); r.skip(2); r.str_()
                elif svc == 13: r.skip(2); r.str_()
                elif svc == 14:
                    bits, num = parse_entity_bits(r); parse_delta_entity(r, bits)
                elif svc == 20:    # frame
                    r.skip(8)
                    r.skip(1)          # suppresscount (protocol 34)
                    ab = r.u8(); r.skip(ab)
                elif svc == 17:    # playerinfo
                    parse_playerstate(r, state)
                elif svc == 18:    # packetentities
                    parse_packetentities(r)
                    frames.append(dict(state))   # frame complete
                elif svc == 9:  parse_sound(r)
                elif svc == 3:  parse_temp_entity(r)
                elif svc in (1, 2): r.skip(3)
                elif svc == 10: r.skip(1); r.str_()
                elif svc == 7: pass
                elif svc == 11: r.str_()
                elif svc == 15: r.str_()
                elif svc == 4:  r.str_()
                elif svc == 5:  r.skip(512)
                elif svc == 6:  pass
                else:
                    raise ValueError(f"svc {svc}")
        except Exception as e:
            # lost sync in this block: keep the frames we have, move on
            continue
    # episode digest
    print(f"{path.split('/')[-1]}: {len(frames)} frames")
    ep = None
    for i, f in enumerate(frames):
        v = f.get('vel', (0,0,0))
        sp = math.sqrt(v[0]**2 + v[1]**2 + v[2]**2)
        hs = math.sqrt(v[0]**2 + v[1]**2)
        if sp >= hi and ep is None:
            ep = i
        elif sp < hi and ep is not None:
            peak = max(math.sqrt(sum(c*c for c in frames[j].get('vel',(0,0,0)))) for j in range(ep, i))
            # anatomy: 1s before the episode
            pre = frames[max(0, ep-10):ep]
            pre_sp = [round(math.sqrt(sum(c*c for c in p.get('vel',(0,0,0))))) for p in pre]
            durf = i - ep
            vz_at_peak = 0
            for j in range(ep, i):
                vv = frames[j].get('vel',(0,0,0))
                if abs(math.sqrt(sum(c*c for c in vv)) - peak) < 1:
                    vz_at_peak = round(vv[2]); break
            print(f"  t={ep/10:.1f}s peak={peak:.0f} dur={durf/10:.1f}s vz@peak={vz_at_peak} pre={pre_sp}")
            ep = None

if __name__ == '__main__':
    hi = float(sys.argv[2]) if len(sys.argv) > 2 else 700.0
    main(sys.argv[1], hi)
