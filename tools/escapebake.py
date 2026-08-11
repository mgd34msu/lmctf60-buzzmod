#!/usr/bin/env python3
"""humanbake.py -- bake escapee (carrier post-steal) traffic into per-map .hme sidecars.

Reads <map>.rune (link table order is the contract) and tools/human/
<map>.escape.json (transition counts from demorune.py), writes
maps/<map>.hme: 16-byte header then one uint8 per link -- the link's
human-traffic tier, log-scaled to 0..255 with 0 = no human ever ran it.

A transition a>b credits every rune link a->b (all actions: if a human
moved seam-to-seam, the road is real regardless of gait). The game loads
the sidecar beside the rune and prices highways cheaper under
sg_humanprior.

Usage: humanbake.py <rune_dir> <human_json_dir> <map> [<map> ...]
"""
import struct, sys, os, json, math

HEADER_FMT = '<4i64s'
SEED_FMT = '<3f2h'
LINK_FMT = '<2i6Bh3f'
HME_MAGIC = 0x484D4531  # "1NMH"


def main():
    rune_dir, hdir = sys.argv[1], sys.argv[2]
    for mapname in sys.argv[3:]:
        rp = None
        for cand in (f'{rune_dir}/maps/{mapname}.rune',
                     f'{rune_dir}/{mapname}.rune'):
            if os.path.exists(cand):
                rp = cand
                break
        hj = f'{hdir}/{mapname}.escape.json'
        if not rp or not os.path.exists(hj):
            print(f"skip {mapname}: rune={bool(rp)} json={os.path.exists(hj)}")
            continue
        data = open(rp, 'rb').read()
        _, _, ns, nl, _ = struct.unpack_from(HEADER_FMT, data, 0)
        off = struct.calcsize(HEADER_FMT) + ns * struct.calcsize(SEED_FMT)
        lsz = struct.calcsize(LINK_FMT)
        pairs = []
        for i in range(nl):
            v = struct.unpack_from(LINK_FMT, data, off + i * lsz)
            pairs.append((v[0], v[1]))
        tr = json.load(open(hj))['transitions']
        counts = [tr.get(f'{a}>{b}', 0) for a, b in pairs]
        top = max(counts) if counts else 0
        tiers = bytes(
            0 if c == 0 else
            max(1, min(255, int(255 * math.log1p(c) / math.log1p(top))))
            for c in counts)
        out = os.path.join(os.path.dirname(rp), f'{mapname}.hme')
        with open(out, 'wb') as f:
            f.write(struct.pack('<4i', HME_MAGIC, 1, nl, 0))
            f.write(tiers)
        used = sum(1 for t in tiers if t)
        print(f"{mapname}: links={nl} human-used={used} "
              f"({100*used//max(nl,1)}%) top_count={top} -> {out}")


if __name__ == '__main__':
    main()
