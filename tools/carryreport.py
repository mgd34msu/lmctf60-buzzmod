#!/usr/bin/env python3
"""carryreport.py -- read the carry ledger, answer the route-stage questions."""
import json
import sys
from collections import Counter, defaultdict

ROLE = {0: 'ATTACK', 1: 'DEFEND', 2: 'CARRY', 3: 'RECOVER', 4: 'ESCORT', None: '?'}


def med(xs):
    xs = sorted(x for x in xs if x is not None)
    return None if not xs else xs[len(xs) // 2]


def mean(xs):
    xs = [x for x in xs if x is not None]
    return None if not xs else sum(xs) / len(xs)


def fmt(x, n=1):
    return '-' if x is None else ('%.*f' % (n, x))


def stage(r):
    f = r.get('frac_rem')
    if f is None:
        return 'unk'
    if f > 0.75:
        return 'early'
    if f >= 0.25:
        return 'middle'
    return 'late'


def main():
    path = sys.argv[1]
    rs = [json.loads(l) for l in open(path)]
    for r in rs:
        r['stage'] = stage(r)
        r['parity'] = ('5v1' if r['slot'] == 's10'
                       else 'ctrl' if r['slot'] == 's09' and r['iter'] >= 284
                       else r['fmt'])
    print('=== corpus ===')
    print('carries=%d  waves %d-%d' % (len(rs), min(r['iter'] for r in rs),
                                       max(r['iter'] for r in rs)))
    print(Counter(r['parity'] for r in rs).most_common())
    print(Counter(r['outcome'] for r in rs).most_common())
    print()

    # -------- Q1: where do carries end
    print('=== Q1  route stage at best progress ===')
    print('%-8s %5s %6s %6s %6s %6s   %8s %8s %8s %7s' %
          ('group', 'n', 'early', 'mid', 'late', 'cap%', 'medFrac',
           'medDur', 'medG0', 'medGmin'))
    def block(label, sub):
        if not sub:
            return
        c = Counter(r['stage'] for r in sub)
        n = len(sub)
        ncap = sum(1 for r in sub if r['outcome'] == 'cap')
        print('%-8s %5d %5.0f%% %5.0f%% %5.0f%% %5.1f%%   %8s %8s %8s %7s' % (
            label, n, 100 * c['early'] / n, 100 * c['middle'] / n,
            100 * c['late'] / n, 100 * ncap / n,
            fmt(med([r['frac_rem'] for r in sub]), 2),
            fmt(med([r['dur'] for r in sub])),
            fmt(med([r['g0'] for r in sub]), 0),
            fmt(med([r['gmin'] for r in sub]), 0)))

    for g in ('2v2', '5v5', '5v3', '7v7', 'ctrl', '5v1'):
        block(g, [r for r in rs if r['parity'] == g])
    print()
    print('-- cover dose (parity servers only, s01-s08) --')
    par = [r for r in rs if r['slot'] not in ('s09', 's10')]
    for dose in sorted(set(r['cover'] for r in par)):
        block('cov%d' % dose, [r for r in par if r['cover'] == dose])
    print()
    print('-- 2v2 cover A/B (s01/s02: 0 in waves 274-283, 800 in 284+) --')
    duel = [r for r in rs if r['slot'] in ('s01', 's02')]
    block('duel-0', [r for r in duel if r['cover'] == 0])
    block('duel-800', [r for r in duel if r['cover'] == 800])
    print()

    # -------- Q2: who kills them, per stage
    print('=== Q2  killer profile by route stage (parity servers) ===')
    par = [r for r in rs if r['slot'] not in ('s10',)]
    for st in ('early', 'middle', 'late'):
        sub = [r for r in par if r['stage'] == st]
        if not sub:
            continue
        oc = Counter(r['outcome'] for r in sub)
        k = [r for r in sub if r['outcome'] == 'killed']
        print('\n-- %s  n=%d  outcomes=%s' % (st, len(sub), dict(oc)))
        if not k:
            continue
        print('   killer role : %s' % dict(
            Counter(ROLE.get(r.get('killer_role'), '?') for r in k)))
        print('   killer mod  : %s' % dict(
            Counter(r.get('modname') for r in k).most_common(6)))
        # position class
        cls = Counter()
        for r in k:
            ds, dh = r.get('killer_d_steal'), r.get('killer_d_home')
            kp, cp = r.get('killer_p'), r.get('carrier_p')
            if ds is None:
                cls['?'] += 1
                continue
            if ds < 1000:
                cls['at enemy stand'] += 1
            elif dh is not None and dh < 1000:
                cls['at our stand'] += 1
            elif kp is not None and cp is not None:
                if kp < cp - 0.08:
                    cls['midmap behind (chase)'] += 1
                elif kp > cp + 0.08:
                    cls['midmap ahead (cut-off)'] += 1
                else:
                    cls['midmap alongside'] += 1
            else:
                cls['midmap ?'] += 1
        print('   killer place: %s' % dict(cls.most_common()))
        print('   med dist killer->carrier %s   killer->enemystand %s   ->ourstand %s'
              % (fmt(med([r.get('killer_d_carrier') for r in k]), 0),
                 fmt(med([r.get('killer_d_steal') for r in k]), 0),
                 fmt(med([r.get('killer_d_home') for r in k]), 0)))
        print('   med final-blow range %s  med dmg-range %s  grounded-attacker frac %s'
              % (fmt(med([r.get('final_rng') for r in k]), 0),
                 fmt(med([r.get('dmg_rng_med') for r in k]), 0),
                 fmt(mean([r.get('dmg_agnd_frac') for r in k]), 2)))

    # -------- Q3: respawn stream
    print('\n=== Q3  respawn stream: killer time since own last death ===')
    for st in ('early', 'middle', 'late'):
        k = [r for r in par if r['stage'] == st and r['outcome'] == 'killed']
        v = [r.get('killer_since_death') for r in k
             if r.get('killer_since_death') is not None]
        never = sum(1 for r in k if r.get('killer_deaths_before') == 0)
        if not v:
            continue
        b = Counter()
        for x in v:
            b['<=5s' if x <= 5 else '5-10s' if x <= 10 else
              '10-20s' if x <= 20 else '20-40s' if x <= 40 else '>40s'] += 1
        print('  %-6s n=%3d  never-died=%d  med=%ss  %s' %
              (st, len(k), never, fmt(med(v)),
               {kk: '%d (%.0f%%)' % (vv, 100 * vv / len(v))
                for kk, vv in b.most_common()}))

    # -------- Q4: escort
    print('\n=== Q4  escort presence at the kill ===')
    print('%-7s %5s %8s %8s %7s %7s %8s %8s %9s' %
          ('stage', 'n', 'medMate', 'medFoe', 'mate<500', 'mate<800',
           'medFoes900', 'interpN', 'interpBots'))
    for st in ('early', 'middle', 'late'):
        sub = [r for r in par if r['stage'] == st]
        if not sub:
            continue
        n = len(sub)
        print('%-7s %5d %8s %8s %6.0f%% %6.0f%% %8s %8s %9s' % (
            st, n,
            fmt(med([r.get('mate_nearest') for r in sub]), 0),
            fmt(med([r.get('foe_nearest') for r in sub]), 0),
            100 * sum(1 for r in sub if (r.get('mates_500') or 0) > 0) / n,
            100 * sum(1 for r in sub if (r.get('mates_800') or 0) > 0) / n,
            fmt(med([r.get('foes_900') for r in sub]), 1),
            fmt(med([r.get('interpose_n') for r in sub]), 0),
            fmt(med([r.get('interpose_bots') for r in sub]), 1)))

    # -------- Q5: what distinguishes the conversions
    print('\n=== Q5  conversions vs deaths ===')
    caps = [r for r in rs if r['outcome'] == 'cap']
    print('caps n=%d' % len(caps))
    for r in sorted(caps, key=lambda x: (x['iter'], x['slot'])):
        print('  w%-3d %-4s %-5s %-8s %-11s dur=%6.1f g0=%6d gmin=%5d '
              'spd=%3d/%3d mate=%-6s foes900=%-3s interp=%-4s dmgN=%-3s' % (
                  r['iter'], r['slot'], r['parity'], r['map'], r['carrier'],
                  r['dur'], r['g0'], r['gmin'], r['spd_med'],
                  round(r['spd_mean']),
                  fmt(r.get('mate_nearest'), 0), r.get('foes_900'),
                  r.get('interpose_n'), r.get('dmg_n')))

    print('\n-- cap vs non-cap, parity servers only --')
    pcap = [r for r in par if r['outcome'] == 'cap']
    pnon = [r for r in par if r['outcome'] != 'cap']
    s10 = [r for r in rs if r['slot'] == 's10']
    s10cap = [r for r in s10 if r['outcome'] == 'cap']
    s10non = [r for r in s10 if r['outcome'] != 'cap']
    cols = [('n', lambda g: len(g)),
            ('medDur', lambda g: med([r['dur'] for r in g])),
            ('medFracRem', lambda g: med([r['frac_rem'] for r in g])),
            ('medSpd', lambda g: med([r['spd_med'] for r in g])),
            ('meanSpd', lambda g: mean([r['spd_mean'] for r in g])),
            ('gndFrac', lambda g: mean([r['gnd_frac'] for r in g])),
            ('medMate', lambda g: med([r.get('mate_nearest') for r in g])),
            ('medFoeNear', lambda g: med([r.get('foe_nearest') for r in g])),
            ('medFoes900', lambda g: mean([r.get('foes_900') for r in g])),
            ('medDmgN', lambda g: med([r.get('dmg_n') for r in g])),
            ('medDmgTot', lambda g: med([r.get('dmg_total') for r in g])),
            ('medInterp', lambda g: med([r.get('interpose_n') for r in g])),
            ('medCarryLost', lambda g: mean([r.get('carrylost') for r in g])),
            ('actNegMean', lambda g: mean([r.get('act_neg') for r in g])),
            ]
    groups = [('parity-cap', pcap), ('parity-other', pnon),
              ('5v1-cap', s10cap), ('5v1-other', s10non)]
    print('%-14s' % 'metric' + ''.join('%14s' % g[0] for g in groups))
    for name, fn in cols:
        print('%-14s' % name + ''.join('%14s' % fmt(fn(g[1]), 2)
                                       for g in groups))

    # -------- outcome census by stage incl. self kills
    print('\n=== outcome x stage (all servers) ===')
    outs = sorted(set(r['outcome'] for r in rs))
    print('%-8s' % 'stage' + ''.join('%10s' % o for o in outs) + '%8s' % 'n')
    for st in ('early', 'middle', 'late', 'unk'):
        sub = [r for r in rs if r['stage'] == st]
        if not sub:
            continue
        c = Counter(r['outcome'] for r in sub)
        print('%-8s' % st + ''.join('%10s' % c.get(o, 0) for o in outs)
              + '%8d' % len(sub))

    # -------- self/wedge deep dive
    print('\n=== self-inflicted carry ends (parity) ===')
    sw = [r for r in par if r['outcome'] in ('self', 'wedge')]
    print('n=%d of %d parity carries (%.0f%%)' % (len(sw), len(par),
                                                  100 * len(sw) / len(par)))
    print('  by stage: %s' % dict(Counter(r['stage'] for r in sw)))
    print('  by mod  : %s' % dict(Counter(r.get('modname') for r in sw).most_common()))
    print('  by map  : %s' % dict(Counter(r['map'] for r in sw).most_common()))
    print('  med dur %s  med spd %s' % (fmt(med([r['dur'] for r in sw])),
                                        fmt(med([r['spd_med'] for r in sw]), 0)))

    # -------- map breakdown
    print('\n=== by map (parity servers) ===')
    print('%-9s %5s %6s %6s %6s %6s %7s %8s' %
          ('map', 'n', 'early', 'mid', 'late', 'caps', 'medFrac', 'medDur'))
    for mp in sorted(set(r['map'] for r in par)):
        sub = [r for r in par if r['map'] == mp]
        c = Counter(r['stage'] for r in sub)
        n = len(sub)
        print('%-9s %5d %5.0f%% %5.0f%% %5.0f%% %6d %7s %8s' % (
            mp, n, 100 * c['early'] / n, 100 * c['middle'] / n,
            100 * c['late'] / n,
            sum(1 for r in sub if r['outcome'] == 'cap'),
            fmt(med([r['frac_rem'] for r in sub]), 2),
            fmt(med([r['dur'] for r in sub]))))

    # -------- progress dynamics: reach then regress?
    print('\n=== progress dynamics (parity) ===')
    print('med time-to-best %ss  med time-after-best %ss  med regress %s ms'
          % (fmt(med([r.get('t_to_best') for r in par])),
             fmt(med([r.get('t_after_best') for r in par])),
             fmt(med([r.get('regress') for r in par]), 0)))
    for st in ('early', 'middle', 'late'):
        sub = [r for r in par if r['stage'] == st]
        print('  %-6s n=%3d  t_to_best=%5ss  t_after_best=%5ss  regress=%6s  '
              'carrylost=%s' % (
                  st, len(sub), fmt(med([r.get('t_to_best') for r in sub])),
                  fmt(med([r.get('t_after_best') for r in sub])),
                  fmt(med([r.get('regress') for r in sub]), 0),
                  fmt(mean([r.get('carrylost') for r in sub]), 2)))


if __name__ == '__main__':
    main()
