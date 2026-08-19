#!/usr/bin/env python3
"""Validate the one RUNE artifact layout and its graph invariants."""
from __future__ import annotations

import argparse
import collections
import os

try:
    import runeio
except ModuleNotFoundError:
    from tools import runeio


def _metadata(artifact):
    header = artifact.header
    return {
        'payload_crc32': header.payload_crc32,
        'bsp_checksum': header.bsp_checksum,
        'entity_crc32': header.entity_crc32,
        'action_contract_crc32': header.action_contract_crc32,
        'physics_flags': header.physics_flags,
        'gravity': header.gravity,
        'airaccelerate': header.airaccelerate,
        'maxvelocity': header.maxvelocity,
        'pmove_substep_ms': header.pmove_substep_ms,
        'server_frame_ms': header.server_frame_ms,
        'host_physics_id': header.host_physics_id,
        'mechanism_contract_crc32': header.mechanism_contract_crc32,
        'activation_nodes': header.num_activation_nodes,
        'activation_edges': header.num_activation_edges,
        'activation_plans': header.num_activation_plans,
        'inventory_edges': header.num_inventory_edges,
        'trigger_count': artifact.trigger_count,
    }


def _as_tuple(artifact):
    seeds = [(*seed.origin, seed.area_hint, seed.flags) for seed in artifact.seeds]
    links = [
        (link.source, link.destination, link.action, link.provenance,
         link.min_speed, link.heading, link.heading_slack, link.exit_speed,
         link.cost_ms, *link.suffix_anchor)
        for link in artifact.links
    ]
    return (artifact.header.magic, artifact.header.map_name,
            artifact.header.num_seeds, artifact.header.num_links, seeds, links, [])


def _load_with_metadata(path):
    artifact = runeio.read(path)
    expected = os.path.splitext(os.path.basename(path))[0]
    if artifact.header.map_name != expected:
        raise ValueError(f'{path}: map identity mismatch')
    return _as_tuple(artifact), _metadata(artifact)


def load(path):
    return _as_tuple(runeio.read(path))


def _objective_reachability_flaws(artifact, objective_roots):
    flaws = []
    seed_count = len(artifact.seeds)
    live = {
        index for index, seed in enumerate(artifact.seeds)
        if not seed.flags & runeio.RSF_TOMBSTONE
    }
    sources = {link.source for link in artifact.links}
    reverse = collections.defaultdict(list)
    for link in artifact.links:
        reverse[link.destination].append(link.source)

    for team, root in zip(('red', 'blue'), objective_roots):
        if not 0 <= root < seed_count:
            flaws.append(f'{team} objective root {root} is out of range')
            continue
        if root not in live:
            flaws.append(f'{team} objective root {root} is a tombstone')
            continue
        if root not in sources:
            flaws.append(f'{team} objective root {root} is not routable')
            continue

        seen = {root}
        pending = [root]
        while pending:
            destination = pending.pop()
            for source in reverse[destination]:
                if source in live and source not in seen:
                    seen.add(source)
                    pending.append(source)
        unreachable = live - seen
        if unreachable:
            flaws.append(
                f'outside {team} flag reverse component (seed {root}): '
                f'{len(unreachable)} ({100 * len(unreachable) // max(1, len(live))}%)'
            )
    return flaws


def lint(path, *, objective_roots=None):
    try:
        artifact = runeio.read(path)
    except (OSError, ValueError) as exc:
        return [f'unreadable RUNE: {exc}']
    flaws = []
    outdegree = collections.Counter(link.source for link in artifact.links)
    for index, seed in enumerate(artifact.seeds):
        if not seed.flags & runeio.RSF_TOMBSTONE and not outdegree[index]:
            flaws.append(f'live seed {index} has no outgoing link')
    if objective_roots is not None:
        flaws.extend(_objective_reachability_flaws(artifact, objective_roots))
    return flaws


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--objective-roots', nargs=2, type=int, metavar=('RED', 'BLUE'),
        help='authoritative post-spawn flag seed indices; require every live '
             'seed to route to both objectives',
    )
    parser.add_argument('paths', nargs='+')
    args = parser.parse_args(argv)
    failed = False
    for path in args.paths:
        flaws = lint(path, objective_roots=args.objective_roots)
        if flaws:
            failed = True
            print(f'== {os.path.basename(path)}')
            for flaw in flaws:
                print(f'   FLAW: {flaw}')
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
