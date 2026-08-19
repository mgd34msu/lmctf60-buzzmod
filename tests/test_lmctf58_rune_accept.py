#!/usr/bin/env python3
"""Focused ten-controller and route-core acceptance regressions."""
from __future__ import annotations

from pathlib import Path
from types import SimpleNamespace
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import lmctf58_rune_accept as accept  # noqa: E402
import rune_contracts_generated as contract  # noqa: E402
import runeio  # noqa: E402


DISPLAY_NAMES = (
    "RedFrontDoor", "BFrontDoor", "RedGate", "BGate",
    "RedCellarDoor", "BCellarDoor", "RedCellarDoor2",
    "BCellarDoor2", "RedCellarDoor3", "BCellarDoor3",
)


def _node(key: int, target_offset: int) -> runeio.RuneActivationNode:
    return runeio.RuneActivationNode(
        key=key, kind=runeio.RUNE_NODE_TRIGGER,
        flags=runeio.RUNE_NODEF_REPEATABLE | runeio.RUNE_NODEF_TOUCHABLE,
        classname_offset=0, target_offset=target_offset,
        targetname_offset=0, killtarget_offset=0,
        owner_key=runeio.RUNE_NO_KEY, team_master_key=runeio.RUNE_NO_KEY,
        spawnflags=0, touch_callback=runeio.RUNE_CALLBACK_TOUCH_MULTI,
        use_callback=0, think_callback=0, blocked_callback=0,
        delay_ms=0, wait_ms=0, speed_q8=0, accel_q8=0, decel_q8=0,
        absmin_q8=(0, 0, 0), absmax_q8=(0, 0, 0),
        path_target_offset=0,
    )


def _link(
    source: int, destination: int, action: int, plan: int
) -> runeio.RuneLink:
    return runeio.RuneLink(
        source=source, destination=destination, action=action,
        provenance=(
            contract.RL_DECLARED if action == contract.RL_DOOR
            else contract.RL_PROVEN
        ),
        min_speed=0, heading=0, heading_slack=0, exit_speed=0,
        cost_ms=100, suffix_anchor=(0.0, 0.0, 0.0),
        mechanism_anchor=(0.0, 0.0, 0.0), sweep_clear_ms=0,
        mode=contract.RLCM_NONE, reserved=0, activation_plan=plan,
    )


def _artifact(*, missing: str | None = None, tombstone: bool = False):
    strings = bytearray(b"\0")
    nodes = []
    plans = []
    links = []
    for index, name in enumerate(DISPLAY_NAMES):
        if name.casefold() == missing:
            continue
        offset = len(strings)
        strings.extend(name.encode("ascii") + b"\0")
        key = 100 + index
        nodes.append(_node(key, offset))
        plan_index = len(plans)
        plans.append(runeio.RuneActivationPlan(
            entry_key=key, mover_key=200 + index, first_edge=0,
            num_edges=0,
            controller_kind=runeio.RUNE_CONTROLLER_DIRECT_TRIGGER_DOOR,
            flags=0, expected_members=1, cooldown_ms=0, closure_crc32=0,
        ))
        links.append(_link(0, 1, contract.RL_DOOR, plan_index))
    links.append(_link(1, 0, contract.RL_RUN, runeio.RUNE_NO_ACTIVATION_PLAN))
    return SimpleNamespace(
        header=SimpleNamespace(map_name="lmctf58"),
        seeds=(
            runeio.RuneSeed((0.0, 0.0, 0.0), flags=0),
            runeio.RuneSeed(
                (64.0, 0.0, 0.0),
                flags=runeio.RSF_TOMBSTONE if tombstone else 0,
            ),
        ),
        links=tuple(links), activation_nodes=tuple(nodes),
        activation_plans=tuple(plans), strings=bytes(strings),
        string_at=lambda offset: bytes(strings)[
            offset:bytes(strings).index(0, offset)
        ].decode("ascii"),
    )


class Lmctf58RuneAcceptTests(unittest.TestCase):
    def test_accepts_all_ten_distinct_direct_door_identities(self):
        evidence = accept.validate(_artifact(), (0, 1))
        self.assertEqual(10, len(evidence["controllers"]))
        self.assertTrue(all(
            count >= 1 for count in evidence["controllers"].values()
        ))
        self.assertEqual(2, evidence["two_flag_route_seeds"])

    def test_rejects_each_missing_required_controller(self):
        for display_name in DISPLAY_NAMES:
            identity = display_name.casefold()
            with self.subTest(identity=identity), self.assertRaisesRegex(
                accept.AcceptanceError, identity
            ):
                accept.validate(_artifact(missing=identity), (0, 1))

    def test_rejects_controller_link_outside_live_route_core(self):
        with self.assertRaisesRegex(
            accept.AcceptanceError, "not a live seed|not routable|tombstone"
        ):
            accept.validate(_artifact(tombstone=True), (0, 1))


if __name__ == "__main__":
    unittest.main()
