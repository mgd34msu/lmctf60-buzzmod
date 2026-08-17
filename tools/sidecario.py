#!/usr/bin/env python3
"""Authenticated explicit-little-endian codecs for RUNE sidecars.

The fixed header binds each payload to one already-authenticated RUNE artifact:
the ordered graph counts, payload CRC, action-contract CRC, and header CRC.  The
wire shape is shared with ``slipgate/sg_sidecar_wire.c``; this module never
serializes native Python or C objects.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import struct
from typing import Iterable, Mapping
import zlib

try:
    import rune_contracts_generated as contract
    import runeio
except ModuleNotFoundError:  # Also support ``python -m tools.sidecario``.
    from tools import rune_contracts_generated as contract
    from tools import runeio


HEADER_STRUCT = struct.Struct("<I6H8I")
HEADER_BYTES = 48
HEADER_CRC_OFFSET = 44
DANGER_MAX = 8000

assert HEADER_STRUCT.size == HEADER_BYTES


def _fourcc(text: str) -> int:
    raw = text.encode("ascii")
    if len(raw) != 4:
        raise AssertionError("sidecar magic must have four bytes")
    return int.from_bytes(raw, "little")


@dataclass(frozen=True)
class SidecarKind:
    name: str
    extension: str
    magic: int
    axis: str
    element_bytes: int
    planes: int


HMN = SidecarKind("human", ".hmn", _fourcc("HMNR"), "link", 1, 1)
HML = SidecarKind("flag-live", ".hml", _fourcc("HMLR"), "link", 1, 1)
HME = SidecarKind("escape", ".hme", _fourcc("HMER"), "link", 1, 1)
DPO = SidecarKind("defense", ".dpo", _fourcc("DPOR"), "seed", 1, 4)
DNG = SidecarKind("danger", ".rune.danger", _fourcc("DNGR"), "seed", 4, 2)

KINDS = (HMN, HML, HME, DPO, DNG)
KIND_BY_MAGIC = {kind.magic: kind for kind in KINDS}
KIND_BY_NAME = {
    name: kind
    for kind, names in (
        (HMN, ("HMN", "HUMAN")),
        (HML, ("HML", "FLAG-LIVE", "FLAG_LIVE")),
        (HME, ("HME", "ESCAPE")),
        (DPO, ("DPO", "DEFENSE")),
        (DNG, ("DNG", "DANGER")),
    )
    for name in names
}
KIND_BY_EXTENSION = {kind.extension: kind for kind in KINDS}

assert HMN.magic == 0x524E4D48
assert HML.magic == 0x524C4D48
assert HME.magic == 0x52454D48
assert DPO.magic == 0x524F5044
assert DNG.magic == 0x52474E44

MAX_FILE_BYTES = HEADER_BYTES + max(
    runeio.MAX_LINKS,
    runeio.MAX_SEEDS * DNG.element_bytes * DNG.planes,
)


class SidecarDiagnostic(IntEnum):
    SCD_OK = 0
    SCD_ABSENT = 1
    SCD_INVALID_ARGUMENT = 2
    SCD_PATH_TOO_LONG = 3
    SCD_IO_ERROR = 4
    SCD_BAD_MAGIC = 5
    SCD_BAD_HEADER_SIZE = 6
    SCD_BAD_HEADER_CRC = 7
    SCD_NONZERO_RESERVED = 8
    SCD_BAD_SHAPE = 9
    SCD_BAD_COUNTS = 10
    SCD_BAD_PAYLOAD_SIZE = 11
    SCD_BAD_FILE_SIZE = 12
    SCD_RUNE_PAYLOAD_MISMATCH = 13
    SCD_ACTION_CONTRACT_MISMATCH = 14
    SCD_RUNE_HEADER_MISMATCH = 15
    SCD_BAD_PAYLOAD_CRC = 16
    SCD_BAD_PAYLOAD_VALUE = 17
    SCD_ALLOCATION_FAILED = 18
    SCD_TEMP_EXHAUSTED = 19
    SCD_STATE_DRIFT = 20
    SCD_INTERNAL_ERROR = 21

    @property
    def symbol(self) -> str:
        return self.name

    @property
    def message(self) -> str:
        return SIDECAR_DIAGNOSTIC_MESSAGES[int(self)]


SIDECAR_DIAGNOSTIC_MESSAGES = (
    "sidecar operation succeeded",
    "optional sidecar is absent",
    "invalid sidecar codec argument",
    "sidecar path is too long",
    "sidecar I/O failed",
    "bad sidecar magic",
    "bad sidecar header size",
    "sidecar header CRC mismatch",
    "sidecar reserved field is nonzero",
    "sidecar payload shape mismatch",
    "sidecar graph counts are invalid",
    "sidecar payload size is invalid",
    "sidecar file size is invalid",
    "sidecar RUNE payload binding mismatch",
    "sidecar action-contract binding mismatch",
    "sidecar RUNE header binding mismatch",
    "sidecar payload CRC mismatch",
    "sidecar payload value is invalid",
    "sidecar allocation failed",
    "sidecar temporary-name attempts exhausted",
    "sidecar state changed during the operation",
    "internal sidecar error",
)

assert len(SIDECAR_DIAGNOSTIC_MESSAGES) == len(SidecarDiagnostic)

SCD_OK = SidecarDiagnostic.SCD_OK
SCD_ABSENT = SidecarDiagnostic.SCD_ABSENT
SCD_INVALID_ARGUMENT = SidecarDiagnostic.SCD_INVALID_ARGUMENT
SCD_PATH_TOO_LONG = SidecarDiagnostic.SCD_PATH_TOO_LONG
SCD_IO_ERROR = SidecarDiagnostic.SCD_IO_ERROR
SCD_BAD_MAGIC = SidecarDiagnostic.SCD_BAD_MAGIC
SCD_BAD_HEADER_SIZE = SidecarDiagnostic.SCD_BAD_HEADER_SIZE
SCD_BAD_HEADER_CRC = SidecarDiagnostic.SCD_BAD_HEADER_CRC
SCD_NONZERO_RESERVED = SidecarDiagnostic.SCD_NONZERO_RESERVED
SCD_BAD_SHAPE = SidecarDiagnostic.SCD_BAD_SHAPE
SCD_BAD_COUNTS = SidecarDiagnostic.SCD_BAD_COUNTS
SCD_BAD_PAYLOAD_SIZE = SidecarDiagnostic.SCD_BAD_PAYLOAD_SIZE
SCD_BAD_FILE_SIZE = SidecarDiagnostic.SCD_BAD_FILE_SIZE
SCD_RUNE_PAYLOAD_MISMATCH = SidecarDiagnostic.SCD_RUNE_PAYLOAD_MISMATCH
SCD_ACTION_CONTRACT_MISMATCH = (
    SidecarDiagnostic.SCD_ACTION_CONTRACT_MISMATCH
)
SCD_RUNE_HEADER_MISMATCH = SidecarDiagnostic.SCD_RUNE_HEADER_MISMATCH
SCD_BAD_PAYLOAD_CRC = SidecarDiagnostic.SCD_BAD_PAYLOAD_CRC
SCD_BAD_PAYLOAD_VALUE = SidecarDiagnostic.SCD_BAD_PAYLOAD_VALUE
SCD_ALLOCATION_FAILED = SidecarDiagnostic.SCD_ALLOCATION_FAILED
SCD_TEMP_EXHAUSTED = SidecarDiagnostic.SCD_TEMP_EXHAUSTED
SCD_STATE_DRIFT = SidecarDiagnostic.SCD_STATE_DRIFT
SCD_INTERNAL_ERROR = SidecarDiagnostic.SCD_INTERNAL_ERROR
SCD_DIAGNOSTIC_COUNT = len(SidecarDiagnostic)


class SidecarError(ValueError):
    """One stable sidecar diagnostic plus optional bounded detail."""

    def __init__(self, diagnostic: SidecarDiagnostic | int, detail: str = ""):
        try:
            diagnostic = SidecarDiagnostic(diagnostic)
        except (TypeError, ValueError) as exc:
            raise AssertionError(
                f"unknown sidecar diagnostic {diagnostic!r}"
            ) from exc
        self.diagnostic = diagnostic
        self.code = int(diagnostic)
        self.symbol = diagnostic.symbol
        self.message = diagnostic.message
        self.detail = detail
        text = f"{self.symbol}: {self.message}"
        if detail:
            text += f": {detail}"
        super().__init__(text)


@dataclass(frozen=True)
class Binding:
    num_seeds: int
    num_links: int
    rune_payload_crc32: int
    action_contract_crc32: int
    rune_header_crc32: int


@dataclass(frozen=True)
class Header:
    kind: SidecarKind
    header_bytes: int
    element_bytes: int
    planes: int
    num_seeds: int
    num_links: int
    rune_payload_crc32: int
    action_contract_crc32: int
    rune_header_crc32: int
    payload_bytes: int
    payload_crc32: int
    header_crc32: int

    @property
    def binding(self) -> Binding:
        return Binding(
            self.num_seeds,
            self.num_links,
            self.rune_payload_crc32,
            self.action_contract_crc32,
            self.rune_header_crc32,
        )


@dataclass(frozen=True)
class Sidecar:
    header: Header
    payload: bytes


def _error(diagnostic: SidecarDiagnostic, detail: str = "") -> SidecarError:
    return SidecarError(diagnostic, detail)


def _uint(value: object, bits: int, label: str) -> int:
    if type(value) is not int or not 0 <= value < 1 << bits:
        raise _error(SCD_INVALID_ARGUMENT, f"{label} is not a u{bits} integer")
    return value


def resolve_kind(kind: SidecarKind | str) -> SidecarKind:
    if isinstance(kind, SidecarKind):
        if KIND_BY_MAGIC.get(kind.magic) != kind:
            raise _error(SCD_INVALID_ARGUMENT, "unknown sidecar kind")
        return kind
    if not isinstance(kind, str):
        raise _error(SCD_INVALID_ARGUMENT, "sidecar kind must be a name")
    resolved = KIND_BY_NAME.get(kind.upper()) or KIND_BY_EXTENSION.get(kind)
    if resolved is None:
        raise _error(SCD_INVALID_ARGUMENT, f"unknown sidecar kind {kind!r}")
    return resolved


def binding_from_rune(rune: Mapping[str, object] | runeio.RuneArtifact) -> Binding:
    """Extract a sidecar binding from one authenticated RUNE artifact."""

    if isinstance(rune, runeio.RuneArtifact):
        header = rune.header
        node_count = _uint(
            header.num_activation_nodes, 32, "mechanism node count"
        )
        edge_count = _uint(
            header.num_activation_edges, 32, "mechanism edge count"
        )
        inventory_count = _uint(
            header.num_inventory_edges, 32, "inventory edge count"
        )
        plan_count = _uint(
            header.num_activation_plans, 32, "mechanism plan count"
        )
        string_bytes = _uint(
            header.string_bytes, 32, "mechanism string bytes"
        )
        if (
            node_count > runeio.RUNE_MAX_ACTIVATION_NODES or
            edge_count > runeio.RUNE_MAX_ACTIVATION_EDGES or
            inventory_count > edge_count or
            plan_count > runeio.RUNE_MAX_ACTIVATION_PLANS or
            not 0 < string_bytes <= runeio.RUNE_MAX_STRING_BYTES or
            len(rune.seeds) != header.num_seeds or
            len(rune.links) != header.num_links or
            len(rune.activation_nodes) != node_count or
            len(rune.activation_edges) != edge_count or
            len(rune.activation_plans) != plan_count or
            len(rune.strings) != string_bytes or
            not isinstance(header.map_name, str) or
            not header.map_name
        ):
            raise _error(SCD_INVALID_ARGUMENT, "malformed RUNE artifact")
        binding = Binding(
            num_seeds=_uint(header.num_seeds, 32, "seed count"),
            num_links=_uint(header.num_links, 32, "link count"),
            rune_payload_crc32=_uint(
                header.payload_crc32, 32, "rune payload CRC"
            ),
            action_contract_crc32=_uint(
                header.action_contract_crc32, 32, "action-contract CRC"
            ),
            rune_header_crc32=_uint(
                header.header_crc32, 32, "rune header CRC"
            ),
        )
    elif isinstance(rune, Mapping):
        try:
            node_count = _uint(
                rune["num_activation_nodes"], 32, "mechanism node count"
            )
            edge_count = _uint(
                rune["num_activation_edges"], 32, "mechanism edge count"
            )
            inventory_count = _uint(
                rune["num_inventory_edges"], 32, "inventory edge count"
            )
            plan_count = _uint(
                rune["num_activation_plans"], 32, "mechanism plan count"
            )
            strings = rune["strings"]
            map_name = rune["map"]
            binding = Binding(
                num_seeds=_uint(rune["num_seeds"], 32, "seed count"),
                num_links=_uint(rune["num_links"], 32, "link count"),
                rune_payload_crc32=_uint(
                    rune["payload_crc32"], 32, "rune payload CRC"
                ),
                action_contract_crc32=_uint(
                    rune["action_contract_crc32"], 32,
                    "action-contract CRC",
                ),
                rune_header_crc32=_uint(
                    rune["header_crc32"], 32, "rune header CRC"
                ),
            )
        except KeyError as exc:
            raise _error(
                SCD_INVALID_ARGUMENT, f"rune metadata lacks {exc.args[0]}"
            ) from exc
        if (
            node_count > runeio.RUNE_MAX_ACTIVATION_NODES or
            edge_count > runeio.RUNE_MAX_ACTIVATION_EDGES or
            inventory_count > edge_count or
            plan_count > runeio.RUNE_MAX_ACTIVATION_PLANS or
            not isinstance(strings, bytes) or
            not 0 < len(strings) <= runeio.RUNE_MAX_STRING_BYTES or
            not isinstance(map_name, str) or
            not map_name
        ):
            raise _error(SCD_INVALID_ARGUMENT, "malformed RUNE artifact metadata")
    else:
        raise _error(SCD_INVALID_ARGUMENT, "rune must be a RUNE artifact")
    return _validate_binding(binding)


def _validate_binding(binding: object, *, action_exact: bool = True) -> Binding:
    if not isinstance(binding, Binding):
        raise _error(SCD_INVALID_ARGUMENT, "expected a RUNE binding")
    for value, bits, label in (
        (binding.num_seeds, 32, "seed count"),
        (binding.num_links, 32, "link count"),
        (binding.rune_payload_crc32, 32, "rune payload CRC"),
        (binding.action_contract_crc32, 32, "action-contract CRC"),
        (binding.rune_header_crc32, 32, "rune header CRC"),
    ):
        _uint(value, bits, label)
    if not 0 < binding.num_seeds <= runeio.MAX_SEEDS:
        raise _error(SCD_BAD_COUNTS, f"invalid seed count {binding.num_seeds}")
    if not 0 <= binding.num_links <= runeio.MAX_LINKS:
        raise _error(SCD_BAD_COUNTS, f"invalid link count {binding.num_links}")
    if (
        action_exact and
        binding.action_contract_crc32 != contract.RUNE_ACTION_CONTRACT_CRC32
    ):
        raise _error(
            SCD_ACTION_CONTRACT_MISMATCH,
            f"0x{binding.action_contract_crc32:08x}",
        )
    return binding


def expected_payload_bytes(kind: SidecarKind | str, binding: Binding) -> int:
    spec = resolve_kind(kind)
    binding = _validate_binding(binding)
    count = binding.num_links if spec.axis == "link" else binding.num_seeds
    return count * spec.element_bytes * spec.planes


def _crc32(data: bytes | bytearray | memoryview) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _header_crc(header: bytes | bytearray | memoryview) -> int:
    if len(header) != HEADER_BYTES:
        raise _error(SCD_BAD_HEADER_SIZE, str(len(header)))
    canonical = bytearray(header)
    canonical[HEADER_CRC_OFFSET:HEADER_CRC_OFFSET + 4] = b"\0" * 4
    return _crc32(canonical)


def _tombstones(
    indices: Iterable[int] | None, num_seeds: int
) -> tuple[int, ...]:
    if indices is None:
        raise _error(
            SCD_INVALID_ARGUMENT,
            "seed sidecars require the bound artifact's tombstone indices",
        )
    try:
        iterator = iter(indices)
    except TypeError as exc:
        raise _error(SCD_INVALID_ARGUMENT, "tombstones must be iterable") from exc
    result: set[int] = set()
    for index in iterator:
        if type(index) is not int:
            raise _error(SCD_INVALID_ARGUMENT, "tombstone index is not an integer")
        if not 0 <= index < num_seeds:
            raise _error(SCD_BAD_PAYLOAD_VALUE, f"tombstone index {index}")
        if index in result:
            raise _error(SCD_INVALID_ARGUMENT, f"duplicate tombstone index {index}")
        result.add(index)
    return tuple(sorted(result))


def _validate_payload(
    kind: SidecarKind,
    binding: Binding,
    payload: bytes,
    tombstone_indices: Iterable[int] | None,
) -> None:
    expected = expected_payload_bytes(kind, binding)
    if len(payload) != expected:
        raise _error(
            SCD_BAD_PAYLOAD_SIZE,
            f"{kind.name} payload has {len(payload)} bytes, expected {expected}",
        )
    if kind.axis != "seed":
        return
    tombstones = frozenset(_tombstones(tombstone_indices, binding.num_seeds))
    for plane in range(kind.planes):
        base = plane * binding.num_seeds
        for seed in range(binding.num_seeds):
            offset = (base + seed) * kind.element_bytes
            value = (
                payload[offset]
                if kind.element_bytes == 1
                else struct.unpack_from("<I", payload, offset)[0]
            )
            if kind is DNG and value > DANGER_MAX:
                raise _error(
                    SCD_BAD_PAYLOAD_VALUE,
                    f"danger plane {plane} seed {seed} has value {value}",
                )
            if seed in tombstones and value != 0:
                raise _error(
                    SCD_BAD_PAYLOAD_VALUE,
                    f"{kind.name} plane {plane} assigns tombstone seed {seed}",
                )


def encode(
    kind: SidecarKind | str,
    binding: Binding,
    payload: bytes | bytearray | memoryview,
    *,
    tombstone_indices: Iterable[int] | None = None,
) -> bytes:
    """Encode one sidecar using the current production wire contract."""

    spec = resolve_kind(kind)
    binding = _validate_binding(binding)
    if not isinstance(payload, (bytes, bytearray, memoryview)):
        raise _error(SCD_INVALID_ARGUMENT, "payload must be bytes-like")
    try:
        payload = bytes(memoryview(payload).cast("B"))
    except (TypeError, ValueError) as exc:
        raise _error(
            SCD_INVALID_ARGUMENT, "payload must be a contiguous byte view"
        ) from exc
    if len(payload) > MAX_FILE_BYTES - HEADER_BYTES:
        raise _error(SCD_BAD_PAYLOAD_SIZE, f"payload has {len(payload)} bytes")
    _validate_payload(spec, binding, payload, tombstone_indices)
    fields = (
        spec.magic,
        0,
        HEADER_BYTES,
        0,
        spec.element_bytes,
        spec.planes,
        0,
        binding.num_seeds,
        binding.num_links,
        binding.rune_payload_crc32,
        binding.action_contract_crc32,
        binding.rune_header_crc32,
        len(payload),
        _crc32(payload),
        0,
    )
    header = HEADER_STRUCT.pack(*fields)
    return HEADER_STRUCT.pack(*(fields[:-1] + (_header_crc(header),))) + payload


def decode(
    data: bytes | bytearray | memoryview,
    *,
    expected_binding: Binding,
    expected_kind: SidecarKind | str,
    tombstone_indices: Iterable[int] | None = None,
) -> Sidecar:
    """Decode and authenticate one sidecar against a RUNE binding."""

    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise _error(SCD_INVALID_ARGUMENT, "data must be bytes-like")
    binding = _validate_binding(expected_binding)
    wanted = resolve_kind(expected_kind)
    try:
        encoded = memoryview(data).cast("B")
    except (TypeError, ValueError) as exc:
        raise _error(
            SCD_INVALID_ARGUMENT, "data must be a contiguous byte view"
        ) from exc
    if encoded.nbytes < HEADER_BYTES:
        raise _error(SCD_BAD_FILE_SIZE, f"sidecar has {encoded.nbytes} bytes")
    raw_header = bytes(encoded[:HEADER_BYTES])
    fields = HEADER_STRUCT.unpack(raw_header)
    (
        magic,
        reserved_format,
        header_bytes,
        reserved_rune,
        element_bytes,
        planes,
        reserved,
        num_seeds,
        num_links,
        rune_payload_crc32,
        action_contract_crc32,
        rune_header_crc32,
        payload_bytes,
        payload_crc32,
        header_crc32,
    ) = fields
    spec = KIND_BY_MAGIC.get(magic)
    if spec is None or spec != wanted:
        raise _error(SCD_BAD_MAGIC, f"0x{magic:08x}")
    if header_bytes != HEADER_BYTES:
        raise _error(SCD_BAD_HEADER_SIZE, str(header_bytes))
    computed_header_crc = _header_crc(raw_header)
    if header_crc32 != computed_header_crc:
        raise _error(
            SCD_BAD_HEADER_CRC,
            f"stored=0x{header_crc32:08x}, computed=0x{computed_header_crc:08x}",
        )
    if reserved_format != 0 or reserved_rune != 0 or reserved != 0:
        raise _error(SCD_NONZERO_RESERVED)
    if element_bytes != spec.element_bytes or planes != spec.planes:
        raise _error(SCD_BAD_SHAPE, f"{element_bytes}x{planes}")
    if num_seeds != binding.num_seeds or num_links != binding.num_links:
        raise _error(
            SCD_BAD_COUNTS,
            f"counts={num_seeds}/{num_links}, expected "
            f"{binding.num_seeds}/{binding.num_links}",
        )
    expected_payload = expected_payload_bytes(spec, binding)
    if payload_bytes != expected_payload:
        raise _error(
            SCD_BAD_PAYLOAD_SIZE,
            f"stored={payload_bytes}, expected={expected_payload}",
        )
    expected_file = HEADER_BYTES + payload_bytes
    if encoded.nbytes != expected_file:
        raise _error(
            SCD_BAD_FILE_SIZE,
            f"sidecar has {encoded.nbytes} bytes, expected {expected_file}",
        )
    if action_contract_crc32 != binding.action_contract_crc32:
        raise _error(SCD_ACTION_CONTRACT_MISMATCH)
    if rune_payload_crc32 != binding.rune_payload_crc32:
        raise _error(SCD_RUNE_PAYLOAD_MISMATCH)
    if rune_header_crc32 != binding.rune_header_crc32:
        raise _error(SCD_RUNE_HEADER_MISMATCH)
    payload = bytes(encoded[HEADER_BYTES:])
    computed_payload_crc = _crc32(payload)
    if payload_crc32 != computed_payload_crc:
        raise _error(
            SCD_BAD_PAYLOAD_CRC,
            f"stored=0x{payload_crc32:08x}, computed=0x{computed_payload_crc:08x}",
        )
    _validate_payload(spec, binding, payload, tombstone_indices)
    header = Header(
        spec,
        header_bytes,
        element_bytes,
        planes,
        num_seeds,
        num_links,
        rune_payload_crc32,
        action_contract_crc32,
        rune_header_crc32,
        payload_bytes,
        payload_crc32,
        header_crc32,
    )
    return Sidecar(header, payload)


def encode_danger(
    binding: Binding,
    red: Iterable[int],
    blue: Iterable[int],
    *,
    tombstone_indices: Iterable[int] | None,
) -> bytes:
    binding = _validate_binding(binding)
    values: list[int] = []
    for label, plane in (("red", red), ("blue", blue)):
        try:
            iterator = iter(plane)
        except TypeError as exc:
            raise _error(SCD_INVALID_ARGUMENT, f"{label} plane is not iterable") from exc
        plane_values: list[int] = []
        for value in iterator:
            if len(plane_values) == binding.num_seeds:
                raise _error(
                    SCD_BAD_COUNTS,
                    f"{label} danger plane has more than "
                    f"{binding.num_seeds} values",
                )
            plane_values.append(value)
        if len(plane_values) != binding.num_seeds:
            raise _error(
                SCD_BAD_COUNTS,
                f"{label} danger plane has {len(plane_values)} values",
            )
        for value in plane_values:
            if type(value) is not int or not 0 <= value <= DANGER_MAX:
                raise _error(
                    SCD_BAD_PAYLOAD_VALUE,
                    f"{label} danger value {value!r} is outside 0..{DANGER_MAX}",
                )
        values.extend(plane_values)
    payload = struct.pack(f"<{len(values)}I", *values)
    return encode(
        DNG,
        binding,
        payload,
        tombstone_indices=tombstone_indices,
    )


def decode_danger(
    data: bytes | bytearray | memoryview,
    *,
    expected_binding: Binding,
    tombstone_indices: Iterable[int] | None,
) -> tuple[tuple[int, ...], tuple[int, ...]]:
    decoded = decode(
        data,
        expected_kind=DNG,
        expected_binding=expected_binding,
        tombstone_indices=tombstone_indices,
    )
    count = decoded.header.num_seeds
    values = struct.unpack(f"<{count * 2}I", decoded.payload)
    return values[:count], values[count:]
