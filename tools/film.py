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

CORPUS ASYMMETRY / --pov-parity (see apply_pov_parity and MODULE NOTE 10):
a HUMAN .dm2 is a client recording, so it only contains entity updates for
players inside the recorder's PVS -- other players' tracks are full of
holes (measured on this corpus: every non-recorder track carries 11-42% of
the frames, whole-demo coverage 0.30-0.41), and every sheet panel inherits
those holes as scars: sparse density maps, thin corridor histograms, carry
windows that end when the recorder looks away. A BOT serverrecord demo has
no PVS culling at all -- every player is sampled every frame, coverage
1.000 -- so bot sheets render CLEANER than human sheets for reasons that
have nothing to do with how the players moved. --pov-parity removes that
asymmetry by picking a virtual recorder inside a serverrecord demo and
dropping every other entity's samples that a real client at that recorder's
position would not have received.

CLI:
    film.py <demo.dm2> [...more demos] --out <dir> [--runedir <dir>]
            [--pov-parity [--pov-ent N] [--pov-radius U] [--pov-fov DEG]]

Writes <hash>.png (the sheet) and <hash>.json (source-mapping sidecar,
NOT blind -- this file exists for the unblinding step only) per demo.
Nothing about --pov-parity is ever drawn on the PNG: whether a sheet was
filtered is recorded ONLY in the sidecar, because a caption saying
"pov-parity" would itself unblind the sheet as a bot demo.
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

# --- duration normalization (judge round 3: sheets were leaking identity
# via raw duration -- bot waves cluster tight around ~895s while human POV
# recordings vary widely, so "how long is this demo" was itself a tell
# before any other panel got read). Every stat on the sheet (trajectories,
# carries, corridors, kinematic strip -- everything) is computed from at
# most DURATION_CAP_S seconds of track data, and demos shorter than
# DURATION_MIN_S are refused outright rather than rendered, because a
# histogram/heatmap built from too few samples reads as "tight" for
# sample-size reasons that have nothing to do with route consistency. -----
DURATION_CAP_S = 850.0      # see cap_tracks_to_duration
DURATION_MIN_S = 300.0      # see render_sheet's refusal check


# --- POV parity (see apply_pov_parity and MODULE NOTE 10) ---------------
# Radius, in world units, of the sphere around the virtual recorder inside
# which another player's sample is kept. This is the calibrated stand-in
# for a BSP PVS test: without map visibility data we cannot ask "is this
# entity's leaf visible from the recorder's leaf", so we ask "is it near
# enough that a real client would usually have had it" and pick the radius
# empirically. Calibration (2026-08-05, mactf06): the target statistic is
# whole-demo visible-player-seconds coverage (coverage_stats below --
# kept samples over players*frames), measured on the real client demos of
# the same map with the same code. The four renderable mactf06 human demos
# in the corpus read 0.300 / 0.320 / 0.375 / 0.410, median 0.348. Sweeping
# this radius over two 5v5 serverrecord waves (film.py --coverage-report
# --pov-parity --pov-radius R), with the virtual recorder chosen by
# pick_pov_entity:
#     R      wave368   wave369   mean
#     400     0.165     0.137    0.151
#     800     0.310     0.301    0.306
#     850     0.333     0.320    0.327
#     875     0.347     0.329    0.338
#     900     0.360     0.339    0.350   <- matches human median 0.348
#     950     0.388     0.361    0.375
#    1000     0.413     0.381    0.397
#    2000     ~0.80     ~0.65    ~0.72
# 900u is the value whose bot-corpus mean coverage equals the human median,
# and it puts both waves inside the human per-demo spread rather than only
# on average. ONE radius is used for every demo on purpose: fitting a
# radius per demo would guarantee a match by construction and measure
# nothing.
POV_RADIUS_DEFAULT = 900.0
POV_FOV_DEG_DEFAULT = None   # facing gate off by default -- see MODULE NOTE 10


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
    """o = [x, y, z, effects, yaw]. Same field order as
    dm2speed.parse_delta_entity, but captures origin, the full effects value
    (needed for EF_FLAG1/EF_FLAG2 carry detection) and the entity yaw angle
    instead of skipping them. Mirrors demoents.parse_delta_entity_track's
    shape with two additions.

    yaw (the U_ANGLE2 byte, angles[YAW]) is the recording-relevant one: for
    a player entity this mod writes the client's actual look direction into
    it -- p_view.c line 1033, `ent->s.angles[YAW] = ent->client->
    v_angle[YAW]` -- so it is a real view angle, not a movement heading, and
    it is present in BOTH demo shapes (measured: the U_ANGLE2 bit is set on
    79.8% of player-entity updates in a serverrecord wave and 71.2% in a
    human client demo). It is captured here for the optional facing gate in
    apply_pov_parity; nothing else on the sheet uses it.

    is_svrecord matters ONLY for the effects field, and only because of a
    real quirk in yquake2's demo writer (server/sv_entities.c
    SV_RecordDemoMessage): unlike a normal client update -- which deltas
    each entity against the state actually last SENT to that client, so an
    absent field bit genuinely means "unchanged since last frame" -- a
    serverrecord capture calls
        MSG_WriteDeltaEntity(&nostate, &ent->s, &buf, false, true)
    with an all-zero `nostate` as the "from" state on EVERY frame, for
    EVERY entity, not the previous frame's actual state. DeltaEntityBits()
    (common/movemsg.c) only sets a field's bit when it differs from `from`,
    so once effects goes back to 0 (flag dropped/captured, powerup
    expired), `to->effects (0) == from->effects (0)` and the bit is simply
    omitted -- exactly like when it was already 0 and stayed 0. A reader
    that treats "bit absent" as "retain last known value" (correct for
    client demos, where that IS the delta semantics) will see the entity's
    effects value get stuck at its last nonzero reading forever, because
    the wire never re-asserts zero. Verified empirically against
    wave360-s03-5v5.dm2 (serverrecord, game log shows a completed steal+
    capture): every tracked player's effects value went nonzero at some
    point and then simply never changed again for the rest of the file.
    The fix: for serverrecord demos only, re-derive effects from scratch
    every frame -- absent bits mean the field equals its zero default,
    because that IS what "from" was. Client demos are unaffected (the
    'retain previous value when bit absent' behavior stays default there,
    matching their true incremental delta wire semantics)."""
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
    # angles[YAW]: one byte, 0..255 mapped over 0..360 degrees (the vanilla
    # MSG_WriteAngle quantization). Same svrecord caveat as effects above --
    # an absent bit means "equals the zero reference", handled at the top.
    if bits & U_ANGLE2: o[4] = r.u8() * (360.0 / 256.0)
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
    'yaws': {entnum: {frame: yaw_degrees}}, 'frames', 'svrecord'}. tracks
    are filtered to entnum 1..maxplayers (player slots) exactly like
    demoents.py and botkin.py.

    'yaws' is kept in a SEPARATE dict rather than as a sixth element of each
    track tuple on purpose: every consumer in this module unpacks tracks as
    `for f, x, y, z, eff in track`, and widening the tuple would mean
    touching all of them for a field only apply_pov_parity's optional
    facing gate reads."""
    data = open(path, 'rb').read()
    off = 0
    mapname = None
    skins = {}
    ents = {}
    tracks = {}
    yaws = {}
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
                    o = ents.setdefault(num, [0.0, 0.0, 0.0, 0, 0.0])
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
    return {'map': mapname, 'skins': skins, 'tracks': tracks, 'yaws': yaws,
            'frames': frame_idx, 'svrecord': bool(svrecord)}


# --------------------------------------------------------- duration cap
def cap_tracks_to_duration(d, cap_s=DURATION_CAP_S):
    """Truncates every track in d['tracks'] (mutated in place) to at most
    cap_s seconds (cap_s * FPS frames) and updates d['frames'] to match, so
    EVERY stat computed downstream -- trajectory density, carry windows,
    corridors, the kinematic strip, the route-dissimilarity panel -- comes
    from an identical time budget regardless of how long the source demo
    actually ran. This runs immediately after walk_demo, before anonymize
    or any analysis, specifically so nothing downstream has to know or care
    whether it happened; it just sees a shorter d['tracks']/d['frames'].

    Why this exists: raw duration was itself an identity leak -- bot
    serverrecord waves cluster tightly around ~895s while human POV
    recordings vary a lot, so a judge could tell demo shapes apart from the
    caption line alone before reading a single panel.

    A carry window still in progress at the cutoff is never closed (the
    effects-bit state machine in carry_windows never sees a return to
    zero), so it simply doesn't appear in the windows list -- this is
    reported via render_sheet's notes, not silently.

    Returns (capped: bool, original_duration_s: float)."""
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
    """Assigns P1..Pn by entnum ascending. Drops short/noise tracks AND
    entities whose skin doesn't resolve to a red/blue team -- these are
    refs, spectators, and (seen in both demo shapes, not a bot-vs-human
    tell) idle reserve slots the test harness or a pickup server leaves
    connected without ever joining a side. A 'player' on this sheet is a
    roster participant; entnum range alone (1..maxclients) isn't enough
    to tell a competitor from a bystander, but a resolved team is.

    Also drops PARKED entities: a track that never moves at all (zero total
    travel, i.e. one single distinct position for its whole life) is not a
    roster participant either -- it is a connected-but-not-playing slot
    whose entity the server wrote once and never updated again, so the
    walker's persistent entity table re-snapshots that one frozen position
    every frame. Five such entities exist in lmctf-2022-02-08-mactf06-20.37
    (each with 3831 samples at exactly 1 distinct position). Left in, they
    do three bad things: they pile thousands of samples into a single
    density cell, they inflate the visible-player-seconds coverage number
    used for POV parity calibration, and -- because they have the most
    samples of any track in the demo -- they used to win the kinematic
    strip's "busiest 3 tracks" selection and render it as three flat 0.00
    u/s lines (see draw_kinematic_strip). The rule is applied to both demo
    shapes identically; serverrecord demos simply have no such entities.

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
        if track_travel(t) <= 0.0:
            continue
        keep.append(n)
        teams[n] = team
    keep.sort()
    labels = {n: f"P{i+1}" for i, n in enumerate(keep)}
    return labels, teams


# -------------------------------------------------------------- POV parity
def coverage_stats(tracks, labels, frames):
    """Visible-player-seconds coverage: how much of a full omniscient
    recording of this match the demo actually contains.

    visible_fraction = (sum of samples over all rostered tracks) /
                       (n_players * frames)
    i.e. 1.0 for a serverrecord capture (every player sampled every frame)
    and, measured across this corpus's mactf06 client demos, 0.32-0.40 for
    a real human POV recording. This is the statistic --pov-parity is
    calibrated against; it is computed by this one function for both demo
    shapes so the two corpora are never measured by different code.

    Returns {'visible_fraction', 'per_track': {entnum: fraction},
    'max_track_fraction', 'median_other_fraction', 'players', 'frames'}."""
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
    """The virtual recorder for --pov-parity: the rostered entity whose own
    track visits the most distinct map cells (track_cells), ties broken by
    most samples, then most travel, then lowest entnum.

    WHY THIS RULE, VALIDATED: it is the same rule for both demo shapes, and
    on a client demo it recovers the ACTUAL recorder every time -- on all
    four renderable mactf06 human demos in this corpus it selects entity 1,
    the recording player's own entity, whose per-track coverage is 0.83,
    0.94, 1.00, 1.00 (i.e. the one track the engine never culled). That is
    the check that makes it legitimate to apply to a serverrecord demo,
    where every track is complete and the rule instead selects the bot that
    actually roamed the map.

    Two rules that look reasonable and are NOT used, because they select
    the wrong kind of player: most samples (in a serverrecord demo every
    player has the identical count, so it decides nothing), and most travel
    (in wave369 the top traveler, ent 15 at 203k units, paces a confined
    area -- as a virtual recorder it produces a sheet with a density fill
    fraction of 0.244 against a human range of 0.417-0.502, because its
    visibility bubble never sweeps most of the map). Recorder choice is the
    single biggest lever on a parity sheet: across the ten candidates in
    wave369 the resulting fill fraction ranges 0.228-0.448. Anyone reading
    a parity sheet should know the rotation spread exists; --pov-ent N
    exists to reproduce it."""
    best = None
    for n in sorted(labels):
        t = tracks.get(n, [])
        key = (track_cells(t), len(t), track_travel(t))
        if best is None or key > best[0]:
            best = (key, n)
    return best[1] if best else None


def apply_pov_parity(d, labels, pov_ent=None, radius=POV_RADIUS_DEFAULT,
                     fov_deg=POV_FOV_DEG_DEFAULT):
    """Rewrites d['tracks'] in place so a serverrecord (omniscient) demo
    carries the same partial-view scars a client demo does.

    WHY: see the module docstring. A human .dm2 only contains entity
    updates for players the recording client's server-side PVS test let
    through (server/sv_ents.c SV_BuildClientFrame: leaf of the client's
    view origin -> CM_ClusterPVS -> per-entity cluster test; entities that
    fail it are sent as U_REMOVE and vanish from the client's entity list,
    which is why 37% of the player-entity updates in a human demo here are
    removes and 0% of them are in a serverrecord wave). A serverrecord
    capture runs no such test, so every bot sheet is built from complete
    tracks while every human sheet is built from shredded ones.

    HOW: pick a virtual recorder (pick_pov_entity, or --pov-ent), then keep
    another entity's sample at frame f only when the recorder also has a
    sample at f and the two are within `radius` world units (3D). The
    recorder's own track is never filtered -- a real client always has
    itself.

    WHY A DISTANCE GATE AND NOT A CONE: the real cull is position-only.
    SV_BuildClientFrame tests the client's view ORIGIN's PVS cluster; it
    never consults the client's view angles, so a player standing behind
    the recorder is received exactly like one in front. A facing gate is
    therefore available (fov_deg, using the real view yaw this mod writes
    into the entity's angles[YAW] -- p_view.c:1033) but OFF by default,
    because switching it on would impose a scar on the bot corpus that the
    human corpus does not have. Distance is the honest approximation: it is
    the one property of the PVS test we can evaluate without the BSP, it is
    monotone in the same direction as real visibility, and it has exactly
    one free parameter to calibrate (POV_RADIUS_DEFAULT). What it cannot
    reproduce is occlusion -- a player on the far side of a wall 200u away
    is kept here and would have been culled for real -- which is precisely
    why the radius is fit to the human coverage statistic rather than
    guessed from map geometry: the fitted radius absorbs the missing
    wall-culling into a smaller sphere.

    Returns an info dict (recorded in the JSON sidecar, never on the PNG)."""
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


def carry_outcome_summary(windows):
    """Compact outcome breakdown for the sheet (judge round 3): counts by
    outcome label (see classify_outcome: 'captured'/'died'/'lost') and
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
    """Pairwise discrete-Frechet distance between every sane carry route in
    this demo (judge round 3 request), plus two summary numbers computed
    from that matrix.

    mean_pairwise: mean of all N*(N-1)/2 off-diagonal distances (world
      units). Expectation per the judge's request: humans read high-mean
      (varied routes across the map), bots read low-mean (near-identical
      routes run over and over).

    entropy_bits: Shannon entropy (bits) of the cluster-SIZE distribution
      after single-linkage clustering the routes at cluster_frac of this
      demo's own max pairwise distance (self-normalizing -- see
      FRECHET_CLUSTER_FRAC). A field of near-identical routes collapses to
      ~1 cluster (entropy -> 0 bits, "blocky low-distance cluster" per the
      judge's framing); a field of genuinely distinct routes spreads across
      more, more-evenly-sized clusters (higher entropy).

    Returns (dist_matrix: NxN ndarray or None, mean_pairwise: float or
    None, entropy_bits: float or None, n_clusters: int). All None/0 when
    fewer than 2 carries -- there is no pair to compare, and clustering one
    route is not meaningful."""
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


LANE_MIN_SHARE = 0.05    # a bin must hold >= this fraction of a corridor's
                          # total crossings to count as its own "lane" --
                          # below this, treat it as noise around a bigger
                          # lane rather than a distinct path


def corridor_diversity(offsets, bin_width=CROSS_SECTION_BIN,
                        cap=CORRIDOR_OFFSET_CAP, min_share=LANE_MIN_SHARE):
    """Three diversity numbers for a corridor's perpendicular-offset
    distribution (see corridor_offsets docstring for what this distribution
    means -- "rope vs band"): a single-file lane and a spread-out crowd
    should NOT look the same on a judge's sheet, and a raw histogram alone
    makes that judgment call visually rather than numerically.

    lane_count: number of distinct local maxima (a bin, or flat run of
      equal-height bins, strictly higher than both its neighbors -- or
      higher than its one neighbor if it's an edge bin) whose share of
      total crossings is >= min_share. A tight single-file corridor scores
      1; a corridor with genuinely separate paths (e.g. two door-width
      lanes either side of an obstacle) scores >1.

    top_lane_share: the tallest bin's share of total crossings (0..1) --
      how dominant the single most-used lane is, regardless of whether it
      clears the min_share bar on its own (it always will, being the max).

    width_fraction: (max(offsets) - min(offsets)) / (2 * cap), i.e. the
      observed spread of crossings as a fraction of the widest possible
      capture window (see CORRIDOR_OFFSET_CAP). Close to 0 means everyone
      threads the same narrow line; close to 1 means traffic is spread
      across the full width this panel can even see. This is a numeric
      read of the same "rope vs band" signal the histogram shows visually.

    Returns {'lane_count': int, 'top_lane_share': float,
    'width_fraction': float}, all zero if offsets is empty."""
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


def draw_trajectory_map(ax, seeds, tracks, labels, teams, windows, stands,
                         extent=None):
    if seeds:
        sx = [s[0] for s in seeds]
        sy = [s[1] for s in seeds]
        ax.scatter(sx, sy, s=1.5, c=MAP_COLOR, zorder=1, linewidths=0)
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
    """BUG FIX (flat 0.00 strip): this panel used to pick the three tracks
    with the most SAMPLES. On a client demo that is the wrong ranking -- a
    parked, never-updated entity is sampled on every single frame (the
    walker re-snapshots its frozen position), so it outranks every real
    player, and the strip came out as three flat 0.00 u/s lines while the
    map panel above it plainly showed players moving. Observed on
    lmctf-2022-02-08-mactf06-20.37.dm2, where entities 2/9/10/12/13 each had
    3831 samples at ONE distinct position and won the old selection outright
    (a human sheet with a dead strip is exactly the kind of artifact that
    gets a human judged as a bot).

    Two independent guards now:
      1. rank by TRAVEL, not sample count (track_travel), so the panel shows
         the demo's three busiest MOVERS. anonymize() already drops
         zero-travel entities from the roster, so this is belt-and-braces
         against any track that is merely mostly-frozen.
      2. reject a candidate whose speed series is empty or all-zero/NaN and
         fall through to the next candidate, so a degenerate series can
         never occupy one of the three slots. If every candidate is
         degenerate the panel says so in words rather than drawing a
         convincing-looking flat line.

    Speed itself is derived from positions (hspeed_series), for both demo
    shapes, deliberately: the playerstate stream (svc_playerinfo) carries a
    real pmove velocity, but ONLY for the recording client and ONLY in
    client demos -- serverrecord captures contain no playerstate at all.
    Using it where available would make the strip a different instrument on
    human demos than on bot demos, which is the exact corpus asymmetry this
    tool exists to avoid."""
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
    """Judge round-3 panel: a triangular pairwise discrete-Frechet heatmap
    (left axes) of every sane carry route in this demo, plus a text summary
    (right axes) of the two requested numbers -- mean pairwise distance and
    route-choice entropy -- and the carry outcome/duration breakdown.
    Colormap is a single perceptually-uniform sequential hue (viridis):
    this panel encodes a magnitude (distance), not team identity, so it
    deliberately does NOT reuse the red/blue team hues used elsewhere on
    this sheet. See carry_route_dissimilarity/carry_outcome_summary for
    what each number means and MODULE NOTES for caveats."""
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
        w['outcome'] = classify_outcome(w, tracks, stands)
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
        for demo in args.demos:
            try:
                d = walk_demo(demo)
                if d['frames'] / FPS < DURATION_MIN_S:
                    print(f"SKIP {os.path.basename(demo)} (under-sampled)")
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
        if rows:
            print(f"\nn={len(rows)} coverage min={min(rows):.3f} "
                  f"median={float(np.median(rows)):.3f} max={max(rows):.3f}")
        return

    if args.pool:
        pooled = []
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
            except Exception as e:
                print(f"FAIL {demo.rsplit('/',1)[-1]} ({type(e).__name__})")
        _dist, mean_pw, ent_bits, n_cl = carry_route_dissimilarity(pooled)
        print(f"POOLED demos={len(args.demos)} carries={len(pooled)} "
              f"mean_frechet={(mean_pw if mean_pw is not None else float('nan')):.0f} "
              f"entropy={(ent_bits if ent_bits is not None else float('nan')):.2f} bits "
              f"clusters={n_cl}")
        return

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


if __name__ == '__main__':
    main()


# ----------------------------------------------------------------- MODULE
# NOTES (limitations a judge should know before trusting a sheet):
#
# 1. Carry-window detection uses only the EF_FLAG1/EF_FLAG2 effects bits on
#    the carrying player's entity. This was cross-validated against a bot
#    (serverrecord) demo where it matched the modelindex3 flag-attachment
#    signal exactly on carry START but modelindex3 silently failed to clear
#    on carry END. That "silently fails to clear" symptom turned out NOT to
#    be a game-code reset bug (nor is the earlier version of this note's
#    "effects is the more complete of the two" framing quite right) -- it's
#    a wire-format quirk in yquake2's server/sv_entities.c
#    SV_RecordDemoMessage: every serverrecord frame deltas each entity
#    against an all-zero reference state (MSG_WriteDeltaEntity(&nostate,
#    &ent->s, ...) with nostate memset to 0), not against the actual
#    previous frame like a normal client update does. So a field that
#    returns to zero (effects clearing, modelindex3 clearing) stops being
#    included in the delta from that frame on, because it now MATCHES the
#    always-zero reference -- there is no "it's zero now" signal on the
#    wire, only silence, and silence is exactly what a naive incremental-
#    delta reader also does when a field is genuinely unchanged. This bit
#    film.py (walk_demo/parse_delta_entity_film) for effects specifically:
#    once a carry started, effects would get stuck at its last nonzero
#    value for the rest of the demo, so the carry-detection state machine
#    (which looks for a return-to-zero to CLOSE a window) never saw one,
#    and reported carries=0 on demos where the game log showed real steals.
#    Confirmed against wave360-s03-5v5.dm2 (serverrecord; log line 12822
#    "Field[SG] captured the blue flag."): every tracked player's raw
#    effects value went nonzero at some point and then never changed again
#    for the rest of the file, under the old decode. Fixed by re-deriving
#    effects from scratch every frame for serverrecord demos specifically
#    (an absent effects bit means "equals its zero default", not "carry
#    forward the last value") -- see parse_delta_entity_film's is_svrecord
#    parameter. Client demos are untouched: their delta stream really is
#    incremental against the previous frame, so "bit absent = unchanged"
#    is the correct read there and always was.
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
#
# 7. Duration normalization (judge round 3): every stat on the sheet is
#    computed from at most DURATION_CAP_S (850s) of track data, and demos
#    under DURATION_MIN_S (300s) are refused rather than rendered -- see
#    cap_tracks_to_duration and render_sheet. This fixes raw duration
#    itself leaking demo identity (bot serverrecord waves cluster tightly
#    around ~895s; human POV recordings vary a lot), but it has a real
#    cost: a carry window still open at the 850s cutoff is dropped
#    entirely (the effects-bit state machine never sees it close), so a
#    capped sheet's carry count/outcome/dissimilarity numbers are computed
#    from a strict subset of the match, not the whole thing. The caption
#    and JSON sidecar both flag when a sheet was capped
#    (duration_capped/duration_original_s) so this is never silent.
#
# 8. Carry-route dissimilarity (judge round 3): the pairwise
#    discrete-Frechet matrix, mean pairwise distance, and route-choice
#    entropy (see carry_route_dissimilarity) are all computed AFTER
#    arc-length resampling each route to FRECHET_RESAMPLE_N points, so they
#    measure route SHAPE, not route duration or raw 10Hz sample count.
#    Two caveats: (a) the entropy number's cluster cutoff
#    (FRECHET_CLUSTER_FRAC of THIS demo's own max pairwise distance) is
#    self-normalized per demo/map on purpose, so entropy_bits is only
#    meaningful as a within-sheet read of "how clustered are these routes,
#    relative to the most different pair this same demo produced" -- it is
#    NOT on an absolute world-unit scale and should not be compared as if
#    it were a physical distance across two different maps; (b) both
#    numbers inherit note #2's PVS asymmetry -- a human demo with fewer
#    detected carries (because the recording player didn't see every
#    steal) has fewer routes to compare, which can itself inflate or
#    deflate the summary statistics independent of actual route diversity.
#    A judge should read n (routes compared) alongside the two numbers, not
#    the numbers alone.
#
# 9. Carry outcome/duration-quartile counts (see carry_outcome_summary)
#    are drawn from the same geometric-guess outcome labels described in
#    note #3 above, and from the same possibly-duration-capped windows
#    list described in note #7 -- both caveats apply here too.
#
# 10. POV parity (--pov-parity, see apply_pov_parity). Note #2 above
#    describes the asymmetry: a client (human) demo only contains what the
#    recorder's PVS let through, a serverrecord (bot) demo contains
#    everything. Measured on this corpus, that is not a subtle difference
#    -- human mactf06 demos carry 0.30-0.41 of the player-seconds a full
#    recording would have, bot waves carry 1.000 -- and every panel on the
#    sheet inherits it, so bot sheets render systematically CLEANER than
#    human sheets for recording-vantage reasons alone. --pov-parity imposes
#    the same scars on the bot corpus. What it does NOT do, and what a
#    judge must still discount:
#      (a) It is a distance gate, not a visibility test. No BSP data is
#          read, so it cannot cull a player standing behind a wall 200u
#          away, and it does cull one visible 1000u down a long sightline.
#          The radius is fitted to the human coverage statistic
#          (POV_RADIUS_DEFAULT), which absorbs the missing wall-culling
#          into a smaller sphere on average but not sample-by-sample.
#      (b) Recorder choice moves the numbers more than the radius does.
#          Rotating the virtual recorder over all ten candidates in
#          wave369 moves the sheet's density fill fraction across
#          0.228-0.448 and its coverage across 0.284-0.376. pick_pov_entity
#          uses a rule validated to recover the true recorder on human
#          demos, but "which player is the camera" remains a real free
#          choice, and --pov-ent N is there to re-run it.
#      (c) Carry COUNTS are still not comparable across demo shapes, and
#          not only in the direction note #2 predicted. A human demo can
#          also OVER-count: losing sight of a carrier mid-run closes one
#          window and the carrier's reappearance opens another, so one real
#          flag run can be reported as several. In the calibration pair the
#          human sheets show 21 and 14 carries against 5 and 4 on the
#          parity bot sheets -- part behavior, part fragmentation.
#      (d) Nothing about this mode is drawn on the PNG (it lives in the
#          sidecar's 'pov_parity' block), because a caption saying
#          "pov-parity" would itself unblind the sheet.
#
# 11. Kinematic strip track selection (bug fix, see draw_kinematic_strip).
#    The strip used to pick the three tracks with the most SAMPLES, which
#    on a client demo selects parked entities -- an entity the server wrote
#    once and never updated is re-snapshotted at its frozen position on
#    every frame, so it outranks every real player and the strip rendered
#    as flat 0.00 u/s lines while the map panel above it showed players
#    moving (lmctf-2022-02-08-mactf06-20.37.dm2: five such entities with
#    3831 samples at one distinct position each). Selection is now by
#    travel distance, parked entities are dropped from the roster entirely
#    (anonymize), and a candidate whose speed series is all-zero is skipped
#    rather than drawn. Speed remains position-derived for both demo
#    shapes: svc_playerinfo carries a real pmove velocity but only for the
#    recording client and only in client demos, so using it would make the
#    strip a different instrument on the two corpora.
