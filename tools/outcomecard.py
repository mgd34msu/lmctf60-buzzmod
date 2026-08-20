#!/usr/bin/env python3
"""Summarize flag pickups, carries, captures, and match outcomes.

The tool reads the same demo shapes as the film tools and can emit scalar or
card output. Geometric carry-end classification is diagnostic; authoritative
release measurements must reconcile CTF logs and persisted statistics.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import film as F
import routesheet as RS         # roc_auc / _ranks / glob helpers ONLY, same
                                 # reuse pattern fightsheet.py and
                                 # teamsheet.py both make of this module
import teamsheet as TS          # resolve_stands / StandsMissing ONLY --
                                 # rung 4 already solved "where is the flag
                                 # stand" and this module needs the identical
                                 # answer, not a second implementation of it

import numpy as np

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

TEAM_COLOR = F.TEAM_COLOR
TEAMS = ('red', 'blue')

StandsMissing = TS.StandsMissing        # re-exported so main()'s except
                                         # clause reads naturally as this
                                         # module's own exception, even
                                         # though it's teamsheet's class


# ------------------------------------------------------------- constants
CAP_RADIUS = 280.0          # u; identical to F.classify_outcome's own
                             # default -- exposed here as a named constant
                             # (rather than left as an implicit default)
                             # only so --calibrate's radius-stability check
                             # (mirroring teamsheet.py's escort/defense
                             # check) has a free parameter of THIS sheet's
                             # own to perturb. render_outcome_card never
                             # passes anything but this default, so what
                             # gets drawn on a PNG is never a function of a
                             # calibration run (L7/L8 still hold).

# Fixed axis ceilings. Every scale on this sheet is a CONSTANT, never a
# function of this demo's own values (L7/L8) -- placeholders, not
# corpus-calibrated (no Stage-A run has been done for this rung yet); being
# a constant is what makes it safe to use uncalibrated, not its exact value.
CAPS_YMAX = 10.0             # cumulative captures per team; bot mactf06
                             # waves run 0-2 caps/game per the design brief,
                             # this leaves generous headroom for other maps
LEAD_YMAX = 8.0              # |cap differential| ceiling
STEALS_YMAX = 30.0           # cumulative steals per team; bot mactf06 waves
                             # run ~10-15 steals/game, again with headroom
INTERVAL_BINS = 20           # fixed histogram bins over the fixed [0, 1]
                             # normalized-time domain of an inter-capture
                             # interval
INTERVAL_COUNT_YMAX = 8      # fixed ceiling for panel 2's histogram count
                             # axis -- NEVER fit to this demo's own max bin
                             # count (L7/L8); a bar that reaches or exceeds
                             # it is clipped, same convention as every other
                             # fixed-ceiling axis on this sheet

GRID_COLS = 12
ROW_HEIGHTS = [2.3, 1.9, 2.1, 2.1, 1.9]
FIGSIZE = (11, 13.2)
FIGDPI = 140


# ============================================================ analysis
def _norm_t(t_seconds, frames_total):
    dur = frames_total / F.FPS
    return (t_seconds / dur) if dur else 0.0


def label_windows(windows, teams, tracks, stands, cap_radius=CAP_RADIUS):
    """Mutates each window in place with 'thief_team' (the carrying
    player's own team, same lookup teamsheet.py's window_escort_fraction
    uses) and 'outcome' (F.classify_outcome, unmodified -- see module
    docstring: this is the ONE outcome heuristic in the toolbox, reused,
    not re-derived)."""
    for w in windows:
        w['thief_team'] = teams.get(w['entnum'])
        w['outcome'] = F.classify_outcome(w, tracks, stands,
                                          cap_radius=cap_radius)
    return windows


def cumulative_step(times_norm, final_x=1.0):
    """Exact event-driven step function: xs/ys such that
    ax.plot(xs, ys, drawstyle='steps-post') traces (0, 0) -> a unit jump at
    every sorted event time -> a flat tail out to final_x. Capture/steal
    counts are already discrete events, so this is the honest rendering --
    no time-bin smoothing is needed or applied (unlike teamsheet.py's
    continuous position samples, which genuinely need binning)."""
    ts = sorted(times_norm)
    xs = [0.0]
    ys = [0]
    for i, t in enumerate(ts, start=1):
        xs.append(t)
        ys.append(i)
    xs.append(final_x)
    ys.append(len(ts))
    return xs, ys


def lead_series(caps_sorted):
    """caps_sorted: capture windows sorted by t1_norm ascending.

    Returns (xs, ys, lead_changes): an exact step series of the cap
    differential (red count - blue count so far) over normalized time, and
    the count of LEADER IDENTITY flips (0 is a tie, not a leader; a change
    is only counted when the leader actually switches from one team to the
    other, i.e. two ties in a row or a tie-then-same-leader never counts)."""
    red = blue = 0
    xs = [0.0]
    ys = [0]
    lead_changes = 0
    prev_leader = None
    for w in caps_sorted:
        if w['thief_team'] == 'red':
            red += 1
        elif w['thief_team'] == 'blue':
            blue += 1
        lead = red - blue
        xs.append(w['t1_norm'])
        ys.append(lead)
        cur_leader = 'red' if lead > 0 else ('blue' if lead < 0 else None)
        if cur_leader is not None:
            if prev_leader is not None and cur_leader != prev_leader:
                lead_changes += 1
            prev_leader = cur_leader
    xs.append(1.0)
    ys.append(ys[-1] if ys else 0)
    return xs, ys, lead_changes


def conversion_series(windows, caps):
    """Exact step series of the running conversion ratio (captures so far /
    steals so far), recomputed at every steal AND every capture (the
    denominator moves on every steal, the numerator only on a capture) --
    a merged event timeline, not a per-team one, since the ratio is a
    single demo-wide number. ratio is undefined (NaN, not plotted) before
    the first steal. Returns (xs, ys)."""
    events = [(w['t0_norm'], 'steal') for w in windows] + \
             [(w['t1_norm'], 'cap') for w in caps]
    events.sort(key=lambda e: e[0])
    xs = [0.0]
    ys = [np.nan]
    steals = caps_n = 0
    for t, kind in events:
        if kind == 'steal':
            steals += 1
        else:
            caps_n += 1
        ratio = (caps_n / steals) if steals else float('nan')
        xs.append(t)
        ys.append(ratio)
    xs.append(1.0)
    ys.append(ys[-1] if ys else float('nan'))
    return xs, ys


# ------------------------------------------------------------- the scalars
SCALAR_KEYS = ['caps_red', 'caps_blue', 'total_caps', 'first_cap_t_norm',
               'lead_changes', 'steals_total', 'conversion']
SCALAR_PANEL = {
    'caps_red': 'panel 1 (score progression)',
    'caps_blue': 'panel 1 (score progression)',
    'total_caps': 'panel 1 (score progression)',
    'first_cap_t_norm': 'panel 2 (cap timing)',
    'lead_changes': 'panel 3 (momentum)',
    'steals_total': 'panel 4 (pressure balance)',
    'conversion': 'panel 4 (pressure balance)',
}


def compute_scalars(windows, caps):
    caps_red = sum(1 for w in caps if w['thief_team'] == 'red')
    caps_blue = sum(1 for w in caps if w['thief_team'] == 'blue')
    total_caps = caps_red + caps_blue
    first_cap_t_norm = min((w['t1_norm'] for w in caps), default=None)
    caps_sorted = sorted(caps, key=lambda w: w['t1_norm'])
    _, _, lead_changes = lead_series(caps_sorted)
    steals_total = len(windows)
    conversion = (total_caps / steals_total) if steals_total else None
    return {
        'caps_red': caps_red,
        'caps_blue': caps_blue,
        'total_caps': total_caps,
        'first_cap_t_norm': first_cap_t_norm,
        'lead_changes': lead_changes,
        'steals_total': steals_total,
        'conversion': conversion,
    }


def analyze_demo(demo_path, pov_parity=False, pov_ent=None,
                 pov_radius=F.POV_RADIUS_DEFAULT,
                 pov_fov=F.POV_FOV_DEG_DEFAULT, stands_file=None,
                 cap_radius=CAP_RADIUS):
    """Everything --scalars and the renderer both need, computed once.

    Control flow mirrors teamsheet.py's analyze_demo exactly -- refuse, cap,
    anonymize, parity-filter, re-anonymize -- because that ordering was
    debugged there and re-deriving it would be a good way to reintroduce a
    fixed bug."""
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
    windows, n_excluded_carries = F.carry_windows(tracks, labels)
    stands = TS.resolve_stands(d['map'], windows, stands_file)

    label_windows(windows, teams, tracks, stands, cap_radius=cap_radius)
    for w in windows:
        w['t0_norm'] = _norm_t(w['t0'], d['frames'])
        w['t1_norm'] = _norm_t(w['t1'], d['frames'])

    caps = [w for w in windows if w['outcome'] == 'captured']
    scalars = compute_scalars(windows, caps)
    coverage = F.coverage_stats(tracks, labels, d['frames'])

    return {
        'd': d, 'labels': labels, 'teams': teams, 'tracks': tracks,
        'windows': windows, 'caps': caps,
        'n_excluded_carries': n_excluded_carries, 'stands': stands,
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


def draw_score_progression(ax, caps):
    """Panel 1 -- cumulative captures per team, exact step function over
    normalized match time."""
    _blank_axes(ax, '1. score progression: cumulative captures per team',
               'normalized match time', 'cumulative captures')
    ax.set_xlim(0, 1)
    ax.set_ylim(0, CAPS_YMAX)
    for team in TEAMS:
        ts = [w['t1_norm'] for w in caps if w['thief_team'] == team]
        xs, ys = cumulative_step(ts)
        ax.plot(xs, np.clip(ys, 0, CAPS_YMAX), color=TEAM_COLOR[team],
               lw=1.5, drawstyle='steps-post')
    handles = [Line2D([], [], color=TEAM_COLOR[t], lw=1.8, label=t)
               for t in TEAMS]
    ax.legend(handles=handles, fontsize=6.5, ncol=2, frameon=False,
              loc='upper left')


def draw_cap_timing(ax, caps):
    """Panel 2 -- histogram of inter-capture intervals (normalized time,
    pooled across both teams' captures in chronological order) plus a
    vertical tick at each team's own first-capture time. An empty
    histogram (fewer than 2 captures total) is drawn empty, not omitted --
    the same 'nothing filled it' convention every fixed-slot panel in this
    toolbox uses."""
    _blank_axes(ax, '2. cap-timing distribution: inter-capture intervals '
                    '(normalized time) + first-cap markers',
               'interval / normalized match time', 'count')
    ax.set_xlim(0, 1)
    times_sorted = sorted(w['t1_norm'] for w in caps)
    intervals = np.diff(np.asarray(times_sorted)) if len(times_sorted) >= 2 \
        else np.array([])
    edges = np.linspace(0.0, 1.0, INTERVAL_BINS + 1)
    counts, _ = np.histogram(intervals, bins=edges)
    ax.bar(edges[:-1], np.clip(counts, 0, INTERVAL_COUNT_YMAX),
          width=np.diff(edges), align='edge', color='#7f7f7f', alpha=0.55,
          edgecolor='#4d4d4d', linewidth=0.4)
    ax.set_ylim(0, INTERVAL_COUNT_YMAX)
    for team in TEAMS:
        ts = [w['t1_norm'] for w in caps if w['thief_team'] == team]
        if not ts:
            continue
        ax.axvline(min(ts), color=TEAM_COLOR[team], lw=1.6, ls='--')
    handles = [Line2D([], [], color='#7f7f7f', lw=6, alpha=0.55,
                      label='inter-capture interval'),
               Line2D([], [], color=TEAM_COLOR['red'], lw=1.6, ls='--',
                      label='red first cap'),
               Line2D([], [], color=TEAM_COLOR['blue'], lw=1.6, ls='--',
                      label='blue first cap')]
    ax.legend(handles=handles, fontsize=6.5, ncol=1, frameon=False,
              loc='upper right')


def draw_momentum(ax, caps):
    """Panel 3 -- cap differential (red - blue) over normalized time, exact
    step function, with a marker at every leader-identity flip."""
    _blank_axes(ax, '3. momentum: lead (cap differential, red - blue) over '
                    'time, lead changes marked',
               'normalized match time', 'lead (red - blue)')
    ax.set_xlim(0, 1)
    ax.set_ylim(-LEAD_YMAX, LEAD_YMAX)
    ax.axhline(0, color='#999999', lw=0.7, ls=':')
    caps_sorted = sorted(caps, key=lambda w: w['t1_norm'])
    xs, ys, lead_changes = lead_series(caps_sorted)
    ax.plot(xs, np.clip(ys, -LEAD_YMAX, LEAD_YMAX), color='#333333', lw=1.4,
           drawstyle='steps-post')
    prev_leader = None
    for x, y in zip(xs[1:-1], ys[1:-1]):
        cur_leader = 'red' if y > 0 else ('blue' if y < 0 else None)
        if cur_leader is not None and prev_leader is not None \
                and cur_leader != prev_leader:
            ax.plot([x], [np.clip(y, -LEAD_YMAX, LEAD_YMAX)], marker='o',
                   ms=5.5, mfc='none', mec='#333333', mew=1.3, zorder=3)
        if cur_leader is not None:
            prev_leader = cur_leader
    ax.text(0.99, 0.04, f"lead changes: {lead_changes}", ha='right',
           va='bottom', fontsize=6.5, transform=ax.transAxes,
           color='#555555')


def draw_pressure(ax, windows, caps):
    """Panel 4 -- cumulative steals per team over normalized time (exact
    step function, carry-window starts), overlaid on a second axis with the
    running conversion ratio (captures so far / steals so far)."""
    _blank_axes(ax, '4. pressure balance: cumulative steals per team, '
                    'overlaid with running conversion ratio',
               'normalized match time', 'cumulative steals')
    ax.set_xlim(0, 1)
    ax.set_ylim(0, STEALS_YMAX)
    for team in TEAMS:
        ts = [w['t0_norm'] for w in windows if w['thief_team'] == team]
        xs, ys = cumulative_step(ts)
        ax.plot(xs, np.clip(ys, 0, STEALS_YMAX), color=TEAM_COLOR[team],
               lw=1.5, drawstyle='steps-post')
    ax2 = ax.twinx()
    ax2.set_ylim(0, 1.0)
    ax2.tick_params(labelsize=6)
    ax2.set_ylabel('conversion ratio (caps / steals so far)', fontsize=7)
    cx, cy = conversion_series(windows, caps)
    ax2.plot(cx, cy, color='#444444', lw=1.2, ls='--',
            drawstyle='steps-post')
    handles = [Line2D([], [], color=TEAM_COLOR[t], lw=1.8,
                      label=f'{t} steals (cumulative)') for t in TEAMS]
    handles.append(Line2D([], [], color='#444444', lw=1.2, ls='--',
                          label='conversion ratio'))
    ax.legend(handles=handles, fontsize=6.5, ncol=3, frameon=False,
              loc='upper left', bbox_to_anchor=(0.0, -0.16))


NOTES_TEXT = "\n".join([
    "reading notes (identical on every sheet of this instrument):",
    "  * time on every panel is normalized to the match. No panel shows an absolute duration, a player count, or",
    "    a frame count; every axis ceiling is a fixed instrument constant, chosen once, never fit to this demo.",
    "  * a 'steal' is a carry-window start (F.carry_windows); a 'capture' is a carry-window end F.classify_outcome",
    "    labels 'captured' -- the same outcome classifier film.py's route panel and teamsheet.py's defense panel",
    "    use (a carry ending within 280u of the thief's own flag stand). No new outcome heuristic exists here.",
    "  * flag stand positions are estimated from this demo's own carry-start positions (or supplied by --stands",
    "    when this demo never had a steal of that colour); they are geometric estimates, not read from game state.",
    "  * cumulative-count panels (1, 3, 4) are exact event-driven step functions, not time-binned -- capture and",
    "    steal counts are already discrete, so no smoothing is applied or needed.",
    "  * panel 2's histogram and panel 3's lead-change count are drawn/reported even when this demo has zero or",
    "    one capture; an empty histogram or a flat zero-lead line means nothing filled it, not that data is missing.",
])


def draw_notes_strip(ax):
    ax.axis('off')
    ax.text(0.005, 1.0, NOTES_TEXT, ha='left', va='top', fontsize=6.4,
            family='monospace', color='#555555', linespacing=1.4,
            transform=ax.transAxes)


# ================================================================== render
def render_outcome_card(demo_path, out_dir, pov_parity=False, pov_ent=None,
                        pov_radius=F.POV_RADIUS_DEFAULT,
                        pov_fov=F.POV_FOV_DEG_DEFAULT, stands_file=None):
    a = analyze_demo(demo_path, pov_parity=pov_parity, pov_ent=pov_ent,
                     pov_radius=pov_radius, pov_fov=pov_fov,
                     stands_file=stands_file)
    d = a['d']
    labels = a['labels']
    windows, caps = a['windows'], a['caps']
    h = F.hash_demo(demo_path)
    os.makedirs(out_dir, exist_ok=True)

    fig = plt.figure(figsize=FIGSIZE, dpi=FIGDPI)
    gs = fig.add_gridspec(len(ROW_HEIGHTS), GRID_COLS,
                          height_ratios=ROW_HEIGHTS,
                          hspace=0.85, wspace=1.4,
                          top=0.955, bottom=0.030, left=0.08, right=0.94)

    draw_score_progression(fig.add_subplot(gs[0, :]), caps)
    draw_cap_timing(fig.add_subplot(gs[1, :]), caps)
    draw_momentum(fig.add_subplot(gs[2, :]), caps)
    draw_pressure(fig.add_subplot(gs[3, :]), windows, caps)
    draw_notes_strip(fig.add_subplot(gs[4, :]))

    # FAIRNESS RULE: map, hash and carry count, nothing else -- the exact
    # set film.py's own caption uses, mirrored verbatim from teamsheet.py.
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
        'sheet': 'outcome',
        'frames': d['frames'],
        'duration_s': d['frames'] / F.FPS,
        'duration_capped': a['duration_capped'],
        'duration_original_s': a['duration_original_s'],
        'players_rendered': len(labels),
        'entnum_to_label': {str(k): v for k, v in labels.items()},
        'label_to_name': {v: d['skins'].get(k - 1, '?').split('\\')[0]
                          for k, v in labels.items()},
        'pov_parity': a['pov_parity'],
        'coverage': {
            'visible_fraction': a['coverage']['visible_fraction'],
            'max_track_fraction': a['coverage']['max_track_fraction'],
            'median_other_track_fraction': a['coverage']['median_other_fraction'],
        },
        'stands': {t: list(a['stands'][t]) for t in TEAMS},
        'n_excluded_carries': a['n_excluded_carries'],
        'carry_windows': [
            {'thief_team': w.get('thief_team'), 'color': w['color'],
             't0': w['t0'], 't1': w['t1'],
             't0_norm': w['t0_norm'], 't1_norm': w['t1_norm'],
             'outcome': w.get('outcome')}
            for w in windows],
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
DEFAULT_CACHE = '~/.cache/outcomecard-scalars.json'


def _cache_key(path, pov_parity, radius, fov, stands_path):
    st = os.stat(path)
    if not pov_parity:
        radius = fov = None
    return f"{os.path.abspath(path)}|{st.st_mtime_ns}|{st.st_size}|" \
           f"{int(bool(pov_parity))}|{radius}|{fov}|{stands_path or ''}" \
           f"|outcomecard-v1"


def load_stands_file(path):
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


def _calib_cache_key(path, pov_parity, cap_radius, stands_path):
    """Separate from _cache_key (used by --scalars) because --calibrate's
    radius-stability check re-runs the same file at multiple cap_radius
    values and must not collide with, or be collided into by, the plain
    --scalars cache entries for that file."""
    st = os.stat(path)
    return f"{os.path.abspath(path)}|{st.st_mtime_ns}|{st.st_size}|" \
           f"{int(bool(pov_parity))}|{cap_radius}|{stands_path or ''}" \
           f"|outcomecard-calib-v1"


def collect_scalars(paths, pov_parity, cap_radius, stands_file, stands_path,
                    cache, maps=None, label=''):
    """Walk a file list and return [{'path','map','shape', **scalars}].

    Cached on (path, mtime, size, parity flag, cap_radius, stands file)
    because the gate gets re-run with a perturbed cap_radius and the demo
    walk is the expensive part."""
    rows = []
    for p in paths:
        key = _calib_cache_key(p, pov_parity, cap_radius, stands_path)
        if key in cache:
            row = dict(cache[key])
        else:
            try:
                a = analyze_demo(p, pov_parity=pov_parity,
                                 stands_file=stands_file,
                                 cap_radius=cap_radius)
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


def run_calibration(human_paths, bot_paths, cap_radius, stands_file,
                    stands_path, cache, maps=None):
    """calibration of the design's two-stage gate, ported from teamsheet.py's
    run_calibration with the same math (RS.roc_auc, same separability
    definition) and the same rationale: the instrument has to prove it has
    power on data where the answer is known before it is allowed to certify
    anything.

    pov-parity is forced ON for the bot arm (L5): without it the bot side is
    an omniscient recording and any separation could be coverage rather than
    behaviour. The knob under test is cap_radius -- this sheet's one free
    parameter, the threshold F.classify_outcome actually uses to decide
    'captured' -- exactly analogous to teamsheet.py's escort_radius/
    defense_radius check."""
    hr = collect_scalars(human_paths, False, cap_radius, stands_file,
                         stands_path, cache, maps=maps, label='human')
    br = collect_scalars(bot_paths, True, cap_radius, stands_file,
                         stands_path, cache, maps=maps, label='bot')
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


def print_calibration(res, title='CALIBRATION'):
    print(f"\n=== {title}: maps={','.join(res['maps']) or '(none shared)'} "
         f"n_human={res['n_human']} n_bot={res['n_bot']} ===")
    print(f"{'scalar':22s} {'human_mean':>11s} {'bot_mean':>11s} "
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
        print(f"{k:22s} {hm} {bm} {au} {sp}  {SCALAR_PANEL[k]}")
        if a['separability'] is not None and (best is None
                                             or a['separability'] > best[1]):
            best = (k, a['separability'], a['auc_bot_over_human'])
    if best:
        direction = 'higher on bots' if best[2] >= 0.5 else 'higher on humans'
        print(f"top separating statistic: {best[0]} "
             f"(separability {best[1]:.3f}, {direction}, "
             f"{SCALAR_PANEL[best[0]]})")
        gate = 'PASS' if best[1] >= 0.85 else 'FAIL'
        print(f"calibration gate (separability >= 0.85 on at least one "
             f"scalar): {gate}")
    hot = [k for k in SCALAR_KEYS
          if (res['auc'][k]['separability'] or 0) >= 0.95]
    if hot:
        print(f"WARNING: separability >= 0.95 on {', '.join(hot)} -- the "
             f"design requires these be inspected by hand and put through "
             f"the +/-100u cap_radius check before they are believed. A "
             f"near-perfect separator is what an instrument leak looks "
             f"like from the inside.")
    return best, hot


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
    ap.add_argument('--calibrate', action='store_true',
                    help='calibration gate: run the instrument over a labeled '
                         'known-set and report ROC AUC per scalar. Renders '
                         'nothing and never writes a label onto any sheet.')
    ap.add_argument('--human', nargs='+', default=[DEFAULT_HUMAN_GLOB],
                    help='globs for the human client-demo arm of --calibrate')
    ap.add_argument('--bot', nargs='+', default=DEFAULT_BOT_GLOBS,
                    help='globs for the serverrecord arm of --calibrate')
    ap.add_argument('--maps', nargs='+', default=None,
                    help='restrict --calibrate to these maps')
    ap.add_argument('--radius-check', action='store_true',
                    help='force the +/-100u cap_radius perturbation check '
                         '(it runs automatically for any scalar that '
                         'reaches 0.95 separability)')
    args = ap.parse_args()

    if args.calibrate:
        stands_file = load_stands_file(args.stands) if args.stands else None
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
        res = run_calibration(human, bot, CAP_RADIUS, stands_file,
                              args.stands, cache, maps=args.maps)
        best, hot = print_calibration(res)
        if args.radius_check or hot:
            if hot and not args.radius_check:
                print(f"\n(running the cap_radius check automatically "
                     f"because {', '.join(hot)} reached 0.95 separability)")
            for dr in (-100.0, +100.0):
                cr2 = CAP_RADIUS + dr
                alt = run_calibration(human, bot, cr2, stands_file,
                                      args.stands, cache, maps=args.maps)
                print_calibration(alt, title=f'CALIBRATION (cap_radius {cr2:.0f}u)')
                print("  radius-perturbation swing vs baseline:")
                for k in SCALAR_KEYS:
                    a0 = res['auc'][k]['auc_bot_over_human']
                    a1 = alt['auc'][k]['auc_bot_over_human']
                    if a0 is None or a1 is None:
                        continue
                    flag = '  <-- COVERAGE-SENSITIVE' if abs(a1 - a0) > 0.10 \
                        else ''
                    print(f"    {k:22s} {a0:.3f} -> {a1:.3f} "
                         f"(d={a1 - a0:+.3f}){flag}")
        os.makedirs(os.path.dirname(cpath) or '.', exist_ok=True)
        with open(cpath, 'w') as f:
            json.dump(cache, f)
        return

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
            vals = ','.join('' if row[k] is None else
                            (f"{row[k]:.6f}" if isinstance(row[k], float)
                             else str(row[k]))
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
            res = render_outcome_card(p, args.out, pov_parity=args.pov_parity,
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
                  f"caps_red={s['caps_red']} caps_blue={s['caps_blue']} "
                  f"first_cap={_f('first_cap_t_norm')} "
                  f"lead_changes={s['lead_changes']} "
                  f"steals={s['steals_total']} "
                  f"conversion={_f('conversion')}")
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
