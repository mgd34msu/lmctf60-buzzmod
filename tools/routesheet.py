#!/usr/bin/env python3
"""Render comparable route-choice sheets from LMCTF demos.

The tool derives movement occupancy and transition scalars from the shared demo
walker and authenticated RUNE geometry. Optional POV parity applies the client
visibility model to serverrecord tracks. PNG output is blind; JSON sidecars
retain source and extraction metadata.
"""
import argparse
import collections
import glob as globmod
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import film as F

import numpy as np

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
from matplotlib.collections import LineCollection


# ------------------------------------------------------------- constants
ROUTE_NODE_TARGET = 28       # nodes in the per-map graph
ROUTE_CELL_TARGET = 3.0      # occupied seed-cells per node before clustering
ROUTE_KMEANS_ITERS = 12      # Lloyd iterations (deterministic init, no RNG)
ROUTE_DWELL_MIN_S = 0.8      # a node visit shorter than this is boundary chatter
ROUTE_MIN_TRANSITIONS = 60   # per player, below which no entropy bar is drawn
ROUTE_MAX_PLAYER_ROWS = 10   # FIXED, padded -- roster size must not leak (L3)
OFFGRAPH_RADIUS = 96.0       # u from the nearest rune seed
EDGE_P_MIN = 0.05            # transition probability below which no edge drawn
REVISIT_CAP = 12             # node-steps, histogram right edge
NODE_SET_VERSION = 2

GRID_COLS = 6
ROW_HEIGHTS = [3.6, 2.0, 1.6, 1.6, 1.6, 1.4]
FIGSIZE = (12, 16.0)
FIGDPI = 140

# Fixed axis ceilings.  Every scale on this sheet is a CONSTANT -- never a
# function of the demo's own values -- so content can never change
# units-per-pixel between two sheets (L7/L8).  The theoretical ceilings for
# the two bit-valued panels are log2(28) = 4.81, but no observed value comes
# near that and using it would squash every real bar into the bottom fifth of
# the panel, so the drawn ceiling is a lower constant with headroom over the
# observed range.  A bar above its ceiling is CLIPPED to the ceiling and the
# fact that clipping can happen is stated in the always-present notes strip --
# never in a note that appears only on the sheets where it happened (L2).
ENTROPY_YMAX = 3.0      # observed range across both corpora: 1.4 - 2.0 bits
KL_YMAX = 2.0           # observed range: 0.2 - 0.8 bits
OFFGRAPH_YMAX = 0.25    # observed range: 0.01 - 0.12
REVISIT_YMAX = 0.8      # the capped right-hand bin carries the most mass

TEAM_COLOR = F.TEAM_COLOR
NEUTRAL_BAR = '#7f7f7f'

# Per-player panels are drawn in ONE neutral colour on purpose.  Colouring the
# ten fixed slots by team would make the red/blue split of the top-10 readable
# off the panel, which is a roster-composition tell (bot waves are exactly 5v5,
# pub rosters are not) of the instrument's own making -- exactly the class of
# leak L3 exists to stop.  Team-resolved quantities live in the two-slot
# per-team panels instead, where the slot count is a constant.


class RouteFixtureMissing(Exception):
    """No rune file for this demo's map, so no node graph can be built.

    Raised (and reported as SKIP, like F.DemoUndersampled) rather than falling
    back to a demo-derived node set: nodes fitted to this demo's own point
    cloud would make the axes a function of the demo, which is the leak L8
    exists to prevent, and would silently make two sheets of the same map
    incomparable."""


# ------------------------------------------------------ per-map node fixture
def _occupied_cells(seeds_xy, xmin, ymin, cell):
    """{(ix, iy): seed_count} for the seed cloud on a `cell`-sized grid."""
    ix = np.floor((seeds_xy[:, 0] - xmin) / cell).astype(np.int64)
    iy = np.floor((seeds_xy[:, 1] - ymin) / cell).astype(np.int64)
    keys, counts = np.unique(np.stack([ix, iy], axis=1), axis=0,
                             return_counts=True)
    return keys, counts


def build_node_set(seeds, extent, target=ROUTE_NODE_TARGET):
    """Quantize the RUNE seed cloud into a deterministic weighted node set."""
    pts = np.asarray(seeds, dtype=np.float64)
    if len(pts) == 0:
        raise RouteFixtureMissing("rune file carries no seeds")
    xmin, xmax, ymin, ymax = extent
    span = max(xmax - xmin, ymax - ymin)

    want = target * ROUTE_CELL_TARGET
    lo, hi = 1.0, max(span, 2.0)
    cell = hi
    for _ in range(60):
        mid = 0.5 * (lo + hi)
        keys, _counts = _occupied_cells(pts, xmin, ymin, mid)
        n_occ = len(keys)
        if n_occ > want:
            lo = mid          # too many cells -> grow the cell size
        else:
            hi = mid
        cell = mid
    cell = 0.5 * (lo + hi)

    keys, counts = _occupied_cells(pts, xmin, ymin, cell)
    centers = np.stack([xmin + (keys[:, 0] + 0.5) * cell,
                        ymin + (keys[:, 1] + 0.5) * cell], axis=1)
    w = counts.astype(np.float64)

    k = int(min(target, len(centers)))
    order = np.lexsort((centers[:, 1], centers[:, 0]))
    ordered = centers[order]
    stride = max(1, len(ordered) // k)
    init_idx = [min(i * stride, len(ordered) - 1) for i in range(k)]
    cent = ordered[init_idx].copy()

    for _ in range(ROUTE_KMEANS_ITERS):
        d2 = ((centers[:, None, :] - cent[None, :, :]) ** 2).sum(axis=2)
        lab = d2.argmin(axis=1)
        for j in range(k):
            m = lab == j
            if not m.any():
                continue          # empty cluster keeps its previous centroid
            wj = w[m]
            cent[j] = (centers[m] * wj[:, None]).sum(axis=0) / wj.sum()

    nodes = [(round(float(c[0]), 3), round(float(c[1]), 3)) for c in cent]
    return nodes, float(cell)


def map_frame(nodes):
    """The map's own (along, across) frame: (origin, v, w).

    `origin` is the node centroid, `v` the leading eigenvector of the node
    coordinates' covariance (the map's long axis), `w` its perpendicular.  A
    function of the node set alone, hence of the map alone -- which is what
    makes it safe to draw on: every sheet of a given map is rendered in the
    identical frame, so the frame cannot encode which demo produced the sheet
    (L8).  It deliberately does NOT use flag stands, spawn points or anything
    else derived from a demo's events."""
    a = np.asarray(nodes, dtype=np.float64)
    origin = a.mean(axis=0)
    c = a - origin
    cov = (c.T @ c) / max(1, len(c))
    vals, vecs = np.linalg.eigh(cov)
    v = vecs[:, int(np.argmax(vals))]
    # Sign convention so the eigenvector's arbitrary polarity cannot flip the
    # frame (and with it the node ordering) between runs of numpy.
    if v[0] < 0 or (v[0] == 0.0 and v[1] < 0):
        v = -v
    w = np.array([-v[1], v[0]])
    return origin, v, w


def to_map_frame(pts, origin, v, w):
    """Rotate world (x, y) into the map's (along, across) frame.

    Purely cosmetic and purely map-derived: it puts the map's long axis
    horizontal so the route graph fills its full-width row instead of being
    squeezed into a narrow column by the equal-aspect constraint.  Because the
    transform comes from the node set, every sheet of a map gets the same one."""
    a = np.asarray(pts, dtype=np.float64)
    if a.size == 0:
        return a.reshape(0, 2)
    c = a - origin
    return np.stack([c @ v, c @ w], axis=1)


def canonical_node_order(nodes):
    """Index permutation putting the nodes in principal-axis order.

    This is what makes cell (i, j) of the transition matrix mean the same
    thing on every sheet of a given map."""
    origin, v, w = map_frame(nodes)
    m = to_map_frame(nodes, origin, v, w)
    return [int(i) for i in np.lexsort((m[:, 1], m[:, 0]))]


def frame_extent(seeds, origin, v, w, pad_frac=0.04):
    """The drawing extent in the map frame -- the same bounding-box-plus-pad
    rule F.compute_seed_extent uses, applied to the rotated seed cloud, so the
    extent stays a property of the rune file and never of the demo."""
    m = to_map_frame(seeds, origin, v, w)
    xmin, xmax = float(m[:, 0].min()), float(m[:, 0].max())
    ymin, ymax = float(m[:, 1].min()), float(m[:, 1].max())
    dx = (xmax - xmin) or 100.0
    dy = (ymax - ymin) or 100.0
    return (xmin - dx * pad_frac, xmax + dx * pad_frac,
            ymin - dy * pad_frac, ymax + dy * pad_frac)


def load_or_build_nodes(rune_dir, mapname, rebuild=False):
    """The shared per-map fixture, cached at <runedir>/<map>.nodes.json.

    Rung 4 (TEAM DECISIONS) later adds a 'halves' key to this same file; the
    'version' field exists so that changing the node construction invalidates
    old sheets loudly instead of silently mixing two node sets in one judge
    set."""
    if not mapname:
        raise RouteFixtureMissing("map name not recovered from demo")
    rune_path = F.find_rune(rune_dir, mapname)
    if not rune_path:
        raise RouteFixtureMissing(f"no rune found for map={mapname}")
    cache_path = os.path.join(os.path.dirname(rune_path),
                              f'{mapname}.nodes.json')
    if not rebuild and os.path.exists(cache_path):
        try:
            with open(cache_path) as f:
                fx = json.load(f)
            if fx.get('version') == NODE_SET_VERSION and \
                    len(fx.get('nodes', [])) > 0:
                fx['nodes'] = [tuple(n) for n in fx['nodes']]
                fx['seeds'] = F.load_rune_seeds(rune_path)
                return fx
        except Exception:
            pass
    seeds = F.load_rune_seeds(rune_path)
    extent = F.compute_seed_extent(seeds)
    if extent is None:
        raise RouteFixtureMissing(f"rune for map={mapname} carries no seeds")
    nodes, cell = build_node_set(seeds, extent)
    origin, v, w = map_frame(nodes)
    fx = {
        'version': NODE_SET_VERSION,
        'map': mapname,
        'nodes': [list(n) for n in nodes],
        'order': canonical_node_order(nodes),
        'cell_size': cell,
        'extent': list(extent),
        'frame_origin': [float(origin[0]), float(origin[1])],
        'frame_axis': [float(v[0]), float(v[1])],
        'frame_extent': list(frame_extent(seeds, origin, v, w)),
        'n_seeds': len(seeds),
        'built_at': time.strftime('%Y-%m-%dT%H:%M:%S'),
    }
    with open(cache_path, 'w') as f:
        json.dump(fx, f, indent=1)
    fx['nodes'] = [tuple(n) for n in nodes]
    fx['seeds'] = seeds
    return fx


# ------------------------------------------------------------- assignment
def _chunked_argmin(pts, ref, chunk=4000):
    """Nearest row of `ref` for every row of `pts`, brute force in chunks --
    the same shape of computation (and the same reason for chunking) as
    F.nearest_seed_counts."""
    out = np.empty(len(pts), dtype=np.int64)
    for s in range(0, len(pts), chunk):
        b = pts[s:s + chunk]
        d2 = ((b[:, None, :] - ref[None, :, :]) ** 2).sum(axis=2)
        out[s:s + len(b)] = d2.argmin(axis=1)
    return out


def _chunked_min_dist(pts, ref, chunk=1000):
    """Distance from every row of `pts` to its nearest row of `ref`."""
    out = np.empty(len(pts), dtype=np.float64)
    for s in range(0, len(pts), chunk):
        b = pts[s:s + chunk]
        d2 = ((b[:, None, :] - ref[None, :, :]) ** 2).sum(axis=2)
        out[s:s + len(b)] = np.sqrt(d2.min(axis=1))
    return out


def assign_nodes(tracks, labels, nodes):
    """{entnum: [(frame, node_index), ...]} -- nearest-node assignment per
    position sample, x/y only (the map graph is planar; z separates floors of
    the same stairwell and would fragment a single lane into two nodes)."""
    ref = np.asarray(nodes, dtype=np.float64)
    out = {}
    for n in sorted(labels):
        t = tracks.get(n, [])
        if not t:
            out[n] = []
            continue
        pts = np.asarray([(s[1], s[2]) for s in t], dtype=np.float64)
        idx = _chunked_argmin(pts, ref)
        out[n] = [(t[i][0], int(idx[i])) for i in range(len(t))]
    return out


def offgraph_fraction(tracks, labels, seeds, radius=OFFGRAPH_RADIUS):
    """{entnum: fraction of that player's samples farther than `radius` from
    any rune seed}.

    The rune seed cloud approximates the map's walkable floor as the bot
    navigation build understands it.  Samples far from every seed are places
    the route graph does not cover: wall-hugging, ledge-riding, hook swings
    over open space, and paths through geometry the seed cloud never reached.
    A fraction, never a count, so demo length and PVS coverage divide out
    (L1/L4)."""
    ref = np.asarray(seeds, dtype=np.float64)
    out = {}
    for n in sorted(labels):
        t = tracks.get(n, [])
        if not t:
            out[n] = None
            continue
        pts = np.asarray([(s[1], s[2]) for s in t], dtype=np.float64)
        # Distinct positions only, then weighted back by how often each
        # occurred.  This protocol transmits origins in 1/8-unit steps, and a
        # player who holds a position re-sends the identical coordinate every
        # frame, so the distinct-position count is a large fraction smaller
        # than the sample count on every demo of both shapes.  Exactly the
        # same answer as measuring every sample; several times less work.
        uniq, inv = np.unique(pts, axis=0, return_inverse=True)
        w = np.bincount(inv.ravel(), minlength=len(uniq)).astype(np.float64)
        dist = _chunked_min_dist(uniq, ref)
        out[n] = float(w[dist > radius].sum() / w.sum())
    return out


def node_sequences(assign, dwell_min_s=ROUTE_DWELL_MIN_S):
    """Collapse sampled node tracks into dwell-filtered visit sequences."""
    seqs, visits = {}, {}
    for n, series in assign.items():
        runs = []
        for f, nd in series:
            if runs and runs[-1][0] == nd:
                runs[-1][1] += 1
            else:
                runs.append([nd, 1])
        min_samples = max(1, int(round(dwell_min_s * F.FPS)))
        kept = [r for r in runs if r[1] >= min_samples]
        seq = []
        for nd, _c in kept:
            if not seq or seq[-1] != nd:
                seq.append(nd)
        seqs[n] = seq
        visits[n] = collections.Counter(seq)
    return seqs, visits


def transition_matrix(seqs, n_nodes, ents=None):
    """(P, counts) over the pooled node sequences of `ents`.

    P is row-normalized with all-zero rows left as zero.  ONLY P reaches the
    PNG: raw transition counts scale with demo length and with PVS coverage,
    so drawing them would put a coverage read on the sheet (L4).  counts goes
    to the sidecar, which is unblinding material by definition."""
    counts = np.zeros((n_nodes, n_nodes), dtype=np.float64)
    for n, seq in seqs.items():
        if ents is not None and n not in ents:
            continue
        for i in range(len(seq) - 1):
            counts[seq[i], seq[i + 1]] += 1.0
    rs = counts.sum(axis=1, keepdims=True)
    P = np.divide(counts, rs, out=np.zeros_like(counts), where=rs > 0)
    return P, counts


def transition_entropy(seqs, n_nodes, min_transitions=ROUTE_MIN_TRANSITIONS):
    """Return per-player conditional next-node entropy and transition counts."""
    ent, ntr = {}, {}
    for n, seq in seqs.items():
        P, counts = transition_matrix({n: seq}, n_nodes)
        total = counts.sum()
        ntr[n] = int(total)
        if total < min_transitions:
            ent[n] = None
            continue
        pi = counts.sum(axis=1) / total
        logp = np.zeros_like(P)
        np.log2(P, out=logp, where=P > 0)
        rowH = -(P * logp).sum(axis=1)
        ent[n] = float((pi * rowH).sum())
    return ent, ntr


def revisit_intervals(seqs, cap=REVISIT_CAP, ents=None):
    """Pooled revisit-interval density: for each occurrence of a node in a
    player's sequence, how many node-steps pass before that node is next
    visited, clipped at `cap`.

    Because consecutive repeats are already collapsed, the smallest possible
    interval is 2 -- so mass at 2 is literally "this player bounced between
    two waypoints".  Returned as a density (sums to 1 over bins 2..cap), never
    as counts, so it does not read demo length (L4).

    Returns (bins: np.ndarray, density: np.ndarray)."""
    bins = np.arange(2, cap + 1)
    hist = np.zeros(len(bins), dtype=np.float64)
    for n, seq in seqs.items():
        if ents is not None and n not in ents:
            continue
        last = {}
        for i, nd in enumerate(seq):
            if nd in last:
                gap = min(i - last[nd], cap)
                if gap >= 2:
                    hist[gap - 2] += 1.0
            last[nd] = i
    tot = hist.sum()
    dens = hist / tot if tot > 0 else hist
    return bins, dens


def occupancy_kl(visits, n_nodes, ents=None):
    """Measure visit-distribution divergence from uniform node occupancy."""
    c = np.zeros(n_nodes, dtype=np.float64)
    for n, ctr in visits.items():
        if ents is not None and n not in ents:
            continue
        for nd, k in ctr.items():
            c[nd] += k
    tot = c.sum()
    if tot <= 0:
        return None
    p = c / tot
    nz = p > 0
    return float((p[nz] * np.log2(p[nz] * n_nodes)).sum())


def edge_density(P, p_min=EDGE_P_MIN):
    """Share of the n_nodes^2 possible transitions whose probability clears
    the drawing threshold -- i.e. how many distinct routes the team actually
    runs, as it appears on panels 1 and 2.  The scalar a judge reads off the
    route graph."""
    n = P.shape[0]
    return float((P >= p_min).sum()) / float(n * n)


# Rung-2 judges repeatedly convicted a wave on "saturated p=1.0 cells in the
# transition matrix" -- at some node, a bot always makes the identical next
# choice.  mean_route_entropy_bits cannot show this: it is pi-weighted over
# EVERY node (transition_entropy's `rowH` term), so a handful of busy,
# perfectly deterministic nodes get averaged against the rest of the graph
# and the aggregate can sit anywhere.  That is exactly what happened to the
# route-dither retry -- entropy moved the wrong direction while the
# cell-level determinism a judge actually reads off panel 2 was never
# measured on its own.  max_transition_mass is that missing cell-level eye.
def max_transition_mass(counts, min_transitions=8):
    """Measure transition-weighted next-node determinism at qualified nodes."""
    rs = counts.sum(axis=1)
    qual = rs >= min_transitions
    if not qual.any():
        return None
    max_p = counts[qual].max(axis=1) / rs[qual]
    return float(np.average(max_p, weights=rs[qual]))


# ------------------------------------------------------------------- draw
def draw_route_graph(ax, seeds, nodes, P_by_team, visits_by_team, fixture):
    """Panel 1: the node graph over the map silhouette, one edge set per team.

    Nodes are unfilled rings (one ring per team, concentric at the same node
    centre) sized by sqrt(visit share) so area does not exaggerate; edges are
    drawn with alpha proportional to transition probability and suppressed
    below EDGE_P_MIN.  Reads at a glance as "which routes does this team
    actually use, and how many of them are there".

    Drawn in the map's own (along, across) frame (map_frame) so the map's long
    axis is horizontal and the graph fills this full-width row.  The frame,
    like the nodes and the extent, comes from the rune file."""
    origin = np.asarray(fixture['frame_origin'], dtype=np.float64)
    v = np.asarray(fixture['frame_axis'], dtype=np.float64)
    w = np.array([-v[1], v[0]])
    extent = tuple(fixture['frame_extent'])
    F.draw_map_silhouette(ax, [tuple(p) for p in
                               to_map_frame(seeds, origin, v, w)], extent)
    nd = to_map_frame(nodes, origin, v, w)
    for team in ('red', 'blue'):
        P = P_by_team.get(team)
        col = TEAM_COLOR[team]
        if P is not None:
            segs, cols = [], []
            r, g, b = mcolors.to_rgb(col)
            for i in range(len(nd)):
                for j in range(len(nd)):
                    if i == j or P[i, j] < EDGE_P_MIN:
                        continue
                    segs.append([(nd[i, 0], nd[i, 1]), (nd[j, 0], nd[j, 1])])
                    a = 0.10 + 0.75 * min(1.0, float(P[i, j]))
                    cols.append((r, g, b, a))
            if segs:
                ax.add_collection(LineCollection(segs, colors=cols,
                                                 linewidths=1.3, zorder=3))
        vis = visits_by_team.get(team)
        if vis is not None and vis.sum() > 0:
            share = vis / vis.sum()
            size = 40.0 + 900.0 * np.sqrt(share)
            ax.scatter(nd[:, 0], nd[:, 1], s=size, facecolors='none',
                       edgecolors=col, linewidths=1.2, zorder=4)
    if extent:
        ax.set_xlim(extent[0], extent[1])
        ax.set_ylim(extent[2], extent[3])
    ax.set_aspect('equal', adjustable='box')
    ax.set_xticks([]); ax.set_yticks([])
    ax.set_title('route graph over map nodes  '
                 '(ring size = visit share, edge alpha = transition '
                 'probability, edges below p=0.05 hidden)', fontsize=9)


def draw_transition_matrix(ax, P, order, team):
    """Panel 2: row-normalized transition matrix in canonical node order.

    Row-normalized on purpose: raw counts differ between two sheets for
    coverage reasons as much as for behavioural ones (L4).  A team that always
    runs the same two lanes shows bright isolated cells against dark rows; a
    team that improvises shows its mass spread along each row."""
    M = P[np.ix_(order, order)]
    ax.imshow(M, origin='lower', cmap=F._truncated_cmap('Greys', lo=0.0),
              vmin=0.0, vmax=1.0, interpolation='nearest', aspect='equal')
    ax.set_xticks([0, len(order) - 1]); ax.set_xticklabels(['0', str(len(order) - 1)])
    ax.set_yticks([0, len(order) - 1]); ax.set_yticklabels(['0', str(len(order) - 1)])
    ax.set_xlabel('next node (canonical order)', fontsize=7)
    ax.set_ylabel('current node', fontsize=7)
    ax.tick_params(labelsize=7)
    ax.set_title(f'{team} team transition matrix P(next | current)', fontsize=8)


def _fixed_bar_panel(ax, values, k, ymax, title, ylabel):
    """The shared fixed-slot bar renderer for every per-player panel.

    ALWAYS draws exactly k slots.  Values are sorted descending and padded
    with empty slots, so the panel's geometry is a constant and neither the
    roster size nor which player is which can be read off it (L3/L7).  Empty
    slots are drawn as a visible baseline tick rather than omitted, so a
    reader can see that the slot exists and is unfilled."""
    vals = sorted([v for v in values if v is not None], reverse=True)[:k]
    vals = [min(v, ymax) for v in vals]
    xs = np.arange(k)
    ax.bar(xs[:len(vals)], vals, width=0.7, color=NEUTRAL_BAR)
    for i in range(len(vals), k):
        ax.plot([i - 0.25, i + 0.25], [0, 0], color='#bbbbbb', lw=1.2)
    ax.set_xlim(-0.7, k - 0.3)
    ax.set_ylim(0, ymax)
    ax.set_xticks(xs)
    ax.set_xticklabels([str(i + 1) for i in range(k)], fontsize=7)
    ax.set_xlabel('player slot (sorted, fixed count, padded)', fontsize=7)
    ax.set_ylabel(ylabel, fontsize=7)
    ax.tick_params(labelsize=7)
    ax.grid(axis='y', alpha=0.25, lw=0.5)
    ax.set_title(title, fontsize=8)


def draw_entropy_bars(ax, ent_by_ent, k=ROUTE_MAX_PLAYER_ROWS):
    """Panel 3: per-player conditional route entropy H(next | current).

    The game-scale generalization of rung 1's carry-route entropy -- and
    unlike that number, this one is on a comparable scale across two sheets of
    the same map, because both are computed over the identical node set.  The
    ceiling is log2(28), i.e. "picks uniformly among all nodes"."""
    _fixed_bar_panel(ax, list(ent_by_ent.values()), k, ENTROPY_YMAX,
                     'per-player conditional route entropy  '
                     'H(next node | current node)', 'bits')


def draw_revisit_hist(ax, bins, dens):
    """Panel 4: pooled revisit-interval density.

    Node-steps between consecutive visits to the same node.  A route sampler
    that ping-pongs between two waypoints puts a hard spike at 2; a player
    working a map produces a long ragged tail.  Density, not counts."""
    ax.bar(bins, np.minimum(dens, REVISIT_YMAX), width=0.8, color=NEUTRAL_BAR)
    ax.set_xlim(1.4, REVISIT_CAP + 0.6)
    ax.set_ylim(0, REVISIT_YMAX)
    ax.set_xticks(list(bins))
    ax.set_xticklabels([str(int(b)) if b < REVISIT_CAP else f'{REVISIT_CAP}+'
                        for b in bins], fontsize=7)
    ax.set_xlabel('node-steps until the same node is revisited', fontsize=7)
    ax.set_ylabel('density', fontsize=7)
    ax.tick_params(labelsize=7)
    ax.grid(axis='y', alpha=0.25, lw=0.5)
    ax.set_title('revisit-interval distribution (both teams pooled)',
                 fontsize=8)


def draw_kl_bars(ax, kl_by_team):
    """Panel 5: occupancy vs map divergence, two fixed slots.

    KL(where this team spent its time || uniform over the map's nodes).  Zero
    is "used the whole map"; the dotted ceiling is "lived in one node"."""
    teams = ['red', 'blue']
    xs = np.arange(2)
    vals = [kl_by_team.get(t) for t in teams]
    for i, (t, v) in enumerate(zip(teams, vals)):
        if v is None:
            ax.plot([i - 0.25, i + 0.25], [0, 0], color='#bbbbbb', lw=1.2)
            continue
        ax.bar([i], [min(v, KL_YMAX)], width=0.5, color=TEAM_COLOR[t])
        ax.text(i, min(v + 0.06, KL_YMAX * 0.94), f'{v:.2f}', ha='center',
                fontsize=7)
    ax.set_xlim(-0.6, 1.6)
    ax.set_ylim(0, KL_YMAX)
    ax.set_xticks(xs); ax.set_xticklabels(teams, fontsize=7)
    ax.set_ylabel('bits', fontsize=7)
    ax.tick_params(labelsize=7)
    ax.grid(axis='y', alpha=0.25, lw=0.5)
    ax.set_title('map-occupancy divergence  KL(visits || uniform over nodes)',
                 fontsize=8)


def draw_offgraph_panel(ax, offgraph, k=ROUTE_MAX_PLAYER_ROWS):
    """Panel 6: per-player off-graph fraction.

    Share of a player's position samples farther than OFFGRAPH_RADIUS from any
    rune seed -- time spent where the map's navigation cloud has no coverage.
    Wall-hugging, ledge-riding and pathing through unmapped geometry light
    this up; a player using the floor sits near a low baseline."""
    _fixed_bar_panel(ax, list(offgraph.values()), k, OFFGRAPH_YMAX,
                     f'per-player off-graph fraction  '
                     f'(samples > {OFFGRAPH_RADIUS:.0f}u from any map node seed)',
                     'fraction')


# The notes strip is CONSTANT text, rendered on every sheet without exception.
# Leak checklist L2: judge set #3 was ruled partly void because the PRESENCE of
# a note discriminated -- only bot demos ever tripped the condition that
# produced it.  The fix is not "write a better note", it is "never let a note's
# presence, absence or wording depend on anything about this demo".
NOTES_TEXT = (
    "reading notes (identical on every sheet of this instrument):\n"
    "  * nodes, node order and all axis extents come from the map's navigation seed cloud, "
    "not from this recording; two sheets of one map share them exactly\n"
    "  * every matrix is row-normalized and every histogram is a density, so nothing here "
    "reads how much of the match was sampled\n"
    "  * per-player panels always show a fixed number of slots, sorted by value and padded; "
    "a slot is empty when no player filled it\n"
    "  * a player with too few node transitions to estimate an entropy is left out of panel 3 "
    "rather than shown as a noisy bar\n"
    "  * every bar panel has a fixed ceiling chosen once for the instrument; a value above its "
    "panel's ceiling is drawn at the ceiling"
)


def draw_notes_strip(ax):
    ax.axis('off')
    ax.text(0.01, 0.95, NOTES_TEXT, ha='left', va='top', fontsize=7,
            family='monospace', color='#555555', transform=ax.transAxes)


# ----------------------------------------------------------------- analysis
def analyze_demo(demo_path, rune_dir, pov_parity=False, pov_ent=None,
                 pov_radius=F.POV_RADIUS_DEFAULT,
                 pov_fov=F.POV_FOV_DEG_DEFAULT):
    """Everything both --scalars and the sheet renderer need, computed once.

    Control flow mirrors F.render_sheet's exactly -- refuse, cap, anonymize,
    parity-filter, re-anonymize -- because that ordering was debugged there
    and re-deriving it would be a good way to reintroduce a fixed bug."""
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

    fx = load_or_build_nodes(rune_dir, d['map'])
    nodes = fx['nodes']
    seeds = fx['seeds']
    extent = tuple(fx['extent'])
    n_nodes = len(nodes)

    assign = assign_nodes(tracks, labels, nodes)
    seqs, visits = node_sequences(assign)
    offgraph = offgraph_fraction(tracks, labels, seeds)
    ent, ntr = transition_entropy(seqs, n_nodes)

    ents_by_team = {t: {n for n in labels if teams.get(n) == t}
                    for t in ('red', 'blue')}
    P_by_team, counts_by_team, visits_vec, kl_by_team = {}, {}, {}, {}
    for t in ('red', 'blue'):
        members = ents_by_team[t]
        P, counts = transition_matrix(seqs, n_nodes, ents=members)
        P_by_team[t] = P
        counts_by_team[t] = counts
        v = np.zeros(n_nodes, dtype=np.float64)
        for n in members:
            for nd, k in visits.get(n, {}).items():
                v[nd] += k
        visits_vec[t] = v
        kl_by_team[t] = occupancy_kl(visits, n_nodes, ents=members)

    P_all, counts_all = transition_matrix(seqs, n_nodes)
    bins, dens = revisit_intervals(seqs)

    ent_vals = [v for v in ent.values() if v is not None]
    off_vals = [v for v in offgraph.values() if v is not None]
    kl_vals = [v for v in kl_by_team.values() if v is not None]
    mtm_by_team = {t: max_transition_mass(counts_by_team[t])
                  for t in ('red', 'blue')}
    mtm_vals = [v for v in mtm_by_team.values() if v is not None]
    scalars = {
        'mean_route_entropy_bits': float(np.mean(ent_vals)) if ent_vals else None,
        'occupancy_kl_bits': float(np.mean(kl_vals)) if kl_vals else None,
        'offgraph_fraction': float(np.mean(off_vals)) if off_vals else None,
        'revisit_spike2_mass': float(dens[0]) if len(dens) else None,
        'edge_density': edge_density(P_all),
        'max_transition_mass': float(np.mean(mtm_vals)) if mtm_vals else None,
    }

    coverage = F.coverage_stats(tracks, labels, d['frames'])
    return {
        'd': d, 'labels': labels, 'teams': teams, 'tracks': tracks,
        'fixture': fx, 'nodes': nodes, 'seeds': seeds, 'extent': extent,
        'n_nodes': n_nodes, 'seqs': seqs, 'visits': visits,
        'offgraph': offgraph, 'entropy': ent, 'n_transitions': ntr,
        'P_by_team': P_by_team, 'counts_by_team': counts_by_team,
        'visits_vec': visits_vec, 'kl_by_team': kl_by_team,
        'P_all': P_all, 'counts_all': counts_all,
        'revisit_bins': bins, 'revisit_dens': dens,
        'scalars': scalars, 'coverage': coverage, 'pov_parity': pov_info,
        'duration_capped': duration_capped, 'duration_original_s': orig_duration,
    }


SCALAR_KEYS = ['mean_route_entropy_bits', 'occupancy_kl_bits',
               'offgraph_fraction', 'revisit_spike2_mass', 'edge_density',
               'max_transition_mass']
SCALAR_PANEL = {
    'mean_route_entropy_bits': 'panel 3 (entropy bars)',
    'occupancy_kl_bits': 'panel 5 (KL bars)',
    'offgraph_fraction': 'panel 6 (off-graph)',
    'revisit_spike2_mass': 'panel 4 (revisit histogram)',
    'edge_density': 'panels 1-2 (route graph / matrix)',
    'max_transition_mass': 'panel 2 (transition matrix, cell-level)',
}


# ------------------------------------------------------------------ render
def render_routes_sheet(demo_path, rune_dir, out_dir, pov_parity=False,
                        pov_ent=None, pov_radius=F.POV_RADIUS_DEFAULT,
                        pov_fov=F.POV_FOV_DEG_DEFAULT):
    a = analyze_demo(demo_path, rune_dir, pov_parity=pov_parity,
                     pov_ent=pov_ent, pov_radius=pov_radius, pov_fov=pov_fov)
    d = a['d']
    labels, teams = a['labels'], a['teams']
    h = F.hash_demo(demo_path)
    os.makedirs(out_dir, exist_ok=True)

    fig = plt.figure(figsize=FIGSIZE, dpi=FIGDPI)
    gs = fig.add_gridspec(len(ROW_HEIGHTS), GRID_COLS,
                          height_ratios=ROW_HEIGHTS,
                          hspace=0.55, wspace=0.30,
                          top=0.945, bottom=0.045, left=0.06, right=0.97)

    ax_graph = fig.add_subplot(gs[0, :])
    draw_route_graph(ax_graph, a['seeds'], a['nodes'], a['P_by_team'],
                     a['visits_vec'], a['fixture'])

    order = a['fixture']['order']
    draw_transition_matrix(fig.add_subplot(gs[1, 0:3]), a['P_by_team']['red'],
                           order, 'red')
    draw_transition_matrix(fig.add_subplot(gs[1, 3:6]), a['P_by_team']['blue'],
                           order, 'blue')

    draw_entropy_bars(fig.add_subplot(gs[2, :]), a['entropy'])
    draw_revisit_hist(fig.add_subplot(gs[3, 0:3]), a['revisit_bins'],
                      a['revisit_dens'])
    draw_kl_bars(fig.add_subplot(gs[3, 3:6]), a['kl_by_team'])
    draw_offgraph_panel(fig.add_subplot(gs[4, :]), a['offgraph'])
    draw_notes_strip(fig.add_subplot(gs[5, :]))

    # L10: map and hash, nothing else.  The map is matched across a judge set
    # by design; the hash is the blind identity.  No players=, no carries=, no
    # duration, no parity marker -- see film.py's caption comment for the
    # ledger of what each of those cost.
    caption = f"map={d['map'] or '?'}   hash={h}"
    fig.text(0.5, 0.985, caption, ha='center', fontsize=10, weight='bold')

    png_path = os.path.join(out_dir, f'{h}.png')
    fig.savefig(png_path)
    plt.close(fig)

    sidecar = {
        'hash': h,
        'source_path': os.path.abspath(demo_path),
        'source_basename': os.path.basename(demo_path),
        'map': d['map'],
        'demo_shape': 'serverrecord(bot)' if d['svrecord'] else 'client(human)',
        'sheet': 'routes',
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
        'node_set_version': NODE_SET_VERSION,
        'n_nodes': a['n_nodes'],
        'node_cell_size': a['fixture']['cell_size'],
        'map_extent': list(a['extent']),
        'transition_counts': {t: a['counts_by_team'][t].astype(int).tolist()
                              for t in ('red', 'blue')},
        'entropy_by_label': {labels[e]: v for e, v in a['entropy'].items()},
        'n_transitions_by_label': {labels[e]: v
                                   for e, v in a['n_transitions'].items()},
        'offgraph_by_label': {labels[e]: v for e, v in a['offgraph'].items()},
        'occupancy_kl': a['kl_by_team'],
        'revisit_hist': {'bins': [int(b) for b in a['revisit_bins']],
                         'density': [float(x) for x in a['revisit_dens']]},
        'scalars': a['scalars'],
        'rendered_at': time.strftime('%Y-%m-%dT%H:%M:%S'),
    }
    json_path = os.path.join(out_dir, f'{h}.json')
    with open(json_path, 'w') as f:
        json.dump(sidecar, f, indent=1)

    return {'hash': h, 'map': d['map'], 'svrecord': d['svrecord'],
            'players': len(labels), 'png': png_path, 'json': json_path,
            'pov_parity': a['pov_parity'], 'scalars': a['scalars'],
            'visible_fraction': a['coverage']['visible_fraction']}


# ------------------------------------------------------------- calibration
def _ranks(x):
    """Average ranks, ties shared -- the tie handling matters here because
    several scalars are bounded and can repeat exactly across demos."""
    x = np.asarray(x, dtype=np.float64)
    order = np.argsort(x, kind='mergesort')
    r = np.empty(len(x), dtype=np.float64)
    r[order] = np.arange(1, len(x) + 1, dtype=np.float64)
    i = 0
    xs = x[order]
    while i < len(xs):
        j = i
        while j + 1 < len(xs) and xs[j + 1] == xs[i]:
            j += 1
        if j > i:
            r[order[i:j + 1]] = (i + j + 2) / 2.0
        i = j + 1
    return r


def roc_auc(pos, neg):
    """P(a random `pos` value exceeds a random `neg` value), ties at 0.5 --
    the Mann-Whitney form, which needs no threshold sweep and is exact."""
    pos = [v for v in pos if v is not None]
    neg = [v for v in neg if v is not None]
    if not pos or not neg:
        return None
    r = _ranks(list(pos) + list(neg))
    n1, n2 = len(pos), len(neg)
    return float((r[:n1].sum() - n1 * (n1 + 1) / 2.0) / (n1 * n2))


DEFAULT_HUMAN_GLOB = '~/Games/Quake2/lmctf-hooktest/demos/*.dm2'
DEFAULT_BOT_GLOBS = [
    '~/.local/share/YamagiQ2/lmctf-hooktest/demos/wave39[0-9]*-s03*.dm2',
    '~/.local/share/YamagiQ2/lmctf-hooktest/demos/wave40*-s03*.dm2',
]
DEFAULT_CACHE = '~/.cache/routesheet-scalars.json'


def _expand(pats):
    out = []
    for p in pats:
        out.extend(sorted(globmod.glob(os.path.expanduser(p))))
    return out


_HUMAN_NAME_RE = re.compile(
    r'^lmctf-\d{4}-\d{2}-\d{2}-(\w+)-\d{2}\.\d{2}\.dm2$')


def _map_from_basename(path):
    """The human corpus names its files lmctf-YYYY-MM-DD-<map>-HH.MM.dm2, so
    a --maps restriction can skip a non-matching one without paying for a full
    demo walk.  Returns None for any other naming (bot serverrecord files
    carry a wave/slot name and no map), and a None answer means 'walk it and
    read the map out of configstring 33' -- never 'exclude it'."""
    m = _HUMAN_NAME_RE.match(os.path.basename(path))
    return m.group(1) if m else None


def _cache_key(path, pov_parity, radius, fov):
    st = os.stat(path)
    # The parity radius and fov only change the answer when parity is actually
    # applied, so they are dropped from the key otherwise -- without this the
    # radius-perturbation check re-walks the whole human arm once per radius
    # to arrive at the identical numbers.
    if not pov_parity:
        radius = fov = None
    return f"{os.path.abspath(path)}|{st.st_mtime_ns}|{st.st_size}|" \
           f"{int(bool(pov_parity))}|{radius}|{fov}|v{NODE_SET_VERSION}"


def collect_scalars(paths, rune_dir, pov_parity, radius, fov, cache,
                    maps=None, label=''):
    """Walk a file list and return [{'path','map','shape', **scalars}].

    Cached on (path, mtime, size, parity settings, node-set version) because
    the calibration gate gets re-run with perturbed parity radii and a demo
    walk is the expensive part."""
    rows = []
    for p in paths:
        hint = _map_from_basename(p)
        if maps and hint is not None and hint not in maps:
            continue
        key = _cache_key(p, pov_parity, radius, fov)
        if key in cache:
            row = dict(cache[key])
        else:
            try:
                a = analyze_demo(p, rune_dir, pov_parity=pov_parity,
                                 pov_radius=radius, pov_fov=fov)
            except (F.DemoUndersampled, RouteFixtureMissing) as e:
                cache[key] = {'skip': f'{type(e).__name__}'}
                continue
            except Exception as e:
                sys.stderr.write(f"FAIL {os.path.basename(p)}: "
                                 f"{type(e).__name__}: {e}\n")
                continue
            row = {'map': a['d']['map'],
                   'shape': 'bot' if a['d']['svrecord'] else 'human',
                   'parity_applied': bool(a['pov_parity'].get('applied')),
                   'visible_fraction': a['coverage']['visible_fraction']}
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
                         f"map={row.get('map')}\n")
    return rows


def run_calibration(human_paths, bot_paths, rune_dir, radius, fov, cache,
                    maps=None):
    """Measure whether the fixed route instrument separates a labeled calibration set."""
    hr = collect_scalars(human_paths, rune_dir, False, radius, fov, cache,
                         maps=maps, label='human')
    br = collect_scalars(bot_paths, rune_dir, True, radius, fov, cache,
                         maps=maps, label='bot')
    hmaps = {r['map'] for r in hr}
    bmaps = {r['map'] for r in br}
    shared = sorted(hmaps & bmaps)
    hr = [r for r in hr if r['map'] in shared]
    br = [r for r in br if r['map'] in shared]
    out = {'maps': shared, 'n_human': len(hr), 'n_bot': len(br), 'auc': {},
           'human_paths': [r['path'] for r in hr],
           'bot_paths': [r['path'] for r in br]}
    for k in SCALAR_KEYS:
        hv = [r.get(k) for r in hr]
        bv = [r.get(k) for r in br]
        auc = roc_auc(bv, hv)
        out['auc'][k] = {
            'auc_bot_over_human': auc,
            'separability': (max(auc, 1.0 - auc) if auc is not None else None),
            'human_mean': (float(np.mean([v for v in hv if v is not None]))
                           if any(v is not None for v in hv) else None),
            'bot_mean': (float(np.mean([v for v in bv if v is not None]))
                         if any(v is not None for v in bv) else None),
        }
    return out


def print_calibration(res, title='CALIBRATION'):
    print(f"\n=== {title}: maps={','.join(res['maps']) or '(none shared)'} "
          f"n_human={res['n_human']} n_bot={res['n_bot']} ===")
    print(f"{'scalar':28s} {'human_mean':>11s} {'bot_mean':>11s} "
          f"{'AUC(b>h)':>9s} {'separab.':>9s}  panel")
    best = None
    for k in SCALAR_KEYS:
        a = res['auc'][k]
        hm = '     n/a' if a['human_mean'] is None else f"{a['human_mean']:11.4f}"
        bm = '     n/a' if a['bot_mean'] is None else f"{a['bot_mean']:11.4f}"
        au = '      n/a' if a['auc_bot_over_human'] is None \
            else f"{a['auc_bot_over_human']:9.3f}"
        sp = '      n/a' if a['separability'] is None \
            else f"{a['separability']:9.3f}"
        print(f"{k:28s} {hm} {bm} {au} {sp}  {SCALAR_PANEL[k]}")
        if a['separability'] is not None and (best is None
                                              or a['separability'] > best[1]):
            best = (k, a['separability'], a['auc_bot_over_human'])
    if best:
        direction = 'higher on bots' if best[2] >= 0.5 else 'higher on humans'
        print(f"\ntop separating statistic: {best[0]} "
              f"(separability {best[1]:.3f}, {direction}, "
              f"{SCALAR_PANEL[best[0]]})")
        gate = 'PASS' if best[1] >= 0.85 else 'FAIL'
        print(f"calibration gate (separability >= 0.85 on at least one "
              f"scalar): {gate}")
        if best[1] >= 0.95:
            print("WARNING: separability >= 0.95 -- the design requires this "
                  "be inspected by hand before it is believed. A near-perfect "
                  "separator is what an instrument leak looks like from the "
                  "inside.")
    return best


# ------------------------------------------------------------------- main
DEFAULT_RUNEDIR = F.DEFAULT_RUNEDIR


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('demos', nargs='*', help='.dm2 demo files')
    ap.add_argument('--out', help='output directory (required to render)')
    ap.add_argument('--runedir', default=DEFAULT_RUNEDIR,
                    help=f'directory holding <map>.rune files '
                         f'(default: {DEFAULT_RUNEDIR})')
    ap.add_argument('--pov-parity', action='store_true',
                    help='serverrecord demos only: keep another player\'s '
                         'sample only when a virtual recorder could plausibly '
                         'have seen it (F.apply_pov_parity). MANDATORY on '
                         'every serverrecord sheet that enters a judge set.')
    ap.add_argument('--pov-ent', type=int, default=None)
    ap.add_argument('--pov-radius', type=float, default=F.POV_RADIUS_DEFAULT)
    ap.add_argument('--pov-fov', type=float, default=F.POV_FOV_DEG_DEFAULT)
    ap.add_argument('--build-nodes', action='store_true',
                    help='build and cache the per-map node fixture for every '
                         'map named by the input demos; render nothing')
    ap.add_argument('--rebuild-nodes', action='store_true',
                    help='with --build-nodes, ignore an existing cache')
    ap.add_argument('--scalars', action='store_true',
                    help='render nothing; print one CSV row of calibration '
                         'scalars per demo')
    ap.add_argument('--calibrate', action='store_true',
                    help='calibration gate: run the instrument over a labeled '
                         'known-set and report ROC AUC per scalar. Renders '
                         'nothing and never writes a label onto any sheet.')
    ap.add_argument('--human', nargs='+', default=[DEFAULT_HUMAN_GLOB],
                    help='globs for the human client-demo arm of --calibrate')
    ap.add_argument('--bot', nargs='+', default=DEFAULT_BOT_GLOBS,
                    help='globs for the serverrecord arm of --calibrate')
    ap.add_argument('--maps', nargs='+', default=None,
                    help='restrict --calibrate to these maps (map-matched '
                         'sets are the point; without this the two arms are '
                         'auto-restricted to whatever maps they share)')
    ap.add_argument('--radius-check', action='store_true',
                    help='re-run the calibration gate with the parity radius at '
                         '+/-100u. Any scalar whose AUC swings more than '
                         '~0.10 is measuring coverage, not behaviour.')
    ap.add_argument('--cache', default=DEFAULT_CACHE,
                    help='scalar cache file for --calibrate')
    args = ap.parse_args()

    if args.calibrate:
        cpath = os.path.expanduser(args.cache)
        cache = {}
        if os.path.exists(cpath):
            try:
                cache = json.load(open(cpath))
            except Exception:
                cache = {}
        human = _expand(args.human)
        bot = _expand(args.bot)
        sys.stderr.write(f"calibrate: {len(human)} human candidate(s), "
                         f"{len(bot)} bot candidate(s)\n")
        res = run_calibration(human, bot, args.runedir, args.pov_radius,
                              args.pov_fov, cache, maps=args.maps)
        best = print_calibration(res)
        if args.radius_check:
            for dr in (-100.0, +100.0):
                r2 = args.pov_radius + dr
                alt = run_calibration(human, bot, args.runedir, r2,
                                      args.pov_fov, cache, maps=args.maps)
                print_calibration(alt, title=f'CALIBRATION (parity radius '
                                             f'{r2:.0f}u)')
                print("  radius-perturbation swing vs baseline:")
                for k in SCALAR_KEYS:
                    a0 = res['auc'][k]['auc_bot_over_human']
                    a1 = alt['auc'][k]['auc_bot_over_human']
                    if a0 is None or a1 is None:
                        continue
                    flag = '  <-- COVERAGE-SENSITIVE' if abs(a1 - a0) > 0.10 \
                        else ''
                    print(f"    {k:28s} {a0:.3f} -> {a1:.3f} "
                          f"(d={a1 - a0:+.3f}){flag}")
        os.makedirs(os.path.dirname(cpath) or '.', exist_ok=True)
        with open(cpath, 'w') as f:
            json.dump(cache, f)
        return

    if not args.demos:
        ap.error('no demos given')

    if args.build_nodes:
        seen = {}
        failures = 0
        for p in args.demos:
            try:
                d = F.walk_demo(p)
            except Exception as e:
                print(f"FAIL {os.path.basename(p)}: {e}")
                failures += 1
                continue
            m = d['map']
            if not m or m in seen:
                continue
            try:
                fx = load_or_build_nodes(args.runedir, m,
                                         rebuild=args.rebuild_nodes)
                seen[m] = fx
                print(f"OK   map={m} nodes={len(fx['nodes'])} "
                      f"cell={fx['cell_size']:.1f}u seeds={fx['n_seeds']} "
                      f"-> {os.path.join(os.path.dirname(F.find_rune(args.runedir, m)), m + '.nodes.json')}")
            except Exception as e:
                print(f"FAIL map={m}: {type(e).__name__}: {e}")
                failures += 1
        print(f"\n{len(seen)} map fixture(s) available")
        return 1 if failures else 0

    if args.scalars:
        scalar_failures = 0
        print('demo_shape,map,basename,' + ','.join(SCALAR_KEYS))
        for p in args.demos:
            try:
                a = analyze_demo(p, args.runedir, pov_parity=args.pov_parity,
                                 pov_ent=args.pov_ent,
                                 pov_radius=args.pov_radius,
                                 pov_fov=args.pov_fov)
            except (F.DemoUndersampled, RouteFixtureMissing) as e:
                sys.stderr.write(f"SKIP {os.path.basename(p)}: {e}\n")
                scalar_failures += 1
                continue
            except Exception as e:
                sys.stderr.write(f"FAIL {os.path.basename(p)}: "
                                 f"{type(e).__name__}: {e}\n")
                scalar_failures += 1
                continue
            shape = 'bot' if a['d']['svrecord'] else 'human'
            vals = ','.join('' if a['scalars'][k] is None
                            else f"{a['scalars'][k]:.6f}" for k in SCALAR_KEYS)
            print(f"{shape},{a['d']['map']},{os.path.basename(p)},{vals}")
        return 1 if scalar_failures else 0

    if not args.out:
        ap.error('--out is required to render sheets')

    ok, skipped, failed = [], [], []
    for p in args.demos:
        try:
            res = render_routes_sheet(p, args.runedir, args.out,
                                      pov_parity=args.pov_parity,
                                      pov_ent=args.pov_ent,
                                      pov_radius=args.pov_radius,
                                      pov_fov=args.pov_fov)
            ok.append(res)
            pov = res['pov_parity']
            if res['svrecord'] and not pov.get('applied'):
                sys.stderr.write(
                    f"WARNING {os.path.basename(p)}: serverrecord demo "
                    f"rendered WITHOUT pov-parity. Leak checklist L5 makes "
                    f"parity mandatory on every bot sheet that enters a judge "
                    f"set; this sheet must not be used in one.\n")
            pov_str = (f" pov=ent{pov['pov_entnum']}@{pov['radius_u']:.0f}u"
                       if pov.get('applied') else "")
            s = res['scalars']
            print(f"OK   {os.path.basename(p)} -> {res['hash']}.png  "
                  f"map={res['map']} "
                  f"{'bot' if res['svrecord'] else 'human'} "
                  f"players={res['players']}{pov_str} "
                  f"vis={res['visible_fraction']:.3f} "
                  f"H={s['mean_route_entropy_bits']:.2f} "
                  f"KL={s['occupancy_kl_bits']:.2f} "
                  f"off={s['offgraph_fraction']:.3f} "
                  f"rv2={s['revisit_spike2_mass']:.3f}")
        except (F.DemoUndersampled, RouteFixtureMissing) as e:
            skipped.append((p, str(e)))
            print(f"SKIP {os.path.basename(p)}: {e}")
        except Exception as e:
            failed.append((p, str(e)))
            print(f"FAIL {os.path.basename(p)}: {type(e).__name__}: {e}")

    print(f"\n{len(ok)} sheet(s) written to {args.out}, "
          f"{len(skipped)} skipped, {len(failed)} failed")
    return 1 if skipped or failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
