#!/usr/bin/env python3
"""
runeview.py -- permanent visual dump tool for SLIPGATE .rune files.

Build order (slipgate/SLIPGATE.md, step 2) calls for walking a rune by eye
before any bot reads it. This replaces the throwaway python that used to do
that with a proper script.

Reads a .rune file exactly as slipgate/sg_rune.c writes it: a rune_header_t,
then num_seeds rune_seed_t records, then num_links rune_link_t records, flat
binary, little-endian, native struct layout (no packing pragmas in the C
source, confirmed by measuring the real header on-disk against a compiled
struct probe). Struct layouts are documented in slipgate/sg_rune.h.

Produces a single self-contained HTML file: top-down component view, a
directed-reachability view for a goal seed, an optional side-elevation slice,
a stats block, and an optional diff against an older rune.

No external resources: everything (SVG, CSS, JS for pan/zoom) is inlined.
"""

import argparse
import html
import os
import re
import struct
import sys
from collections import deque

# --------------------------------------------------------------------- I/O

RUNE_MAGIC = 0x454E5552
RUNE_VERSION = 1

# rune_header_t: int magic, version, num_seeds, num_links; char mapname[64]
HEADER_FMT = '<4i64s'
HEADER_SIZE = struct.calcsize(HEADER_FMT)

# rune_seed_t: vec3_t origin (3 float); short area_hint, flags
SEED_FMT = '<3f2h'
SEED_SIZE = struct.calcsize(SEED_FMT)

# rune_link_t: int from, to; byte action, provenance, min_speed, heading,
# heading_slack, exit_speed; short cost_ms; vec3_t anchor (3 float)
LINK_FMT = '<2i6Bh3f'
LINK_SIZE = struct.calcsize(LINK_FMT)

assert HEADER_SIZE == 80, HEADER_SIZE
assert SEED_SIZE == 16, SEED_SIZE
assert LINK_SIZE == 28, LINK_SIZE

ACTION_NAMES = {0: 'RUN', 1: 'JUMP', 2: 'DROP', 3: 'HOOK', 4: 'SWIM'}
# grey, cyan, yellow, orange, blue -- per spec, do not change
ACTION_COLORS = {
    0: '#9a9a9a',   # run
    1: '#00c8d7',   # jump
    2: '#e0c000',   # drop
    3: '#ff8c1a',   # hook
    4: '#3d7dff',   # swim
}
PROVENANCE_NAMES = {0: 'PROVEN', 1: 'OBSERVED', 2: 'ADJUSTED'}


class Rune:
    __slots__ = ('path', 'magic', 'version', 'num_seeds', 'num_links',
                 'mapname', 'seeds', 'links')


def load_rune(path):
    """Read a .rune file into a Rune object. Tolerant of a links/seeds
    count that overruns the actual file size (reads as many full records
    as are present and warns), since this tool exists to look at files
    that may be mid-generation."""
    with open(path, 'rb') as f:
        data = f.read()

    if len(data) < HEADER_SIZE:
        raise ValueError(f"{path}: file is {len(data)} bytes, "
                          f"smaller than the {HEADER_SIZE}-byte header")

    magic, version, num_seeds, num_links, mapname_raw = \
        struct.unpack_from(HEADER_FMT, data, 0)

    if magic != RUNE_MAGIC:
        raise ValueError(f"{path}: bad magic 0x{magic:08x}, "
                          f"expected 0x{RUNE_MAGIC:08x}")
    if version != RUNE_VERSION:
        sys.stderr.write(f"warning: {path}: rune version {version}, "
                          f"this tool knows version {RUNE_VERSION}\n")

    mapname = mapname_raw.split(b'\x00', 1)[0].decode('ascii', 'replace')

    r = Rune()
    r.path = path
    r.magic = magic
    r.version = version
    r.mapname = mapname

    off = HEADER_SIZE
    seeds = []
    avail_seeds = min(num_seeds, (len(data) - off) // SEED_SIZE)
    if avail_seeds < num_seeds:
        sys.stderr.write(f"warning: {path}: header claims {num_seeds} seeds, "
                          f"file only has room for {avail_seeds}\n")
    for i in range(avail_seeds):
        x, y, z, area_hint, flags = struct.unpack_from(SEED_FMT, data, off)
        seeds.append({'x': x, 'y': y, 'z': z,
                       'area_hint': area_hint, 'flags': flags})
        off += SEED_SIZE

    links = []
    avail_links = min(num_links, (len(data) - off) // LINK_SIZE)
    if avail_links < num_links:
        sys.stderr.write(f"warning: {path}: header claims {num_links} links, "
                          f"file only has room for {avail_links}\n")
    for i in range(avail_links):
        (frm, to, action, provenance, min_speed, heading, heading_slack,
         exit_speed, cost_ms, ax, ay, az) = struct.unpack_from(LINK_FMT, data, off)
        links.append({'from': frm, 'to': to, 'action': action,
                       'provenance': provenance, 'min_speed': min_speed,
                       'heading': heading, 'heading_slack': heading_slack,
                       'exit_speed': exit_speed, 'cost_ms': cost_ms,
                       'anchor': (ax, ay, az)})
        off += LINK_SIZE

    r.num_seeds = len(seeds)
    r.num_links = len(links)
    r.seeds = seeds
    r.links = links
    return r


# ------------------------------------------------------------- graph work

def undirected_components(rune):
    """Union-find over seeds, edges from links regardless of direction.
    Returns (comp_of: list[int] seed->component id,
              comps: dict[int, list[int]] component id -> seed indices,
              ordered: list[int] component ids sorted by size descending)."""
    n = rune.num_seeds
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

    for link in rune.links:
        a, b = link['from'], link['to']
        if 0 <= a < n and 0 <= b < n:
            union(a, b)

    comps = {}
    for i in range(n):
        r = find(i)
        comps.setdefault(r, []).append(i)

    ordered = sorted(comps.keys(), key=lambda r: -len(comps[r]))
    remap = {root: idx for idx, root in enumerate(ordered)}
    comp_of = [remap[find(i)] for i in range(n)]
    comps_by_idx = {remap[root]: seeds for root, seeds in comps.items()}
    return comp_of, comps_by_idx, list(range(len(ordered)))


def directed_adjacency(rune):
    fwd = [[] for _ in range(rune.num_seeds)]
    rev = [[] for _ in range(rune.num_seeds)]
    for link in rune.links:
        a, b = link['from'], link['to']
        if 0 <= a < rune.num_seeds and 0 <= b < rune.num_seeds:
            fwd[a].append(b)
            rev[b].append(a)
    return fwd, rev


def reachable_to_goal(rune, goal, rev_adj):
    """Seeds that can reach `goal` by following directed links forward --
    computed as a BFS over the reversed graph starting at goal."""
    n = rune.num_seeds
    seen = [False] * n
    if not (0 <= goal < n):
        return seen
    seen[goal] = True
    q = deque([goal])
    while q:
        u = q.popleft()
        for v in rev_adj[u]:
            if not seen[v]:
                seen[v] = True
                q.append(v)
    return seen


def one_way_set(rune):
    """Set of link indices whose reverse (to->from) does not exist as any
    link in the file -- these get a direction arrow in the top-down view."""
    pairs = set()
    for link in rune.links:
        pairs.add((link['from'], link['to']))
    one_way = []
    for i, link in enumerate(rune.links):
        if (link['to'], link['from']) not in pairs:
            one_way.append(i)
    return set(one_way)


# ------------------------------------------------------------ flag lookup

def _parse_entity_text(text):
    """Parse a quake .ent-style entity text blob into a list of dicts."""
    ents = []
    for block in re.findall(r'\{([^{}]*)\}', text, re.S):
        kv = {}
        for m in re.finditer(r'"([^"]*)"\s*"([^"]*)"', block):
            kv[m.group(1)] = m.group(2)
        if kv:
            ents.append(kv)
    return ents


def _bsp_entity_text(bsp_path):
    """Minimal IBSP reader: just enough to pull the entity lump (lump 0)."""
    with open(bsp_path, 'rb') as f:
        data = f.read()
    if len(data) < 8 or data[0:4] not in (b'IBSP', b'QBSP'):
        return None
    # lump directory starts right after the 8-byte header (magic+version),
    # 19 lumps of (offset int32, length int32); lump 0 is entities.
    off, length = struct.unpack_from('<ii', data, 8)
    if off < 0 or length < 0 or off + length > len(data):
        return None
    return data[off:off + length].decode('ascii', 'replace')


def find_red_flag_origin(rune_path, mapname):
    """Best-effort: look next to the .rune file for mapname.ent or
    mapname.bsp and pull an info_flag* entity whose classname mentions
    'red'. Returns (x, y, z) or None if nothing usable was found."""
    directory = os.path.dirname(os.path.abspath(rune_path))
    candidates = []
    if mapname:
        candidates.append(os.path.join(directory, mapname + '.ent'))
        candidates.append(os.path.join(directory, mapname + '.bsp'))
    base = os.path.splitext(os.path.basename(rune_path))[0]
    if base != mapname:
        candidates.append(os.path.join(directory, base + '.ent'))
        candidates.append(os.path.join(directory, base + '.bsp'))

    for path in candidates:
        if not os.path.isfile(path):
            continue
        try:
            if path.endswith('.ent'):
                with open(path, 'r', errors='replace') as f:
                    text = f.read()
            else:
                text = _bsp_entity_text(path)
                if text is None:
                    continue
        except OSError:
            continue

        ents = _parse_entity_text(text)
        best = None
        for e in ents:
            cn = e.get('classname', '')
            if not cn.lower().startswith('info_flag'):
                continue
            if 'red' in cn.lower():
                best = e
                break
            if best is None:
                best = e  # any info_flag is better than nothing
        if best is not None and 'origin' in best:
            try:
                parts = [float(v) for v in best['origin'].split()]
                if len(parts) == 3:
                    return tuple(parts)
            except ValueError:
                pass
    return None


def nearest_seed(rune, point):
    px, py, pz = point
    best_i, best_d = 0, None
    for i, s in enumerate(rune.seeds):
        dx, dy, dz = s['x'] - px, s['y'] - py, s['z'] - pz
        d = dx * dx + dy * dy + dz * dz
        if best_d is None or d < best_d:
            best_d, best_i = d, i
    return best_i


def resolve_goal(rune, explicit_goal):
    """--goal N wins outright. Otherwise: seed nearest the mapname's red
    flag if an info_flag entity can be found next to the rune file;
    otherwise seed 0."""
    if explicit_goal is not None:
        if not (0 <= explicit_goal < rune.num_seeds):
            sys.stderr.write(f"warning: --goal {explicit_goal} is out of "
                              f"range [0,{rune.num_seeds}), using 0\n")
            return 0, 'explicit (clamped)'
        return explicit_goal, 'explicit'

    origin = find_red_flag_origin(rune.path, rune.mapname)
    if origin is not None and rune.num_seeds > 0:
        seed = nearest_seed(rune, origin)
        return seed, f'nearest info_flag_red at ({origin[0]:.0f}, {origin[1]:.0f}, {origin[2]:.0f})'
    return 0, 'default (no info_flag found)'


# --------------------------------------------------------------- palette

BRIGHT_PALETTE = [
    '#e6194b', '#f58231', '#4363d8', '#911eb1', '#46f0f0',
    '#f032e6', '#fabebe', '#008080', '#e6beff', '#9a6324',
    '#800000', '#aaffc3', '#808000', '#ffd8b1', '#000075',
    '#f5c400', '#7f5aa2', '#c71585', '#20b2aa', '#ff69b4',
]


def component_color(comp_idx, comp_is_largest):
    if comp_is_largest:
        return None  # handled per-seed via height shading
    return BRIGHT_PALETTE[comp_idx % len(BRIGHT_PALETTE)]


def lerp(a, b, t):
    return a + (b - a) * t


def height_shade(z, zmin, zmax):
    """Dark green (low) to bright green (high) for the largest component."""
    if zmax - zmin < 1e-6:
        t = 0.5
    else:
        t = (z - zmin) / (zmax - zmin)
        t = max(0.0, min(1.0, t))
    r = int(lerp(10, 140, t))
    g = int(lerp(70, 255, t))
    b = int(lerp(10, 90, t))
    return f'#{r:02x}{g:02x}{b:02x}'


# ------------------------------------------------------------------ SVG

SVG_HEADER = ('<svg xmlns="http://www.w3.org/2000/svg" class="runeview-svg" '
              'viewBox="{minx:.1f} {miny:.1f} {w:.1f} {h:.1f}" '
              'width="{pxw}" height="{pxh}" preserveAspectRatio="xMidYMid meet">')

ARROW_MARKER = (
    '<defs><marker id="arrow-{mid}" viewBox="0 0 10 10" refX="8" refY="5" '
    'markerWidth="{msize}" markerHeight="{msize}" orient="auto-start-reverse">'
    '<path d="M0,0 L10,5 L0,10 z" fill="{color}"/></marker></defs>'
)


def _bbox(points, pad_frac=0.06):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    minx, maxx = min(xs), max(xs)
    miny, maxy = min(ys), max(ys)
    w = maxx - minx
    h = maxy - miny
    if w < 1e-6:
        w = 1.0
    if h < 1e-6:
        h = 1.0
    padx = w * pad_frac
    pady = h * pad_frac
    return minx - padx, miny - pady, w + 2 * padx, h + 2 * pady


def render_topdown_svg(rune, comp_of, comps_by_idx, largest_comp, one_way):
    pts = [(s['x'], -s['y']) for s in rune.seeds]
    if not pts:
        return '<p>(no seeds)</p>'
    minx, miny, w, h = _bbox(pts)
    diag = (w ** 2 + h ** 2) ** 0.5
    radius = max(diag / 700.0, 2.5)
    stroke = max(diag / 2400.0, 0.6)

    zs = [s['z'] for i, s in enumerate(rune.seeds) if comp_of[i] == largest_comp]
    zmin, zmax = (min(zs), max(zs)) if zs else (0.0, 0.0)

    out = []
    out.append(SVG_HEADER.format(minx=minx, miny=miny, w=w, h=h, pxw=1400, pxh=int(1400 * h / w) if w else 900))
    out.append('<rect x="{:.1f}" y="{:.1f}" width="{:.1f}" height="{:.1f}" class="rv-bg"/>'.format(minx, miny, w, h))

    for action, color in ACTION_COLORS.items():
        out.append(ARROW_MARKER.format(mid=action, msize=max(diag / 350.0, 4), color=color))

    out.append('<g class="rv-links">')
    for i, link in enumerate(rune.links):
        a, b = link['from'], link['to']
        if not (0 <= a < rune.num_seeds and 0 <= b < rune.num_seeds):
            continue
        x1, y1 = pts[a]
        x2, y2 = pts[b]
        color = ACTION_COLORS.get(link['action'], '#ffffff')
        marker = f' marker-end="url(#arrow-{link["action"]})"' if i in one_way else ''
        out.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
                    f'stroke="{color}" stroke-width="{stroke:.2f}" opacity="0.55"{marker}/>')
    out.append('</g>')

    out.append('<g class="rv-seeds">')
    for i, s in enumerate(rune.seeds):
        x, y = pts[i]
        c = comp_of[i]
        if c == largest_comp:
            color = height_shade(s['z'], zmin, zmax)
        else:
            color = component_color(c, False)
        out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{radius:.2f}" fill="{color}" '
                    f'stroke="#000" stroke-width="{stroke*0.4:.2f}"><title>seed {i} '
                    f'({s["x"]:.0f}, {s["y"]:.0f}, {s["z"]:.0f}) comp {c}</title></circle>')
    out.append('</g>')
    out.append('</svg>')
    return ''.join(out)


def render_reach_svg(rune, goal, in_reach):
    pts = [(s['x'], -s['y']) for s in rune.seeds]
    if not pts:
        return '<p>(no seeds)</p>'
    minx, miny, w, h = _bbox(pts)
    diag = (w ** 2 + h ** 2) ** 0.5
    radius = max(diag / 700.0, 2.5)
    stroke = max(diag / 2400.0, 0.6)

    out = []
    out.append(SVG_HEADER.format(minx=minx, miny=miny, w=w, h=h, pxw=1400, pxh=int(1400 * h / w) if w else 900))
    out.append('<rect x="{:.1f}" y="{:.1f}" width="{:.1f}" height="{:.1f}" class="rv-bg"/>'.format(minx, miny, w, h))

    out.append('<g class="rv-links">')
    for link in rune.links:
        a, b = link['from'], link['to']
        if not (0 <= a < rune.num_seeds and 0 <= b < rune.num_seeds):
            continue
        x1, y1 = pts[a]
        x2, y2 = pts[b]
        out.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
                    f'stroke="#666666" stroke-width="{stroke*0.7:.2f}" opacity="0.25"/>')
    out.append('</g>')

    out.append('<g class="rv-seeds">')
    for i, s in enumerate(rune.seeds):
        x, y = pts[i]
        if i == goal:
            out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{radius*2.2:.2f}" fill="#ffffff" '
                        f'stroke="#000000" stroke-width="{stroke:.2f}"><title>GOAL seed {i}</title></circle>')
            continue
        if in_reach[i]:
            out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{radius:.2f}" fill="#33cc55" '
                        f'stroke="#0a5a20" stroke-width="{stroke*0.4:.2f}"><title>seed {i}: '
                        f'can reach goal</title></circle>')
        else:
            out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{radius:.2f}" fill="#2a2a2a" '
                        f'stroke="#ff3b3b" stroke-width="{max(stroke,1.0):.2f}"><title>seed {i}: '
                        f'CANNOT reach goal</title></circle>')
    out.append('</g>')
    out.append('</svg>')
    return ''.join(out)


def render_elevation_svg(rune, region):
    x0, x1, y0, y1 = region
    idx = [i for i, s in enumerate(rune.seeds)
           if x0 <= s['x'] <= x1 and y0 <= s['y'] <= y1]
    if not idx:
        return '<p>(no seeds fall inside the given --region)</p>'
    idx_set = set(idx)
    pts = {i: (rune.seeds[i]['x'], -rune.seeds[i]['z']) for i in idx}

    minx, miny, w, h = _bbox(list(pts.values()))
    diag = (w ** 2 + h ** 2) ** 0.5
    radius = max(diag / 250.0, 3.0)
    stroke = max(diag / 900.0, 0.8)

    out = []
    out.append(SVG_HEADER.format(minx=minx, miny=miny, w=w, h=h, pxw=1400, pxh=int(1400 * h / w) if w else 500))
    out.append('<rect x="{:.1f}" y="{:.1f}" width="{:.1f}" height="{:.1f}" class="rv-bg"/>'.format(minx, miny, w, h))

    out.append('<g class="rv-links">')
    for link in rune.links:
        a, b = link['from'], link['to']
        if a not in idx_set or b not in idx_set:
            continue
        x1p, y1p = pts[a]
        x2p, y2p = pts[b]
        color = ACTION_COLORS.get(link['action'], '#ffffff')
        out.append(f'<line x1="{x1p:.1f}" y1="{y1p:.1f}" x2="{x2p:.1f}" y2="{y2p:.1f}" '
                    f'stroke="{color}" stroke-width="{stroke:.2f}" opacity="0.7"/>')
    out.append('</g>')

    out.append('<g class="rv-seeds">')
    for i in idx:
        s = rune.seeds[i]
        x, y = pts[i]
        out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{radius:.2f}" fill="#66aaff" '
                    f'stroke="#000" stroke-width="{stroke*0.4:.2f}"><title>seed {i} '
                    f'({s["x"]:.0f}, {s["y"]:.0f}, {s["z"]:.0f})</title></circle>')
    out.append('</g>')
    out.append('</svg>')
    return ''.join(out)


# ---------------------------------------------------------------- stats

FRONTIER_BUCKETS = [
    (-float('inf'), -512, 'dz <= -512 (far drop)'),
    (-512, -160, '-512 < dz <= -160 (drop range)'),
    (-160, -32, '-160 < dz <= -32 (gentle down)'),
    (-32, 32, '-32 < dz < 32 (level)'),
    (32, 128, '32 <= dz < 128 (step/jump up)'),
    (128, 512, '128 <= dz < 512 (high, hook territory)'),
    (512, float('inf'), 'dz >= 512 (far up)'),
]


def bucket_dz(dz):
    for lo, hi, label in FRONTIER_BUCKETS:
        if lo <= dz < hi if hi != float('inf') else dz >= lo:
            return label
    return FRONTIER_BUCKETS[-1][2]


def frontier_analysis(rune, in_reach):
    """Pairs where an in-reach seed adjoins an out-of-reach seed (adjoins =
    connected by any link in either direction), bucketed by dz = z(out) -
    z(in)."""
    seen_pairs = set()
    buckets = {label: 0 for _, _, label in FRONTIER_BUCKETS}
    total = 0
    for link in rune.links:
        a, b = link['from'], link['to']
        if not (0 <= a < rune.num_seeds and 0 <= b < rune.num_seeds):
            continue
        key = (a, b) if a < b else (b, a)
        if key in seen_pairs:
            continue
        seen_pairs.add(key)
        ra, rb = in_reach[a], in_reach[b]
        if ra == rb:
            continue
        in_i, out_i = (a, b) if ra else (b, a)
        dz = rune.seeds[out_i]['z'] - rune.seeds[in_i]['z']
        label = bucket_dz(dz)
        buckets[label] += 1
        total += 1
    return total, buckets


def compute_stats(rune, comp_of, comps_by_idx, comp_order, goal, goal_reason, in_reach):
    action_counts = {a: 0 for a in ACTION_NAMES}
    for link in rune.links:
        action_counts[link['action']] = action_counts.get(link['action'], 0) + 1

    comp_sizes = sorted((len(v) for v in comps_by_idx.values()), reverse=True)
    reach_count = sum(1 for v in in_reach if v)
    coverage_pct = (100.0 * reach_count / rune.num_seeds) if rune.num_seeds else 0.0

    frontier_total, frontier_buckets = frontier_analysis(rune, in_reach)

    return {
        'seeds': rune.num_seeds,
        'links_total': rune.num_links,
        'action_counts': action_counts,
        'component_count': len(comp_sizes),
        'component_sizes': comp_sizes,
        'goal': goal,
        'goal_reason': goal_reason,
        'reach_count': reach_count,
        'coverage_pct': coverage_pct,
        'frontier_total': frontier_total,
        'frontier_buckets': frontier_buckets,
    }


def render_stats_html(stats):
    lines = []
    lines.append('<div class="rv-stats">')
    lines.append('<h2>Stats</h2>')
    lines.append('<table class="rv-table">')
    lines.append(f'<tr><td>seeds</td><td>{stats["seeds"]}</td></tr>')
    lines.append(f'<tr><td>links (total)</td><td>{stats["links_total"]}</td></tr>')
    for a in sorted(ACTION_NAMES):
        lines.append(f'<tr><td>&nbsp;&nbsp;links: {ACTION_NAMES[a]}</td>'
                      f'<td><span class="swatch" style="background:{ACTION_COLORS[a]}"></span>'
                      f'{stats["action_counts"].get(a, 0)}</td></tr>')
    lines.append(f'<tr><td>components</td><td>{stats["component_count"]}</td></tr>')
    sizes = stats['component_sizes']
    shown = sizes[:20]
    rest = sizes[20:]
    size_str = ', '.join(str(s) for s in shown)
    if rest:
        size_str += f', ... (+{len(rest)} more, {sum(rest)} seeds)'
    lines.append(f'<tr><td>&nbsp;&nbsp;component sizes</td><td>{html.escape(size_str)}</td></tr>')
    lines.append(f'<tr><td>goal seed</td><td>{stats["goal"]} '
                 f'<span class="dim">({html.escape(stats["goal_reason"])})</span></td></tr>')
    lines.append(f'<tr><td>directed coverage</td><td>{stats["reach_count"]} / {stats["seeds"]} '
                 f'seeds = {stats["coverage_pct"]:.1f}%</td></tr>')
    lines.append('</table>')

    lines.append('<h3>Frontier analysis</h3>')
    lines.append(f'<p class="dim">In-reach seeds adjoining out-of-reach seeds, '
                 f'{stats["frontier_total"]} pairs total, bucketed by '
                 f'dz = z(out-of-reach) - z(in-reach):</p>')
    lines.append('<table class="rv-table">')
    for lo, hi, label in FRONTIER_BUCKETS:
        lines.append(f'<tr><td>{html.escape(label)}</td>'
                      f'<td>{stats["frontier_buckets"].get(label, 0)}</td></tr>')
    lines.append('</table>')
    lines.append('</div>')
    return ''.join(lines)


def stats_plaintext(stats):
    lines = []
    lines.append(f"seeds: {stats['seeds']}")
    lines.append(f"links (total): {stats['links_total']}")
    for a in sorted(ACTION_NAMES):
        lines.append(f"  links: {ACTION_NAMES[a]}: {stats['action_counts'].get(a, 0)}")
    lines.append(f"components: {stats['component_count']}")
    sizes = stats['component_sizes']
    shown = sizes[:20]
    rest = sizes[20:]
    size_str = ', '.join(str(s) for s in shown)
    if rest:
        size_str += f', ... (+{len(rest)} more, {sum(rest)} seeds)'
    lines.append(f"  component sizes: {size_str}")
    lines.append(f"goal seed: {stats['goal']} ({stats['goal_reason']})")
    lines.append(f"directed coverage: {stats['reach_count']} / {stats['seeds']} "
                 f"seeds = {stats['coverage_pct']:.1f}%")
    lines.append(f"frontier pairs (in-reach adjoining out-of-reach): {stats['frontier_total']}")
    for lo, hi, label in FRONTIER_BUCKETS:
        lines.append(f"  {label}: {stats['frontier_buckets'].get(label, 0)}")
    return '\n'.join(lines)


# ------------------------------------------------------------------ diff

def _seed_key(s):
    return (round(s['x'], 1), round(s['y'], 1), round(s['z'], 1))


def compute_diff(new_rune, old_rune, new_goal, new_in_reach):
    new_keys = {_seed_key(s): i for i, s in enumerate(new_rune.seeds)}
    old_keys = {_seed_key(s): i for i, s in enumerate(old_rune.seeds)}

    added_seeds = [k for k in new_keys if k not in old_keys]
    removed_seeds = [k for k in old_keys if k not in new_keys]

    def link_key(rune, link):
        a = rune.seeds[link['from']] if 0 <= link['from'] < rune.num_seeds else None
        b = rune.seeds[link['to']] if 0 <= link['to'] < rune.num_seeds else None
        if a is None or b is None:
            return None
        return (_seed_key(a), _seed_key(b), link['action'])

    new_link_keys = set()
    for l in new_rune.links:
        k = link_key(new_rune, l)
        if k is not None:
            new_link_keys.add(k)
    old_link_keys = set()
    for l in old_rune.links:
        k = link_key(old_rune, l)
        if k is not None:
            old_link_keys.add(k)

    added_links = new_link_keys - old_link_keys
    removed_links = old_link_keys - new_link_keys

    # Coverage delta: map the new goal's world position onto the nearest
    # seed in the old rune, compute old coverage the same way, and diff.
    goal_pos = (new_rune.seeds[new_goal]['x'], new_rune.seeds[new_goal]['y'],
                new_rune.seeds[new_goal]['z']) if new_rune.num_seeds else (0, 0, 0)
    old_goal = nearest_seed(old_rune, goal_pos) if old_rune.num_seeds else 0
    old_fwd, old_rev = directed_adjacency(old_rune)
    old_in_reach = reachable_to_goal(old_rune, old_goal, old_rev)
    old_reach_count = sum(1 for v in old_in_reach if v)
    old_coverage = (100.0 * old_reach_count / old_rune.num_seeds) if old_rune.num_seeds else 0.0
    new_reach_count = sum(1 for v in new_in_reach if v)
    new_coverage = (100.0 * new_reach_count / new_rune.num_seeds) if new_rune.num_seeds else 0.0

    return {
        'old_path': old_rune.path,
        'old_seeds': old_rune.num_seeds,
        'old_links': old_rune.num_links,
        'seeds_added': len(added_seeds),
        'seeds_removed': len(removed_seeds),
        'links_added': len(added_links),
        'links_removed': len(removed_links),
        'old_coverage_pct': old_coverage,
        'new_coverage_pct': new_coverage,
        'coverage_delta': new_coverage - old_coverage,
        'old_goal': old_goal,
    }


def render_diff_html(diff):
    lines = []
    lines.append('<div class="rv-diff">')
    lines.append('<h2>Diff vs ' + html.escape(diff['old_path']) + '</h2>')
    lines.append('<table class="rv-table">')
    lines.append(f'<tr><td>old seeds</td><td>{diff["old_seeds"]}</td></tr>')
    lines.append(f'<tr><td>old links</td><td>{diff["old_links"]}</td></tr>')
    lines.append(f'<tr><td>seeds added</td><td>+{diff["seeds_added"]}</td></tr>')
    lines.append(f'<tr><td>seeds removed</td><td>-{diff["seeds_removed"]}</td></tr>')
    lines.append(f'<tr><td>links added</td><td>+{diff["links_added"]}</td></tr>')
    lines.append(f'<tr><td>links removed</td><td>-{diff["links_removed"]}</td></tr>')
    lines.append(f'<tr><td>old coverage (goal-matched)</td><td>{diff["old_coverage_pct"]:.1f}% '
                 f'(seed {diff["old_goal"]})</td></tr>')
    lines.append(f'<tr><td>new coverage</td><td>{diff["new_coverage_pct"]:.1f}%</td></tr>')
    sign = '+' if diff['coverage_delta'] >= 0 else ''
    lines.append(f'<tr><td>coverage delta</td><td>{sign}{diff["coverage_delta"]:.1f} pts</td></tr>')
    lines.append('</table>')
    lines.append('<p class="dim">Seed/link identity is matched by rounded position '
                 '(0.1 unit) since indices are not stable across a regenerate.</p>')
    lines.append('</div>')
    return ''.join(lines)


# ------------------------------------------------------------------ page

PAGE_CSS = """
:root { color-scheme: dark light; }
body { background:#111; color:#eee; font-family: system-ui, sans-serif; margin: 0; padding: 1.5rem; }
h1 { font-size: 1.4rem; margin-bottom: 0.2rem; }
h2 { font-size: 1.15rem; margin-top: 2rem; border-bottom: 1px solid #444; padding-bottom: 0.3rem; }
h3 { font-size: 1.0rem; margin-top: 1.2rem; }
.dim { color: #999; font-size: 0.9em; }
.rv-section { margin-bottom: 2rem; }
.rv-svg-wrap { border: 1px solid #333; background: #0a0a0a; overflow: hidden; resize: vertical; height: 640px; }
.runeview-svg { width: 100%; height: 100%; display: block; cursor: grab; }
.runeview-svg:active { cursor: grabbing; }
.rv-bg { fill: #05050a; }
.rv-table { border-collapse: collapse; }
.rv-table td { padding: 2px 10px 2px 0; border-bottom: 1px solid #222; }
.swatch { display:inline-block; width:10px; height:10px; margin-right:6px; border-radius:2px; vertical-align:middle; }
.legend { display:flex; flex-wrap:wrap; gap:14px; margin: 0.5rem 0 1rem; font-size:0.9em; }
.legend span.swatch { width:12px; height:12px; }
code { background:#1a1a1a; padding:1px 4px; border-radius:3px; }
@media (prefers-color-scheme: light) {
  body { background:#f6f6f6; color:#111; }
  .rv-svg-wrap { background:#fff; border-color:#ccc; }
  .rv-bg { fill:#fafafa; }
  .rv-table td { border-bottom-color:#ddd; }
  code { background:#eee; }
}
"""

PAGE_JS = """
(function() {
  function enablePanZoom(svg) {
    var vb = svg.viewBox.baseVal;
    var start = {x:0, y:0}, dragging = false;
    svg.addEventListener('wheel', function(e) {
      e.preventDefault();
      var scale = e.deltaY < 0 ? 0.9 : 1.1;
      var rect = svg.getBoundingClientRect();
      var mx = vb.x + (e.clientX - rect.left) / rect.width * vb.width;
      var my = vb.y + (e.clientY - rect.top) / rect.height * vb.height;
      vb.width *= scale; vb.height *= scale;
      vb.x = mx - (mx - vb.x) * scale;
      vb.y = my - (my - vb.y) * scale;
    }, {passive:false});
    svg.addEventListener('mousedown', function(e) {
      dragging = true; start.x = e.clientX; start.y = e.clientY;
    });
    window.addEventListener('mouseup', function() { dragging = false; });
    window.addEventListener('mousemove', function(e) {
      if (!dragging) return;
      var rect = svg.getBoundingClientRect();
      var dx = (e.clientX - start.x) / rect.width * vb.width;
      var dy = (e.clientY - start.y) / rect.height * vb.height;
      vb.x -= dx; vb.y -= dy;
      start.x = e.clientX; start.y = e.clientY;
    });
  }
  document.querySelectorAll('.runeview-svg').forEach(enablePanZoom);
})();
"""


def build_page(rune, stats_html, stats_text_html, topdown_svg, reach_svg,
               elevation_svg, diff_html, region):
    action_legend = ''.join(
        f'<span><span class="swatch" style="background:{ACTION_COLORS[a]}"></span>{ACTION_NAMES[a]}</span>'
        for a in sorted(ACTION_NAMES))

    elevation_section = ''
    if region is not None:
        elevation_section = f"""
<div class="rv-section">
<h2>Side elevation (x, z) -- region x:[{region[0]:.0f},{region[1]:.0f}] y:[{region[2]:.0f},{region[3]:.0f}]</h2>
<div class="rv-svg-wrap">{elevation_svg}</div>
</div>
"""

    diff_section = f'<div class="rv-section">{diff_html}</div>' if diff_html else ''

    return f"""<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>runeview: {html.escape(rune.mapname or os.path.basename(rune.path))}</title>
<style>{PAGE_CSS}</style>
</head>
<body>
<h1>runeview -- {html.escape(rune.mapname or '(no mapname)')}</h1>
<p class="dim">{html.escape(rune.path)} -- {rune.num_seeds} seeds, {rune.num_links} links</p>

<div class="rv-section">
<h2>Top-down: components &amp; links</h2>
<p class="dim">Largest connected component shaded green by height (dark = low, bright = high).
Other components get a distinct bright color each. Links colored by action; arrowheads mark
one-way links (no return link exists).</p>
<div class="legend">{action_legend}</div>
<div class="rv-svg-wrap">{topdown_svg}</div>
</div>

<div class="rv-section">
<h2>Directed reachability to goal</h2>
<p class="dim">Green = can reach the goal seed by following links in their recorded direction.
Red ring = cannot. White = the goal itself.</p>
<div class="rv-svg-wrap">{reach_svg}</div>
</div>
{elevation_section}
<div class="rv-section">{stats_html}</div>
{diff_section}

<script>{PAGE_JS}</script>
</body>
</html>
"""


# ------------------------------------------------------------------- CLI

def parse_region(s):
    parts = s.split(',')
    if len(parts) != 4:
        raise argparse.ArgumentTypeError(
            '--region expects x0,x1,y0,y1 (four comma-separated numbers)')
    try:
        x0, x1, y0, y1 = (float(p) for p in parts)
    except ValueError:
        raise argparse.ArgumentTypeError('--region values must be numbers')
    if x0 > x1:
        x0, x1 = x1, x0
    if y0 > y1:
        y0, y1 = y1, y0
    return (x0, x1, y0, y1)


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog='runeview.py',
        description='Render a SLIPGATE .rune file to a self-contained HTML visual dump.')
    ap.add_argument('rune_file', help='path to the .rune file to visualize')
    ap.add_argument('--goal', type=int, default=None, metavar='N',
                     help='seed index to use as the reachability goal. '
                          'Default: seed nearest the mapname\'s red flag '
                          '(info_flag*) if one can be found next to the '
                          'rune file, else seed 0.')
    ap.add_argument('--region', type=parse_region, default=None,
                     metavar='X0,X1,Y0,Y1',
                     help='world-space box to render as a side elevation '
                          '(x,z) slice, e.g. --region -800,-200,-1600,-1200')
    ap.add_argument('--compare', metavar='OLD.rune', default=None,
                     help='an older .rune file to diff against')
    ap.add_argument('-o', '--output', default=None, metavar='PATH',
                     help='output HTML path (default: <rune_file>.html)')
    args = ap.parse_args(argv)

    try:
        rune = load_rune(args.rune_file)
    except (OSError, ValueError) as e:
        sys.stderr.write(f'runeview: {e}\n')
        return 1

    comp_of, comps_by_idx, comp_order = undirected_components(rune)
    largest_comp = comp_order[0] if comp_order else 0
    one_way = one_way_set(rune)

    goal, goal_reason = resolve_goal(rune, args.goal)
    fwd, rev = directed_adjacency(rune)
    in_reach = reachable_to_goal(rune, goal, rev)

    stats = compute_stats(rune, comp_of, comps_by_idx, comp_order, goal, goal_reason, in_reach)
    stats_html = render_stats_html(stats)

    topdown_svg = render_topdown_svg(rune, comp_of, comps_by_idx, largest_comp, one_way)
    reach_svg = render_reach_svg(rune, goal, in_reach)
    elevation_svg = render_elevation_svg(rune, args.region) if args.region else ''

    diff_html = ''
    if args.compare:
        try:
            old_rune = load_rune(args.compare)
        except (OSError, ValueError) as e:
            sys.stderr.write(f'runeview: --compare: {e}\n')
            return 1
        diff = compute_diff(rune, old_rune, goal, in_reach)
        diff_html = render_diff_html(diff)

    page = build_page(rune, stats_html, None, topdown_svg, reach_svg,
                       elevation_svg, diff_html, args.region)

    out_path = args.output or (args.rune_file + '.html')
    with open(out_path, 'w') as f:
        f.write(page)

    print(f'wrote {out_path}')
    print()
    print(stats_plaintext(stats))
    return 0


if __name__ == '__main__':
    sys.exit(main())
