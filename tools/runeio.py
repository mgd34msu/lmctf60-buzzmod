#!/usr/bin/env python3
"""Strict, explicit little-endian RUNE wire codecs.

Decoding authenticates the fixed wire contract without authorizing an
action or mechanism for live execution.  Runtime support remains a separate
outer policy.
"""

from __future__ import annotations

from dataclasses import dataclass
import argparse
import json
import math
import os
import re
import struct
from typing import Iterable, Mapping
import zlib

try:
    import rune_contracts_generated as contract
except ModuleNotFoundError:  # Also support ``python -m tools.runeio``.
    from tools import rune_contracts_generated as contract


RUNE_MAGIC = 0x454E5552
RUNE_PREFIX_HEADER_BYTES = 128
SEED_BYTES = 16
RUNE_POLICY_LINK_BYTES = 44
MAP_NAME_BYTES = 64
HEADER_CRC_OFFSET = 60
MAX_SEEDS = 32768
MAX_LINKS = 262144
MIN_COST_MS = 1
MAX_COST_MS = 30000

HEADER_STRUCT = struct.Struct("<IHHHHIIIIIIIfffHHII64s")
SEED_STRUCT = struct.Struct("<fffhh")
RUNE_POLICY_LINK_STRUCT = struct.Struct("<IIBBBBBBh3f3fHBB")

# The fixed header and record geometry are explicit rather than inferred from
# native C layouts.
RUNE_HEADER_EXTENSION_STRUCT = struct.Struct("<HHHHIIIIII")
RUNE_LINK_STRUCT = struct.Struct("<IIBBBBBBh3f3fHBBI")
RUNE_ACTIVATION_NODE_STRUCT = struct.Struct(
    "<IHHIIIIIIIHHHHiiIII3h3hI3f"
)
RUNE_ACTIVATION_EDGE_STRUCT = struct.Struct("<IIHHI")
RUNE_ACTIVATION_PLAN_STRUCT = struct.Struct("<IIIIHHHHII")

RUNE_HEADER_BYTES = 160
RUNE_SEED_BYTES = 16
RUNE_LINK_BYTES = 48
RUNE_ACTIVATION_NODE_BYTES = 92
RUNE_ACTIVATION_EDGE_BYTES = 16
RUNE_ACTIVATION_PLAN_BYTES = 32
RUNE_HEADER_CRC_OFFSET = HEADER_CRC_OFFSET
RUNE_MAX_ACTIVATION_NODES = 8192
RUNE_MAX_ACTIVATION_EDGES = 262144
RUNE_MAX_ACTIVATION_PLANS = 262144
RUNE_MAX_PLAN_EDGES = 65536
RUNE_MAX_STRING_BYTES = 1048576
RUNE_MAX_TIME_MS = 30000
RUNE_MAX_Q8 = 262136
RUNE_MAX_TEAM_MEMBERS = 16
RUNE_NO_KEY = 0xFFFFFFFF
RUNE_NO_ACTIVATION_PLAN = 0xFFFFFFFF

RUNE_MECHANISM_CONTRACT_CRC32 = (
    contract.RUNE_MECHANISM_CONTRACT_CRC32
)

RUNE_CONTROLLER_AUTO_DOOR = contract.SG_MECHANISM_CONTROLLER_AUTO_DOOR
RUNE_CONTROLLER_DIRECT_TRIGGER_DOOR = (
    contract.SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR
)
RUNE_CONTROLLER_BUTTON_DOOR = contract.SG_MECHANISM_CONTROLLER_BUTTON_DOOR
RUNE_CONTROLLER_RELAY_DOOR = contract.SG_MECHANISM_CONTROLLER_RELAY_DOOR
RUNE_CONTROLLER_PLATFORM = contract.SG_MECHANISM_CONTROLLER_PLATFORM
RUNE_CONTROLLER_TELEPORT = contract.SG_MECHANISM_CONTROLLER_TELEPORT
RUNE_CONTROLLER_PUSH = contract.SG_MECHANISM_CONTROLLER_PUSH
RUNE_CONTROLLER_TRAIN = contract.SG_MECHANISM_CONTROLLER_TRAIN
RUNE_CONTROLLER_TRAIN_SHOOT = contract.SG_MECHANISM_CONTROLLER_TRAIN_SHOOT


def _carrier_door_spawnflags(spawnflags: int) -> bool:
    return spawnflags in (4, 5)

RUNE_CALLBACK_TOUCH_MULTI = 1
RUNE_CALLBACK_TOUCH_DOOR_TRIGGER = 2
RUNE_CALLBACK_BUTTON_TOUCH = 3
RUNE_CALLBACK_USE_MULTI = 4
RUNE_CALLBACK_BUTTON_USE = 5
RUNE_CALLBACK_BLOCKED_DOOR = 8
RUNE_CALLBACK_USE_TRIGGER_RELAY = 9
RUNE_CALLBACK_USE_DOOR = 10
RUNE_CALLBACK_THINK_CALC_MOVE_SPEED = 11
RUNE_CALLBACK_THINK_SPAWN_DOOR_TRIGGER = 12
RUNE_CALLBACK_TOUCH_PLAT_CENTER = 14
RUNE_CALLBACK_TRAIN_USE = 18
RUNE_CALLBACK_FUNC_TRAIN_FIND = 19
RUNE_CALLBACK_TRAIN_NEXT = 20
RUNE_CALLBACK_BLOCKED_TRAIN = 22
RUNE_CALLBACK_TRIGGER_PUSH_TOUCH = 25
RUNE_CALLBACK_TELEPORTER_TOUCH = 26
RUNE_CALLBACK_PATH_CORNER_TOUCH = 27
RUNE_CALLBACK_USE_TARGET_SPEAKER = 32
RUNE_CALLBACK_USE_AREAPORTAL = 33

RUNE_NODE_NONE = 0
RUNE_NODE_TRIGGER = 1
RUNE_NODE_BUTTON = 2
RUNE_NODE_RELAY = 3
RUNE_NODE_DOOR_MASTER = 4
RUNE_NODE_DOOR_MEMBER = 5
RUNE_NODE_AUTO_DOOR_TRIGGER = 6
RUNE_NODE_PLATFORM = 7
RUNE_NODE_PLATFORM_TRIGGER = 8
RUNE_NODE_TRAIN = 9
RUNE_NODE_PATH_CORNER = 10
RUNE_NODE_ELEVATOR = 11
RUNE_NODE_PUSH_TRIGGER = 12
RUNE_NODE_TELEPORTER = 13
RUNE_NODE_TELEPORT_TRIGGER = 14
RUNE_NODE_TELEPORT_DEST = 15
RUNE_NODE_OBJECTIVE = 16
RUNE_NODE_SECRET_DOOR = 17
RUNE_NODE_OTHER_TRIGGER = 18
RUNE_NODE_OTHER_MOVER = 19
RUNE_NODE_CONTEXTUAL = 20
RUNE_NODE_TARGET_SPEAKER = 21
RUNE_NODE_AREAPORTAL = 22
RUNE_NODEF_SYNTHETIC = 1
RUNE_NODEF_REPEATABLE = 2
RUNE_NODEF_TOUCHABLE = 4
RUNE_NODEF_USABLE = 8
RUNE_NODEF_MOVER = 16
RUNE_NODEF_TEAM_MASTER = 32
RUNE_NODEF_TEAM_MEMBER = 64
RUNE_NODEF_INVENTORY_ONLY = 128
RUNE_NODEF_ONE_SHOT = 256
RUNE_NODEF_SHOOTABLE = 512
RUNE_NODEF_START_DISABLED = 1024
RUNE_NODEF_FRAME_COMPLETE_MOVER = 2048
RUNE_NODE_FLAG_MASK = 4095
RUNE_CALLBACK_UNKNOWN = 0xFFFF
RUNE_CALLBACK_MAX_KNOWN = 33

_RUNE_TOUCH_CALLBACKS = frozenset((0, 1, 2, 3, 14, 25, 26, 27, 28, 0xFFFF))
_RUNE_USE_CALLBACKS = frozenset(
    (0, 4, 5, 9, 10, 13, 15, 18, 23, 30, 32, 33, 0xFFFF)
)
_RUNE_THINK_CALLBACKS = frozenset((0, 6, 7, 11, 12, 16, 19, 20, 21, 24, 29, 0xFFFF))
_RUNE_BLOCKED_CALLBACKS = frozenset((0, 8, 17, 22, 31, 0xFFFF))

RUNE_NODE_KIND_NAMES = {
    RUNE_NODE_NONE: "none",
    RUNE_NODE_TRIGGER: "trigger",
    RUNE_NODE_BUTTON: "button",
    RUNE_NODE_RELAY: "relay",
    RUNE_NODE_DOOR_MASTER: "door_master",
    RUNE_NODE_DOOR_MEMBER: "door_member",
    RUNE_NODE_AUTO_DOOR_TRIGGER: "auto_door_trigger",
    RUNE_NODE_PLATFORM: "platform",
    RUNE_NODE_PLATFORM_TRIGGER: "platform_trigger",
    RUNE_NODE_TRAIN: "train",
    RUNE_NODE_PATH_CORNER: "path_corner",
    RUNE_NODE_ELEVATOR: "elevator",
    RUNE_NODE_PUSH_TRIGGER: "push_trigger",
    RUNE_NODE_TELEPORTER: "teleporter",
    RUNE_NODE_TELEPORT_TRIGGER: "teleport_trigger",
    RUNE_NODE_TELEPORT_DEST: "teleport_dest",
    RUNE_NODE_OBJECTIVE: "objective",
    RUNE_NODE_SECRET_DOOR: "secret_door",
    RUNE_NODE_OTHER_TRIGGER: "other_trigger",
    RUNE_NODE_OTHER_MOVER: "other_mover",
    RUNE_NODE_CONTEXTUAL: "contextual",
    RUNE_NODE_TARGET_SPEAKER: "target_speaker",
    RUNE_NODE_AREAPORTAL: "areaportal",
}

RUNE_EDGE_TARGET = 1
RUNE_EDGE_KILLTARGET = 2
RUNE_EDGE_OWNER = 3
RUNE_EDGE_TEAM = 4
RUNE_EDGE_PATH_TARGET = 5
RUNE_EDGE_MOVE_TARGET = 6
RUNE_EDGE_TARGET_ENT = 7
RUNE_EDGE_ENEMY = 8
RUNE_EDGE_ROUTE_TARGET = 9

RUNE_EDGE_KIND_NAMES = {
    RUNE_EDGE_TARGET: "target",
    RUNE_EDGE_KILLTARGET: "killtarget",
    RUNE_EDGE_OWNER: "owner",
    RUNE_EDGE_TEAM: "team",
    RUNE_EDGE_PATH_TARGET: "path_target",
    RUNE_EDGE_MOVE_TARGET: "move_target",
    RUNE_EDGE_TARGET_ENT: "target_ent",
    RUNE_EDGE_ENEMY: "enemy",
    RUNE_EDGE_ROUTE_TARGET: "route_target",
}

_RUNE_TRIGGER_NODE_KINDS = frozenset(
    (
        RUNE_NODE_TRIGGER,
        RUNE_NODE_BUTTON,
        RUNE_NODE_RELAY,
        RUNE_NODE_AUTO_DOOR_TRIGGER,
        RUNE_NODE_PLATFORM_TRIGGER,
        RUNE_NODE_ELEVATOR,
        RUNE_NODE_PUSH_TRIGGER,
        RUNE_NODE_TELEPORT_TRIGGER,
        RUNE_NODE_OTHER_TRIGGER,
    )
)

assert HEADER_STRUCT.size == 128
assert RUNE_HEADER_EXTENSION_STRUCT.size == 32
assert RUNE_LINK_STRUCT.size == RUNE_LINK_BYTES
assert RUNE_ACTIVATION_NODE_STRUCT.size == RUNE_ACTIVATION_NODE_BYTES
assert RUNE_ACTIVATION_EDGE_STRUCT.size == RUNE_ACTIVATION_EDGE_BYTES
assert RUNE_ACTIVATION_PLAN_STRUCT.size == RUNE_ACTIVATION_PLAN_BYTES

assert HEADER_STRUCT.size == RUNE_PREFIX_HEADER_BYTES
assert SEED_STRUCT.size == SEED_BYTES
assert RUNE_POLICY_LINK_STRUCT.size == RUNE_POLICY_LINK_BYTES

RSF_WATER = 1
RSF_TOMBSTONE = 2
SEED_FLAG_MASK = RSF_WATER | RSF_TOMBSTONE

_MAP_NAME = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_-]{0,62}\Z")
_ZERO_12 = b"\x00" * 12
_WORLD_MIN = (
    contract.RUNE_PROOF_WORLD_FIXED_MIN /
    contract.RUNE_PROOF_WORLD_FIXED_SCALE
)
_WORLD_MAX = (
    contract.RUNE_PROOF_WORLD_FIXED_MAX /
    contract.RUNE_PROOF_WORLD_FIXED_SCALE
)

_RUNE_WIRE_DIAGNOSTICS: Mapping[int, tuple[str, str]] = {
    128: ("RLRUNE_BAD_MECHANISM_CONTRACT", "bad current mechanism contract"),
    129: ("RLRUNE_BAD_ACTIVATION_NODE", "bad current activation node"),
    130: ("RLRUNE_BAD_ACTIVATION_EDGE", "bad current activation edge"),
    131: ("RLRUNE_BAD_ACTIVATION_PLAN", "bad current activation plan"),
    132: ("RLRUNE_BAD_STRING_POOL", "bad current string pool"),
    133: ("RLRUNE_DUPLICATE_NODE_KEY", "duplicate current node key"),
    134: ("RLRUNE_BAD_MECHANISM_GRAPH", "bad current mechanism graph"),
}

RLRUNE_BAD_MECHANISM_CONTRACT = 128
RLRUNE_BAD_ACTIVATION_NODE = 129
RLRUNE_BAD_ACTIVATION_EDGE = 130
RLRUNE_BAD_ACTIVATION_PLAN = 131
RLRUNE_BAD_STRING_POOL = 132
RLRUNE_DUPLICATE_NODE_KEY = 133
RLRUNE_BAD_MECHANISM_GRAPH = 134


class RuneWireError(ValueError):
    """A stable generated wire diagnostic plus optional record context."""

    def __init__(self, code: int, detail: str | None = None):
        try:
            diagnostic = contract.WIRE_DIAGNOSTIC_BY_ID[code]
            symbol = diagnostic["symbol"]
            message = diagnostic["message"]
        except (AttributeError, KeyError):
            try:
                symbol, message = _RUNE_WIRE_DIAGNOSTICS[code]
            except KeyError as exc:
                raise AssertionError(
                    f"unknown wire diagnostic {code!r}"
                ) from exc
        self.code = code
        self.symbol = symbol
        self.message = message
        self.detail = detail
        text = f"{self.symbol}: {self.message}"
        if detail:
            text += f": {detail}"
        super().__init__(text)


def _wire_error(code: int, detail: str | None = None) -> RuneWireError:
    return RuneWireError(code, detail)


@dataclass(frozen=True)
class RuneIdentity:
    """External map and movement identity for artifact authentication."""

    map_name: str
    bsp_checksum: int
    entity_crc32: int
    gravity: float
    airaccelerate: float
    maxvelocity: float
    host_physics_id: int
    physics_flags: int = contract.RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED
    pmove_substep_ms: int = contract.RUNE_PROOF_PMOVE_SUBSTEP_MS
    server_frame_ms: int = contract.RUNE_PROOF_SERVER_FRAME_MS


@dataclass(frozen=True)
class RuneSeed:
    origin: tuple[float, float, float]
    area_hint: int = 0
    flags: int = 0


@dataclass(frozen=True)
class RunePolicyLink:
    source: int
    destination: int
    action: int
    provenance: int
    min_speed: int
    heading: int
    heading_slack: int
    exit_speed: int
    cost_ms: int
    suffix_anchor: tuple[float, float, float] = (0.0, 0.0, 0.0)
    mechanism_anchor: tuple[float, float, float] = (0.0, 0.0, 0.0)
    sweep_clear_ms: int = 0
    mode: int = contract.RLCM_NONE
    reserved: int = 0



@dataclass(frozen=True)
class RuneHeader:
    """Authenticated RUNE header."""

    magic: int
    header_bytes: int
    seed_bytes: int
    link_bytes: int
    num_seeds: int
    num_links: int
    payload_crc32: int
    bsp_checksum: int
    entity_crc32: int
    action_contract_crc32: int
    physics_flags: int
    gravity: float
    airaccelerate: float
    maxvelocity: float
    pmove_substep_ms: int
    server_frame_ms: int
    host_physics_id: int
    header_crc32: int
    map_name: str
    activation_node_bytes: int
    activation_edge_bytes: int
    activation_plan_bytes: int
    num_activation_nodes: int
    num_activation_edges: int
    num_activation_plans: int
    string_bytes: int
    mechanism_contract_crc32: int
    num_inventory_edges: int

    @property
    def identity(self) -> RuneIdentity:
        return RuneIdentity(
            map_name=self.map_name,
            bsp_checksum=self.bsp_checksum,
            entity_crc32=self.entity_crc32,
            gravity=self.gravity,
            airaccelerate=self.airaccelerate,
            maxvelocity=self.maxvelocity,
            host_physics_id=self.host_physics_id,
            physics_flags=self.physics_flags,
            pmove_substep_ms=self.pmove_substep_ms,
            server_frame_ms=self.server_frame_ms,
        )

    @property
    def num_plan_edges(self) -> int:
        return self.num_activation_edges - self.num_inventory_edges


@dataclass(frozen=True)
class RuneLink:
    source: int
    destination: int
    action: int
    provenance: int
    min_speed: int
    heading: int
    heading_slack: int
    exit_speed: int
    cost_ms: int
    suffix_anchor: tuple[float, float, float]
    mechanism_anchor: tuple[float, float, float]
    sweep_clear_ms: int
    mode: int
    reserved: int
    activation_plan: int


@dataclass(frozen=True)
class RuneActivationNode:
    key: int
    kind: int
    flags: int
    classname_offset: int
    target_offset: int
    targetname_offset: int
    killtarget_offset: int
    owner_key: int
    team_master_key: int
    spawnflags: int
    touch_callback: int
    use_callback: int
    think_callback: int
    blocked_callback: int
    delay_ms: int
    wait_ms: int
    speed_q8: int
    accel_q8: int
    decel_q8: int
    absmin_q8: tuple[int, int, int]
    absmax_q8: tuple[int, int, int]
    path_target_offset: int
    push_velocity: tuple[float, float, float]

    @property
    def kind_name(self) -> str:
        return RUNE_NODE_KIND_NAMES.get(self.kind, f"unknown_{self.kind}")


@dataclass(frozen=True)
class RuneActivationEdge:
    from_key: int
    to_key: int
    kind: int
    ordinal: int
    delay_ms: int

    @property
    def kind_name(self) -> str:
        return RUNE_EDGE_KIND_NAMES.get(self.kind, f"unknown_{self.kind}")


@dataclass(frozen=True)
class RuneActivationPlan:
    entry_key: int
    mover_key: int
    first_edge: int
    num_edges: int
    controller_kind: int
    flags: int
    expected_members: int
    cooldown_ms: int
    closure_crc32: int


@dataclass(frozen=True)
class RuneArtifact:
    """One authenticated RUNE artifact and its ordered records."""

    header: RuneHeader
    seeds: tuple[RuneSeed, ...]
    links: tuple[RuneLink, ...]
    activation_nodes: tuple[RuneActivationNode, ...]
    activation_edges: tuple[RuneActivationEdge, ...]
    activation_plans: tuple[RuneActivationPlan, ...]
    strings: bytes
    payload: bytes

    @property
    def identity(self) -> RuneIdentity:
        return self.header.identity

    @property
    def inventory_edges(self) -> tuple[RuneActivationEdge, ...]:
        return self.activation_edges[:self.header.num_inventory_edges]

    @property
    def plan_edges(self) -> tuple[RuneActivationEdge, ...]:
        return self.activation_edges[self.header.num_inventory_edges:]

    @property
    def trigger_count(self) -> int:
        return sum(
            node.kind in _RUNE_TRIGGER_NODE_KINDS
            for node in self.activation_nodes
        )

    def string_at(self, offset: int) -> str:
        """Return one already-validated ASCII string-pool entry."""

        if type(offset) is not int or not 0 <= offset < len(self.strings):
            raise IndexError(offset)
        end = self.strings.find(b"\0", offset)
        if end < 0 or (offset and self.strings[offset - 1] != 0):
            raise IndexError(offset)
        return self.strings[offset:end].decode("ascii")


def _as_tuple(
    records: Iterable[object], name: str, maximum: int
) -> tuple[object, ...]:
    try:
        iterator = iter(records)
    except TypeError as exc:
        raise _wire_error(
            contract.RLW_INVALID_ARGUMENT, f"{name} must be iterable"
        ) from exc
    bounded = []
    for record in iterator:
        if len(bounded) == maximum:
            raise _wire_error(
                contract.RLW_BAD_COUNTS,
                f"more than {maximum} {name}",
            )
        bounded.append(record)
    return tuple(bounded)


def _crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _f32(value: object, field: str, code: int) -> tuple[float, bytes]:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise _wire_error(code, f"{field} must be a real number")
    try:
        encoded = struct.pack("<f", value)
    except (OverflowError, struct.error) as exc:
        raise _wire_error(code, f"{field} is outside finite f32 range") from exc
    decoded = struct.unpack("<f", encoded)[0]
    if not math.isfinite(decoded):
        raise _wire_error(code, f"{field} must be finite")
    return decoded, encoded


def _u32(value: object, field: str) -> int:
    if type(value) is not int or not 0 <= value <= 0xFFFFFFFF:
        raise _wire_error(
            contract.RLW_INVALID_ARGUMENT, f"{field} must be a u32 integer"
        )
    return value


def _bounded_int(
    value: object,
    low: int,
    high: int,
    field: str,
    code: int = contract.RLW_INVALID_ARGUMENT,
) -> int:
    if type(value) is not int or not low <= value <= high:
        raise _wire_error(
            code,
            f"{field} must be an integer in {low}..{high}",
        )
    return value


def _map_bytes(map_name: object) -> bytes:
    if type(map_name) is not str or not _MAP_NAME.fullmatch(map_name):
        raise _wire_error(contract.RLW_BAD_MAPNAME, repr(map_name))
    encoded = map_name.encode("ascii")
    return encoded + b"\x00" * (MAP_NAME_BYTES - len(encoded))


def _decode_map_name(raw: bytes) -> str:
    nul = raw.find(b"\x00")
    if nul <= 0 or any(raw[nul:]):
        raise _wire_error(contract.RLW_BAD_MAPNAME, "missing NUL or nonzero tail")
    try:
        map_name = raw[:nul].decode("ascii")
    except UnicodeDecodeError as exc:
        raise _wire_error(contract.RLW_BAD_MAPNAME, "non-ASCII bytes") from exc
    if not _MAP_NAME.fullmatch(map_name):
        raise _wire_error(contract.RLW_BAD_MAPNAME, repr(map_name))
    return map_name


def _vector_f32(
    value: object, field: str, code: int
) -> tuple[tuple[float, float, float], bytes]:
    if not isinstance(value, (tuple, list)) or len(value) != 3:
        raise _wire_error(code, f"{field} must contain exactly three values")
    converted = [_f32(item, f"{field}[{index}]", code) for index, item in enumerate(value)]
    return (
        (converted[0][0], converted[1][0], converted[2][0]),
        b"".join(item[1] for item in converted),
    )


def _in_fixed_world(vector: tuple[float, float, float]) -> bool:
    return all(_WORLD_MIN <= component <= _WORLD_MAX for component in vector)


def _on_fixed_lattice(vector: tuple[float, float, float], scale: int) -> bool:
    for component in vector:
        scaled = component * scale
        if scaled != int(scaled):
            return False
        if not (
            contract.RUNE_PROOF_WORLD_FIXED_MIN <= scaled <=
            contract.RUNE_PROOF_WORLD_FIXED_MAX
        ):
            return False
    return True


def _normalize_identity(identity: object) -> RuneIdentity:
    if not isinstance(identity, RuneIdentity):
        raise _wire_error(
            contract.RLW_INVALID_ARGUMENT, "identity must be RuneIdentity"
        )
    _map_bytes(identity.map_name)
    bsp_checksum = _u32(identity.bsp_checksum, "bsp_checksum")
    entity_crc32 = _u32(identity.entity_crc32, "entity_crc32")
    physics_flags = _u32(identity.physics_flags, "physics_flags")
    host_physics_id = _u32(identity.host_physics_id, "host_physics_id")
    gravity, _ = _f32(
        identity.gravity, "gravity", contract.RLW_BAD_PHYSICS_LAW
    )
    airaccelerate, _ = _f32(
        identity.airaccelerate, "airaccelerate", contract.RLW_BAD_PHYSICS_LAW
    )
    maxvelocity, _ = _f32(
        identity.maxvelocity, "maxvelocity", contract.RLW_BAD_PHYSICS_LAW
    )
    pmove_substep_ms = _bounded_int(
        identity.pmove_substep_ms,
        0,
        0xFFFF,
        "pmove_substep_ms",
        contract.RLW_BAD_PHYSICS_LAW,
    )
    server_frame_ms = _bounded_int(
        identity.server_frame_ms,
        0,
        0xFFFF,
        "server_frame_ms",
        contract.RLW_BAD_PHYSICS_LAW,
    )
    if physics_flags != contract.RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED:
        raise _wire_error(
            contract.RLW_BAD_PHYSICS_LAW,
            f"unsupported physics flags 0x{physics_flags:08x}",
        )
    if (
        gravity < contract.RUNE_PROOF_GRAVITY_MIN or
        gravity > contract.RUNE_PROOF_GRAVITY_MAX or
        (
            contract.RUNE_PROOF_GRAVITY_INTEGRAL_REQUIRED and
            gravity != int(gravity)
        )
    ):
        raise _wire_error(contract.RLW_BAD_PHYSICS_LAW, f"gravity {gravity!r}")
    if (
        contract.RUNE_PROOF_AIRACCELERATE_ZERO_REQUIRED and
        airaccelerate != 0.0
    ):
        raise _wire_error(
            contract.RLW_BAD_PHYSICS_LAW,
            f"airaccelerate {airaccelerate!r}",
        )
    if maxvelocity < contract.RUNE_PROOF_MAXVELOCITY_MIN:
        raise _wire_error(
            contract.RLW_BAD_PHYSICS_LAW, f"maxvelocity {maxvelocity!r}"
        )
    if pmove_substep_ms != contract.RUNE_PROOF_PMOVE_SUBSTEP_MS:
        raise _wire_error(
            contract.RLW_BAD_PHYSICS_LAW,
            f"pmove substep {pmove_substep_ms!r}",
        )
    if server_frame_ms != contract.RUNE_PROOF_SERVER_FRAME_MS:
        raise _wire_error(
            contract.RLW_BAD_PHYSICS_LAW,
            f"server frame {server_frame_ms!r}",
        )
    if host_physics_id < contract.RUNE_PROOF_HOST_PHYSICS_ID_MIN:
        raise _wire_error(
            contract.RLW_IDENTITY_UNAVAILABLE, "host physics ID is zero"
        )
    return RuneIdentity(
        map_name=identity.map_name,
        bsp_checksum=bsp_checksum,
        entity_crc32=entity_crc32,
        gravity=gravity,
        airaccelerate=airaccelerate,
        maxvelocity=maxvelocity,
        host_physics_id=host_physics_id,
        physics_flags=physics_flags,
        pmove_substep_ms=pmove_substep_ms,
        server_frame_ms=server_frame_ms,
    )


def _verify_expected_identity(
    actual: RuneIdentity, expected: RuneIdentity
) -> None:
    """Require an artifact identity to match one caller-supplied authority."""

    expected = _normalize_identity(expected)
    if actual.map_name != expected.map_name:
        raise _wire_error(
            contract.RLW_MAPNAME_MISMATCH,
            f"header={actual.map_name!r}, expected={expected.map_name!r}",
        )
    if actual.bsp_checksum != expected.bsp_checksum:
        raise _wire_error(
            contract.RLW_BSP_CHECKSUM_MISMATCH,
            f"header=0x{actual.bsp_checksum:08x}, "
            f"expected=0x{expected.bsp_checksum:08x}",
        )
    if actual.entity_crc32 != expected.entity_crc32:
        raise _wire_error(
            contract.RLW_ENTITY_CRC_MISMATCH,
            f"header=0x{actual.entity_crc32:08x}, "
            f"expected=0x{expected.entity_crc32:08x}",
        )
    if actual.host_physics_id != expected.host_physics_id:
        raise _wire_error(
            contract.RLW_PHYSICS_ID_MISMATCH,
            f"header={actual.host_physics_id}, "
            f"expected={expected.host_physics_id}",
        )
    actual_law = (
        actual.physics_flags,
        struct.pack("<f", actual.gravity),
        struct.pack("<f", actual.airaccelerate),
        struct.pack("<f", actual.maxvelocity),
        actual.pmove_substep_ms,
        actual.server_frame_ms,
    )
    expected_law = (
        expected.physics_flags,
        struct.pack("<f", expected.gravity),
        struct.pack("<f", expected.airaccelerate),
        struct.pack("<f", expected.maxvelocity),
        expected.pmove_substep_ms,
        expected.server_frame_ms,
    )
    if actual_law != expected_law:
        raise _wire_error(
            contract.RLW_BAD_PHYSICS_LAW,
            "header does not match expected active physics",
        )


def _encode_seed(seed: object, index: int) -> tuple[RuneSeed, bytes]:
    if not isinstance(seed, RuneSeed):
        raise _wire_error(
            contract.RLW_INVALID_ARGUMENT,
            f"seed {index} must be RuneSeed",
        )
    origin, _ = _vector_f32(
        seed.origin, f"seed {index} origin", contract.RLW_BAD_SEED_RECORD
    )
    if not _in_fixed_world(origin):
        raise _wire_error(
            contract.RLW_BAD_SEED_RECORD,
            f"seed {index} origin outside fixed world",
        )
    if type(seed.area_hint) is not int or not 0 <= seed.area_hint <= 255:
        raise _wire_error(
            contract.RLW_BAD_SEED_RECORD,
            f"seed {index} area_hint {seed.area_hint!r}",
        )
    if type(seed.flags) is not int or seed.flags & ~SEED_FLAG_MASK:
        raise _wire_error(
            contract.RLW_BAD_SEED_RECORD,
            f"seed {index} flags {seed.flags!r}",
        )
    normalized = RuneSeed(origin, seed.area_hint, seed.flags)
    return normalized, SEED_STRUCT.pack(*origin, seed.area_hint, seed.flags)


def _encode_link(link: object, index: int) -> tuple[RunePolicyLink, bytes]:
    if not isinstance(link, RunePolicyLink):
        raise _wire_error(
            contract.RLW_INVALID_ARGUMENT,
            f"link {index} must be RunePolicyLink",
        )
    source = _bounded_int(
        link.source,
        0,
        0xFFFFFFFF,
        f"link {index} source",
        contract.RLW_BAD_LINK_RECORD,
    )
    destination = _bounded_int(
        link.destination,
        0,
        0xFFFFFFFF,
        f"link {index} destination",
        contract.RLW_BAD_LINK_RECORD,
    )
    action = _bounded_int(
        link.action, 0, 255, f"link {index} action",
        contract.RLW_BAD_LINK_RECORD,
    )
    provenance = _bounded_int(
        link.provenance, 0, 255, f"link {index} provenance",
        contract.RLW_BAD_LINK_RECORD,
    )
    min_speed = _bounded_int(
        link.min_speed, 0, 255, f"link {index} min_speed",
        contract.RLW_BAD_LINK_RECORD,
    )
    heading = _bounded_int(
        link.heading, 0, 255, f"link {index} heading",
        contract.RLW_BAD_LINK_RECORD,
    )
    heading_slack = _bounded_int(
        link.heading_slack, 0, 255, f"link {index} heading_slack",
        contract.RLW_BAD_LINK_RECORD,
    )
    exit_speed = _bounded_int(
        link.exit_speed, 0, 255, f"link {index} exit_speed",
        contract.RLW_BAD_LINK_RECORD,
    )
    cost_ms = _bounded_int(
        link.cost_ms,
        MIN_COST_MS,
        MAX_COST_MS,
        f"link {index} cost_ms",
        contract.RLW_BAD_LINK_RECORD,
    )
    suffix_anchor, _ = _vector_f32(
        link.suffix_anchor,
        f"link {index} suffix_anchor",
        contract.RLW_BAD_LINK_RECORD,
    )
    mechanism_anchor, _ = _vector_f32(
        link.mechanism_anchor,
        f"link {index} mechanism_anchor",
        contract.RLW_BAD_LINK_RECORD,
    )
    sweep_clear_ms = _bounded_int(
        link.sweep_clear_ms, 0, 0xFFFF, f"link {index} sweep_clear_ms",
        contract.RLW_BAD_LINK_RECORD,
    )
    mode = _bounded_int(
        link.mode, 0, 255, f"link {index} mode",
        contract.RLW_BAD_LINK_RECORD,
    )
    reserved = _bounded_int(
        link.reserved, 0, 255, f"link {index} reserved",
        contract.RLW_BAD_LINK_RECORD,
    )
    normalized = RunePolicyLink(
        source=source,
        destination=destination,
        action=action,
        provenance=provenance,
        min_speed=min_speed,
        heading=heading,
        heading_slack=heading_slack,
        exit_speed=exit_speed,
        cost_ms=cost_ms,
        suffix_anchor=suffix_anchor,
        mechanism_anchor=mechanism_anchor,
        sweep_clear_ms=sweep_clear_ms,
        mode=mode,
        reserved=reserved,
    )
    encoded = RUNE_POLICY_LINK_STRUCT.pack(
        source,
        destination,
        action,
        provenance,
        min_speed,
        heading,
        heading_slack,
        exit_speed,
        cost_ms,
        *suffix_anchor,
        *mechanism_anchor,
        sweep_clear_ms,
        mode,
        reserved,
    )
    return normalized, encoded


_WORLD_ANCHOR_POLICIES = frozenset(
    (
        contract.RLAP_RUN_WAYPOINT,
        contract.RLAP_DROP_LIP,
        contract.RLAP_WORLD,
        contract.RLAP_TELEPORT_PAD,
        contract.RLAP_DOOR_WAIT,
        contract.RLAP_DOOR_PREOPEN_CONTACT,
        contract.RLAP_DOOR_RIDE_INGRESS_LIP,
        contract.RLAP_TRAIN_CROSS,
    )
)
_FIXED_DOOR_ANCHOR_POLICIES = frozenset(
    (
        contract.RLAP_DOOR_WAIT,
        contract.RLAP_DOOR_PREOPEN_CONTACT,
        contract.RLAP_DOOR_RIDE_INGRESS_LIP,
        contract.RLAP_TRAIN_CROSS,
    )
)


def _endpoint_allowed(policy: int, from_water: bool, to_water: bool) -> bool:
    if policy == contract.RLEP_ANY:
        return True
    if policy == contract.RLEP_DRY_BOTH:
        return not from_water and not to_water
    if policy == contract.RLEP_FROM_DRY:
        return not from_water
    if policy == contract.RLEP_AT_LEAST_ONE_WATER:
        return from_water or to_water
    if policy == contract.RLEP_NOT_BOTH_WATER:
        return not (from_water and to_water)
    if policy == contract.RLEP_FROM_WATER:
        return from_water
    if policy == contract.RLEP_WATER_TO_DRY:
        return from_water and not to_water
    return False


def _validate_anchor_policy(
    anchor: tuple[float, float, float],
    raw: bytes,
    policy: int,
    index: int,
    field: str,
) -> None:
    if not all(math.isfinite(component) for component in anchor):
        raise _wire_error(
            contract.RLW_BAD_LINK_RECORD,
            f"link {index} has non-finite {field}",
        )
    if policy == contract.RLAP_ZERO:
        if raw != _ZERO_12:
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has nonzero {field}",
            )
        return
    if policy in _WORLD_ANCHOR_POLICIES:
        if not _in_fixed_world(anchor):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} {field} is outside fixed world",
            )
        if (
            policy in _FIXED_DOOR_ANCHOR_POLICIES and
            not _on_fixed_lattice(anchor, contract.RUNE_PROOF_DOOR_ANCHOR_SCALE)
        ):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} {field} is off the 1/8 door lattice",
            )
        return
    if policy == contract.RLAP_ROCKET_CONTROL:
        pitch, yaw, health = anchor
        if (
            pitch != int(pitch) or yaw != int(yaw) or health != int(health) or
            not -32768 <= pitch <= 32767 or
            not -32768 <= yaw <= 32767 or
            not contract.RUNE_PROOF_ROCKETJUMP_HEALTH_MIN <= health <=
                contract.RUNE_PROOF_ROCKETJUMP_HEALTH_MAX
        ):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has noncanonical rocket control",
            )
        return
    if policy != contract.RLAP_HOOK_CONTROL:
        raise _wire_error(
            contract.RLW_BAD_LINK_RECORD,
            f"link {index} has unknown {field} policy {policy}",
        )


def _validate_graph(
    seeds: tuple[RuneSeed, ...],
    links: tuple[RunePolicyLink, ...],
    raw_links: tuple[bytes, ...],
    activation_plans: tuple[int, ...],
) -> None:
    if not 0 < len(seeds) <= MAX_SEEDS:
        raise _wire_error(
            contract.RLW_BAD_COUNTS, f"{len(seeds)} seeds"
        )
    if not 0 <= len(links) <= MAX_LINKS:
        raise _wire_error(
            contract.RLW_BAD_COUNTS, f"{len(links)} links"
        )
    if len(raw_links) != len(links) or len(activation_plans) != len(links):
        raise AssertionError("link metadata count does not match decoded links")

    for index, seed in enumerate(seeds):
        if (
            len(seed.origin) != 3 or
            not all(math.isfinite(component) for component in seed.origin) or
            not _in_fixed_world(seed.origin) or
            not 0 <= seed.area_hint <= 255 or
            seed.flags & ~SEED_FLAG_MASK
        ):
            raise _wire_error(
                contract.RLW_BAD_SEED_RECORD, f"seed {index}"
            )

    linked_sources: set[int] = set()
    identities: set[tuple[int, int, int, int]] = set()
    for index, (link, raw, activation_plan) in enumerate(
        zip(links, raw_links, activation_plans)
    ):
        if len(raw) != RUNE_POLICY_LINK_BYTES:
            raise AssertionError("raw link has wrong internal size")
        if (
            not 0 <= link.source < len(seeds) or
            not 0 <= link.destination < len(seeds)
        ):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} endpoint outside seed array",
            )
        if link.source == link.destination:
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD, f"link {index} is a self-link"
            )
        if not contract.action_valid(link.action):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has an unknown action {link.action}",
            )
        if not 0 <= link.provenance <= contract.RL_CONTRACTED:
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has an unknown provenance "
                f"{link.provenance}",
            )
        if not contract.RLCM_NONE <= link.mode <= contract.RLCM_RIDE:
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has an unknown mode {link.mode}",
            )
        try:
            action = contract.action_contract(link.action)
        except (TypeError, ValueError) as exc:
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has unknown action {link.action}",
            ) from exc
        identity = (
            link.source,
            link.destination,
            link.action,
            activation_plan,
        )
        if identity in identities:
            raise _wire_error(
                contract.RLW_DUPLICATE_LINK,
                f"link {index} repeats {identity}",
            )
        identities.add(identity)

        if (
            seeds[link.source].flags & RSF_TOMBSTONE or
            seeds[link.destination].flags & RSF_TOMBSTONE
        ):
            raise _wire_error(
                contract.RLW_BAD_ROUTE_OWNERSHIP,
                f"link {index} touches a tombstone seed",
            )
        linked_sources.add(link.source)
        if not contract.allows_provenance(link.action, link.provenance):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} forbids provenance {link.provenance}",
            )
        if not contract.allows_mode(link.action, link.mode):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} forbids mode {link.mode}",
            )
        if not (
            MIN_COST_MS <= link.cost_ms <=
            MAX_COST_MS
        ):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has cost {link.cost_ms}",
            )
        if link.action == contract.RL_DROP and (
            link.cost_ms < contract.RUNE_PROOF_SERVER_FRAME_MS or
            link.cost_ms >= contract.RUNE_PROOF_DROP_TOTAL_MS or
            link.cost_ms % contract.RUNE_PROOF_SERVER_FRAME_MS != 0
        ):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has noncanonical DROP cost {link.cost_ms}",
            )
        if link.action == contract.RL_ROCKETJUMP and (
            link.provenance != contract.RL_PROVEN or
            link.min_speed != 0 or
            link.heading_slack != contract.RUNE_PROOF_ROCKETJUMP_HEADING_SLACK or
            link.cost_ms > contract.RUNE_PROOF_ROCKETJUMP_TOTAL_MS or
            link.cost_ms % contract.RUNE_PROOF_PMOVE_SUBSTEP_MS != 0
        ):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has noncanonical rocket control fields",
            )
        if link.reserved != 0:
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} reserved byte is nonzero",
            )

        from_water = bool(seeds[link.source].flags & RSF_WATER)
        to_water = bool(seeds[link.destination].flags & RSF_WATER)
        if not _endpoint_allowed(
            action["endpoint_policy"], from_water, to_water
        ):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} violates endpoint water policy",
            )

        _validate_anchor_policy(
            link.suffix_anchor,
            raw[16:28],
            action["suffix_anchor_policy"],
            index,
            "suffix anchor",
        )
        compound = bool(action["trait_mask"] & contract.SG_ACTF_ATOMIC)
        if not compound:
            if raw[28:40] != _ZERO_12 or link.sweep_clear_ms != 0:
                raise _wire_error(
                    contract.RLW_BAD_LINK_RECORD,
                    f"link {index} has nonzero noncompound mechanism proof",
                )
            continue

        if (
            link.sweep_clear_ms <= 0 or
            link.sweep_clear_ms % contract.RUNE_PROOF_SERVER_FRAME_MS != 0 or
            link.sweep_clear_ms > link.cost_ms
        ):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has invalid sweep-clear time",
            )
        if action["mechanism_policy"] not in (
            contract.RLMP_DOOR_WORLD_FIXED_1_8,
            contract.RLMP_TRAIN_WORLD_FIXED_1_8,
        ):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has unknown mechanism policy",
            )
        if link.mode == contract.RLCM_PREOPEN:
            mechanism_anchor_policy = action[
                "preopen_mechanism_anchor_policy"
            ]
        elif link.mode == contract.RLCM_RIDE:
            mechanism_anchor_policy = action["ride_mechanism_anchor_policy"]
        else:
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} compound mode is NONE",
            )
        _validate_anchor_policy(
            link.mechanism_anchor,
            raw[28:40],
            mechanism_anchor_policy,
            index,
            "mechanism anchor",
        )
        if (
            link.action == contract.RL_BUTTON_DOOR and
            all(component == 0.0 for component in link.mechanism_anchor)
        ):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has zero BUTTON_DOOR displacement",
            )
        if (
            link.action == contract.RL_BUTTON_DOOR and
            link.mode == contract.RLCM_RIDE
        ):
            scale = contract.RUNE_PROOF_DOOR_ANCHOR_SCALE
            for axis, (anchor, displacement) in enumerate(zip(
                link.suffix_anchor, link.mechanism_anchor
            )):
                endpoint_q8 = int(anchor * scale) + int(
                    displacement * scale
                )
                if not (
                    contract.RUNE_PROOF_WORLD_FIXED_MIN <= endpoint_q8 <=
                    contract.RUNE_PROOF_WORLD_FIXED_MAX
                ):
                    raise _wire_error(
                        contract.RLW_BAD_LINK_RECORD,
                        f"link {index} BUTTON_DOOR endpoint axis {axis} "
                        "is outside signed q8 world",
                    )

    for index, seed in enumerate(seeds):
        tombstone = bool(seed.flags & RSF_TOMBSTONE)
        if tombstone == (index in linked_sources):
            raise _wire_error(
                contract.RLW_BAD_ROUTE_OWNERSHIP,
                f"seed {index} tombstone/outgoing identity mismatch",
            )


def _rune_mechanism_contract_crc32() -> int:
    """Return the generated mechanism-contract pin, or fail closed."""

    value = contract.RUNE_MECHANISM_CONTRACT_CRC32
    if type(value) is not int or not 0 < value <= 0xFFFFFFFF:
        raise _wire_error(
            RLRUNE_BAD_MECHANISM_CONTRACT,
            "mechanism descriptor CRC is not pinned",
        )
    return value


def _rune_header_crc(header: bytes) -> int:
    if len(header) != RUNE_HEADER_BYTES:
        raise AssertionError("current header CRC received wrong byte count")
    canonical = bytearray(header)
    struct.pack_into("<I", canonical, RUNE_HEADER_CRC_OFFSET, 0)
    return _crc32(canonical)


def _rune_file_size(header: RuneHeader) -> int:
    """Validate count relationships and return exact file size."""

    if not 0 < header.num_seeds <= MAX_SEEDS:
        raise _wire_error(
            contract.RLW_BAD_COUNTS, f"{header.num_seeds} seeds"
        )
    if not 0 <= header.num_links <= MAX_LINKS:
        raise _wire_error(
            contract.RLW_BAD_COUNTS, f"{header.num_links} links"
        )
    if not 0 <= header.num_activation_nodes <= RUNE_MAX_ACTIVATION_NODES:
        raise _wire_error(
            contract.RLW_BAD_COUNTS,
            f"{header.num_activation_nodes} activation nodes",
        )
    if not 0 <= header.num_activation_edges <= RUNE_MAX_ACTIVATION_EDGES:
        raise _wire_error(
            contract.RLW_BAD_COUNTS,
            f"{header.num_activation_edges} activation edges",
        )
    if not 0 <= header.num_activation_plans <= RUNE_MAX_ACTIVATION_PLANS:
        raise _wire_error(
            contract.RLW_BAD_COUNTS,
            f"{header.num_activation_plans} activation plans",
        )
    if header.num_activation_plans > header.num_links:
        raise _wire_error(
            contract.RLW_BAD_COUNTS,
            "activation plans exceed links",
        )
    if not 0 < header.string_bytes <= RUNE_MAX_STRING_BYTES:
        raise _wire_error(
            contract.RLW_BAD_COUNTS, f"{header.string_bytes} string bytes"
        )
    if not 0 <= header.num_inventory_edges <= header.num_activation_edges:
        raise _wire_error(
            contract.RLW_BAD_COUNTS,
            "inventory edges exceed total activation edges",
        )
    if header.num_activation_nodes == 0 and (
        header.num_inventory_edges != 0 or
        header.num_activation_edges != 0 or
        header.num_activation_plans != 0
    ):
        raise _wire_error(
            contract.RLW_BAD_COUNTS,
            "node-free artifact contains mechanism edges or plans",
        )
    if header.num_activation_plans and header.num_activation_nodes == 0:
        raise _wire_error(
            contract.RLW_BAD_COUNTS,
            "plans require at least one node",
        )
    return (
        RUNE_HEADER_BYTES +
        header.num_seeds * RUNE_SEED_BYTES +
        header.num_links * RUNE_LINK_BYTES +
        header.num_activation_nodes * RUNE_ACTIVATION_NODE_BYTES +
        header.num_activation_edges * RUNE_ACTIVATION_EDGE_BYTES +
        header.num_activation_plans * RUNE_ACTIVATION_PLAN_BYTES +
        header.string_bytes
    )


def _decode_rune_header(data: bytes) -> RuneHeader:
    prefix = HEADER_STRUCT.unpack_from(data)
    extension = RUNE_HEADER_EXTENSION_STRUCT.unpack_from(data, 128)
    header = RuneHeader(
        magic=prefix[0],
        header_bytes=prefix[2],
        seed_bytes=prefix[3],
        link_bytes=prefix[4],
        num_seeds=prefix[5],
        num_links=prefix[6],
        payload_crc32=prefix[7],
        bsp_checksum=prefix[8],
        entity_crc32=prefix[9],
        action_contract_crc32=prefix[10],
        physics_flags=prefix[11],
        gravity=prefix[12],
        airaccelerate=prefix[13],
        maxvelocity=prefix[14],
        pmove_substep_ms=prefix[15],
        server_frame_ms=prefix[16],
        host_physics_id=prefix[17],
        header_crc32=prefix[18],
        map_name=_decode_map_name(prefix[19]),
        activation_node_bytes=extension[0],
        activation_edge_bytes=extension[1],
        activation_plan_bytes=extension[2],
        num_activation_nodes=extension[4],
        num_activation_edges=extension[5],
        num_activation_plans=extension[6],
        string_bytes=extension[7],
        mechanism_contract_crc32=extension[8],
        num_inventory_edges=extension[9],
    )
    if header.magic != RUNE_MAGIC:
        raise _wire_error(contract.RLW_BAD_MAGIC, f"0x{header.magic:08x}")
    if prefix[1] != 0:
        raise _wire_error(contract.RLR_NONZERO_RESERVED, str(prefix[1]))
    if header.header_bytes != RUNE_HEADER_BYTES:
        raise _wire_error(contract.RLW_BAD_HEADER_SIZE, str(header.header_bytes))
    if header.seed_bytes != RUNE_SEED_BYTES:
        raise _wire_error(contract.RLW_BAD_SEED_SIZE, str(header.seed_bytes))
    if header.link_bytes != RUNE_LINK_BYTES:
        raise _wire_error(contract.RLW_BAD_LINK_SIZE, str(header.link_bytes))
    if (
        header.activation_node_bytes != RUNE_ACTIVATION_NODE_BYTES or
        header.activation_edge_bytes != RUNE_ACTIVATION_EDGE_BYTES or
        header.activation_plan_bytes != RUNE_ACTIVATION_PLAN_BYTES
    ):
        raise _wire_error(
            RLRUNE_BAD_MECHANISM_CONTRACT,
            "record sizes do not match",
        )
    if extension[3] != 0:
        raise _wire_error(contract.RLR_NONZERO_RESERVED, str(extension[3]))
    expected_mechanism_crc = _rune_mechanism_contract_crc32()
    if header.mechanism_contract_crc32 != expected_mechanism_crc:
        raise _wire_error(
            RLRUNE_BAD_MECHANISM_CONTRACT,
            f"stored=0x{header.mechanism_contract_crc32:08x}, "
            f"expected=0x{expected_mechanism_crc:08x}",
        )
    expected_action_crc = contract.RUNE_ACTION_CONTRACT_CRC32
    if header.action_contract_crc32 != expected_action_crc:
        raise _wire_error(
            contract.RLW_BAD_ACTION_CONTRACT,
            f"stored=0x{header.action_contract_crc32:08x}, "
            f"expected=0x{expected_action_crc:08x}",
        )
    _rune_file_size(header)
    computed_header_crc = _rune_header_crc(data[:RUNE_HEADER_BYTES])
    if header.header_crc32 != computed_header_crc:
        raise _wire_error(
            contract.RLW_BAD_HEADER_CRC,
            f"stored=0x{header.header_crc32:08x}, "
            f"computed=0x{computed_header_crc:08x}",
        )
    # Normalize the embedded identity before exposing the decoded artifact.
    _normalize_identity(header.identity)
    return header


def _decode_rune_link(raw: bytes, index: int) -> RuneLink:
    values = RUNE_LINK_STRUCT.unpack(raw)
    link = RuneLink(
        source=values[0],
        destination=values[1],
        action=values[2],
        provenance=values[3],
        min_speed=values[4],
        heading=values[5],
        heading_slack=values[6],
        exit_speed=values[7],
        cost_ms=values[8],
        suffix_anchor=(values[9], values[10], values[11]),
        mechanism_anchor=(values[12], values[13], values[14]),
        sweep_clear_ms=values[15],
        mode=values[16],
        reserved=values[17],
        activation_plan=values[18],
    )
    if not contract.action_valid(link.action):
        raise _wire_error(
            contract.RLW_BAD_LINK_RECORD,
            f"link {index} has action outside current range {link.action}",
        )
    return link


def _decode_rune_node(raw: bytes, index: int) -> RuneActivationNode:
    values = RUNE_ACTIVATION_NODE_STRUCT.unpack(raw)
    node = RuneActivationNode(
        key=values[0],
        kind=values[1],
        flags=values[2],
        classname_offset=values[3],
        target_offset=values[4],
        targetname_offset=values[5],
        killtarget_offset=values[6],
        owner_key=values[7],
        team_master_key=values[8],
        spawnflags=values[9],
        touch_callback=values[10],
        use_callback=values[11],
        think_callback=values[12],
        blocked_callback=values[13],
        delay_ms=values[14],
        wait_ms=values[15],
        speed_q8=values[16],
        accel_q8=values[17],
        decel_q8=values[18],
        absmin_q8=(values[19], values[20], values[21]),
        absmax_q8=(values[22], values[23], values[24]),
        path_target_offset=values[25],
        push_velocity=(values[26], values[27], values[28]),
    )
    callbacks = (
        node.touch_callback,
        node.use_callback,
        node.think_callback,
        node.blocked_callback,
    )
    valid_callbacks = (
        node.touch_callback in _RUNE_TOUCH_CALLBACKS and
        node.use_callback in _RUNE_USE_CALLBACKS and
        node.think_callback in _RUNE_THINK_CALLBACKS and
        node.blocked_callback in _RUNE_BLOCKED_CALLBACKS
    )
    if (
        node.key in (0, RUNE_NO_KEY) or
        node.kind not in RUNE_NODE_KIND_NAMES or
        node.kind == RUNE_NODE_NONE or
        node.flags & ~RUNE_NODE_FLAG_MASK or
        node.owner_key == 0 or
        node.team_master_key == 0 or
        not valid_callbacks or
        (
            RUNE_CALLBACK_UNKNOWN in callbacks and
            not node.flags & RUNE_NODEF_INVENTORY_ONLY
        ) or
        node.speed_q8 > RUNE_MAX_Q8 or
        node.accel_q8 > RUNE_MAX_Q8 or
        node.decel_q8 > RUNE_MAX_Q8 or
        any(
            minimum > maximum
            for minimum, maximum in zip(node.absmin_q8, node.absmax_q8)
        ) or
        not all(math.isfinite(component) for component in node.push_velocity) or
        (
            node.kind != RUNE_NODE_PUSH_TRIGGER and
            raw[80:92] != b"\0" * 12
        ) or
        (
            node.kind == RUNE_NODE_PUSH_TRIGGER and
            not any(component != 0.0 for component in node.push_velocity)
        )
    ):
        raise _wire_error(RLRUNE_BAD_ACTIVATION_NODE, f"node {index}")
    if node.flags & RUNE_NODEF_FRAME_COMPLETE_MOVER and (
        node.kind != RUNE_NODE_BUTTON or
        node.flags & (RUNE_NODEF_MOVER | RUNE_NODEF_SHOOTABLE) !=
        (RUNE_NODEF_MOVER | RUNE_NODEF_SHOOTABLE) or
        node.flags & (RUNE_NODEF_SYNTHETIC | RUNE_NODEF_INVENTORY_ONLY) or
        node.speed_q8 == 0 or
        node.accel_q8 != node.speed_q8 or
        node.decel_q8 != node.speed_q8 or
        node.speed_q8 % 10
    ):
        raise _wire_error(
            RLRUNE_BAD_ACTIVATION_NODE,
            f"node {index} has invalid frame-complete mover fields",
        )
    if (
        node.flags & 1 and node.owner_key == RUNE_NO_KEY
    ):
        raise _wire_error(
            RLRUNE_BAD_ACTIVATION_NODE,
            f"synthetic node {index} has no owner",
        )
    if node.kind == RUNE_NODE_DOOR_MEMBER and (
        node.team_master_key in (RUNE_NO_KEY, node.key)
    ):
        raise _wire_error(
            RLRUNE_BAD_ACTIVATION_NODE,
            f"door member node {index} has invalid team master",
        )
    if (
        node.kind == RUNE_NODE_DOOR_MASTER and
        node.team_master_key != node.key
    ):
        raise _wire_error(
            RLRUNE_BAD_ACTIVATION_NODE,
            f"door master node {index} is not self-mastered",
        )
    return node


def _decode_rune_edge(raw: bytes, index: int) -> RuneActivationEdge:
    values = RUNE_ACTIVATION_EDGE_STRUCT.unpack(raw)
    edge = RuneActivationEdge(*values)
    if (
        edge.from_key in (0, RUNE_NO_KEY) or
        edge.to_key in (0, RUNE_NO_KEY) or
        edge.kind not in RUNE_EDGE_KIND_NAMES
    ):
        raise _wire_error(RLRUNE_BAD_ACTIVATION_EDGE, f"edge {index}")
    return edge


def _decode_rune_plan(raw: bytes, index: int) -> RuneActivationPlan:
    values = RUNE_ACTIVATION_PLAN_STRUCT.unpack(raw)
    plan = RuneActivationPlan(
        values[0], values[1], values[2], values[3], values[4], values[6],
        values[7], values[8], values[9],
    )
    expected_flags = contract.mechanism_controller_plan_flags(
        plan.controller_kind
    )
    if values[5] != 0:
        raise _wire_error(contract.RLR_NONZERO_RESERVED, str(values[5]))
    if (
        plan.entry_key in (0, RUNE_NO_KEY) or
        not 0 <= plan.num_edges <= RUNE_MAX_PLAN_EDGES or
        not expected_flags or
        plan.flags != expected_flags or
        not 0 < plan.expected_members <= RUNE_MAX_TEAM_MEMBERS or
        plan.cooldown_ms > RUNE_MAX_TIME_MS or
        plan.closure_crc32 == 0
    ):
        raise _wire_error(RLRUNE_BAD_ACTIVATION_PLAN, f"plan {index}")
    if plan.controller_kind == RUNE_CONTROLLER_PUSH:
        if (
            plan.mover_key != RUNE_NO_KEY or plan.num_edges != 0 or
            plan.expected_members != 1 or plan.cooldown_ms != 0
        ):
            raise _wire_error(RLRUNE_BAD_ACTIVATION_PLAN, f"plan {index}")
    elif (
        plan.mover_key in (0, RUNE_NO_KEY) or
        (
            plan.entry_key == plan.mover_key and
            plan.controller_kind != RUNE_CONTROLLER_TRAIN_SHOOT
        ) or
        plan.num_edges == 0
    ):
        raise _wire_error(RLRUNE_BAD_ACTIVATION_PLAN, f"plan {index}")
    return plan


def _rune_project_link(link: RuneLink) -> RunePolicyLink:
    action = contract.action_mechanism_link_policy_action(link.action)
    if action is None:
        raise _wire_error(
            contract.RLW_BAD_LINK_RECORD,
            f"action {link.action} has no current link-policy projection",
        )
    return RunePolicyLink(
        source=link.source,
        destination=link.destination,
        action=action,
        provenance=link.provenance,
        min_speed=link.min_speed,
        heading=link.heading,
        heading_slack=link.heading_slack,
        exit_speed=link.exit_speed,
        cost_ms=link.cost_ms,
        suffix_anchor=link.suffix_anchor,
        mechanism_anchor=link.mechanism_anchor,
        sweep_clear_ms=link.sweep_clear_ms,
        mode=link.mode,
        reserved=link.reserved,
    )


def _rune_validate_strings(
    nodes: tuple[RuneActivationNode, ...], strings: bytes
) -> None:
    if not strings or strings[0] != 0:
        raise _wire_error(RLRUNE_BAD_STRING_POOL, "missing leading NUL")
    referenced: set[int] = set()
    for index, node in enumerate(nodes):
        for field, offset in (
            ("classname", node.classname_offset),
            ("target", node.target_offset),
            ("targetname", node.targetname_offset),
            ("killtarget", node.killtarget_offset),
            ("path_target", node.path_target_offset),
        ):
            if offset == 0:
                continue
            if (
                offset >= len(strings) or
                strings[offset] == 0 or
                strings[offset - 1] != 0 or
                strings.find(b"\0", offset) < 0
            ):
                raise _wire_error(
                    RLRUNE_BAD_STRING_POOL,
                    f"node {index} has invalid {field} offset {offset}",
                )
            referenced.add(offset)

    offsets: list[int] = []
    previous: bytes | None = None
    offset = 1
    while offset < len(strings):
        end = strings.find(b"\0", offset)
        if end < 0 or end == offset:
            raise _wire_error(RLRUNE_BAD_STRING_POOL, f"offset {offset}")
        value = strings[offset:end]
        if previous is not None and previous >= value:
            raise _wire_error(
                RLRUNE_BAD_STRING_POOL,
                f"strings are not strictly sorted at {offset}",
            )
        offsets.append(offset)
        previous = value
        offset = end + 1
    if set(offsets) != referenced:
        raise _wire_error(
            RLRUNE_BAD_STRING_POOL,
            "string pool contains unreferenced or missing entries",
        )


def _rune_string(strings: bytes, offset: int) -> bytes:
    if offset == 0:
        return b""
    return strings[offset:strings.index(0, offset)]


def _rune_node_executable(node: RuneActivationNode) -> bool:
    return bool(
        not node.flags & RUNE_NODEF_INVENTORY_ONLY and
        RUNE_CALLBACK_UNKNOWN not in (
            node.touch_callback,
            node.use_callback,
            node.think_callback,
            node.blocked_callback,
        )
    )


def _rune_edge_relation_valid(
    edge: RuneActivationEdge,
    node_by_key: Mapping[int, RuneActivationNode],
    strings: bytes,
) -> bool:
    source = node_by_key[edge.from_key]
    destination = node_by_key[edge.to_key]
    if edge.kind in (RUNE_EDGE_TARGET, RUNE_EDGE_ROUTE_TARGET):
        left = source.target_offset
        right = destination.targetname_offset
    elif edge.kind == RUNE_EDGE_KILLTARGET:
        left = source.killtarget_offset
        right = destination.targetname_offset
    elif edge.kind == RUNE_EDGE_PATH_TARGET:
        left = source.path_target_offset
        right = destination.targetname_offset
    elif edge.kind == RUNE_EDGE_OWNER:
        return source.owner_key == destination.key
    elif edge.kind == RUNE_EDGE_TEAM:
        return bool(
            destination.team_master_key == source.key and
            source.flags & RUNE_NODEF_TEAM_MASTER and
            destination.flags & RUNE_NODEF_TEAM_MEMBER
        )
    else:
        # MOVE_TARGET, TARGET_ENT, and ENEMY are authenticated inventory
        # context, but the fixed record carries no independent string/key field with
        # which an executable plan could re-prove their live relationship.
        return False
    return bool(
        left and right and
        _rune_string(strings, left).lower() == _rune_string(strings, right).lower()
    )


def _rune_door_node_valid(
    node: RuneActivationNode,
    master_key: int,
    *,
    master: bool,
    strings: bytes,
) -> bool:
    required_flag = (
        RUNE_NODEF_TEAM_MASTER if master else RUNE_NODEF_TEAM_MEMBER
    )
    forbidden_flag = (
        RUNE_NODEF_TEAM_MEMBER if master else RUNE_NODEF_TEAM_MASTER
    )
    # Health and targetname independently select Think_CalcMoveSpeed.
    # Shootable brushes therefore retain it even without a targetname.
    expected_think = (
        RUNE_CALLBACK_THINK_CALC_MOVE_SPEED
        if node.flags & RUNE_NODEF_SHOOTABLE or node.targetname_offset else
        RUNE_CALLBACK_THINK_SPAWN_DOOR_TRIGGER
    )
    return bool(
        _rune_node_executable(node) and
        node.kind == (
            RUNE_NODE_DOOR_MASTER if master else
            RUNE_NODE_DOOR_MEMBER
        ) and
        node.flags & (
            RUNE_NODEF_USABLE | RUNE_NODEF_MOVER | required_flag
        ) == (RUNE_NODEF_USABLE | RUNE_NODEF_MOVER | required_flag) and
        not node.flags & forbidden_flag and
        _rune_string(strings, node.classname_offset) in (
            b"func_door", b"func_door_rotating"
        ) and
        node.owner_key == RUNE_NO_KEY and
        node.team_master_key == master_key and
        # START_OPEN mutates the live endpoints and TOGGLE has no bounded
        # close lease.  REVERSE and CRUSHER are explicitly admissible.
        not node.spawnflags & (1 | 32) and
        node.touch_callback == 0 and
        node.use_callback == RUNE_CALLBACK_USE_DOOR and
        node.think_callback == expected_think and
        node.blocked_callback == RUNE_CALLBACK_BLOCKED_DOOR and
        node.delay_ms >= 0 and
        (node.delay_ms == 0 or node.target_offset == 0) and
        node.wait_ms > 0 and
        node.speed_q8 != 0 and
        node.accel_q8 == node.speed_q8 and
        node.decel_q8 == node.speed_q8 and
        node.killtarget_offset == 0 and
        node.path_target_offset == 0
    )


def _rune_button_node_valid(
    node: RuneActivationNode,
    strings: bytes,
) -> bool:
    return bool(
        _rune_node_executable(node) and
        node.kind == RUNE_NODE_BUTTON and
        node.flags == (
            RUNE_NODEF_REPEATABLE |
            RUNE_NODEF_TOUCHABLE |
            RUNE_NODEF_USABLE |
            RUNE_NODEF_MOVER
        ) and
        _rune_string(strings, node.classname_offset) == b"func_button" and
        node.target_offset != 0 and
        node.targetname_offset == node.killtarget_offset ==
        node.path_target_offset == 0 and
        node.owner_key == node.team_master_key == RUNE_NO_KEY and
        node.spawnflags == 0 and
        node.touch_callback == RUNE_CALLBACK_BUTTON_TOUCH and
        node.use_callback == RUNE_CALLBACK_BUTTON_USE and
        node.think_callback == node.blocked_callback == 0 and
        node.delay_ms == 0 and node.wait_ms > 0 and
        node.speed_q8 != 0 and
        node.accel_q8 == node.speed_q8 and
        node.decel_q8 == node.speed_q8
    )


def _rune_push_node_valid(
    node: RuneActivationNode,
    strings: bytes,
) -> bool:
    return bool(
        _rune_node_executable(node) and
        node.kind == RUNE_NODE_PUSH_TRIGGER and
        node.flags == (RUNE_NODEF_REPEATABLE | RUNE_NODEF_TOUCHABLE) and
        _rune_string(strings, node.classname_offset) == b"trigger_push" and
        node.target_offset == node.targetname_offset ==
        node.killtarget_offset == node.path_target_offset == 0 and
        node.owner_key == node.team_master_key == RUNE_NO_KEY and
        node.spawnflags == 0 and
        node.touch_callback == RUNE_CALLBACK_TRIGGER_PUSH_TOUCH and
        node.use_callback == node.think_callback == node.blocked_callback == 0 and
        node.delay_ms == node.wait_ms == 0 and
        node.speed_q8 == 680 and node.accel_q8 == node.decel_q8 == 0 and
        all(math.isfinite(component) for component in node.push_velocity) and
        any(component != 0.0 for component in node.push_velocity)
    )


def _rune_push_closure_crc(node: RuneActivationNode) -> int:
    return _crc32(struct.pack(
        "<4sI3f", b"PUSH", node.key, *node.push_velocity
    ))


def _rune_frame_complete_button_valid(
    node: RuneActivationNode,
    strings: bytes,
) -> bool:
    expected_flags = (
        RUNE_NODEF_REPEATABLE |
        RUNE_NODEF_USABLE |
        RUNE_NODEF_MOVER |
        RUNE_NODEF_SHOOTABLE |
        RUNE_NODEF_FRAME_COMPLETE_MOVER
    )
    return bool(
        node.kind == RUNE_NODE_BUTTON and
        node.flags == expected_flags and
        _rune_string(strings, node.classname_offset) == b"func_button" and
        node.target_offset != 0 and
        node.targetname_offset == node.killtarget_offset ==
        node.path_target_offset == 0 and
        node.owner_key == node.team_master_key == RUNE_NO_KEY and
        node.spawnflags == 0 and
        node.touch_callback == 0 and
        node.use_callback == RUNE_CALLBACK_BUTTON_USE and
        node.think_callback == node.blocked_callback == 0 and
        node.delay_ms == 0 and node.wait_ms > 0 and
        node.speed_q8 != 0 and
        node.accel_q8 == node.speed_q8 and
        node.decel_q8 == node.speed_q8 and
        node.speed_q8 % 10 == 0
    )


def _rune_safe_speaker(node: RuneActivationNode) -> bool:
    return bool(
        _rune_node_executable(node) and
        node.kind == RUNE_NODE_TARGET_SPEAKER and
        node.use_callback == RUNE_CALLBACK_USE_TARGET_SPEAKER and
        node.touch_callback == node.think_callback == node.blocked_callback == 0 and
        # Looped-on/looped-off speakers toggle persistent state when used.
        not node.spawnflags & 3 and
        node.target_offset == node.killtarget_offset ==
        node.path_target_offset == 0
    )


def _rune_safe_areaportal(node: RuneActivationNode) -> bool:
    return bool(
        _rune_node_executable(node) and
        node.kind == RUNE_NODE_AREAPORTAL and
        node.use_callback == RUNE_CALLBACK_USE_AREAPORTAL and
        node.touch_callback == node.think_callback == node.blocked_callback == 0 and
        node.target_offset == node.killtarget_offset ==
        node.path_target_offset == 0
    )


def _rune_relay_shape(node: RuneActivationNode) -> bool:
    return bool(
        _rune_node_executable(node) and
        node.kind == RUNE_NODE_RELAY and
        node.use_callback == RUNE_CALLBACK_USE_TRIGGER_RELAY and
        node.touch_callback == node.think_callback == node.blocked_callback == 0 and
        node.delay_ms >= 0 and
        node.killtarget_offset == node.path_target_offset == 0 and
        node.target_offset != 0
    )


def _rune_safe_relay(node: RuneActivationNode) -> bool:
    return _rune_relay_shape(node) and node.delay_ms == 0


def _rune_train_button_shape(
    button: RuneActivationNode, *, shoot: bool
) -> bool:
    expected_flags = (
        RUNE_NODEF_REPEATABLE |
        RUNE_NODEF_USABLE |
        RUNE_NODEF_MOVER |
        (RUNE_NODEF_SHOOTABLE if shoot else RUNE_NODEF_TOUCHABLE)
    )
    return bool(
        _rune_node_executable(button) and
        button.kind == RUNE_NODE_BUTTON and
        button.flags == expected_flags and
        button.touch_callback == (
            0 if shoot else RUNE_CALLBACK_BUTTON_TOUCH
        ) and
        button.use_callback == RUNE_CALLBACK_BUTTON_USE and
        button.think_callback == button.blocked_callback == 0 and
        button.spawnflags == 0 and button.delay_ms == 0 and
        button.wait_ms > 0 and button.target_offset != 0 and
        button.killtarget_offset == button.path_target_offset == 0
    )


def _rune_train_mover_shape(train: RuneActivationNode) -> bool:
    expected_flags = (
        RUNE_NODEF_REPEATABLE | RUNE_NODEF_USABLE | RUNE_NODEF_MOVER
    )
    return bool(
        _rune_node_executable(train) and
        train.kind == RUNE_NODE_TRAIN and train.flags == expected_flags and
        train.spawnflags == 2 and train.touch_callback == 0 and
        train.use_callback == RUNE_CALLBACK_TRAIN_USE and
        train.think_callback in (0, RUNE_CALLBACK_FUNC_TRAIN_FIND) and
        train.blocked_callback == RUNE_CALLBACK_BLOCKED_TRAIN and
        train.delay_ms == 0 and train.speed_q8 != 0 and
        train.speed_q8 == train.accel_q8 == train.decel_q8 and
        train.target_offset != 0 and train.targetname_offset != 0 and
        train.killtarget_offset == train.path_target_offset == 0
    )


def _rune_train_corner_shape(corner: RuneActivationNode) -> bool:
    return bool(
        _rune_node_executable(corner) and
        corner.kind == RUNE_NODE_PATH_CORNER and
        corner.flags == (RUNE_NODEF_TOUCHABLE | RUNE_NODEF_ONE_SHOT) and
        corner.spawnflags == 0 and
        corner.touch_callback == RUNE_CALLBACK_PATH_CORNER_TOUCH and
        corner.use_callback == corner.think_callback ==
        corner.blocked_callback == 0 and
        corner.delay_ms == 0 and corner.wait_ms == -1000 and
        corner.target_offset != 0 and corner.killtarget_offset == 0
    )


def _rune_validate_production_plan(
    plan: RuneActivationPlan,
    plan_edges: tuple[RuneActivationEdge, ...],
    owner_link: RuneLink,
    node_by_key: Mapping[int, RuneActivationNode],
    inventory_fanout: Mapping[
        tuple[int, int], tuple[RuneActivationEdge, ...]
    ],
    door_members_by_master: Mapping[
        int, tuple[RuneActivationNode, ...]
    ],
    strings: bytes,
    plan_index: int,
) -> None:
    entry = node_by_key.get(plan.entry_key)
    mover = node_by_key.get(plan.mover_key)

    def fail(detail: str, *, edge: bool = False) -> None:
        raise _wire_error(
            RLRUNE_BAD_ACTIVATION_EDGE if edge else RLRUNE_BAD_ACTIVATION_PLAN,
            f"plan {plan_index} {detail}",
        )

    if entry is None or not _rune_node_executable(entry):
        fail("has a non-executable entry")
    if plan.controller_kind == RUNE_CONTROLLER_PUSH:
        if (
            mover is not None or plan_edges or
            not _rune_push_node_valid(entry, strings)
        ):
            fail("does not satisfy the push controller law")
        return
    if mover is None or not _rune_node_executable(mover):
        fail("has a non-executable entry or mover")
    if len(set(plan_edges)) != len(plan_edges):
        fail("contains a duplicate executable edge")
    for edge in plan_edges:
        source = node_by_key.get(edge.from_key)
        destination = node_by_key.get(edge.to_key)
        if (
            source is None or destination is None or
            edge.from_key == edge.to_key or
            not _rune_node_executable(source) or
            not _rune_node_executable(destination) or
            not _rune_edge_relation_valid(edge, node_by_key, strings)
        ):
            fail("contains an unproved executable relation", edge=True)

    expected: set[RuneActivationEdge] = set()
    relay_invocations: set[int] = set()
    pending_relays: list[RuneActivationNode] = []

    def delayed_sound_only_relay(
        node: RuneActivationNode, depth: int
    ) -> bool:
        if depth > 4 or not _rune_relay_shape(node):
            return False
        fanout = inventory_fanout.get((node.key, RUNE_EDGE_TARGET), ())
        if not fanout:
            return False
        for child in fanout:
            if child.delay_ms != node.delay_ms:
                return False
            destination = node_by_key[child.to_key]
            if _rune_safe_speaker(destination):
                continue
            if not delayed_sound_only_relay(destination, depth + 1):
                return False
        return True

    def add_edge(edge: RuneActivationEdge) -> None:
        if edge.delay_ms != 0:
            fail("contains a delayed controller edge")
        if edge in expected:
            fail("would execute the same inventory edge more than once")
        expected.add(edge)

    def add_platform_trigger_edge(
        edge: RuneActivationEdge, delay_ms: int
    ) -> None:
        if edge.kind != RUNE_EDGE_TARGET or edge.delay_ms != delay_ms:
            fail("contains a carrier trigger edge with the wrong delay")
        if edge in expected:
            fail("would execute the same inventory edge more than once")
        expected.add(edge)

    def add_side_effect(
        edge: RuneActivationEdge, *, allow_areaportal: bool
    ) -> None:
        if edge.kind != RUNE_EDGE_TARGET or edge.delay_ms != 0:
            fail("contains a delayed or non-target side effect")
        add_edge(edge)
        destination = node_by_key[edge.to_key]
        if _rune_safe_speaker(destination):
            return
        if allow_areaportal and _rune_safe_areaportal(destination):
            return
        # Preserve the inbound engine ordinal, but stop before the bound
        # positive-delay relay can schedule DelayedUse.  Its omitted
        # inventory suffix must be completely sound-only.
        if (
            destination.delay_ms > 0 and
            delayed_sound_only_relay(destination, 1)
        ):
            return
        if not _rune_safe_relay(destination):
            fail("contains a mutable speaker, relay, or side-effect endpoint")
        if destination.key in relay_invocations:
            fail("contains a cyclic or multiply invoked relay closure")
        relay_invocations.add(destination.key)
        pending_relays.append(destination)

    def add_door_closure(
        masters: list[int], expected_physical: int
    ) -> None:
        if not masters or len(set(masters)) != len(masters):
            fail("admits no door master or admits one more than once")
        physical: list[RuneActivationNode] = []
        for master_key in masters:
            master_node = node_by_key.get(master_key)
            if master_node is None or not _rune_door_node_valid(
                master_node, master_key, master=True, strings=strings
            ):
                fail("contains a noncanonical door master")
            physical.append(master_node)
            members = door_members_by_master.get(master_key, ())
            for member in members:
                if not _rune_door_node_valid(
                    member, master_key, master=False, strings=strings
                ):
                    fail("contains a noncanonical door member")
            team_edges = inventory_fanout.get(
                (master_key, RUNE_EDGE_TEAM), ()
            )
            if {edge.to_key for edge in team_edges} != {
                member.key for member in members
            }:
                fail("does not authenticate the complete physical door team")
            for edge in team_edges:
                add_edge(edge)
            physical.extend(members)
        if len(physical) != expected_physical:
            fail("expected_members excludes or adds physical door members")
        for door in physical:
            for edge in inventory_fanout.get(
                (door.key, RUNE_EDGE_TARGET), ()
            ):
                add_side_effect(edge, allow_areaportal=True)
        relay_index = 0
        while relay_index < len(pending_relays):
            relay = pending_relays[relay_index]
            relay_index += 1
            fanout = inventory_fanout.get(
                (relay.key, RUNE_EDGE_TARGET), ()
            )
            if not fanout:
                fail("contains an empty sound relay")
            for edge in fanout:
                destination = node_by_key[edge.to_key]
                if destination.kind == RUNE_NODE_DOOR_MASTER:
                    if destination.key not in masters:
                        fail("relay targets an unauthenticated door master")
                    add_edge(edge)
                elif destination.kind == RUNE_NODE_DOOR_MEMBER:
                    if destination.team_master_key not in masters:
                        fail("relay targets an unauthenticated door member")
                    add_edge(edge)
                else:
                    add_side_effect(edge, allow_areaportal=False)

    controller = plan.controller_kind
    if controller == RUNE_CONTROLLER_PLATFORM:
        owner = inventory_fanout.get((entry.key, RUNE_EDGE_OWNER), ())
        targets = inventory_fanout.get((entry.key, RUNE_EDGE_TARGET), ())
        stock = (
            entry.touch_callback == RUNE_CALLBACK_TOUCH_PLAT_CENTER and
            bool(entry.flags & RUNE_NODEF_SYNTHETIC) and
            not targets and plan.cooldown_ms == 0
        )
        carrier = (
            entry.touch_callback == RUNE_CALLBACK_TOUCH_MULTI and
            entry.use_callback == RUNE_CALLBACK_USE_MULTI and
            entry.flags & (
                RUNE_NODEF_SYNTHETIC | RUNE_NODEF_REPEATABLE |
                RUNE_NODEF_TOUCHABLE | RUNE_NODEF_USABLE
            ) == (
                RUNE_NODEF_REPEATABLE | RUNE_NODEF_TOUCHABLE |
                RUNE_NODEF_USABLE
            ) and
            entry.delay_ms == 0 and entry.wait_ms > 0 and
            entry.killtarget_offset == 0 and entry.path_target_offset == 0 and
            mover.use_callback == RUNE_CALLBACK_USE_DOOR and
            mover.blocked_callback == RUNE_CALLBACK_BLOCKED_DOOR and
            _carrier_door_spawnflags(mover.spawnflags) and
            mover.flags & (
                RUNE_NODEF_MOVER | RUNE_NODEF_TEAM_MASTER |
                RUNE_NODEF_SHOOTABLE
            ) == (RUNE_NODEF_MOVER | RUNE_NODEF_TEAM_MASTER) and
            len(targets) == 1 and targets[0].to_key == mover.key and
            plan.cooldown_ms == min(entry.wait_ms, RUNE_MAX_TIME_MS)
        )
        if (
            entry.kind != RUNE_NODE_PLATFORM_TRIGGER or
            mover.kind != RUNE_NODE_PLATFORM or
            len(owner) != 1 or owner[0].to_key != mover.key or
            not (stock or carrier)
        ):
            fail("does not satisfy the platform controller law")
        if carrier:
            add_edge(targets[0])
        add_edge(owner[0])
        if stock:
            if owner_link.mode not in (
                contract.RLCM_NONE, contract.RLCM_RIDE
            ) or (
                owner_link.mode == contract.RLCM_RIDE and
                plan.expected_members <= 1
            ):
                fail("has an unsupported stock platform mode")
            if plan.expected_members > 1:
                plan_edge_set = set(plan_edges)
                auto_stages = []
                for node in node_by_key.values():
                    fanout = inventory_fanout.get(
                        (node.key, RUNE_EDGE_OWNER), ()
                    )
                    if (
                        node.kind == RUNE_NODE_AUTO_DOOR_TRIGGER and
                        node.touch_callback ==
                            RUNE_CALLBACK_TOUCH_DOOR_TRIGGER and
                        node.flags & RUNE_NODEF_SYNTHETIC and
                        len(fanout) == 1 and fanout[0] in plan_edge_set
                    ):
                        auto_stages.append(fanout[0])
                if len(auto_stages) != 1:
                    fail("does not have one automatic-door platform stage")
                add_edge(auto_stages[0])
                add_door_closure(
                    [auto_stages[0].to_key], plan.expected_members - 1
                )
        if carrier and plan.expected_members > 1:
            def carrier_trigger_shape(node: RuneActivationNode) -> bool:
                return bool(
                    _rune_node_executable(node) and
                    node.kind == RUNE_NODE_TRIGGER and
                    node.touch_callback == RUNE_CALLBACK_TOUCH_MULTI and
                    node.use_callback == RUNE_CALLBACK_USE_MULTI and
                    node.flags & (
                        RUNE_NODEF_REPEATABLE | RUNE_NODEF_TOUCHABLE |
                        RUNE_NODEF_USABLE
                    ) == (
                        RUNE_NODEF_REPEATABLE | RUNE_NODEF_TOUCHABLE |
                        RUNE_NODEF_USABLE
                    ) and
                    node.delay_ms >= 0 and node.wait_ms > 0 and
                    node.target_offset != 0 and
                    node.killtarget_offset == node.path_target_offset == 0
                )

            plan_edge_set = set(plan_edges)
            planned_triggers = [
                node for node in node_by_key.values()
                if carrier_trigger_shape(node) and any(
                    edge in plan_edge_set for edge in inventory_fanout.get(
                        (node.key, RUNE_EDGE_TARGET), ()
                    )
                )
            ]

            def trigger_signature(
                node: RuneActivationNode,
            ) -> tuple[int, int, tuple[tuple[int, int, int], ...]]:
                return (
                    node.delay_ms,
                    node.target_offset,
                    tuple(
                        (edge.to_key, edge.ordinal, edge.delay_ms)
                        for edge in inventory_fanout.get(
                            (node.key, RUNE_EDGE_TARGET), ()
                        )
                    ),
                )

            def trigger_contains_anchor(node: RuneActivationNode) -> bool:
                hull_min_q8 = (-136, -136, -200)
                hull_max_q8 = (136, 136, 264)
                for axis, coordinate in enumerate(owner_link.suffix_anchor):
                    scaled = coordinate * 8.0
                    if not math.isfinite(scaled) or scaled != int(scaled):
                        return False
                    anchor_q8 = int(scaled)
                    if (
                        anchor_q8 + hull_max_q8[axis] <= node.absmin_q8[axis] or
                        anchor_q8 + hull_min_q8[axis] >= node.absmax_q8[axis]
                    ):
                        return False
                return True

            if owner_link.action != contract.RL_LIFT:
                fail("is not owned by a lift link")
            classes: dict[
                tuple[int, int, tuple[tuple[int, int, int], ...]],
                list[RuneActivationNode],
            ] = {}
            for node in planned_triggers:
                classes.setdefault(trigger_signature(node), []).append(node)
            if not classes or len(classes) > 2:
                fail("has an invalid number of carrier trigger classes")
            containing = [
                signature for signature, members in classes.items()
                if any(trigger_contains_anchor(node) for node in members)
            ]
            if len(containing) != 1:
                fail("does not have one anchor-selected carrier approach")
            approach_signature = containing[0]
            ordered_signatures = [approach_signature] + [
                signature for signature in classes
                if signature != approach_signature
            ]
            masters: list[int] = []
            for class_signature in ordered_signatures:
                reference = classes[class_signature][0]
                signature = class_signature[2]
                stage_masters: list[int] = []
                for node in node_by_key.values():
                    if not carrier_trigger_shape(node) or (
                        node.delay_ms != class_signature[0] or
                        node.target_offset != class_signature[1]
                    ):
                        continue
                    fanout = inventory_fanout.get(
                        (node.key, RUNE_EDGE_TARGET), ()
                    )
                    if tuple(
                        (edge.to_key, edge.ordinal, edge.delay_ms)
                        for edge in fanout
                    ) != signature:
                        fail("has inconsistent equivalent carrier-trigger fanout")
                    seen_masters = set(stage_masters)
                    for edge in fanout:
                        destination = node_by_key[edge.to_key]
                        if destination.kind == RUNE_NODE_DOOR_MASTER:
                            if destination.key in masters:
                                fail("reuses one door mover across carrier stages")
                            if destination.key not in seen_masters:
                                stage_masters.append(destination.key)
                                seen_masters.add(destination.key)
                        elif (
                            destination.kind != RUNE_NODE_DOOR_MEMBER or
                            destination.team_master_key not in seen_masters
                        ):
                            fail("carrier trigger targets a slave before its master")
                        add_platform_trigger_edge(edge, class_signature[0])
                if not stage_masters:
                    fail("carrier stage has no door master")
                masters.extend(stage_masters)
            add_door_closure(masters, plan.expected_members - 1)
        elif carrier and plan.expected_members != 1:
            fail("has an invalid carrier member count")

    elif controller == RUNE_CONTROLLER_TELEPORT:
        owner = inventory_fanout.get((entry.key, RUNE_EDGE_OWNER), ())
        targets = inventory_fanout.get((entry.key, RUNE_EDGE_TARGET), ())
        if (
            entry.kind != RUNE_NODE_TELEPORT_TRIGGER or
            entry.touch_callback != RUNE_CALLBACK_TELEPORTER_TOUCH or
            not entry.flags & RUNE_NODEF_SYNTHETIC or
            mover.kind != RUNE_NODE_TELEPORTER or
            len(owner) != 1 or owner[0].to_key != mover.key or
            len(targets) != 1 or
            node_by_key[targets[0].to_key].kind != RUNE_NODE_TELEPORT_DEST or
            plan.expected_members != 1 or plan.cooldown_ms != 0
        ):
            fail("does not satisfy the teleport controller law")
        add_edge(owner[0])
        add_edge(targets[0])

    elif (
        controller == RUNE_CONTROLLER_TRAIN_SHOOT and
        entry.kind == RUNE_NODE_DOOR_MASTER
    ):
        if (
            owner_link.action != contract.RL_TRAIN or
            owner_link.mode != contract.RLCM_PREOPEN or
            entry.key != mover.key or
            not entry.flags & RUNE_NODEF_SHOOTABLE or
            plan.expected_members <= 0 or
            not 0 < plan.cooldown_ms <= RUNE_MAX_TIME_MS
        ):
            fail("does not satisfy the shootable-door controller law")
        add_door_closure([entry.key], plan.expected_members)

    elif controller in (RUNE_CONTROLLER_TRAIN, RUNE_CONTROLLER_TRAIN_SHOOT):
        button_targets = inventory_fanout.get(
            (entry.key, RUNE_EDGE_TARGET), ()
        )
        train_routes = inventory_fanout.get(
            (mover.key, RUNE_EDGE_ROUTE_TARGET), ()
        )
        open_node = (
            node_by_key.get(train_routes[0].to_key)
            if len(train_routes) == 1 else None
        )
        open_routes = (
            inventory_fanout.get(
                (open_node.key, RUNE_EDGE_ROUTE_TARGET), ()
            )
            if open_node is not None else ()
        )
        closed_node = (
            node_by_key.get(open_routes[0].to_key)
            if len(open_routes) == 1 else None
        )
        closed_routes = (
            inventory_fanout.get(
                (closed_node.key, RUNE_EDGE_ROUTE_TARGET), ()
            )
            if closed_node is not None else ()
        )

        def train_no_side_effects(node: RuneActivationNode) -> bool:
            return not (
                inventory_fanout.get((node.key, RUNE_EDGE_KILLTARGET), ()) or
                inventory_fanout.get((node.key, RUNE_EDGE_PATH_TARGET), ())
            )

        if (
            owner_link.action != contract.RL_TRAIN or
            plan.expected_members != 1 or
            not 0 < plan.cooldown_ms <= RUNE_MAX_TIME_MS or
            not _rune_train_button_shape(
                entry,
                shoot=controller == RUNE_CONTROLLER_TRAIN_SHOOT,
            ) or
            not _rune_train_mover_shape(mover) or
            len(button_targets) != 1 or
            button_targets[0].to_key != mover.key or
            len(train_routes) != 1 or
            open_node is None or closed_node is None or
            open_node.key == closed_node.key or
            len(open_routes) != 1 or len(closed_routes) != 1 or
            closed_routes[0].to_key != open_node.key or
            not _rune_train_corner_shape(open_node) or
            not _rune_train_corner_shape(closed_node) or
            not all(train_no_side_effects(node) for node in (
                entry, mover, open_node, closed_node
            ))
        ):
            fail("does not satisfy the train controller law")
        add_edge(button_targets[0])
        add_edge(train_routes[0])
        add_edge(closed_routes[0])
        add_edge(open_routes[0])

    elif controller in (
        RUNE_CONTROLLER_AUTO_DOOR,
        RUNE_CONTROLLER_DIRECT_TRIGGER_DOOR,
        RUNE_CONTROLLER_BUTTON_DOOR,
    ):
        masters: list[int] = []
        if controller == RUNE_CONTROLLER_AUTO_DOOR:
            owner = inventory_fanout.get((entry.key, RUNE_EDGE_OWNER), ())
            if (
                entry.kind != RUNE_NODE_AUTO_DOOR_TRIGGER or
                entry.touch_callback != RUNE_CALLBACK_TOUCH_DOOR_TRIGGER or
                not entry.flags & RUNE_NODEF_SYNTHETIC or
                len(owner) != 1 or owner[0].to_key != mover.key or
                plan.cooldown_ms != 1000
            ):
                fail("does not satisfy the automatic-door entry law")
            add_edge(owner[0])
            masters.append(mover.key)
        elif controller == RUNE_CONTROLLER_BUTTON_DOOR:
            targets = inventory_fanout.get((entry.key, RUNE_EDGE_TARGET), ())
            matching_targets = tuple(
                node for node in node_by_key.values()
                if node.targetname_offset and
                _rune_string(strings, node.targetname_offset).lower() ==
                _rune_string(strings, entry.target_offset).lower()
            ) if entry.target_offset else ()
            if (
                not _rune_button_node_valid(entry, strings) or
                not targets or
                len(matching_targets) != len(targets) or
                plan.cooldown_ms != entry.wait_ms
            ):
                fail("does not satisfy the button-door entry law")
            # Stock G_UseTargets invokes the canonical team master first;
            # later same-team slave targets are authenticated door_use
            # no-ops.  Preserve that exhaustive engine-order fanout rather
            # than narrowing a button controller to a single unteamed brush.
            if tuple(
                edge for edge in plan_edges
                if edge.from_key == entry.key and
                edge.kind == RUNE_EDGE_TARGET
            ) != targets:
                fail("does not preserve ordered button target fanout")
            target_keys: set[int] = set()
            for ordinal, edge in enumerate(targets):
                destination = node_by_key[edge.to_key]
                if destination.key in target_keys:
                    fail("targets a button-door destination more than once")
                target_keys.add(destination.key)
                if ordinal == 0:
                    if (
                        destination.kind != RUNE_NODE_DOOR_MASTER or
                        destination.key != mover.key
                    ):
                        fail("does not target its door master first")
                elif (
                    destination.kind != RUNE_NODE_DOOR_MEMBER or
                    destination.team_master_key != mover.key
                ):
                    fail("targets a foreign or non-door button destination")
                add_edge(edge)
            masters.append(mover.key)
        else:
            targets = inventory_fanout.get((entry.key, RUNE_EDGE_TARGET), ())
            cooldown = min(entry.wait_ms, RUNE_MAX_TIME_MS)
            if (
                entry.kind != RUNE_NODE_TRIGGER or
                entry.touch_callback != RUNE_CALLBACK_TOUCH_MULTI or
                not entry.flags & RUNE_NODEF_REPEATABLE or
                entry.delay_ms != 0 or entry.wait_ms <= 0 or
                plan.cooldown_ms != cooldown or not targets or
                entry.killtarget_offset != 0 or
                entry.path_target_offset != 0
            ):
                fail("does not satisfy the direct-trigger entry law")
            # Filtering preserves the authenticated engine target-fanout
            # ordinal even when team and side-effect edges are interleaved.
            if tuple(
                edge for edge in plan_edges
                if edge.from_key == entry.key and
                edge.kind == RUNE_EDGE_TARGET
            ) != targets:
                fail("does not preserve ordered entry target fanout")
            seen_masters: set[int] = set()
            for edge in targets:
                destination = node_by_key[edge.to_key]
                if destination.kind == RUNE_NODE_DOOR_MASTER:
                    masters.append(destination.key)
                    seen_masters.add(destination.key)
                    add_edge(edge)
                elif destination.kind == RUNE_NODE_DOOR_MEMBER:
                    if destination.team_master_key not in seen_masters:
                        fail("targets a door slave before its master")
                    add_edge(edge)  # authenticated door_use no-op
                else:
                    add_side_effect(edge, allow_areaportal=False)
            for relay in tuple(pending_relays):
                for edge in inventory_fanout.get(
                    (relay.key, RUNE_EDGE_TARGET), ()
                ):
                    destination = node_by_key[edge.to_key]
                    if destination.kind == RUNE_NODE_DOOR_MASTER:
                        if destination.key in seen_masters:
                            fail("relay admits one door master more than once")
                        masters.append(destination.key)
                        seen_masters.add(destination.key)
                    elif (
                        destination.kind == RUNE_NODE_DOOR_MEMBER and
                        destination.team_master_key not in seen_masters
                    ):
                        fail("relay targets a door slave before its master")
            if not masters or mover.key != min(masters):
                fail("does not bind mover_key to the smallest admitted master")

        add_door_closure(masters, plan.expected_members)
    else:
        fail("uses an unsupported controller")

    if set(plan_edges) != expected or len(plan_edges) != len(expected):
        fail("edge closure is incomplete or contains controller-foreign edges")


def _rune_validate_mechanisms(
    header: RuneHeader,
    links: tuple[RuneLink, ...],
    nodes: tuple[RuneActivationNode, ...],
    edges: tuple[RuneActivationEdge, ...],
    raw_edges: tuple[bytes, ...],
    plans: tuple[RuneActivationPlan, ...],
    strings: bytes,
) -> None:
    node_keys = tuple(node.key for node in nodes)
    if any(left >= right for left, right in zip(node_keys, node_keys[1:])):
        raise _wire_error(
            RLRUNE_DUPLICATE_NODE_KEY,
            "node keys must be strictly ascending",
        )
    key_set = set(node_keys)
    node_by_key = {node.key: node for node in nodes}
    for index, node in enumerate(nodes):
        if (
            node.owner_key != RUNE_NO_KEY and
            node.owner_key not in key_set
        ) or (
            node.team_master_key != RUNE_NO_KEY and
            node.team_master_key not in key_set
        ):
            raise _wire_error(
                RLRUNE_BAD_MECHANISM_GRAPH,
                f"node {index} references an absent key",
            )
        if node.flags & 64:
            master = node_by_key.get(node.team_master_key)
            if master is None or not master.flags & 32:
                raise _wire_error(
                    RLRUNE_BAD_MECHANISM_GRAPH,
                    f"team member node {index} has no team master",
                )
    for index, edge in enumerate(edges):
        if edge.from_key not in key_set or edge.to_key not in key_set:
            raise _wire_error(
                RLRUNE_BAD_MECHANISM_GRAPH,
                f"edge {index} references an absent key",
            )

    inventory = edges[:header.num_inventory_edges]
    for index, edge in enumerate(inventory):
        previous = inventory[index - 1] if index else None
        same_fanout = bool(
            previous is not None and
            previous.from_key == edge.from_key and
            previous.kind == edge.kind
        )
        expected_ordinal = previous.ordinal + 1 if same_fanout else 0
        if edge.ordinal != expected_ordinal:
            raise _wire_error(
                RLRUNE_BAD_ACTIVATION_EDGE,
                f"inventory edge {index} has ordinal {edge.ordinal}, "
                f"expected {expected_ordinal}",
            )
        if previous is not None and (
            previous.from_key > edge.from_key or
            (
                previous.from_key == edge.from_key and
                previous.kind > edge.kind
            )
        ):
            raise _wire_error(
                RLRUNE_BAD_ACTIVATION_EDGE,
                f"inventory edge {index} is out of canonical order",
            )

    fanout_lists: dict[
        tuple[int, int], list[RuneActivationEdge]
    ] = {}
    for edge in inventory:
        fanout_lists.setdefault((edge.from_key, edge.kind), []).append(edge)
    inventory_fanout = {
        key: tuple(fanout) for key, fanout in fanout_lists.items()
    }
    member_lists: dict[int, list[RuneActivationNode]] = {}
    for node in nodes:
        if node.kind == RUNE_NODE_DOOR_MEMBER:
            member_lists.setdefault(node.team_master_key, []).append(node)
    door_members_by_master = {
        key: tuple(members) for key, members in member_lists.items()
    }

    plan_owners: list[RuneLink | None] = [None] * len(plans)
    for index, link in enumerate(links):
        if not contract.action_mechanism_admitted(link.action):
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} uses disabled action {link.action}",
            )
        has_plan = link.activation_plan != RUNE_NO_ACTIVATION_PLAN
        if has_plan != contract.action_mechanism_plan_required(link.action):
            state = "missing" if not has_plan else "unexpected"
            raise _wire_error(
                RLRUNE_BAD_ACTIVATION_PLAN,
                f"link {index} has {state} activation plan",
            )
        if not has_plan:
            continue
        if (
            link.activation_plan >= len(plans) or
            not contract.action_mechanism_plan_allowed(
                link.action,
                plans[link.activation_plan].controller_kind,
            )
        ):
            raise _wire_error(
                RLRUNE_BAD_ACTIVATION_PLAN,
                f"link {index} has invalid plan binding",
            )
        if plan_owners[link.activation_plan] is not None:
            raise _wire_error(
                RLRUNE_BAD_ACTIVATION_PLAN,
                "one plan is referenced by more than one link",
            )
        plan_owners[link.activation_plan] = link
    if any(owner is None for owner in plan_owners):
        raise _wire_error(
            RLRUNE_BAD_ACTIVATION_PLAN,
            "each plan must be referenced by exactly one link",
        )

    expected_first_edge = header.num_inventory_edges
    if plans:
        if plans[0].first_edge != header.num_inventory_edges:
            raise _wire_error(
                RLRUNE_BAD_MECHANISM_CONTRACT,
                "inventory-edge count disagrees with first plan",
            )
    elif header.num_inventory_edges != len(edges):
        raise _wire_error(
            RLRUNE_BAD_MECHANISM_CONTRACT,
            "planless artifact does not mark every edge as inventory",
        )
    inventory_raw = frozenset(raw_edges[:header.num_inventory_edges])
    for index, plan in enumerate(plans):
        owner_link = plan_owners[index]
        assert owner_link is not None
        push_plan = plan.controller_kind == RUNE_CONTROLLER_PUSH
        if (
            plan.entry_key not in key_set or
            (not push_plan and plan.mover_key not in key_set) or
            (push_plan and plan.mover_key != RUNE_NO_KEY)
        ):
            raise _wire_error(
                RLRUNE_BAD_ACTIVATION_PLAN,
                f"plan {index} references an absent key",
            )
        if plan.first_edge != expected_first_edge:
            raise _wire_error(
                RLRUNE_BAD_ACTIVATION_PLAN,
                f"plan {index} does not begin at {expected_first_edge}",
            )
        end = plan.first_edge + plan.num_edges
        if end > len(edges):
            raise _wire_error(
                RLRUNE_BAD_ACTIVATION_PLAN,
                f"plan {index} edge range exceeds payload",
            )
        plan_raw = raw_edges[plan.first_edge:end]
        if any(raw not in inventory_raw for raw in plan_raw):
            raise _wire_error(
                RLRUNE_BAD_ACTIVATION_PLAN,
                f"plan {index} contains a non-inventory edge",
            )
        closure_crc = (
            _rune_push_closure_crc(node_by_key[plan.entry_key])
            if push_plan else _crc32(b"".join(plan_raw))
        )
        if plan.closure_crc32 != closure_crc:
            raise _wire_error(
                RLRUNE_BAD_ACTIVATION_PLAN,
                f"plan {index} closure CRC stored=0x{plan.closure_crc32:08x}, "
                f"computed=0x{closure_crc:08x}",
            )
        _rune_validate_production_plan(
            plan,
            edges[plan.first_edge:end],
            owner_link,
            node_by_key,
            inventory_fanout,
            door_members_by_master,
            strings,
            index,
        )
        expected_first_edge = end
    if expected_first_edge != len(edges):
        raise _wire_error(
            RLRUNE_BAD_ACTIVATION_PLAN,
            "plan edge ranges do not partition the post-inventory suffix",
        )


def _rune_validate_seed_lattice(seeds: tuple[RuneSeed, ...]) -> None:
    """Require every current seed to be an exact signed-q8 pmove origin."""

    for index, seed in enumerate(seeds):
        if not _on_fixed_lattice(
            seed.origin, contract.RUNE_PROOF_WORLD_FIXED_SCALE
        ):
            raise _wire_error(
                contract.RLW_BAD_SEED_RECORD,
                f"seed {index} origin is not exact signed-q8",
            )


def _decode_rune_artifact(
    data: bytes | bytearray | memoryview,
    *,
    expected_identity: RuneIdentity | None = None,
) -> RuneArtifact:
    """Decode and structurally lint one authenticated RUNE artifact."""

    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise _wire_error(
            contract.RLW_INVALID_ARGUMENT, "data must be bytes-like"
        )
    data = bytes(data)
    if len(data) < RUNE_HEADER_BYTES:
        raise _wire_error(
            contract.RLW_BAD_FILE_SIZE,
            f"truncated current header: {len(data)} bytes",
        )
    header = _decode_rune_header(data)
    expected_size = _rune_file_size(header)
    if len(data) != expected_size:
        kind = "truncated" if len(data) < expected_size else "trailing bytes"
        raise _wire_error(
            contract.RLW_BAD_FILE_SIZE,
            f"{kind}: got {len(data)}, expected {expected_size}",
        )
    payload = data[RUNE_HEADER_BYTES:]
    computed_payload_crc = _crc32(payload)
    if header.payload_crc32 != computed_payload_crc:
        raise _wire_error(
            contract.RLW_BAD_PAYLOAD_CRC,
            f"stored=0x{header.payload_crc32:08x}, "
            f"computed=0x{computed_payload_crc:08x}",
        )

    offset = RUNE_HEADER_BYTES
    seeds = []
    for _ in range(header.num_seeds):
        values = SEED_STRUCT.unpack_from(data, offset)
        seeds.append(RuneSeed((values[0], values[1], values[2]), values[3], values[4]))
        offset += RUNE_SEED_BYTES

    links = []
    raw_prefix_links = []
    for index in range(header.num_links):
        raw = data[offset:offset + RUNE_LINK_BYTES]
        link = _decode_rune_link(raw, index)
        links.append(link)
        projected = bytearray(raw[:RUNE_POLICY_LINK_BYTES])
        projected_action = contract.action_mechanism_link_policy_action(
            link.action
        )
        if projected_action is None:
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has no current link-policy projection",
            )
        projected[8] = projected_action
        raw_prefix_links.append(bytes(projected))
        offset += RUNE_LINK_BYTES

    nodes = []
    for index in range(header.num_activation_nodes):
        raw = data[offset:offset + RUNE_ACTIVATION_NODE_BYTES]
        nodes.append(_decode_rune_node(raw, index))
        offset += RUNE_ACTIVATION_NODE_BYTES

    edges = []
    raw_edges = []
    for index in range(header.num_activation_edges):
        raw = data[offset:offset + RUNE_ACTIVATION_EDGE_BYTES]
        edges.append(_decode_rune_edge(raw, index))
        raw_edges.append(raw)
        offset += RUNE_ACTIVATION_EDGE_BYTES

    plans = []
    for index in range(header.num_activation_plans):
        raw = data[offset:offset + RUNE_ACTIVATION_PLAN_BYTES]
        plans.append(_decode_rune_plan(raw, index))
        offset += RUNE_ACTIVATION_PLAN_BYTES

    strings = data[offset:offset + header.string_bytes]
    offset += header.string_bytes
    if offset != len(data):
        raise AssertionError("validated current section arithmetic drift")

    seeds_tuple = tuple(seeds)
    links_tuple = tuple(links)
    nodes_tuple = tuple(nodes)
    edges_tuple = tuple(edges)
    plans_tuple = tuple(plans)
    projected_links = tuple(_rune_project_link(link) for link in links_tuple)
    _validate_graph(
        seeds_tuple,
        projected_links,
        tuple(raw_prefix_links),
        tuple(link.activation_plan for link in links_tuple),
    )
    _rune_validate_seed_lattice(seeds_tuple)
    _rune_validate_strings(nodes_tuple, strings)
    for index, node in enumerate(nodes_tuple):
        if (
            node.flags & RUNE_NODEF_FRAME_COMPLETE_MOVER and
            not _rune_frame_complete_button_valid(node, strings)
        ):
            raise _wire_error(
                RLRUNE_BAD_ACTIVATION_NODE,
                f"node {index} has invalid frame-complete button semantics",
            )
    _rune_validate_mechanisms(
        header,
        links_tuple,
        nodes_tuple,
        edges_tuple,
        tuple(raw_edges),
        plans_tuple,
        strings,
    )
    if expected_identity is not None:
        _verify_expected_identity(header.identity, expected_identity)
    return RuneArtifact(
        header=header,
        seeds=seeds_tuple,
        links=links_tuple,
        activation_nodes=nodes_tuple,
        activation_edges=edges_tuple,
        activation_plans=plans_tuple,
        strings=strings,
        payload=payload,
    )


MAX_RUNE_FILE_BYTES = (
    RUNE_HEADER_BYTES +
    MAX_SEEDS * RUNE_SEED_BYTES +
    MAX_LINKS * RUNE_LINK_BYTES +
    RUNE_MAX_ACTIVATION_NODES * RUNE_ACTIVATION_NODE_BYTES +
    RUNE_MAX_ACTIVATION_EDGES * RUNE_ACTIVATION_EDGE_BYTES +
    RUNE_MAX_ACTIVATION_PLANS * RUNE_ACTIVATION_PLAN_BYTES +
    RUNE_MAX_STRING_BYTES
)

def _read_rune_artifact(
    path: str | os.PathLike[str],
    *,
    expected_identity: RuneIdentity | None = None,
) -> RuneArtifact:
    """Read, authenticate, and structurally lint one bounded current artifact."""

    try:
        with open(path, "rb") as stream:
            size = os.fstat(stream.fileno()).st_size
            if size > MAX_RUNE_FILE_BYTES:
                raise _wire_error(
                    contract.RLW_BAD_FILE_SIZE,
                    f"{size} bytes exceeds {MAX_RUNE_FILE_BYTES}",
                )
            data = stream.read(MAX_RUNE_FILE_BYTES + 1)
    except RuneWireError:
        raise
    except (OSError, TypeError, ValueError) as exc:
        raise _wire_error(contract.RLW_IO_ERROR, str(exc)) from exc
    return _decode_rune_artifact(data, expected_identity=expected_identity)


def decode_rune(
    data: bytes | bytearray | memoryview,
    *,
    expected_identity: RuneIdentity | None = None,
) -> RuneArtifact:
    """Decode the current production RUNE artifact."""

    return _decode_rune_artifact(data, expected_identity=expected_identity)


def read_rune(
    path: str | os.PathLike[str],
    *,
    expected_identity: RuneIdentity | None = None,
) -> RuneArtifact:
    """Read the current production RUNE artifact."""

    return _read_rune_artifact(path, expected_identity=expected_identity)


# Production spellings resolve to the one RUNE layout.
decode = decode_rune
read = read_rune
load = read_rune


def summarize_rune(rune: RuneArtifact) -> dict[str, object]:
    """Return stable artifact counts suitable for JSON reporting."""

    if not isinstance(rune, RuneArtifact):
        raise TypeError("rune must be the current RuneArtifact")
    kind_counts = {
        name: sum(node.kind == kind for node in rune.activation_nodes)
        for kind, name in RUNE_NODE_KIND_NAMES.items()
        if kind != RUNE_NODE_NONE and
        any(node.kind == kind for node in rune.activation_nodes)
    }
    return {
        "map_name": rune.header.map_name,
        "seed_count": len(rune.seeds),
        "link_count": len(rune.links),
        "trigger_count": rune.trigger_count,
        "node_count": len(rune.activation_nodes),
        "inventory_edge_count": rune.header.num_inventory_edges,
        "plan_edge_count": rune.header.num_plan_edges,
        "edge_count": len(rune.activation_edges),
        "plan_count": len(rune.activation_plans),
        "node_kinds": kind_counts,
        "payload_crc32": f"{rune.header.payload_crc32:08x}",
        "mechanism_contract_crc32": (
            f"{rune.header.mechanism_contract_crc32:08x}"
        ),
    }




def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Authenticate and inspect a RUNE artifact."
    )
    parser.add_argument(
        "--require-mechanisms",
        action="store_true",
        help="also require nonzero trigger, node, inventory-edge, and plan counts",
    )
    parser.add_argument(
        "--expected-identity",
        metavar="REFERENCE_RUNE",
        help=(
            "require the artifact identity to match the authenticated identity "
            "in REFERENCE_RUNE"
        ),
    )
    parser.add_argument("artifact", help="path to the generated .rune file")
    args = parser.parse_args(argv)
    try:
        expected_identity = None
        if args.expected_identity is not None:
            expected_identity = read_rune(
                args.expected_identity
            ).header.identity
        summary = summarize_rune(
            read_rune(args.artifact, expected_identity=expected_identity)
        )
    except RuneWireError as exc:
        parser.error(str(exc))
    if args.require_mechanisms and any(
        summary[key] == 0
        for key in (
            "trigger_count", "node_count", "inventory_edge_count", "plan_count"
        )
    ):
        parser.error(
            "artifact lacks the required nonzero "
            "trigger/node/inventory-edge/plan counts"
        )
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
