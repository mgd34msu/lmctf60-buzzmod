#!/usr/bin/env python3
"""teamsheet.py -- rung 4 (TEAM PLAY): a blind team-behavior sheet.

Same blinding discipline as film.py, routesheet.py and fightsheet.py:
identical extraction for client (human) and serverrecord (bot) demos, no
durations, no roster counts, fixed geometry, constant axis scales.  See
film.py's MODULE NOTES 1-11; notes 2, 7 and 10 apply here verbatim.  Nothing
on the PNG reveals demo shape; the sidecar is the unblinding artifact.

WHAT THIS SHEET ASKS: not "can this player fight" (rung 3) or "does this
player navigate the map well" (rung 2), but does this TEAM play as a team --
do teammates stay in supporting distance of each other, does a flag carrier
get escorted home, is the home flag actually watched, and does an attack
arrive as a coordinated push or as five separate solo runs.

ZERO NEW DEMO PARSING.  Every panel here is built entirely from
F.walk_demo's position stream (d['tracks']) and F.carry_windows' effects-bit
flag-carry detection -- both already proven by film.py and routesheet.py.
There is one extraction path and it is shared, so nothing here can be
richer on one demo shape than on the other (L6).

FLAG STAND POSITIONS.  film.py's walker tracks only player entities
(entnum 1..maxplayers); the flag item entities are never captured, so there
is no first-frame flag-entity position to read off the wire.  The stand
position this module uses instead is F.flag_stands' existing estimate: the
median (x, y) of this demo's own carry-start positions for that color --
already computed and used by film.py's outcome classifier, reused here
rather than re-derived.  That only gives an answer for a color that was
actually stolen at least once in THIS demo.  When it wasn't (including the
common case of zero steals of one color, or a demo with no carries at all),
this module falls back to --stands, a JSON file of
{"mapname": {"red": [x,y,z], "blue": [x,y,z]}}, and RAISES StandsMissing
with a clear message identifying the map and the missing color(s) when
neither source has an answer.  Panels 3 and 4 need both stand positions to
mean anything (a defense radius around an unknown point, a push axis with
one endpoint missing); panels 1 and 2 do not, but the sheet is refused as a
whole rather than rendered with two panels silently blank, because a sheet
some demos have a panel and some don't is exactly the kind of drawn-string-
set asymmetry the leak checklist (L2/L3) exists to catch.

REQUIRED PANELS, one figure per demo:
  1. spacing      -- pairwise teammate distance, median + IQR band, per team,
                      over normalized match time.
  2. escort       -- carry windows plotted over time, colour = fraction of
                      the window with a teammate within ESCORT_RADIUS of the
                      carrier; companion bar shows the per-team mean.
  3. defense      -- fraction of time each team has >=1 player within
                      DEFENSE_RADIUS of its own flag stand, with steal/
                      capture tick marks.
  4. push sync    -- count of each team's players simultaneously on the
                      enemy side of the axis connecting the two flag stands.

FAIRNESS RULE (ABSOLUTE).  The caption may show ONLY: map name, an 8-char
content hash of the demo file, and the count of carry windows -- the exact
set film.py's own caption uses (see film.py's caption comment).  No
duration, no player counts, no filenames, no population-conditional
annotations: nothing whose presence or format differs between a bot and a
human demo.

CLI:
    teamsheet.py <demo.dm2> [...] --out <dir> [--stands <file.json>]
                 [--pov-parity [--pov-ent N] [--pov-radius U] [--pov-fov D]]
    teamsheet.py <demo.dm2> [...] --scalars [--stands <file.json>]
                 [--pov-parity] [--cache <path>]

Writes <hash>.png (the sheet) and <hash>.json (source-mapping sidecar, NOT
blind -- that file exists for the unblinding step only) per demo, hash-named
by film.py's hash_demo so one demo carries one hash across every rung and a
single unblinding table serves all of them.
"""
import argparse
import collections
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import film as F
import routesheet as RS         # roc_auc / _ranks / glob helpers ONLY, for
                                 # --calibrate -- same reuse fightsheet.py
                                 # makes of this module
import mapflags as MF           # read_game_file/bsp_entities/parse_ents ONLY,
                                 # for major_item_locations -- same
                                 # already-proven-elsewhere reuse rule
                                 # resolve_stands applies to F.flag_stands

import numpy as np

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D


# ------------------------------------------------------------- constants
ESCORT_RADIUS = 400.0      # u; "teammate is escorting the carrier" threshold,
                            # per the design brief
DEFENSE_RADIUS = 600.0     # u; "a player is watching the home flag" threshold
TIME_BINS = 60             # fixed bins across normalized match time, used by
                            # panels 1, 3 and 4 -- an instrument constant, the
                            # same on every sheet regardless of this demo's
                            # own duration (which is itself never shown, L1)
ESCORT_SLOTS = 8           # FIXED, padded (L3) -- carry-window rows shown on
                            # panel 2's timeline, chosen by duration, drawn in
                            # time order, same rule fightsheet's timeline and
                            # disengage panels use

# Fixed axis ceilings.  Every scale on this sheet is a CONSTANT, never a
# function of this demo's own values, so units-per-pixel can never change
# between two sheets (L7/L8).  A value above its ceiling is CLIPPED, and that
# clipping is stated in the always-present notes strip, never in a note that
# only appears on the sheets where it happened (L2).
SPACING_YMAX = 4000.0      # u; headroom over a full map diagonal -- this is a
                            # placeholder ceiling, not corpus-calibrated (no
                            # Stage-A run has been done for this rung yet);
                            # being a constant is what makes it safe to use
                            # uncalibrated, not its exact value
PUSH_YMAX = 6.0             # attacker count; headroom above a 5-a-side roster

# --- EYE 1 (major_item_presence, gates the item-timing dark feature) and
# EYE 2 (postspawn_purpose, gates sg_spawnbeat) instrument constants ------
MAJOR_ITEM_CLASSNAMES = ('item_health_mega', 'item_armor_body')
                            # mega health and red/body armor -- verified in
                            # this mod's own source, not assumed: both
                            # respawn on the SAME fixed cycle
                            # (g_items.c:595 MegaHealth_think's
                            # SetRespawn(self, 20); g_items.c:759
                            # Pickup_Armor's generic SetRespawn(ent, 20),
                            # which item_armor_body falls through to
                            # unmodified -- there is no armor-specific
                            # override).
MAJOR_ITEM_RESPAWN_S = 20.0 # both items in MAJOR_ITEM_CLASSNAMES, confirmed above
MAJOR_ITEM_RADIUS = 300.0   # u; "near the item", per the design brief
ITEM_PICKUP_RADIUS = 48.0   # u; item bbox is +/-16, player bbox +/-16 in the
                            # horizontal -- ~32u is the true touch distance;
                            # 48u pads for 10Hz position quantization (the
                            # tick the engine registers a touch and the
                            # tick this stream happens to sample are not
                            # guaranteed to be the same one).
ITEM_TAKE_MERGE_S = 3.0     # two dwell episodes -- by the same or different
                            # players -- within this many seconds of each
                            # other are almost certainly the same physical
                            # pickup moment (a contested item two players
                            # both reach for), not two independent takes;
                            # merged to the earlier one so a scramble does
                            # not manufacture a fake short respawn cycle.
ITEM_PRE_RESPAWN_WINDOW_S = 5.0  # per the design brief
POSTSPAWN_WINDOW_S = 3.0    # seconds after a respawn measured for heading
                            # consistency, per the design brief
POSTSPAWN_MIN_FRAMES = 10   # >=1.0s of clean, gap-free, teleport-free path
                            # required before a life's heading-consistency
                            # ratio is trusted -- below this the ratio is
                            # close to structurally 1.0 or undefined on 1-2
                            # samples regardless of intent (see
                            # postspawn_heading_consistency's docstring).

TEAM_COLOR = F.TEAM_COLOR
TEAMS = ('red', 'blue')

GRID_COLS = 24
ROW_HEIGHTS = [3.0, 2.2, 1.1, 2.4, 2.4, 1.5]
FIGSIZE = (12, 15.5)
FIGDPI = 140


class StandsMissing(Exception):
    """Raised when panels 3/4 need a flag stand position this demo's own
    carry windows never established and --stands does not supply either.
    Caught in main() and reported as SKIP, the same treatment
    F.DemoUndersampled and routesheet.RouteFixtureMissing get -- a refusal
    to render a misleading (partial) sheet, not a crash."""


# ============================================================ analysis
def position_index(tracks, ents):
    """{entnum: {frame: (x, y, z)}}, restricted to the given entities."""
    return {n: {s[0]: (s[1], s[2], s[3]) for s in tracks.get(n, [])}
            for n in ents}


def team_members(labels, teams):
    return {t: sorted(n for n in labels if teams.get(n) == t) for t in TEAMS}


def resolve_stands(mapname, windows, stands_file):
    """{'red': (x,y), 'blue': (x,y)} or raise StandsMissing.

    Primary source is F.flag_stands(windows), the same carry-start-position
    estimate film.py's own outcome classifier uses.  --stands (a mapname ->
    {red:[x,y,z], blue:[x,y,z]} JSON file) fills in a color this demo never
    had a carry of.  Only the (x, y) plane is used from either source --
    flag_stands never had a z component (film.py MODULE the carry path only
    stores it for completeness, flag_stands' own median is 2-D), so keeping
    both sources 2-D avoids inventing a z the two could disagree on."""
    derived = F.flag_stands(windows)
    out = {}
    missing = []
    for color in TEAMS:
        if color in derived:
            out[color] = (float(derived[color][0]), float(derived[color][1]))
            continue
        entry = (stands_file or {}).get(mapname or '', {}).get(color)
        if entry is not None and len(entry) >= 2:
            out[color] = (float(entry[0]), float(entry[1]))
            continue
        missing.append(color)
    if missing:
        raise StandsMissing(
            f"flag stand position(s) missing for {missing} on "
            f"map={mapname!r}: not derivable from this demo's own carry "
            f"windows (no steal of that color occurred) and not present in "
            f"the --stands file for this map. Provide --stands as a JSON "
            f"file mapping mapname -> "
            f'{{"red": [x,y,z], "blue": [x,y,z]}}.')
    return out


def teammate_pairwise_distances(posidx, members):
    """{team: {frame: [distance, ...]}} -- every pairwise teammate distance
    (3-D, world units) sampled at a frame both teammates were seen at."""
    out = {t: collections.defaultdict(list) for t in TEAMS}
    for team, ents in members.items():
        for i in range(len(ents)):
            for j in range(i + 1, len(ents)):
                a, b = ents[i], ents[j]
                pa, pb = posidx[a], posidx[b]
                for f in (set(pa) & set(pb)):
                    xa, ya, za = pa[f]
                    xb, yb, zb = pb[f]
                    d = math.sqrt((xa - xb) ** 2 + (ya - yb) ** 2
                                  + (za - zb) ** 2)
                    out[team][f].append(d)
    return out


def _bin_index(frame, frames_total, nbins):
    if frames_total <= 0:
        return 0
    return min(nbins - 1, int(((frame - 1) / frames_total) * nbins))


def bin_pooled(frame_values, frames_total, nbins=TIME_BINS):
    """{frame: [values]} -> list of nbins lists, each the pool of every
    value whose frame fell in that bin."""
    bins = [[] for _ in range(nbins)]
    for f, vals in frame_values.items():
        bins[_bin_index(f, frames_total, nbins)].extend(vals)
    return bins


def bin_median_iqr(bins):
    n = len(bins)
    med = np.full(n, np.nan)
    lo = np.full(n, np.nan)
    hi = np.full(n, np.nan)
    for i, vals in enumerate(bins):
        if vals:
            arr = np.asarray(vals, dtype=np.float64)
            med[i] = np.median(arr)
            lo[i] = np.percentile(arr, 25)
            hi[i] = np.percentile(arr, 75)
    return med, lo, hi


def window_escort_fraction(w, posidx, teams, labels, radius=ESCORT_RADIUS):
    """Fraction of `w`'s sampled carrier frames (w['path']) during which at
    least one OTHER rostered player on the carrier's own team was sampled
    within `radius` of the carrier.  Returns (fraction or None, total,
    escorted, team) -- total/escorted are frame counts, kept so the
    demo-level scalar can be a duration-weighted mean rather than a mean of
    per-window fractions (a 2-second window and a 90-second window should
    not count equally).

    KNOWN DISTORTION, kept for the record rather than fixed in place (see
    window_escort_fraction_obs below for the fix): `total` here is every
    carrier-sampled frame, escorted or not, and a frame where the carrier
    was sampled but no teammate's position was sampled ANYWHERE in the demo
    (occluded, off the recorder's PVS, out of the bot pov-parity sphere --
    the module cannot tell which) is silently counted in `total` and NOT in
    `escorted`, i.e. scored as unescorted. Human demos hit this far more
    than bot demos (real BSP occlusion vs a 900u distance sphere with no
    occlusion at all), so this scalar deflates the human number specifically
    rather than measuring escort behaviour on equal footing. This function
    is left exactly as it was -- the Stage-A record above is keyed to this
    exact number and must stay reproducible -- do not redefine it here."""
    carrier = w['entnum']
    team = teams.get(carrier)
    mates = [n for n in labels if teams.get(n) == team and n != carrier]
    total = 0
    escorted = 0
    r2 = radius * radius
    for f, x, y, z in w['path']:
        total += 1
        for m in mates:
            p = posidx.get(m, {}).get(f)
            if p is None:
                continue
            dx, dy, dz = p[0] - x, p[1] - y, p[2] - z
            if dx * dx + dy * dy + dz * dz <= r2:
                escorted += 1
                break
    frac = (escorted / total) if total else None
    return frac, total, escorted, team


def window_escort_fraction_obs(w, posidx, teams, labels, radius=ESCORT_RADIUS):
    """Same question as window_escort_fraction (is the carrier escorted),
    but with an honest denominator: a carrier-sampled frame only counts
    (in either the numerator or the denominator) when at least one of the
    carrier's teammates was ALSO sampled somewhere in the demo at that same
    frame -- i.e. the demo stream could, in principle, have shown a nearby
    teammate at that instant. A frame where the carrier was sampled but no
    teammate's position exists anywhere in the demo at that frame gives no
    information about whether an escort was present (occlusion, PVS, or the
    bot pov-parity sphere could each explain the absence) and is dropped
    from both the numerator and the denominator, rather than being read as
    "not escorted" the way window_escort_fraction reads it.

    Returns (fraction_obs or None, total_obs, escorted_obs, implied_frames,
    team). implied_frames is the window's nominal frame span (last sampled
    carrier frame minus first, +1) -- what carry_coverage's denominator
    uses, i.e. how many frames the window would have spanned under
    continuous sampling regardless of what was actually captured."""
    carrier = w['entnum']
    team = teams.get(carrier)
    mates = [n for n in labels if teams.get(n) == team and n != carrier]
    total_obs = 0
    escorted_obs = 0
    r2 = radius * radius
    for f, x, y, z in w['path']:
        mate_seen = False
        within = False
        for m in mates:
            p = posidx.get(m, {}).get(f)
            if p is None:
                continue
            mate_seen = True
            dx, dy, dz = p[0] - x, p[1] - y, p[2] - z
            if dx * dx + dy * dy + dz * dz <= r2:
                within = True
                break
        if not mate_seen:
            continue
        total_obs += 1
        if within:
            escorted_obs += 1
    if w['path']:
        implied_frames = w['path'][-1][0] - w['path'][0][0] + 1
    else:
        implied_frames = 0
    frac_obs = (escorted_obs / total_obs) if total_obs else None
    return frac_obs, total_obs, escorted_obs, implied_frames, team


def major_item_locations(mapname, gamedir, items_file):
    """[(x, y, z), ...] major-item (mega health / body armor) origins for
    `mapname`, plus a short provenance string.

    Primary source: the map's own BSP entity lump, read with mapflags.py's
    existing read_game_file/bsp_entities/parse_ents (reused verbatim, not
    re-implemented here -- the same rule resolve_stands already follows by
    reusing F.flag_stands rather than re-deriving flag positions).
    Requires --gamedir, a Quake2 game directory containing
    maps/<mapname>.bsp, loose or inside a .pak (mapflags.read_game_file
    checks both, later pak wins, matching the engine's own search order).

    Fallback: --items, a JSON file of {mapname: [[x,y,z], ...]}, for a map
    whose BSP is unavailable. That is NOT the same case as a map whose BSP
    IS available but simply has no item_health_mega/item_armor_body
    entity in it at all -- lmctf22 is exactly this case (confirmed by
    running this function against it; see the measurement note at the
    bottom of this file). A map with zero major items is a real map fact,
    not a missing-data gap --items can fix, and is reported as a distinct
    'none: bsp read but no major item entity' reason rather than silently
    falling through to --items and reading as "nobody supplied it".

    Returns (locations, source) where source is 'bsp', 'items-file', or a
    'none: ...' string naming why neither source had an answer."""
    if gamedir:
        try:
            data = MF.read_game_file(gamedir, f'maps/{mapname}.bsp')
        except Exception:
            data = None
        if data:
            ents = MF.parse_ents(MF.bsp_entities(data))
            locs = []
            for e in ents:
                if e.get('classname') in MAJOR_ITEM_CLASSNAMES:
                    o = e.get('origin', '0 0 0').split()
                    if len(o) == 3:
                        locs.append(tuple(float(v) for v in o))
            if locs:
                return locs, 'bsp'
            if data[:4] == b'IBSP':
                return [], ('none: bsp read but no item_health_mega/'
                            'item_armor_body entity in it -- this map has '
                            'no major item, not a missing-data gap')
    entry = (items_file or {}).get(mapname or '')
    if entry:
        return [tuple(float(v) for v in p) for p in entry], 'items-file'
    return [], 'none: no --gamedir BSP read and no --items entry for this map'


def _item_take_events(loc, tracks, labels, radius=ITEM_PICKUP_RADIUS):
    """Probable-take moments (seconds) for one major-item location: a
    maximal run of consecutive-frame samples, from ANY rostered player on
    either team, within `radius` of `loc`, that is followed by a sampled
    frame OUTSIDE `radius` for that same player -- the 'dwelling ... then
    leaving' signal the design brief specifies. t_take is the episode's
    FIRST frame, not its last: the item is claimed on contact, so the
    entry frame is the closer approximation of the true touch instant;
    'leaving' is only used to CONFIRM the episode was a real visit rather
    than a still-in-progress approach the track happens to end during (a
    dangling episode still `in_radius` at the track's last sample is
    dropped, not promoted to a take on a guess).

    Episodes from DIFFERENT players that start within ITEM_TAKE_MERGE_S of
    each other are merged into one take event (earliest start kept) -- a
    contested pickup with two players in the area at once must not read
    as two independent respawn cycles."""
    raw = []
    r2 = radius * radius
    for n in labels:
        in_radius = False
        ep_start = None
        for f, x, y, z, _eff in tracks.get(n, []):
            near = ((x - loc[0]) ** 2 + (y - loc[1]) ** 2
                    + (z - loc[2]) ** 2) <= r2
            if near and not in_radius:
                ep_start = f
            if not near and in_radius and ep_start is not None:
                raw.append(ep_start / F.FPS)
                ep_start = None
            in_radius = near
    raw.sort()
    merged = []
    for t in raw:
        if merged and t - merged[-1] <= ITEM_TAKE_MERGE_S:
            continue
        merged.append(t)
    return merged


def item_pre_respawn_windows(take_times, respawn_s=MAJOR_ITEM_RESPAWN_S,
                             pre_s=ITEM_PRE_RESPAWN_WINDOW_S):
    """[(t0, t1), ...] -- one window per detected take, ending at that
    take's own inferred respawn moment (take_time + respawn_s) and
    starting pre_s seconds before it. ONE window per take, not a repeating
    respawn_s cycle projected forward indefinitely: after a respawn the
    item sits live and unclaimed for however long it takes someone to walk
    back over it, and nothing in a position-only stream says when (or
    whether) that happens -- only the interval this module can actually
    ground in an observed take-then-leave gets a window."""
    return [(t + respawn_s - pre_s, t + respawn_s) for t in take_times]


def major_item_scalars(tracks, teams, members, locations, radius=MAJOR_ITEM_RADIUS):
    """EYE 1 (gates the item-timing dark feature -- see the measurement
    note at the bottom of this file for the exact match/mismatch against
    TRIALS.md's own sg_clockplay entry, which as written models score/
    clock-margin posture, not item timing; this eye was built to the
    brief given to this module, and that naming tension is reported rather
    than papered over).

    Returns (major_item_presence, major_item_presence_dwell_overall,
    n_locations, n_take_events):

    major_item_presence -- the PRIMARY number. For each major-item
    location, build its own pre-respawn windows (item_pre_respawn_windows)
    from take events observed AT THAT LOCATION; within those windows only,
    accumulate per team how many sampled player-frames were within
    `radius` of that item (numerator) against every sampled player-frame
    that fell in one of that item's own windows regardless of distance
    (denominator). Multiple major-item locations are POOLED BY SUMMING
    their counts -- the same pooling compute_scalars already uses for
    escort_fraction's tot/esc across every carry window in a demo,
    regardless of which carrier produced it. Per-team fractions are then
    averaged unweighted into one demo-level number, mirroring how
    compute_scalars' defense_fraction averages defense_frac_overall across
    TEAMS -- consistent treatment for two scalars asking the same shape of
    question ('what fraction of team X's time was spent doing Y').

    major_item_presence_dwell_overall -- the FALLBACK proxy, computed
    unconditionally alongside the primary number rather than only when
    asked for: fraction of a team's TOTAL player-time (the whole capped
    demo, no take/respawn inference at all) spent within `radius` of ANY
    major-item location (a single per-frame minimum-distance check, so a
    player near two items at once is not double-counted the way the
    windowed primary's sum-across-locations pooling would double-count
    it). This is the number the module docstring's escape hatch points to
    if the take/respawn inference above proves too noisy to trust."""
    r2 = radius * radius

    win_num = {t: 0 for t in TEAMS}
    win_den = {t: 0 for t in TEAMS}
    n_take_events = 0
    for loc in locations:
        takes = _item_take_events(loc, tracks, [n for ents in members.values()
                                                for n in ents])
        n_take_events += len(takes)
        windows = item_pre_respawn_windows(takes)
        if not windows:
            continue
        for team, ents in members.items():
            for n in ents:
                for f, x, y, z, _eff in tracks.get(n, []):
                    t_s = f / F.FPS
                    if not any(w0 <= t_s < w1 for w0, w1 in windows):
                        continue
                    win_den[team] += 1
                    d2 = ((x - loc[0]) ** 2 + (y - loc[1]) ** 2
                          + (z - loc[2]) ** 2)
                    if d2 <= r2:
                        win_num[team] += 1

    dwell_num = {t: 0 for t in TEAMS}
    dwell_den = {t: 0 for t in TEAMS}
    if locations:
        for team, ents in members.items():
            for n in ents:
                for f, x, y, z, _eff in tracks.get(n, []):
                    dwell_den[team] += 1
                    best = min((x - lx) ** 2 + (y - ly) ** 2 + (z - lz) ** 2
                              for lx, ly, lz in locations)
                    if best <= r2:
                        dwell_num[team] += 1

    def _avg(num, den):
        fracs = [num[t] / den[t] for t in TEAMS if den[t] > 0]
        return float(np.mean(fracs)) if fracs else None

    presence = _avg(win_num, win_den) if locations else None
    dwell_overall = _avg(dwell_num, dwell_den) if locations else None
    return presence, dwell_overall, len(locations), n_take_events


def postspawn_heading_consistency(tracks, labels, window_s=POSTSPAWN_WINDOW_S,
                                  min_frames=POSTSPAWN_MIN_FRAMES):
    """EYE 2 (gates sg_spawnbeat). Per-demo mean of (net displacement /
    path length) over the first `window_s` seconds after each detected
    respawn, pooled across every rostered player -- 1.0 is a straight
    beeline away from the landing spot, low is doubling back or standing
    and turning ('wander'). Returns (mean or None, n_events).

    Respawn moments are F.death_ticks' own teleport-jump detector (already
    proven by the kinematic strip and classify_outcome, ZERO NEW PARSING
    per this module's own rule) -- the frame where a track's position
    jumps by more than F.TELEPORT_UNITS on a single consecutive-frame step
    is read as the landing frame of a new life, the same convention every
    other consumer of death_ticks in this toolbox uses. This also fires on
    a map-opening spawn this module never saw a preceding death for; that
    life still begins somewhere the player could not have pre-planned a
    heading for, so it is kept rather than filtered on unavailable death
    evidence.

    The window is walked frame-by-frame from the landing frame and CUT
    SHORT at the first non-consecutive frame gap or the first jump beyond
    TELEPORT_UNITS (a life that ends again inside the 3-second window has
    no intact 3-second path to measure) -- whatever was captured before
    the cut still counts, as long as it clears min_frames. A life that
    dies again, or goes unobserved, before min_frames of clean path exist
    is dropped rather than scored on 1-2 samples, where the ratio is close
    to structurally 1.0 or undefined regardless of intent -- the same
    trade window_escort_fraction_obs makes when it drops an unobservable
    frame instead of guessing at it."""
    window_frames = int(round(window_s * F.FPS))
    ratios = []
    for n in labels:
        track = tracks.get(n, [])
        if len(track) < 2:
            continue
        by_frame = {s[0]: s for s in track}
        for land_f in F.death_ticks(track):
            landing = by_frame.get(land_f)
            if landing is None:
                continue
            _f0, sx, sy, _sz, _e0 = landing
            path_len = 0.0
            px, py = sx, sy
            cur_f = land_f
            n_clean = 0
            for _step in range(window_frames):
                nxt = by_frame.get(cur_f + 1)
                if nxt is None:
                    break
                _f1, x1, y1, _z1, _e1 = nxt
                dist = math.hypot(x1 - px, y1 - py)
                if dist > F.TELEPORT_UNITS:
                    break
                path_len += dist
                px, py = x1, y1
                cur_f += 1
                n_clean += 1
            if n_clean < min_frames or path_len <= 0:
                continue
            net = math.hypot(px - sx, py - sy)
            ratios.append(min(net / path_len, 1.0))
    mean = float(np.mean(ratios)) if ratios else None
    return mean, len(ratios)


def defenders_present(tracks, members, stands, radius=DEFENSE_RADIUS):
    """{team: {frame: True}} -- frames at which >=1 of that team's rostered
    players was sampled within `radius` (x/y only -- flag_stands has no z,
    see resolve_stands) of that team's own flag stand."""
    r2 = radius * radius
    out = {}
    for team, ents in members.items():
        sx, sy = stands[team]
        present = {}
        for n in ents:
            for f, x, y, z, _eff in tracks.get(n, []):
                if present.get(f):
                    continue
                dx, dy = x - sx, y - sy
                if dx * dx + dy * dy <= r2:
                    present[f] = True
        out[team] = present
    return out


def bin_binary_fraction(present, frames_total, nbins=TIME_BINS):
    """Fraction of frames in each bin where `present` was True, over the
    FULL frame range 1..frames_total -- a frame nobody was sampled at reads
    as "not defending", the same treatment on both demo shapes (a client
    demo's PVS holes and a serverrecord's pov-parity holes both mean "this
    module cannot see a defender here", which is the honest answer either
    way, not a claim the flag was actually unguarded)."""
    count = np.zeros(nbins)
    total = np.zeros(nbins)
    for f in range(1, frames_total + 1):
        idx = _bin_index(f, frames_total, nbins)
        total[idx] += 1
        if present.get(f):
            count[idx] += 1
    with np.errstate(invalid='ignore', divide='ignore'):
        return np.where(total > 0, count / np.maximum(total, 1), np.nan)


def axis_from_stands(stands):
    """Unit vector from the red stand to the blue stand, and their
    midpoint -- the axis panel 4 projects every player position onto."""
    rx, ry = stands['red']
    bx, by = stands['blue']
    vx, vy = bx - rx, by - ry
    norm = math.hypot(vx, vy) or 1.0
    return vx / norm, vy / norm, (rx + bx) / 2.0, (ry + by) / 2.0


def attacker_counts(tracks, members, stands, frames_total):
    """(red_counts, blue_counts): arrays indexed 1..frames_total (index 0
    unused) of how many of that team's rostered players were, at that
    frame, on the ENEMY side of the red-blue stand axis -- positive
    projection (toward the blue stand) counts as red attacking, negative
    (toward the red stand) counts as blue attacking."""
    ux, uy, mx, my = axis_from_stands(stands)
    red_c = np.zeros(frames_total + 1)
    blue_c = np.zeros(frames_total + 1)
    for team, ents in members.items():
        for n in ents:
            for f, x, y, z, _eff in tracks.get(n, []):
                if f > frames_total:
                    continue
                s = (x - mx) * ux + (y - my) * uy
                if team == 'red' and s > 0:
                    red_c[f] += 1
                elif team == 'blue' and s < 0:
                    blue_c[f] += 1
    return red_c, blue_c


def bin_mean_series(counts, frames_total, nbins=TIME_BINS):
    s = np.zeros(nbins)
    n = np.zeros(nbins)
    for f in range(1, frames_total + 1):
        idx = _bin_index(f, frames_total, nbins)
        s[idx] += counts[f]
        n[idx] += 1
    return s / np.maximum(n, 1)


# ------------------------------------------------------------- the scalars
# APPEND-ONLY (L-diff): new keys are always added at the END of this list,
# never inserted -- --scalars' CSV header is 'demo_shape,map,basename,' +
# ','.join(SCALAR_KEYS), so every existing column's position and value stay
# byte-identical across a diff on the same two demos, and only trailing
# columns appear or change when this list grows.
SCALAR_KEYS = ['spacing_median', 'escort_fraction', 'defense_fraction',
               'mean_simultaneous_attackers', 'escort_fraction_obs',
               'carry_coverage', 'major_item_presence',
               'major_item_presence_dwell_overall', 'postspawn_purpose']
SCALAR_PANEL = {
    'spacing_median': 'panel 1 (spacing)',
    'escort_fraction': 'panel 2 (escort)',
    'defense_fraction': 'panel 3 (defense posture)',
    'mean_simultaneous_attackers': 'panel 4 (push synchronization)',
    'escort_fraction_obs': 'panel 2 (escort, observed-frames-only -- '
                           'not drawn, scalar-only)',
    'carry_coverage': 'panel 2 (escort denominator honesty check -- '
                      'not drawn, scalar-only)',
    'major_item_presence': 'EYE 1 / item-timing dark feature -- pre-'
                           'respawn-window presence (not drawn, '
                           'scalar-only)',
    'major_item_presence_dwell_overall': 'EYE 1 fallback proxy -- dwell-'
                           'near-major-items share, no take/respawn '
                           'inference (not drawn, scalar-only)',
    'postspawn_purpose': 'EYE 2 / sg_spawnbeat -- post-respawn heading '
                         'consistency (not drawn, scalar-only)',
}


def compute_scalars(dist_by_team, windows, defense_frac_overall,
                    red_counts, blue_counts, frames_total,
                    item_presence=None, item_presence_dwell=None,
                    postspawn_purpose=None):
    all_d = [v for team in TEAMS for vals in dist_by_team[team].values()
             for v in vals]
    spacing_median = float(np.median(all_d)) if all_d else None

    tot = sum(w['escort_total'] for w in windows)
    esc = sum(w['escort_escorted'] for w in windows)
    escort_fraction = (esc / tot) if tot else None

    dvals = [v for v in defense_frac_overall.values() if v is not None]
    defense_fraction = float(np.mean(dvals)) if dvals else None

    if frames_total > 0:
        total_series = red_counts[1:frames_total + 1] \
            + blue_counts[1:frames_total + 1]
        mean_simultaneous_attackers = float(np.mean(total_series))
    else:
        mean_simultaneous_attackers = None

    # NEW (see window_escort_fraction_obs): escort_fraction computed over
    # frames where the carrier AND at least one teammate were both sampled
    # -- a frame neither side could have shown an escort in is excluded
    # from both the numerator and the denominator, instead of being read
    # as "not escorted". carry_coverage is the honesty check alongside it:
    # what fraction of each window's nominal (implied) frame span actually
    # produced a scoreable (carrier+teammate observed) frame at all -- so a
    # reader can see directly how much of the escort_fraction_obs
    # denominator survived, and how that differs between demo shapes,
    # rather than having to rediscover the asymmetry.
    tot_obs = sum(w['escort_obs_total'] for w in windows)
    esc_obs = sum(w['escort_obs_escorted'] for w in windows)
    escort_fraction_obs = (esc_obs / tot_obs) if tot_obs else None

    implied_total = sum(w['implied_frames'] for w in windows)
    carry_coverage = (tot_obs / implied_total) if implied_total else None

    return {
        'spacing_median': spacing_median,
        'escort_fraction': escort_fraction,
        'defense_fraction': defense_fraction,
        'mean_simultaneous_attackers': mean_simultaneous_attackers,
        'escort_fraction_obs': escort_fraction_obs,
        'carry_coverage': carry_coverage,
        'major_item_presence': item_presence,
        'major_item_presence_dwell_overall': item_presence_dwell,
        'postspawn_purpose': postspawn_purpose,
    }


def analyze_demo(demo_path, pov_parity=False, pov_ent=None,
                 pov_radius=F.POV_RADIUS_DEFAULT,
                 pov_fov=F.POV_FOV_DEG_DEFAULT, stands_file=None,
                 escort_radius=ESCORT_RADIUS, defense_radius=DEFENSE_RADIUS,
                 gamedir=None, items_file=None):
    """Everything --scalars and the renderer both need, computed once.

    Control flow mirrors F.render_sheet's / routesheet.analyze_demo's /
    fightsheet.analyze_demo's exactly -- refuse, cap, anonymize, parity-
    filter, re-anonymize -- because that ordering was debugged there and
    re-deriving it would be a good way to reintroduce a fixed bug.

    escort_radius/defense_radius default to the sheet's own fixed instrument
    constants and only ever move away from them inside --calibrate's
    +/-100u radius-stability check (run_calibration below); render_team_sheet
    never passes anything but the defaults, so what gets drawn on a PNG is
    never a function of a calibration run (L7/L8 still hold).

    gamedir/items_file feed EYE 1 (major_item_presence) only -- see
    major_item_locations. Neither is drawn on any panel (FAIRNESS RULE),
    so a demo analyzed without either still renders and scores normally;
    major_item_presence and major_item_presence_dwell_overall simply come
    back None (this map's major-item locations are unknown), the same
    "None means unavailable, not zero" convention every other optional
    scalar in this module already follows."""
    d = F.walk_demo(demo_path)
    uncapped = d['frames'] / F.FPS
    if uncapped < F.DURATION_MIN_S:
        raise F.DemoUndersampled(
            f"demo duration {uncapped:.1f}s is under the "
            f"{F.DURATION_MIN_S:.0f}s minimum sample threshold -- too short "
            f"to render reliable stats, skipped rather than producing a "
            f"misleading sheet")
    duration_capped, orig_duration = F.cap_tracks_to_duration(d)

    labels, teams = F.anonymize(d)
    pov_info = {'applied': False}
    if pov_parity:
        if not d['svrecord']:
            pov_info = {'applied': False,
                        'reason': 'not a serverrecord demo -- a client demo '
                                  'is already PVS-filtered by the engine'}
        else:
            pov_info = F.apply_pov_parity(d, labels, pov_ent=pov_ent,
                                          radius=pov_radius, fov_deg=pov_fov)
            labels, teams = F.anonymize(d)

    tracks = d['tracks']
    members = team_members(labels, teams)
    posidx = position_index(tracks, labels)

    windows, n_excluded_carries = F.carry_windows(tracks, labels)
    stands = resolve_stands(d['map'], windows, stands_file)

    for w in windows:
        frac, total, escorted, team = window_escort_fraction(
            w, posidx, teams, labels, radius=escort_radius)
        w['escort_fraction'] = frac
        w['escort_total'] = total
        w['escort_escorted'] = escorted
        w['thief_team'] = team
        w['outcome'] = F.classify_outcome(w, tracks, stands)

        frac_obs, total_obs, escorted_obs, implied_frames, _team_obs = \
            window_escort_fraction_obs(w, posidx, teams, labels,
                                       radius=escort_radius)
        w['escort_fraction_obs'] = frac_obs
        w['escort_obs_total'] = total_obs
        w['escort_obs_escorted'] = escorted_obs
        w['implied_frames'] = implied_frames

    dist_by_team = teammate_pairwise_distances(posidx, members)

    present = defenders_present(tracks, members, stands, radius=defense_radius)
    defense_bins = {t: bin_binary_fraction(present[t], d['frames'])
                    for t in TEAMS}
    defense_frac_overall = {
        t: (len(present[t]) / d['frames']) if d['frames'] else None
        for t in TEAMS}

    red_counts, blue_counts = attacker_counts(tracks, members, stands,
                                              d['frames'])
    red_push_bins = bin_mean_series(red_counts, d['frames'])
    blue_push_bins = bin_mean_series(blue_counts, d['frames'])

    # EYE 1 (major_item_presence) -- see major_item_locations/
    # major_item_scalars. item_locations_source is carried into the
    # sidecar/scalar report only; it is never drawn (FAIRNESS RULE).
    item_locations, item_locations_source = major_item_locations(
        d['map'], gamedir, items_file)
    item_presence, item_presence_dwell, n_item_locs, n_item_takes = \
        major_item_scalars(tracks, teams, members, item_locations)

    # EYE 2 (postspawn_purpose) -- see postspawn_heading_consistency.
    postspawn_purpose, n_postspawn_events = postspawn_heading_consistency(
        tracks, labels)

    scalars = compute_scalars(dist_by_team, windows, defense_frac_overall,
                              red_counts, blue_counts, d['frames'],
                              item_presence=item_presence,
                              item_presence_dwell=item_presence_dwell,
                              postspawn_purpose=postspawn_purpose)
    coverage = F.coverage_stats(tracks, labels, d['frames'])

    return {
        'd': d, 'labels': labels, 'teams': teams, 'tracks': tracks,
        'members': members, 'posidx': posidx, 'windows': windows,
        'n_excluded_carries': n_excluded_carries, 'stands': stands,
        'item_locations': item_locations,
        'item_locations_source': item_locations_source,
        'n_item_locations': n_item_locs, 'n_item_take_events': n_item_takes,
        'n_postspawn_events': n_postspawn_events,
        'dist_by_team': dist_by_team,
        'defense_bins': defense_bins,
        'defense_frac_overall': defense_frac_overall,
        'red_push_bins': red_push_bins, 'blue_push_bins': blue_push_bins,
        'scalars': scalars, 'coverage': coverage, 'pov_parity': pov_info,
        'duration_capped': duration_capped,
        'duration_original_s': orig_duration,
    }


# ==================================================================== draw
def _blank_axes(ax, title, xlabel=None, ylabel=None):
    ax.set_title(title, fontsize=8, pad=3)
    if xlabel:
        ax.set_xlabel(xlabel, fontsize=7)
    if ylabel:
        ax.set_ylabel(ylabel, fontsize=7)
    ax.tick_params(labelsize=6)
    for s in ('top', 'right'):
        ax.spines[s].set_visible(False)


def _norm_t(t_seconds, frames_total):
    dur = frames_total / F.FPS
    return (t_seconds / dur) if dur else 0.0


def draw_spacing(ax, dist_by_team, frames_total):
    """Panel 1 -- pairwise teammate distance, median + IQR band, per team,
    over normalized match time.  A team that plays tight produces a low,
    steady band; a team that spreads out or has stragglers produces a wide
    or high one."""
    _blank_axes(ax, '1. teammate spacing over time (median + IQR band, '
                    'per team)', 'normalized match time',
               'pairwise distance (world units)')
    ax.set_xlim(0, 1)
    ax.set_ylim(0, SPACING_YMAX)
    centers = (np.arange(TIME_BINS) + 0.5) / TIME_BINS
    for team in TEAMS:
        bins = bin_pooled(dist_by_team[team], frames_total)
        med, lo, hi = bin_median_iqr(bins)
        ax.fill_between(centers, np.clip(lo, 0, SPACING_YMAX),
                        np.clip(hi, 0, SPACING_YMAX),
                        color=TEAM_COLOR[team], alpha=0.18, linewidth=0)
        ax.plot(centers, np.clip(med, 0, SPACING_YMAX),
               color=TEAM_COLOR[team], lw=1.3)
    handles = [Line2D([], [], color=TEAM_COLOR[t], lw=1.8, label=t)
               for t in TEAMS]
    ax.legend(handles=handles, fontsize=6.5, ncol=2, frameon=False,
              loc='upper right')


def draw_escort_timeline(ax, windows, teams, frames_total):
    """Panel 2 -- carry windows placed on the match timeline (fixed
    ESCORT_SLOTS rows, chosen by duration, padded and drawn in time order,
    same rule as fightsheet's timeline/disengage panels), coloured by the
    fraction of the window a teammate spent within ESCORT_RADIUS of the
    carrier.  A thin left-edge tick in the carrier's team colour marks whose
    carry it was, so the colour scale itself can stay a single fixed
    ramp shared by both teams."""
    _blank_axes(ax, '2. escort presence: carry windows over time (colour = '
                    'fraction of the window with a teammate within '
                    f'{ESCORT_RADIUS:.0f}u of the carrier)',
               'normalized match time', 'carry window slot')
    ax.set_xlim(0, 1)
    ax.set_ylim(ESCORT_SLOTS, 0)
    ax.set_yticks([])
    cmap = plt.get_cmap('RdYlGn')
    shown = sorted(windows, key=lambda w: -(w['t1'] - w['t0']))[:ESCORT_SLOTS]
    shown = sorted(shown, key=lambda w: w['t0'])
    for i in range(ESCORT_SLOTS):
        ax.axhline(i, color='#e8e8e8', lw=0.5)
        if i >= len(shown):
            continue
        w = shown[i]
        x0 = _norm_t(w['t0'], frames_total)
        x1 = _norm_t(w['t1'], frames_total)
        frac = w['escort_fraction']
        color = cmap(0.0 if frac is None else frac)
        ax.barh(i + 0.5, max(x1 - x0, 0.002), left=x0, height=0.72,
               color=color, edgecolor='#333333', linewidth=0.4, zorder=2)
        team = w.get('thief_team')
        ax.plot([x0, x0], [i + 0.08, i + 0.92],
               color=TEAM_COLOR.get(team), lw=1.8, zorder=3)
    handles = [Line2D([], [], marker='s', ls='none', ms=7,
                      color=cmap(v), label=lbl)
               for v, lbl in ((0.0, '0.0'), (0.5, '0.5'), (1.0, '1.0'))]
    ax.legend(handles=handles, loc='upper center',
              bbox_to_anchor=(0.5, -0.20), ncol=3, fontsize=6.5,
              frameon=False, handlelength=1.4,
              title='escort fraction of window', title_fontsize=6.5)


def draw_escort_aggregate(ax, windows):
    """Companion to panel 2: duration-weighted mean escort fraction per
    team, two fixed bars always both labelled, so the tick-label string set
    is identical whatever this demo's carry mix looked like."""
    _blank_axes(ax, '2b. mean escort fraction (duration-weighted), '
                    'per thief team', None, 'fraction')
    ax.set_ylim(0, 1.0)
    vals = []
    for t in TEAMS:
        tw = [w for w in windows if w.get('thief_team') == t]
        tot = sum(w['escort_total'] for w in tw)
        esc = sum(w['escort_escorted'] for w in tw)
        vals.append((esc / tot) if tot else 0.0)
    xs = np.arange(len(TEAMS))
    ax.bar(xs, vals, color=[TEAM_COLOR[t] for t in TEAMS], width=0.5)
    ax.set_xticks(xs)
    ax.set_xticklabels(TEAMS, fontsize=6.5)


def draw_defense(ax, defense_bins, windows, frames_total):
    """Panel 3 -- fraction of time each team has >=1 player within
    DEFENSE_RADIUS of its own flag stand, over normalized match time, with
    steal ticks (colour = flag colour stolen) and capture ticks (downward
    triangle) along the top edge.  The tick row is always drawn, empty when
    this demo had no events of that kind -- an empty row means nothing
    filled it, the same convention every fixed-slot panel in this toolbox
    uses (L2/L3)."""
    _blank_axes(ax, '3. defense posture: fraction of time with a defender '
                    f'within {DEFENSE_RADIUS:.0f}u of own flag stand',
               'normalized match time', 'fraction')
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1.06)
    ax.set_yticks([0, 0.5, 1.0])
    centers = (np.arange(TIME_BINS) + 0.5) / TIME_BINS
    for t in TEAMS:
        ax.plot(centers, defense_bins[t], color=TEAM_COLOR[t], lw=1.3)
    for w in windows:
        x0 = _norm_t(w['t0'], frames_total)
        ax.plot([x0, x0], [1.00, 1.05], color=TEAM_COLOR[w['color']],
               lw=1.0, alpha=0.8)
        if w.get('outcome') == 'captured':
            x1 = _norm_t(w['t1'], frames_total)
            ax.plot([x1], [1.05], marker='v', ms=4.0,
                   color=TEAM_COLOR[w['color']], ls='none')
    handles = [Line2D([], [], color=TEAM_COLOR[t], lw=1.8, label=t)
               for t in TEAMS]
    handles.append(Line2D([], [], color='#333333', marker='|', ls='none',
                          ms=8, label='steal (colour = flag stolen)'))
    handles.append(Line2D([], [], color='#333333', marker='v', ls='none',
                          ms=4, label='capture'))
    ax.legend(handles=handles, fontsize=6, ncol=4, frameon=False,
              loc='upper center', bbox_to_anchor=(0.5, -0.16),
              handlelength=1.4)


def draw_push(ax, red_bins, blue_bins):
    """Panel 4 -- count of each team's players simultaneously on the enemy
    side of the red-blue flag-stand axis, over normalized match time.  A
    coordinated push reads as a rising, shared plateau; five separate solo
    runs read as noisy, low, and rarely more than 1."""
    _blank_axes(ax, '4. push synchronization: attackers simultaneously in '
                    'enemy half (sign of the red-blue flag-stand axis)',
               'normalized match time', 'attacker count')
    ax.set_xlim(0, 1)
    ax.set_ylim(0, PUSH_YMAX)
    centers = (np.arange(TIME_BINS) + 0.5) / TIME_BINS
    ax.plot(centers, np.clip(red_bins, 0, PUSH_YMAX), color=TEAM_COLOR['red'],
           lw=1.2)
    ax.plot(centers, np.clip(blue_bins, 0, PUSH_YMAX),
           color=TEAM_COLOR['blue'], lw=1.2)
    handles = [Line2D([], [], color=TEAM_COLOR['red'], lw=1.8,
                      label='red attacking (in blue half)'),
               Line2D([], [], color=TEAM_COLOR['blue'], lw=1.8,
                      label='blue attacking (in red half)')]
    ax.legend(handles=handles, fontsize=6.5, ncol=2, frameon=False,
              loc='upper right')


NOTES_TEXT = "\n".join([
    "reading notes (identical on every sheet of this instrument):",
    "  * time on every panel is normalized to the match. No panel shows an absolute duration, a player count, or",
    "    a frame count; every axis ceiling is a fixed instrument constant, chosen once, never fit to this demo.",
    "  * escort, defense and spacing radii (400u / 600u) are fixed thresholds, identical on every sheet.",
    "  * flag stand positions are estimated from this demo's own carry-start positions (or supplied by --stands",
    "    when this demo never had a steal of that colour); they are geometric estimates, not read from game state.",
    "  * panel 2's carry-window rows are a fixed, padded count, chosen by duration and drawn in time order; an",
    "    empty row means nothing filled it. Panel 3's steal/capture ticks are drawn even when a demo has none.",
    "  * a frame with no sampled player near a threshold reads as 'not present' on both demo shapes alike -- a",
    "    client demo's PVS holes and a serverrecord's pov-parity holes are treated identically (L6).",
])


def draw_notes_strip(ax):
    ax.axis('off')
    ax.text(0.005, 1.0, NOTES_TEXT, ha='left', va='top', fontsize=6.4,
            family='monospace', color='#555555', linespacing=1.4,
            transform=ax.transAxes)


# ================================================================== render
def render_team_sheet(demo_path, out_dir, pov_parity=False, pov_ent=None,
                      pov_radius=F.POV_RADIUS_DEFAULT,
                      pov_fov=F.POV_FOV_DEG_DEFAULT, stands_file=None,
                      gamedir=None, items_file=None):
    a = analyze_demo(demo_path, pov_parity=pov_parity, pov_ent=pov_ent,
                     pov_radius=pov_radius, pov_fov=pov_fov,
                     stands_file=stands_file, gamedir=gamedir,
                     items_file=items_file)
    d = a['d']
    labels, teams = a['labels'], a['teams']
    windows = a['windows']
    h = F.hash_demo(demo_path)
    os.makedirs(out_dir, exist_ok=True)

    fig = plt.figure(figsize=FIGSIZE, dpi=FIGDPI)
    gs = fig.add_gridspec(len(ROW_HEIGHTS), GRID_COLS,
                          height_ratios=ROW_HEIGHTS,
                          hspace=0.70, wspace=1.4,
                          top=0.955, bottom=0.030, left=0.07, right=0.97)

    draw_spacing(fig.add_subplot(gs[0, :]), a['dist_by_team'], d['frames'])
    draw_escort_timeline(fig.add_subplot(gs[1, :]), windows, teams,
                         d['frames'])
    draw_escort_aggregate(fig.add_subplot(gs[2, 9:15]), windows)
    draw_defense(fig.add_subplot(gs[3, :]), a['defense_bins'], windows,
                d['frames'])
    draw_push(fig.add_subplot(gs[4, :]), a['red_push_bins'],
             a['blue_push_bins'])
    draw_notes_strip(fig.add_subplot(gs[5, :]))

    # FAIRNESS RULE: map, hash and carry count, nothing else -- the exact
    # set film.py's own caption uses (see film.py's caption comment for the
    # ledger of what each additional field would have cost).
    caption = f"map={d['map'] or '?'}   hash={h}   carries={len(windows)}"
    fig.text(0.5, 0.99, caption, ha='center', fontsize=10, weight='bold')

    png_path = os.path.join(out_dir, f'{h}.png')
    fig.savefig(png_path)
    plt.close(fig)

    sidecar = {
        'hash': h,
        'source_path': os.path.abspath(demo_path),
        'source_basename': os.path.basename(demo_path),
        'map': d['map'],
        'demo_shape': 'serverrecord(bot)' if d['svrecord'] else 'client(human)',
        'sheet': 'team',
        'frames': d['frames'],
        'duration_s': d['frames'] / F.FPS,
        'duration_capped': a['duration_capped'],
        'duration_original_s': a['duration_original_s'],
        'players_rendered': len(labels),
        'entnum_to_label': {str(k): v for k, v in labels.items()},
        'label_to_name': {v: d['skins'].get(k - 1, '?').split('\\')[0]
                          for k, v in labels.items()},
        'label_to_team': {labels[k]: teams[k] for k in labels},
        'pov_parity': a['pov_parity'],
        'coverage': {
            'visible_fraction': a['coverage']['visible_fraction'],
            'max_track_fraction': a['coverage']['max_track_fraction'],
            'median_other_track_fraction': a['coverage']['median_other_fraction'],
            'per_track_fraction': {labels[e]: v for e, v
                                   in a['coverage']['per_track'].items()},
        },
        'stands': {t: list(a['stands'][t]) for t in TEAMS},
        'n_excluded_carries': a['n_excluded_carries'],
        'carry_windows': [
            {'thief_team': w.get('thief_team'), 'color': w['color'],
             't0': w['t0'], 't1': w['t1'],
             'escort_fraction': w['escort_fraction'],
             'escort_fraction_obs': w.get('escort_fraction_obs'),
             'implied_frames': w.get('implied_frames'),
             'outcome': w.get('outcome')}
            for w in windows],
        'defense_frac_overall': a['defense_frac_overall'],
        'scalars': a['scalars'],
        'major_item_locations': {
            'locations': [list(p) for p in a['item_locations']],
            'source': a['item_locations_source'],
            'n_take_events': a['n_item_take_events'],
        },
        'n_postspawn_events': a['n_postspawn_events'],
        'rendered_at': time.strftime('%Y-%m-%dT%H:%M:%S'),
    }
    json_path = os.path.join(out_dir, f'{h}.json')
    with open(json_path, 'w') as f:
        json.dump(sidecar, f, indent=1)

    return {'hash': h, 'map': d['map'], 'svrecord': d['svrecord'],
            'players': len(labels), 'png': png_path, 'json': json_path,
            'pov_parity': a['pov_parity'], 'scalars': a['scalars'],
            'n_carries': len(windows),
            'visible_fraction': a['coverage']['visible_fraction']}


# =================================================================== main
DEFAULT_CACHE = '~/.cache/teamsheet-scalars.json'


def _cache_key(path, pov_parity, radius, fov, stands_path, gamedir=None,
              items_path=None):
    st = os.stat(path)
    if not pov_parity:
        radius = fov = None
    return f"{os.path.abspath(path)}|{st.st_mtime_ns}|{st.st_size}|" \
           f"{int(bool(pov_parity))}|{radius}|{fov}|{stands_path or ''}" \
           f"|{gamedir or ''}|{items_path or ''}|teamsheet-v2"


def load_stands_file(path):
    if not path:
        return None
    with open(path) as f:
        return json.load(f)


def load_items_file(path):
    if not path:
        return None
    with open(path) as f:
        return json.load(f)


# ------------------------------------------------------------- calibration
DEFAULT_HUMAN_GLOB = '~/Games/Quake2/lmctf-hooktest/demos/*.dm2'
DEFAULT_BOT_GLOBS = [
    '~/.local/share/YamagiQ2/lmctf-hooktest/demos/wave39[0-9]*-s03*.dm2',
    '~/.local/share/YamagiQ2/lmctf-hooktest/demos/wave40*-s03*.dm2',
]


def _calib_cache_key(path, pov_parity, escort_radius, defense_radius,
                     stands_path, gamedir=None, items_path=None):
    """Separate from _cache_key (used by --scalars) because --calibrate's
    radius-stability check re-runs the same file at three different
    (escort_radius, defense_radius) pairs and must not collide with, or be
    collided into by, the plain --scalars cache entries for that file."""
    st = os.stat(path)
    return f"{os.path.abspath(path)}|{st.st_mtime_ns}|{st.st_size}|" \
           f"{int(bool(pov_parity))}|{escort_radius}|{defense_radius}|" \
           f"{stands_path or ''}|{gamedir or ''}|{items_path or ''}" \
           f"|teamsheet-calib-v2"


def collect_scalars(paths, pov_parity, escort_radius, defense_radius,
                    stands_file, stands_path, cache, maps=None, label='',
                    gamedir=None, items_file=None, items_path=None):
    """Walk a file list and return [{'path','map','shape', **scalars}].

    Cached on (path, mtime, size, parity flag, escort/defense radius,
    stands file, gamedir, items file) because the gate gets re-run with
    perturbed escort/defense radii and the demo walk is the expensive
    part."""
    rows = []
    for p in paths:
        key = _calib_cache_key(p, pov_parity, escort_radius, defense_radius,
                               stands_path, gamedir=gamedir,
                               items_path=items_path)
        if key in cache:
            row = dict(cache[key])
        else:
            try:
                a = analyze_demo(p, pov_parity=pov_parity,
                                 stands_file=stands_file,
                                 escort_radius=escort_radius,
                                 defense_radius=defense_radius,
                                 gamedir=gamedir, items_file=items_file)
            except (F.DemoUndersampled, StandsMissing) as e:
                cache[key] = {'skip': f'{type(e).__name__}: {e}'}
                continue
            except Exception as e:
                sys.stderr.write(f"FAIL {os.path.basename(p)}: "
                                 f"{type(e).__name__}: {e}\n")
                continue
            row = {'map': a['d']['map'],
                  'shape': 'bot' if a['d']['svrecord'] else 'human',
                  'parity_applied': bool(a['pov_parity'].get('applied')),
                  'visible_fraction': a['coverage']['visible_fraction'],
                  'n_players': len(a['labels']),
                  'n_carries': len(a['windows'])}
            row.update(a['scalars'])
            cache[key] = row
        if row.get('skip'):
            continue
        if maps and row.get('map') not in maps:
            continue
        row = dict(row)
        row['path'] = p
        rows.append(row)
        sys.stderr.write(f"  [{label}] {os.path.basename(p):45s} "
                         f"map={row.get('map')} carries={row.get('n_carries')}\n")
    return rows


def run_calibration(human_paths, bot_paths, escort_radius, defense_radius,
                    stands_file, stands_path, cache, maps=None,
                    gamedir=None, items_file=None, items_path=None):
    """Stage A of the design's two-stage gate, ported from fightsheet.py's
    run_calibration with the same math (RS.roc_auc, same separability
    definition) and the same rationale: the instrument has to prove it has
    power on data where the answer is known before it is allowed to certify
    anything.

    pov-parity is forced ON for the bot arm (L5): without it the bot side is
    an omniscient recording and any separation could be coverage rather than
    behaviour.  Unlike fightsheet.py, the radius knob under test here is not
    the pov-parity radius (left at F.POV_RADIUS_DEFAULT throughout) but
    escort_radius/defense_radius -- the two thresholds panels 2 and 3
    actually draw with -- because those are this sheet's own free
    parameters, the ones a Stage-A run has to show are not doing the
    separating by themselves.

    gamedir/items_file feed EYE 1 only, identically on both arms (same
    map, same BSP, so the item locations an arm gets are never a function
    of demo shape)."""
    hr = collect_scalars(human_paths, False, escort_radius, defense_radius,
                         stands_file, stands_path, cache, maps=maps,
                         label='human', gamedir=gamedir,
                         items_file=items_file, items_path=items_path)
    br = collect_scalars(bot_paths, True, escort_radius, defense_radius,
                         stands_file, stands_path, cache, maps=maps,
                         label='bot', gamedir=gamedir,
                         items_file=items_file, items_path=items_path)
    shared = sorted({r['map'] for r in hr} & {r['map'] for r in br})
    hr = [r for r in hr if r['map'] in shared]
    br = [r for r in br if r['map'] in shared]
    out = {'maps': shared, 'n_human': len(hr), 'n_bot': len(br), 'auc': {},
          'human_paths': [r['path'] for r in hr],
          'bot_paths': [r['path'] for r in br]}
    for k in SCALAR_KEYS:
        hv = [r.get(k) for r in hr]
        bv = [r.get(k) for r in br]
        auc = RS.roc_auc(bv, hv)
        out['auc'][k] = {
            'auc_bot_over_human': auc,
            'separability': (max(auc, 1.0 - auc) if auc is not None else None),
            'n_human': sum(1 for v in hv if v is not None),
            'n_bot': sum(1 for v in bv if v is not None),
            'human_mean': (float(np.mean([v for v in hv if v is not None]))
                          if any(v is not None for v in hv) else None),
            'bot_mean': (float(np.mean([v for v in bv if v is not None]))
                        if any(v is not None for v in bv) else None),
        }
    return out


def print_calibration(res, title='STAGE A'):
    print(f"\n=== {title}: maps={','.join(res['maps']) or '(none shared)'} "
         f"n_human={res['n_human']} n_bot={res['n_bot']} ===")
    print(f"{'scalar':30s} {'human_mean':>11s} {'bot_mean':>11s} "
         f"{'AUC(b>h)':>9s} {'separab.':>9s}  panel")
    best = None
    for k in SCALAR_KEYS:
        a = res['auc'][k]
        hm = '        n/a' if a['human_mean'] is None else f"{a['human_mean']:11.4f}"
        bm = '        n/a' if a['bot_mean'] is None else f"{a['bot_mean']:11.4f}"
        au = '      n/a' if a['auc_bot_over_human'] is None \
            else f"{a['auc_bot_over_human']:9.3f}"
        sp = '      n/a' if a['separability'] is None \
            else f"{a['separability']:9.3f}"
        print(f"{k:30s} {hm} {bm} {au} {sp}  {SCALAR_PANEL[k]}")
        if a['separability'] is not None and (best is None
                                             or a['separability'] > best[1]):
            best = (k, a['separability'], a['auc_bot_over_human'])
    if best:
        direction = 'higher on bots' if best[2] >= 0.5 else 'higher on humans'
        print(f"top separating statistic: {best[0]} "
             f"(separability {best[1]:.3f}, {direction}, "
             f"{SCALAR_PANEL[best[0]]})")
        gate = 'PASS' if best[1] >= 0.85 else 'FAIL'
        print(f"Stage A gate (separability >= 0.85 on at least one "
             f"scalar): {gate}")
    hot = [k for k in SCALAR_KEYS
          if (res['auc'][k]['separability'] or 0) >= 0.95]
    if hot:
        print(f"WARNING: separability >= 0.95 on {', '.join(hot)} -- the "
             f"design requires these be inspected by hand and put through "
             f"the +/-100u escort/defense-radius check before they are "
             f"believed. A near-perfect separator is what an instrument "
             f"leak looks like from the inside.")
    return best, hot


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('demos', nargs='*', help='.dm2 demo files')
    ap.add_argument('--out', help='output directory (required to render)')
    ap.add_argument('--stands', default=None,
                    help='JSON file: mapname -> {"red":[x,y,z],'
                         '"blue":[x,y,z]}, used when a demo\'s own carry '
                         'windows never establish one of the two stands')
    ap.add_argument('--gamedir', default=None,
                    help='Quake2 game directory (maps/<name>.bsp loose or '
                         'in a .pak) EYE 1 (major_item_presence) reads '
                         'item_health_mega/item_armor_body origins from, '
                         'via mapflags.py\'s own BSP entity reader')
    ap.add_argument('--items', default=None,
                    help='JSON file: mapname -> [[x,y,z], ...] major-item '
                         'locations, used when --gamedir is not given or a '
                         'map\'s BSP is unreadable -- NOT used when the '
                         'BSP was read successfully and simply has no '
                         'major-item entity (that is a map fact, not a '
                         'missing-data gap; see major_item_locations)')
    ap.add_argument('--pov-parity', action='store_true',
                    help='serverrecord demos only: keep another player\'s '
                         'sample only when a virtual recorder could '
                         'plausibly have seen it. MANDATORY on every '
                         'serverrecord sheet in a judge set (L5).')
    ap.add_argument('--pov-ent', type=int, default=None)
    ap.add_argument('--pov-radius', type=float, default=F.POV_RADIUS_DEFAULT)
    ap.add_argument('--pov-fov', type=float, default=F.POV_FOV_DEG_DEFAULT)
    ap.add_argument('--scalars', action='store_true',
                    help='render nothing; print one CSV row of scalars per '
                         'demo')
    ap.add_argument('--cache', default=DEFAULT_CACHE,
                    help='parse cache for --scalars, keyed on file path, '
                         'mtime, size and parity/stands settings')
    ap.add_argument('--calibrate', action='store_true',
                    help='Stage A gate: run the instrument over a labeled '
                         'known-set and report ROC AUC per scalar. Renders '
                         'nothing and never writes a label onto any sheet.')
    ap.add_argument('--human', nargs='+', default=[DEFAULT_HUMAN_GLOB],
                    help='globs for the human client-demo arm of --calibrate')
    ap.add_argument('--bot', nargs='+', default=DEFAULT_BOT_GLOBS,
                    help='globs for the serverrecord arm of --calibrate')
    ap.add_argument('--maps', nargs='+', default=None,
                    help='restrict --calibrate to these maps')
    ap.add_argument('--radius-check', action='store_true',
                    help='force the +/-100u escort/defense-radius '
                         'perturbation check (it runs automatically for any '
                         'scalar that reaches 0.95 separability)')
    args = ap.parse_args()

    if args.calibrate:
        stands_file = load_stands_file(args.stands) if args.stands else None
        items_file = load_items_file(args.items) if args.items else None
        cpath = os.path.expanduser(args.cache)
        cache = {}
        if os.path.exists(cpath):
            try:
                cache = json.load(open(cpath))
            except Exception:
                cache = {}
        human = RS._expand(args.human)
        bot = RS._expand(args.bot)
        sys.stderr.write(f"calibrate: {len(human)} human candidate(s), "
                         f"{len(bot)} bot candidate(s)\n")
        res = run_calibration(human, bot, ESCORT_RADIUS, DEFENSE_RADIUS,
                              stands_file, args.stands, cache,
                              maps=args.maps, gamedir=args.gamedir,
                              items_file=items_file, items_path=args.items)
        best, hot = print_calibration(res)
        if args.radius_check or hot:
            if hot and not args.radius_check:
                print(f"\n(running the escort/defense-radius check "
                     f"automatically because {', '.join(hot)} reached "
                     f"0.95 separability)")
            for dr in (-100.0, +100.0):
                er2 = ESCORT_RADIUS + dr
                dr2 = DEFENSE_RADIUS + dr
                alt = run_calibration(human, bot, er2, dr2, stands_file,
                                      args.stands, cache, maps=args.maps,
                                      gamedir=args.gamedir,
                                      items_file=items_file,
                                      items_path=args.items)
                print_calibration(alt, title=f'STAGE A (escort {er2:.0f}u, '
                                             f'defense {dr2:.0f}u)')
                print("  radius-perturbation swing vs baseline:")
                for k in SCALAR_KEYS:
                    a0 = res['auc'][k]['auc_bot_over_human']
                    a1 = alt['auc'][k]['auc_bot_over_human']
                    if a0 is None or a1 is None:
                        continue
                    flag = '  <-- COVERAGE-SENSITIVE' if abs(a1 - a0) > 0.10 \
                        else ''
                    print(f"    {k:30s} {a0:.3f} -> {a1:.3f} "
                         f"(d={a1 - a0:+.3f}){flag}")
        os.makedirs(os.path.dirname(cpath) or '.', exist_ok=True)
        with open(cpath, 'w') as f:
            json.dump(cache, f)
        return

    if not args.demos:
        ap.error('no demos given')

    stands_file = load_stands_file(args.stands) if args.stands else None
    items_file = load_items_file(args.items) if args.items else None

    if args.scalars:
        cpath = os.path.expanduser(args.cache)
        cache = {}
        if os.path.exists(cpath):
            try:
                cache = json.load(open(cpath))
            except Exception:
                cache = {}
        print('demo_shape,map,basename,' + ','.join(SCALAR_KEYS))
        for p in args.demos:
            key = _cache_key(p, args.pov_parity, args.pov_radius,
                             args.pov_fov, args.stands,
                             gamedir=args.gamedir, items_path=args.items)
            if key in cache:
                row = cache[key]
                if row.get('skip'):
                    sys.stderr.write(f"SKIP {os.path.basename(p)}: "
                                     f"{row['skip']} (cached)\n")
                    continue
            else:
                try:
                    a = analyze_demo(p, pov_parity=args.pov_parity,
                                     pov_ent=args.pov_ent,
                                     pov_radius=args.pov_radius,
                                     pov_fov=args.pov_fov,
                                     stands_file=stands_file,
                                     gamedir=args.gamedir,
                                     items_file=items_file)
                except (F.DemoUndersampled, StandsMissing) as e:
                    sys.stderr.write(f"SKIP {os.path.basename(p)}: "
                                     f"{type(e).__name__}: {e}\n")
                    cache[key] = {'skip': f'{type(e).__name__}: {e}'}
                    continue
                except Exception as e:
                    sys.stderr.write(f"FAIL {os.path.basename(p)}: "
                                     f"{type(e).__name__}: {e}\n")
                    continue
                row = {'map': a['d']['map'],
                       'shape': 'bot' if a['d']['svrecord'] else 'human'}
                row.update(a['scalars'])
                cache[key] = row
            shape = row['shape']
            vals = ','.join('' if row[k] is None else f"{row[k]:.6f}"
                            for k in SCALAR_KEYS)
            print(f"{shape},{row.get('map')},{os.path.basename(p)},{vals}")
        os.makedirs(os.path.dirname(cpath) or '.', exist_ok=True)
        with open(cpath, 'w') as f:
            json.dump(cache, f)
        return

    if not args.out:
        ap.error('--out is required to render sheets')

    ok, skipped, failed = [], [], []
    for p in args.demos:
        try:
            res = render_team_sheet(p, args.out, pov_parity=args.pov_parity,
                                    pov_ent=args.pov_ent,
                                    pov_radius=args.pov_radius,
                                    pov_fov=args.pov_fov,
                                    stands_file=stands_file,
                                    gamedir=args.gamedir,
                                    items_file=items_file)
            ok.append(res)
            pov = res['pov_parity']
            if res['svrecord'] and not pov.get('applied'):
                sys.stderr.write(
                    f"WARNING {os.path.basename(p)}: serverrecord demo "
                    f"rendered WITHOUT pov-parity. Leak checklist L5 makes "
                    f"parity mandatory on every bot sheet that enters a "
                    f"judge set; this sheet must not be used in one.\n")
            s = res['scalars']
            def _f(k, fmt='.3f'):
                v = s[k]
                return 'n/a' if v is None else format(v, fmt)
            print(f"OK   {os.path.basename(p)} -> {res['hash']}.png  "
                  f"map={res['map']} "
                  f"{'bot' if res['svrecord'] else 'human'} "
                  f"players={res['players']} carries={res['n_carries']} "
                  f"vis={res['visible_fraction']:.3f} "
                  f"spacing={_f('spacing_median')} "
                  f"escort={_f('escort_fraction')} "
                  f"defense={_f('defense_fraction')} "
                  f"attackers={_f('mean_simultaneous_attackers')} "
                  f"escort_obs={_f('escort_fraction_obs')} "
                  f"carry_cov={_f('carry_coverage')} "
                  f"item_presence={_f('major_item_presence')} "
                  f"item_dwell={_f('major_item_presence_dwell_overall')} "
                  f"postspawn={_f('postspawn_purpose')}")
        except (F.DemoUndersampled, StandsMissing) as e:
            skipped.append((p, str(e)))
            print(f"SKIP {os.path.basename(p)}: {e}")
        except Exception as e:
            failed.append((p, str(e)))
            print(f"FAIL {os.path.basename(p)}: {type(e).__name__}: {e}")

    print(f"\n{len(ok)} sheet(s) written to {args.out}, "
          f"{len(skipped)} skipped, {len(failed)} failed")


if __name__ == '__main__':
    main()
