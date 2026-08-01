#!/usr/bin/env python3
"""runelint.py -- structural invariants for SLIPGATE rune files.

Every check here is a claim the generator implicitly makes; a violation
is a generator flaw by definition. Run over one rune or a directory.
"""
import struct, sys, glob, collections, os

ACTS = ['RUN','JUMP','DROP','HOOK','SWIM','LIFT','TELE','RJ']

def load(path):
    f = open(path, 'rb')
    magic, ver, ns, nl = struct.unpack('<4i', f.read(16))
    mapname = f.read(64).split(b'\0')[0].decode()
    seeds = [struct.unpack('<3f2h', f.read(16)) for _ in range(ns)]
    links = [struct.unpack('<2i6bh3f', f.read(28)) for _ in range(nl)]
    return magic, ver, mapname, seeds, links

def lint(path):
    magic, ver, mapname, seeds, links = load(path)
    ns, nl = len(seeds), len(links)
    flaws = []
    if magic != 0x454E5552: flaws.append(f"BAD MAGIC {magic:#x}")
    if ver != 1: flaws.append(f"BAD VERSION {ver}")

    outdeg = collections.Counter(); indeg = collections.Counter()
    dup = collections.Counter(); acts = collections.Counter()
    self_links = zero_cost = huge_cost = bad_idx = neg_exit = 0
    hook_anchor_low = 0
    for l in links:
        fr, to, act, prov, minsp, hdg, slack, exsp, cost = l[:9]
        anchor = l[9:12]
        if not (0 <= fr < ns and 0 <= to < ns):
            bad_idx += 1; continue
        outdeg[fr] += 1; indeg[to] += 1
        dup[(fr, to, act)] += 1
        acts[ACTS[act] if act < len(ACTS) else f"?{act}"] += 1
        if fr == to: self_links += 1
        if cost <= 0: zero_cost += 1
        if cost >= 30000: huge_cost += 1
        if act == 3 and anchor[2] < seeds[fr][2] and seeds[to][2] > seeds[fr][2] + 40:
            hook_anchor_low += 1   # a CLIMB whose anchor is under the floor
                                   # (descending rides naturally anchor low --
                                   # triaged on lmctf16: 191 of 191 legitimate)
    dups = sum(1 for k, v in dup.items() if v > 1)
    orphans = sum(1 for i in range(ns) if not outdeg[i] and not indeg[i])
    deadends = sum(1 for i in range(ns) if not outdeg[i] and indeg[i])
    sources  = sum(1 for i in range(ns) if outdeg[i] and not indeg[i])

    # connectivity in the direction the FIELDS flood: reverse reachability
    # (who can get TO a goal). Forward-rooted checks called lmctf05 84%
    # broken when it is simply strongly one-way (currents, drops) and 87%
    # reverse-reachable -- the direction the bots actually use. Best root
    # over a sample stands in for the flag seeds, which the file does not
    # name.
    radj = collections.defaultdict(list)
    for l in links:
        if 0 <= l[0] < ns and 0 <= l[1] < ns:
            radj[l[1]].append(l[0])
    def rsweep(root):
        seen = {root}; stack = [root]
        while stack:
            u = stack.pop()
            for v in radj[u]:
                if v not in seen:
                    seen.add(v); stack.append(v)
        return len(seen)
    best = 0
    for root in range(0, ns, max(1, ns // 40)):
        r = rsweep(root)
        if r > best: best = r
    unreach = ns - best

    if bad_idx:        flaws.append(f"links with out-of-range seeds: {bad_idx}")
    if self_links:     flaws.append(f"self-links (from==to): {self_links}")
    if zero_cost:      flaws.append(f"links with cost<=0 ms: {zero_cost}")
    if huge_cost:      flaws.append(f"links with cost>=30s: {huge_cost}")
    if dups:           flaws.append(f"duplicate (from,to,action) triples: {dups}")
    if orphans:        flaws.append(f"orphan seeds (no links at all): {orphans}")
    if deadends > ns * 0.02:
        flaws.append(f"dead-end seeds (in, no out): {deadends} ({100*deadends//ns}%)")
    if sources > ns * 0.02:
        flaws.append(f"source-only seeds (out, no in): {sources} ({100*sources//ns}%)")
    if unreach > ns * 0.05:
        flaws.append(f"unreachable from seed 0: {unreach} ({100*unreach//ns}%)")
    if hook_anchor_low:
        flaws.append(f"hook anchors BELOW their firing floor: {hook_anchor_low}")

    name = os.path.basename(path)
    print(f"== {name}: seeds={ns} links={nl} " +
          " ".join(f"{k}={v}" for k, v in sorted(acts.items())))
    for fl in flaws:
        print(f"   FLAW: {fl}")
    if not flaws:
        print("   clean")
    return flaws

if __name__ == '__main__':
    args = sys.argv[1:] or ['/home/buzzkill/Games/Quake2/lmctf-hooktest/maps/*.rune']
    total = 0
    for pat in args:
        for p in sorted(glob.glob(pat)):
            total += len(lint(p))
    print(f"TOTAL FLAWS: {total}")
