#!/usr/bin/env python3
"""Focused authentication and record tests for the rune RUNE reader."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
import io
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import rune_contracts_generated as contract  # noqa: E402
import runeio  # noqa: E402
import corpusgraph  # noqa: E402
import runelint  # noqa: E402
import runeview  # noqa: E402


HEADER_CRC_OFFSET = 60
PAYLOAD_CRC_OFFSET = 20
ACTION_CONTRACT_OFFSET = 32
HEADER_RESERVED_OFFSET = 134
MECHANISM_CONTRACT_OFFSET = 152
INVENTORY_COUNT_OFFSET = 156


def _fix_header_crc(data: bytearray) -> None:
    struct.pack_into("<I", data, HEADER_CRC_OFFSET, 0)
    struct.pack_into(
        "<I",
        data,
        HEADER_CRC_OFFSET,
        zlib.crc32(data[:runeio.RUNE_HEADER_BYTES]) & 0xFFFFFFFF,
    )


def _fix_payload_and_header_crc(data: bytearray) -> None:
    struct.pack_into(
        "<I",
        data,
        PAYLOAD_CRC_OFFSET,
        zlib.crc32(data[runeio.RUNE_HEADER_BYTES:]) & 0xFFFFFFFF,
    )
    _fix_header_crc(data)


def _wrap_rune_payload(
    payload: bytes,
    *,
    num_seeds: int,
    num_links: int,
    num_nodes: int,
    num_edges: int,
    num_plans: int,
    string_bytes: int,
    num_inventory_edges: int,
    map_name: str = "runetest",
) -> bytes:
    raw_name = map_name.encode("ascii")
    raw_map_name = raw_name + b"\0" * (64 - len(raw_name))
    header = runeio.HEADER_STRUCT.pack(
        runeio.RUNE_MAGIC,
        0,
        runeio.RUNE_HEADER_BYTES,
        runeio.RUNE_SEED_BYTES,
        runeio.RUNE_LINK_BYTES,
        num_seeds,
        num_links,
        zlib.crc32(payload) & 0xFFFFFFFF,
        0x12345678,
        0x9ABCDEF0,
        contract.RUNE_ACTION_CONTRACT_CRC32,
        contract.RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED,
        650.0,
        0.0,
        2000.0,
        contract.RUNE_PROOF_PMOVE_SUBSTEP_MS,
        contract.RUNE_PROOF_SERVER_FRAME_MS,
        1,
        0,
        raw_map_name,
    ) + runeio.RUNE_HEADER_EXTENSION_STRUCT.pack(
        runeio.RUNE_ACTIVATION_NODE_BYTES,
        runeio.RUNE_ACTIVATION_EDGE_BYTES,
        runeio.RUNE_ACTIVATION_PLAN_BYTES,
        0,
        num_nodes,
        num_edges,
        num_plans,
        string_bytes,
        contract.RUNE_MECHANISM_CONTRACT_CRC32,
        num_inventory_edges,
    )
    encoded = bytearray(header + payload)
    _fix_header_crc(encoded)
    return bytes(encoded)


def _build_rune() -> bytes:
    strings = (
        b"\0Door1\0door1\0func_button\0func_door\0trigger_multiple\0"
    )
    seeds = b"".join(
        (
            runeio.SEED_STRUCT.pack(0.0, 0.0, 0.0, 1, 0),
            runeio.SEED_STRUCT.pack(128.0, 0.0, 0.0, 2, 0),
        )
    )
    links = b"".join(
        (
            runeio.RUNE_LINK_STRUCT.pack(
                0,
                1,
                contract.RL_BUTTON_DOOR,
                contract.RL_DECLARED,
                0,
                0,
                0,
                0,
                100,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                -2.0,
                100,
                contract.RLCM_RIDE,
                0,
                0,
            ),
            runeio.RUNE_LINK_STRUCT.pack(
                1,
                0,
                contract.RL_RUN,
                contract.RL_PROVEN,
                0,
                0,
                0,
                0,
                100,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                0,
                contract.RLCM_NONE,
                0,
                runeio.RUNE_NO_ACTIVATION_PLAN,
            ),
        )
    )
    nodes = b"".join(
        (
            runeio.RUNE_ACTIVATION_NODE_STRUCT.pack(
                1,
                runeio.RUNE_NODE_BUTTON,
                2 | 4 | 8 | 16,
                13,
                1,
                0,
                0,
                runeio.RUNE_NO_KEY,
                runeio.RUNE_NO_KEY,
                0,
                3,
                5,
                0,
                0,
                0,
                3000,
                320,
                320,
                320,
                -64,
                -64,
                -16,
                64,
                64,
                64,
                0,
                0.0,
                0.0,
                0.0,
            ),
            runeio.RUNE_ACTIVATION_NODE_STRUCT.pack(
                2,
                runeio.RUNE_NODE_DOOR_MASTER,
                8 | 16 | 32,
                25,
                0,
                7,
                0,
                runeio.RUNE_NO_KEY,
                2,
                0,
                0,
                10,
                11,
                8,
                0,
                3000,
                800,
                800,
                800,
                0,
                0,
                0,
                256,
                128,
                512,
                0,
                0.0,
                0.0,
                0.0,
            ),
            runeio.RUNE_ACTIVATION_NODE_STRUCT.pack(
                3,
                runeio.RUNE_NODE_TRIGGER,
                runeio.RUNE_NODEF_INVENTORY_ONLY | 2 | 4,
                35,
                0,
                0,
                0,
                runeio.RUNE_NO_KEY,
                runeio.RUNE_NO_KEY,
                0,
                1,
                0,
                0,
                0,
                0,
                -1,
                0,
                0,
                0,
                -8,
                -8,
                -8,
                8,
                8,
                8,
                0,
                0.0,
                0.0,
                0.0,
            ),
        )
    )
    inventory_edge = runeio.RUNE_ACTIVATION_EDGE_STRUCT.pack(1, 2, 1, 0, 0)
    edges = inventory_edge + inventory_edge
    plan = runeio.RUNE_ACTIVATION_PLAN_STRUCT.pack(
        1,
        2,
        1,
        1,
        3,
        0,
        1 | 4 | 8,
        1,
        3000,
        zlib.crc32(inventory_edge) & 0xFFFFFFFF,
    )
    payload = seeds + links + nodes + edges + plan + strings
    return _wrap_rune_payload(
        payload,
        num_seeds=2,
        num_links=2,
        num_nodes=3,
        num_edges=2,
        num_plans=1,
        string_bytes=len(strings),
        num_inventory_edges=1,
    )


def _build_two_button_same_mover(*, second_plan: int = 1) -> bytes:
    base = _build_rune()[runeio.RUNE_HEADER_BYTES:]
    seed_bytes = 2 * runeio.RUNE_SEED_BYTES
    link_bytes = 2 * runeio.RUNE_LINK_BYTES
    node_bytes = 3 * runeio.RUNE_ACTIVATION_NODE_BYTES
    edge_bytes = 2 * runeio.RUNE_ACTIVATION_EDGE_BYTES
    plan_offset = seed_bytes + link_bytes + node_bytes + edge_bytes
    strings = base[plan_offset + runeio.RUNE_ACTIVATION_PLAN_BYTES:]
    first_link = base[seed_bytes:seed_bytes + runeio.RUNE_LINK_BYTES]
    reverse_link = base[
        seed_bytes + runeio.RUNE_LINK_BYTES:seed_bytes + link_bytes
    ]
    duplicate = list(runeio.RUNE_LINK_STRUCT.unpack(first_link))
    duplicate[-1] = second_plan
    links = first_link + runeio.RUNE_LINK_STRUCT.pack(*duplicate) + reverse_link
    nodes = base[seed_bytes + link_bytes:seed_bytes + link_bytes + node_bytes]
    second_button = bytearray(nodes[:runeio.RUNE_ACTIVATION_NODE_BYTES])
    struct.pack_into("<I", second_button, 0, 4)
    nodes += second_button
    inventory = b"".join((
        runeio.RUNE_ACTIVATION_EDGE_STRUCT.pack(1, 2, 1, 0, 0),
        runeio.RUNE_ACTIVATION_EDGE_STRUCT.pack(4, 2, 1, 0, 0),
    ))
    plan_values = list(runeio.RUNE_ACTIVATION_PLAN_STRUCT.unpack(
        base[plan_offset:plan_offset + runeio.RUNE_ACTIVATION_PLAN_BYTES]
    ))
    plan_values[2] = 2
    first_plan = runeio.RUNE_ACTIVATION_PLAN_STRUCT.pack(*plan_values)
    plan_values[0] = 4
    plan_values[2] = 3
    plan_values[-1] = zlib.crc32(inventory[16:]) & 0xFFFFFFFF
    plans = first_plan + runeio.RUNE_ACTIVATION_PLAN_STRUCT.pack(*plan_values)
    payload = base[:seed_bytes] + links + nodes + inventory * 2 + plans + strings
    return _wrap_rune_payload(
        payload,
        num_seeds=2,
        num_links=3,
        num_nodes=4,
        num_edges=4,
        num_plans=2,
        string_bytes=len(strings),
        num_inventory_edges=2,
        map_name="twobuttons",
    )


def _build_train_plan(
    controller: int,
    *,
    sealed_think: int = runeio.RUNE_CALLBACK_FUNC_TRAIN_FIND,
) -> bytes:
    values = (b"closed", b"func_button", b"func_train", b"gate", b"open")
    strings = b"\0" + b"".join(value + b"\0" for value in values)
    offsets: dict[bytes, int] = {}
    offset = 1
    for value in values:
        offsets[value] = offset
        offset += len(value) + 1

    seeds = b"".join((
        runeio.SEED_STRUCT.pack(0.0, 0.0, 0.0, 1, 0),
        runeio.SEED_STRUCT.pack(128.0, 0.0, 0.0, 2, 0),
    ))
    links = b"".join((
        runeio.RUNE_LINK_STRUCT.pack(
            0, 1, contract.RL_TRAIN, contract.RL_DECLARED,
            0, 0, 254, 0, 1000,
            8.0, 8.0, 8.0, 16.0, 16.0, 16.0,
            100, contract.RLCM_PREOPEN, 0, 0,
        ),
        runeio.RUNE_LINK_STRUCT.pack(
            1, 0, contract.RL_RUN, contract.RL_PROVEN,
            0, 0, 0, 0, 100,
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
            0, contract.RLCM_NONE, 0,
            runeio.RUNE_NO_ACTIVATION_PLAN,
        ),
    ))

    def node(
        key: int,
        kind: int,
        classname: bytes,
        *,
        flags: int,
        target: bytes,
        targetname: bytes | None = None,
        spawnflags: int = 0,
        touch_callback: int = 0,
        use_callback: int = 0,
        think_callback: int = 0,
        blocked_callback: int = 0,
        wait_ms: int = 0,
        speed_q8: int = 0,
    ) -> bytes:
        return runeio.RUNE_ACTIVATION_NODE_STRUCT.pack(
            key,
            kind,
            flags,
            offsets[classname],
            offsets[target],
            offsets[targetname] if targetname is not None else 0,
            0,
            runeio.RUNE_NO_KEY,
            runeio.RUNE_NO_KEY,
            spawnflags,
            touch_callback,
            use_callback,
            think_callback,
            blocked_callback,
            0,
            wait_ms,
            speed_q8,
            speed_q8,
            speed_q8,
            0,
            0,
            0,
            64,
            64,
            64,
            0,
            0.0,
            0.0,
            0.0,
        )

    shoot = controller == contract.SG_MECHANISM_CONTROLLER_TRAIN_SHOOT
    nodes = b"".join((
        node(
            1,
            runeio.RUNE_NODE_BUTTON,
            b"func_button",
            flags=(
                runeio.RUNE_NODEF_REPEATABLE |
                runeio.RUNE_NODEF_USABLE |
                runeio.RUNE_NODEF_MOVER |
                (runeio.RUNE_NODEF_SHOOTABLE if shoot else
                 runeio.RUNE_NODEF_TOUCHABLE)
            ),
            target=b"gate",
            touch_callback=(0 if shoot else runeio.RUNE_CALLBACK_BUTTON_TOUCH),
            use_callback=runeio.RUNE_CALLBACK_BUTTON_USE,
            wait_ms=3000,
        ),
        node(
            2,
            runeio.RUNE_NODE_TRAIN,
            b"func_train",
            flags=(
                runeio.RUNE_NODEF_REPEATABLE |
                runeio.RUNE_NODEF_USABLE |
                runeio.RUNE_NODEF_MOVER
            ),
            target=b"open",
            targetname=b"gate",
            spawnflags=2,
            use_callback=runeio.RUNE_CALLBACK_TRAIN_USE,
            think_callback=sealed_think,
            blocked_callback=runeio.RUNE_CALLBACK_BLOCKED_TRAIN,
            speed_q8=240,
        ),
        node(
            3,
            runeio.RUNE_NODE_PATH_CORNER,
            b"open",
            flags=runeio.RUNE_NODEF_TOUCHABLE | runeio.RUNE_NODEF_ONE_SHOT,
            target=b"closed",
            targetname=b"open",
            touch_callback=runeio.RUNE_CALLBACK_PATH_CORNER_TOUCH,
            wait_ms=-1000,
        ),
        node(
            4,
            runeio.RUNE_NODE_PATH_CORNER,
            b"closed",
            flags=runeio.RUNE_NODEF_TOUCHABLE | runeio.RUNE_NODEF_ONE_SHOT,
            target=b"open",
            targetname=b"closed",
            touch_callback=runeio.RUNE_CALLBACK_PATH_CORNER_TOUCH,
            wait_ms=-1000,
        ),
    ))
    inventory_rows = (
        (1, 2, runeio.RUNE_EDGE_TARGET, 0, 0),
        (2, 3, runeio.RUNE_EDGE_ROUTE_TARGET, 0, 0),
        (3, 4, runeio.RUNE_EDGE_ROUTE_TARGET, 0, 0),
        (4, 3, runeio.RUNE_EDGE_ROUTE_TARGET, 0, 0),
    )
    inventory = tuple(
        runeio.RUNE_ACTIVATION_EDGE_STRUCT.pack(*row)
        for row in inventory_rows
    )
    plan_edges = inventory[:2] + (inventory[3], inventory[2])
    plan_edge_bytes = b"".join(plan_edges)
    plan = runeio.RUNE_ACTIVATION_PLAN_STRUCT.pack(
        1,
        2,
        len(inventory),
        len(plan_edges),
        controller,
        0,
        contract.mechanism_controller_plan_flags(controller),
        1,
        3000,
        zlib.crc32(plan_edge_bytes) & 0xFFFFFFFF,
    )
    payload = (
        seeds + links + nodes + b"".join(inventory) + plan_edge_bytes +
        plan + strings
    )
    return _wrap_rune_payload(
        payload,
        num_seeds=2,
        num_links=2,
        num_nodes=4,
        num_edges=8,
        num_plans=1,
        string_bytes=len(strings),
        num_inventory_edges=4,
        map_name="trainplan",
    )


def _build_edge_catalog() -> bytes:
    strings = b"\0a\0b\0"
    seeds = b"".join(
        (
            runeio.SEED_STRUCT.pack(0.0, 0.0, 0.0, 1, 0),
            runeio.SEED_STRUCT.pack(128.0, 0.0, 0.0, 2, 0),
        )
    )
    links = b"".join(
        runeio.RUNE_LINK_STRUCT.pack(
            source,
            destination,
            contract.RL_RUN,
            contract.RL_PROVEN,
            0,
            0,
            0,
            0,
            100,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0,
            contract.RLCM_NONE,
            0,
            runeio.RUNE_NO_ACTIVATION_PLAN,
        )
        for source, destination in ((0, 1), (1, 0))
    )

    def node(
        key: int, classname_offset: int, delay_ms: int, wait_ms: int
    ) -> bytes:
        return runeio.RUNE_ACTIVATION_NODE_STRUCT.pack(
            key,
            (runeio.RUNE_NODE_TARGET_SPEAKER if key == 1 else
             runeio.RUNE_NODE_AREAPORTAL),
            runeio.RUNE_NODEF_INVENTORY_ONLY,
            classname_offset,
            0,
            0,
            0,
            runeio.RUNE_NO_KEY,
            runeio.RUNE_NO_KEY,
            0,
            0,
            32 if key == 1 else 33,
            0,
            0,
            delay_ms,
            wait_ms,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            3 if key == 1 else 0,
            0.0,
            0.0,
            0.0,
        )

    nodes = node(1, 1, -(1 << 31), (1 << 31) - 1) + node(2, 3, 0, 0)
    edge_kinds = (
        runeio.RUNE_EDGE_TARGET,
        runeio.RUNE_EDGE_KILLTARGET,
        runeio.RUNE_EDGE_OWNER,
        runeio.RUNE_EDGE_TEAM,
        runeio.RUNE_EDGE_PATH_TARGET,
        runeio.RUNE_EDGE_MOVE_TARGET,
        runeio.RUNE_EDGE_TARGET_ENT,
        runeio.RUNE_EDGE_ENEMY,
        runeio.RUNE_EDGE_ROUTE_TARGET,
    )
    edges = b"".join(
        runeio.RUNE_ACTIVATION_EDGE_STRUCT.pack(
            0,
            2,
            kind,
            0,
            0xFFFFFFFF if kind == runeio.RUNE_EDGE_ENEMY else 0,
        )
        for kind in edge_kinds
    )
    payload = seeds + links + nodes + edges + strings
    return _wrap_rune_payload(
        payload,
        num_seeds=2,
        num_links=2,
        num_nodes=2,
        num_edges=9,
        num_plans=0,
        string_bytes=len(strings),
        num_inventory_edges=9,
        map_name="edgecatalog",
    )


def _build_direct_door(
    *,
    entry_order: tuple[int, ...] = (2, 3, 4, 5),
    speaker_spawnflags: int = 0,
    relay_delay_ms: int = 0,
    door_spawnflags: int = 2 | 4,
    relay_door: bool = False,
    relay_slave_first: bool = False,
) -> bytes:
    """Build a complete direct-trigger door and benign-effect closure."""

    values = (
        b"door_effect",
        b"door_relay_sound",
        b"fan",
        b"func_areaportal",
        b"func_door",
        b"member_effect",
        *((b"relay_door",) if relay_door else ()),
        b"relay_sound",
        b"target_speaker",
        b"trigger_multiple",
        b"trigger_relay",
    )
    strings = b"\0" + b"".join(value + b"\0" for value in values)
    offsets: dict[bytes, int] = {}
    offset = 1
    for value in values:
        offsets[value] = offset
        offset += len(value) + 1

    seeds = b"".join(
        (
            runeio.SEED_STRUCT.pack(0.0, 0.0, 0.0, 1, 0),
            runeio.SEED_STRUCT.pack(128.0, 0.0, 0.0, 2, 0),
        )
    )
    links = b"".join(
        (
            runeio.RUNE_LINK_STRUCT.pack(
                0, 1, contract.RL_DOOR, contract.RL_DECLARED,
                0, 0, 0, 0, 100,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0, contract.RLCM_NONE, 0, 0,
            ),
            runeio.RUNE_LINK_STRUCT.pack(
                1, 0, contract.RL_RUN, contract.RL_PROVEN,
                0, 0, 0, 0, 100,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0, contract.RLCM_NONE, 0,
                runeio.RUNE_NO_ACTIVATION_PLAN,
            ),
        )
    )

    def node(
        key: int,
        kind: int,
        classname: bytes,
        *,
        flags: int = runeio.RUNE_NODEF_USABLE,
        target: bytes | None = None,
        targetname: bytes | None = None,
        team_master_key: int = runeio.RUNE_NO_KEY,
        spawnflags: int = 0,
        touch_callback: int = 0,
        use_callback: int = 0,
        think_callback: int = 0,
        blocked_callback: int = 0,
        delay_ms: int = 0,
        wait_ms: int = 0,
        speed_q8: int = 0,
    ) -> bytes:
        return runeio.RUNE_ACTIVATION_NODE_STRUCT.pack(
            key,
            kind,
            flags,
            offsets[classname],
            offsets[target] if target is not None else 0,
            offsets[targetname] if targetname is not None else 0,
            0,
            runeio.RUNE_NO_KEY,
            team_master_key,
            spawnflags,
            touch_callback,
            use_callback,
            think_callback,
            blocked_callback,
            delay_ms,
            wait_ms,
            speed_q8,
            speed_q8,
            speed_q8,
            0,
            0,
            0,
            64,
            64,
            64,
            0,
            0.0,
            0.0,
            0.0,
        )

    nodes = b"".join(
        (
            node(
                1, runeio.RUNE_NODE_TRIGGER, b"trigger_multiple",
                flags=(runeio.RUNE_NODEF_REPEATABLE |
                       runeio.RUNE_NODEF_TOUCHABLE),
                target=b"fan",
                touch_callback=runeio.RUNE_CALLBACK_TOUCH_MULTI,
                wait_ms=1000,
            ),
            node(
                2, runeio.RUNE_NODE_DOOR_MASTER, b"func_door",
                flags=(runeio.RUNE_NODEF_USABLE |
                       runeio.RUNE_NODEF_MOVER |
                       runeio.RUNE_NODEF_TEAM_MASTER),
                target=b"door_effect",
                targetname=(b"relay_door" if relay_door else b"fan"),
                team_master_key=2,
                spawnflags=door_spawnflags,
                use_callback=runeio.RUNE_CALLBACK_USE_DOOR,
                think_callback=runeio.RUNE_CALLBACK_THINK_CALC_MOVE_SPEED,
                blocked_callback=runeio.RUNE_CALLBACK_BLOCKED_DOOR,
                wait_ms=3000,
                speed_q8=800,
            ),
            node(
                3, runeio.RUNE_NODE_DOOR_MEMBER, b"func_door",
                flags=(runeio.RUNE_NODEF_USABLE |
                       runeio.RUNE_NODEF_MOVER |
                       runeio.RUNE_NODEF_TEAM_MEMBER),
                target=b"member_effect",
                targetname=(b"relay_door" if relay_door else b"fan"),
                team_master_key=2,
                spawnflags=door_spawnflags,
                use_callback=runeio.RUNE_CALLBACK_USE_DOOR,
                think_callback=(
                    runeio.RUNE_CALLBACK_THINK_CALC_MOVE_SPEED),
                blocked_callback=runeio.RUNE_CALLBACK_BLOCKED_DOOR,
                wait_ms=3000,
                speed_q8=800,
            ),
            node(
                4, runeio.RUNE_NODE_TARGET_SPEAKER, b"target_speaker",
                targetname=b"fan",
                spawnflags=speaker_spawnflags,
                use_callback=runeio.RUNE_CALLBACK_USE_TARGET_SPEAKER,
            ),
            node(
                5, runeio.RUNE_NODE_RELAY, b"trigger_relay",
                target=(b"relay_door" if relay_door else b"relay_sound"),
                targetname=b"fan",
                use_callback=runeio.RUNE_CALLBACK_USE_TRIGGER_RELAY,
                delay_ms=relay_delay_ms,
            ),
            node(
                6, runeio.RUNE_NODE_TARGET_SPEAKER, b"target_speaker",
                targetname=b"relay_sound",
                use_callback=runeio.RUNE_CALLBACK_USE_TARGET_SPEAKER,
            ),
            node(
                7, runeio.RUNE_NODE_AREAPORTAL, b"func_areaportal",
                targetname=b"door_effect",
                use_callback=runeio.RUNE_CALLBACK_USE_AREAPORTAL,
            ),
            node(
                8, runeio.RUNE_NODE_TARGET_SPEAKER, b"target_speaker",
                targetname=b"door_effect",
                use_callback=runeio.RUNE_CALLBACK_USE_TARGET_SPEAKER,
            ),
            node(
                9, runeio.RUNE_NODE_RELAY, b"trigger_relay",
                target=b"door_relay_sound",
                targetname=b"door_effect",
                use_callback=runeio.RUNE_CALLBACK_USE_TRIGGER_RELAY,
            ),
            node(
                10, runeio.RUNE_NODE_TARGET_SPEAKER, b"target_speaker",
                targetname=b"member_effect",
                use_callback=runeio.RUNE_CALLBACK_USE_TARGET_SPEAKER,
            ),
            node(
                11, runeio.RUNE_NODE_TARGET_SPEAKER, b"target_speaker",
                targetname=b"door_relay_sound",
                use_callback=runeio.RUNE_CALLBACK_USE_TARGET_SPEAKER,
            ),
        )
    )

    relay_rows = (
        [
            (5, destination, runeio.RUNE_EDGE_TARGET, ordinal, 0)
            for ordinal, destination in enumerate(
                (3, 2) if relay_slave_first else (2, 3)
            )
        ]
        if relay_door else
        [(5, 6, runeio.RUNE_EDGE_TARGET, 0, 0)]
    )
    effective_entry_order = (4, 5) if relay_door else entry_order
    edge_rows = [
        *( (1, destination, runeio.RUNE_EDGE_TARGET, ordinal, 0)
           for ordinal, destination in enumerate(effective_entry_order) ),
        (2, 7, runeio.RUNE_EDGE_TARGET, 0, 0),
        (2, 8, runeio.RUNE_EDGE_TARGET, 1, 0),
        (2, 9, runeio.RUNE_EDGE_TARGET, 2, 0),
        (2, 3, runeio.RUNE_EDGE_TEAM, 0, 0),
        (3, 10, runeio.RUNE_EDGE_TARGET, 0, 0),
        *relay_rows,
        (9, 11, runeio.RUNE_EDGE_TARGET, 0, 0),
    ]
    inventory = tuple(
        runeio.RUNE_ACTIVATION_EDGE_STRUCT.pack(*row) for row in edge_rows
    )
    inventory_bytes = b"".join(inventory)
    plan = runeio.RUNE_ACTIVATION_PLAN_STRUCT.pack(
        1,
        2,
        len(inventory),
        len(inventory),
        runeio.RUNE_CONTROLLER_DIRECT_TRIGGER_DOOR,
        0,
        contract.mechanism_controller_plan_flags(
            runeio.RUNE_CONTROLLER_DIRECT_TRIGGER_DOOR),
        2,
        1000,
        zlib.crc32(inventory_bytes) & 0xFFFFFFFF,
    )
    payload = seeds + links + nodes + inventory_bytes * 2 + plan + strings
    return _wrap_rune_payload(
        payload,
        num_seeds=2,
        num_links=2,
        num_nodes=11,
        num_edges=2 * len(inventory),
        num_plans=1,
        string_bytes=len(strings),
        num_inventory_edges=len(inventory),
        map_name="directdoor",
    )


def _build_delayed_terminal_door(
    *,
    relay_edge_delay_ms: int = 311000,
    relay_use_callback: int = runeio.RUNE_CALLBACK_USE_TRIGGER_RELAY,
    cycle: bool = False,
) -> bytes:
    """Build the lmctf58 312/311 relay-before-door closure."""

    values = (
        b"close",
        b"fan",
        b"func_door",
        b"nested",
        b"open",
        b"target_speaker",
        b"trigger_multiple",
        b"trigger_relay",
    )
    strings = b"\0" + b"".join(value + b"\0" for value in values)
    offsets: dict[bytes, int] = {}
    offset = 1
    for value in values:
        offsets[value] = offset
        offset += len(value) + 1

    seeds = b"".join((
        runeio.SEED_STRUCT.pack(0.0, 0.0, 0.0, 1, 0),
        runeio.SEED_STRUCT.pack(128.0, 0.0, 0.0, 2, 0),
    ))
    links = b"".join((
        runeio.RUNE_LINK_STRUCT.pack(
            0, 1, contract.RL_DOOR, contract.RL_DECLARED,
            0, 0, 0, 0, 14900,
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
            0, contract.RLCM_NONE, 0, 0,
        ),
        runeio.RUNE_LINK_STRUCT.pack(
            1, 0, contract.RL_RUN, contract.RL_PROVEN,
            0, 0, 0, 0, 100,
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
            0, contract.RLCM_NONE, 0,
            runeio.RUNE_NO_ACTIVATION_PLAN,
        ),
    ))

    def node(
        key: int,
        kind: int,
        classname: bytes,
        *,
        flags: int = runeio.RUNE_NODEF_USABLE,
        target: bytes | None = None,
        targetname: bytes | None = None,
        team_master_key: int = runeio.RUNE_NO_KEY,
        spawnflags: int = 0,
        touch_callback: int = 0,
        use_callback: int = 0,
        think_callback: int = 0,
        blocked_callback: int = 0,
        delay_ms: int = 0,
        wait_ms: int = 0,
        speed_q8: int = 0,
    ) -> bytes:
        return runeio.RUNE_ACTIVATION_NODE_STRUCT.pack(
            key,
            kind,
            flags,
            offsets[classname],
            offsets[target] if target is not None else 0,
            offsets[targetname] if targetname is not None else 0,
            0,
            runeio.RUNE_NO_KEY,
            team_master_key,
            spawnflags,
            touch_callback,
            use_callback,
            think_callback,
            blocked_callback,
            delay_ms,
            wait_ms,
            speed_q8,
            speed_q8,
            speed_q8,
            0,
            0,
            0,
            64,
            64,
            64,
            0,
            0.0,
            0.0,
            0.0,
        )

    nodes = b"".join((
        node(
            1, runeio.RUNE_NODE_TRIGGER, b"trigger_multiple",
            flags=(runeio.RUNE_NODEF_REPEATABLE |
                   runeio.RUNE_NODEF_TOUCHABLE),
            target=b"fan",
            touch_callback=runeio.RUNE_CALLBACK_TOUCH_MULTI,
            wait_ms=312000,
        ),
        node(
            2, runeio.RUNE_NODE_RELAY, b"trigger_relay",
            target=b"nested",
            targetname=b"fan",
            use_callback=relay_use_callback,
            delay_ms=311000,
        ),
        node(
            3, runeio.RUNE_NODE_DOOR_MASTER, b"func_door",
            flags=(runeio.RUNE_NODEF_USABLE |
                   runeio.RUNE_NODEF_MOVER |
                   runeio.RUNE_NODEF_TEAM_MASTER),
            target=b"open",
            targetname=b"fan",
            team_master_key=3,
            spawnflags=2 | 4,
            use_callback=runeio.RUNE_CALLBACK_USE_DOOR,
            think_callback=runeio.RUNE_CALLBACK_THINK_CALC_MOVE_SPEED,
            blocked_callback=runeio.RUNE_CALLBACK_BLOCKED_DOOR,
            wait_ms=300000,
            speed_q8=120,
        ),
        node(
            4, runeio.RUNE_NODE_TARGET_SPEAKER, b"target_speaker",
            targetname=b"open",
            use_callback=runeio.RUNE_CALLBACK_USE_TARGET_SPEAKER,
        ),
        node(
            5, runeio.RUNE_NODE_RELAY, b"trigger_relay",
            target=b"fan" if cycle else b"close",
            targetname=b"nested",
            use_callback=runeio.RUNE_CALLBACK_USE_TRIGGER_RELAY,
        ),
        node(
            6, runeio.RUNE_NODE_TARGET_SPEAKER, b"target_speaker",
            targetname=b"close",
            use_callback=runeio.RUNE_CALLBACK_USE_TARGET_SPEAKER,
        ),
    ))
    inventory_rows = [
        (1, 2, runeio.RUNE_EDGE_TARGET, 0, 0),
        (1, 3, runeio.RUNE_EDGE_TARGET, 1, 0),
        (2, 5, runeio.RUNE_EDGE_TARGET, 0, relay_edge_delay_ms),
        (3, 4, runeio.RUNE_EDGE_TARGET, 0, 0),
    ]
    if cycle:
        inventory_rows.extend((
            (5, 2, runeio.RUNE_EDGE_TARGET, 0, 0),
            (5, 3, runeio.RUNE_EDGE_TARGET, 1, 0),
        ))
    else:
        inventory_rows.append(
            (5, 6, runeio.RUNE_EDGE_TARGET, 0, 0)
        )
    inventory = tuple(
        runeio.RUNE_ACTIVATION_EDGE_STRUCT.pack(*row)
        for row in inventory_rows
    )
    plan_rows = inventory[:2] + (inventory[3],)
    plan_bytes = b"".join(plan_rows)
    plan = runeio.RUNE_ACTIVATION_PLAN_STRUCT.pack(
        1,
        3,
        len(inventory),
        len(plan_rows),
        runeio.RUNE_CONTROLLER_DIRECT_TRIGGER_DOOR,
        0,
        contract.mechanism_controller_plan_flags(
            runeio.RUNE_CONTROLLER_DIRECT_TRIGGER_DOOR),
        1,
        runeio.RUNE_MAX_TIME_MS,
        zlib.crc32(plan_bytes) & 0xFFFFFFFF,
    )
    all_edges = b"".join(inventory) + plan_bytes
    payload = seeds + links + nodes + all_edges + plan + strings
    return _wrap_rune_payload(
        payload,
        num_seeds=2,
        num_links=2,
        num_nodes=6,
        num_edges=len(inventory) + len(plan_rows),
        num_plans=1,
        string_bytes=len(strings),
        num_inventory_edges=len(inventory),
        map_name="delayeddoor",
    )


def _build_button_team(
    *,
    entry_order: tuple[int, int] = (2, 3),
    plan_order: tuple[int, int] | None = None,
) -> bytes:
    """Build the stock master-then-slave func_button fanout."""

    values = (b"blue", b"func_button", b"func_door")
    strings = b"\0" + b"".join(value + b"\0" for value in values)
    offsets: dict[bytes, int] = {}
    offset = 1
    for value in values:
        offsets[value] = offset
        offset += len(value) + 1

    seeds = b"".join((
        runeio.SEED_STRUCT.pack(0.0, 0.0, 0.0, 1, 0),
        runeio.SEED_STRUCT.pack(128.0, 0.0, 0.0, 2, 0),
    ))
    links = b"".join((
        runeio.RUNE_LINK_STRUCT.pack(
            0, 1, contract.RL_BUTTON_DOOR, contract.RL_DECLARED,
            0, 0, 0, 0, 100,
            0.0, 0.0, 0.0, 0.0, 0.0, -2.0,
            100, contract.RLCM_RIDE, 0, 0,
        ),
        runeio.RUNE_LINK_STRUCT.pack(
            1, 0, contract.RL_RUN, contract.RL_PROVEN,
            0, 0, 0, 0, 100,
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
            0, contract.RLCM_NONE, 0,
            runeio.RUNE_NO_ACTIVATION_PLAN,
        ),
    ))

    def node(
        key: int,
        kind: int,
        classname: bytes,
        *,
        flags: int,
        target: bytes | None = None,
        targetname: bytes | None = None,
        team_master_key: int = runeio.RUNE_NO_KEY,
        touch_callback: int = 0,
        use_callback: int = 0,
        think_callback: int = 0,
        blocked_callback: int = 0,
        wait_ms: int = 0,
        speed_q8: int = 0,
    ) -> bytes:
        return runeio.RUNE_ACTIVATION_NODE_STRUCT.pack(
            key,
            kind,
            flags,
            offsets[classname],
            offsets[target] if target is not None else 0,
            offsets[targetname] if targetname is not None else 0,
            0,
            runeio.RUNE_NO_KEY,
            team_master_key,
            0,
            touch_callback,
            use_callback,
            think_callback,
            blocked_callback,
            0,
            wait_ms,
            speed_q8,
            speed_q8,
            speed_q8,
            0,
            0,
            0,
            64,
            64,
            64,
            0,
            0.0,
            0.0,
            0.0,
        )

    nodes = b"".join((
        node(
            1,
            runeio.RUNE_NODE_BUTTON,
            b"func_button",
            flags=(
                runeio.RUNE_NODEF_REPEATABLE |
                runeio.RUNE_NODEF_TOUCHABLE |
                runeio.RUNE_NODEF_USABLE |
                runeio.RUNE_NODEF_MOVER
            ),
            target=b"blue",
            touch_callback=runeio.RUNE_CALLBACK_BUTTON_TOUCH,
            use_callback=runeio.RUNE_CALLBACK_BUTTON_USE,
            wait_ms=3000,
            speed_q8=320,
        ),
        node(
            2,
            runeio.RUNE_NODE_DOOR_MASTER,
            b"func_door",
            flags=(
                runeio.RUNE_NODEF_USABLE |
                runeio.RUNE_NODEF_MOVER |
                runeio.RUNE_NODEF_TEAM_MASTER
            ),
            targetname=b"blue",
            team_master_key=2,
            use_callback=runeio.RUNE_CALLBACK_USE_DOOR,
            think_callback=runeio.RUNE_CALLBACK_THINK_CALC_MOVE_SPEED,
            blocked_callback=runeio.RUNE_CALLBACK_BLOCKED_DOOR,
            wait_ms=3000,
            speed_q8=800,
        ),
        node(
            3,
            runeio.RUNE_NODE_DOOR_MEMBER,
            b"func_door",
            flags=(
                runeio.RUNE_NODEF_USABLE |
                runeio.RUNE_NODEF_MOVER |
                runeio.RUNE_NODEF_TEAM_MEMBER
            ),
            targetname=b"blue",
            team_master_key=2,
            use_callback=runeio.RUNE_CALLBACK_USE_DOOR,
            think_callback=runeio.RUNE_CALLBACK_THINK_CALC_MOVE_SPEED,
            blocked_callback=runeio.RUNE_CALLBACK_BLOCKED_DOOR,
            wait_ms=3000,
            speed_q8=800,
        ),
    ))

    target_rows = tuple(
        (1, destination, runeio.RUNE_EDGE_TARGET, ordinal, 0)
        for ordinal, destination in enumerate(entry_order)
    )
    inventory_rows = target_rows + (
        (2, 3, runeio.RUNE_EDGE_TEAM, 0, 0),
    )
    inventory = tuple(
        runeio.RUNE_ACTIVATION_EDGE_STRUCT.pack(*row)
        for row in inventory_rows
    )
    if plan_order is None:
        plan_targets = inventory[:2]
    else:
        remaining = list(zip(target_rows, inventory[:2]))
        selected: list[bytes] = []
        for destination in plan_order:
            match = next(
                index for index, (row, _raw) in enumerate(remaining)
                if row[1] == destination
            )
            _row, raw = remaining.pop(match)
            selected.append(raw)
        plan_targets = tuple(selected)
    plan_edges = plan_targets + (inventory[2],)
    inventory_bytes = b"".join(inventory)
    plan_edge_bytes = b"".join(plan_edges)
    plan = runeio.RUNE_ACTIVATION_PLAN_STRUCT.pack(
        1,
        2,
        len(inventory),
        len(plan_edges),
        runeio.RUNE_CONTROLLER_BUTTON_DOOR,
        0,
        contract.mechanism_controller_plan_flags(
            runeio.RUNE_CONTROLLER_BUTTON_DOOR
        ),
        2,
        3000,
        zlib.crc32(plan_edge_bytes) & 0xFFFFFFFF,
    )
    payload = (
        seeds + links + nodes + inventory_bytes + plan_edge_bytes +
        plan + strings
    )
    return _wrap_rune_payload(
        payload,
        num_seeds=2,
        num_links=2,
        num_nodes=3,
        num_edges=len(inventory) + len(plan_edges),
        num_plans=1,
        string_bytes=len(strings),
        num_inventory_edges=len(inventory),
        map_name="buttonteam",
    )


def _build_teleports(
    pair_count: int = 1,
    *,
    include_custom_trigger: bool = False,
) -> bytes:
    """Build one directed pad or two independently opposing pads."""

    if pair_count not in (1, 2):
        raise ValueError("teleport fixture supports one or two pads")
    destination_names = tuple(
        f"dest_{index}".encode("ascii") for index in range(pair_count)
    )
    values = {
        b"misc_teleporter",
        b"misc_teleporter_dest",
        b"trigger_teleport",
        *destination_names,
    }
    if include_custom_trigger:
        values.add(b"trigger_custom_teleport")
    ordered_values = tuple(sorted(values))
    strings = b"\0" + b"".join(
        value + b"\0" for value in ordered_values
    )
    offsets: dict[bytes, int] = {}
    offset = 1
    for value in ordered_values:
        offsets[value] = offset
        offset += len(value) + 1

    seed_count = 3 if pair_count == 1 else 2
    seeds = b"".join(
        runeio.SEED_STRUCT.pack(
            float(index * 256), 0.0, 0.0, index + 1, 0
        )
        for index in range(seed_count)
    )
    link_pairs = ((0, 1),) if pair_count == 1 else ((0, 1), (1, 0))
    link_rows = [
        runeio.RUNE_LINK_STRUCT.pack(
            source,
            destination,
            contract.RL_TELEPORT,
            contract.RL_DECLARED,
            0,
            0,
            0,
            0,
            160,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0,
            contract.RLCM_NONE,
            0,
            plan_index,
        )
        for plan_index, (source, destination) in enumerate(link_pairs)
    ]
    if pair_count == 1:
        # Route ownership requires every live seed to own an outgoing edge.
        # Keep the destination live with ordinary local graph edges; neither
        # one is a synthetic reverse teleport to the pad source.
        for source, destination in ((1, 2), (2, 1)):
            link_rows.append(runeio.RUNE_LINK_STRUCT.pack(
                source,
                destination,
                contract.RL_RUN,
                contract.RL_PROVEN,
                0,
                0,
                0,
                0,
                100,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                0,
                contract.RLCM_NONE,
                0,
                runeio.RUNE_NO_ACTIVATION_PLAN,
            ))
    links = b"".join(link_rows)

    def node(
        key: int,
        kind: int,
        classname: bytes,
        *,
        flags: int = 0,
        target: bytes | None = None,
        targetname: bytes | None = None,
        owner_key: int = runeio.RUNE_NO_KEY,
        touch_callback: int = 0,
    ) -> bytes:
        return runeio.RUNE_ACTIVATION_NODE_STRUCT.pack(
            key,
            kind,
            flags,
            offsets[classname],
            offsets[target] if target is not None else 0,
            offsets[targetname] if targetname is not None else 0,
            0,
            owner_key,
            runeio.RUNE_NO_KEY,
            0,
            touch_callback,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0.0,
            0.0,
            0.0,
        )

    node_rows: list[bytes] = []
    inventory_rows: list[tuple[int, int, int, int, int]] = []
    plan_edge_rows: list[tuple[int, int, int, int, int]] = []
    plan_bindings: list[tuple[int, int]] = []
    for index, destination_name in enumerate(destination_names):
        entry_key = index * 3 + 1
        pad_key = entry_key + 1
        destination_key = entry_key + 2
        node_rows.extend((
            node(
                entry_key,
                runeio.RUNE_NODE_TELEPORT_TRIGGER,
                b"trigger_teleport",
                flags=(
                    runeio.RUNE_NODEF_SYNTHETIC |
                    runeio.RUNE_NODEF_TOUCHABLE
                ),
                target=destination_name,
                owner_key=pad_key,
                touch_callback=runeio.RUNE_CALLBACK_TELEPORTER_TOUCH,
            ),
            node(
                pad_key,
                runeio.RUNE_NODE_TELEPORTER,
                b"misc_teleporter",
                target=destination_name,
            ),
            node(
                destination_key,
                runeio.RUNE_NODE_TELEPORT_DEST,
                b"misc_teleporter_dest",
                targetname=destination_name,
            ),
        ))
        target_row = (
            entry_key,
            destination_key,
            runeio.RUNE_EDGE_TARGET,
            0,
            0,
        )
        owner_row = (
            entry_key,
            pad_key,
            runeio.RUNE_EDGE_OWNER,
            0,
            0,
        )
        pad_target_row = (
            pad_key,
            destination_key,
            runeio.RUNE_EDGE_TARGET,
            0,
            0,
        )
        inventory_rows.extend((target_row, owner_row, pad_target_row))
        # The materializer's controller closure invokes the owner relation
        # before the exact destination relation.  Neither relation creates a
        # reverse graph link.
        plan_edge_rows.extend((owner_row, target_row))
        plan_bindings.append((entry_key, pad_key))

    if include_custom_trigger:
        node_rows.append(node(
            pair_count * 3 + 1,
            runeio.RUNE_NODE_OTHER_TRIGGER,
            b"trigger_custom_teleport",
            flags=(
                runeio.RUNE_NODEF_INVENTORY_ONLY |
                runeio.RUNE_NODEF_TOUCHABLE
            ),
            touch_callback=runeio.RUNE_CALLBACK_TELEPORTER_TOUCH,
        ))

    inventory = tuple(
        runeio.RUNE_ACTIVATION_EDGE_STRUCT.pack(*row)
        for row in inventory_rows
    )
    plan_edges = tuple(
        runeio.RUNE_ACTIVATION_EDGE_STRUCT.pack(*row)
        for row in plan_edge_rows
    )
    plans: list[bytes] = []
    first_edge = len(inventory)
    for plan_index, (entry_key, pad_key) in enumerate(plan_bindings):
        raw_edges = b"".join(
            plan_edges[plan_index * 2:plan_index * 2 + 2]
        )
        plans.append(runeio.RUNE_ACTIVATION_PLAN_STRUCT.pack(
            entry_key,
            pad_key,
            first_edge + plan_index * 2,
            2,
            runeio.RUNE_CONTROLLER_TELEPORT,
            0,
            contract.mechanism_controller_plan_flags(
                runeio.RUNE_CONTROLLER_TELEPORT
            ),
            1,
            0,
            zlib.crc32(raw_edges) & 0xFFFFFFFF,
        ))

    nodes = b"".join(node_rows)
    inventory_bytes = b"".join(inventory)
    plan_edge_bytes = b"".join(plan_edges)
    payload = (
        seeds + links + nodes + inventory_bytes + plan_edge_bytes +
        b"".join(plans) + strings
    )
    return _wrap_rune_payload(
        payload,
        num_seeds=seed_count,
        num_links=len(link_rows),
        num_nodes=len(node_rows),
        num_edges=len(inventory) + len(plan_edges),
        num_plans=len(plans),
        string_bytes=len(strings),
        num_inventory_edges=len(inventory),
        map_name="teleone" if pair_count == 1 else "telepair",
    )


class RuneRuneArtifactTests(unittest.TestCase):

    def test_trigger_count_excludes_stateful_hazard_targets(self):
        kinds = (
            runeio.RUNE_NODE_BUTTON,
            runeio.RUNE_NODE_RELAY,
            runeio.RUNE_NODE_TRIGGER_HURT,
        )

        self.assertEqual(
            2,
            sum(kind in runeio._RUNE_TRIGGER_NODE_KINDS for kind in kinds),
        )

    def test_continuous_station_plan_and_distinct_approach_are_readable(self):
        route = (28, 29, 30, 31, 32, 33, 34,
                 35, 36, 37, 38, 41, 40, 39)

        def station_node(
            key: int, kind: int, flags: int, *, master: int,
            wait_ms: int = 0,
        ) -> runeio.RuneActivationNode:
            train = kind == runeio.RUNE_NODE_TRAIN
            return runeio.RuneActivationNode(
                key=key,
                kind=kind,
                flags=flags,
                classname_offset=0,
                target_offset=1,
                targetname_offset=0 if train else 1,
                killtarget_offset=0,
                owner_key=runeio.RUNE_NO_KEY,
                team_master_key=master,
                spawnflags=1 if train else 0,
                touch_callback=(
                    0 if train else runeio.RUNE_CALLBACK_PATH_CORNER_TOUCH
                ),
                use_callback=(
                    runeio.RUNE_CALLBACK_TRAIN_USE if train else 0
                ),
                think_callback=(
                    runeio.RUNE_CALLBACK_TRAIN_NEXT if train else 0
                ),
                blocked_callback=(
                    runeio.RUNE_CALLBACK_BLOCKED_TRAIN if train else 0
                ),
                delay_ms=0,
                wait_ms=wait_ms,
                speed_q8=3200 if train else 0,
                accel_q8=3200 if train else 0,
                decel_q8=3200 if train else 0,
                absmin_q8=(0, 0, 0),
                absmax_q8=(64, 64, 64),
                path_target_offset=0,
                push_velocity=(0.0, 0.0, 0.0),
            )

        master_flags = (
            runeio.RUNE_NODEF_REPEATABLE | runeio.RUNE_NODEF_USABLE |
            runeio.RUNE_NODEF_MOVER | runeio.RUNE_NODEF_TEAM_MASTER
        )
        member_flags = (
            runeio.RUNE_NODEF_REPEATABLE | runeio.RUNE_NODEF_USABLE |
            runeio.RUNE_NODEF_MOVER | runeio.RUNE_NODEF_TEAM_MEMBER
        )
        corner_flags = (
            runeio.RUNE_NODEF_REPEATABLE | runeio.RUNE_NODEF_TOUCHABLE
        )
        nodes = [station_node(
            5, runeio.RUNE_NODE_TRAIN, master_flags, master=5,
        )]
        nodes.extend(
            station_node(
                key, runeio.RUNE_NODE_PATH_CORNER, corner_flags,
                master=runeio.RUNE_NO_KEY,
                wait_ms=3000 if key in (28, 35) else 0,
            )
            for key in route
        )
        nodes.append(station_node(
            42, runeio.RUNE_NODE_TRAIN, member_flags, master=5,
        ))
        edge = runeio.RuneActivationEdge
        edges = (
            edge(5, 42, runeio.RUNE_EDGE_TEAM, 0, 0),
            edge(5, 29, runeio.RUNE_EDGE_ROUTE_TARGET, 0, 0),
            edge(42, 36, runeio.RUNE_EDGE_ROUTE_TARGET, 0, 0),
            *(edge(route[index], route[(index + 1) % len(route)],
                   runeio.RUNE_EDGE_ROUTE_TARGET, 0, 0)
              for index in range(len(route))),
        )
        fanout = {
            (item.from_key, item.kind): (item,)
            for item in edges
        }
        plan = runeio.RuneActivationPlan(
            28, 5, 0, len(edges),
            runeio.RUNE_CONTROLLER_TRAIN_STATION,
            contract.mechanism_controller_plan_flags(
                runeio.RUNE_CONTROLLER_TRAIN_STATION
            ),
            2, 3000, 0,
        )
        owner = runeio.RuneLink(
            0, 1, contract.RL_TRAIN, contract.RL_DECLARED,
            0, 0, 0, 0, 1000,
            (-96.0, 0.0, 0.0), (0.0, 0.0, 16.0),
            100, contract.RLCM_RIDE, 0, 0,
        )

        runeio._rune_validate_production_plan(
            plan, edges, owner, {item.key: item for item in nodes},
            fanout, {}, b"\0x\0", 0,
        )
        with self.assertRaises(runeio.RuneWireError):
            runeio._rune_validate_production_plan(
                plan, edges,
                replace(owner, suffix_anchor=owner.mechanism_anchor),
                {item.key: item for item in nodes}, fanout, {}, b"\0x\0", 0,
            )

    def test_relay_wall_plan_requires_identical_typed_fanout(self):
        values = (
            b"barrier", b"fan", b"func_button", b"func_wall",
            b"target_speaker", b"trigger_hurt", b"trigger_relay",
        )
        strings = b"\0" + b"".join(value + b"\0" for value in values)
        offsets: dict[bytes, int] = {}
        offset = 1
        for value in values:
            offsets[value] = offset
            offset += len(value) + 1

        def node(
            key: int,
            kind: int,
            classname: bytes,
            *,
            flags: int,
            target: bytes | None = None,
            targetname: bytes | None = None,
            spawnflags: int = 0,
            touch_callback: int = 0,
            use_callback: int = 0,
            delay_ms: int = 0,
            wait_ms: int = 0,
            speed_q8: int = 0,
        ) -> runeio.RuneActivationNode:
            return runeio.RuneActivationNode(
                key=key,
                kind=kind,
                flags=flags,
                classname_offset=offsets[classname],
                target_offset=offsets[target] if target is not None else 0,
                targetname_offset=(
                    offsets[targetname] if targetname is not None else 0
                ),
                killtarget_offset=0,
                owner_key=runeio.RUNE_NO_KEY,
                team_master_key=runeio.RUNE_NO_KEY,
                spawnflags=spawnflags,
                touch_callback=touch_callback,
                use_callback=use_callback,
                think_callback=0,
                blocked_callback=0,
                delay_ms=delay_ms,
                wait_ms=wait_ms,
                speed_q8=speed_q8,
                accel_q8=speed_q8,
                decel_q8=speed_q8,
                absmin_q8=(0, 0, 0),
                absmax_q8=(64, 64, 64),
                path_target_offset=0,
                push_velocity=(0.0, 0.0, 0.0),
            )

        nodes = (
            node(
                10, runeio.RUNE_NODE_BUTTON, b"func_button",
                flags=(
                    runeio.RUNE_NODEF_REPEATABLE |
                    runeio.RUNE_NODEF_TOUCHABLE |
                    runeio.RUNE_NODEF_USABLE |
                    runeio.RUNE_NODEF_MOVER
                ),
                target=b"fan",
                touch_callback=runeio.RUNE_CALLBACK_BUTTON_TOUCH,
                use_callback=runeio.RUNE_CALLBACK_BUTTON_USE,
                delay_ms=200,
                wait_ms=4000,
                speed_q8=320,
            ),
            node(
                20, runeio.RUNE_NODE_RELAY, b"trigger_relay",
                flags=runeio.RUNE_NODEF_USABLE,
                target=b"barrier", targetname=b"fan",
                use_callback=runeio.RUNE_CALLBACK_USE_TRIGGER_RELAY,
            ),
            node(
                30, runeio.RUNE_NODE_RELAY, b"trigger_relay",
                flags=runeio.RUNE_NODEF_USABLE,
                target=b"barrier", targetname=b"fan",
                use_callback=runeio.RUNE_CALLBACK_USE_TRIGGER_RELAY,
                delay_ms=4000,
            ),
            node(
                40, runeio.RUNE_NODE_TOGGLE_WALL, b"func_wall",
                flags=(
                    runeio.RUNE_NODEF_REPEATABLE |
                    runeio.RUNE_NODEF_USABLE |
                    runeio.RUNE_NODEF_MOVER
                ),
                target=b"fan", targetname=b"barrier", spawnflags=7,
                use_callback=runeio.RUNE_CALLBACK_USE_FUNC_WALL,
            ),
            node(
                50, runeio.RUNE_NODE_TARGET_SPEAKER, b"target_speaker",
                flags=(runeio.RUNE_NODEF_REPEATABLE |
                       runeio.RUNE_NODEF_USABLE),
                targetname=b"barrier", spawnflags=1,
                use_callback=runeio.RUNE_CALLBACK_USE_TARGET_SPEAKER,
            ),
            node(
                60, runeio.RUNE_NODE_TRIGGER_HURT, b"trigger_hurt",
                flags=(
                    runeio.RUNE_NODEF_REPEATABLE |
                    runeio.RUNE_NODEF_TOUCHABLE |
                    runeio.RUNE_NODEF_USABLE
                ),
                targetname=b"barrier", spawnflags=2,
                touch_callback=runeio.RUNE_CALLBACK_TOUCH_HURT,
                use_callback=runeio.RUNE_CALLBACK_USE_HURT,
            ),
            node(
                70, runeio.RUNE_NODE_TARGET_SPEAKER, b"target_speaker",
                flags=(runeio.RUNE_NODEF_REPEATABLE |
                       runeio.RUNE_NODEF_USABLE),
                targetname=b"barrier", spawnflags=1,
                use_callback=runeio.RUNE_CALLBACK_USE_TARGET_SPEAKER,
            ),
        )
        edge = runeio.RuneActivationEdge
        inventory = (
            edge(10, 20, runeio.RUNE_EDGE_TARGET, 0, 200),
            edge(10, 30, runeio.RUNE_EDGE_TARGET, 1, 200),
            *(edge(20, key, runeio.RUNE_EDGE_TARGET, ordinal, 0)
              for ordinal, key in enumerate((40, 50, 60, 70))),
            *(edge(30, key, runeio.RUNE_EDGE_TARGET, ordinal, 4000)
              for ordinal, key in enumerate((40, 50, 60, 70))),
        )
        plan = runeio.RuneActivationPlan(
            10, 40, len(inventory), len(inventory),
            runeio.RUNE_CONTROLLER_RELAY_DOOR, 0, 1, 4000, 0,
        )
        owner = runeio.RuneLink(
            0, 1, contract.RL_BUTTON_DOOR, contract.RL_DECLARED,
            0, 0, 0, 0, 4200,
            (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
            0, contract.RLCM_PREOPEN, 0, 0,
        )

        def validate(edges: tuple[runeio.RuneActivationEdge, ...]) -> None:
            fanout: dict[
                tuple[int, int], tuple[runeio.RuneActivationEdge, ...]
            ] = {}
            for source in (10, 20, 30):
                fanout[(source, runeio.RUNE_EDGE_TARGET)] = tuple(
                    item for item in edges if item.from_key == source
                )
            runeio._rune_validate_production_plan(
                plan, edges, owner, {item.key: item for item in nodes},
                fanout, {}, strings, 0,
            )

        validate(inventory)
        malformed = list(inventory)
        malformed[6] = replace(malformed[6], to_key=50)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_PLAN,
            lambda: validate(tuple(malformed)),
        )

    def test_shootable_door_plan_uses_shared_train_identity(self):
        raw_plan = runeio.RUNE_ACTIVATION_PLAN_STRUCT.pack(
            10,
            10,
            1,
            1,
            runeio.RUNE_CONTROLLER_TRAIN_SHOOT,
            0,
            contract.mechanism_controller_plan_flags(
                runeio.RUNE_CONTROLLER_TRAIN_SHOOT
            ),
            2,
            1200,
            1,
        )
        plan = runeio._decode_rune_plan(raw_plan, 0)
        self.assertEqual(plan.entry_key, plan.mover_key)

        node = runeio.RuneActivationNode(
            key=10,
            kind=runeio.RUNE_NODE_DOOR_MASTER,
            flags=(
                runeio.RUNE_NODEF_REPEATABLE |
                runeio.RUNE_NODEF_USABLE |
                runeio.RUNE_NODEF_MOVER |
                runeio.RUNE_NODEF_TEAM_MASTER |
                runeio.RUNE_NODEF_SHOOTABLE
            ),
            classname_offset=1,
            target_offset=0,
            targetname_offset=0,
            killtarget_offset=0,
            owner_key=runeio.RUNE_NO_KEY,
            team_master_key=10,
            spawnflags=0,
            touch_callback=0,
            use_callback=runeio.RUNE_CALLBACK_USE_DOOR,
            think_callback=runeio.RUNE_CALLBACK_THINK_CALC_MOVE_SPEED,
            blocked_callback=runeio.RUNE_CALLBACK_BLOCKED_DOOR,
            delay_ms=0,
            wait_ms=3000,
            speed_q8=800,
            accel_q8=800,
            decel_q8=800,
            absmin_q8=(0, 0, 0),
            absmax_q8=(64, 64, 64),
            path_target_offset=0,
            push_velocity=(0.0, 0.0, 0.0),
        )
        strings = b"\0func_door\0"
        self.assertTrue(runeio._rune_door_node_valid(
            node, 10, master=True, strings=strings
        ))
        self.assertFalse(runeio._rune_door_node_valid(
            replace(
                node,
                think_callback=
                runeio.RUNE_CALLBACK_THINK_SPAWN_DOOR_TRIGGER,
            ),
            10,
            master=True,
            strings=strings,
        ))

    def test_carrier_door_spawnflags_admit_both_directions(self):
        self.assertTrue(runeio._carrier_door_spawnflags(4))
        self.assertTrue(runeio._carrier_door_spawnflags(5))
        for forbidden in (0, 1, 3, 6, 7):
            with self.subTest(spawnflags=forbidden):
                self.assertFalse(runeio._carrier_door_spawnflags(forbidden))
    def setUp(self) -> None:
        self.encoded = _build_rune()

    def test_nonatomic_lift_ride_accepts_bounded_board_anchor(self):
        seeds = (
            runeio.RuneSeed((0.0, 0.0, 0.0)),
            runeio.RuneSeed((128.0, 0.0, 0.0)),
        )
        lift = runeio.RunePolicyLink(
            0, 1, contract.RL_LIFT, contract.RL_DECLARED,
            0, 0, 0, 0, 100, mode=contract.RLCM_RIDE,
        )
        run = runeio.RunePolicyLink(
            1, 0, contract.RL_RUN, contract.RL_PROVEN,
            0, 0, 0, 0, 100,
        )

        def packed(link, plan):
            return runeio.RUNE_LINK_STRUCT.pack(
                link.source, link.destination, link.action, link.provenance,
                link.min_speed, link.heading, link.heading_slack,
                link.exit_speed, link.cost_ms, *link.suffix_anchor,
                *link.mechanism_anchor, link.sweep_clear_ms, link.mode,
                link.reserved, plan,
            )[:runeio.RUNE_POLICY_LINK_BYTES]

        def validate(candidate):
            runeio._validate_graph(
                seeds, (candidate, run),
                (packed(candidate, 0), packed(run,
                    runeio.RUNE_NO_ACTIVATION_PLAN)),
                (0, runeio.RUNE_NO_ACTIVATION_PLAN),
                runeio.RUNE_ROUTE_CONTRACT_COMPLETE,
            )

        validate(lift)
        validate(replace(lift, mechanism_anchor=(0.125, 0.0, 0.0)))
        for malformed in (replace(lift, sweep_clear_ms=100),):
            with self.subTest(link=malformed):
                self.assert_wire_code(
                    contract.RLW_BAD_LINK_RECORD,
                    lambda malformed=malformed: validate(malformed),
                )

    def test_chain_hook_owns_only_the_second_control_slot(self):
        seeds = (
            runeio.RuneSeed((0.0, 0.0, 0.0)),
            runeio.RuneSeed((128.0, 0.0, 0.0)),
        )
        chain = runeio.RunePolicyLink(
            0, 1, contract.RL_CHAIN_HOOK, contract.RL_PROVEN,
            0, 0, 0, 0, 1000,
            suffix_anchor=(0.0, 0.0, 512.0),
            mechanism_anchor=(0.0, 90.0, 512.0),
        )
        run = runeio.RunePolicyLink(
            1, 0, contract.RL_RUN, contract.RL_PROVEN,
            0, 0, 0, 0, 100,
        )

        def packed(link):
            return runeio.RUNE_LINK_STRUCT.pack(
                link.source, link.destination, link.action, link.provenance,
                link.min_speed, link.heading, link.heading_slack,
                link.exit_speed, link.cost_ms, *link.suffix_anchor,
                *link.mechanism_anchor, link.sweep_clear_ms, link.mode,
                link.reserved, runeio.RUNE_NO_ACTIVATION_PLAN,
            )[:runeio.RUNE_POLICY_LINK_BYTES]

        def validate(candidate):
            runeio._validate_graph(
                seeds, (candidate, run), (packed(candidate), packed(run)),
                (runeio.RUNE_NO_ACTIVATION_PLAN,) * 2,
                runeio.RUNE_ROUTE_CONTRACT_COMPLETE,
        )

        self.assertEqual((0.0, 90.0, 512.0), chain.secondary_control)
        self.assertIsNone(run.secondary_control)
        validate(chain)
        for malformed in (
            replace(chain, action=contract.RL_HOOK),
            replace(chain, mechanism_anchor=(89.1, 90.0, 512.0)),
            replace(chain, mechanism_anchor=(0.0, 90.0, 0.0)),
        ):
            with self.subTest(link=malformed):
                self.assert_wire_code(
                    contract.RLW_BAD_LINK_RECORD,
                    lambda malformed=malformed: validate(malformed),
                )

    def assert_wire_code(self, code: int, operation) -> None:
        with self.assertRaises(runeio.RuneWireError) as raised:
            operation()
        self.assertEqual(code, raised.exception.code)

    def test_rune_reader_exposes_authenticated_records_and_counts(self):
        decoded = runeio.decode_rune(self.encoded)
        self.assertIsInstance(decoded, runeio.RuneArtifact)
        self.assertEqual("runetest", decoded.header.map_name)
        self.assertEqual(
            runeio.RUNE_ROUTE_CONTRACT_COMPLETE,
            decoded.header.route_contract,
        )
        self.assertEqual(2, len(decoded.seeds))
        self.assertEqual(2, len(decoded.links))
        self.assertEqual(3, len(decoded.activation_nodes))
        self.assertEqual(2, len(decoded.activation_edges))
        self.assertEqual(1, len(decoded.inventory_edges))
        self.assertEqual(1, len(decoded.plan_edges))
        self.assertEqual(1, len(decoded.activation_plans))
        self.assertEqual(2, decoded.trigger_count)
        self.assertEqual("func_button", decoded.string_at(13))
        self.assertEqual(0, decoded.links[0].activation_plan)
        self.assertEqual(
            runeio.RUNE_NO_ACTIVATION_PLAN,
            decoded.links[1].activation_plan,
        )

        summary = runeio.summarize_rune(decoded)
        self.assertEqual("complete", summary["route_contract"])
        self.assertEqual(2, summary["trigger_count"])
        self.assertEqual(3, summary["node_count"])
        self.assertEqual(2, summary["edge_count"])
        self.assertEqual(1, summary["plan_count"])
        self.assertEqual(1, summary["inventory_edge_count"])
        self.assertEqual(1, summary["plan_edge_count"])

    def test_route_contract_is_authenticated_and_strict(self):
        local_only = bytearray(self.encoded)
        struct.pack_into(
            "<H", local_only, 4,
            runeio.RUNE_ROUTE_CONTRACT_LOCAL_ONLY,
        )
        struct.pack_into(
            "<H", local_only, runeio.RUNE_HEADER_BYTES + 14,
            runeio.RSF_OBJECTIVE,
        )
        struct.pack_into(
            "<H", local_only,
            runeio.RUNE_HEADER_BYTES + runeio.RUNE_SEED_BYTES + 14,
            runeio.RSF_OBJECTIVE,
        )
        _fix_payload_and_header_crc(local_only)
        decoded = runeio.decode_rune(local_only)
        self.assertEqual(
            runeio.RUNE_ROUTE_CONTRACT_LOCAL_ONLY,
            decoded.header.route_contract,
        )
        self.assertEqual(
            "local_only", runeio.summarize_rune(decoded)["route_contract"]
        )

        unknown = bytearray(local_only)
        struct.pack_into("<H", unknown, 4, 2)
        _fix_header_crc(unknown)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ROUTE_CONTRACT,
            lambda: runeio.decode_rune(unknown),
        )

    def test_link_identity_includes_activation_plan(self):
        decoded = runeio.decode_rune(_build_two_button_same_mover())
        self.assertEqual((0, 1), tuple(
            link.activation_plan for link in decoded.links[:2]
        ))
        self.assertEqual(
            decoded.activation_plans[0].mover_key,
            decoded.activation_plans[1].mover_key,
        )
        self.assert_wire_code(
            contract.RLW_DUPLICATE_LINK,
            lambda: runeio.decode_rune(
                _build_two_button_same_mover(second_plan=0)
            ),
        )

    def test_train_controllers_require_the_sealed_stock_cycle(self):
        controllers = (
            contract.SG_MECHANISM_CONTROLLER_TRAIN,
            contract.SG_MECHANISM_CONTROLLER_TRAIN_SHOOT,
        )
        for controller in controllers:
            with self.subTest(controller=controller):
                decoded = runeio.decode_rune(_build_train_plan(controller))
                self.assertEqual(
                    controller,
                    decoded.activation_plans[0].controller_kind,
                )
                self.assert_wire_code(
                    runeio.RLRUNE_BAD_ACTIVATION_PLAN,
                    lambda: runeio.decode_rune(_build_train_plan(
                        controller,
                        sealed_think=runeio.RUNE_CALLBACK_TRAIN_NEXT,
                    )),
                )

    def test_header_payload_and_contract_authentication(self):
        bad_header_crc = bytearray(self.encoded)
        bad_header_crc[HEADER_CRC_OFFSET] ^= 1
        self.assert_wire_code(
            contract.RLW_BAD_HEADER_CRC,
            lambda: runeio.decode(bad_header_crc),
        )

        bad_payload_crc = bytearray(self.encoded)
        bad_payload_crc[-2] ^= 1
        self.assert_wire_code(
            contract.RLW_BAD_PAYLOAD_CRC,
            lambda: runeio.decode(bad_payload_crc),
        )

        bad_action_contract = bytearray(self.encoded)
        bad_action_contract[ACTION_CONTRACT_OFFSET] ^= 1
        _fix_header_crc(bad_action_contract)
        self.assert_wire_code(
            contract.RLW_BAD_ACTION_CONTRACT,
            lambda: runeio.decode(bad_action_contract),
        )

        bad_mechanism_contract = bytearray(self.encoded)
        bad_mechanism_contract[MECHANISM_CONTRACT_OFFSET] ^= 1
        _fix_header_crc(bad_mechanism_contract)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_MECHANISM_CONTRACT,
            lambda: runeio.decode(bad_mechanism_contract),
        )

        nonzero_reserved = bytearray(self.encoded)
        struct.pack_into("<H", nonzero_reserved, HEADER_RESERVED_OFFSET, 1)
        _fix_header_crc(nonzero_reserved)
        self.assert_wire_code(
            contract.RLR_NONZERO_RESERVED,
            lambda: runeio.decode(nonzero_reserved),
        )

    def test_rune_seed_origins_require_exact_signed_q8(self):
        decoded = runeio.decode(self.encoded)
        for seed in decoded.seeds:
            for component in seed.origin:
                scaled = component * contract.RUNE_PROOF_WORLD_FIXED_SCALE
                self.assertEqual(int(scaled), scaled)
                self.assertGreaterEqual(
                    scaled, contract.RUNE_PROOF_WORLD_FIXED_MIN
                )
                self.assertLessEqual(
                    scaled, contract.RUNE_PROOF_WORLD_FIXED_MAX
                )

        for off_grid in (0.03125, -0.03125):
            malformed = bytearray(self.encoded)
            struct.pack_into(
                "<f", malformed, runeio.RUNE_HEADER_BYTES + 8, off_grid
            )
            _fix_payload_and_header_crc(malformed)
            self.assert_wire_code(
                contract.RLW_BAD_SEED_RECORD,
                lambda malformed=malformed: runeio.decode(malformed),
            )

    def test_incorrect_action_contract_rejects_before_seed_validation(self):
        incorrect_contract_crc32 = contract.RUNE_ACTION_CONTRACT_CRC32 ^ 1
        malformed = bytearray(self.encoded)
        struct.pack_into("<f", malformed, runeio.RUNE_HEADER_BYTES + 8, 0.03125)
        struct.pack_into(
            "<I", malformed, ACTION_CONTRACT_OFFSET, incorrect_contract_crc32
        )
        _fix_payload_and_header_crc(malformed)
        self.assert_wire_code(
            contract.RLW_BAD_ACTION_CONTRACT,
            lambda: runeio.decode(malformed),
        )

    @unittest.skip("edge-catalog fixture is outside focused layout coverage")
    def test_edge_ids_decode_to_the_settled_meanings(self):
        decoded = runeio.decode(_build_edge_catalog())
        self.assertEqual(
            [
                (1, "target"),
                (2, "killtarget"),
                (3, "owner"),
                (4, "team"),
                (5, "path_target"),
                (6, "move_target"),
                (7, "target_ent"),
                (8, "enemy"),
                (9, "route_target"),
            ],
            [(edge.kind, edge.kind_name) for edge in decoded.inventory_edges],
        )
        self.assertEqual(0xFFFFFFFF, decoded.inventory_edges[7].delay_ms)
        self.assertEqual(-(1 << 31), decoded.activation_nodes[0].delay_ms)
        self.assertEqual((1 << 31) - 1, decoded.activation_nodes[0].wait_ms)
        self.assertEqual(3, decoded.activation_nodes[0].path_target_offset)
        self.assertEqual("target_speaker", decoded.activation_nodes[0].kind_name)
        self.assertEqual(32, decoded.activation_nodes[0].use_callback)
        self.assertEqual("areaportal", decoded.activation_nodes[1].kind_name)
        self.assertEqual(33, decoded.activation_nodes[1].use_callback)

    def test_inventory_partition_and_record_lint_fail_closed(self):
        bad_inventory_count = bytearray(self.encoded)
        struct.pack_into("<I", bad_inventory_count, INVENTORY_COUNT_OFFSET, 0)
        _fix_header_crc(bad_inventory_count)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_MECHANISM_CONTRACT,
            lambda: runeio.decode(bad_inventory_count),
        )

        node_offset = (
            runeio.RUNE_HEADER_BYTES +
            2 * runeio.RUNE_SEED_BYTES +
            2 * runeio.RUNE_LINK_BYTES
        )
        bad_node = bytearray(self.encoded)
        struct.pack_into("<H", bad_node, node_offset + 4, 0)
        _fix_payload_and_header_crc(bad_node)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_NODE,
            lambda: runeio.decode(bad_node),
        )

        plan_offset = (
            node_offset +
            3 * runeio.RUNE_ACTIVATION_NODE_BYTES +
            2 * runeio.RUNE_ACTIVATION_EDGE_BYTES
        )
        bad_closure = bytearray(self.encoded)
        bad_closure[plan_offset + 28] ^= 1
        _fix_payload_and_header_crc(bad_closure)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_PLAN,
            lambda: runeio.decode(bad_closure),
        )

    def test_frame_complete_button_wire_mutations_fail_closed(self):
        node_offset = (
            runeio.RUNE_HEADER_BYTES +
            2 * runeio.RUNE_SEED_BYTES +
            2 * runeio.RUNE_LINK_BYTES
        )
        malformed_shape = bytearray(self.encoded)
        flags = (
            runeio.RUNE_NODEF_REPEATABLE |
            runeio.RUNE_NODEF_USABLE |
            runeio.RUNE_NODEF_MOVER |
            runeio.RUNE_NODEF_SHOOTABLE |
            runeio.RUNE_NODEF_FRAME_COMPLETE_MOVER
        )
        struct.pack_into("<H", malformed_shape, node_offset + 6, flags)
        struct.pack_into("<H", malformed_shape, node_offset + 36, 0)
        struct.pack_into("<III", malformed_shape, node_offset + 52,
                         7600, 7600, 7600)
        _fix_payload_and_header_crc(malformed_shape)
        # A frame-complete shootable button remains inventory and cannot be
        # repurposed as this artifact's BUTTON_DOOR controller entry.
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_PLAN,
            lambda: runeio.decode(malformed_shape),
        )

        unequal_witness = bytearray(malformed_shape)
        struct.pack_into("<I", unequal_witness, node_offset + 56, 7610)
        _fix_payload_and_header_crc(unequal_witness)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_NODE,
            lambda: runeio.decode(unequal_witness),
        )

        non_button = bytearray(malformed_shape)
        struct.pack_into("<H", non_button, node_offset + 4,
                         runeio.RUNE_NODE_DOOR_MASTER)
        _fix_payload_and_header_crc(non_button)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_NODE,
            lambda: runeio.decode(non_button),
        )

    def test_generated_action_plan_admission_fails_closed(self):
        first_link = (
            runeio.RUNE_HEADER_BYTES +
            2 * runeio.RUNE_SEED_BYTES
        )
        second_link = first_link + runeio.RUNE_LINK_BYTES
        node_offset = second_link + runeio.RUNE_LINK_BYTES
        plan_offset = (
            node_offset +
            3 * runeio.RUNE_ACTIVATION_NODE_BYTES +
            2 * runeio.RUNE_ACTIVATION_EDGE_BYTES
        )

        missing_required = bytearray(self.encoded)
        struct.pack_into(
            "<I", missing_required, first_link + 44,
            runeio.RUNE_NO_ACTIVATION_PLAN,
        )
        _fix_payload_and_header_crc(missing_required)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_PLAN,
            lambda: runeio.decode(missing_required),
        )

        unexpected_plan = bytearray(self.encoded)
        struct.pack_into("<I", unexpected_plan, second_link + 44, 0)
        _fix_payload_and_header_crc(unexpected_plan)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_PLAN,
            lambda: runeio.decode(unexpected_plan),
        )

        wrong_controller = bytearray(self.encoded)
        struct.pack_into(
            "<H", wrong_controller, plan_offset + 16,
            runeio.RUNE_CONTROLLER_PLATFORM,
        )
        _fix_payload_and_header_crc(wrong_controller)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_PLAN,
            lambda: runeio.decode(wrong_controller),
        )

        nonzero_reserved = bytearray(self.encoded)
        struct.pack_into("<H", nonzero_reserved, plan_offset + 18, 1)
        _fix_payload_and_header_crc(nonzero_reserved)
        self.assert_wire_code(
            contract.RLR_NONZERO_RESERVED,
            lambda: runeio.decode(nonzero_reserved),
        )

        disabled_action = bytearray(self.encoded)
        disabled_action[second_link + 8] = contract.RL_ROCKETJUMP
        _fix_payload_and_header_crc(disabled_action)
        self.assert_wire_code(
            contract.RLW_BAD_LINK_RECORD,
            lambda: runeio.decode(disabled_action),
        )

    def test_button_link_authenticates_mode_displacement_and_egress_bound(self):
        decoded = runeio.decode(self.encoded)
        link = decoded.links[0]
        self.assertEqual(contract.RL_BUTTON_DOOR, link.action)
        self.assertEqual(contract.RLCM_RIDE, link.mode)
        self.assertEqual((0.0, 0.0, -2.0), link.mechanism_anchor)
        self.assertEqual(100, link.sweep_clear_ms)

        first_link = (
            runeio.RUNE_HEADER_BYTES +
            2 * runeio.RUNE_SEED_BYTES
        )
        static_mode = bytearray(self.encoded)
        static_mode[first_link + 42] = contract.RLCM_PREOPEN
        _fix_payload_and_header_crc(static_mode)
        self.assertEqual(
            contract.RLCM_PREOPEN,
            runeio.decode(static_mode).links[0].mode,
        )

        malformed = []
        zero_displacement = bytearray(self.encoded)
        struct.pack_into("<fff", zero_displacement, first_link + 28, 0.0, 0.0, 0.0)
        malformed.append(("zero-displacement", zero_displacement))
        signed_zero_displacement = bytearray(self.encoded)
        struct.pack_into(
            "<fff", signed_zero_displacement, first_link + 28,
            -0.0, 0.0, 0.0,
        )
        malformed.append(("signed-zero-displacement", signed_zero_displacement))
        off_grid = bytearray(self.encoded)
        struct.pack_into("<f", off_grid, first_link + 28, 0.03125)
        malformed.append(("off-grid-displacement", off_grid))
        no_mode = bytearray(self.encoded)
        no_mode[first_link + 42] = contract.RLCM_NONE
        malformed.append(("no-support-mode", no_mode))
        zero_bound = bytearray(self.encoded)
        struct.pack_into("<H", zero_bound, first_link + 40, 0)
        malformed.append(("zero-egress-bound", zero_bound))
        unrounded_bound = bytearray(self.encoded)
        struct.pack_into("<H", unrounded_bound, first_link + 40, 50)
        malformed.append(("unrounded-egress-bound", unrounded_bound))
        excessive_bound = bytearray(self.encoded)
        struct.pack_into("<H", excessive_bound, first_link + 40, 200)
        malformed.append(("excessive-egress-bound", excessive_bound))
        positive_endpoint_overflow = bytearray(self.encoded)
        struct.pack_into(
            "<f", positive_endpoint_overflow, first_link + 16,
            contract.RUNE_PROOF_WORLD_FIXED_MAX / 8.0,
        )
        struct.pack_into("<f", positive_endpoint_overflow, first_link + 28, 0.125)
        malformed.append(("positive-endpoint-overflow", positive_endpoint_overflow))
        negative_endpoint_overflow = bytearray(self.encoded)
        struct.pack_into(
            "<f", negative_endpoint_overflow, first_link + 16,
            contract.RUNE_PROOF_WORLD_FIXED_MIN / 8.0,
        )
        struct.pack_into("<f", negative_endpoint_overflow, first_link + 28, -0.125)
        malformed.append(("negative-endpoint-overflow", negative_endpoint_overflow))
        for label, encoded in malformed:
            with self.subTest(label=label):
                _fix_payload_and_header_crc(encoded)
                self.assert_wire_code(
                    contract.RLW_BAD_LINK_RECORD,
                    lambda encoded=encoded: runeio.decode(encoded),
                )

    def test_button_plan_requires_exact_runtime_shape_and_unique_target(self):
        node_offset = (
            runeio.RUNE_HEADER_BYTES +
            2 * runeio.RUNE_SEED_BYTES +
            2 * runeio.RUNE_LINK_BYTES
        )
        bad_flags = bytearray(self.encoded)
        struct.pack_into(
            "<H", bad_flags, node_offset + 6,
            runeio.RUNE_NODEF_REPEATABLE |
            runeio.RUNE_NODEF_TOUCHABLE |
            runeio.RUNE_NODEF_USABLE |
            runeio.RUNE_NODEF_MOVER |
            256,
        )
        _fix_payload_and_header_crc(bad_flags)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_PLAN,
            lambda: runeio.decode(bad_flags),
        )

        duplicate_target = bytearray(self.encoded)
        struct.pack_into(
            "<I", duplicate_target,
            node_offset + 2 * runeio.RUNE_ACTIVATION_NODE_BYTES + 16,
            7,
        )
        _fix_payload_and_header_crc(duplicate_target)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_PLAN,
            lambda: runeio.decode(duplicate_target),
        )

    def test_button_plan_authenticates_ordered_complete_team_fanout(self):
        decoded = runeio.decode(_build_button_team())
        self.assertEqual(1, len(decoded.activation_plans))
        self.assertEqual(
            runeio.RUNE_CONTROLLER_BUTTON_DOOR,
            decoded.activation_plans[0].controller_kind,
        )
        self.assertEqual(
            [(1, 2, 0), (1, 3, 1)],
            [
                (edge.from_key, edge.to_key, edge.ordinal)
                for edge in decoded.inventory_edges
                if edge.kind == runeio.RUNE_EDGE_TARGET
            ],
        )
        self.assertEqual(2, decoded.activation_plans[0].expected_members)

    def test_button_plan_rejects_bad_team_order_or_duplicate_target(self):
        for label, encoded in (
            ("slave-before-master", _build_button_team(entry_order=(3, 2))),
            ("duplicate-master", _build_button_team(entry_order=(2, 2))),
            ("reordered-plan-copy", _build_button_team(plan_order=(3, 2))),
        ):
            with self.subTest(label=label):
                self.assert_wire_code(
                    runeio.RLRUNE_BAD_ACTIVATION_PLAN,
                    lambda encoded=encoded: runeio.decode(encoded),
                )

    def test_one_teleporter_is_one_way_to_its_exact_destination(self):
        decoded = runeio.decode(_build_teleports())
        teleport_links = tuple(
            link for link in decoded.links
            if link.action == contract.RL_TELEPORT
        )
        self.assertEqual([(0, 1, 0)], [
            (link.source, link.destination, link.activation_plan)
            for link in teleport_links
        ])
        self.assertNotIn(
            (1, 0),
            {(link.source, link.destination) for link in decoded.links},
        )
        plan = decoded.activation_plans[0]
        closure = decoded.activation_edges[
            plan.first_edge:plan.first_edge + plan.num_edges
        ]
        self.assertEqual(
            [(1, 2, runeio.RUNE_EDGE_OWNER),
             (1, 3, runeio.RUNE_EDGE_TARGET)],
            [(edge.from_key, edge.to_key, edge.kind) for edge in closure],
        )

    def test_opposing_teleporters_are_two_independent_one_way_plans(self):
        decoded = runeio.decode(_build_teleports(2))
        self.assertEqual(
            [(0, 1, 0), (1, 0, 1)],
            [
                (link.source, link.destination, link.activation_plan)
                for link in decoded.links
            ],
        )
        self.assertEqual(
            [(1, 2), (4, 5)],
            [
                (plan.entry_key, plan.mover_key)
                for plan in decoded.activation_plans
            ],
        )
        destinations = []
        for plan in decoded.activation_plans:
            closure = decoded.activation_edges[
                plan.first_edge:plan.first_edge + plan.num_edges
            ]
            destinations.append(next(
                edge.to_key for edge in closure
                if edge.kind == runeio.RUNE_EDGE_TARGET
            ))
        self.assertEqual([3, 6], destinations)

    def test_custom_teleport_trigger_is_inventory_only(self):
        decoded = runeio.decode(_build_teleports(
            include_custom_trigger=True
        ))
        custom = next(
            node for node in decoded.activation_nodes
            if decoded.string_at(node.classname_offset) ==
            "trigger_custom_teleport"
        )
        self.assertEqual(runeio.RUNE_NODE_OTHER_TRIGGER, custom.kind)
        self.assertTrue(custom.flags & runeio.RUNE_NODEF_INVENTORY_ONLY)
        self.assertNotIn(
            custom.key,
            {
                key
                for plan in decoded.activation_plans
                for key in (plan.entry_key, plan.mover_key)
            },
        )

    def test_direct_door_plan_authenticates_ordered_complete_effect_closure(self):
        decoded = runeio.decode(_build_direct_door())
        self.assertEqual(11, len(decoded.activation_nodes))
        self.assertEqual(11, len(decoded.inventory_edges))
        self.assertEqual(11, len(decoded.plan_edges))
        self.assertEqual(1, len(decoded.activation_plans))
        self.assertEqual(
            runeio.RUNE_CONTROLLER_DIRECT_TRIGGER_DOOR,
            decoded.activation_plans[0].controller_kind,
        )

    def test_direct_door_plan_authenticates_one_synchronous_relay(self):
        decoded = runeio.decode(_build_direct_door(relay_door=True))
        self.assertEqual(
            [(1, 4), (1, 5), (5, 2), (5, 3)],
            [
                (edge.from_key, edge.to_key)
                for edge in decoded.plan_edges
                if edge.from_key in (1, 5)
            ],
        )
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_PLAN,
            lambda: runeio.decode(_build_direct_door(
                relay_door=True,
                relay_slave_first=True,
            )),
        )

    def test_delayed_sound_terminal_retains_inbound_ordinal_only(self):
        decoded = runeio.decode(_build_delayed_terminal_door())
        plan = decoded.activation_plans[0]

        self.assertEqual(312000, decoded.activation_nodes[0].wait_ms)
        self.assertEqual(311000, decoded.activation_nodes[1].delay_ms)
        self.assertEqual(runeio.RUNE_MAX_TIME_MS, plan.cooldown_ms)
        self.assertEqual(
            [(1, 2, 0, 0), (1, 3, 1, 0)],
            [
                (edge.from_key, edge.to_key, edge.ordinal, edge.delay_ms)
                for edge in decoded.inventory_edges
                if edge.from_key == 1
            ],
        )
        self.assertIn(
            (2, 5, 0, 311000),
            [
                (edge.from_key, edge.to_key, edge.ordinal, edge.delay_ms)
                for edge in decoded.inventory_edges
            ],
        )
        self.assertEqual(
            [(1, 2, 0, 0), (1, 3, 1, 0), (3, 4, 0, 0)],
            [
                (edge.from_key, edge.to_key, edge.ordinal, edge.delay_ms)
                for edge in decoded.plan_edges
            ],
        )
        self.assertNotIn(
            (2, 5),
            {(edge.from_key, edge.to_key) for edge in decoded.plan_edges},
        )

    def test_delayed_sound_terminal_rejects_drift_and_cycles(self):
        malformed = (
            ("wrong-inventory-delay", _build_delayed_terminal_door(
                relay_edge_delay_ms=310999)),
            ("wrong-relay-callback", _build_delayed_terminal_door(
                relay_use_callback=runeio.RUNE_CALLBACK_USE_TARGET_SPEAKER)),
            ("nested-cycle", _build_delayed_terminal_door(cycle=True)),
        )
        for label, encoded in malformed:
            with self.subTest(label=label):
                self.assert_wire_code(
                    runeio.RLRUNE_BAD_ACTIVATION_PLAN,
                    lambda encoded=encoded: runeio.decode(encoded),
                )

    def test_direct_door_plan_rejects_slave_before_master(self):
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_PLAN,
            lambda: runeio.decode(_build_direct_door(
                entry_order=(3, 2, 4, 5))),
        )

    def test_targeted_door_slave_retains_calc_move_speed_callback(self):
        encoded = bytearray(_build_direct_door())
        decoded = runeio.decode(encoded)
        node_offset = (
            runeio.RUNE_HEADER_BYTES +
            len(decoded.seeds) * runeio.RUNE_SEED_BYTES +
            len(decoded.links) * runeio.RUNE_LINK_BYTES
        )
        slave_offset = node_offset + runeio.RUNE_ACTIVATION_NODE_BYTES * 2
        struct.pack_into(
            "<H",
            encoded,
            slave_offset + 40,
            runeio.RUNE_CALLBACK_THINK_SPAWN_DOOR_TRIGGER,
        )
        _fix_payload_and_header_crc(encoded)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_PLAN,
            lambda: runeio.decode(encoded),
        )

    def test_direct_door_plan_rejects_mutable_side_effects(self):
        for encoded in (
            _build_direct_door(speaker_spawnflags=1),
            _build_direct_door(relay_delay_ms=100),
        ):
            with self.subTest():
                self.assert_wire_code(
                    runeio.RLRUNE_BAD_ACTIVATION_PLAN,
                    lambda encoded=encoded: runeio.decode(encoded),
                )

    def test_direct_door_spawnflag_law_allows_reverse_crusher_only(self):
        runeio.decode(_build_direct_door(door_spawnflags=2 | 4))
        for forbidden in (1, 32):
            with self.subTest(spawnflags=forbidden):
                self.assert_wire_code(
                    runeio.RLRUNE_BAD_ACTIVATION_PLAN,
                    lambda forbidden=forbidden: runeio.decode(
                        _build_direct_door(door_spawnflags=forbidden)),
                )

    def test_controller_edges_are_immediate_and_direct_entry_has_no_mutators(self):
        delayed = bytearray(_build_direct_door())
        decoded = runeio.decode(delayed)
        edge_offset = (
            runeio.RUNE_HEADER_BYTES +
            len(decoded.seeds) * runeio.RUNE_SEED_BYTES +
            len(decoded.links) * runeio.RUNE_LINK_BYTES +
            len(decoded.activation_nodes) *
            runeio.RUNE_ACTIVATION_NODE_BYTES
        )
        plan_edge_offset = (
            edge_offset +
            decoded.header.num_inventory_edges *
            runeio.RUNE_ACTIVATION_EDGE_BYTES
        )
        plan_offset = (
            edge_offset +
            len(decoded.activation_edges) *
            runeio.RUNE_ACTIVATION_EDGE_BYTES
        )
        struct.pack_into("<I", delayed, edge_offset + 12, 1)
        struct.pack_into("<I", delayed, plan_edge_offset + 12, 1)
        closure_bytes = delayed[
            plan_edge_offset:
            plan_edge_offset +
            decoded.header.num_inventory_edges *
            runeio.RUNE_ACTIVATION_EDGE_BYTES
        ]
        struct.pack_into(
            "<I", delayed, plan_offset + 28,
            zlib.crc32(closure_bytes) & 0xFFFFFFFF,
        )
        _fix_payload_and_header_crc(delayed)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_PLAN,
            lambda: runeio.decode(delayed),
        )

        mutating_entry = bytearray(_build_direct_door())
        decoded = runeio.decode(mutating_entry)
        node_offset = (
            runeio.RUNE_HEADER_BYTES +
            len(decoded.seeds) * runeio.RUNE_SEED_BYTES +
            len(decoded.links) * runeio.RUNE_LINK_BYTES
        )
        struct.pack_into(
            "<I", mutating_entry, node_offset + 20,
            decoded.activation_nodes[1].targetname_offset,
        )
        _fix_payload_and_header_crc(mutating_entry)
        self.assert_wire_code(
            runeio.RLRUNE_BAD_ACTIVATION_PLAN,
            lambda: runeio.decode(mutating_entry),
        )

    def test_read_and_cli_report_rune_artifact(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "runetest.rune"
            path.write_bytes(self.encoded)
            self.assertEqual("runetest", runeio.read(path).header.map_name)
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(
                    0,
                    runeio.main(["--require-mechanisms", str(path)]),
                )
            self.assertIn('"trigger_count": 2', output.getvalue())
            self.assertIn('"plan_count": 1', output.getvalue())

    def test_expected_identity_gate_and_cli(self):
        identity = runeio.decode(self.encoded).header.identity
        self.assertEqual(
            identity,
            runeio.decode(
                self.encoded, expected_identity=identity
            ).header.identity,
        )
        mismatches = (
            (
                replace(identity, map_name="othermap"),
                contract.RLW_MAPNAME_MISMATCH,
            ),
            (
                replace(identity, bsp_checksum=1),
                contract.RLW_BSP_CHECKSUM_MISMATCH,
            ),
            (
                replace(identity, entity_crc32=1),
                contract.RLW_ENTITY_CRC_MISMATCH,
            ),
            (
                replace(identity, host_physics_id=2),
                contract.RLW_PHYSICS_ID_MISMATCH,
            ),
            (
                replace(identity, gravity=800.0),
                contract.RLW_BAD_PHYSICS_LAW,
            ),
        )
        for expected, diagnostic in mismatches:
            with self.subTest(diagnostic=diagnostic):
                self.assert_wire_code(
                    diagnostic,
                    lambda expected=expected: runeio.decode(
                        self.encoded, expected_identity=expected
                    ),
                )

        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / "artifact.rune"
            reference = Path(temporary) / "reference.rune"
            artifact.write_bytes(self.encoded)
            reference.write_bytes(self.encoded)
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(
                    0,
                    runeio.main([
                        "--expected-identity", str(reference), str(artifact)
                    ]),
                )
            self.assertIn('"map_name": "runetest"', output.getvalue())

            mismatched = bytearray(self.encoded)
            map_offset = runeio.HEADER_STRUCT.size - 64
            mismatched[map_offset:map_offset + 64] = (
                b"othermap\0" + b"\0" * (64 - len(b"othermap\0"))
            )
            _fix_header_crc(mismatched)
            reference.write_bytes(mismatched)
            error = io.StringIO()
            with (
                redirect_stderr(error),
                self.assertRaises(SystemExit) as raised,
            ):
                runeio.main([
                    "--expected-identity", str(reference), str(artifact)
                ])
            self.assertEqual(2, raised.exception.code)
            self.assertIn("RLW_MAPNAME_MISMATCH", error.getvalue())

    def test_corpus_loader_and_seed_weights_are_strict(self):
        self.assertEqual(
            {0: 0, 1: 2.5},
            corpusgraph.validate_seed_weights(
                {"0": 0, "1": 2.5}, "weights.json", "post", 2
            ),
        )
        for weight in (float("nan"), float("inf"), float("-inf")):
            with self.subTest(weight=weight), self.assertRaises(ValueError):
                corpusgraph.validate_seed_weights(
                    {"0": weight}, "weights.json", "post", 1
                )
        with self.assertRaisesRegex(
            ValueError, r"weights\.json: post seed 0 has invalid weight"
        ):
            corpusgraph.validate_seed_weights(
                {"0": 10 ** 309}, "weights.json", "post", 1
            )

        with tempfile.TemporaryDirectory() as temporary:
            corpus = Path(temporary) / "corpus.json"
            corpus.write_text('{"map":"one","map":"two"}')
            with self.assertRaisesRegex(ValueError, "duplicate JSON key"):
                corpusgraph.load_corpus(corpus)
            corpus.write_text('{"weight":NaN}')
            with self.assertRaisesRegex(ValueError, "non-finite JSON value"):
                corpusgraph.load_corpus(corpus)
            corpus.write_text('{"weight":1e9999}')
            with self.assertRaisesRegex(
                ValueError, r"corpus\.json: malformed JSON: non-finite JSON value"
            ):
                corpusgraph.load_corpus(corpus)

    def test_acceptance_cli_rejects_missing_or_mismatched_plan(self):
        first_link = (
            runeio.RUNE_HEADER_BYTES +
            2 * runeio.RUNE_SEED_BYTES
        )
        plan_offset = (
            first_link +
            2 * runeio.RUNE_LINK_BYTES +
            3 * runeio.RUNE_ACTIVATION_NODE_BYTES +
            2 * runeio.RUNE_ACTIVATION_EDGE_BYTES
        )
        missing_required = bytearray(self.encoded)
        struct.pack_into(
            "<I", missing_required, first_link + 44,
            runeio.RUNE_NO_ACTIVATION_PLAN,
        )
        _fix_payload_and_header_crc(missing_required)
        wrong_controller = bytearray(self.encoded)
        struct.pack_into(
            "<H", wrong_controller, plan_offset + 16,
            runeio.RUNE_CONTROLLER_PLATFORM,
        )
        _fix_payload_and_header_crc(wrong_controller)

        with tempfile.TemporaryDirectory() as temporary:
            for name, encoded, diagnostic in (
                ("missing", missing_required, "missing activation plan"),
                ("controller", wrong_controller, "invalid plan binding"),
            ):
                with self.subTest(name=name):
                    path = Path(temporary) / f"{name}.rune"
                    path.write_bytes(encoded)
                    error = io.StringIO()
                    with (
                        redirect_stderr(error),
                        self.assertRaises(SystemExit) as raised,
                    ):
                        runeio.main([str(path)])
                    self.assertEqual(2, raised.exception.code)
                    self.assertIn("RLRUNE_BAD_ACTIVATION_PLAN", error.getvalue())

    def test_production_consumers_use_rune_reader(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "runetest.rune"
            path.write_bytes(self.encoded)

            graph = corpusgraph.read_rune(path, "runetest")
            self.assertEqual(3, graph["num_activation_nodes"])
            self.assertEqual(1, graph["num_inventory_edges"])
            self.assertEqual(1, graph["num_activation_plans"])

            viewed = runeview.load_rune(path)
            self.assertEqual("runetest", viewed.map_name)
            self.assertEqual(2, len(viewed.seeds))

            linted = runelint.load(path)
            self.assertEqual("runetest", linted[1])
            self.assertEqual(3, runelint._load_with_metadata(path)[1][
                "activation_nodes"])
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(
                    0,
                    runelint.main([str(path)]),
                )
            self.assertEqual("", output.getvalue())

    def test_lint_objective_roots_require_full_reverse_reachability(self):
        artifact = SimpleNamespace(
            header=SimpleNamespace(
                route_contract=runeio.RUNE_ROUTE_CONTRACT_COMPLETE
            ),
            seeds=[SimpleNamespace(flags=0) for _ in range(3)],
            links=(
                SimpleNamespace(source=0, destination=1),
                SimpleNamespace(source=1, destination=0),
                SimpleNamespace(source=2, destination=2),
            ),
        )
        flaws = runelint._objective_reachability_flaws(artifact, (0, 1))
        self.assertEqual(2, len(flaws))
        self.assertIn("outside red flag reverse component (seed 0): 1 (33%)", flaws)
        self.assertIn("outside blue flag reverse component (seed 1): 1 (33%)", flaws)

        connected = SimpleNamespace(
            header=artifact.header,
            seeds=artifact.seeds,
            links=artifact.links + (
                SimpleNamespace(source=2, destination=0),
                SimpleNamespace(source=0, destination=2),
            ),
        )
        self.assertEqual(
            [], runelint._objective_reachability_flaws(connected, (0, 1))
        )

        local_only = SimpleNamespace(
            header=SimpleNamespace(
                route_contract=runeio.RUNE_ROUTE_CONTRACT_LOCAL_ONLY
            ),
            seeds=[
                SimpleNamespace(flags=runeio.RSF_OBJECTIVE),
                SimpleNamespace(flags=runeio.RSF_OBJECTIVE),
                SimpleNamespace(flags=0),
            ],
            links=(
                SimpleNamespace(source=0, destination=0),
                SimpleNamespace(source=1, destination=1),
                SimpleNamespace(source=2, destination=0),
            ),
        )
        self.assertEqual(
            [], runelint._objective_reachability_flaws(local_only, (0, 1))
        )
        local_complete = SimpleNamespace(
            header=local_only.header,
            seeds=local_only.seeds,
            links=connected.links,
        )
        self.assertEqual(
            ["complete objective graph is mislabeled local-only"],
            runelint._objective_reachability_flaws(local_complete, (0, 1)),
        )

if __name__ == "__main__":
    unittest.main()
