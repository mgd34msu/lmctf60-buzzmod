#!/usr/bin/env python3
"""Accept an lmctf58 RUNE only when all declared route doors survive."""
from __future__ import annotations

import argparse
import collections
import json
import sys

try:
    import rune_contracts_generated as contract
    import runeio
except ModuleNotFoundError:  # Also support ``python -m tools...``.
    from tools import rune_contracts_generated as contract
    from tools import runeio


EXPECTED_CONTROLLERS = (
    "redfrontdoor", "bfrontdoor", "redgate", "bgate",
    "redcellardoor", "bcellardoor", "redcellardoor2", "bcellardoor2",
    "redcellardoor3", "bcellardoor3",
)


class AcceptanceError(ValueError):
    """The artifact is valid RUNE wire data but fails lmctf58 semantics."""


def _target_identity(artifact: runeio.RuneArtifact, node) -> str:
    if not node.target_offset:
        return ""
    return artifact.string_at(node.target_offset).strip().casefold()


def _reverse_reach(
    artifact: runeio.RuneArtifact, root: int, live: set[int]
) -> set[int]:
    if root not in live:
        raise AcceptanceError(f"objective root {root} is not a live seed")
    incoming: dict[int, list[int]] = collections.defaultdict(list)
    outdegree = collections.Counter()
    for link in artifact.links:
        if link.source in live and link.destination in live:
            incoming[link.destination].append(link.source)
            outdegree[link.source] += 1
    if not outdegree[root]:
        raise AcceptanceError(f"objective root {root} is not routable")
    reached = {root}
    pending = [root]
    while pending:
        destination = pending.pop()
        for source in incoming[destination]:
            if source not in reached:
                reached.add(source)
                pending.append(source)
    return reached


def validate(
    artifact: runeio.RuneArtifact, objective_roots: tuple[int, int]
) -> dict[str, object]:
    """Return stable evidence or raise when one required controller vanished."""
    if artifact.header.map_name.casefold() != "lmctf58":
        raise AcceptanceError(
            f"expected map lmctf58, got {artifact.header.map_name!r}"
        )
    if len(objective_roots) != 2:
        raise AcceptanceError("exactly two objective roots are required")

    live = {
        index for index, seed in enumerate(artifact.seeds)
        if not seed.flags & runeio.RSF_TOMBSTONE
    }
    red_reach = _reverse_reach(artifact, objective_roots[0], live)
    blue_reach = _reverse_reach(artifact, objective_roots[1], live)
    both = red_reach & blue_reach
    nodes = {node.key: node for node in artifact.activation_nodes}
    retained: dict[str, list[int]] = collections.defaultdict(list)

    for link_index, link in enumerate(artifact.links):
        if (
            link.action != contract.RL_DOOR
            or link.activation_plan == runeio.RUNE_NO_ACTIVATION_PLAN
        ):
            continue
        plan = artifact.activation_plans[link.activation_plan]
        if plan.controller_kind != runeio.RUNE_CONTROLLER_DIRECT_TRIGGER_DOOR:
            continue
        entry = nodes.get(plan.entry_key)
        if entry is None:
            continue
        identity = _target_identity(artifact, entry)
        if identity not in EXPECTED_CONTROLLERS:
            continue
        if link.source not in live or link.destination not in live:
            raise AcceptanceError(
                f"{identity} link {link_index} touches a tombstone"
            )
        if link.source not in both or link.destination not in both:
            raise AcceptanceError(
                f"{identity} link {link_index} is outside the two-flag route core"
            )
        retained[identity].append(link_index)

    missing = [name for name in EXPECTED_CONTROLLERS if not retained[name]]
    if missing:
        raise AcceptanceError(
            "missing required RL_DOOR controllers: " + ", ".join(missing)
        )
    return {
        "map_name": artifact.header.map_name,
        "objective_roots": list(objective_roots),
        "live_seeds": len(live),
        "two_flag_route_seeds": len(both),
        "controllers": {
            name: len(retained[name]) for name in EXPECTED_CONTROLLERS
        },
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--objective-roots", nargs=2, required=True, type=int,
        metavar=("RED", "BLUE"),
        help="authoritative roots printed by the generating server",
    )
    parser.add_argument("artifact")
    args = parser.parse_args(argv)
    try:
        artifact = runeio.read(args.artifact)
    except OSError as exc:
        print(f"lmctf58 gate infrastructure failure: {exc}", file=sys.stderr)
        return 3
    except ValueError as exc:
        print(f"lmctf58 gate infrastructure failure: {exc}", file=sys.stderr)
        return 3
    try:
        evidence = validate(artifact, tuple(args.objective_roots))
    except AcceptanceError as exc:
        print(json.dumps({
            "finding": str(exc),
            "map_name": artifact.header.map_name,
            "objective_roots": list(args.objective_roots),
            "status": "finding",
        }, sort_keys=True))
        return 1
    print(json.dumps(evidence, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
