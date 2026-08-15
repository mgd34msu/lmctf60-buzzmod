#!/usr/bin/env python3
"""Strict, explicit little-endian RUNE v3 wire codec.

This module deliberately knows the v3 wire registry without authorizing an
action for live execution.  Runtime support remains a separate outer-action
policy in :mod:`rune_contracts_generated`.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
import os
import re
import struct
from typing import Iterable
import zlib

try:
    import rune_contracts_generated as contract
except ModuleNotFoundError:  # Also support ``python -m tools.runeio``.
    from tools import rune_contracts_generated as contract


HEADER_STRUCT = struct.Struct("<IHHHHIIIIIIIfffHHII64s")
SEED_STRUCT = struct.Struct("<fffhh")
LINK_STRUCT = struct.Struct("<IIBBBBBBh3f3fHBB")

assert HEADER_STRUCT.size == contract.RUNE_V3_HEADER_BYTES
assert SEED_STRUCT.size == contract.RUNE_V3_SEED_BYTES
assert LINK_STRUCT.size == contract.RUNE_V3_LINK_BYTES

RSF_WATER = 1
RSF_TOMBSTONE = 2
SEED_FLAG_MASK = RSF_WATER | RSF_TOMBSTONE

_MAP_NAME = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_-]{0,62}\Z")
_ZERO_12 = b"\x00" * 12
_ZERO_16 = b"\x00" * contract.RUNE_V3_NONCOMPOUND_TAIL_BYTES
_WORLD_MIN = (
    contract.RUNE_PROOF_WORLD_FIXED_MIN /
    contract.RUNE_PROOF_WORLD_FIXED_SCALE
)
_WORLD_MAX = (
    contract.RUNE_PROOF_WORLD_FIXED_MAX /
    contract.RUNE_PROOF_WORLD_FIXED_SCALE
)


class RuneWireError(ValueError):
    """A stable generated wire diagnostic plus optional record context."""

    def __init__(self, code: int, detail: str | None = None):
        try:
            diagnostic = contract.WIRE_DIAGNOSTIC_BY_ID[code]
        except (AttributeError, KeyError) as exc:
            raise AssertionError(f"unknown generated wire diagnostic {code!r}") from exc
        self.code = diagnostic["id"]
        self.symbol = diagnostic["symbol"]
        self.message = diagnostic["message"]
        self.detail = detail
        text = f"{self.symbol}: {self.message}"
        if detail:
            text += f": {detail}"
        super().__init__(text)


def _wire_error(code: int, detail: str | None = None) -> RuneWireError:
    return RuneWireError(code, detail)


@dataclass(frozen=True)
class RuneIdentityV3:
    """External map and movement identity used to encode or authenticate v3."""

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
class RuneHeaderV3:
    magic: int
    version: int
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

    @property
    def identity(self) -> RuneIdentityV3:
        return RuneIdentityV3(
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


@dataclass(frozen=True)
class RuneSeedV3:
    origin: tuple[float, float, float]
    area_hint: int = 0
    flags: int = 0


@dataclass(frozen=True)
class RuneLinkV3:
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
class RuneV3:
    """One decoded v3 file with ordered immutable records and exact payload."""

    header: RuneHeaderV3
    seeds: tuple[RuneSeedV3, ...]
    links: tuple[RuneLinkV3, ...]
    payload: bytes

    @property
    def payload_crc32(self) -> int:
        return self.header.payload_crc32

    @property
    def identity(self) -> RuneIdentityV3:
        return self.header.identity


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
    return encoded + b"\x00" * (contract.RUNE_V3_MAP_NAME_BYTES - len(encoded))


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


def _normalize_identity(identity: object) -> RuneIdentityV3:
    if not isinstance(identity, RuneIdentityV3):
        raise _wire_error(
            contract.RLW_INVALID_ARGUMENT, "identity must be RuneIdentityV3"
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
    return RuneIdentityV3(
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


def _encode_seed(seed: object, index: int) -> tuple[RuneSeedV3, bytes]:
    if not isinstance(seed, RuneSeedV3):
        raise _wire_error(
            contract.RLW_INVALID_ARGUMENT,
            f"seed {index} must be RuneSeedV3",
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
    normalized = RuneSeedV3(origin, seed.area_hint, seed.flags)
    return normalized, SEED_STRUCT.pack(*origin, seed.area_hint, seed.flags)


def _encode_link(link: object, index: int) -> tuple[RuneLinkV3, bytes]:
    if not isinstance(link, RuneLinkV3):
        raise _wire_error(
            contract.RLW_INVALID_ARGUMENT,
            f"link {index} must be RuneLinkV3",
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
        contract.RUNE_V3_MIN_COST_MS,
        contract.RUNE_V3_MAX_COST_MS,
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
    normalized = RuneLinkV3(
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
    encoded = LINK_STRUCT.pack(
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
    )
)
_FIXED_DOOR_ANCHOR_POLICIES = frozenset(
    (
        contract.RLAP_DOOR_WAIT,
        contract.RLAP_DOOR_PREOPEN_CONTACT,
        contract.RLAP_DOOR_RIDE_INGRESS_LIP,
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
    if policy not in (contract.RLAP_HOOK_CONTROL, contract.RLAP_UNSUPPORTED):
        raise _wire_error(
            contract.RLW_BAD_LINK_RECORD,
            f"link {index} has unknown {field} policy {policy}",
        )


def _validate_graph(
    seeds: tuple[RuneSeedV3, ...],
    links: tuple[RuneLinkV3, ...],
    raw_links: tuple[bytes, ...],
) -> None:
    if not 0 < len(seeds) <= contract.RUNE_V3_MAX_SEEDS:
        raise _wire_error(
            contract.RLW_BAD_COUNTS, f"{len(seeds)} seeds"
        )
    if not 0 <= len(links) <= contract.RUNE_V3_MAX_LINKS:
        raise _wire_error(
            contract.RLW_BAD_COUNTS, f"{len(links)} links"
        )
    if len(raw_links) != len(links):
        raise AssertionError("raw link count does not match decoded links")

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
    identities: set[tuple[int, int, int]] = set()
    for index, (link, raw) in enumerate(zip(links, raw_links)):
        if len(raw) != contract.RUNE_V3_LINK_BYTES:
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
        # The v3 wire namespace is frozen independently of later registry
        # growth.  Per-action masks refine these bounds; they must never make
        # a future action, provenance, or mode byte legal in an old v3 file.
        if not 0 <= link.action <= contract.RL_DOOR_HOOK:
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has action outside frozen v3 range "
                f"{link.action}",
            )
        if not 0 <= link.provenance <= contract.RL_CONTRACTED:
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has provenance outside frozen v3 range "
                f"{link.provenance}",
            )
        if not contract.RLCM_NONE <= link.mode <= contract.RLCM_RIDE:
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has mode outside frozen v3 range {link.mode}",
            )
        try:
            action = contract.action_contract(link.action)
        except (TypeError, ValueError) as exc:
            raise _wire_error(
                contract.RLW_BAD_LINK_RECORD,
                f"link {index} has unknown action {link.action}",
            ) from exc
        identity = (link.source, link.destination, link.action)
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
            contract.RUNE_V3_MIN_COST_MS <= link.cost_ms <=
            contract.RUNE_V3_MAX_COST_MS
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
            if raw[contract.RUNE_V3_NONCOMPOUND_TAIL_OFFSET:] != _ZERO_16:
                raise _wire_error(
                    contract.RLW_BAD_LINK_RECORD,
                    f"link {index} has nonzero noncompound tail",
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
        if action["mechanism_policy"] != contract.RLMP_DOOR_WORLD_FIXED_1_8:
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

    for index, seed in enumerate(seeds):
        tombstone = bool(seed.flags & RSF_TOMBSTONE)
        if tombstone == (index in linked_sources):
            raise _wire_error(
                contract.RLW_BAD_ROUTE_OWNERSHIP,
                f"seed {index} tombstone/outgoing identity mismatch",
            )


def _header_crc(header: bytes) -> int:
    if len(header) != contract.RUNE_V3_HEADER_BYTES:
        raise AssertionError("header CRC received wrong byte count")
    canonical = bytearray(header)
    struct.pack_into(
        "<I", canonical, contract.RUNE_V3_HEADER_CRC_OFFSET, 0
    )
    return _crc32(canonical)


def encode_v3(
    identity: RuneIdentityV3,
    seeds: Iterable[RuneSeedV3],
    links: Iterable[RuneLinkV3],
) -> bytes:
    """Encode one canonical v3 file after complete structural validation."""

    identity = _normalize_identity(identity)
    input_seeds = _as_tuple(seeds, "seeds", contract.RUNE_V3_MAX_SEEDS)
    input_links = _as_tuple(links, "links", contract.RUNE_V3_MAX_LINKS)
    if not 0 < len(input_seeds) <= contract.RUNE_V3_MAX_SEEDS:
        raise _wire_error(
            contract.RLW_BAD_COUNTS, f"{len(input_seeds)} seeds"
        )
    if not 0 <= len(input_links) <= contract.RUNE_V3_MAX_LINKS:
        raise _wire_error(
            contract.RLW_BAD_COUNTS, f"{len(input_links)} links"
        )

    encoded_seeds = [
        _encode_seed(seed, index) for index, seed in enumerate(input_seeds)
    ]
    encoded_links = [
        _encode_link(link, index) for index, link in enumerate(input_links)
    ]
    normalized_seeds = tuple(item[0] for item in encoded_seeds)
    normalized_links = tuple(item[0] for item in encoded_links)
    raw_links = tuple(item[1] for item in encoded_links)
    _validate_graph(normalized_seeds, normalized_links, raw_links)
    payload = b"".join(item[1] for item in encoded_seeds) + b"".join(raw_links)
    payload_crc32 = _crc32(payload)
    header = HEADER_STRUCT.pack(
        contract.RUNE_V3_MAGIC,
        contract.RUNE_V3_VERSION,
        contract.RUNE_V3_HEADER_BYTES,
        contract.RUNE_V3_SEED_BYTES,
        contract.RUNE_V3_LINK_BYTES,
        len(normalized_seeds),
        len(normalized_links),
        payload_crc32,
        identity.bsp_checksum,
        identity.entity_crc32,
        contract.CONTRACT_CRC32,
        identity.physics_flags,
        identity.gravity,
        identity.airaccelerate,
        identity.maxvelocity,
        identity.pmove_substep_ms,
        identity.server_frame_ms,
        identity.host_physics_id,
        0,
        _map_bytes(identity.map_name),
    )
    canonical_header = bytearray(header)
    struct.pack_into(
        "<I",
        canonical_header,
        contract.RUNE_V3_HEADER_CRC_OFFSET,
        _header_crc(header),
    )
    return bytes(canonical_header) + payload


def _verify_expected_identity(
    actual: RuneIdentityV3, expected: RuneIdentityV3
) -> None:
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


def decode_v3(
    data: bytes | bytearray | memoryview,
    *,
    expected_identity: RuneIdentityV3 | None = None,
) -> RuneV3:
    """Decode and structurally validate exactly one complete v3 byte string.

    Structural validation alone does not authenticate the currently running
    map.  Callers loading for execution must provide ``expected_identity``.
    """

    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise _wire_error(
            contract.RLW_INVALID_ARGUMENT, "data must be bytes-like"
        )
    data = bytes(data)
    if len(data) < contract.RUNE_V3_HEADER_BYTES:
        raise _wire_error(
            contract.RLW_BAD_FILE_SIZE,
            f"truncated header: {len(data)} bytes",
        )
    fields = HEADER_STRUCT.unpack_from(data)
    (
        magic,
        version,
        header_bytes,
        seed_bytes,
        link_bytes,
        num_seeds,
        num_links,
        payload_crc32,
        bsp_checksum,
        entity_crc32,
        action_contract_crc32,
        physics_flags,
        gravity,
        airaccelerate,
        maxvelocity,
        pmove_substep_ms,
        server_frame_ms,
        host_physics_id,
        header_crc32,
        raw_map_name,
    ) = fields
    if magic != contract.RUNE_V3_MAGIC:
        raise _wire_error(
            contract.RLW_BAD_MAGIC,
            f"0x{magic:08x}",
        )
    if version != contract.RUNE_V3_VERSION:
        raise _wire_error(contract.RLW_UNSUPPORTED_VERSION, str(version))
    if header_bytes != contract.RUNE_V3_HEADER_BYTES:
        raise _wire_error(contract.RLW_BAD_HEADER_SIZE, str(header_bytes))
    if seed_bytes != contract.RUNE_V3_SEED_BYTES:
        raise _wire_error(contract.RLW_BAD_SEED_SIZE, str(seed_bytes))
    if link_bytes != contract.RUNE_V3_LINK_BYTES:
        raise _wire_error(contract.RLW_BAD_LINK_SIZE, str(link_bytes))
    header_bytes_raw = data[:contract.RUNE_V3_HEADER_BYTES]
    computed_header_crc32 = _header_crc(header_bytes_raw)
    if header_crc32 != computed_header_crc32:
        raise _wire_error(
            contract.RLW_BAD_HEADER_CRC,
            f"stored=0x{header_crc32:08x}, "
            f"computed=0x{computed_header_crc32:08x}",
        )
    if not 0 < num_seeds <= contract.RUNE_V3_MAX_SEEDS:
        raise _wire_error(contract.RLW_BAD_COUNTS, f"{num_seeds} seeds")
    if not 0 <= num_links <= contract.RUNE_V3_MAX_LINKS:
        raise _wire_error(contract.RLW_BAD_COUNTS, f"{num_links} links")

    expected_size = (
        contract.RUNE_V3_HEADER_BYTES +
        num_seeds * contract.RUNE_V3_SEED_BYTES +
        num_links * contract.RUNE_V3_LINK_BYTES
    )
    if len(data) != expected_size:
        kind = "truncated" if len(data) < expected_size else "trailing bytes"
        raise _wire_error(
            contract.RLW_BAD_FILE_SIZE,
            f"{kind}: got {len(data)}, expected {expected_size}",
        )
    map_name = _decode_map_name(raw_map_name)
    if action_contract_crc32 != contract.CONTRACT_CRC32:
        raise _wire_error(
            contract.RLW_BAD_ACTION_CONTRACT,
            f"stored=0x{action_contract_crc32:08x}, "
            f"expected=0x{contract.CONTRACT_CRC32:08x}",
        )
    identity = _normalize_identity(
        RuneIdentityV3(
            map_name=map_name,
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
    )
    payload = data[contract.RUNE_V3_HEADER_BYTES:]
    computed_payload_crc32 = _crc32(payload)
    if payload_crc32 != computed_payload_crc32:
        raise _wire_error(
            contract.RLW_BAD_PAYLOAD_CRC,
            f"stored=0x{payload_crc32:08x}, "
            f"computed=0x{computed_payload_crc32:08x}",
        )

    seeds = []
    offset = contract.RUNE_V3_HEADER_BYTES
    for _ in range(num_seeds):
        x, y, z, area_hint, flags = SEED_STRUCT.unpack_from(data, offset)
        seeds.append(RuneSeedV3((x, y, z), area_hint, flags))
        offset += contract.RUNE_V3_SEED_BYTES

    links = []
    raw_links = []
    for _ in range(num_links):
        raw = data[offset:offset + contract.RUNE_V3_LINK_BYTES]
        values = LINK_STRUCT.unpack(raw)
        links.append(
            RuneLinkV3(
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
            )
        )
        raw_links.append(raw)
        offset += contract.RUNE_V3_LINK_BYTES
    seeds_tuple = tuple(seeds)
    links_tuple = tuple(links)
    _validate_graph(seeds_tuple, links_tuple, tuple(raw_links))
    if expected_identity is not None:
        _verify_expected_identity(identity, expected_identity)

    header = RuneHeaderV3(
        magic=magic,
        version=version,
        header_bytes=header_bytes,
        seed_bytes=seed_bytes,
        link_bytes=link_bytes,
        num_seeds=num_seeds,
        num_links=num_links,
        payload_crc32=payload_crc32,
        bsp_checksum=bsp_checksum,
        entity_crc32=entity_crc32,
        action_contract_crc32=action_contract_crc32,
        physics_flags=physics_flags,
        gravity=gravity,
        airaccelerate=airaccelerate,
        maxvelocity=maxvelocity,
        pmove_substep_ms=pmove_substep_ms,
        server_frame_ms=server_frame_ms,
        host_physics_id=host_physics_id,
        header_crc32=header_crc32,
        map_name=map_name,
    )
    return RuneV3(header, seeds_tuple, links_tuple, payload)


MAX_V3_FILE_BYTES = (
    contract.RUNE_V3_HEADER_BYTES +
    contract.RUNE_V3_MAX_SEEDS * contract.RUNE_V3_SEED_BYTES +
    contract.RUNE_V3_MAX_LINKS * contract.RUNE_V3_LINK_BYTES
)


def looks_like_v3_prefix(data: bytes | bytearray | memoryview) -> bool:
    """Recognize a v3-family prefix before any legacy-layout interpretation.

    The legacy layout stores its version as a 32-bit integer, so the first six
    bytes of a legacy ``version == 3`` file are indistinguishable from v3's
    magic plus 16-bit version.  Require either v3's exact header size, or its
    exact link size.  The latter cannot be an accepted legacy count: byte 44
    occupies the high half of legacy ``num_seeds`` and therefore implies at
    least 2,883,584 seeds, beyond the frozen 32,768 cap.
    Those independent fixed
    fields keep one-field-corrupt v3 files on the strict v3 diagnostic path
    without stealing legacy forensic files from the legacy readers.
    """

    if not isinstance(data, (bytes, bytearray, memoryview)):
        return False
    prefix = bytes(data)
    if (len(prefix) >= 8 and
            struct.unpack_from("<H", prefix, 6)[0] ==
            contract.RUNE_V3_HEADER_BYTES):
        return True
    return (
        len(prefix) >= 12 and
        struct.unpack_from("<H", prefix, 10)[0] ==
        contract.RUNE_V3_LINK_BYTES
    )


def read_v3(
    path: str | os.PathLike[str],
    *,
    expected_identity: RuneIdentityV3 | None = None,
) -> RuneV3:
    """Read one bounded v3 file and apply :func:`decode_v3`."""

    try:
        with open(path, "rb") as stream:
            size = os.fstat(stream.fileno()).st_size
            if size > MAX_V3_FILE_BYTES:
                raise _wire_error(
                    contract.RLW_BAD_FILE_SIZE,
                    f"{size} bytes exceeds {MAX_V3_FILE_BYTES}",
                )
            data = stream.read(MAX_V3_FILE_BYTES + 1)
    except RuneWireError:
        raise
    except (OSError, TypeError, ValueError) as exc:
        raise _wire_error(contract.RLW_IO_ERROR, str(exc)) from exc
    return decode_v3(data, expected_identity=expected_identity)
