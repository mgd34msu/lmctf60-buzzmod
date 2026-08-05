#!/usr/bin/env python3
"""
carryforensics.py -- per-carry ledger from wave logs.

For every "CARRY X begins" .. "CARRY X ends" episode in a wave log, record:
  * route progress: goal cost at steal, minimum goal cost reached, fraction remaining
  * how it ended: capture / enemy kill / self (wedge, lava, own splash) / other
  * who ended it: killer name, killer role, killer position, killer's own last death
  * escort geometry: nearest teammate to the carrier at the moment it ended
  * position geometry: projection of carrier and killer onto steal-stand -> home-stand axis

Emits one JSON record per carry to stdout (JSONL) or a file.
"""

import json
import os
import re
import sys
import math
from multiprocessing import Pool

RE_SG = re.compile(
    r'^SG (\S+): role=(\d+) seed=(-?\d+) goal=(-?\d+) sgoal=(-?\d+) spd=(-?\d+) '
    r'org=\((-?\d+) (-?\d+) (-?\d+)\) link=(-?\d+) act=(-?\d+) '
    r'hp=(-?\d+) dh=(-?\d+) dl=(-?\d+) st=([-\d.]+) gnd=(\d)')
RE_CBT = re.compile(r'^CBTWHY frames=(\d+)')
RE_DEATH = re.compile(
    r'^BOTDEATH: (\S+) t(\d) distflag=(-?\d+) org=\((-?\d+) (-?\d+) (-?\d+)\).*?'
    r'mod=(-?\d+) deaths=(\d+) nearflag=(\d+)')
RE_CARRYB = re.compile(r'^CARRY (\S+) begins')
RE_CARRYE = re.compile(r'^CARRY (\S+) ends after ([\d.]+)s')
RE_CARRYLOST = re.compile(r'^CARRYLOST (\S+) best=(-?\d+) now=(-?\d+)')
RE_GRAB = re.compile(r'^GRABMODE (\S+) room=(-?\d+) (\w+)')
RE_TEAM = re.compile(r'^(\S+\[SG\]) is now on the (red|blue) team\.')
RE_STOLE = re.compile(r'^(\S+\[SG\]) stole the (red|blue) flag\.')
RE_CAP = re.compile(r'^(\S+\[SG\]) captured the (red|blue) flag\.')
RE_RET = re.compile(r'^(\S+\[SG\]) returned the (red|blue) flag\.')
RE_LOSTFLAG = re.compile(r'^(\S+\[SG\]) lost the (red|blue) flag\.')
RE_INTERPOSE = re.compile(r'^INTERPOSE (\S+) seed=(-?\d+)')
RE_RALLY = re.compile(r'^RALLY (\S+) waits')
RE_FLAGORG = re.compile(
    r'^FLAGDIAG: (red|blue)flag\s+cls=flag org=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)')
RE_DMG = re.compile(
    r'^DMG (\S+)>(\S+) take=(-?\d+) mod=(-?\d+) fc=(\d) agnd=(\d) tgnd=(\d) rng=(-?\d+)')
RE_NAME = re.compile(r'([A-Za-z0-9_]+\[SG\])')
RE_WEDGE = re.compile(r'^WEDGEKILL (\S+) at')
RE_KIT = re.compile(r'([A-Za-z0-9_]+\[SG\]):(\S*?)/a(-?\d+)/h(-?\d+)/d(\d+)')
RE_FCKILL = re.compile(r'^([A-Za-z0-9_]+\[SG\]) killed the enemy flag carrier\.$')

# exact obituary templates observed across waves 274-308 (uniq -c census)
N = r'([A-Za-z0-9_]+\[SG\])'
RE_OBIT_KILLER = [re.compile(r'^' + N + s + '$') for s in (
    r' was railed by ' + N,
    r' ate ' + N + r"'s rocket",
    r' almost dodged ' + N + r"'s rocket",
    r' caught ' + N + r"'s handgrenade",
    r" didn't see " + N + r"'s handgrenade",
    r' was blown away by ' + N + r"'s super shotgun",
    r' was blasted by ' + N,
    r' was melted by ' + N + r"'s hyperblaster",
    r' was cut in half by ' + N + r"'s chaingun",
    r' was machinegunned by ' + N,
    r' was gored by ' + N + r"'s grappling hook",
    r' was gunned down by ' + N,
    r' was popped by ' + N + r"'s grenade",
)]
RE_OBIT_SELF = re.compile(
    r'^' + N + r' (killed itself\.|blew itself up\.|tripped on its own grenade\.|'
    r'melted\.|does a back flip into the lava\.|sank like a rock\.|cratered\.|'
    r'was squished\.|was in the wrong place\.)$')

MODNAMES = {
    1: 'blaster', 2: 'shotgun', 3: 'sshotgun', 4: 'machinegun', 5: 'chaingun',
    6: 'grenade', 7: 'gsplash', 8: 'rocket', 9: 'rsplash', 10: 'hyperblaster',
    11: 'railgun', 12: 'bfg_laser', 13: 'bfg_blast', 14: 'bfg_effect',
    15: 'handgrenade', 16: 'hg_splash', 17: 'water', 18: 'slime', 19: 'lava',
    20: 'crush', 21: 'telefrag', 22: 'falling', 23: 'suicide', 24: 'barrel',
    25: 'exit', 26: 'splash', 27: 'target_laser', 28: 'trigger_hurt',
    29: 'hit', 30: 'target_blaster', 31: 'grapple', 32: 'trap',
}

SELF_MODS = {17, 18, 19, 20, 22, 23, 25, 26, 28}


def dist(a, b):
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def dist2d(a, b):
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2)


def proj_frac(p, a, b):
    """fraction along a->b of the projection of p (unclamped)"""
    ax = [b[i] - a[i] for i in range(3)]
    n = sum(v * v for v in ax)
    if n <= 0:
        return None
    return sum((p[i] - a[i]) * ax[i] for i in range(3)) / n


def parse_file(path):
    fname = os.path.basename(path)
    m = re.match(r'(s\d+)-([\w:]+)-(\w+)\.log', fname)
    if not m:
        return []
    slot, fmt, mapname = m.group(1), m.group(2), m.group(3)
    it = os.path.basename(os.path.dirname(path))
    wave = int(it.split('-')[1]) if '-' in it else -1
    sidx = int(slot[1:])

    # carrier-cover dose for this (wave, server) from iterate.sh / iterate2.sh history
    if wave < 274:
        cover = 0
    elif wave <= 275:
        cover = 0 if sidx in (1, 2, 10) else 400
    elif wave == 276:
        cover = 0 if sidx in (1, 2, 10) else 800
    elif wave == 277:
        cover = 0 if sidx in (1, 2, 10) else 1200
    elif wave <= 283:
        cover = 0 if sidx in (1, 2, 10) else 800
    else:
        cover = 0 if sidx in (9, 10) else 800
    doctrine = 0 if (wave >= 284 and sidx in (9, 10)) or (wave <= 283 and sidx == 10) else 1

    stands = {}          # 'red'/'blue' -> org
    teamof = {}          # name -> 1 (red) / 2 (blue)
    lastsg = {}          # name -> dict
    sghist = {}          # name -> list of (line, t, org)
    lastdeath = {}       # name -> (line, t)
    clock = []           # (line, frames)
    events = []          # (line, kind, payload)
    carries = []
    open_carry = {}
    interpose_lines = []
    rally_lines = []
    dmg_on_carrier = []  # (line, att, tgt, mod, rng, agnd, tgnd, take)
    kits = []            # (line, {name: (weaponletters, armour, health)})
    names = set()

    with open(path, 'r', errors='replace') as fh:
        for ln, raw in enumerate(fh):
            line = raw.rstrip('\n')
            if not line:
                continue
            c0 = line[0]

            if c0 == 'S' and line.startswith('SG '):
                mm = RE_SG.match(line)
                if mm:
                    nm = mm.group(1)
                    names.add(nm)
                    org = (int(mm.group(7)), int(mm.group(8)), int(mm.group(9)))
                    rec = dict(line=ln, role=int(mm.group(2)), seed=int(mm.group(3)),
                               goal=int(mm.group(4)), sgoal=int(mm.group(5)),
                               spd=int(mm.group(6)), org=org, link=int(mm.group(10)),
                               act=int(mm.group(11)), gnd=int(mm.group(16)))
                    lastsg[nm] = rec
                    sghist.setdefault(nm, []).append((ln, org, rec['role'], rec['spd']))
                    if nm in open_carry and rec['role'] == 2:
                        open_carry[nm]['samples'].append(rec)
                continue

            if c0 == 'C' and (line.startswith('CBTWHY') or
                              line.startswith('CARRY') or
                              line.startswith('CMD ') or
                              line.startswith('CBTSCAN')):
                if line.startswith('CBTWHY frames='):
                    clock.append((ln, int(RE_CBT.match(line).group(1))))
                    continue
                mm = RE_CARRYB.match(line)
                if mm:
                    nm = mm.group(1)
                    open_carry[nm] = dict(name=nm, begin=ln, samples=[],
                                          carrylost=[], grabmode=None)
                    continue
                mm = RE_CARRYE.match(line)
                if mm:
                    nm = mm.group(1)
                    if nm in open_carry:
                        cy = open_carry.pop(nm)
                        cy['end'] = ln
                        cy['dur'] = float(mm.group(2))
                        carries.append(cy)
                    continue
                mm = RE_CARRYLOST.match(line)
                if mm and mm.group(1) in open_carry:
                    open_carry[mm.group(1)]['carrylost'].append(
                        (ln, int(mm.group(2)), int(mm.group(3))))
                continue

            if c0 == 'G' and line.startswith('GRABMODE'):
                mm = RE_GRAB.match(line)
                if mm and mm.group(1) in open_carry:
                    open_carry[mm.group(1)]['grabmode'] = (int(mm.group(2)),
                                                           mm.group(3))
                continue

            if c0 == 'B' and line.startswith('BOTKIT:'):
                kit = {}
                for km in RE_KIT.finditer(line):
                    kit[km.group(1)] = (km.group(2), int(km.group(3)),
                                        int(km.group(4)))
                if kit:
                    kits.append((ln, kit))
                continue

            if c0 == 'B' and line.startswith('BOTDEATH:'):
                mm = RE_DEATH.match(line)
                if mm:
                    nm = mm.group(1)
                    names.add(nm)
                    teamof[nm] = int(mm.group(2))
                    events.append((ln, 'death', dict(
                        name=nm, team=int(mm.group(2)),
                        distflag=int(mm.group(3)),
                        org=(int(mm.group(4)), int(mm.group(5)), int(mm.group(6))),
                        mod=int(mm.group(7)), deaths=int(mm.group(8)),
                        nearflag=int(mm.group(9)))))
                continue

            if c0 == 'D' and line.startswith('DMG '):
                mm = RE_DMG.match(line)
                if mm and mm.group(5) == '1':
                    dmg_on_carrier.append(dict(
                        line=ln, att=mm.group(1), tgt=mm.group(2),
                        take=int(mm.group(3)), mod=int(mm.group(4)),
                        agnd=int(mm.group(6)), tgnd=int(mm.group(7)),
                        rng=int(mm.group(8))))
                continue

            if c0 == 'I' and line.startswith('INTERPOSE'):
                mm = RE_INTERPOSE.match(line)
                if mm:
                    interpose_lines.append((ln, mm.group(1)))
                continue

            if c0 == 'R' and line.startswith('RALLY'):
                mm = RE_RALLY.match(line)
                if mm:
                    rally_lines.append((ln, mm.group(1)))
                continue

            if c0 == 'F' and line.startswith('FLAGDIAG: '):
                mm = RE_FLAGORG.match(line)
                if mm:
                    stands[mm.group(1)] = (float(mm.group(2)), float(mm.group(3)),
                                           float(mm.group(4)))
                continue

            if c0 == 'W' and line.startswith('WEDGEKILL'):
                mm = RE_WEDGE.match(line)
                if mm:
                    events.append((ln, 'wedge', dict(name=mm.group(1))))
                continue

            # broadcast lines
            if '[SG]' in line:
                mm = RE_TEAM.match(line)
                if mm:
                    teamof[mm.group(1)] = 1 if mm.group(2) == 'red' else 2
                    names.add(mm.group(1))
                    continue
                mm = RE_STOLE.match(line)
                if mm:
                    events.append((ln, 'stole', dict(name=mm.group(1),
                                                     flag=mm.group(2))))
                    continue
                mm = RE_CAP.match(line)
                if mm:
                    events.append((ln, 'cap', dict(name=mm.group(1),
                                                   flag=mm.group(2))))
                    continue
                mm = RE_RET.match(line)
                if mm:
                    events.append((ln, 'return', dict(name=mm.group(1))))
                    continue
                mm = RE_LOSTFLAG.match(line)
                if mm:
                    events.append((ln, 'lostflag', dict(name=mm.group(1))))
                    continue
                mm = RE_FCKILL.match(line)
                if mm:
                    events.append((ln, 'fckill', dict(killer=mm.group(1))))
                    continue
                mm = RE_OBIT_SELF.match(line)
                if mm:
                    events.append((ln, 'obit', dict(victim=mm.group(1),
                                                    killer=None, text=line)))
                    continue
                for rx in RE_OBIT_KILLER:
                    mm = rx.match(line)
                    if mm:
                        events.append((ln, 'obit', dict(
                            victim=mm.group(1), killer=mm.group(2), text=line)))
                        break
                continue

    # ---- clock: frames -> seconds, calibrated off reported carry durations
    def frames_at(ln):
        if not clock:
            return None
        lo, hi = 0, len(clock) - 1
        if ln <= clock[0][0]:
            return float(clock[0][1])
        if ln >= clock[-1][0]:
            return float(clock[-1][1])
        while lo < hi - 1:
            mid = (lo + hi) // 2
            if clock[mid][0] <= ln:
                lo = mid
            else:
                hi = mid
        l0, f0 = clock[lo]
        l1, f1 = clock[hi]
        if l1 == l0:
            return float(f0)
        return f0 + (f1 - f0) * (ln - l0) / (l1 - l0)

    ratios = []
    for cy in carries:
        if cy['dur'] < 3.0:
            continue
        f0, f1 = frames_at(cy['begin']), frames_at(cy['end'])
        if f0 is None or f1 is None or f1 <= f0:
            continue
        ratios.append(cy['dur'] / (f1 - f0))
    ratios.sort()
    k = ratios[len(ratios) // 2] if ratios else 0.01   # sec per frame-unit

    def tsec(ln):
        f = frames_at(ln)
        return None if f is None else f * k

    # ---- index events
    deaths = [(ln, p) for ln, kind, p in events if kind == 'death']
    obits = [(ln, p) for ln, kind, p in events if kind == 'obit']
    caps = [(ln, p) for ln, kind, p in events if kind == 'cap']
    steals = [(ln, p) for ln, kind, p in events if kind == 'stole']
    wedges = [(ln, p) for ln, kind, p in events if kind == 'wedge']
    rets = [(ln, p) for ln, kind, p in events if kind == 'return']

    death_by_name = {}
    for ln, p in deaths:
        death_by_name.setdefault(p['name'], []).append(ln)

    # infer team from steal if BOTDEATH never fired for a bot
    for ln, p in steals:
        # stealing the red flag => the thief is blue (team 2)
        teamof.setdefault(p['name'], 2 if p['flag'] == 'red' else 1)

    out = []
    for cy in carries:
        nm = cy['name']
        b, e = cy['begin'], cy['end']
        team = teamof.get(nm)
        samples = cy['samples']
        if not samples:
            continue

        # which flag: nearest preceding steal by this bot
        flag = None
        for ln, p in reversed(steals):
            if ln <= b and p['name'] == nm:
                flag = p['flag']
                break
        if flag is None and team:
            flag = 'red' if team == 2 else 'blue'
        steal_stand = stands.get(flag)
        home_stand = stands.get('blue' if flag == 'red' else 'red')

        goals = [s['goal'] for s in samples if s['goal'] > 0]
        if not goals:
            continue
        g0 = goals[0]
        # the 1Hz SG sample under-reports the true minimum; the progress
        # guard's own best= column (10fps) fills the gaps it saw
        gmin = min(goals + [bst for _, bst, _ in cy['carrylost'] if bst > 0])
        best_i = min(range(len(samples)),
                     key=lambda i: samples[i]['goal'] if samples[i]['goal'] > 0 else 1 << 30)
        frac_rem = gmin / g0 if g0 > 0 else None

        # outcome
        outcome = 'other'
        killer = None
        killer_mod = None
        death_ln = None
        obit_text = None
        capped = any(p['name'] == nm and b <= ln <= e + 200 for ln, p in caps)
        if capped:
            outcome = 'cap'
        else:
            cands = [ln for ln in death_by_name.get(nm, []) if b <= ln <= e + 300]
            if cands:
                death_ln = min(cands, key=lambda x: abs(x - e))
                dp = next(p for ln, p in deaths if ln == death_ln)
                killer_mod = dp['mod']
            anch = death_ln if death_ln is not None else e
            ocands = [(ln, p) for ln, p in obits
                      if p['victim'] == nm and b <= ln <= e + 300]
            if ocands:
                oln, op = min(ocands, key=lambda x: abs(x[0] - anch))
                obit_text = op['text']
                killer = op['killer']
                if death_ln is None:
                    death_ln = oln
                    anch = oln
            # "X killed the enemy flag carrier." -- fires even when the
            # obituary template is one we did not whitelist
            fck = [(ln, p) for ln, p in
                   [(l, q) for l, kind, q in events if kind == 'fckill']
                   if abs(ln - anch) <= 60 and b <= ln <= e + 300]
            if fck and not killer:
                killer = min(fck, key=lambda x: abs(x[0] - anch))[1]['killer']
            wedged = any(p['name'] == nm and b <= ln <= e + 300 for ln, p in wedges)
            if wedged:
                outcome = 'wedge'
                killer = None
            elif killer and killer != nm:
                outcome = ('teamkill' if teamof.get(killer) == team else 'killed')
            elif obit_text is not None:
                outcome = 'self'
            elif killer_mod in SELF_MODS:
                outcome = 'self'
            elif death_ln is not None:
                outcome = 'killed_unknown'
            elif any(b <= ln <= e + 300 for ln, p in rets):
                outcome = 'returned'

        anchor = death_ln if death_ln is not None else e
        carrier_org = None
        for s in reversed(samples):
            if s['line'] <= anchor:
                carrier_org = s['org']
                break
        if carrier_org is None:
            carrier_org = samples[-1]['org']

        rec = dict(
            iter=wave, slot=slot, fmt=fmt, map=mapname, cover=cover,
            doctrine=doctrine, carrier=nm, team=team, flag=flag,
            begin=b, end=e, dur=cy['dur'], death_line=death_ln,
            g0=g0, gmin=gmin, gend=goals[-1], frac_rem=frac_rem,
            grab_room=(cy['grabmode'][0] if cy['grabmode'] else None),
            grab_mode=(cy['grabmode'][1] if cy['grabmode'] else None),
            outcome=outcome, killer=killer, mod=killer_mod,
            modname=MODNAMES.get(killer_mod, str(killer_mod)),
            obit=obit_text, nsamples=len(samples),
            carrylost=len(cy['carrylost']),
            spd_med=sorted(s['spd'] for s in samples)[len(samples) // 2],
            spd_mean=sum(s['spd'] for s in samples) / len(samples),
            act_neg=sum(1 for s in samples if s['act'] < 0),
            gnd_frac=sum(s['gnd'] for s in samples) / len(samples),
            carrier_org=carrier_org,
            t_begin=tsec(b), t_end=tsec(e),
            t_best=tsec(samples[best_i]['line']),
            frac_death=(goals[-1] / g0) if g0 > 0 else None,
            regress=goals[-1] - gmin,
        )
        if rec['t_best'] is not None and rec['t_begin'] is not None:
            rec['t_to_best'] = rec['t_best'] - rec['t_begin']
            rec['t_after_best'] = (rec['t_end'] or 0) - rec['t_best']

        # ---- geometry
        if steal_stand and home_stand:
            rec['route_len'] = dist(steal_stand, home_stand)
            rec['carrier_p'] = proj_frac(carrier_org, steal_stand, home_stand)
            rec['carrier_d_steal'] = dist(carrier_org, steal_stand)
            rec['carrier_d_home'] = dist(carrier_org, home_stand)
            best_org = samples[best_i]['org']
            rec['best_p'] = proj_frac(best_org, steal_stand, home_stand)
            rec['best_d_home'] = dist(best_org, home_stand)
            rec['best_d_steal'] = dist(best_org, steal_stand)
            rec['travel'] = dist(samples[0]['org'], carrier_org)

        # ---- killer forensics
        if killer:
            khist = sghist.get(killer, [])
            korg = None
            for kln, kor, krole, kspd in reversed(khist):
                if kln <= anchor:
                    korg = kor
                    rec['killer_role'] = krole
                    rec['killer_spd'] = kspd
                    rec['killer_sg_lag'] = anchor - kln
                    break
            if korg:
                rec['killer_org'] = korg
                rec['killer_d_carrier'] = dist(korg, carrier_org)
                if steal_stand and home_stand:
                    rec['killer_d_steal'] = dist(korg, steal_stand)
                    rec['killer_d_home'] = dist(korg, home_stand)
                    rec['killer_p'] = proj_frac(korg, steal_stand, home_stand)
            # was the killer already standing there, or did it arrive?
            # SG lines are 1 Hz per bot, so 5 samples back is ~5 s back.
            idx = None
            for j in range(len(khist) - 1, -1, -1):
                if khist[j][0] <= anchor:
                    idx = j
                    break
            if idx is not None and idx >= 5:
                p5 = khist[idx - 5][1]
                rec['killer_move_5s'] = dist(p5, khist[idx][1])
                if steal_stand:
                    rec['killer_d_steal_5s'] = dist(p5, steal_stand)
            if idx is not None and idx >= 10:
                p10 = khist[idx - 10][1]
                rec['killer_move_10s'] = dist(p10, khist[idx][1])
                if steal_stand:
                    rec['killer_d_steal_10s'] = dist(p10, steal_stand)

            # respawn stream: killer's own last death before the kill
            kd = [ln for ln in death_by_name.get(killer, []) if ln < anchor]
            if kd:
                last = max(kd)
                t1, t2 = tsec(last), tsec(anchor)
                if t1 is not None and t2 is not None:
                    rec['killer_since_death'] = t2 - t1
                rec['killer_deaths_before'] = len(kd)
            else:
                rec['killer_since_death'] = None
                rec['killer_deaths_before'] = 0

        # ---- escort geometry at the kill moment
        if team:
            mates = [n for n, t in teamof.items() if t == team and n != nm]
            best_d = None
            n_within = {500: 0, 800: 0, 1200: 0}
            for mt in mates:
                mh = sghist.get(mt, [])
                morg = None
                for mln, mor, mrole, mspd in reversed(mh):
                    if mln <= anchor:
                        if anchor - mln > 2500:   # stale sample
                            break
                        morg = mor
                        break
                if morg is None:
                    continue
                d = dist(morg, carrier_org)
                if best_d is None or d < best_d:
                    best_d = d
                for thr in n_within:
                    if d < thr:
                        n_within[thr] += 1
            # same census at the GRAB, not the kill: the actionable moment
            grab_org = samples[0]['org']
            gm, gf = None, None
            gm500 = 0
            for who, store in (('m', mates),
                               ('f', [n for n, t in teamof.items()
                                      if t and t != team])):
                for xt in store:
                    xh = sghist.get(xt, [])
                    xorg = None
                    for xln, xo, xr, xs in reversed(xh):
                        if xln <= samples[0]['line']:
                            if samples[0]['line'] - xln > 2500:
                                break
                            xorg = xo
                            break
                    if xorg is None:
                        continue
                    d = dist(xorg, grab_org)
                    if who == 'm':
                        if gm is None or d < gm:
                            gm = d
                        if d < 500:
                            gm500 += 1
                    else:
                        if gf is None or d < gf:
                            gf = d
            rec['mate_nearest0'] = gm
            rec['mates0_500'] = gm500
            rec['foe_nearest0'] = gf

            rec['mate_nearest'] = best_d
            rec['mates_500'] = n_within[500]
            rec['mates_800'] = n_within[800]
            rec['mates_1200'] = n_within[1200]

            # enemy collapse: how many opponents were near the carrier
            foes = [n for n, t in teamof.items() if t and t != team]
            e_best, e_900 = None, 0
            for ft in foes:
                fh2 = sghist.get(ft, [])
                forg = None
                for fln, fo, frole, fspd in reversed(fh2):
                    if fln <= anchor:
                        if anchor - fln > 2500:
                            break
                        forg = fo
                        break
                if forg is None:
                    continue
                d = dist(forg, carrier_org)
                if e_best is None or d < e_best:
                    e_best = d
                if d < 900:
                    e_900 += 1
            rec['foe_nearest'] = e_best
            rec['foes_900'] = e_900

            # is anyone actually ON the line the shot took?  For every
            # teammate with a fresh sample, project onto carrier->killer.
            korg = rec.get('killer_org')
            if korg:
                on_line = 0
                best_perp = None
                for mt in mates:
                    mh = sghist.get(mt, [])
                    morg = None
                    mrole = None
                    for mln, mor, mr, msp in reversed(mh):
                        if mln <= anchor:
                            if anchor - mln > 2500:
                                break
                            morg, mrole = mor, mr
                            break
                    if morg is None:
                        continue
                    t = proj_frac(morg, carrier_org, korg)
                    if t is None:
                        continue
                    seg = dist(carrier_org, korg)
                    foot = [carrier_org[i] + t * (korg[i] - carrier_org[i])
                            for i in range(3)]
                    perp = dist(morg, foot)
                    if 0.0 <= t <= 1.0:
                        if best_perp is None or perp < best_perp:
                            best_perp = perp
                        if perp < 120.0:
                            on_line += 1
                rec['screen_on_line'] = on_line
                rec['screen_perp'] = best_perp

            # roles the rest of the team was running when the carry ended
            rc = Counter() if False else {}
            for mt in mates:
                mh = sghist.get(mt, [])
                for mln, mor, mr, msp in reversed(mh):
                    if mln <= anchor:
                        if anchor - mln <= 2500:
                            rc[mr] = rc.get(mr, 0) + 1
                        break
            rec['mate_roles'] = {str(k): v for k, v in rc.items()}

        # kit at the moment the carry ended (armour/health census)
        kit_at = None
        for kln, kit in reversed(kits):
            if kln <= anchor:
                kit_at = kit
                break
        kit_start = None
        for kln, kit in reversed(kits):
            if kln <= b:
                kit_start = kit
                break
        if kit_at and nm in kit_at:
            rec['kit_weap'], rec['kit_armour'], rec['kit_health'] = kit_at[nm]
        if kit_start and nm in kit_start:
            rec['kit0_weap'], rec['kit0_armour'], rec['kit0_health'] = kit_start[nm]

        # interpose activity during the carry (teammates only)
        rec['interpose_n'] = sum(1 for ln, n in interpose_lines
                                 if b <= ln <= anchor and teamof.get(n) == team)
        rec['interpose_bots'] = len(set(n for ln, n in interpose_lines
                                        if b <= ln <= anchor and teamof.get(n) == team))
        rec['rally_n'] = sum(1 for ln, n in rally_lines
                             if b <= ln <= anchor and teamof.get(n) == team)

        # damage taken while carrying
        dmgs = [d for d in dmg_on_carrier if b <= d['line'] <= anchor + 5
                and d['tgt'] == nm]
        rec['dmg_n'] = len(dmgs)
        rec['dmg_total'] = sum(d['take'] for d in dmgs)
        rec['dmg_by_mod'] = {}
        rec['dmg_attackers'] = len(set(d['att'] for d in dmgs))
        for d in dmgs:
            key = MODNAMES.get(d['mod'], str(d['mod']))
            rec['dmg_by_mod'][key] = rec['dmg_by_mod'].get(key, 0) + d['take']
        if dmgs:
            rec['dmg_rng_med'] = sorted(d['rng'] for d in dmgs)[len(dmgs) // 2]
            rec['dmg_agnd_frac'] = sum(d['agnd'] for d in dmgs) / len(dmgs)
        # final blow damage line (last one before the death anchor)
        if dmgs:
            fb = dmgs[-1]
            rec['final_att'] = fb['att']
            rec['final_mod'] = MODNAMES.get(fb['mod'], str(fb['mod']))
            rec['final_rng'] = fb['rng']

        out.append(rec)

    return out


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else \
        '/home/buzzkill/Projects/lmctf6-stats/tools'
    lo = int(sys.argv[2]) if len(sys.argv) > 2 else 274
    hi = int(sys.argv[3]) if len(sys.argv) > 3 else 308
    outp = sys.argv[4] if len(sys.argv) > 4 else '/dev/stdout'

    files = []
    for w in range(lo, hi + 1):
        d = os.path.join(root, 'iter-%d' % w)
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if f.endswith('.log'):
                files.append(os.path.join(d, f))

    with Pool(8) as p:
        results = p.map(parse_file, files, chunksize=1)

    with open(outp, 'w') as fh:
        for rs in results:
            for r in rs:
                fh.write(json.dumps(r) + '\n')
    sys.stderr.write('files=%d carries=%d\n'
                     % (len(files), sum(len(r) for r in results)))


if __name__ == '__main__':
    main()
