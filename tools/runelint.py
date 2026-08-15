#!/usr/bin/env python3
"""runelint.py -- format and structural invariants for SLIPGATE rune files.

Every check here is a claim the generator implicitly makes; a violation
is a generator flaw by definition. Run over one rune or a directory.
"""
import argparse
import collections
import glob
import math
import os
import struct
import sys

from mapflags import flag_origins_with_source, nearest
try:
    import rune_contracts_generated as contract
    from rune_contracts_generated import (
        ACTION_SHORT_NAMES as CONTRACT_ACTION_SHORT_NAMES,
        RL_ADJUSTED, RL_DECLARED, RL_DOOR, RL_DROP, RL_HOOK, RL_JUMP,
        RL_LIFT, RL_PROVEN, RL_ROCKETJUMP, RL_RUN, RL_SWIM, RL_TELEPORT,
    )
except ModuleNotFoundError:  # also support `python -m tools.runelint`
    from tools import rune_contracts_generated as contract
    from tools.rune_contracts_generated import (
        ACTION_SHORT_NAMES as CONTRACT_ACTION_SHORT_NAMES,
        RL_ADJUSTED, RL_DECLARED, RL_DOOR, RL_DROP, RL_HOOK, RL_JUMP,
        RL_LIFT, RL_PROVEN, RL_ROCKETJUMP, RL_RUN, RL_SWIM, RL_TELEPORT,
    )
try:
    import runeio
except ModuleNotFoundError:  # also support `python -m tools.runelint`
    from tools import runeio

RUNE_MAGIC = 0x454E5552
RUNE_VERSION = 2
RUNE_V3_VERSION = contract.RUNE_V3_VERSION
READABLE_RUNE_VERSIONS = (1, RUNE_VERSION)
RUNE_MAX_SEEDS = 32768
RUNE_MAX_LINKS = 262144
RUNE_HOOK_MAX_RAY = 8192.0
RUNE_DECLARED_CONTROL_MARKER = 254
RUNE_DROP_CONTROL_MARKER = 254
RUNE_HOOK_CONTROL_SLACK = 24
RUNE_WATER_HOOK_CONTROL_MARKER = 253
RSF_WATER = 1
RSF_TOMBSTONE = 2
PMOVE_COORD_MIN = -4096.0
PMOVE_COORD_MAX = 4095.875

HEADER_FMT = '<4i64s'
SEED_FMT = '<3f2h'
LINK_FMT = '<2i6Bh3f'
HEADER_SIZE = struct.calcsize(HEADER_FMT)
SEED_SIZE = struct.calcsize(SEED_FMT)
LINK_SIZE = struct.calcsize(LINK_FMT)

# Keep the historical CLI labels and inventory (0..8) while sourcing their
# spelling from the reviewed registry. Registered V3 actions must not appear in
# legacy output or become accepted by virtue of having display metadata.
ACTS = [CONTRACT_ACTION_SHORT_NAMES[action]
        for action in range(RL_DOOR + 1)]

# Frozen layout-specific gates. V3 is intentionally absent until S2 provides
# its 128/16/44-byte decoder; neither registry growth nor effective suffixes may
# widen an older flat record. V1/V2 encode no mode byte and therefore admit
# only their implicit NONE mode.
LEGACY_ACTION_MAX = {1: RL_ROCKETJUMP, RUNE_VERSION: RL_DOOR}
LEGACY_PROVENANCE_MAX = {1: RL_DECLARED, RUNE_VERSION: RL_DECLARED}


def _signed_short(value):
    value &= 0xffff
    return value - 0x10000 if value & 0x8000 else value


def short_to_angle(value):
    """The q_shared.h SHORT2ANGLE macro, after its explicit short cast."""
    return _signed_short(value) * (360.0 / 65536.0)


def canonical_short_angle(value):
    """Whether value survives C's SHORT2ANGLE((short)ANGLE2SHORT(value))."""
    if not math.isfinite(value) or value < -180.0 or value >= 180.0:
        return False
    encoded = int(value * 65536.0 / 360.0) & 0xffff
    return value == short_to_angle(encoded)


def hook_control_errors(anchor):
    """Version-2 RL_HOOK validation, matching Rune_Load's C contract."""
    pitch, yaw, distance = anchor
    if not all(math.isfinite(value) for value in anchor):
        return ('nonfinite',)

    errors = []
    if not canonical_short_angle(pitch):
        errors.append('pitch_lattice')
    elif pitch < -89.0 or pitch > 89.0:
        errors.append('pitch_reach')
    if not canonical_short_angle(yaw):
        errors.append('yaw_lattice')
    if distance < 1.0 or distance > RUNE_HOOK_MAX_RAY:
        errors.append('distance')
    return tuple(errors)


def _load_legacy(path, data=None):
    if data is None:
        with open(path, 'rb') as stream:
            data = stream.read(runeio.MAX_V3_FILE_BYTES + 1)
    if len(data) < HEADER_SIZE:
        raise ValueError(f'file is {len(data)} bytes, shorter than '
                         f'{HEADER_SIZE}-byte rune header')
    actual_size = len(data)
    header = data[:HEADER_SIZE]

    magic, ver, ns, nl, raw_map = struct.unpack_from(HEADER_FMT, header, 0)
    flaws = []
    if magic != RUNE_MAGIC:
        flaws.append(f'BAD MAGIC {magic:#x}')
    if ver not in READABLE_RUNE_VERSIONS:
        flaws.append(f'BAD VERSION {ver} (known: 1, {RUNE_VERSION})')

    counts_ok = (0 < ns <= RUNE_MAX_SEEDS and
                 0 <= nl <= RUNE_MAX_LINKS)
    if not counts_ok:
        flaws.append(f'counts outside format limits: {ns} seeds, {nl} links')

    if b'\0' not in raw_map:
        mapname = raw_map.decode('ascii', 'replace')
        flaws.append('unterminated map name')
    else:
        raw_name = raw_map.split(b'\0', 1)[0]
        try:
            mapname = raw_name.decode('ascii')
        except UnicodeDecodeError:
            mapname = raw_name.decode('ascii', 'replace')
            flaws.append('non-ASCII map name')
    expected_map = os.path.splitext(os.path.basename(path))[0]
    if mapname.casefold() != expected_map.casefold():
        flaws.append(f'map identity mismatch: header={mapname!r} '
                     f'file={expected_map!r}')

    size_ok = False
    if counts_ok:
        expected_size = HEADER_SIZE + ns * SEED_SIZE + nl * LINK_SIZE
        size_ok = actual_size == expected_size
        if not size_ok:
            kind = ('truncated' if actual_size < expected_size
                    else 'trailing data')
            flaws.append(f'{kind}: {actual_size} bytes, '
                         f'expected {expected_size}')

    # Unknown magic/version or an invalid payload boundary is not safe to
    # reinterpret as this flat layout. Header flaws are still reported, but
    # graph-level checks wait for a complete supported payload.
    if (magic != RUNE_MAGIC or ver not in READABLE_RUNE_VERSIONS or
            not counts_ok or not size_ok):
        return magic, ver, mapname, ns, nl, None, None, flaws

    payload = data[HEADER_SIZE:expected_size]
    if len(payload) != expected_size - HEADER_SIZE:
        flaws.append('short payload read after exact-size check')
        return magic, ver, mapname, ns, nl, None, None, flaws

    off = HEADER_SIZE
    seeds = []
    for _ in range(ns):
        seeds.append(struct.unpack_from(SEED_FMT, data, off))
        off += SEED_SIZE
    links = []
    for _ in range(nl):
        links.append(struct.unpack_from(LINK_FMT, data, off))
        off += LINK_SIZE
    return magic, ver, mapname, ns, nl, seeds, links, flaws


def _load_v3(path, data=None):
    decoded = (runeio.read_v3(path) if data is None else
               runeio.decode_v3(data))
    expected_map = os.path.splitext(os.path.basename(path))[0]
    if decoded.header.map_name != expected_map:
        raise runeio.RuneWireError(
            contract.RLW_MAPNAME_MISMATCH,
            f'header={decoded.header.map_name!r}, file={expected_map!r}',
        )
    seeds = [(*seed.origin, seed.area_hint, seed.flags)
             for seed in decoded.seeds]
    links = [
        (link.source, link.destination, link.action, link.provenance,
         link.min_speed, link.heading, link.heading_slack, link.exit_speed,
         link.cost_ms, *link.suffix_anchor)
        for link in decoded.links
    ]
    metadata = {
        'payload_crc32': decoded.header.payload_crc32,
        'bsp_checksum': decoded.header.bsp_checksum,
        'entity_crc32': decoded.header.entity_crc32,
        'action_contract_crc32': decoded.header.action_contract_crc32,
        'physics_flags': decoded.header.physics_flags,
        'gravity': decoded.header.gravity,
        'airaccelerate': decoded.header.airaccelerate,
        'maxvelocity': decoded.header.maxvelocity,
        'pmove_substep_ms': decoded.header.pmove_substep_ms,
        'server_frame_ms': decoded.header.server_frame_ms,
        'host_physics_id': decoded.header.host_physics_id,
    }
    result = (decoded.header.magic, decoded.header.version,
              decoded.header.map_name, decoded.header.num_seeds,
              decoded.header.num_links, seeds, links, [])
    return result, metadata


def _load_with_metadata(path):
    # Route and decode one immutable byte snapshot.  Final runes are installed
    # by atomic rename; reopening the pathname after probing can otherwise
    # classify one inode and decode a different one.
    with open(path, 'rb') as stream:
        data = stream.read(runeio.MAX_V3_FILE_BYTES + 1)
    if runeio.looks_like_v3_prefix(data[:12]):
        if len(data) > runeio.MAX_V3_FILE_BYTES:
            raise runeio.RuneWireError(
                contract.RLW_BAD_FILE_SIZE,
                f'{len(data)} bytes exceeds {runeio.MAX_V3_FILE_BYTES}',
            )
        return _load_v3(path, data)
    return _load_legacy(path, data), None


def load(path):
    """Load a legacy forensic or structurally valid v3 graph.

    The historical eight-item return shape is intentionally retained. Header
    identity metadata is reported by :func:`lint` for v3 rather than widening
    this API and breaking callers that unpack it.
    """
    result, _ = _load_with_metadata(path)
    return result


def _objective_roots(path, mapname, seeds, linked, gamedir=None):
    """Resolve both map flag objectives with the runtime's seed metric."""
    if gamedir is None:
        rune_dir = os.path.dirname(os.path.abspath(path))
        gamedir = (os.path.dirname(rune_dir)
                   if os.path.basename(rune_dir).casefold() == 'maps'
                   else rune_dir)
    origins, source = flag_origins_with_source(
        os.path.abspath(gamedir), mapname)
    missing = [team for team in ('red', 'blue') if team not in origins]
    if missing:
        raise ValueError(f'{source}: missing {" and ".join(missing)} flag')
    roots = {}
    for team in ('red', 'blue'):
        seed, distance = nearest(seeds, origins[team], linked)
        if seed < 0:
            raise ValueError(f'{source}: cannot resolve {team} flag to a seed')
        roots[team] = (seed, distance)
    return roots, source


def lint(path, runtime_v2=False, gamedir=None, objective_root_indices=None,
         runtime_v3=False):
    name = os.path.basename(path)
    try:
        loaded, header_metadata = _load_with_metadata(path)
        magic, ver, mapname, ns, nl, seeds, links, flaws = loaded
    except (OSError, ValueError, struct.error) as e:
        flaw = f'unreadable rune: {e}'
        print(f'== {name}')
        print(f'   FLAW: {flaw}')
        return [flaw]

    if runtime_v2 and ver != RUNE_VERSION:
        flaws.append(f'deployment requires runtime v{RUNE_VERSION}, got v{ver}')
    if runtime_v3 and ver != RUNE_V3_VERSION:
        flaws.append(f'deployment requires runtime v{RUNE_V3_VERSION}, got v{ver}')
    if runtime_v2 and runtime_v3:
        flaws.append('deployment cannot require runtime v2 and v3 together')
    runtime_deployment = runtime_v2 or runtime_v3

    outdeg = collections.Counter()
    indeg = collections.Counter()
    dup = collections.Counter()
    acts = collections.Counter()
    if seeds is not None:
        bad_seed_geom = sum(
            1 for s in seeds
            if (not all(math.isfinite(v) for v in s[:3]) or
                not all(PMOVE_COORD_MIN <= v <= PMOVE_COORD_MAX
                        for v in s[:3])))
        bad_seed_flags = sum(
            1 for s in seeds if s[4] & ~(RSF_WATER | RSF_TOMBSTONE))
        bad_seed_hints = sum(1 for s in seeds if not 0 <= s[3] <= 255)
        nearby = 0
        seed_cells = collections.defaultdict(list)
        for i, seed in enumerate(seeds):
            if (not all(math.isfinite(v) for v in seed[:3]) or
                    not all(PMOVE_COORD_MIN <= v <= PMOVE_COORD_MAX
                            for v in seed[:3])):
                continue
            cell = (math.floor(seed[0] / 64.0),
                    math.floor(seed[1] / 64.0),
                    math.floor(seed[2] / 128.0))
            for dz in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        for j in seed_cells.get(
                                (cell[0] + dx, cell[1] + dy,
                                 cell[2] + dz), ()):
                            other = seeds[j]
                            zdelta = other[2] - seed[2]
                            xdelta = other[0] - seed[0]
                            ydelta = other[1] - seed[1]
                            if (-48.0 < zdelta < 48.0 and
                                    xdelta * xdelta + ydelta * ydelta <
                                    64.0 * 64.0 * 0.81):
                                nearby += 1
            seed_cells[cell].append(i)
        if bad_seed_geom:
            flaws.append(f'seeds outside finite pmove range: {bad_seed_geom}')
        if bad_seed_flags:
            flaws.append(f'seeds with unknown flags: {bad_seed_flags}')
        if bad_seed_hints:
            flaws.append(f'seeds with area_hint outside 0..255: '
                         f'{bad_seed_hints}')
        if nearby:
            flaws.append(f'near-duplicate seed pairs: {nearby}')
        if nl == RUNE_MAX_LINKS:
            flaws.append(f'link table saturated at format cap: {nl}')
        if ns == RUNE_MAX_SEEDS:
            flaws.append(f'seed table saturated at format cap: {ns}')

        self_links = zero_cost = huge_cost = bad_idx = 0
        bad_action = bad_prov = bad_anchor = bad_rj = bad_drop = 0
        bad_drop_cost = 0
        bad_water_dry_action = bad_water_special = bad_hook_schema = 0
        bad_swim = bad_momentum_jump = bad_unsupported_rj = 0
        bad_action_anchor = bad_tombstone_link = 0
        bad_hook_control = collections.Counter()
        runtime_unsupported = collections.Counter()
        hook_anchor_low = 0
        for link in links:
            fr, to, act, prov, minsp, hdg, slack, exsp, cost = link[:9]
            anchor = link[9:12]
            if ver == RUNE_V3_VERSION:
                action_label = CONTRACT_ACTION_SHORT_NAMES.get(act, f'?{act}')
            else:
                action_label = ACTS[act] if act <= RL_DOOR else f'?{act}'
            acts[action_label] += 1
            if (ver in LEGACY_ACTION_MAX and
                    act > LEGACY_ACTION_MAX[ver]):
                bad_action += 1
            if (ver in LEGACY_PROVENANCE_MAX and
                    prov > LEGACY_PROVENANCE_MAX[ver]):
                bad_prov += 1
            if (runtime_v3 and ver == RUNE_V3_VERSION and
                    not contract.is_runtime_supported(act)):
                runtime_unsupported[action_label] += 1
            enforce_controller_laws = (
                ver == RUNE_VERSION or
                (runtime_v3 and ver == RUNE_V3_VERSION and
                 contract.is_runtime_supported(act))
            )
            anchor_finite = all(math.isfinite(v) for v in anchor)
            if not anchor_finite and not (ver == RUNE_VERSION and
                                          act == RL_HOOK):
                bad_anchor += 1
            if (ver in READABLE_RUNE_VERSIONS and
                    act == RL_ROCKETJUMP and anchor_finite and
                    (anchor[0] * anchor[0] + anchor[1] * anchor[1] > 1.0 or
                     anchor[2] <= 0.0 or anchor[2] > 255.0)):
                bad_rj += 1
            if enforce_controller_laws and act == RL_HOOK:
                bad_hook_control.update(hook_control_errors(anchor))
            if not (0 <= fr < ns and 0 <= to < ns):
                bad_idx += 1
                continue
            if enforce_controller_laws and act == RL_DROP and anchor_finite:
                dx = anchor[0] - seeds[fr][0]
                dy = anchor[1] - seeds[fr][1]
                dz = anchor[2] - seeds[fr][2]
                lip_horiz = math.hypot(dx, dy)
                lip_yaw = math.degrees(math.atan2(dy, dx))
                stored_yaw = hdg * (360.0 / 256.0)
                yaw_delta = (lip_yaw - stored_yaw + 180.0) % 360.0 - 180.0
                if ((ver == RUNE_V3_VERSION and prov != RL_PROVEN) or
                        minsp != 0 or
                        slack != RUNE_DROP_CONTROL_MARKER or
                        not 2.0 <= lip_horiz <= 256.0 or
                        (seeds[fr][4] & RSF_WATER) or
                        abs(dz - 8.0) > 0.25 or
                        abs(yaw_delta) > 360.0 / 256.0):
                    bad_drop += 1
            if (runtime_v3 and ver == RUNE_V3_VERSION and act == RL_DROP and
                    (cost < contract.RUNE_PROOF_SERVER_FRAME_MS or
                     cost >= contract.RUNE_PROOF_DROP_TOTAL_MS or
                     cost % contract.RUNE_PROOF_SERVER_FRAME_MS != 0)):
                bad_drop_cost += 1
            if (enforce_controller_laws and
                    act in (RL_RUN, RL_JUMP, RL_DOOR) and
                    ((seeds[fr][4] | seeds[to][4]) & RSF_WATER)):
                bad_water_dry_action += 1
            from_water = bool(seeds[fr][4] & RSF_WATER)
            to_water = bool(seeds[to][4] & RSF_WATER)
            if (enforce_controller_laws and act == RL_HOOK and
                    (prov != RL_PROVEN or minsp != 0 or
                     (from_water and to_water) or
                     slack != (RUNE_WATER_HOOK_CONTROL_MARKER
                               if from_water else RUNE_HOOK_CONTROL_SLACK))):
                bad_hook_schema += 1
            if (ver == RUNE_VERSION and act == RL_ROCKETJUMP and
                    ((seeds[fr][4] | seeds[to][4]) & RSF_WATER)):
                bad_water_special += 1
            if (enforce_controller_laws and act == RL_SWIM and
                    (not ((seeds[fr][4] | seeds[to][4]) & RSF_WATER) or
                     minsp != 0 or hdg != 0 or slack != 0 or
                     ((ver == RUNE_V3_VERSION and prov != RL_PROVEN) or
                      (ver == RUNE_VERSION and
                       prov not in (RL_PROVEN, RL_ADJUSTED))) or
                     not anchor_finite or
                     any(v != 0.0 for v in anchor))):
                bad_swim += 1
            if enforce_controller_laws and act == RL_JUMP and minsp != 0:
                bad_momentum_jump += 1
            if ver == RUNE_VERSION and act == RL_ROCKETJUMP:
                bad_unsupported_rj += 1
            anchor_zero = anchor_finite and all(v == 0.0 for v in anchor)
            anchor_world = anchor_finite and all(
                PMOVE_COORD_MIN <= v <= PMOVE_COORD_MAX for v in anchor)
            anchor_dx = anchor[0] - seeds[fr][0] if anchor_finite else 0.0
            anchor_dy = anchor[1] - seeds[fr][1] if anchor_finite else 0.0
            anchor_dz = anchor[2] - seeds[fr][2] if anchor_finite else 0.0
            door_to_dx = seeds[to][0] - anchor[0] if anchor_finite else 0.0
            door_to_dy = seeds[to][1] - anchor[1] if anchor_finite else 0.0
            door_to_dz = seeds[to][2] - anchor[2] if anchor_finite else 0.0
            if enforce_controller_laws and (
                    (act == RL_RUN and not anchor_zero and not anchor_world) or
                    (act == RL_JUMP and not anchor_zero) or
                    # Trigger identity and mover-sweep proof need live map
                    # entities; lint enforces the serialized DOOR controls.
                    (act in (RL_LIFT, RL_TELEPORT, RL_DOOR) and
                     (not anchor_world or prov != RL_DECLARED or minsp != 0 or
                      hdg != 0 or slack != RUNE_DECLARED_CONTROL_MARKER or
                      exsp != 0)) or
                    (act == RL_DOOR and anchor_finite and
                     (any(v != int(v * 8.0) * 0.125 for v in anchor) or
                      math.hypot(anchor_dx, anchor_dy) > 320.0 or
                      abs(anchor_dz) > 48.0 or
                      math.hypot(door_to_dx, door_to_dy) > 768.0 or
                      abs(door_to_dz) > 96.0)) or
                    (act == RL_TELEPORT and anchor_finite and
                     (math.hypot(anchor_dx, anchor_dy) > 128.0 or
                      abs(anchor_dz) > 128.0))):
                bad_action_anchor += 1
            if (seeds[fr][4] & RSF_TOMBSTONE or
                    seeds[to][4] & RSF_TOMBSTONE):
                bad_tombstone_link += 1
            outdeg[fr] += 1
            indeg[to] += 1
            dup[(fr, to, act)] += 1
            if fr == to:
                self_links += 1
            if cost <= 0:
                zero_cost += 1
            if cost > 30000:
                huge_cost += 1
            if (ver == 1 and act == RL_HOOK and
                    anchor[2] < seeds[fr][2] and
                    seeds[to][2] > seeds[fr][2] + 40):
                # A CLIMB whose anchor is under the floor. Descending rides
                # naturally anchor low (lmctf16: 191 of 191 legitimate).
                hook_anchor_low += 1

        dups = sum(1 for count in dup.values() if count > 1)
        tombstones = {
            i for i, seed in enumerate(seeds)
            if ver in (RUNE_VERSION, RUNE_V3_VERSION) and
            seed[4] & RSF_TOMBSTONE
        }
        bad_ownership = sum(
            1 for i in range(ns)
            if ((i in tombstones) == bool(outdeg[i]))
        ) if ver in (RUNE_VERSION, RUNE_V3_VERSION) else 0
        if bad_ownership:
            flaws.append(f'seeds violating route-core ownership: '
                         f'{bad_ownership}')
        orphans = sum(1 for i in range(ns)
                      if i not in tombstones and
                      not outdeg[i] and not indeg[i])
        deadends = sum(1 for i in range(ns)
                       if i not in tombstones and
                       not outdeg[i] and indeg[i])
        sources = sum(1 for i in range(ns)
                      if i not in tombstones and
                      outdeg[i] and not indeg[i])

        # Connectivity in the direction the FIELDS flood: reverse
        # reachability (who can get TO a goal). The rune does not name its
        # objectives, so resolve the map's two flag entities exactly as the
        # runtime's eligibility/distance rules do; inspection alone retains
        # a graph-only fallback. The engine-only world trace remains the
        # final runtime authority when stacked solid geometry is ambiguous.
        radj = collections.defaultdict(list)
        linked = set()
        for link in links:
            if 0 <= link[0] < ns and 0 <= link[1] < ns:
                radj[link[1]].append(link[0])
                # Rune_Load marks linked_seed only for a link's source;
                # destination-only dead ends are not localization targets.
                linked.add(link[0])

        def rsweep(root):
            seen = {root}
            stack = [root]
            while stack:
                u = stack.pop()
                for v in radj[u]:
                    if v not in seen:
                        seen.add(v)
                        stack.append(v)
            return len(seen)

        objective_roots = {}
        objective_source = None
        objective_error = None
        if objective_root_indices is not None:
            errors = []
            for team, root in zip(('red', 'blue'), objective_root_indices):
                if not 0 <= root < ns:
                    errors.append(f'{team} objective root {root} is out of range')
                elif root not in linked:
                    errors.append(f'{team} objective root {root} is not routable')
                elif seeds[root][4] & RSF_TOMBSTONE:
                    errors.append(f'{team} objective root {root} is a tombstone')
                else:
                    objective_roots[team] = (root, None)
            if errors:
                objective_roots = {}
                objective_error = '; '.join(errors)
            else:
                objective_source = 'server post-spawn roots'
        elif runtime_deployment:
            objective_error = (
                'deployment requires --objective-roots RED BLUE from the '
                'generating server log')
        else:
            try:
                objective_roots, objective_source = _objective_roots(
                    path, mapname, seeds, linked, gamedir)
            except (OSError, ValueError, struct.error) as error:
                objective_error = str(error)

        if objective_roots:
            for team in ('red', 'blue'):
                root, distance = objective_roots[team]
                routable = ns - len(tombstones)
                unreachable = routable - rsweep(root)
                # A deployment graph is only safe when every seed can flow
                # to both objectives.  The 5% tolerance is useful for
                # exploratory inspection, but allowing it here would let
                # runegen install small, fully stranded graph islands.
                if ((runtime_deployment and unreachable) or
                        (not runtime_deployment and
                         unreachable > routable * 0.05)):
                    metric = (f', nearest metric {distance:.1f}'
                              if distance is not None else '')
                    flaws.append(
                        f'outside {team} flag reverse component (seed {root}'
                        f'{metric}): {unreachable} '
                        f'({100 * unreachable // max(1, routable)}%)')
        else:
            # Inspection without map assets retains the old graph-only
            # approximation. Deployment is stricter below: both map-derived
            # objective roots must be knowable.
            best = 0
            for root in range(0, ns, max(1, ns // 40)):
                best = max(best, rsweep(root))
            routable = ns - len(tombstones)
            unreach = routable - best
            if unreach > routable * 0.05:
                flaws.append(
                    f'outside best sampled reverse component: {unreach} '
                    f'({100 * unreach // max(1, routable)}%)')
        if runtime_deployment and objective_error:
            flaws.append(f'cannot validate flag objectives: {objective_error}')

        if runtime_unsupported:
            detail = ', '.join(
                f'{action}={count}'
                for action, count in sorted(runtime_unsupported.items()))
            flaws.append(f'runtime v3 unsupported actions: {detail}')

        if bad_idx:
            flaws.append(f'links with out-of-range seeds: {bad_idx}')
        if bad_tombstone_link:
            flaws.append(f'links touching route-core tombstones: '
                         f'{bad_tombstone_link}')
        if bad_action:
            flaws.append(f'links with unknown action: {bad_action}')
        if bad_prov:
            flaws.append(f'links with unknown provenance: {bad_prov}')
        if bad_anchor:
            flaws.append(f'links with non-finite anchor: {bad_anchor}')
        version_label = f'v{ver}'
        hook_labels = (
            ('nonfinite', f'{version_label} hook controls with non-finite values'),
            ('pitch_lattice',
             f'{version_label} hook controls with non-canonical pitch'),
            ('yaw_lattice',
             f'{version_label} hook controls with non-canonical yaw'),
            ('pitch_reach',
             f'{version_label} hook controls with Pmove-unreachable pitch'),
            ('distance',
             f'{version_label} hook controls with ray distance outside 1..8192'),
        )
        for key, label in hook_labels:
            if bad_hook_control[key]:
                flaws.append(f'{label}: {bad_hook_control[key]}')
        if bad_rj:
            flaws.append(f'rocket jumps with invalid envelope: {bad_rj}')
        if bad_drop:
            flaws.append(f'{version_label} drops with invalid lip control: '
                         f'{bad_drop}')
        if bad_drop_cost:
            flaws.append(f'v3 drops with invalid replay cost: {bad_drop_cost}')
        if bad_water_dry_action:
            flaws.append(f'{version_label} RUN/JUMP/DOOR links with water '
                         f'endpoint: '
                         f'{bad_water_dry_action}')
        if bad_water_special:
            flaws.append(f'v2 rocket jumps with invalid water endpoint: '
                         f'{bad_water_special}')
        if bad_hook_schema:
            flaws.append(f'{version_label} hooks with invalid provenance, '
                         f'speed, marker, or wet destination: '
                         f'{bad_hook_schema}')
        if bad_swim:
            flaws.append(f'{version_label} swims with malformed exact '
                         f'control: {bad_swim}')
        if bad_momentum_jump:
            flaws.append(f'{version_label} jumps with unsupported momentum '
                         f'envelope: '
                         f'{bad_momentum_jump}')
        if bad_unsupported_rj:
            flaws.append(f'v2 rocket jumps with unserialized launch state: '
                         f'{bad_unsupported_rj}')
        if bad_action_anchor:
            flaws.append(f'{version_label} links with invalid action '
                         f'anchor/control: '
                         f'{bad_action_anchor}')
        if self_links:
            flaws.append(f'self-links (from==to): {self_links}')
        if zero_cost:
            flaws.append(f'links with cost<=0 ms: {zero_cost}')
        if huge_cost:
            flaws.append(f'links with cost>30s: {huge_cost}')
        if dups:
            flaws.append(f'duplicate (from,to,action) triples: {dups}')
        if orphans:
            flaws.append(f'orphan seeds (no links at all): {orphans}')
        if deadends > ns * 0.02:
            flaws.append(f'dead-end seeds (in, no out): {deadends} '
                         f'({100 * deadends // ns}%)')
        if sources > ns * 0.02:
            flaws.append(f'source-only seeds (out, no in): {sources} '
                         f'({100 * sources // ns}%)')
        if hook_anchor_low:
            flaws.append(f'legacy v1 hook world anchors BELOW their firing floor: '
                         f'{hook_anchor_low}')

    objective_detail = ''
    if seeds is not None and objective_roots:
        objective_detail = ' objectives=' + ','.join(
            f'{team}:{objective_roots[team][0]}' for team in ('red', 'blue'))
        objective_detail += f' source={objective_source}'
    print(f'== {name}: version={ver} seeds={ns} links={nl} ' +
          ' '.join(f'{key}={value}' for key, value in sorted(acts.items())) +
          objective_detail)
    if header_metadata is not None:
        print('   HEADER: '
              f'bsp=0x{header_metadata["bsp_checksum"]:08x} '
              f'entity=0x{header_metadata["entity_crc32"]:08x} '
              f'payload=0x{header_metadata["payload_crc32"]:08x} '
              f'action_contract=0x{header_metadata["action_contract_crc32"]:08x} '
              f'physics_flags=0x{header_metadata["physics_flags"]:08x} '
              f'gravity={header_metadata["gravity"]:g} '
              f'airaccelerate={header_metadata["airaccelerate"]:g} '
              f'maxvelocity={header_metadata["maxvelocity"]:g} '
              f'pmove={header_metadata["pmove_substep_ms"]}ms '
              f'server={header_metadata["server_frame_ms"]}ms '
              f'host_physics_id={header_metadata["host_physics_id"]}')
    if ver == 1:
        print('   NOTE: legacy v1 layout is readable, but its hook proofs use '
              'the old 25 ms pull model; the current runtime requires v2')
    for flaw in flaws:
        print(f'   FLAW: {flaw}')
    if not flaws:
        print('   clean')
    return flaws


def main(argv=None):
    parser = argparse.ArgumentParser(
        description='Validate SLIPGATE rune format and graph invariants.')
    deployment = parser.add_mutually_exclusive_group()
    deployment.add_argument(
        '--runtime-v2', '--deployment', action='store_true',
        dest='runtime_v2',
        help='deployment gate: require v2 plus resolvable red/blue flag '
             'objectives')
    deployment.add_argument(
        '--runtime-v3', action='store_true',
        help='deployment gate: require structurally valid v3, reject actions '
             'without a live controller, and require resolvable red/blue '
             'flag objectives')
    parser.add_argument(
        '--gamedir',
        help='game directory used for approximate BSP/ENT objective lookup '
             'in inspection mode; inferred from a rune under maps/ when omitted')
    parser.add_argument(
        '--objective-roots', nargs=2, type=int, metavar=('RED', 'BLUE'),
        help='authoritative post-spawn red/blue seed indices printed by the '
             'generating server; required by a runtime deployment gate')
    parser.add_argument('patterns', nargs='*', metavar='RUNE_OR_GLOB')
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    patterns = args.patterns or [
        '/home/buzzkill/Games/Quake2/lmctf-hooktest/maps/*.rune']
    if args.objective_roots is not None:
        matched = [path for pattern in patterns
                   for path in sorted(glob.glob(pattern))]
        if len(matched) != 1:
            parser.error('--objective-roots requires exactly one matched rune')
    total = 0
    for pattern in patterns:
        paths = sorted(glob.glob(pattern))
        if not paths:
            print(f'== {pattern}')
            print('   FLAW: no files match')
            total += 1
        for path in paths:
            total += len(lint(path, runtime_v2=args.runtime_v2,
                              gamedir=args.gamedir,
                              objective_root_indices=args.objective_roots,
                              runtime_v3=args.runtime_v3))
    print(f'TOTAL FLAWS: {total}')
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main())
