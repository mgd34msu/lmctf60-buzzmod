#!/usr/bin/env python3
"""Render comparable movement sheets from LMCTF demos.

The parser supports client and serverrecord streams. Event detection uses
entity effects available in both shapes. --pov-parity filters complete
serverrecord tracks through a virtual client view so visibility coverage does
not identify the recording type. PNGs are blind artifacts; JSON sidecars retain
source and filter metadata.
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

try:
    import runeio
except ModuleNotFoundError:
    from tools import runeio
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

# --- handoff detection (classify_outcome's 'handed_off' label): a carrier
# dies next to / drops for a teammate who immediately continues the run.
# Two windows of the SAME flag color, DIFFERENT entnum, SAME team as the
# first carrier, where the second starts close in time and space to where
# the first ended. HANDOFF_TIME_S is generous (a drop -> pickup can take a
# couple seconds of teammate travel); HANDOFF_TIME_TOLERANCE_S absorbs the
# 10Hz (FPS) frame quantization, where the two windows' t0/t1 can appear to
# overlap by a tick depending on per-entity frame order within the same
# server frame. HANDOFF_DIST_U is tighter than cap_radius -- a handoff is a
# body-length exchange, not a stand-radius event. Unvalidated against real
# handoff footage; see film.py's handoff measurement notes for what the
# corpus actually showed with these values.
HANDOFF_TIME_S = 2.0
HANDOFF_TIME_TOLERANCE_S = 0.15
HANDOFF_DIST_U = 300.0

# Fixed duration bounds prevent recording length from identifying a corpus.
DURATION_CAP_S = 850.0
DURATION_MIN_S = 300.0


# Fixed virtual-view radius used for every POV-parity sheet.
POV_RADIUS_DEFAULT = 900.0
POV_FOV_DEG_DEFAULT = None


class DemoUndersampled(Exception):
    """Raised by render_sheet when a demo is shorter than DURATION_MIN_S --
    caught separately in main() and reported as SKIP, not FAIL, since this
    is a deliberate refusal (avoid a misleading sheet), not an error."""

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

# --- carry-route dissimilarity diagnostic (judge round 3; see
# carry_route_dissimilarity/draw_dissimilarity_panel below) ---------------
FRECHET_RESAMPLE_N = 30      # points every carry route is arc-length
                              # resampled to before pairwise discrete-Frechet
                              # distance is computed -- makes the comparison
                              # about ROUTE SHAPE, not raw sample count (a
                              # short bot carry and a long human carry of the
                              # same physical route should compare as
                              # similar; unequal point counts alone would
                              # bias the DP toward calling them dissimilar).
FRECHET_CLUSTER_FRAC = 0.25  # single-linkage cluster cutoff for the
                              # route-choice entropy number, as a fraction of
                              # THIS demo's own max pairwise distance --
                              # self-normalizing per map/demo rather than a
                              # fixed world-unit threshold that would mean
                              # different things on different maps.

# --- fixed sheet layout (diagnostic fix: raster scale must not depend on
# whether carry panels exist -- see render_sheet) -------------------------
GRID_COLS = 6
ROW_HEIGHTS = [3.6, 1.35, 1.55, 1.55, 1.7, 1.7]
# map / carry / corridor / window / kin / route-dissimilarity+outcomes


# ------------------------------------------------------------ low-level walk
def parse_delta_entity_film(r, bits, o, is_svrecord=False):
    """Decode origin, effects, and view yaw into ``o``.

    Serverrecord entities are encoded against a zero baseline every frame, so
    absent effects and yaw fields reset to zero. Client demos retain prior
    values when those delta bits are absent.
    """
    if is_svrecord:
        o[3] = 0
        o[4] = 0.0
    if bits & U_MODEL: r.skip(1)
    if bits & U_MODEL2: r.skip(1)
    if bits & U_MODEL3: r.skip(1)
    if bits & U_MODEL4: r.skip(1)
    if bits & U_FRAME8: r.skip(1)
    if bits & U_FRAME16: r.skip(2)
    if (bits & U_SKIN8) and (bits & U_SKIN16): r.skip(4)
    elif bits & U_SKIN8: r.skip(1)
    elif bits & U_SKIN16: r.skip(2)
    # EFFECTS8 and EFFECTS16 together encode one 32-bit value.
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
    # angles[YAW]: one byte, 0..255 mapped over 0..360 degrees (the vanilla
    # MSG_WriteAngle quantization). Same svrecord caveat as effects above --
    # an absent bit means "equals the zero reference", handled at the top.
    if bits & U_ANGLE2: o[4] = r.u8() * (360.0 / 256.0)
    if bits & U_ANGLE3: r.skip(1)
    if bits & U_OLDORIGIN: r.skip(6)
    if bits & U_SOUND: r.skip(1)
    if bits & U_EVENT: r.skip(1)
    if bits & U_SOLID: r.skip(2)


def walk_demo(path, maxplayers=32, *, strict=False):
    """Decode client or serverrecord DM2 packets into player tracks, yaws, and skin epochs."""
    with open(path, 'rb') as source:
        data = source.read()
    off = 0
    mapname = None
    skins = {}
    skin_epochs = {}
    ents = {}
    tracks = {}
    yaws = {}
    wire_framenums = []
    frame_idx = 0
    svrecord = None
    parse_complete = True
    message_count = 0
    terminated = False

    def read_packetentities():
        while True:
            bits, num = D.parse_entity_bits(r)
            if num == 0:
                break
            if bits & U_REMOVE:
                ents.pop(num, None)
                continue
            o = ents.setdefault(num, [0.0, 0.0, 0.0, 0, 0.0])
            parse_delta_entity_film(r, bits, o, is_svrecord=bool(svrecord))

    def snapshot():
        nonlocal frame_idx
        frame_idx += 1
        for num, o in ents.items():
            if 1 <= num <= maxplayers:
                tracks.setdefault(num, []).append(
                    (frame_idx, o[0], o[1], o[2], o[3]))
                yaws.setdefault(num, {})[frame_idx] = o[4]

    while off < len(data):
        if off + 4 > len(data):
            if strict:
                raise ValueError("truncated demo message length")
            parse_complete = False
            break
        (mlen,) = struct.unpack_from('<i', data, off)
        off += 4
        if mlen == -1:
            terminated = True
            if strict and off != len(data):
                raise ValueError("demo has bytes after terminal marker")
            break
        if mlen < 0 or off + mlen > len(data):
            if strict:
                raise ValueError("invalid or truncated demo message")
            parse_complete = False
            break
        message_count += 1
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
                        slot = idx - 1312
                        if skins.get(slot) != s:
                            skins[slot] = s
                            skin_epochs.setdefault(slot, []).append(
                                (frame_idx + 1, s))
                    elif idx == 33:
                        m = re.match(r'maps/(\w+)\.bsp', s)
                        if m:
                            mapname = m.group(1)
                elif svc == 14:
                    bits, num = D.parse_entity_bits(r)
                    o = ents.setdefault(num, [0.0, 0.0, 0.0, 0, 0.0])
                    parse_delta_entity_film(r, bits, o)
                elif svc == 20:
                    if svrecord:
                        wire_framenums.append(r.s32())
                        svc2 = r.u8()
                        if svc2 != 18:
                            raise ValueError(
                                f"svrecord frame not followed by "
                                f"packetentities (got {svc2})")
                        # SV_RecordDemoMessage deltas every entity against an
                        # all-zero state, not the previous frame.  Each frame
                        # is therefore a complete independent inventory: an
                        # omitted entity is absent and an omitted origin field
                        # is exactly zero.  Retaining ``ents`` here would
                        # synthesize stale players and stale zero coordinates.
                        ents.clear()
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
            if strict and r.pos() != len(r.b):
                raise ValueError("demo message was not consumed exactly")
        except Exception as exc:
            if strict:
                raise ValueError(
                    f"malformed demo message ending at byte {off}") from exc
            parse_complete = False
            continue
    if strict:
        if message_count == 0 or svrecord is None or frame_idx == 0:
            raise ValueError("strict demo has no serverdata or frame messages")
        if not terminated:
            raise ValueError("strict demo has no terminal marker")
        if svrecord:
            if len(wire_framenums) != frame_idx:
                raise ValueError("serverrecord snapshot/frame inventory differs")
            if any(right != left + 1
                   for left, right in zip(wire_framenums, wire_framenums[1:])):
                raise ValueError("serverrecord wire frames are not consecutive")
    parse_complete = parse_complete and terminated
    return {'map': mapname, 'skins': skins, 'skin_epochs': skin_epochs,
            'tracks': tracks, 'yaws': yaws, 'frames': frame_idx,
            'wire_framenums': wire_framenums, 'svrecord': bool(svrecord),
            'parse_complete': parse_complete, 'terminated': terminated}


# --------------------------------------------------------- duration cap
def cap_tracks_to_duration(d, cap_s=DURATION_CAP_S):
    """Mutate decoded tracks and frame count to one common maximum duration."""
    orig_duration = d['frames'] / FPS
    cap_frames = int(round(cap_s * FPS))
    if d['frames'] <= cap_frames:
        return False, orig_duration
    for n in list(d['tracks'].keys()):
        d['tracks'][n] = [s for s in d['tracks'][n] if s[0] <= cap_frames]
    for n in list(d.get('yaws', {}).keys()):
        d['yaws'][n] = {f: y for f, y in d['yaws'][n].items()
                        if f <= cap_frames}
    d['frames'] = cap_frames
    return True, orig_duration


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
def track_travel(track):
    """Total horizontal distance covered by a track over consecutive-frame
    (dt=0.1s) steps, teleports/respawns excluded (same >TELEPORT_UNITS rule
    as hspeed_series/death_ticks). Used to tell a roster participant from a
    parked entity, and to pick which tracks the kinematic strip shows."""
    total = 0.0
    for i in range(1, len(track)):
        f0, x0, y0, z0, _ = track[i - 1]
        f1, x1, y1, z1, _ = track[i]
        if f1 - f0 != 1:
            continue
        dist = math.hypot(x1 - x0, y1 - y0)
        if dist <= TELEPORT_UNITS:
            total += dist
    return total


def anonymize(d):
    """Assign stable anonymous player labels and discard non-roster or noise tracks."""
    tracks = d['tracks']
    keep = []
    teams = {}
    for n, t in tracks.items():
        if len(t) < MIN_TRACK_SAMPLES:
            continue
        team = team_of(d['skins'].get(n - 1))
        if team is None:
            continue
        if track_travel(t) <= 0.0:
            continue
        keep.append(n)
        teams[n] = team
    keep.sort()
    labels = {n: f"P{i+1}" for i, n in enumerate(keep)}
    return labels, teams


# -------------------------------------------------------------- POV parity
def coverage_stats(tracks, labels, frames):
    """Measure visible player-seconds against a complete recording."""
    n = len(labels)
    if not n or not frames:
        return {'visible_fraction': 0.0, 'per_track': {}, 'players': n,
                'frames': frames, 'max_track_fraction': 0.0,
                'median_other_fraction': 0.0}
    per = {e: len(tracks.get(e, [])) / float(frames) for e in labels}
    fracs = sorted(per.values())
    total = sum(len(tracks.get(e, [])) for e in labels)
    others = fracs[:-1] if len(fracs) > 1 else fracs
    return {
        'visible_fraction': total / float(n * frames),
        'per_track': per,
        'max_track_fraction': fracs[-1],
        'median_other_fraction': float(np.median(others)) if others else 0.0,
        'players': n,
        'frames': frames,
    }


def track_cells(track, bin_size=None):
    """How many distinct world cells a track's samples land in, on the same
    fixed grid the density panel uses. A measure of how much of the MAP a
    track saw, as opposed to how far it ran (a defender who paces a small
    area racks up travel without covering ground)."""
    if bin_size is None:
        bin_size = TRAJ_DENSITY_BIN
    return len({(int(s[1] // bin_size), int(s[2] // bin_size))
                for s in track})


def pick_pov_entity(tracks, labels):
    """Select the rostered track with the broadest map coverage.

    Ties prefer more samples, then more travel, then the lowest entity number.
    The same deterministic rule applies to both demo shapes.
    """
    best = None
    for n in sorted(labels):
        t = tracks.get(n, [])
        key = (track_cells(t), len(t), track_travel(t))
        if best is None or key > best[0]:
            best = (key, n)
    return best[1] if best else None


def apply_pov_parity(d, labels, pov_ent=None, radius=POV_RADIUS_DEFAULT,
                     fov_deg=POV_FOV_DEG_DEFAULT):
    """Filter serverrecord tracks through a deterministic virtual-client view."""
    tracks = d['tracks']
    if pov_ent is not None:
        # An explicitly requested recorder that doesn't exist is an error,
        # not a silent no-op: falling through unfiltered would hand back a
        # fully omniscient sheet that looks exactly like a parity sheet
        # everywhere except one line of the sidecar.
        if pov_ent not in labels:
            raise ValueError(
                f"--pov-ent {pov_ent} is not a rostered track in this demo "
                f"(rostered: {sorted(labels)})")
    else:
        pov_ent = pick_pov_entity(tracks, labels)
    if pov_ent is None or pov_ent not in tracks:
        return {'applied': False, 'reason': 'no rostered track to record from'}

    rec = {s[0]: (s[1], s[2], s[3]) for s in tracks[pov_ent]}
    rec_yaw = d.get('yaws', {}).get(pov_ent, {})
    r2 = radius * radius
    half_fov = (fov_deg / 2.0) if fov_deg else None

    before = sum(len(t) for t in tracks.values())
    for n in list(tracks.keys()):
        if n == pov_ent:
            continue
        kept = []
        for s in tracks[n]:
            f, x, y, z = s[0], s[1], s[2], s[3]
            p = rec.get(f)
            if p is None:
                continue
            dx, dy, dz = x - p[0], y - p[1], z - p[2]
            if dx * dx + dy * dy + dz * dz > r2:
                continue
            if half_fov is not None:
                yaw = rec_yaw.get(f)
                if yaw is None:
                    continue
                bearing = math.degrees(math.atan2(dy, dx))
                delta = abs((bearing - yaw + 180.0) % 360.0 - 180.0)
                if delta > half_fov:
                    continue
            kept.append(s)
        tracks[n] = kept
    after = sum(len(t) for t in tracks.values())
    return {'applied': True, 'pov_entnum': pov_ent, 'radius_u': radius,
            'fov_deg': fov_deg, 'samples_before': before,
            'samples_after': after,
            'sample_keep_fraction': (after / before) if before else 0.0}


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


def classify_outcome(w, tracks, stands, cap_radius=280.0, lookahead_s=1.6,
                      windows=None, teams=None,
                      handoff_time_s=HANDOFF_TIME_S,
                      handoff_dist_u=HANDOFF_DIST_U):
    """Infer a carry outcome from flag effects, positions, and teleport geometry."""
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
    died = False
    for i in range(1, len(track)):
        f0, x0, y0, z0, _ = track[i - 1]
        f1, x1, y1, z1, _ = track[i]
        if f1 < end_frame or f1 > end_frame + la:
            continue
        if f1 - f0 == 1 and math.hypot(x1 - x0, y1 - y0) > TELEPORT_UNITS:
            died = True
            break
    if windows is not None and teams is not None:
        carrier_team = teams.get(w['entnum'])
        if carrier_team is not None:
            for w2 in windows:
                if w2 is w or w2['color'] != w['color']:
                    continue
                if w2['entnum'] == w['entnum']:
                    continue
                if teams.get(w2['entnum']) != carrier_team:
                    continue
                dt = w2['t0'] - w['t1']
                if dt < -HANDOFF_TIME_TOLERANCE_S or dt > handoff_time_s:
                    continue
                start_pos = w2['path'][0][1:3]
                dist = math.hypot(start_pos[0] - end_pos[0],
                                   start_pos[1] - end_pos[1])
                if dist <= handoff_dist_u:
                    return 'handed_off'
    return 'died' if died else 'lost'


def carry_outcome_summary(windows):
    """Compact outcome breakdown for the sheet (judge round 3): counts by
    outcome label (see classify_outcome: 'captured'/'died'/'lost', plus
    'handed_off' when film.py's own render_sheet call supplies windows/
    teams) and
    duration quartiles (seconds) across every sane carry window in this
    demo. Returns {'counts': {label: n}, 'quartiles': (q1,q2,q3) or None,
    'n': int}. quartiles is None when there are zero carries."""
    counts = collections.Counter(w.get('outcome', '?') for w in windows)
    durations = sorted(w['t1'] - w['t0'] for w in windows)
    quartiles = None
    if durations:
        q1, q2, q3 = np.percentile(durations, [25, 50, 75])
        quartiles = (float(q1), float(q2), float(q3))
    return {'counts': dict(counts), 'quartiles': quartiles, 'n': len(windows)}


# ------------------------------------------------ carry-route dissimilarity
def resample_path_xy(path, n=FRECHET_RESAMPLE_N):
    """Arc-length resample of a carry path's (x,y) to n evenly spaced
    points, so discrete-Frechet distance (below) compares route SHAPE, not
    raw sample count -- see FRECHET_RESAMPLE_N. `path` is a carry window's
    'path' list of (f,x,y,z) tuples (see carry_windows). A degenerate
    (single-point or zero-length) path resamples to n copies of that one
    point rather than raising."""
    pts = np.array([(p[1], p[2]) for p in path], dtype=np.float64)
    if len(pts) == 1:
        return np.repeat(pts, n, axis=0)
    seglen = np.hypot(np.diff(pts[:, 0]), np.diff(pts[:, 1]))
    cum = np.concatenate(([0.0], np.cumsum(seglen)))
    total = cum[-1]
    if total == 0:
        return np.repeat(pts[:1], n, axis=0)
    targets = np.linspace(0.0, total, n)
    xs = np.interp(targets, cum, pts[:, 0])
    ys = np.interp(targets, cum, pts[:, 1])
    return np.stack([xs, ys], axis=1)


def discrete_frechet(P, Q):
    """Discrete Frechet distance (Eiter & Mannila 1994) between two
    polylines P, Q (each an Nx2 array of resampled points): the standard
    O(n*m) coupling-measure DP, ca[i,j] = max(point-distance(P[i],Q[j]),
    min of the three predecessor cells), filled iteratively bottom-up (not
    recursively -- avoids a Python recursion-depth ceiling and keeps this
    fast for the many pairs a busy demo produces)."""
    n, m = len(P), len(Q)
    ca = np.zeros((n, m), dtype=np.float64)
    for i in range(n):
        pix, piy = P[i]
        for j in range(m):
            d = math.hypot(pix - Q[j][0], piy - Q[j][1])
            if i == 0 and j == 0:
                ca[i, j] = d
            elif i == 0:
                ca[i, j] = max(ca[0, j - 1], d)
            elif j == 0:
                ca[i, j] = max(ca[i - 1, 0], d)
            else:
                ca[i, j] = max(min(ca[i - 1, j], ca[i - 1, j - 1],
                                    ca[i, j - 1]), d)
    return ca[n - 1, m - 1]


def carry_route_dissimilarity(windows, n_resample=FRECHET_RESAMPLE_N,
                               cluster_frac=FRECHET_CLUSTER_FRAC):
    """Return pairwise discrete Frechet distances and route-cluster summaries."""
    n = len(windows)
    if n < 2:
        return None, None, None, 0
    resampled = [resample_path_xy(w['path'], n_resample) for w in windows]
    dist = np.zeros((n, n), dtype=np.float64)
    for i in range(n):
        for j in range(i + 1, n):
            d = discrete_frechet(resampled[i], resampled[j])
            dist[i, j] = d
            dist[j, i] = d
    pairwise = dist[np.triu_indices(n, k=1)]
    mean_pairwise = float(pairwise.mean())

    max_d = float(pairwise.max())
    threshold = max_d * cluster_frac
    parent = list(range(n))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    for i in range(n):
        for j in range(i + 1, n):
            if dist[i, j] <= threshold:
                union(i, j)
    sizes = np.array(list(collections.Counter(
        find(i) for i in range(n)).values()), dtype=np.float64)
    p = sizes / sizes.sum()
    entropy_bits = float(-(p * np.log2(p)).sum())
    return dist, mean_pairwise, entropy_bits, len(sizes)


# ------------------------------------------------------------------- rune
def load_rune_seeds(rune_path):
    artifact = runeio.read(rune_path)
    return [(seed.origin[0], seed.origin[1]) for seed in artifact.seeds]


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
    """Return the highest-traffic straight corridors over the RUNE seed cloud."""
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


LANE_MIN_SHARE = 0.05    # a bin must hold >= this fraction of a corridor's
                          # total crossings to count as its own "lane" --
                          # below this, treat it as noise around a bigger
                          # lane rather than a distinct path


def corridor_diversity(offsets, bin_width=CROSS_SECTION_BIN,
                        cap=CORRIDOR_OFFSET_CAP, min_share=LANE_MIN_SHARE):
    """Summarize corridor offsets by lane count, dominant share, and width fraction."""
    zero = {'lane_count': 0, 'top_lane_share': 0.0, 'width_fraction': 0.0}
    if not offsets:
        return zero
    lo = math.floor(min(offsets) / bin_width) * bin_width
    hi = math.ceil(max(offsets) / bin_width) * bin_width
    if hi <= lo:
        hi = lo + bin_width
    bins = np.arange(lo, hi + bin_width, bin_width)
    counts, _ = np.histogram(offsets, bins=bins)
    total = int(counts.sum())
    if total == 0:
        return zero
    shares = counts / total

    lane_count = 0
    n = len(counts)
    i = 0
    while i < n:
        if shares[i] < min_share:
            i += 1
            continue
        j = i
        while j + 1 < n and counts[j + 1] == counts[i]:
            j += 1
        left_ok = (i == 0) or (counts[i] > counts[i - 1])
        right_ok = (j == n - 1) or (counts[j] > counts[j + 1])
        if left_ok and right_ok:
            lane_count += 1
        i = j + 1

    top_lane_share = float(shares.max())
    width = max(offsets) - min(offsets)
    width_fraction = min(1.0, width / (2 * cap)) if cap > 0 else 0.0
    return {'lane_count': lane_count, 'top_lane_share': top_lane_share,
            'width_fraction': width_fraction}


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
import matplotlib.colors as mcolors

TEAM_COLOR = {'red': '#c0392b', 'blue': '#2166ac', None: '#7f7f7f'}
MAP_COLOR = '#c9c9c9'
KIN_COLORS = ['#c0392b', '#2166ac', '#7a7a7a']

# DIAGNOSTIC FIX (log-scale trajectory density): world-space bin size for
# the full-game trajectory panel's per-pixel traversal-count histogram.
# "per-pixel" here means one fixed-size world-unit cell, not one screen
# pixel -- consistent with the rest of this sheet's fixed-raster-scale
# design (see the DIAGNOSTIC FIX 1 comment in draw_trajectory_map below):
# the same map always gets the same bin grid regardless of which demo is
# being rendered, so density maps are comparable sheet-to-sheet.
TRAJ_DENSITY_BIN = 16.0


def _truncated_cmap(name, lo=0.28, hi=1.0, n=256):
    """A sequential colormap with its palest end clipped off -- the raw
    'Reds'/'Blues' colormaps start at near-white, which would make sparse
    (but real) low-traffic cells invisible against this sheet's white
    background. Clipping the low end means even a single-crossing cell
    still shows a visible tint."""
    base = plt.get_cmap(name)
    return mcolors.ListedColormap(base(np.linspace(lo, hi, n)))


DENSITY_CMAPS = {'red': _truncated_cmap('Reds'), 'blue': _truncated_cmap('Blues')}


def alpha_ramp_segments(xs, ys, base_hex, lo=0.12, hi=0.92):
    if len(xs) < 2:
        return None
    pts = list(zip(xs, ys))
    segs = [[pts[i], pts[i + 1]] for i in range(len(pts) - 1)]
    r, g, b = mcolors.to_rgb(base_hex)
    n = len(segs)
    colors = [(r, g, b, lo + (hi - lo) * (i / max(1, n - 1)))
              for i in range(n)]
    return LineCollection(segs, colors=colors, linewidths=1.6)


def _autoscale_extent(tracks, labels, pad_frac=0.05):
    """Fallback world-extent when no rune was loaded for this map (see
    compute_seed_extent) -- derived from this demo's own kept-track points
    instead, so the density histogram still has bin edges to work with."""
    xs, ys = [], []
    for n, track in tracks.items():
        if n not in labels:
            continue
        for f, x, y, z, eff in track:
            xs.append(x); ys.append(y)
    if not xs:
        return (-100.0, 100.0, -100.0, 100.0)
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    dx = (xmax - xmin) or 100.0
    dy = (ymax - ymin) or 100.0
    return (xmin - dx * pad_frac, xmax + dx * pad_frac,
            ymin - dy * pad_frac, ymax + dy * pad_frac)


def _densify_track_points(track, step=None):
    """Sub-samples points along consecutive-frame (dt=0.1s), non-teleport
    segments of a track at roughly `step` world-unit spacing (default:
    half a density bin), so the 10Hz sample rate doesn't leave gaps in the
    per-pixel traversal-count histogram along fast-moving segments -- this
    makes the histogram count how much of the floor a path actually
    covered, not just where its 10Hz samples happened to land. Segments
    spanning a teleport (>TELEPORT_UNITS in one tick, same rule as
    hspeed_series/death_ticks) are not interpolated across, but both
    endpoints are still counted individually."""
    if step is None:
        step = TRAJ_DENSITY_BIN * 0.5
    pts = []
    prev = None
    for f, x, y, z, eff in track:
        if prev is not None:
            f0, x0, y0 = prev
            if f - f0 == 1:
                dist = math.hypot(x - x0, y - y0)
                if dist <= TELEPORT_UNITS:
                    nseg = max(1, int(dist // step))
                    for k in range(1, nseg):
                        t = k / nseg
                        pts.append((x0 + (x - x0) * t, y0 + (y - y0) * t))
        pts.append((x, y))
        prev = (f, x, y)
    return pts


def trajectory_density_hist(tracks, labels, teams, team, extent,
                             bin_size=TRAJ_DENSITY_BIN):
    """Per-pixel (see TRAJ_DENSITY_BIN) traversal-count 2D histogram for
    every kept track on `team`, pooled. Returns (H, xedges, yedges) like
    np.histogram2d, or None if `team` has no kept tracks with data."""
    xmin, xmax, ymin, ymax = extent
    nx = max(1, int(math.ceil((xmax - xmin) / bin_size)))
    ny = max(1, int(math.ceil((ymax - ymin) / bin_size)))
    xedges = xmin + np.arange(nx + 1) * bin_size
    yedges = ymin + np.arange(ny + 1) * bin_size
    xs, ys = [], []
    for n, track in tracks.items():
        if n not in labels or teams.get(n) != team:
            continue
        for x, y in _densify_track_points(track):
            xs.append(x); ys.append(y)
    if not xs:
        return None
    H, xe, ye = np.histogram2d(xs, ys, bins=[xedges, yedges])
    return H, xe, ye


def density_fill_stats(tracks, labels, teams, extent, seeds=None,
                        bin_size=TRAJ_DENSITY_BIN):
    """How much of the map panel's density grid this demo actually lit up.

    fill_fraction      = occupied cells / all cells in the fixed grid
    reachable_fraction = occupied cells / cells that contain at least one
                         rune seed (i.e. cells that are walkable floor at
                         all -- most of the grid is void, so this is the
                         more readable of the two; it is None when no rune
                         was loaded).
    Both are computed from the same fixed, seed-derived extent the map
    panel is drawn on (see draw_trajectory_map's DIAGNOSTIC FIX 1), so they
    are comparable sheet-to-sheet for a given map. Occupancy pools both
    teams -- a cell counts as filled if either team crossed it."""
    occupied = None
    for team in ('red', 'blue'):
        res = trajectory_density_hist(tracks, labels, teams, team, extent,
                                       bin_size=bin_size)
        if res is None:
            continue
        H = res[0]
        occupied = (H >= 1) if occupied is None else (occupied | (H >= 1))
    if occupied is None:
        return {'fill_fraction': 0.0, 'reachable_fraction': None,
                'cells_occupied': 0, 'cells_total': 0, 'cells_reachable': 0}
    total = int(occupied.size)
    n_occ = int(occupied.sum())
    reach = None
    n_reach = 0
    if seeds:
        xmin, xmax, ymin, ymax = extent
        nx, ny = occupied.shape
        sx = np.asarray([s[0] for s in seeds], dtype=np.float64)
        sy = np.asarray([s[1] for s in seeds], dtype=np.float64)
        ix = np.clip(((sx - xmin) / bin_size).astype(int), 0, nx - 1)
        iy = np.clip(((sy - ymin) / bin_size).astype(int), 0, ny - 1)
        seedgrid = np.zeros_like(occupied)
        seedgrid[ix, iy] = True
        n_reach = int(seedgrid.sum())
        if n_reach:
            reach = float((occupied & seedgrid).sum()) / n_reach
    return {'fill_fraction': n_occ / float(total), 'reachable_fraction': reach,
            'cells_occupied': n_occ, 'cells_total': total,
            'cells_reachable': n_reach}


def draw_map_silhouette(ax, seeds, extent=None):
    """The faint rune-seed cloud that every map panel is drawn on top of.

    Lifted verbatim out of draw_trajectory_map (which still calls it) so the
    routes sheet in routesheet.py can put its node graph on the identical
    background without duplicating the call -- a pure refactor, no behavior
    change: same marker size, same color, same zorder, same call order, so a
    rung-1 sheet rendered before and after this change is byte-identical.

    `extent` is accepted and ignored; it exists so callers that already hold
    the map's fixed seed-derived extent can pass it without a special case
    (the seed cloud IS the extent's source, so clipping to it is a no-op)."""
    if seeds:
        sx = [s[0] for s in seeds]
        sy = [s[1] for s in seeds]
        ax.scatter(sx, sy, s=1.5, c=MAP_COLOR, zorder=1, linewidths=0)


def draw_trajectory_map(ax, seeds, tracks, labels, teams, windows, stands,
                         extent=None):
    draw_map_silhouette(ax, seeds, extent)
    # DIAGNOSTIC FIX (log-scale trajectory density): the old alpha-ramp
    # line plot drew every kept track's full-game path as a translucent
    # line; with a busy roster and a long match, thousands of overlapping
    # segments saturate to solid color and all spatial information about
    # WHERE traffic concentrates is lost (a "hairball"). This bins every
    # traversed point (both teams pooled per-team, then overlaid) into a
    # fixed-size world-space grid and renders traversal COUNT on a log
    # color scale, so a corridor crossed 200 times reads as visibly denser
    # than one crossed twice, instead of both reading as "a line was here."
    hist_extent = extent if extent is not None else _autoscale_extent(tracks, labels)
    layers = []
    vmax = 1
    for team in ('red', 'blue'):
        res = trajectory_density_hist(tracks, labels, teams, team, hist_extent)
        if res is None:
            continue
        H, xe, ye = res
        layers.append((team, H, xe, ye))
        vmax = max(vmax, int(H.max()))
    if layers:
        norm = mcolors.LogNorm(vmin=1, vmax=vmax)
        for team, H, xe, ye in layers:
            Hm = np.ma.masked_less(H, 1)
            # histogram2d indexes H as [x, y]; imshow wants [row=y, col=x].
            ax.imshow(Hm.T, origin='lower',
                      extent=(xe[0], xe[-1], ye[0], ye[-1]),
                      cmap=DENSITY_CMAPS[team], norm=norm, alpha=0.88,
                      interpolation='nearest', zorder=2, aspect='auto')
        sm = plt.cm.ScalarMappable(norm=norm, cmap='Greys')
        sm.set_array([])
        # fig.colorbar(..., ax=ax) (rather than an inset axes placed by
        # hand) carves its space OUT of ax's own layout box before the
        # equal-aspect data limits are resolved at draw time, so it can
        # never land on top of plotted density -- it shrinks the map panel
        # by a small, always-identical amount instead, which preserves
        # this sheet's fixed-scale guarantee (every render shrinks it the
        # same way, so world-units-per-pixel is still constant map-to-map).
        cb = ax.figure.colorbar(sm, ax=ax, fraction=0.035, pad=0.015,
                                 shrink=0.5, aspect=25)
        cb.set_label('traversal count (log)', fontsize=6)
        cb.ax.tick_params(labelsize=6)
    # event markers (steal/captured/died/lost/stand) are drawn AFTER the
    # density layers and at a higher zorder, so they stay legible on top
    # of even the most saturated density cells.
    for color, pos in stands.items():
        ax.scatter([pos[0]], [pos[1]], marker='*', s=260,
                    c=TEAM_COLOR[color], edgecolors='black',
                    linewidths=0.8, zorder=4)
    markers = {'steal': ('^', 90), 'captured': ('*', 140),
               'died': ('X', 70), 'lost': ('v', 70),
               'handed_off': ('P', 90)}
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
        # Keep the shrunk (equal-aspect) box centered in its cell even
        # after fig.colorbar(..., ax=ax) steals a slice of this axes'
        # width for the density colorbar -- otherwise the box anchors to
        # whichever corner is left over from the steal, which reads as the
        # map being randomly offset rather than just smaller.
        ax.set_anchor('C')
    else:
        ax.set_aspect('equal', adjustable='datalim')
    ax.set_xticks([]); ax.set_yticks([])
    for spine in ax.spines.values():
        spine.set_visible(False)
    legend_handles = [
        Line2D([0], [0], color=DENSITY_CMAPS['red'](0.85), lw=6,
               label='red team density'),
        Line2D([0], [0], color=DENSITY_CMAPS['blue'](0.85), lw=6,
               label='blue team density'),
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
    ax.set_title(f'full-game trajectory density ({TRAJ_DENSITY_BIN:.0f}u cells, log scale)',
                 fontsize=10)


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
    div = corridor_diversity(offsets)
    # DIAGNOSTIC FIX (corridor slot overlap, text half): with 8 panels
    # across one figure width each panel is only ~1.1-1.3in wide -- the
    # old single long title line (world coordinates + counts + bins all on
    # one line) was wider than that at any legible font size and spilled
    # text across the panel boundary into whichever neighbor happened to
    # be drawn first, which is what actually produced the "clashing
    # titles" a judge would see (the histograms themselves were, by that
    # point, already in distinct cells -- see the corridor_gs fix in
    # render_sheet). Keeping every line short and dropping the world
    # coordinates from the on-sheet title (still in the JSON sidecar's
    # 'corridors' list, which is explicitly not the blind artifact) fixes
    # the visual collision without touching the layout.
    # One Text object (set_title with embedded newlines), not a title plus
    # a second hand-placed text block -- two separately-anchored text
    # objects both sitting near the axes' top edge is what caused the
    # stats line to overlap the "corridor N" label in an earlier version
    # of this fix; matplotlib lays out a single multi-line title's lines
    # without that collision.
    ax.set_title(
        f"corridor {idx}\n"
        f"n={len(offsets)}\n"
        f"lanes={div['lane_count']}  top={div['top_lane_share']*100:.0f}%\n"
        f"width={div['width_fraction']*100:.0f}%",
        fontsize=6, linespacing=1.5)
    ax.set_xlabel('offset (u)', fontsize=6.5)
    ax.tick_params(labelsize=5.5)
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
    """Draw speed traces for the three tracks with the greatest travel."""
    candidates = sorted(
        ((n, t) for n, t in tracks.items() if n in labels),
        key=lambda kv: -track_travel(kv[1]))
    drawn = 0
    for n, track in candidates:
        if drawn >= 3:
            break
        series = hspeed_series(track)
        finite = [v for _, v in series if v == v]
        if not series or not finite or max(finite) <= 0.0:
            continue
        ts = [s[0] for s in series]
        vs = [s[1] for s in series]
        color = KIN_COLORS[drawn % len(KIN_COLORS)]
        ax.plot(ts, vs, color=color, lw=0.8, alpha=0.85,
                label=f"track {drawn+1}")
        for fr in death_by_ent.get(n, []):
            ax.axvline(fr / FPS, color=color, lw=0.6, ls=':', alpha=0.5)
        drawn += 1
    if drawn == 0:
        ax.text(0.5, 0.5, 'no track in this demo has a non-zero speed '
                'series -- strip suppressed rather than drawn flat',
                ha='center', va='center', fontsize=8, color='#993333',
                transform=ax.transAxes)
    ax.set_xlabel('time (s)', fontsize=8)
    ax.set_ylabel('h-speed (u/s)', fontsize=8)
    ax.set_title('kinematic strip -- busiest 3 tracks (dotted = death tick)',
                  fontsize=9)
    ax.tick_params(labelsize=7)
    if drawn:
        ax.legend(fontsize=7, loc='upper right')


def draw_dissimilarity_panel(ax_heat, ax_text, windows, dist_matrix,
                              mean_pairwise, entropy_bits, n_clusters,
                              outcome_summary):
    """Draw carry-route distance, entropy, and outcome summaries."""
    n = len(windows)
    if dist_matrix is None or n < 2:
        ax_heat.axis('off')
        ax_heat.text(0.5, 0.5,
                      f"insufficient carries for route-dissimilarity "
                      f"analysis (need >=2, got {n})",
                      ha='center', va='center', fontsize=8, color='#666666',
                      transform=ax_heat.transAxes, wrap=True)
    else:
        mask = np.triu(np.ones(dist_matrix.shape, dtype=bool), k=1)
        masked = np.ma.masked_array(dist_matrix, mask=mask)
        im = ax_heat.imshow(masked, cmap='viridis', origin='upper',
                             aspect='equal', interpolation='nearest')
        cb = ax_heat.figure.colorbar(im, ax=ax_heat, fraction=0.045,
                                      pad=0.03, shrink=0.85)
        cb.set_label('Frechet distance (u)', fontsize=6)
        cb.ax.tick_params(labelsize=6)
        step = max(1, n // 15)
        ticks = list(range(n))
        labels = [str(i + 1) if i % step == 0 else '' for i in ticks]
        ax_heat.set_xticks(ticks); ax_heat.set_yticks(ticks)
        ax_heat.set_xticklabels(labels, fontsize=5)
        ax_heat.set_yticklabels(labels, fontsize=5)
        ax_heat.tick_params(length=0)
        for spine in ax_heat.spines.values():
            spine.set_visible(False)
    ax_heat.set_title(f'carry-route dissimilarity (n={n} routes, '
                       f'{FRECHET_RESAMPLE_N}-pt resampled, lower '
                       f'triangle)', fontsize=8)

    ax_text.axis('off')
    lines = []
    if mean_pairwise is not None:
        lines.append(f"mean pairwise distance: {mean_pairwise:.1f} u")
        lines.append(f"route-choice entropy: {entropy_bits:.2f} bits")
        lines.append(f"  ({n_clusters} cluster(s) @ "
                      f"{FRECHET_CLUSTER_FRAC*100:.0f}% cutoff)")
    else:
        lines.append("mean pairwise distance: n/a (< 2 carries)")
        lines.append("route-choice entropy: n/a (< 2 carries)")
    lines.append("")
    oc = outcome_summary['counts']
    lines.append("carry outcomes:")
    lines.append(f"  captured={oc.get('captured', 0)}  "
                  f"lost={oc.get('lost', 0)}  died={oc.get('died', 0)}")
    q = outcome_summary['quartiles']
    if q:
        lines.append("carry duration quartiles:")
        lines.append(f"  Q1={q[0]:.1f}s  med={q[1]:.1f}s  Q3={q[2]:.1f}s")
    else:
        lines.append("carry duration quartiles: n/a (no carries)")
    ax_text.text(0.02, 0.95, '\n'.join(lines), ha='left', va='top',
                 fontsize=8, family='monospace', transform=ax_text.transAxes)
    ax_text.set_title('carry outcome / route summary', fontsize=8)


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
def render_sheet(demo_path, rune_dir, out_dir, max_carry_panels=6,
                 pov_parity=False, pov_ent=None,
                 pov_radius=POV_RADIUS_DEFAULT, pov_fov=POV_FOV_DEG_DEFAULT):
    d = walk_demo(demo_path)

    # duration normalization (judge round 3): refuse demos too short to
    # sample reliably rather than rendering a misleading sheet, then cap
    # everything else to at most DURATION_CAP_S seconds so raw duration
    # stops leaking demo identity. This must happen before anonymize/
    # carry_windows/anything else -- see cap_tracks_to_duration docstring.
    uncapped_duration = d['frames'] / FPS
    if uncapped_duration < DURATION_MIN_S:
        raise DemoUndersampled(
            f"demo duration {uncapped_duration:.1f}s is under the "
            f"{DURATION_MIN_S:.0f}s minimum sample threshold -- too short "
            f"to render reliable stats, skipped rather than producing a "
            f"misleading sheet")
    duration_capped, orig_duration = cap_tracks_to_duration(d)

    # POV parity (see apply_pov_parity). Order matters: anonymize once to
    # get a roster to pick the virtual recorder from, filter, then anonymize
    # AGAIN so the final roster is the one that survived the filter -- a
    # player the virtual recorder never got near enough to see should drop
    # off this sheet exactly like a player a human recorder never saw drops
    # off theirs (MIN_TRACK_SAMPLES / zero-travel rules do that).
    labels, teams = anonymize(d)
    pov_info = {'applied': False}
    if pov_parity:
        if not d['svrecord']:
            pov_info = {'applied': False,
                        'reason': 'not a serverrecord demo -- a client demo '
                                  'is already PVS-filtered by the engine'}
        else:
            pov_info = apply_pov_parity(d, labels, pov_ent=pov_ent,
                                        radius=pov_radius, fov_deg=pov_fov)
            labels, teams = anonymize(d)
    tracks = d['tracks']
    windows, n_excluded_carries = carry_windows(tracks, labels)
    stands = flag_stands(windows)
    for w in windows:
        w['outcome'] = classify_outcome(w, tracks, stands,
                                        windows=windows, teams=teams)
    death_by_ent = {n: death_ticks(t) for n, t in tracks.items() if n in labels}

    dist_matrix, mean_pairwise, entropy_bits, n_clusters = \
        carry_route_dissimilarity(windows)
    outcome_summary = carry_outcome_summary(windows)

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
    hist_extent = map_extent if map_extent is not None \
        else _autoscale_extent(tracks, labels)
    coverage = coverage_stats(tracks, labels, d['frames'])
    fill = density_fill_stats(tracks, labels, teams, hist_extent, seeds)
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

    # row 2: corridor cross-section histograms -- fixed N_CORRIDORS slots.
    # DIAGNOSTIC FIX (corridor slot overlap): GRID_COLS (6) does not evenly
    # divide N_CORRIDORS (8), so splitting this row's integer column range
    # by round(i * GRID_COLS/N_CORRIDORS) produced duplicate column ranges
    # for some i (two panels landing on the exact same gs[2, c0:c1] cell,
    # drawn one over the other -- two histograms and two clashing titles
    # superimposed) while other slots were skipped. A nested subgridspec
    # scoped to just this row's cell sidesteps the divisibility problem
    # entirely: it gets its own N_CORRIDORS equal-width columns, unrelated
    # to GRID_COLS, so every one of the 8 panels gets a distinct cell.
    corridor_gs = gs[2, :].subgridspec(1, N_CORRIDORS, wspace=0.45)
    corridor_diversity_list = []
    corridor_crossings = []
    for i in range(N_CORRIDORS):
        ax = fig.add_subplot(corridor_gs[0, i])
        if i < len(corridors):
            offs = corridor_offsets(corridors[i], tracks, labels)
            draw_corridor_panel(ax, corridors[i], offs, i + 1)
            corridor_diversity_list.append(corridor_diversity(offs))
            corridor_crossings.append(len(offs))
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

    # row 5: carry-route dissimilarity heatmap (left 4 cols) + outcome/
    # duration summary text (right 2 cols) -- judge round 3.
    ax_diss = fig.add_subplot(gs[5, 0:4])
    ax_diss_text = fig.add_subplot(gs[5, 4:6])
    draw_dissimilarity_panel(ax_diss, ax_diss_text, windows, dist_matrix,
                              mean_pairwise, entropy_bits, n_clusters,
                              outcome_summary)

    # The caption carries NOTHING structural (leak checklist L1/L2, and the
    # embarrassing sequel to the set-#3 seal: the cap NOTE was removed while
    # this line kept printing duration= -- and players=, which reads 10 on
    # every wave and anything on a pub demo. Sets #3 and #4 both showed
    # judges these fields; the ledger carries the taint. Map and hash only:
    # the map is matched across the whole set by design, the hash is the
    # blind identity.
    caption = (f"map={d['map'] or '?'}   hash={h}   "
               f"carries={len(windows)}")
    fig.text(0.5, 0.985, caption, ha='center', fontsize=10, weight='bold')
    notes = []
    if rune_note:
        notes.append(rune_note)
    if n_excluded_carries:
        notes.append(f"{n_excluded_carries} anomalously long carry "
                      f"window(s) excluded (>{MAX_CARRY_S:.0f}s, likely a "
                      f"stuck flag-carry bit across a round boundary)")
    # Duration says NOTHING on the sheet, ever (judge set #3): the cap note
    # printed the original to the tenth, and bot waves share one timelimit
    # to the tenth -- a judge sorted the corpus on "exactly 895.2s" alone.
    # Worse, the note's mere PRESENCE discriminated: human client demos run
    # under the cap and carried no note, bot waves always did. The cap and
    # the original now live only in the JSON sidecar, which is unblinding
    # material by definition.
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
        'duration_capped': duration_capped,
        'duration_original_s': orig_duration,
        'players_rendered': n_players,
        'carry_windows': len(windows),
        'carry_windows_excluded_anomalous': n_excluded_carries,
        'carry_route_mean_pairwise_frechet': mean_pairwise,
        'carry_route_choice_entropy_bits': entropy_bits,
        'carry_route_clusters': n_clusters,
        'carry_outcome_counts': outcome_summary['counts'],
        'carry_duration_quartiles_s': outcome_summary['quartiles'],
        'entnum_to_label': {str(k): v for k, v in labels.items()},
        'label_to_name': {v: d['skins'].get(k - 1, '?').split('\\')[0]
                           for k, v in labels.items()},
        'label_to_team': {labels[k]: teams[k] for k in labels},
        'rune_used': bool(seeds),
        'map_extent': list(map_extent) if map_extent else None,
        # pov_parity, coverage and density_fill live in the sidecar ONLY --
        # never in the caption or anywhere else on the PNG. A sheet that
        # announced it had been POV-filtered would identify itself as a
        # serverrecord demo, which is exactly the tell this mode removes.
        'pov_parity': pov_info,
        'coverage': {
            'visible_fraction': coverage['visible_fraction'],
            'max_track_fraction': coverage['max_track_fraction'],
            'median_other_track_fraction': coverage['median_other_fraction'],
            'per_track_fraction': {labels[e]: v
                                   for e, v in coverage['per_track'].items()},
        },
        'density_fill': fill,
        'corridors': [{'p0': c['p0'], 'p1': c['p1'], 'traffic': c['traffic'],
                       'crossings': corridor_crossings[i],
                       **corridor_diversity_list[i]}
                      for i, c in enumerate(corridors)],
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
        'duration_capped': duration_capped,
        'carries': len(windows), 'png': png_path, 'json': json_path,
        'mean_pairwise_frechet': mean_pairwise,
        'route_choice_entropy_bits': entropy_bits,
        'pov_parity': pov_info,
        'visible_fraction': coverage['visible_fraction'],
        'fill_fraction': fill['fill_fraction'],
        'reachable_fraction': fill['reachable_fraction'],
        'corridor_crossings': corridor_crossings,
        'outcome_counts': outcome_summary['counts'],
    }


DEFAULT_RUNEDIR = os.path.expanduser('~/Games/Quake2/lmctf-hooktest/maps')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('demos', nargs='+', help='.dm2 demo files')
    ap.add_argument('--out', required=True, help='output directory')
    ap.add_argument('--pool', action='store_true',
                    help='no sheets: pool carry routes across ALL input '
                         'demos and print one dissimilarity/entropy read '
                         '(per-wave entropies on 5-13 carries bounce '
                         '+/-0.7 bits; arms differ by less)')
    ap.add_argument('--runedir', default=DEFAULT_RUNEDIR,
                     help='directory holding <map>.rune files '
                          f'(default: {DEFAULT_RUNEDIR})')
    ap.add_argument('--pov-parity', action='store_true',
                     help='serverrecord demos only: keep another player\'s '
                          'sample only when a virtual recorder could '
                          'plausibly have seen it, so bot sheets carry the '
                          'same partial-view scars client (human) demos do '
                          '(see apply_pov_parity). No-op on client demos, '
                          'which the engine already PVS-filtered.')
    ap.add_argument('--pov-ent', type=int, default=None,
                     help='entity number to use as the virtual recorder '
                          '(default: most samples, then most travel)')
    ap.add_argument('--pov-radius', type=float, default=POV_RADIUS_DEFAULT,
                     help='visibility radius in world units for --pov-parity '
                          f'(default: {POV_RADIUS_DEFAULT:.0f}, calibrated '
                          'against human client-demo coverage)')
    ap.add_argument('--pov-fov', type=float, default=POV_FOV_DEG_DEFAULT,
                     help='optional facing gate in degrees (full cone width) '
                          'for --pov-parity. OFF by default and normally '
                          'should stay off: the engine cull is position-only '
                          '(SV_BuildClientFrame tests the view origin\'s PVS '
                          'cluster, not the view angles), so a cone imposes a '
                          'scar the human corpus does not have.')
    ap.add_argument('--coverage-report', action='store_true',
                     help='no sheets: print the visible-player-seconds '
                          'coverage of each input demo (the statistic '
                          '--pov-radius is calibrated against) and their '
                          'median')
    args = ap.parse_args()

    if args.coverage_report:
        rows = []
        failures = 0
        for demo in args.demos:
            try:
                d = walk_demo(demo)
                if d['frames'] / FPS < DURATION_MIN_S:
                    print(f"SKIP {os.path.basename(demo)} (under-sampled)")
                    failures += 1
                    continue
                cap_tracks_to_duration(d)
                labels, _teams = anonymize(d)
                if args.pov_parity and d['svrecord']:
                    apply_pov_parity(d, labels, pov_ent=args.pov_ent,
                                     radius=args.pov_radius,
                                     fov_deg=args.pov_fov)
                    labels, _teams = anonymize(d)
                cov = coverage_stats(d['tracks'], labels, d['frames'])
                rows.append(cov['visible_fraction'])
                print(f"{'bot ' if d['svrecord'] else 'human'} "
                      f"{os.path.basename(demo):45s} map={d['map']} "
                      f"players={cov['players']:2d} "
                      f"coverage={cov['visible_fraction']:.3f} "
                      f"max_track={cov['max_track_fraction']:.3f} "
                      f"median_other={cov['median_other_fraction']:.3f}")
            except Exception as e:
                print(f"FAIL {os.path.basename(demo)}: {type(e).__name__}: {e}")
                failures += 1
        if rows:
            print(f"\nn={len(rows)} coverage min={min(rows):.3f} "
                  f"median={float(np.median(rows)):.3f} max={max(rows):.3f}")
        return 1 if failures else 0

    if args.pool:
        pooled = []
        failures = 0
        for demo in args.demos:
            try:
                d = walk_demo(demo)
                cap_tracks_to_duration(d)
                labels, _teams = anonymize(d)
                if args.pov_parity and d['svrecord']:
                    apply_pov_parity(d, labels, pov_ent=args.pov_ent,
                                     radius=args.pov_radius,
                                     fov_deg=args.pov_fov)
                    labels, _teams = anonymize(d)
                ws, _excl = carry_windows(d['tracks'], labels)
                pooled.extend(ws)
            except DemoUndersampled:
                print(f"SKIP {demo.rsplit('/',1)[-1]} (under-sampled)")
                failures += 1
            except Exception as e:
                print(f"FAIL {demo.rsplit('/',1)[-1]} ({type(e).__name__})")
                failures += 1
        _dist, mean_pw, ent_bits, n_cl = carry_route_dissimilarity(pooled)
        print(f"POOLED demos={len(args.demos)} carries={len(pooled)} "
              f"mean_frechet={(mean_pw if mean_pw is not None else float('nan')):.0f} "
              f"entropy={(ent_bits if ent_bits is not None else float('nan')):.2f} bits "
              f"clusters={n_cl}")
        return 1 if failures else 0

    ok, failed, skipped = [], [], []
    for path in args.demos:
        try:
            res = render_sheet(path, args.runedir, args.out,
                                pov_parity=args.pov_parity,
                                pov_ent=args.pov_ent,
                                pov_radius=args.pov_radius,
                                pov_fov=args.pov_fov)
            ok.append(res)
            dur_str = f"{res['duration']:.1f}s(capped)" if res['duration_capped'] \
                else f"{res['duration']:.1f}s"
            pov = res['pov_parity']
            pov_str = (f" pov=ent{pov['pov_entnum']}@{pov['radius_u']:.0f}u"
                       if pov.get('applied') else "")
            print(f"OK   {os.path.basename(path)} -> {res['hash']}.png  "
                  f"map={res['map']} {'bot' if res['svrecord'] else 'human'} "
                  f"players={res['players']} dur={dur_str} "
                  f"carries={res['carries']}{pov_str} "
                  f"vis={res['visible_fraction']:.3f} "
                  f"fill={res['fill_fraction']:.3f}")
        except DemoUndersampled as e:
            skipped.append((path, str(e)))
            print(f"SKIP {os.path.basename(path)}: {e}")
        except Exception as e:
            failed.append((path, str(e)))
            print(f"FAIL {os.path.basename(path)}: {e}")

    print(f"\n{len(ok)} sheet(s) written to {args.out}, "
          f"{len(skipped)} skipped (under-sampled), "
          f"{len(failed)} failed")
    return 1 if skipped or failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
