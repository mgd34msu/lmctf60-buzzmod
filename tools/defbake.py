#!/usr/bin/env python3
"""defbake.py -- bake human defensive occupancy into per-map .dpo sidecars.

PROPOSED FORMAT -- the game does not read this yet.  Writing the file is
tools-side and harmless; the loader, the cvar and the role change are
game code and are held pending sign-off.  The layout deliberately mirrors
the .hmn/.hml/.hme sidecars that Rune_Load() already understands
(slipgate/sg_arach.c:264-323), except that those are indexed by LINK and
this one is indexed by SEED, so the header validates against num_seeds:

    offset 0   int  magic     0x314F5044   ('DPO1')
    offset 4   int  version   1
    offset 8   int  num_seeds must equal rune->hdr.num_seeds
    offset 12  int  planes    4
    offset 16  u8[num_seeds]  post tier, red team
               u8[num_seeds]  post tier, blue team
               u8[num_seeds]  intercept tier, red team
               u8[num_seeds]  intercept tier, blue team

A tier is log-scaled 0..255 exactly as humanbake.py scales link traffic,
with 0 meaning "no human ever held this seed".  Post tiers come from
seconds of standing-still time inside defradius of the team's own flag
stand while that flag was home; intercept tiers come from where defenders
ended up ten seconds after an enemy steal.

Usage: defbake.py <rune_dir> <defense_json_dir> [<map> ...]
"""
import struct, sys, os, json, math, glob

HEADER_FMT = '<4i64s'
SEED_FMT = '<3f2h'
DPO_MAGIC = 0x314F5044


def num_seeds(runepath):
    data = open(runepath, 'rb').read(struct.calcsize(HEADER_FMT))
    magic, ver, ns, nl, name = struct.unpack_from(HEADER_FMT, data, 0)
    return ns


def tiers(weights, ns):
    """{seed: weight} -> bytes(ns), log-scaled 0..255, 0 = never held"""
    top = max(weights.values()) if weights else 0
    out = bytearray(ns)
    if top <= 0:
        return bytes(out)
    lt = math.log1p(top)
    for s, w in weights.items():
        s = int(s)
        if 0 <= s < ns and w > 0:
            out[s] = max(1, min(255, int(255 * math.log1p(w) / lt)))
    return bytes(out)


def main():
    rune_dir, jdir = sys.argv[1], sys.argv[2]
    maps = sys.argv[3:]
    if not maps:
        maps = sorted(os.path.basename(p).split('.')[0]
                      for p in glob.glob(os.path.join(jdir, '*.defense.json')))
    for m in maps:
        rp = None
        for cand in (f'{rune_dir}/maps/{m}.rune', f'{rune_dir}/{m}.rune'):
            if os.path.exists(cand):
                rp = cand
                break
        jp = os.path.join(jdir, f'{m}.defense.json')
        if not rp or not os.path.exists(jp):
            print(f'skip {m}: rune={bool(rp)} json={os.path.exists(jp)}')
            continue
        j = json.load(open(jp))
        ns = num_seeds(rp)
        planes = [
            tiers(j['dwell_seed']['red'], ns),
            tiers(j['dwell_seed']['blue'], ns),
            tiers(j['intercept_seed']['red'], ns),
            tiers(j['intercept_seed']['blue'], ns),
        ]
        # beside the rune, like every other sidecar -- unless DPO_OUT names
        # a staging directory (used to validate the format without dropping
        # files into a live game directory)
        odir = os.environ.get('DPO_OUT') or os.path.dirname(rp)
        os.makedirs(odir, exist_ok=True)
        out = os.path.join(odir, f'{m}.dpo')
        with open(out, 'wb') as f:
            f.write(struct.pack('<4i', DPO_MAGIC, 1, ns, len(planes)))
            for p in planes:
                f.write(p)
        used = [sum(1 for b in p if b) for p in planes]
        print(f'{m}: seeds={ns} held red/blue={used[0]}/{used[1]} '
              f'intercept red/blue={used[2]}/{used[3]} -> {out}')


if __name__ == '__main__':
    main()
