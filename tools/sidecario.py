#!/usr/bin/env python3
"""Pure explicit-little-endian codecs for RUNE-v3 graph sidecars.

The sidecar never serializes a native C object.  Its 48-byte header binds the
payload to one already-validated RUNE v3 header: both graph counts, the full
encoded RUNE payload CRC, the canonical action-contract CRC, and the RUNE
header CRC (the compact binding to map, BSP/entity identity, and proof law).

The four-character magic fixes the payload axis and shape:

* HMN3/HML3/HME3: one unsigned byte for every ordered link;
* DPO3: four unsigned-byte seed planes (post red/blue, intercept red/blue);
* DNG3: two signed-little-endian i32 seed planes (red/blue danger).
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import struct
from typing import Iterable, Mapping
import zlib

try:
    import rune_contracts_generated as contract
except ModuleNotFoundError:  # Also support ``python -m tools.sidecario``.
    from tools import rune_contracts_generated as contract


HEADER_STRUCT = struct.Struct("<I6H8I")
HEADER_BYTES = 48
FORMAT_VERSION = 1
HEADER_CRC_OFFSET = 44
DANGER_MAX = 8000

assert HEADER_STRUCT.size == HEADER_BYTES


def _fourcc(a: str, b: str, c: str, d: str) -> int:
    raw = bytes((ord(a), ord(b), ord(c), ord(d)))
    return int.from_bytes(raw, "little")


@dataclass(frozen=True)
class SidecarKind:
    name: str
    extension: str
    magic: int
    axis: str
    element_bytes: int
    planes: int


HMN = SidecarKind("HMN", ".hmn", _fourcc("H", "M", "N", "3"),
                  "link", 1, 1)
HML = SidecarKind("HML", ".hml", _fourcc("H", "M", "L", "3"),
                  "link", 1, 1)
HME = SidecarKind("HME", ".hme", _fourcc("H", "M", "E", "3"),
                  "link", 1, 1)
DPO = SidecarKind("DPO", ".dpo", _fourcc("D", "P", "O", "3"),
                  "seed", 1, 4)
DNG = SidecarKind("DNG", ".rune.danger", _fourcc("D", "N", "G", "3"),
                  "seed", 4, 2)

KINDS = (HMN, HML, HME, DPO, DNG)
KIND_BY_MAGIC = {kind.magic: kind for kind in KINDS}
KIND_BY_NAME = {kind.name: kind for kind in KINDS}
KIND_BY_EXTENSION = {kind.extension: kind for kind in KINDS}

assert HMN.magic == 0x334E4D48
assert HML.magic == 0x334C4D48
assert HME.magic == 0x33454D48
assert DPO.magic == 0x334F5044
assert DNG.magic == 0x33474E44

MAX_FILE_BYTES = HEADER_BYTES + max(
    contract.RUNE_V3_MAX_LINKS,
    contract.RUNE_V3_MAX_SEEDS * DNG.element_bytes * DNG.planes,
)


class SidecarDiagnostic(IntEnum):
    SCD_OK = 0
    SCD_ABSENT = 1
    SCD_INVALID_ARGUMENT = 2
    SCD_PATH_TOO_LONG = 3
    SCD_IO_ERROR = 4
    SCD_BAD_MAGIC = 5
    SCD_UNSUPPORTED_VERSION = 6
    SCD_BAD_HEADER_SIZE = 7
    SCD_BAD_RUNE_VERSION = 8
    SCD_BAD_HEADER_CRC = 9
    SCD_NONZERO_RESERVED = 10
    SCD_BAD_SHAPE = 11
    SCD_BAD_COUNTS = 12
    SCD_BAD_PAYLOAD_SIZE = 13
    SCD_BAD_FILE_SIZE = 14
    SCD_RUNE_PAYLOAD_MISMATCH = 15
    SCD_ACTION_CONTRACT_MISMATCH = 16
    SCD_RUNE_HEADER_MISMATCH = 17
    SCD_BAD_PAYLOAD_CRC = 18
    SCD_BAD_PAYLOAD_VALUE = 19
    SCD_ALLOCATION_FAILED = 20
    SCD_TEMP_EXHAUSTED = 21
    SCD_STATE_DRIFT = 22
    SCD_INTERNAL_ERROR = 23

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
    "unsupported sidecar format version",
    "bad sidecar header size",
    "bad bound RUNE version",
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
SCD_UNSUPPORTED_VERSION = SidecarDiagnostic.SCD_UNSUPPORTED_VERSION
SCD_BAD_HEADER_SIZE = SidecarDiagnostic.SCD_BAD_HEADER_SIZE
SCD_BAD_RUNE_VERSION = SidecarDiagnostic.SCD_BAD_RUNE_VERSION
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

    def __init__(self, diagnostic: SidecarDiagnostic | int, detail: str):
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


# Transitional import compatibility for callers that used the initial name.
SidecarWireError = SidecarError


@dataclass(frozen=True)
class RuneBindingV3:
    rune_version: int
    num_seeds: int
    num_links: int
    rune_payload_crc32: int
    action_contract_crc32: int
    rune_header_crc32: int


@dataclass(frozen=True)
class SidecarHeaderV3:
    kind: SidecarKind
    format_version: int
    header_bytes: int
    rune_version: int
    element_bytes: int
    planes: int
    reserved: int
    num_seeds: int
    num_links: int
    rune_payload_crc32: int
    action_contract_crc32: int
    rune_header_crc32: int
    payload_bytes: int
    payload_crc32: int
    header_crc32: int

    @property
    def binding(self) -> RuneBindingV3:
        return RuneBindingV3(
            self.rune_version,
            self.num_seeds,
            self.num_links,
            self.rune_payload_crc32,
            self.action_contract_crc32,
            self.rune_header_crc32,
        )


@dataclass(frozen=True)
class SidecarV3:
    header: SidecarHeaderV3
    payload: bytes


def _error(diagnostic: SidecarDiagnostic, detail: str) -> SidecarError:
    return SidecarError(diagnostic, detail)


def _uint(value: object, bits: int, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise _error(SCD_INVALID_ARGUMENT, f"{label} must be an integer")
    if value < 0 or value >= 1 << bits:
        raise _error(SCD_INVALID_ARGUMENT, f"{label} is outside u{bits}")
    return value


def resolve_kind(kind: SidecarKind | str) -> SidecarKind:
    if isinstance(kind, SidecarKind):
        if KIND_BY_MAGIC.get(kind.magic) != kind:
            raise _error(SCD_INVALID_ARGUMENT, "unknown sidecar kind")
        return kind
    if not isinstance(kind, str):
        raise _error(SCD_INVALID_ARGUMENT, "sidecar kind must be a name")
    key = kind.upper()
    if key in KIND_BY_NAME:
        return KIND_BY_NAME[key]
    if kind in KIND_BY_EXTENSION:
        return KIND_BY_EXTENSION[kind]
    raise _error(SCD_INVALID_ARGUMENT, f"unknown sidecar kind {kind!r}")


def binding_from_rune(rune: Mapping[str, object]) -> RuneBindingV3:
    """Extract the exact v3 binding from ``corpusgraph.read_rune`` metadata."""
    if not isinstance(rune, Mapping):
        raise _error(SCD_INVALID_ARGUMENT, "rune metadata must be a mapping")
    try:
        binding = RuneBindingV3(
            rune_version=_uint(rune["version"], 16, "rune version"),
            num_seeds=_uint(rune["num_seeds"], 32, "seed count"),
            num_links=_uint(rune["num_links"], 32, "link count"),
            rune_payload_crc32=_uint(
                rune["payload_crc32"], 32, "rune payload CRC"
            ),
            action_contract_crc32=_uint(
                rune["action_contract_crc32"], 32, "action-contract CRC"
            ),
            rune_header_crc32=_uint(
                rune["header_crc32"], 32, "rune header CRC"
            ),
        )
    except KeyError as exc:
        raise _error(
            SCD_INVALID_ARGUMENT, f"rune metadata lacks {exc.args[0]}"
        ) from exc
    _validate_binding(binding)
    return binding


def _validate_binding(binding: object, *,
                      require_action_contract: bool = True) -> RuneBindingV3:
    if not isinstance(binding, RuneBindingV3):
        raise _error(SCD_INVALID_ARGUMENT, "expected RuneBindingV3")
    for value, bits, label in (
        (binding.rune_version, 16, "rune version"),
        (binding.num_seeds, 32, "seed count"),
        (binding.num_links, 32, "link count"),
        (binding.rune_payload_crc32, 32, "rune payload CRC"),
        (binding.action_contract_crc32, 32, "action-contract CRC"),
        (binding.rune_header_crc32, 32, "rune header CRC"),
    ):
        _uint(value, bits, label)
    if binding.rune_version != contract.RUNE_V3_VERSION:
        raise _error(
            SCD_BAD_RUNE_VERSION,
            f"sidecars require RUNE v{contract.RUNE_V3_VERSION}",
        )
    if not 0 < binding.num_seeds <= contract.RUNE_V3_MAX_SEEDS:
        raise _error(SCD_BAD_COUNTS, f"invalid seed count {binding.num_seeds}")
    if not 0 <= binding.num_links <= contract.RUNE_V3_MAX_LINKS:
        raise _error(SCD_BAD_COUNTS, f"invalid link count {binding.num_links}")
    if (require_action_contract and
            binding.action_contract_crc32 != contract.CONTRACT_CRC32):
        raise _error(
            SCD_ACTION_CONTRACT_MISMATCH,
            f"0x{binding.action_contract_crc32:08x}",
        )
    return binding


def expected_payload_bytes(kind: SidecarKind | str,
                           binding: RuneBindingV3) -> int:
    spec = resolve_kind(kind)
    _validate_binding(binding)
    count = binding.num_links if spec.axis == "link" else binding.num_seeds
    return count * spec.element_bytes * spec.planes


def _crc32(data: bytes | bytearray | memoryview) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _header_crc(data: bytes | bytearray | memoryview) -> int:
    if len(data) != HEADER_BYTES:
        raise _error(SCD_BAD_HEADER_SIZE, str(len(data)))
    canonical = bytearray(data)
    canonical[HEADER_CRC_OFFSET:HEADER_CRC_OFFSET + 4] = b"\0" * 4
    return _crc32(canonical)


def _tombstones(indices: Iterable[int] | None,
                num_seeds: int) -> tuple[int, ...]:
    if indices is None:
        return ()
    out = set()
    try:
        iterator = iter(indices)
    except TypeError as exc:
        raise _error(SCD_INVALID_ARGUMENT, "tombstones must be iterable") from exc
    for index in iterator:
        if isinstance(index, bool) or not isinstance(index, int):
            raise _error(SCD_INVALID_ARGUMENT, "tombstone index is not an integer")
        if not 0 <= index < num_seeds:
            raise _error(SCD_BAD_PAYLOAD_VALUE, f"tombstone index {index}")
        if index in out:
            raise _error(
                SCD_INVALID_ARGUMENT, f"duplicate tombstone index {index}"
            )
        out.add(index)
    return tuple(sorted(out))


def _validate_payload(kind: SidecarKind, binding: RuneBindingV3,
                      payload: bytes,
                      tombstone_indices: Iterable[int] | None) -> None:
    expected = expected_payload_bytes(kind, binding)
    if len(payload) != expected:
        raise _error(
            SCD_BAD_PAYLOAD_SIZE,
            f"{kind.name} payload has {len(payload)} bytes, expected {expected}",
        )
    if kind.axis == "seed" and tombstone_indices is None:
        raise _error(
            SCD_INVALID_ARGUMENT,
            f"{kind.name} requires the bound rune's tombstone indices",
        )
    if kind == DPO:
        tombstones = _tombstones(tombstone_indices, binding.num_seeds)
        for plane in range(kind.planes):
            offset = plane * binding.num_seeds
            for index in tombstones:
                if payload[offset + index] != 0:
                    raise _error(
                        SCD_BAD_PAYLOAD_VALUE,
                        f"DPO plane {plane} assigns tombstone seed {index}",
                    )
    elif kind == DNG:
        tombstones = _tombstones(tombstone_indices, binding.num_seeds)
        for index in range(binding.num_seeds * kind.planes):
            value = struct.unpack_from("<i", payload, index * 4)[0]
            if not 0 <= value <= DANGER_MAX:
                raise _error(
                    SCD_BAD_PAYLOAD_VALUE,
                    f"DNG value {index} is outside 0..{DANGER_MAX}",
                )
        for plane in range(kind.planes):
            offset = plane * binding.num_seeds
            for index in tombstones:
                value = struct.unpack_from("<i", payload,
                                           (offset + index) * 4)[0]
                if value != 0:
                    raise _error(
                        SCD_BAD_PAYLOAD_VALUE,
                        f"DNG plane {plane} assigns tombstone seed {index}",
                    )


def encode_v3(kind: SidecarKind | str, binding: RuneBindingV3,
              payload: bytes | bytearray | memoryview, *,
              tombstone_indices: Iterable[int] | None = None) -> bytes:
    spec = resolve_kind(kind)
    _validate_binding(binding)
    if not isinstance(payload, (bytes, bytearray, memoryview)):
        raise _error(SCD_INVALID_ARGUMENT, "payload must be bytes-like")
    payload_size = (payload.nbytes if isinstance(payload, memoryview)
                    else len(payload))
    if payload_size > MAX_FILE_BYTES - HEADER_BYTES:
        raise _error(
            SCD_BAD_PAYLOAD_SIZE,
            f"payload has {payload_size} bytes; limit is "
            f"{MAX_FILE_BYTES - HEADER_BYTES}",
        )
    payload = bytes(payload)
    _validate_payload(spec, binding, payload, tombstone_indices)
    payload_crc = _crc32(payload)
    fields = (
        spec.magic,
        FORMAT_VERSION,
        HEADER_BYTES,
        binding.rune_version,
        spec.element_bytes,
        spec.planes,
        0,
        binding.num_seeds,
        binding.num_links,
        binding.rune_payload_crc32,
        binding.action_contract_crc32,
        binding.rune_header_crc32,
        len(payload),
        payload_crc,
        0,
    )
    header = HEADER_STRUCT.pack(*fields)
    fields = fields[:-1] + (_header_crc(header),)
    return HEADER_STRUCT.pack(*fields) + payload


def decode_v3(data: bytes | bytearray | memoryview, *,
              expected_binding: RuneBindingV3 | None = None,
              expected_kind: SidecarKind | str | None = None,
              tombstone_indices: Iterable[int] | None = None) -> SidecarV3:
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise _error(SCD_INVALID_ARGUMENT, "data must be bytes-like")
    expected_binding = _validate_binding(expected_binding)
    wanted_spec = resolve_kind(expected_kind)
    try:
        encoded = memoryview(data).cast("B")
    except (TypeError, ValueError) as exc:
        raise _error(
            SCD_INVALID_ARGUMENT, "data must be a contiguous byte view"
        ) from exc
    input_size = encoded.nbytes
    if input_size < HEADER_BYTES:
        raise _error(
            SCD_BAD_HEADER_SIZE,
            f"truncated sidecar header: {input_size} bytes",
        )
    encoded_header = bytes(encoded[:HEADER_BYTES])
    fields = HEADER_STRUCT.unpack(encoded_header)
    (magic, format_version, header_bytes, rune_version, element_bytes,
     planes, reserved, num_seeds, num_links, rune_payload_crc32,
     action_contract_crc32, rune_header_crc32, payload_bytes, payload_crc32,
     header_crc32) = fields
    spec = KIND_BY_MAGIC.get(magic)
    if spec is None:
        raise _error(SCD_BAD_MAGIC, f"0x{magic:08x}")
    if spec != wanted_spec:
        raise _error(
            SCD_BAD_MAGIC,
            f"got {spec.name}, expected {wanted_spec.name}",
        )
    if format_version != FORMAT_VERSION:
        raise _error(SCD_UNSUPPORTED_VERSION, str(format_version))
    if header_bytes != HEADER_BYTES:
        raise _error(SCD_BAD_HEADER_SIZE, str(header_bytes))
    if rune_version != contract.RUNE_V3_VERSION:
        raise _error(
            SCD_BAD_RUNE_VERSION,
            f"stored={rune_version}, expected={contract.RUNE_V3_VERSION}",
        )
    computed_header_crc = _header_crc(encoded_header)
    if header_crc32 != computed_header_crc:
        raise _error(
            SCD_BAD_HEADER_CRC,
            f"stored=0x{header_crc32:08x}, computed=0x{computed_header_crc:08x}",
        )
    if reserved != 0:
        raise _error(SCD_NONZERO_RESERVED, str(reserved))
    if element_bytes != spec.element_bytes or planes != spec.planes:
        raise _error(
            SCD_BAD_SHAPE,
            f"{spec.name} shape {element_bytes}x{planes}",
        )
    binding = RuneBindingV3(
        rune_version,
        num_seeds,
        num_links,
        rune_payload_crc32,
        action_contract_crc32,
        rune_header_crc32,
    )
    _validate_binding(binding, require_action_contract=False)
    if (binding.num_seeds != expected_binding.num_seeds or
            binding.num_links != expected_binding.num_links):
        raise _error(
            SCD_BAD_COUNTS,
            f"counts={binding.num_seeds}/{binding.num_links}, expected "
            f"{expected_binding.num_seeds}/{expected_binding.num_links}",
        )
    count = binding.num_links if spec.axis == "link" else binding.num_seeds
    expected_payload = count * spec.element_bytes * spec.planes
    if payload_bytes != expected_payload:
        raise _error(
            SCD_BAD_PAYLOAD_SIZE,
            f"stored payload size {payload_bytes}, expected {expected_payload}",
        )
    expected_file = HEADER_BYTES + payload_bytes
    if input_size != expected_file:
        raise _error(
            SCD_BAD_FILE_SIZE,
            f"sidecar has {input_size} bytes, expected {expected_file}",
        )
    expected_action_crc = expected_binding.action_contract_crc32
    if action_contract_crc32 != expected_action_crc:
        raise _error(
            SCD_ACTION_CONTRACT_MISMATCH,
            f"stored=0x{action_contract_crc32:08x}, "
            f"expected=0x{expected_action_crc:08x}",
        )
    if rune_payload_crc32 != expected_binding.rune_payload_crc32:
        raise _error(
            SCD_RUNE_PAYLOAD_MISMATCH,
            f"stored=0x{rune_payload_crc32:08x}, "
            f"expected=0x{expected_binding.rune_payload_crc32:08x}",
        )
    if rune_header_crc32 != expected_binding.rune_header_crc32:
        raise _error(
            SCD_RUNE_HEADER_MISMATCH,
            f"stored=0x{rune_header_crc32:08x}, "
            f"expected=0x{expected_binding.rune_header_crc32:08x}",
        )
    payload = bytes(encoded[HEADER_BYTES:])
    computed_payload_crc = _crc32(payload)
    if payload_crc32 != computed_payload_crc:
        raise _error(
            SCD_BAD_PAYLOAD_CRC,
            f"stored=0x{payload_crc32:08x}, computed=0x{computed_payload_crc:08x}",
        )
    _validate_payload(spec, binding, payload, tombstone_indices)
    header = SidecarHeaderV3(
        spec, format_version, header_bytes, rune_version, element_bytes,
        planes, reserved, num_seeds, num_links, rune_payload_crc32,
        action_contract_crc32, rune_header_crc32, payload_bytes,
        payload_crc32, header_crc32,
    )
    return SidecarV3(header, payload)


def encode_danger(binding: RuneBindingV3, red: Iterable[int],
                  blue: Iterable[int], *,
                  tombstone_indices: Iterable[int] | None = None) -> bytes:
    _validate_binding(binding)
    values = []
    for label, plane in (("red", red), ("blue", blue)):
        try:
            iterator = iter(plane)
        except TypeError as exc:
            raise _error(SCD_INVALID_ARGUMENT, f"{label} plane is not iterable") from exc
        plane_values = []
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
            if (isinstance(value, bool) or not isinstance(value, int) or
                    not 0 <= value <= DANGER_MAX):
                raise _error(
                    SCD_BAD_PAYLOAD_VALUE,
                    f"{label} danger value {value!r} is outside 0..{DANGER_MAX}",
                )
        values.extend(plane_values)
    payload = bytearray(len(values) * 4)
    for index, value in enumerate(values):
        struct.pack_into("<i", payload, index * 4, value)
    return encode_v3(
        DNG, binding, payload, tombstone_indices=tombstone_indices
    )


def decode_danger(data: bytes | bytearray | memoryview, *,
                  expected_binding: RuneBindingV3 | None = None,
                  tombstone_indices: Iterable[int] | None = None
                  ) -> tuple[tuple[int, ...], tuple[int, ...]]:
    decoded = decode_v3(
        data, expected_kind=DNG, expected_binding=expected_binding,
        tombstone_indices=tombstone_indices,
    )
    count = decoded.header.num_seeds
    values = tuple(
        struct.unpack_from("<i", decoded.payload, index * 4)[0]
        for index in range(count * 2)
    )
    return values[:count], values[count:]
