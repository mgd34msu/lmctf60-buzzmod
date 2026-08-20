#!/usr/bin/env python3
"""Derive map-specific human defense behavior from demos.

The tool identifies defenders from team skins and flag state, then measures
stand occupancy and post-steal reactions. Output is identity-stamped development
data for optional defense sidecars.
"""
import struct, sys, os, re, json, math, glob, argparse, collections
import multiprocessing as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dm2speed as D
from demokin import parse_playerstate_full
from demoents import parse_delta_entity_track
from mapflags import flag_origins, load_graph_metadata, read_graph_metadata
from corpusgraph import (atomic_write_json, rune_identity_from_rune,
                         stamp_corpus_identity)

U_REMOVE = 1 << 6
CS_MODELS = 32
CS_MAXCLIENTS = 60
CS_PLAYERSKINS = 1312
HZ = 10.0


# --------------------------------------------------------------- rune index
class SeedGrid:
    """spatial hash: nearest seed without an O(n) scan per frame"""
    CELL = 256.0

    def __init__(self, seeds, eligible=None):
        self.seeds = seeds
        self.cells = collections.defaultdict(list)
        if eligible is None:
            self.eligible = None
        else:
            checked = set()
            for i in eligible:
                if (isinstance(i, bool) or not isinstance(i, int) or
                        not 0 <= i < len(seeds) or i in checked):
                    raise ValueError(f'invalid eligible seed {i!r}')
                checked.add(i)
            self.eligible = frozenset(checked)
        for i, s in enumerate(seeds):
            self.cells[self.key(s)].append(i)

    def key(self, p):
        return (int(p[0] // self.CELL), int(p[1] // self.CELL),
                int(p[2] // self.CELL))

    def nearest(self, p, maxr=340.0):
        kx, ky, kz = self.key(p)
        best, bd = -1, maxr * maxr
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for i in self.cells.get((kx + dx, ky + dy, kz + dz), ()):
                        s = self.seeds[i]
                        d = ((s[0]-p[0])**2 + (s[1]-p[1])**2 + (s[2]-p[2])**2)
                        if d < bd:
                            bd, best = d, i
        if (best >= 0 and self.eligible is not None and
                best not in self.eligible):
            return -1
        return best


# ------------------------------------------------------------- demo walking
def walk(path, maxplayers=32):
    """one pass: player tracks, skin history, prints, map, POV slot"""
    data = open(path, 'rb').read()
    off = 0
    pov = None
    mapname = None
    maxclients = None
    skin_hist = []          # (frame, slot, skinstring)
    prints = []             # (frame, level, text)
    ents = {}               # entnum -> live [x, y, z]
    tracks = {}             # entnum -> [(frame, x, y, z), ...]
    seen_slots = set()
    frame_idx = 0
    desyncs = 0
    while off + 4 <= len(data):
        (mlen,) = struct.unpack_from('<i', data, off)
        off += 4
        if mlen == -1 or mlen < 0 or off + mlen > len(data):
            break
        r = D.R(data[off:off + mlen])
        off += mlen
        try:
            while not r.done():
                svc = r.u8()
                if svc == 12:
                    # only the real serverdata counts: once sync is lost a
                    # stray 0x0c byte reads as svc 12 and would clobber the
                    # POV slot with garbage
                    r.skip(9); r.str_(); n = r.u16(); r.str_()
                    if pov is None:
                        pov = n
                elif svc == 13:
                    idx = r.u16(); s = r.str_()
                    if CS_PLAYERSKINS <= idx < CS_PLAYERSKINS + 256:
                        slot = idx - CS_PLAYERSKINS
                        skin_hist.append((frame_idx, slot, s))
                        if s:
                            seen_slots.add(slot)
                    elif idx == CS_MODELS + 1:
                        m = re.match(r'maps/(\w+)\.bsp', s)
                        if m:
                            mapname = m.group(1)
                    elif idx == CS_MAXCLIENTS:
                        try:
                            maxclients = int(s)
                        except ValueError:
                            pass
                elif svc == 14:
                    bits, num = D.parse_entity_bits(r)
                    parse_delta_entity_track(
                        r, bits, ents.setdefault(num, [0.0, 0.0, 0.0]))
                elif svc == 20:
                    r.skip(9); ab = r.u8(); r.skip(ab)
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
                        parse_delta_entity_track(
                            r, bits, ents.setdefault(num, [0.0, 0.0, 0.0]))
                    frame_idx += 1
                    for num, o in ents.items():
                        if 1 <= num <= maxplayers:
                            tracks.setdefault(num, []).append(
                                (frame_idx, o[0], o[1], o[2]))
                elif svc == 9: D.parse_sound(r)
                elif svc == 3: D.parse_temp_entity(r)
                elif svc in (1, 2): r.skip(3)
                elif svc == 10:
                    lvl = r.u8(); s = r.str_()
                    prints.append((frame_idx, lvl, s.rstrip('\n')))
                elif svc in (11, 15, 4): r.str_()
                elif svc == 5: r.skip(512)
                elif svc in (6, 7): pass
                else: raise ValueError(svc)
        except Exception:
            desyncs += 1
            continue
    # entity numbers above the client range are world props, not players
    limit = maxclients if maxclients else (max(seen_slots) + 1 if seen_slots else 0)
    tracks = {n: t for n, t in tracks.items() if n - 1 in seen_slots and n <= limit}
    return {'map': mapname, 'pov': pov, 'frames': frame_idx, 'desyncs': desyncs,
            'skin_hist': skin_hist, 'prints': prints, 'tracks': tracks}


# ------------------------------------------------------------ skins / teams
SKIN_TEAM = re.compile(r'rb-([rb])', re.I)


def skin_team(s):
    m = SKIN_TEAM.search(s or '')
    if not m:
        return None
    return 'red' if m.group(1).lower() == 'r' else 'blue'


class Roster:
    """per-slot (name, team) as of any frame, from the configstring history"""

    def __init__(self, skin_hist):
        self.hist = collections.defaultdict(list)   # slot -> [(frame, name, team)]
        for f, slot, s in skin_hist:
            name = s.split('\\')[0] if s else ''
            self.hist[slot].append((f, name, skin_team(s)))
        for slot in self.hist:
            self.hist[slot].sort(key=lambda e: e[0])

    def at(self, slot, frame):
        h = self.hist.get(slot)
        if not h:
            return ('', None)
        best = None
        for f, name, team in h:
            if f <= frame:
                best = (name, team)
            else:
                break
        if best is None:
            best = (h[0][1], h[0][2])
        return best

    def slots_named(self, name, frame):
        """slots whose name matches at this frame (usually exactly one)"""
        out = []
        for slot in self.hist:
            n, t = self.at(slot, frame)
            if n and n == name:
                out.append((slot, t))
        return out

    def all_names(self):
        out = set()
        for slot, h in self.hist.items():
            for f, n, t in h:
                if n:
                    out.add(n)
        return out


# ------------------------------------------------------------- flag events
FLAG_RE = re.compile(
    r'^(?P<who>.+?) (?P<verb>stole|returned|captured|lost) '
    r'(the (?P<color>red|blue) flag\.|your flag!)$')


def parse_flag_events(prints, roster, pov_team):
    """[(frame, verb, color, actor_name, actor_slot, actor_team), ...]"""
    out = []
    for f, lvl, text in prints:
        m = FLAG_RE.match(text.strip())
        if not m:
            continue
        who = m.group('who')
        verb = m.group('verb')
        color = m.group('color')
        cands = roster.slots_named(who, f)
        slot, team = (cands[0] if cands else (None, None))
        if color is None:
            # "your flag": resolve from the actor's own team when we know it
            if team in ('red', 'blue'):
                color = team if verb == 'returned' else \
                    ('blue' if team == 'red' else 'red')
            elif pov_team in ('red', 'blue'):
                color = pov_team
            else:
                continue
        out.append((f, verb, color, who, slot, team))
    out.sort(key=lambda e: e[0])
    return out


def home_intervals(events, nframes):
    """per color, the frame ranges in which that flag sat on its stand"""
    res = {}
    for color in ('red', 'blue'):
        spans, home_since, home = [], 0, True
        for f, verb, c, who, slot, team in events:
            if c != color:
                continue
            if verb == 'stole' and home:
                spans.append((home_since, f))
                home = False
            elif verb in ('returned', 'captured') and not home:
                home_since = f
                home = True
        if home:
            spans.append((home_since, nframes))
        res[color] = [s for s in spans if s[1] > s[0]]
    return res


def in_spans(spans):
    """membership test as a sorted-boundary lookup"""
    import bisect
    starts = [a for a, b in spans]
    ends = [b for a, b in spans]

    def test(f):
        i = bisect.bisect_right(starts, f) - 1
        return i >= 0 and f < ends[i]
    return test


def dist(a, b):
    return math.sqrt((a[0]-b[0])**2 + (a[1]-b[1])**2 + (a[2]-b[2])**2)


# --------------------------------------------------------------- per demo
def analyse_demo(path, gamedir, cfg):
    d = walk(path)
    mapname = d['map']
    if not mapname:
        return None
    rune = os.path.join(gamedir, 'maps', f'{mapname}.rune')
    if not os.path.exists(rune) or d['frames'] < 600:
        return None
    flags = flag_origins(gamedir, mapname)
    if 'red' not in flags or 'blue' not in flags:
        return None
    rune_graph, seeds, eligible = load_graph_metadata(rune, mapname)
    grid = SeedGrid(seeds, eligible)

    roster = Roster(d['skin_hist'])
    pov_name, pov_team = roster.at(d['pov'], d['frames']) if d['pov'] is not None \
        else ('', None)
    events = parse_flag_events(d['prints'], roster, pov_team)
    homes = home_intervals(events, d['frames'])
    home_test = {c: in_spans(homes[c]) for c in ('red', 'blue')}

    R = collections.Counter          # shorthand
    res = {
        'map': mapname, 'demo': os.path.basename(path), 'frames': d['frames'],
        '_rune_identity': rune_identity_from_rune(rune_graph),
        'desyncs': d['desyncs'],
        'pov': pov_name, 'pov_team': pov_team,
        'defenders': [],
        # dwell_def: only players who cleared the >defshare bar
        # dwell_any: every same-team player standing still at home while the
        #            flag is on its stand -- that IS defending, whatever the
        #            player's nominal role, and it is the denser signal
        'dwell_def': {'red': R(), 'blue': R()},
        'dwell_any': {'red': R(), 'blue': R()},
        'visit_any': {'red': R(), 'blue': R()},
        'responses': [],
        'steals': 0,
    }

    # ---- who defends -----------------------------------------------------
    pos = {}          # ent -> {frame: (x,y,z)}
    team_of = {}
    for ent, tr in d['tracks'].items():
        slot = ent - 1
        name, team = roster.at(slot, tr[len(tr)//2][0])
        if team not in ('red', 'blue'):
            continue
        team_of[ent] = (name, team)
        pos[ent] = {f: (x, y, z) for f, x, y, z in tr}

    RADII = (600, 1000, 1500, 2000, 2500)
    defenders = {}
    for ent, (name, team) in team_of.items():
        stand = flags[team]
        htest = home_test[team]
        tot = 0
        hits = [0] * len(RADII)
        for f, p in pos[ent].items():
            if not htest(f):
                continue
            tot += 1
            dd = dist(p, stand)
            for k, rr in enumerate(RADII):
                if dd < rr:
                    hits[k] += 1
        if tot < cfg.minframes:
            continue
        share = hits[RADII.index(1500)] / tot
        rec = {'ent': ent, 'name': name, 'team': team, 'frames_home': tot,
               'near_share': round(share, 3),
               'shares': {str(rr): round(h / tot, 3)
                          for rr, h in zip(RADII, hits)},
               'is_pov': (ent - 1 == d['pov'])}
        res['defenders'].append(rec)
        if share >= cfg.defshare:
            defenders[ent] = (name, team)

    # ---- where they dwell ------------------------------------------------
    for ent, (name, team) in team_of.items():
        stand = flags[team]
        htest = home_test[team]
        tr = d['tracks'][ent]
        is_def = ent in defenders
        W = int(cfg.dwellwin * HZ / 2)
        for i in range(len(tr)):
            f, x, y, z = tr[i]
            if not htest(f):
                continue
            p = (x, y, z)
            if dist(p, stand) >= cfg.defradius:
                continue
            s = grid.nearest(p)
            if s < 0:
                continue
            res['visit_any'][team][s] += 1
            # a post is held, not merely touched: the player must stay inside
            # dwellspan for the whole dwellwin window, which counts someone
            # strafing in place as camping and a runner passing through as not
            if i - W < 0 or i + W >= len(tr):
                continue
            if tr[i + W][0] - tr[i - W][0] != 2 * W:
                continue
            if max(dist(p, tr[j][1:]) for j in range(i - W, i + W + 1)) \
                    >= cfg.dwellspan:
                continue
            res['dwell_any'][team][s] += 1
            if is_def:
                res['dwell_def'][team][s] += 1

    # ---- how they react to a steal ---------------------------------------
    win = int(cfg.window * HZ)
    for f0, verb, color, who, slot, team in events:
        if verb != 'stole':
            continue
        res['steals'] += 1
        stand = flags[color]
        other = 'blue' if color == 'red' else 'red'
        estand = flags[other]
        carrier_ent = (slot + 1) if slot is not None else None
        cpos = pos.get(carrier_ent, {}) if carrier_ent else {}
        for ent, (name, dteam) in team_of.items():
            if dteam != color or ent == carrier_ent:
                continue
            p0 = pos[ent].get(f0)
            if p0 is None or dist(p0, stand) >= cfg.defradius:
                continue
            seq = [(f, pos[ent][f]) for f in range(f0, f0 + win + 1)
                   if f in pos[ent]]
            # a death mid-window respawns the client somewhere else; that
            # jump is not movement, so the window ends at the first
            # physically impossible step
            for i in range(1, len(seq)):
                gap = max(1, seq[i][0] - seq[i-1][0])
                if dist(seq[i][1], seq[i-1][1]) * HZ / gap > cfg.teleport:
                    seq = seq[:i]
                    break
            if len(seq) < cfg.minresp:
                continue
            # departure delay: first sample more than 200u from the post
            delay = None
            for f, p in seq:
                if dist(p, p0) > 200.0:
                    delay = (f - f0) / HZ
                    break
            pend = seq[-1][1]
            disp = dist(p0, pend)
            maxdisp = max(dist(p0, p) for f, p in seq)
            # gap to the carrier, when the carrier is on screen
            both = [(f, p, cpos[f]) for f, p in seq if f in cpos]
            dc0 = dc1 = None
            if len(both) >= cfg.minresp // 2:
                dc0 = dist(both[0][1], both[0][2])
                dc1 = dist(both[-1][1], both[-1][2])
            dh0, dh1 = dist(p0, stand), dist(pend, stand)
            de0, de1 = dist(p0, estand), dist(pend, estand)
            if maxdisp < 300:
                kind = 'hold'
            elif dc0 is not None and dc0 - dc1 > 400:
                kind = 'chase'
            elif de0 - de1 > 500:
                kind = 'cutoff'
            else:
                kind = 'reposition'
            # heading test: aim at where the carrier IS vs where he ENDS UP
            aim_now = aim_lead = None
            if both and disp > 200:
                v = (pend[0]-p0[0], pend[1]-p0[1], pend[2]-p0[2])
                for tgt, key in ((both[0][2], 'now'), (both[-1][2], 'lead')):
                    w = (tgt[0]-p0[0], tgt[1]-p0[1], tgt[2]-p0[2])
                    nw = math.sqrt(sum(c*c for c in w)) or 1.0
                    cos = sum(a*b for a, b in zip(v, w)) / (disp * nw)
                    if key == 'now':
                        aim_now = cos
                    else:
                        aim_lead = cos
            rseeds = []
            for f, p in seq:
                s = grid.nearest(p)
                if s >= 0:
                    rseeds.append(s)
            res['responses'].append({
                'team': color, 'defender': name, 'carrier': who,
                'delay': delay, 'kind': kind,
                'disp': round(disp), 'maxdisp': round(maxdisp),
                'dc0': None if dc0 is None else round(dc0),
                'dc1': None if dc1 is None else round(dc1),
                'dhome0': round(dh0), 'dhome1': round(dh1),
                'denemy0': round(de0), 'denemy1': round(de1),
                'aim_now': None if aim_now is None else round(aim_now, 3),
                'aim_lead': None if aim_lead is None else round(aim_lead, 3),
                # where the defender actually ended up: the point a bot
                # would have to navigate to in order to copy the human
                'end_seed': grid.nearest(pend),
                'end_secs': round((seq[-1][0] - f0) / HZ, 1),
                'seeds': rseeds,
            })
    for k in ('dwell_def', 'dwell_any', 'visit_any'):
        res[k] = {t: dict(c) for t, c in res[k].items()}
    return res


def _worker(args):
    path, gamedir, cfg = args
    try:
        return analyse_demo(path, gamedir, cfg)
    except Exception as e:
        return {'error': f'{os.path.basename(path)}: {e!r}'}


# ----------------------------------------------------------------- rollup
def pick_posts(dwell, seeds, sep, limit, minshare):
    """greedy leader clustering by occupancy.

    Rune seeds are dense (a couple of hundred units apart), so a single
    camp spot spreads its seconds over half a dozen neighbours.  Walking
    the seeds heaviest-first and folding every seed within `sep` of an
    already-claimed centre into that centre turns those neighbours into
    one post whose share is the whole cluster's -- shares then sum to 1
    and mean "this fraction of all defensive standing-still time".
    """
    total = sum(dwell.values()) or 1
    order = sorted(dwell.items(), key=lambda kv: (-kv[1], kv[0]))
    centres = []            # [seed, secs_total, [member seeds]]
    for s, c in order:
        p = seeds[s]
        hit = None
        for cen in centres:
            if dist(p, seeds[cen[0]]) < sep:
                hit = cen
                break
        if hit:
            hit[1] += c
            hit[2].append(s)
        else:
            centres.append([s, c, [s]])
    centres.sort(key=lambda cen: -cen[1])
    posts = []
    for s, c, members in centres:
        if c / total < minshare:
            break
        posts.append({'seed': s, 'share': round(c / total, 4),
                      'secs': round(c / HZ, 1),
                      'xyz': [round(v) for v in seeds[s]],
                      'seeds': sorted(members)})
        if len(posts) >= limit:
            break
    return posts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gamedir', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--defradius', type=float, default=1500.0)
    ap.add_argument('--defshare', type=float, default=0.60)
    ap.add_argument('--minframes', type=int, default=300)
    ap.add_argument('--dwellwin', type=float, default=2.0)
    ap.add_argument('--dwellspan', type=float, default=300.0)
    ap.add_argument('--postsep', type=float, default=320.0)
    ap.add_argument('--postlimit', type=int, default=10)
    ap.add_argument('--minpostshare', type=float, default=0.01)
    ap.add_argument('--window', type=float, default=10.0)
    ap.add_argument('--minresp', type=int, default=40)
    ap.add_argument('--teleport', type=float, default=2500.0)
    ap.add_argument('--mindemos', type=int, default=2)
    ap.add_argument('demos', nargs='+')
    cfg = ap.parse_args()

    jobs = [(p, cfg.gamedir, cfg) for p in cfg.demos]
    out = []
    if cfg.jobs > 1:
        with mp.Pool(cfg.jobs) as pool:
            for r in pool.imap_unordered(_worker, jobs):
                out.append(r)
    else:
        out = [_worker(j) for j in jobs]

    per_map = collections.defaultdict(list)
    for r in out:
        if not r:
            continue
        if 'error' in r:
            print('ERR', r['error'])
            continue
        per_map[r['map']].append(r)

    os.makedirs(cfg.out, exist_ok=True)
    summary = {}
    for mapname, rs in sorted(per_map.items()):
        # the pool returns demos out of order; fix the order here so two
        # runs over the same corpus produce byte-identical JSON
        rs.sort(key=lambda r: r['demo'])
        rune = os.path.join(cfg.gamedir, 'maps', f'{mapname}.rune')
        rune_graph, seeds, eligible = read_graph_metadata(rune, mapname)
        identity = rune_identity_from_rune(rune_graph)
        for result in rs:
            if result.get('_rune_identity') != identity:
                raise ValueError(
                    f'{rune}: rune identity changed while mining; rerun this '
                    'map instead of mixing seed indices')
        flags = flag_origins(cfg.gamedir, mapname)
        dwell_def = {t: collections.Counter() for t in ('red', 'blue')}
        dwell_any = {t: collections.Counter() for t in ('red', 'blue')}
        visit = {t: collections.Counter() for t in ('red', 'blue')}
        defs, cands, resp, steals, frames = [], [], [], 0, 0
        for r in rs:
            for t in ('red', 'blue'):
                for s, c in r['dwell_def'][t].items():
                    dwell_def[t][int(s)] += c
                for s, c in r['dwell_any'][t].items():
                    dwell_any[t][int(s)] += c
                for s, c in r['visit_any'][t].items():
                    visit[t][int(s)] += c
            cands.extend(dict(dd, demo=r['demo']) for dd in r['defenders'])
            defs.extend(dd for dd in r['defenders']
                        if dd['near_share'] >= cfg.defshare)
            resp.extend(r['responses'])
            steals += r['steals']
            frames += r['frames']
        # the strict defender set is usually thin (LMCTF defenders roam a
        # long way), so the post map is built from every same-team player
        # standing still at home -- see dwell_any above.
        posts, posts_strict = {}, {}
        for t in ('red', 'blue'):
            posts[t] = pick_posts(dwell_any[t], seeds, cfg.postsep,
                                  cfg.postlimit, cfg.minpostshare)
            posts_strict[t] = pick_posts(dwell_def[t], seeds, cfg.postsep,
                                         cfg.postlimit, cfg.minpostshare)
        # merged view: both stands' posts, share normalised within its team
        flat = []
        for t in ('red', 'blue'):
            stand = flags.get(t)
            for p in posts[t]:
                q = dict(p)
                q['team'] = t
                q['dist_home'] = round(dist(p['xyz'], stand)) if stand else None
                flat.append(q)
        flat.sort(key=lambda q: -q['share'])

        delays = [x['delay'] for x in resp if x['delay'] is not None]
        kinds = collections.Counter(x['kind'] for x in resp)
        closes = [x['dc0'] - x['dc1'] for x in resp
                  if x['dc0'] is not None and x['dc1'] is not None]
        aims_now = [x['aim_now'] for x in resp if x['aim_now'] is not None]
        aims_lead = [x['aim_lead'] for x in resp if x['aim_lead'] is not None]
        rseed = {t: collections.Counter() for t in ('red', 'blue')}
        endseed = {t: collections.Counter() for t in ('red', 'blue')}
        for x in resp:
            t = x['team']
            for s in x['seeds']:
                rseed[t][s] += 1
            if x['end_seed'] >= 0 and x['kind'] != 'hold':
                endseed[t][x['end_seed']] += 1

        def med(v):
            v = sorted(v)
            return round(v[len(v)//2], 2) if v else None

        response = {
            'steals_seen': steals,
            'samples': len(resp),
            'left_post': len(delays),
            'never_left': len(resp) - len(delays),
            'delay_mean': round(sum(delays)/len(delays), 2) if delays else None,
            'delay_median': med(delays),
            'delay_p90': (round(sorted(delays)[min(len(delays) - 1,
                                                   int(math.ceil(0.9 * len(delays))) - 1)], 2)
                          if delays else None),
            'kinds': dict(kinds),
            'kind_share': {k: round(v/len(resp), 3) for k, v in kinds.items()}
                          if resp else {},
            'gap_close_mean': round(sum(closes)/len(closes)) if closes else None,
            # + = ended further from own stand / closer to the enemy stand
            'home_drift_mean': (round(sum(x['dhome1'] - x['dhome0']
                                          for x in resp) / len(resp))
                                if resp else None),
            'enemy_drift_mean': (round(sum(x['denemy0'] - x['denemy1']
                                           for x in resp) / len(resp))
                                 if resp else None),
            'aim_at_carrier_now': round(sum(aims_now)/len(aims_now), 3)
                                  if aims_now else None,
            'aim_at_carrier_lead': round(sum(aims_lead)/len(aims_lead), 3)
                                   if aims_lead else None,
            # corridor the defenders run down, and where the run ends
            'lanes': {t: [[s, c] for s, c in rseed[t].most_common(15)]
                      for t in ('red', 'blue')},
            'intercepts': {t: pick_posts(endseed[t], seeds, cfg.postsep,
                                         cfg.postlimit, 0.02)
                           for t in ('red', 'blue')},
        }
        doc = {
            'map': mapname,
            '_schema': (
                'posts_by_team[team] is the authoritative ranked post list: '
                'share is that post cluster\'s fraction of all seconds the '
                'team spent standing still (dwellspan/dwellwin) within '
                'defradius of its own flag stand while that flag was home. '
                'posts[] is the same data flattened with a team tag, so its '
                'shares sum to ~2. seeds[] lists every rune seed folded into '
                'the cluster. response.* is measured over the window seconds '
                'after each "stole" print.'),
            'demos': len(rs), 'frames': frames,
            'demo_files': sorted(r['demo'] for r in rs),
            'desyncs': sum(r.get('desyncs', 0) for r in rs),
            'flags': {t: [round(v) for v in flags[t]] for t in flags},
            'flag_seed': {t: SeedGrid(seeds, eligible).nearest(flags[t], 500.0)
                          for t in flags},
            'defenders': sorted(
                ({'name': d0['name'], 'team': d0['team'],
                  'share': d0['near_share'], 'secs': round(d0['frames_home']/HZ),
                  'pov': d0['is_pov']} for d0 in defs),
                key=lambda d0: -d0['secs']),
            'params': {'defradius': cfg.defradius, 'defshare': cfg.defshare,
                       'dwellwin': cfg.dwellwin,
                       'dwellspan': cfg.dwellspan, 'postsep': cfg.postsep,
                       'window': cfg.window},
            'posts': flat,
            'posts_by_team': posts,
            'posts_strict': posts_strict,
            'candidates': sorted(
                ({'name': c['name'], 'team': c['team'], 'demo': c['demo'],
                  'secs': round(c['frames_home']/HZ), 'shares': c['shares'],
                  'pov': c['is_pov']} for c in cands),
                key=lambda c: -c['shares']['1500']),
            # raw per-seed occupancy, so a sidecar baker does not have to
            # re-derive it from the cluster membership lists
            'dwell_seed': {t: {str(s): round(c / HZ, 1)
                               for s, c in sorted(dwell_any[t].items())}
                           for t in ('red', 'blue')},
            'intercept_seed': {t: {str(s): c
                                   for s, c in sorted(endseed[t].items())}
                               for t in ('red', 'blue')},
            'dwell_secs': {t: round(sum(dwell_any[t].values())/HZ, 1)
                           for t in ('red', 'blue')},
            'dwell_secs_strict': {t: round(sum(dwell_def[t].values())/HZ, 1)
                                  for t in ('red', 'blue')},
            'response': response,
            # the per-sample seed paths are already rolled up into
            # top_response_seeds; keeping them per row would triple the file
            'responses': [{k: v for k, v in x.items() if k != 'seeds'}
                          for x in resp],
        }
        stamp_corpus_identity(doc, identity)
        if len(rs) < cfg.mindemos or not flat:
            print(f'{mapname}: thin ({len(rs)} demos, {len(flat)} posts) '
                  f'-- writing anyway')
        path = os.path.join(cfg.out, f'{mapname}.defense.json')
        atomic_write_json(path, doc)
        summary[mapname] = {'demos': len(rs), 'defenders': len(defs),
                            'posts': len(flat), 'responses': len(resp)}
        print(f'WROTE {path}: demos={len(rs)} defenders={len(defs)} '
              f'posts={len(flat)} responses={len(resp)} steals={steals}')
    print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    main()
