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
    not count equally)."""
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
SCALAR_KEYS = ['spacing_median', 'escort_fraction', 'defense_fraction',
               'mean_simultaneous_attackers']
SCALAR_PANEL = {
    'spacing_median': 'panel 1 (spacing)',
    'escort_fraction': 'panel 2 (escort)',
    'defense_fraction': 'panel 3 (defense posture)',
    'mean_simultaneous_attackers': 'panel 4 (push synchronization)',
}


def compute_scalars(dist_by_team, windows, defense_frac_overall,
                    red_counts, blue_counts, frames_total):
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

    return {
        'spacing_median': spacing_median,
        'escort_fraction': escort_fraction,
        'defense_fraction': defense_fraction,
        'mean_simultaneous_attackers': mean_simultaneous_attackers,
    }


def analyze_demo(demo_path, pov_parity=False, pov_ent=None,
                 pov_radius=F.POV_RADIUS_DEFAULT,
                 pov_fov=F.POV_FOV_DEG_DEFAULT, stands_file=None):
    """Everything --scalars and the renderer both need, computed once.

    Control flow mirrors F.render_sheet's / routesheet.analyze_demo's /
    fightsheet.analyze_demo's exactly -- refuse, cap, anonymize, parity-
    filter, re-anonymize -- because that ordering was debugged there and
    re-deriving it would be a good way to reintroduce a fixed bug."""
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
            w, posidx, teams, labels)
        w['escort_fraction'] = frac
        w['escort_total'] = total
        w['escort_escorted'] = escorted
        w['thief_team'] = team
        w['outcome'] = F.classify_outcome(w, tracks, stands)

    dist_by_team = teammate_pairwise_distances(posidx, members)

    present = defenders_present(tracks, members, stands)
    defense_bins = {t: bin_binary_fraction(present[t], d['frames'])
                    for t in TEAMS}
    defense_frac_overall = {
        t: (len(present[t]) / d['frames']) if d['frames'] else None
        for t in TEAMS}

    red_counts, blue_counts = attacker_counts(tracks, members, stands,
                                              d['frames'])
    red_push_bins = bin_mean_series(red_counts, d['frames'])
    blue_push_bins = bin_mean_series(blue_counts, d['frames'])

    scalars = compute_scalars(dist_by_team, windows, defense_frac_overall,
                              red_counts, blue_counts, d['frames'])
    coverage = F.coverage_stats(tracks, labels, d['frames'])

    return {
        'd': d, 'labels': labels, 'teams': teams, 'tracks': tracks,
        'members': members, 'posidx': posidx, 'windows': windows,
        'n_excluded_carries': n_excluded_carries, 'stands': stands,
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
                      pov_fov=F.POV_FOV_DEG_DEFAULT, stands_file=None):
    a = analyze_demo(demo_path, pov_parity=pov_parity, pov_ent=pov_ent,
                     pov_radius=pov_radius, pov_fov=pov_fov,
                     stands_file=stands_file)
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
             'outcome': w.get('outcome')}
            for w in windows],
        'defense_frac_overall': a['defense_frac_overall'],
        'scalars': a['scalars'],
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


def _cache_key(path, pov_parity, radius, fov, stands_path):
    st = os.stat(path)
    if not pov_parity:
        radius = fov = None
    return f"{os.path.abspath(path)}|{st.st_mtime_ns}|{st.st_size}|" \
           f"{int(bool(pov_parity))}|{radius}|{fov}|{stands_path or ''}" \
           f"|teamsheet-v1"


def load_stands_file(path):
    if not path:
        return None
    with open(path) as f:
        return json.load(f)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('demos', nargs='*', help='.dm2 demo files')
    ap.add_argument('--out', help='output directory (required to render)')
    ap.add_argument('--stands', default=None,
                    help='JSON file: mapname -> {"red":[x,y,z],'
                         '"blue":[x,y,z]}, used when a demo\'s own carry '
                         'windows never establish one of the two stands')
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
    args = ap.parse_args()

    if not args.demos:
        ap.error('no demos given')

    stands_file = load_stands_file(args.stands) if args.stands else None

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
                             args.pov_fov, args.stands)
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
                                     stands_file=stands_file)
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
                                    stands_file=stands_file)
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
                  f"attackers={_f('mean_simultaneous_attackers')}")
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
