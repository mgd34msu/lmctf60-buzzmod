#!/usr/bin/env python3
"""defreport.py -- readable tables out of the <map>.defense.json files.

Usage: defreport.py <human_dir> [--posts N]
"""
import json, sys, os, glob, collections

d = sys.argv[1]
NP = int(sys.argv[2]) if len(sys.argv) > 2 else 8

tot = collections.Counter()
allkinds = collections.Counter()
delays = []
print('== corpus ==')
print(f"{'map':9s} {'demos':>5s} {'min':>6s} {'defs':>4s} {'dwell_r':>7s} "
      f"{'dwell_b':>7s} {'steals':>6s} {'resp':>5s} {'desync':>6s}")
for p in sorted(glob.glob(os.path.join(d, '*.defense.json'))):
    j = json.load(open(p))
    r = j['response']
    print(f"{j['map']:9s} {j['demos']:5d} {j['frames']/600:6.0f} "
          f"{len(j['defenders']):4d} {j['dwell_secs']['red']:7.0f} "
          f"{j['dwell_secs']['blue']:7.0f} {r['steals_seen']:6d} "
          f"{r['samples']:5d} {sum(x.get('desyncs',0) for x in [j]) or 0:6d}")
    tot['demos'] += j['demos']; tot['steals'] += r['steals_seen']
    tot['resp'] += r['samples']

print()
for p in sorted(glob.glob(os.path.join(d, '*.defense.json'))):
    j = json.load(open(p))
    print(f"=== {j['map']}  demos={j['demos']} "
          f"flag_seed red={j['flag_seed'].get('red')} "
          f"blue={j['flag_seed'].get('blue')} ===")
    for team in ('red', 'blue'):
        ps = j['posts_by_team'][team][:NP]
        if not ps:
            print(f"  {team}: (no dwell)")
            continue
        tot_s = sum(x['secs'] for x in j['posts_by_team'][team])
        print(f"  {team} posts (total dwell {tot_s:.0f}s):")
        for q in ps:
            fl = j['flags'][team]
            dh = round(sum((a-b)**2 for a, b in zip(q['xyz'], fl)) ** 0.5)
            print(f"    seed {q['seed']:5d} share {q['share']*100:5.1f}%  "
                  f"{q['secs']:6.1f}s  d_home {dh:5d}  "
                  f"xyz {q['xyz']}")
    r = j['response']
    if r['samples']:
        ks = ', '.join(f"{k} {v*100:.0f}%" for k, v in
                       sorted(r['kind_share'].items(), key=lambda kv: -kv[1]))
        print(f"  response: n={r['samples']} of {r['steals_seen']} steals | "
              f"delay med {r['delay_median']}s mean {r['delay_mean']}s "
              f"p90 {r['delay_p90']}s | never-left {r['never_left']}")
        print(f"            kinds: {ks}")
        print(f"            gap-to-carrier closed {r['gap_close_mean']}u | "
              f"drift off-post +{r['home_drift_mean']}u | "
              f"drift toward enemy base +{r['enemy_drift_mean']}u")
        print(f"            aim@carrier-now {r['aim_at_carrier_now']} vs "
              f"aim@carrier-lead {r['aim_at_carrier_lead']}")
        for team in ('red', 'blue'):
            ic = r['intercepts'][team][:4]
            if ic:
                print(f"            {team} intercepts: " + ', '.join(
                    f"seed {q['seed']} {q['share']*100:.0f}%" for q in ic))
        allkinds.update(r['kinds'])
        if r['delay_median'] is not None:
            delays.append((j['map'], r['delay_median'], r['samples']))
    else:
        print('  response: (no samples)')
    print()

print('== pooled response ==')
n = sum(allkinds.values()) or 1
for k, v in allkinds.most_common():
    print(f'  {k:12s} {v:5d} {100*v/n:5.1f}%')
w = sum(s for _, _, s in delays) or 1
print(f'  weighted median-delay {sum(m*s for _, m, s in delays)/w:.2f}s')
print(f"  totals: demos={tot['demos']} steals={tot['steals']} "
      f"responses={tot['resp']}")
