#!/usr/bin/env python3
"""film.py -- the eyes: a blind film renderer for LMCTF .dm2 demos.

Produces one PNG "film sheet" per demo, built identically regardless of
whether the demo is a HUMAN client recording or a BOT serverrecord capture,
so a judge can compare sheets without being told which is which.

Reuses the low-level machinery already proven in this toolbox:
  * dm2speed.py    -- byte reader, entity-bits parser, sound/TE shapes.
  * demokin.py     -- parse_playerstate_full (svc_playerinfo), used only to
                       stay in byte-sync; its output is discarded here.
  * demoents.py    -- the auto-detect skeleton for client vs serverrecord
                       .dm2 shape (see its docstring for why svc_frame is
                       parsed two different ways).
  * demorune.py    -- the rune seed-cloud loader (HEADER_FMT/SEED_FMT) used
                       to draw the map silhouette.

What's new here (nothing else in tools/ captures it): the player entity's
`effects` field. This mod's flag-carry visual (p_view.c G_SetClientEffects:
`if (redflag->owner == ent) ent->s.effects |= EF_FLAG1;` and the EF_FLAG2
mirror for blue) is broadcast on the normal delta-entity effects bits like
any other entity field, and it is IDENTICALLY available in both demo
shapes -- unlike the svc_print stream, which carries flag/obituary text
for client demos but is completely absent from serverrecord bot demos
(verified empirically: 0 svc_print messages across every wave*.dm2
serverrecord sample checked, despite thousands of sound/temp-entity
messages in the same files -- prints are per-client unicast in this
engine, never multicast, so serverrecord's multicast-only capture never
sees them). Because prints are asymmetric between the two demo shapes and
effects bits are not, this tool uses ONLY the effects-bit signal for event
detection, on purpose, even though prints would give more precise labels
on human demos -- using the richer signal on one demo type and not the
other would itself be a tell, breaking the blind. See MODULE NOTES at the
bottom for the full limitations list.

CLI:
    film.py <demo.dm2> [...more demos] --out <dir> [--runedir <dir>]

Writes <hash>.png (the sheet) and <hash>.json (source-mapping sidecar,
NOT blind -- this file exists for the unblinding step only) per demo.
"""
import argparse
import collections
import hashlib
import json
import math
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dm2speed as D
from demokin import parse_playerstate_full

import numpy as np

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection
from matplotlib.lines import Line2D

# --------------------------------------------------------------- bit tables
U_ORIGIN1 = 1 << 0
U_ORIGIN2 = 1 << 1
U_ANGLE2 = 1 << 2
U_ANGLE3 = 1 << 3
U_FRAME8 = 1 << 4
U_EVENT = 1 << 5
U_REMOVE = 1 << 6
U_MOREBITS1 = 1 << 7
U_NUMBER16 = 1 << 8
U_ORIGIN3 = 1 << 9
U_ANGLE1 = 1 << 10
U_MODEL = 1 << 11
U_RENDERFX8 = 1 << 12
U_EFFECTS8 = 1 << 14
U_MOREBITS2 = 1 << 15
U_SKIN8 = 1 << 16
U_FRAME16 = 1 << 17
U_RENDERFX16 = 1 << 18
U_EFFECTS16 = 1 << 19
U_MODEL2 = 1 << 20
U_MODEL3 = 1 << 21
U_MODEL4 = 1 << 22
U_MOREBITS3 = 1 << 23
U_OLDORIGIN = 1 << 24
U_SKIN16 = 1 << 25
U_SOUND = 1 << 26
U_SOLID = 1 << 27

# q_shared.h: EF_FLAG1 = 0x00040000 (red flag carried), EF_FLAG2 = 0x00080000
EF_FLAG1 = 0x00040000
EF_FLAG2 = 0x00080000
EF_FLAG_MASK = EF_FLAG1 | EF_FLAG2

FPS = 10.0                  # this protocol's entity stream is 10Hz, throughout
TELEPORT_UNITS = 180.0      # per-tick jump beyond this = respawn/teleport, not movement
MIN_TRACK_SAMPLES = 8       # below this, treat an entity as noise (ref/late join/etc)
MAX_CARRY_S = 150.0         # longer than this is almost certainly a stuck effects
                             # bit spanning a map-restart/round boundary in a
                             # combined-session recording, not one real flag run
                             # (observed once in calibration: a 753.8s "carry" in
                             # a multi-round bot session) -- excluded, not rendered
                             # as if it were a real carry, and reported separately.

# --- corridor cross-section diagnostic (rendering-only addition; see
# find_corridors/corridor_offsets below) ---------------------------------
CORRIDOR_ANGLE_STEP_DEG = 6.0    # candidate corridor directions scanned, 0..174
CORRIDOR_BAND_WIDTH = 56.0       # perpendicular width (u) of a candidate lane
CORRIDOR_SCAN_LEN = 320.0        # length (u) of the sliding window used to size
                                  # a candidate corridor segment
CORRIDOR_MIN_TRAFFIC = 40        # minimum pooled trajectory points binned into a
                                  # window before it counts as a candidate at all
CORRIDOR_MIN_SEPARATION = 220.0  # world units between two accepted corridors'
                                  # midpoints, so the top-N stay spatially
                                  # distinct instead of the same hallway picked
                                  # twice at two adjacent scan angles
CORRIDOR_OFFSET_CAP = 160.0      # max |perpendicular offset| (~3x the lane
                                  # width) counted as a "crossing" of a given
                                  # corridor -- without this cap, a sample far
                                  # off the corridor's line but which happens
                                  # to share its along-axis coordinate (e.g.
                                  # traffic in a completely different room)
                                  # gets counted too, swamping the local
                                  # rope-vs-band signal with map-scale noise
CROSS_SECTION_BIN = 4.0          # cross-section histogram bin width (spec'd)
N_CORRIDORS = 8

# --- windowed-detail diagnostic (rendering-only addition; see
# best_travel_window/draw_window_panel below) -----------------------------
WINDOW_S = 90.0                  # detail-panel window length, seconds
WINDOW_MAX_TRACKS = 3

# --- fixed sheet layout (diagnostic fix: raster scale must not depend on
# whether carry panels exist -- see render_sheet) -------------------------
GRID_COLS = 6
ROW_HEIGHTS = [3.6, 1.35, 1.55, 1.55, 1.7]   # map / carry / corridor / window / kin


# ------------------------------------------------------------ low-level walk
def parse_delta_entity_film(r, bits, o):
    """o = [x, y, z, effects]. Same field order as dm2speed.parse_delta_entity,
    but captures origin AND the full effects value (needed for EF_FLAG1/
    EF_FLAG2 carry detection) instead of skipping it. Mirrors
    demoents.parse_delta_entity_track's shape with one addition."""
    if bits & U_MODEL: r.skip(1)
    if bits & U_MODEL2: r.skip(1)
    if bits & U_MODEL3: r.skip(1)
    if bits & U_MODEL4: r.skip(1)
    if bits & U_FRAME8: r.skip(1)
    if bits & U_FRAME16: r.skip(2)
    if (bits & U_SKIN8) and (bits & U_SKIN16): r.skip(4)
    elif bits & U_SKIN8: r.skip(1)
    elif bits & U_SKIN16: r.skip(2)
    # effects: vanilla q2 wire rule (verified against dm2speed.parse_delta_entity
    # and against this mod's actual EF_FLAG1/2 bits, see MODULE NOTES) --
    # EFFECTS8 alone = 1 byte, EFFECTS16 alone = 1 short, BOTH set = one
    # 4-byte long (NOT a byte followed by a short: an earlier draft of this
    # walker read u8()+u16() here, under-consuming by one byte and silently
    # desyncing the rest of the block -- caught by cross-checking error
    # counts before/after the fix).
    if (bits & U_EFFECTS8) and (bits & U_EFFECTS16):
        o[3] = r.u32()
    elif bits & U_EFFECTS8:
        o[3] = r.u8()
    elif bits & U_EFFECTS16:
        o[3] = r.u16()
    if (bits & U_RENDERFX8) and (bits & U_RENDERFX16): r.skip(4)
    elif bits & U_RENDERFX8: r.skip(1)
    elif bits & U_RENDERFX16: r.skip(2)
    if bits & U_ORIGIN1: o[0] = r.s16() / 8.0
    if bits & U_ORIGIN2: o[1] = r.s16() / 8.0
    if bits & U_ORIGIN3: o[2] = r.s16() / 8.0
    if bits & U_ANGLE1: r.skip(1)
    if bits & U_ANGLE2: r.skip(1)
    if bits & U_ANGLE3: r.skip(1)
    if bits & U_OLDORIGIN: r.skip(6)
    if bits & U_SOUND: r.skip(1)
    if bits & U_EVENT: r.skip(1)
    if bits & U_SOLID: r.skip(2)


def walk_demo(path, maxplayers=32):
    """Auto-detects client vs serverrecord .dm2 shape (see demoents.
    walk_entities' docstring for the full rationale -- svrecord is sniffed
    from playernum==0xffff in the signon block, and its svc_frame carries
    only a framenum before an unconditional packetentities, vs the client
    shape's frame+deltaframe+suppresscount+areabits header).

    Returns {'map', 'skins', 'tracks': {entnum: [(frame,x,y,z,effects)]},
    'frames', 'svrecord'}. tracks are filtered to entnum 1..maxplayers
    (player slots) exactly like demoents.py and botkin.py."""
    data = open(path, 'rb').read()
    off = 0
    mapname = None
    skins = {}
    ents = {}
    tracks = {}
    frame_idx = 0
    svrecord = None

    def read_packetentities():
        while True:
            bits, num = D.parse_entity_bits(r)
            if num == 0:
                break
            if bits & U_REMOVE:
                ents.pop(num, None)
                continue
            o = ents.setdefault(num, [0.0, 0.0, 0.0, 0])
            parse_delta_entity_film(r, bits, o)

    def snapshot():
        nonlocal frame_idx
        frame_idx += 1
        for num, o in ents.items():
            if 1 <= num <= maxplayers:
                tracks.setdefault(num, []).append(
                    (frame_idx, o[0], o[1], o[2], o[3]))

    while off + 4 <= len(data):
        (mlen,) = struct.unpack_from('<i', data, off)
        off += 4
        if mlen == -1:
            break
        r = D.R(data[off:off + mlen])
        off += mlen
        try:
            while not r.done():
                svc = r.u8()
                if svc == 12:
                    r.skip(9); r.str_(); pn = r.u16(); r.str_()
                    svrecord = (pn == 0xffff)
                elif svc == 13:
                    idx = r.u16()
                    s = r.str_()
                    if 1312 <= idx < 1312 + 256:
                        skins[idx - 1312] = s
                    elif idx == 33:
                        m = re.match(r'maps/(\w+)\.bsp', s)
                        if m:
                            mapname = m.group(1)
                elif svc == 14:
                    bits, num = D.parse_entity_bits(r)
                    o = ents.setdefault(num, [0.0, 0.0, 0.0, 0])
                    parse_delta_entity_film(r, bits, o)
                elif svc == 20:
                    if svrecord:
                        r.skip(4)                 # framenum only
                        svc2 = r.u8()
                        if svc2 != 18:
                            raise ValueError(
                                f"svrecord frame not followed by "
                                f"packetentities (got {svc2})")
                        read_packetentities()
                        snapshot()
                    else:
                        r.skip(9)
                        ab = r.u8(); r.skip(ab)
                elif svc == 17:
                    parse_playerstate_full(r, {})
                elif svc == 18:
                    read_packetentities()
                    snapshot()
                elif svc == 9: D.parse_sound(r)
                elif svc == 3: D.parse_temp_entity(r)
                elif svc in (1, 2): r.skip(3)
                elif svc == 10: r.skip(1); r.str_()
                elif svc in (11, 15, 4): r.str_()
                elif svc == 5: r.skip(512)
                elif svc in (6, 7): pass
                else: raise ValueError(svc)
        except Exception:
            continue
    return {'map': mapname, 'skins': skins, 'tracks': tracks,
            'frames': frame_idx, 'svrecord': bool(svrecord)}


# ------------------------------------------------------------------- teams
_TEAM_RE = re.compile(r'/rb-([rb])[mf]\d*$', re.IGNORECASE)


def team_of(skin):
    """Team from the skin path, not from join-message text -- this works
    identically for human and bot demos (both send 'name\\model/rb-Xm#')
    and needs no print-stream text at all."""
    if not skin:
        return None
    m = _TEAM_RE.search(skin.split('\\')[-1])
    if not m:
        return None
    return 'red' if m.group(1).lower() == 'r' else 'blue'


# --------------------------------------------------------------- anonymize
def anonymize(d):
    """Assigns P1..Pn by entnum ascending. Drops short/noise tracks AND
    entities whose skin doesn't resolve to a red/blue team -- these are
    refs, spectators, and (seen in both demo shapes, not a bot-vs-human
    tell) idle reserve slots the test harness or a pickup server leaves
    connected without ever joining a side. A 'player' on this sheet is a
    roster participant; entnum range alone (1..maxclients) isn't enough
    to tell a competitor from a bystander, but a resolved team is.
    Returns (labels: {entnum: 'Pk'}, teams: {entnum: 'red'/'blue'})."""
    tracks = d['tracks']
    keep = []
    teams = {}
    for n, t in tracks.items():
        if len(t) < MIN_TRACK_SAMPLES:
            continue
        team = team_of(d['skins'].get(n - 1))
        if team is None:
            continue
        keep.append(n)
        teams[n] = team
    keep.sort()
    labels = {n: f"P{i+1}" for i, n in enumerate(keep)}
    return labels, teams


# --------------------------------------------------------- carry detection
def carry_windows(tracks, labels):
    """Scans each kept track's effects field for EF_FLAG1/EF_FLAG2 bit
    transitions. Returns a list of dicts:
        {entnum, color ('red'|'blue' -- the flag color carried),
         t0, t1, path: [(t,x,y,z), ...]}
    color='red' means the RED flag was carried (i.e. a blue-team player
    normally); color='blue' means the BLUE flag was carried."""
    windows = []
    for n, track in tracks.items():
        if n not in labels:
            continue
        prev = 0
        start = None
        path = []
        for f, x, y, z, eff in track:
            cur = int(eff) & EF_FLAG_MASK
            if cur != prev:
                if prev == 0 and cur != 0:
                    start = (f, x, y, z)
                    path = [(f, x, y, z)]
                elif prev != 0 and cur == 0 and start is not None:
                    color = 'red' if prev == EF_FLAG1 else 'blue'
                    windows.append({
                        'entnum': n, 'color': color,
                        't0': start[0] / FPS, 't1': f / FPS,
                        'path': path + [(f, x, y, z)],
                    })
                    start = None
                    path = []
                prev = cur
            elif cur != 0:
                path.append((f, x, y, z))
    windows.sort(key=lambda w: w['t0'])
    sane = [w for w in windows if (w['t1'] - w['t0']) <= MAX_CARRY_S]
    n_excluded = len(windows) - len(sane)
    return sane, n_excluded


def flag_stands(windows):
    """Home-stand estimate per color: median of carry-start positions for
    that color (the flag is always picked up at its own stand). None if
    a color never got stolen in this demo."""
    stands = {}
    for color in ('red', 'blue'):
        pts = [w['path'][0][1:3] for w in windows if w['color'] == color]
        if pts:
            xs = sorted(p[0] for p in pts)
            ys = sorted(p[1] for p in pts)
            stands[color] = (xs[len(xs) // 2], ys[len(ys) // 2])
    return stands


def death_ticks(track):
    """Frame indices where the track jumps > TELEPORT_UNITS on a single
    consecutive-frame (dt=0.1s) step -- a respawn teleport, not movement.
    Used both as the 'death' proxy for classifying carry endings and as
    the tick marks on the kinematic strip."""
    out = []
    for i in range(1, len(track)):
        f0, x0, y0, z0, _ = track[i - 1]
        f1, x1, y1, z1, _ = track[i]
        if f1 - f0 != 1:
            continue
        if math.hypot(x1 - x0, y1 - y0) > TELEPORT_UNITS:
            out.append(f1)
    return out


def classify_outcome(w, tracks, stands, cap_radius=280.0, lookahead_s=1.6):
    """Best-effort outcome label from position + teleport geometry only
    (no print text, see module docstring). thief_team is the OTHER color
    from the flag carried (you can only carry the enemy flag)."""
    thief_color = 'blue' if w['color'] == 'red' else 'red'
    end_pos = w['path'][-1][1:3]
    stand = stands.get(thief_color)
    if stand is not None:
        d = math.hypot(end_pos[0] - stand[0], end_pos[1] - stand[1])
        if d < cap_radius:
            return 'captured'
    track = tracks.get(w['entnum'], [])
    end_frame = w['path'][-1][0]
    la = int(lookahead_s * FPS)
    for i in range(1, len(track)):
        f0, x0, y0, z0, _ = track[i - 1]
        f1, x1, y1, z1, _ = track[i]
        if f1 < end_frame or f1 > end_frame + la:
            continue
        if f1 - f0 == 1 and math.hypot(x1 - x0, y1 - y0) > TELEPORT_UNITS:
            return 'died'
    return 'lost'


# ------------------------------------------------------------------- rune
HEADER_FMT = '<4i64s'
SEED_FMT = '<3f2h'


def load_rune_seeds(rune_path):
    data = open(rune_path, 'rb').read()
    magic, ver, ns, nl, name = struct.unpack_from(HEADER_FMT, data, 0)
    off = struct.calcsize(HEADER_FMT)
    ssz = struct.calcsize(SEED_FMT)
    seeds = []
    for i in range(ns):
        x, y, z, ah, fl = struct.unpack_from(SEED_FMT, data, off)
        off += ssz
        seeds.append((x, y))
    return seeds


def find_rune(rune_dir, mapname):
    for cand in (f'{rune_dir}/{mapname}.rune',
                 f'{rune_dir}/maps/{mapname}.rune',
                 f'{rune_dir}/runes/{mapname}.rune'):
        if os.path.exists(cand):
            return cand
    return None


def compute_seed_extent(seeds, pad_frac=0.04):
    """Fixed world-coordinate bounding box for the map panel, derived ONLY
    from the rune seed cloud (the same file every time a given map is
    rendered) -- NOT from this demo's trajectory data. See the fixed-scale
    note in draw_trajectory_map. Returns (xmin, xmax, ymin, ymax), or None
    if no rune was loaded (falls back to old autoscale behavior for that
    one sheet -- there's no map-independent reference to pin the scale to)."""
    if not seeds:
        return None
    xs = [s[0] for s in seeds]
    ys = [s[1] for s in seeds]
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    dx = (xmax - xmin) or 100.0
    dy = (ymax - ymin) or 100.0
    xmin -= dx * pad_frac; xmax += dx * pad_frac
    ymin -= dy * pad_frac; ymax += dy * pad_frac
    return (xmin, xmax, ymin, ymax)


# --------------------------------------------------------------- kinematics
def hspeed_series(track):
    """[(t, hspeed)] from consecutive-frame (dt=0.1s) steps only. A step
    beyond TELEPORT_UNITS is a respawn, not movement -- it's dropped (with
    a plotted gap, via NaN) rather than plotted as a multi-thousand-u/s
    speed spike that would swamp the real 0-800 u/s movement scale."""
    out = []
    for i in range(1, len(track)):
        f0, x0, y0, z0, _ = track[i - 1]
        f1, x1, y1, z1, _ = track[i]
        if f1 - f0 != 1:
            continue
        dist = math.hypot(x1 - x0, y1 - y0)
        if dist > TELEPORT_UNITS:
            out.append((f1 / FPS, float('nan')))
            continue
        out.append((f1 / FPS, dist / 0.1))
    return out


# ------------------------------------------------------- corridor diagnostic
def _all_points(tracks, labels):
    pts = []
    for n, track in tracks.items():
        if n not in labels:
            continue
        for f, x, y, z, eff in track:
            pts.append((x, y))
    return pts


def nearest_seed_counts(seed_arr, pts_xy, chunk=4000):
    """For every point in pts_xy (N x 2), assigns it to its nearest row of
    seed_arr (S x 2) and returns per-seed traffic counts (length S). Brute
    force nearest-neighbor in chunks -- no scipy dependency, and S is only
    ~1-2k seeds so this is fast even for a full-length demo's worth of
    trajectory samples."""
    counts = np.zeros(len(seed_arr), dtype=np.int64)
    if len(pts_xy) == 0:
        return counts
    pts = np.asarray(pts_xy, dtype=np.float64)
    for start in range(0, len(pts), chunk):
        batch = pts[start:start + chunk]
        d2 = ((batch[:, None, :] - seed_arr[None, :, :]) ** 2).sum(axis=2)
        idx = d2.argmin(axis=1)
        counts += np.bincount(idx, minlength=len(seed_arr))
    return counts


def find_corridors(seeds, tracks, labels, n=N_CORRIDORS):
    """The N highest-traffic straight corridor segments.

    Method: bin every trajectory point (all kept tracks, both teams
    pooled) onto its nearest rune seed -- the seed cloud approximates the
    walkable floor, so this rasterizes foot traffic onto map geometry
    instead of an arbitrary grid. Then scan candidate corridor directions
    every CORRIDOR_ANGLE_STEP_DEG degrees: rotate the seed cloud into that
    direction's (along, across) frame, group seeds into
    CORRIDOR_BAND_WIDTH-wide across-axis strips (candidate lanes), and
    within each strip slide a CORRIDOR_SCAN_LEN-long window along the
    along-axis to find the traffic-densest run in that lane. The
    traffic-highest windows overall, kept spatially distinct (see
    CORRIDOR_MIN_SEPARATION), are returned as corridor candidates.

    Returns a list of dicts {p0: (x,y), p1: (x,y), traffic: int,
    seed_idx: [...]}, sorted by traffic descending, len <= n. Empty if no
    rune was loaded or traffic is too sparse to clear
    CORRIDOR_MIN_TRAFFIC anywhere."""
    if not seeds:
        return []
    seed_arr = np.asarray(seeds, dtype=np.float64)
    pts = _all_points(tracks, labels)
    if not pts:
        return []
    traffic = nearest_seed_counts(seed_arr, pts)

    candidates = []
    n_angles = max(1, int(round(180.0 / CORRIDOR_ANGLE_STEP_DEG)))
    for k in range(n_angles):
        theta = math.radians(k * CORRIDOR_ANGLE_STEP_DEG)
        c, s = math.cos(theta), math.sin(theta)
        u = seed_arr[:, 0] * c + seed_arr[:, 1] * s
        v = -seed_arr[:, 0] * s + seed_arr[:, 1] * c
        vbin = np.floor(v / CORRIDOR_BAND_WIDTH).astype(np.int64)
        order = np.argsort(vbin, kind='stable')
        vbin_sorted = vbin[order]
        boundaries = np.where(np.diff(vbin_sorted) != 0)[0] + 1
        for grp in np.split(order, boundaries):
            if len(grp) < 4:
                continue
            gorder = np.argsort(u[grp])
            gidx = grp[gorder]
            gu = u[gidx]
            gtraf = traffic[gidx]
            # two-pointer sliding window: densest run <= CORRIDOR_SCAN_LEN
            # long, in along-axis (u) units, within this across-axis strip
            left = 0
            running = 0
            best_sum = -1
            best_lo = best_hi = 0
            for right in range(len(gu)):
                running += gtraf[right]
                while gu[right] - gu[left] > CORRIDOR_SCAN_LEN:
                    running -= gtraf[left]
                    left += 1
                if running > best_sum:
                    best_sum = running
                    best_lo, best_hi = left, right
            if best_sum < CORRIDOR_MIN_TRAFFIC:
                continue
            if gu[best_hi] - gu[best_lo] < CORRIDOR_SCAN_LEN * 0.5:
                continue  # too short a run to call a "corridor segment"
            v_avg = float(np.mean(v[gidx[best_lo:best_hi + 1]]))
            u0, u1 = float(gu[best_lo]), float(gu[best_hi])
            p0 = (u0 * c - v_avg * s, u0 * s + v_avg * c)
            p1 = (u1 * c - v_avg * s, u1 * s + v_avg * c)
            candidates.append({
                'p0': p0, 'p1': p1, 'traffic': int(best_sum),
                'seed_idx': gidx[best_lo:best_hi + 1].tolist(),
            })

    candidates.sort(key=lambda cnd: -cnd['traffic'])
    chosen = []
    for cnd in candidates:
        mid = ((cnd['p0'][0] + cnd['p1'][0]) / 2,
               (cnd['p0'][1] + cnd['p1'][1]) / 2)
        if any(math.hypot(mid[0] - (ch['p0'][0] + ch['p1'][0]) / 2,
                           mid[1] - (ch['p0'][1] + ch['p1'][1]) / 2)
               < CORRIDOR_MIN_SEPARATION for ch in chosen):
            continue
        chosen.append(cnd)
        if len(chosen) >= n:
            break
    return chosen


def corridor_offsets(corridor, tracks, labels, pad_frac=0.15,
                      cap=CORRIDOR_OFFSET_CAP):
    """Perpendicular-offset distribution (world units) of every trajectory
    sample (all kept tracks, both teams pooled) whose along-corridor
    position falls within the corridor segment's span (padded by pad_frac
    of its length at each end so samples right at the ends aren't dropped
    by an arbitrary boundary) AND whose perpendicular offset is within
    +/-cap of the corridor's line. The cap matters: without it, a sample
    far off the corridor's line that happens to share its along-axis
    coordinate (e.g. traffic in an unrelated room several hundred units
    away) gets counted as a "crossing" too, which swamps the local rope-
    vs-band signal this panel exists to show with map-scale noise. This is
    that signal: a tight 1-2 bin spike means everyone threads the same
    line, a wide graded histogram means a spread-out movement profile."""
    x0, y0 = corridor['p0']
    x1, y1 = corridor['p1']
    length = math.hypot(x1 - x0, y1 - y0)
    if length == 0:
        return []
    ux, uy = (x1 - x0) / length, (y1 - y0) / length
    nx, ny = -uy, ux
    pad = length * pad_frac
    offsets = []
    for n, track in tracks.items():
        if n not in labels:
            continue
        for f, x, y, z, eff in track:
            su = (x - x0) * ux + (y - y0) * uy
            if -pad <= su <= length + pad:
                d = (x - x0) * nx + (y - y0) * ny
                if abs(d) <= cap:
                    offsets.append(d)
    return offsets


# --------------------------------------------------------- windowed detail
def best_travel_window(tracks, labels, total_frames, window_s=WINDOW_S):
    """Finds the window_s-second window (all kept tracks pooled) with the
    most total travel distance (teleports/respawns excluded, same rule as
    hspeed_series). Returns (frame_start, frame_end)."""
    window_frames = max(1, int(round(window_s * FPS)))
    combined = np.zeros(total_frames + 2, dtype=np.float64)
    for n, track in tracks.items():
        if n not in labels:
            continue
        for i in range(1, len(track)):
            f0, x0, y0, z0, _ = track[i - 1]
            f1, x1, y1, z1, _ = track[i]
            if f1 - f0 != 1:
                continue
            dist = math.hypot(x1 - x0, y1 - y0)
            if dist > TELEPORT_UNITS:
                continue
            if 0 <= f1 < len(combined):
                combined[f1] += dist
    if total_frames <= window_frames:
        return 0, total_frames
    csum = np.cumsum(np.concatenate(([0.0], combined)))
    n_starts = total_frames - window_frames + 1
    sums = csum[window_frames:window_frames + n_starts] - csum[:n_starts]
    start = int(np.argmax(sums))
    return start, start + window_frames


def pick_window_tracks(tracks, labels, f0, f1, max_tracks=WINDOW_MAX_TRACKS):
    """The at-most max_tracks busiest (most travel distance within
    [f0, f1]) kept tracks, for the windowed-detail panel."""
    scored = []
    for n, track in tracks.items():
        if n not in labels:
            continue
        seg = [(f, x, y, z) for f, x, y, z, eff in track if f0 <= f <= f1]
        if len(seg) < 2:
            continue
        dist = 0.0
        for i in range(1, len(seg)):
            fa, xa, ya, za = seg[i - 1]
            fb, xb, yb, zb = seg[i]
            if fb - fa != 1:
                continue
            d = math.hypot(xb - xa, yb - ya)
            if d <= TELEPORT_UNITS:
                dist += d
        scored.append((dist, n, seg))
    scored.sort(key=lambda t: -t[0])
    return scored[:max_tracks]


# --------------------------------------------------------------- rendering
TEAM_COLOR = {'red': '#c0392b', 'blue': '#2166ac', None: '#7f7f7f'}
MAP_COLOR = '#c9c9c9'
KIN_COLORS = ['#c0392b', '#2166ac', '#7a7a7a']


def alpha_ramp_segments(xs, ys, base_hex, lo=0.12, hi=0.92):
    import matplotlib.colors as mcolors
    if len(xs) < 2:
        return None
    pts = list(zip(xs, ys))
    segs = [[pts[i], pts[i + 1]] for i in range(len(pts) - 1)]
    r, g, b = mcolors.to_rgb(base_hex)
    n = len(segs)
    colors = [(r, g, b, lo + (hi - lo) * (i / max(1, n - 1)))
              for i in range(n)]
    return LineCollection(segs, colors=colors, linewidths=1.6)


def draw_trajectory_map(ax, seeds, tracks, labels, teams, windows, stands,
                         extent=None):
    if seeds:
        sx = [s[0] for s in seeds]
        sy = [s[1] for s in seeds]
        ax.scatter(sx, sy, s=1.5, c=MAP_COLOR, zorder=1, linewidths=0)
    for n, track in tracks.items():
        if n not in labels:
            continue
        xs = [p[1] for p in track]
        ys = [p[2] for p in track]
        base = TEAM_COLOR.get(teams.get(n))
        lc = alpha_ramp_segments(xs, ys, base)
        if lc is not None:
            lc.set_zorder(2)
            ax.add_collection(lc)
    for color, pos in stands.items():
        ax.scatter([pos[0]], [pos[1]], marker='*', s=260,
                    c=TEAM_COLOR[color], edgecolors='black',
                    linewidths=0.8, zorder=4)
    markers = {'steal': ('^', 90), 'captured': ('*', 140),
               'died': ('X', 70), 'lost': ('v', 70)}
    for w in windows:
        sx0, sy0 = w['path'][0][1], w['path'][0][2]
        ex0, ey0 = w['path'][-1][1], w['path'][-1][2]
        col = TEAM_COLOR[w['color']]
        mk, sz = markers['steal']
        ax.scatter([sx0], [sy0], marker=mk, s=sz, c=col,
                    edgecolors='black', linewidths=0.5, zorder=5)
        outcome = w.get('outcome', 'lost')
        mk, sz = markers[outcome]
        ax.scatter([ex0], [ey0], marker=mk, s=sz, c=col,
                    edgecolors='black', linewidths=0.5, zorder=5)
    # DIAGNOSTIC FIX 1 (fixed raster scale): the world extent shown here
    # comes ONLY from the rune seed cloud (same file every render of this
    # map), never from this demo's own trajectory/marker data. Setting it
    # explicitly -- instead of letting matplotlib autoscale to whatever
    # this particular demo's tracks happen to cover -- means two sheets of
    # the same map always show the same world rectangle. Combined with the
    # constant figure/gridspec layout in render_sheet (map panel is always
    # the same size in pixels, carry/corridor/window rows are fixed-height
    # strips present whether or not they have content), this pins
    # world-units-per-pixel identically across every sheet of a given map.
    if extent is not None:
        # Both xlim and ylim are pinned here (from the seed cloud, not this
        # demo's data) -- adjustable='datalim' can only satisfy an equal
        # aspect ratio by silently stretching one of two explicitly-fixed
        # limits (matplotlib warns "Ignoring fixed x limits..." and does
        # exactly that), which would defeat the fixed-scale guarantee.
        # adjustable='box' instead shrinks/pads the rendered box within its
        # constant gridspec cell to fit these exact limits, so the same
        # seed-derived extent always maps to the same box the same way.
        ax.set_xlim(extent[0], extent[1])
        ax.set_ylim(extent[2], extent[3])
        ax.set_aspect('equal', adjustable='box')
    else:
        ax.set_aspect('equal', adjustable='datalim')
    ax.set_xticks([]); ax.set_yticks([])
    for spine in ax.spines.values():
        spine.set_visible(False)
    legend_handles = [
        Line2D([0], [0], color=TEAM_COLOR['red'], lw=2, label='red team'),
        Line2D([0], [0], color=TEAM_COLOR['blue'], lw=2, label='blue team'),
        Line2D([0], [0], marker='*', color='none', markerfacecolor='gray',
               markeredgecolor='black', markersize=11, label='flag stand'),
        Line2D([0], [0], marker='^', color='none', markerfacecolor='gray',
               markeredgecolor='black', markersize=8, label='steal'),
        Line2D([0], [0], marker='*', color='none', markerfacecolor='gray',
               markeredgecolor='black', markersize=10, label='captured'),
        Line2D([0], [0], marker='X', color='none', markerfacecolor='gray',
               markeredgecolor='black', markersize=8, label='died carrying'),
        Line2D([0], [0], marker='v', color='none', markerfacecolor='gray',
               markeredgecolor='black', markersize=8, label='flag lost'),
    ]
    ax.legend(handles=legend_handles, loc='upper right', fontsize=7,
              framealpha=0.85)
    ax.set_title('full-game trajectory', fontsize=10)


def draw_carry_panel(ax, w, tracks, labels, teams, idx):
    for n, track in tracks.items():
        if n not in labels:
            continue
        xs = [p[1] for p in track]
        ys = [p[2] for p in track]
        ax.plot(xs, ys, color='#bbbbbb', lw=0.5, alpha=0.6, zorder=1)
    path = w['path']
    xs = [p[1] for p in path]
    ys = [p[2] for p in path]
    col = TEAM_COLOR[w['color']]
    ax.plot(xs, ys, color=col, lw=2.2, zorder=3)
    ax.scatter([xs[0]], [ys[0]], marker='^', s=60, c=col,
                edgecolors='black', linewidths=0.6, zorder=4)
    ax.scatter([xs[-1]], [ys[-1]], marker='o', s=50, c=col,
                edgecolors='black', linewidths=0.6, zorder=4)
    dur = w['t1'] - w['t0']
    ax.set_title(f"carry {idx}: {w.get('outcome','?')}, {dur:.1f}s",
                 fontsize=8)
    ax.set_aspect('equal', adjustable='datalim')
    ax.set_xticks([]); ax.set_yticks([])
    for spine in ax.spines.values():
        spine.set_visible(False)


def draw_corridor_panel(ax, corridor, offsets, idx):
    x0, y0 = corridor['p0']
    x1, y1 = corridor['p1']
    if not offsets:
        ax.text(0.5, 0.5, 'no crossings', ha='center', va='center',
                fontsize=8, transform=ax.transAxes, color='#666666')
    else:
        lo = math.floor(min(offsets) / CROSS_SECTION_BIN) * CROSS_SECTION_BIN
        hi = math.ceil(max(offsets) / CROSS_SECTION_BIN) * CROSS_SECTION_BIN
        if hi <= lo:
            hi = lo + CROSS_SECTION_BIN
        bins = np.arange(lo, hi + CROSS_SECTION_BIN, CROSS_SECTION_BIN)
        ax.hist(offsets, bins=bins, color='#555577', edgecolor='none')
    ax.axvline(0, color='#c0392b', lw=0.8, ls='--', alpha=0.7)
    ax.set_title(
        f"corridor {idx}: ({x0:.0f},{y0:.0f})-({x1:.0f},{y1:.0f})\n"
        f"n={len(offsets)} crossings, {CROSS_SECTION_BIN:.0f}u bins",
        fontsize=7)
    ax.set_xlabel('perpendicular offset (u)', fontsize=7)
    ax.tick_params(labelsize=6)
    ax.set_yticks([])


def draw_window_panel(ax, tracks, labels, teams, f0, f1):
    picked = pick_window_tracks(tracks, labels, f0, f1)
    for dist, n, seg in picked:
        xs = [p[1] for p in seg]
        ys = [p[2] for p in seg]
        base = TEAM_COLOR.get(teams.get(n))
        lc = alpha_ramp_segments(xs, ys, base)
        if lc is not None:
            lc.set_linewidth(2.0)
            ax.add_collection(lc)
        if xs:
            ax.scatter([xs[0]], [ys[0]], marker='^', s=55, c=base,
                       edgecolors='black', linewidths=0.6, zorder=4)
            ax.scatter([xs[-1]], [ys[-1]], marker='o', s=45, c=base,
                       edgecolors='black', linewidths=0.6, zorder=4)
    ax.set_aspect('equal', adjustable='datalim')
    ax.set_xticks([]); ax.set_yticks([])
    for spine in ax.spines.values():
        spine.set_visible(False)
    t0s, t1s = f0 / FPS, f1 / FPS
    ax.set_title(
        f"windowed detail: t={t0s:.1f}-{t1s:.1f}s "
        f"(highest-travel {WINDOW_S:.0f}s span, <={WINDOW_MAX_TRACKS} tracks)",
        fontsize=9)
    if not picked:
        ax.text(0.5, 0.5, 'no track data in this window', ha='center',
                va='center', fontsize=8, color='#666666',
                transform=ax.transAxes)


def draw_kinematic_strip(ax, tracks, labels, death_by_ent):
    busiest = sorted(
        ((n, t) for n, t in tracks.items() if n in labels),
        key=lambda kv: -len(kv[1]))[:3]
    for i, (n, track) in enumerate(busiest):
        series = hspeed_series(track)
        if not series:
            continue
        ts = [s[0] for s in series]
        vs = [s[1] for s in series]
        color = KIN_COLORS[i % len(KIN_COLORS)]
        ax.plot(ts, vs, color=color, lw=0.8, alpha=0.85,
                label=f"track {i+1}")
        for fr in death_by_ent.get(n, []):
            ax.axvline(fr / FPS, color=color, lw=0.6, ls=':', alpha=0.5)
    ax.set_xlabel('time (s)', fontsize=8)
    ax.set_ylabel('h-speed (u/s)', fontsize=8)
    ax.set_title('kinematic strip -- busiest 3 tracks (dotted = death tick)',
                  fontsize=9)
    ax.tick_params(labelsize=7)
    ax.legend(fontsize=7, loc='upper right')


# ------------------------------------------------------------------- hash
def hash_demo(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()[:12]


# ------------------------------------------------------------------- main
def render_sheet(demo_path, rune_dir, out_dir, max_carry_panels=6):
    d = walk_demo(demo_path)
    labels, teams = anonymize(d)
    tracks = d['tracks']
    windows, n_excluded_carries = carry_windows(tracks, labels)
    stands = flag_stands(windows)
    for w in windows:
        w['outcome'] = classify_outcome(w, tracks, stands)
    death_by_ent = {n: death_ticks(t) for n, t in tracks.items() if n in labels}

    seeds = []
    rune_note = None
    if d['map']:
        rp = find_rune(rune_dir, d['map'])
        if rp:
            seeds = load_rune_seeds(rp)
        else:
            rune_note = f"no rune found for map={d['map']}"
    else:
        rune_note = "map name not recovered from demo"

    map_extent = compute_seed_extent(seeds)
    corridors = find_corridors(seeds, tracks, labels)
    corridor_note = None
    if not corridors:
        corridor_note = ("no corridor cross-sections available" +
                          ("" if seeds else
                           " (no rune loaded for this map)"))

    win_f0, win_f1 = best_travel_window(tracks, labels, d['frames'])

    n_players = len(labels)
    duration = d['frames'] / FPS

    h = hash_demo(demo_path)
    os.makedirs(out_dir, exist_ok=True)

    shown = windows[:max_carry_panels]
    n_extra = len(windows) - len(shown)

    # DIAGNOSTIC FIX 1 (fixed raster scale): figure size, row count, and
    # row height ratios are now CONSTANT -- they no longer depend on how
    # many carry windows or corridors this particular demo has. The carry
    # strip, corridor row, and windowed-detail panel are fixed-height rows
    # that are always present (rendered empty, with a note, when there's
    # nothing to show) instead of being omitted and letting the map row
    # grow to fill the freed space, which is what silently changed the map
    # panel's pixel size (and therefore its world-units-per-pixel) between
    # sheets before this fix.
    fig = plt.figure(figsize=(12, 18.0), dpi=140)
    gs = fig.add_gridspec(
        len(ROW_HEIGHTS), GRID_COLS, height_ratios=ROW_HEIGHTS,
        hspace=0.55, wspace=0.30,
        top=0.945, bottom=0.045, left=0.06, right=0.97)

    ax_map = fig.add_subplot(gs[0, :])
    draw_trajectory_map(ax_map, seeds, tracks, labels, teams, windows, stands,
                         extent=map_extent)

    # row 1: carry strip -- fixed height, present even when empty
    if shown:
        col_w = GRID_COLS / max(1, len(shown))
        for i, w in enumerate(shown):
            c0 = int(round(i * col_w))
            c1 = max(c0 + 1, int(round((i + 1) * col_w)))
            ax = fig.add_subplot(gs[1, c0:c1])
            draw_carry_panel(ax, w, tracks, labels, teams, i + 1)
        if n_extra > 0:
            row_bottom = gs[1, :].get_position(fig).y0
            fig.text(0.5, row_bottom - 0.006,
                      f"(+{n_extra} more carry window(s) not shown)",
                      ha='center', fontsize=7, color='#666666')
    else:
        ax = fig.add_subplot(gs[1, :])
        ax.axis('off')
        ax.text(0.5, 0.5, "no carry windows detected in this demo "
                "(effects-bit signal never observed changing -- see "
                "caption notes)", ha='center', va='center', fontsize=8,
                color='#666666', transform=ax.transAxes)

    # row 2: corridor cross-section histograms -- fixed N_CORRIDORS slots
    col_w = GRID_COLS / N_CORRIDORS
    for i in range(N_CORRIDORS):
        c0 = int(round(i * col_w))
        c1 = max(c0 + 1, int(round((i + 1) * col_w)))
        ax = fig.add_subplot(gs[2, c0:c1])
        if i < len(corridors):
            offs = corridor_offsets(corridors[i], tracks, labels)
            draw_corridor_panel(ax, corridors[i], offs, i + 1)
        else:
            ax.axis('off')
            if i == 0 and corridor_note:
                ax.text(0.5, 0.5, corridor_note, ha='center', va='center',
                        fontsize=7, color='#666666',
                        transform=ax.transAxes, wrap=True)

    # row 3: windowed-detail panel -- fixed height, full width
    ax_win = fig.add_subplot(gs[3, :])
    draw_window_panel(ax_win, tracks, labels, teams, win_f0, win_f1)

    # row 4: kinematic strip (existing)
    ax_kin = fig.add_subplot(gs[4, :])
    draw_kinematic_strip(ax_kin, tracks, labels, death_by_ent)

    caption = (f"map={d['map'] or '?'}   hash={h}   "
               f"players={n_players}   duration={duration:.1f}s   "
               f"carries={len(windows)}")
    fig.text(0.5, 0.985, caption, ha='center', fontsize=10, weight='bold')
    notes = []
    if rune_note:
        notes.append(rune_note)
    if n_excluded_carries:
        notes.append(f"{n_excluded_carries} anomalously long carry "
                      f"window(s) excluded (>{MAX_CARRY_S:.0f}s, likely a "
                      f"stuck flag-carry bit across a round boundary)")
    if notes:
        fig.text(0.5, 0.968, '; '.join(notes), ha='center', fontsize=7,
                  color='#993333')

    png_path = os.path.join(out_dir, f'{h}.png')
    fig.savefig(png_path)
    plt.close(fig)

    sidecar = {
        'hash': h,
        'source_path': os.path.abspath(demo_path),
        'source_basename': os.path.basename(demo_path),
        'map': d['map'],
        'demo_shape': 'serverrecord(bot)' if d['svrecord'] else 'client(human)',
        'frames': d['frames'],
        'duration_s': duration,
        'players_rendered': n_players,
        'carry_windows': len(windows),
        'carry_windows_excluded_anomalous': n_excluded_carries,
        'entnum_to_label': {str(k): v for k, v in labels.items()},
        'label_to_name': {v: d['skins'].get(k - 1, '?').split('\\')[0]
                           for k, v in labels.items()},
        'label_to_team': {labels[k]: teams[k] for k in labels},
        'rune_used': bool(seeds),
        'map_extent': list(map_extent) if map_extent else None,
        'corridors': [{'p0': c['p0'], 'p1': c['p1'], 'traffic': c['traffic']}
                      for c in corridors],
        'window_s': WINDOW_S,
        'window_t0_s': win_f0 / FPS,
        'window_t1_s': win_f1 / FPS,
        'rendered_at': time.strftime('%Y-%m-%dT%H:%M:%S'),
    }
    json_path = os.path.join(out_dir, f'{h}.json')
    with open(json_path, 'w') as f:
        json.dump(sidecar, f, indent=1)

    return {
        'hash': h, 'map': d['map'], 'svrecord': d['svrecord'],
        'players': n_players, 'duration': duration,
        'carries': len(windows), 'png': png_path, 'json': json_path,
    }


DEFAULT_RUNEDIR = os.path.expanduser('~/Games/Quake2/lmctf-hooktest/maps')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('demos', nargs='+', help='.dm2 demo files')
    ap.add_argument('--out', required=True, help='output directory')
    ap.add_argument('--runedir', default=DEFAULT_RUNEDIR,
                     help='directory holding <map>.rune files '
                          f'(default: {DEFAULT_RUNEDIR})')
    args = ap.parse_args()

    ok, failed = [], []
    for path in args.demos:
        try:
            res = render_sheet(path, args.runedir, args.out)
            ok.append(res)
            print(f"OK   {os.path.basename(path)} -> {res['hash']}.png  "
                  f"map={res['map']} {'bot' if res['svrecord'] else 'human'} "
                  f"players={res['players']} dur={res['duration']:.1f}s "
                  f"carries={res['carries']}")
        except Exception as e:
            failed.append((path, str(e)))
            print(f"FAIL {os.path.basename(path)}: {e}")

    print(f"\n{len(ok)} sheet(s) written to {args.out}, "
          f"{len(failed)} failed")


if __name__ == '__main__':
    main()


# ----------------------------------------------------------------- MODULE
# NOTES (limitations a judge should know before trusting a sheet):
#
# 1. Carry-window detection uses only the EF_FLAG1/EF_FLAG2 effects bits on
#    the carrying player's entity. This was cross-validated against a bot
#    (serverrecord) demo where it matched the modelindex3 flag-attachment
#    signal exactly on carry START but modelindex3 silently failed to clear
#    on carry END (a real quirk in this codebase's reset path) -- effects
#    was the more complete of the two and is what's used here.
#
# 2. On at least one human client demo probed during development, a known
#    steal+capture pair (confirmed via svc_print text, which this tool does
#    NOT use for detection -- see the module docstring) produced zero
#    effects-bit transitions, because the recording player's client never
#    had the carrier back in its PVS between t=30s and the end of the
#    round: the entity simply has no delta updates at all in that window,
#    for ANY field, not just effects. This is a property of client demos
#    generally (a spectator/participant only receives what's in their
#    potentially-visible-set) and will silently produce fewer or zero
#    carry windows on some human demos. It is NOT asymmetric with bot
#    demos by design -- serverrecord captures every entity's state every
#    frame with no PVS culling, so bot demos will tend to show MORE carry
#    windows for the same match than a human POV recording of the same
#    match would. A judge comparing carry-panel COUNTS across demo types
#    should discount this: it reflects recording vantage point, not skill.
#
# 3. 'captured' vs 'died' vs 'lost' is a geometric guess (proximity to a
#    stand estimated from this demo's own steal positions, or a
#    teleport-sized jump within ~1.6s of the flag clearing), not a ground
#    truth read from game state. Treat carry-panel outcome labels as
#    hypotheses, not verified facts.
#
# 4. The teleport/death-tick heuristic (>180 map units in one 100ms tick,
#    consecutive frames only) will also fire on legitimate high-speed
#    movement in rare cases (this corpus's own dm2speed.py episode digest
#    has logged sustained speeds past 700 u/s, i.e. 70 u/tick -- still
#    well under the 180 threshold, but a compounding trick jump plus a
#    frame at the edge of a sample gap could in principle trip it).
#
# 5. Team color comes from the skin path suffix (/rb-r../rb-b..), not
#    from join-team print text, so it's available identically for both
#    demo shapes. Entities whose skin doesn't match that pattern (refs,
#    unusual skins) are drawn in gray and excluded from flag-stand/carry
#    math only insofar as they're extremely unlikely to ever carry.
#
# 6. Flag-stand markers are a per-demo median of steal-start positions for
#    that color; a demo with zero steals of a given color draws no stand
#    marker for it (there's nothing to estimate from).
