#!/usr/bin/env python3
"""Golden-vector and fail-closed tests for the isolated RUNE v3 codec."""

from __future__ import annotations

from dataclasses import FrozenInstanceError, replace
import hashlib
import itertools
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import rune_contracts_generated as contract  # noqa: E402
import runeio  # noqa: E402


HEADER_BYTES = contract.RUNE_V3_HEADER_BYTES
SEED_BYTES = contract.RUNE_V3_SEED_BYTES
LINK_BYTES = contract.RUNE_V3_LINK_BYTES
HEADER_CRC_OFFSET = contract.RUNE_V3_HEADER_CRC_OFFSET
PAYLOAD_CRC_OFFSET = 20
MAP_OFFSET = 64
SEED0_OFFSET = HEADER_BYTES
SEED1_OFFSET = SEED0_OFFSET + SEED_BYTES
LINK0_OFFSET = HEADER_BYTES + 2 * SEED_BYTES
LINK1_OFFSET = LINK0_OFFSET + LINK_BYTES


def _golden_bytes():
    text = (
        ROOT / "tests" / "fixtures" / "rune_v3_wire_golden.hex"
    ).read_text(encoding="ascii")
    return bytes.fromhex(text)


def _fix_header_crc(data):
    struct.pack_into("<I", data, HEADER_CRC_OFFSET, 0)
    crc = zlib.crc32(data[:HEADER_BYTES]) & 0xFFFFFFFF
    struct.pack_into("<I", data, HEADER_CRC_OFFSET, crc)


def _fix_payload_crc(data):
    crc = zlib.crc32(data[HEADER_BYTES:]) & 0xFFFFFFFF
    struct.pack_into("<I", data, PAYLOAD_CRC_OFFSET, crc)
    _fix_header_crc(data)


class RuneIoV3Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.golden = _golden_bytes()
        cls.identity = runeio.RuneIdentityV3(
            map_name="lmctf07",
            bsp_checksum=0x12345678,
            entity_crc32=0x9ABCDEF0,
            gravity=650.0,
            airaccelerate=0.0,
            maxvelocity=2000.0,
            host_physics_id=1,
        )
        cls.seeds = (
            runeio.RuneSeedV3((0.0, 0.0, 0.0), 7, 0),
            runeio.RuneSeedV3((128.0, 0.0, 0.0), 255, 0),
        )
        cls.links = (
            runeio.RuneLinkV3(
                0, 1, contract.RL_RUN, contract.RL_PROVEN,
                4, 64, 8, 50, 125, (64.0, 16.0, 0.0),
            ),
            runeio.RuneLinkV3(
                1, 0, contract.RL_DOOR_DROP, contract.RL_CONTRACTED,
                0, 128, 254, 20, 500,
                (120.0, 0.0, 8.0), (112.0, 0.0, 0.0),
                100, contract.RLCM_PREOPEN,
            ),
        )

    def assert_wire_code(self, code, operation):
        with self.assertRaises(runeio.RuneWireError) as caught:
            operation()
        error = caught.exception
        self.assertEqual(code, error.code)
        self.assertEqual(contract.WIRE_DIAGNOSTIC_SYMBOLS[code], error.symbol)
        self.assertEqual(contract.WIRE_DIAGNOSTIC_MESSAGES[code], error.message)
        self.assertIn(error.symbol, str(error))
        return error

    def mutate_header(self, offset, fmt, value):
        data = bytearray(self.golden)
        struct.pack_into(fmt, data, offset, value)
        _fix_header_crc(data)
        return bytes(data)

    def mutate_payload(self, offset, fmt, value):
        data = bytearray(self.golden)
        struct.pack_into(fmt, data, offset, value)
        _fix_payload_crc(data)
        return bytes(data)

    def test_golden_header_seed_link_vectors_and_round_trip(self):
        self.assertEqual(128, runeio.HEADER_STRUCT.size)
        self.assertEqual(16, runeio.SEED_STRUCT.size)
        self.assertEqual(44, runeio.LINK_STRUCT.size)
        self.assertEqual(248, len(self.golden))
        self.assertEqual(
            "33eb936509585d92c0799b491cea97575758785455b99a85ff159bf59813d7a5",
            hashlib.sha256(self.golden).hexdigest(),
        )
        self.assertEqual(
            "00000000000000000000000007000000",
            self.golden[SEED0_OFFSET:SEED1_OFFSET].hex(),
        )
        self.assertEqual(
            "000000430000000000000000ff000000",
            self.golden[SEED1_OFFSET:LINK0_OFFSET].hex(),
        )
        self.assertEqual(
            "00000000010000000000044008327d0000008042000080410000000000000000"
            "000000000000000000000000",
            self.golden[LINK0_OFFSET:LINK1_OFFSET].hex(),
        )
        self.assertEqual(
            "010000000000000009040080fe14f4010000f04200000000000000410000e042"
            "000000000000000064000100",
            self.golden[LINK1_OFFSET:].hex(),
        )

        encoded = runeio.encode_v3(self.identity, self.seeds, self.links)
        self.assertEqual(self.golden, encoded)
        decoded = runeio.decode_v3(
            self.golden, expected_identity=self.identity
        )
        self.assertEqual(self.seeds, decoded.seeds)
        self.assertEqual(self.links, decoded.links)
        self.assertEqual(self.golden[HEADER_BYTES:], decoded.payload)
        self.assertEqual(0xE3D0AC5F, decoded.payload_crc32)
        self.assertEqual(contract.CONTRACT_CRC32, decoded.header.action_contract_crc32)
        self.assertEqual(self.golden, runeio.encode_v3(
            decoded.identity, decoded.seeds, decoded.links
        ))
        with self.assertRaises(FrozenInstanceError):
            decoded.header.map_name = "changed"

    def test_v3_prefix_detection_survives_independent_field_corruption(self):
        self.assertFalse(runeio.looks_like_v3_prefix(b""))
        self.assertFalse(runeio.looks_like_v3_prefix(self.golden[:5]))
        self.assertFalse(runeio.looks_like_v3_prefix(self.golden[:6]))
        self.assertTrue(runeio.looks_like_v3_prefix(self.golden[:8]))

        bad_version = bytearray(self.golden[:8])
        struct.pack_into("<H", bad_version, 4, 2)
        self.assertTrue(runeio.looks_like_v3_prefix(bad_version))

        bad_size = bytearray(self.golden[:12])
        struct.pack_into("<H", bad_size, 6, 0)
        self.assertTrue(runeio.looks_like_v3_prefix(bad_size))

        bad_version_and_size = bytearray(bad_size)
        struct.pack_into("<H", bad_version_and_size, 4, 2)
        self.assertTrue(runeio.looks_like_v3_prefix(bad_version_and_size))

        bad_header_and_seed_size = bytearray(bad_size)
        struct.pack_into("<H", bad_header_and_seed_size, 8, 15)
        self.assertTrue(
            runeio.looks_like_v3_prefix(bad_header_and_seed_size)
        )

        legacy_v3 = struct.pack("<4i", contract.RUNE_V3_MAGIC, 3, 2, 0)
        self.assertFalse(runeio.looks_like_v3_prefix(legacy_v3))

    def test_wire_error_exposes_generated_diagnostic(self):
        error = runeio.RuneWireError(contract.RLW_BAD_MAGIC, "probe")
        self.assertEqual(contract.RLW_BAD_MAGIC, error.code)
        self.assertEqual("RLW_BAD_MAGIC", error.symbol)
        self.assertEqual("bad magic", error.message)
        self.assertEqual("probe", error.detail)
        self.assertEqual("RLW_BAD_MAGIC: bad magic: probe", str(error))

    def test_structural_decode_does_not_claim_active_identity(self):
        decoded = runeio.decode_v3(self.golden)
        self.assertEqual(self.identity, decoded.identity)
        mismatches = (
            (replace(self.identity, map_name="LMCTF07"), contract.RLW_MAPNAME_MISMATCH),
            (replace(self.identity, bsp_checksum=1), contract.RLW_BSP_CHECKSUM_MISMATCH),
            (replace(self.identity, entity_crc32=1), contract.RLW_ENTITY_CRC_MISMATCH),
            (replace(self.identity, host_physics_id=2), contract.RLW_PHYSICS_ID_MISMATCH),
            (replace(self.identity, gravity=800.0), contract.RLW_BAD_PHYSICS_LAW),
            (replace(self.identity, maxvelocity=2100.0), contract.RLW_BAD_PHYSICS_LAW),
        )
        for expected, code in mismatches:
            with self.subTest(code=code, expected=expected):
                self.assert_wire_code(
                    code,
                    lambda expected=expected: runeio.decode_v3(
                        self.golden, expected_identity=expected
                    ),
                )

    def test_header_magic_version_and_record_sizes_reject(self):
        mutations = (
            (contract.RLW_BAD_MAGIC, 0, "<I", 0),
            (contract.RLW_UNSUPPORTED_VERSION, 4, "<H", 2),
            (contract.RLW_BAD_HEADER_SIZE, 6, "<H", 127),
            (contract.RLW_BAD_SEED_SIZE, 8, "<H", 15),
            (contract.RLW_BAD_LINK_SIZE, 10, "<H", 43),
        )
        for code, offset, fmt, value in mutations:
            with self.subTest(code=code):
                data = self.mutate_header(offset, fmt, value)
                self.assert_wire_code(code, lambda data=data: runeio.decode_v3(data))

    def test_count_overflow_truncation_and_trailing_bytes_reject(self):
        count_bitflip = bytearray(self.golden)
        count_bitflip[12] ^= 1
        self.assert_wire_code(
            contract.RLW_BAD_HEADER_CRC,
            lambda: runeio.decode_v3(count_bitflip),
        )
        for offset, value in (
            (12, 0),
            (12, contract.RUNE_V3_MAX_SEEDS + 1),
            (16, contract.RUNE_V3_MAX_LINKS + 1),
        ):
            with self.subTest(offset=offset, value=value):
                data = self.mutate_header(offset, "<I", value)
                self.assert_wire_code(
                    contract.RLW_BAD_COUNTS,
                    lambda data=data: runeio.decode_v3(data),
                )
        for data in (self.golden[:127], self.golden[:-1], self.golden + b"\x00"):
            with self.subTest(length=len(data)):
                self.assert_wire_code(
                    contract.RLW_BAD_FILE_SIZE,
                    lambda data=data: runeio.decode_v3(data),
                )

    def test_header_and_exact_encoded_payload_crc_reject(self):
        bad_header = bytearray(self.golden)
        bad_header[24] ^= 1
        self.assert_wire_code(
            contract.RLW_BAD_HEADER_CRC,
            lambda: runeio.decode_v3(bad_header),
        )
        bad_payload = bytearray(self.golden)
        bad_payload[SEED0_OFFSET] ^= 1
        self.assert_wire_code(
            contract.RLW_BAD_PAYLOAD_CRC,
            lambda: runeio.decode_v3(bad_payload),
        )

    def test_every_single_bit_corruption_rejects(self):
        for offset in range(len(self.golden)):
            for bit in range(8):
                data = bytearray(self.golden)
                data[offset] ^= 1 << bit
                with self.subTest(offset=offset, bit=bit):
                    with self.assertRaises(runeio.RuneWireError):
                        runeio.decode_v3(data)

    def test_map_name_encoding_nul_tail_and_exact_case_reject(self):
        variants = []
        for raw in (
            b"a" * 64,
            b"lmctf07\x00x" + b"\x00" * 55,
            b"bad/name\x00" + b"\x00" * 55,
            b"\xffmap\x00" + b"\x00" * 59,
            b"\x00" * 64,
        ):
            data = bytearray(self.golden)
            data[MAP_OFFSET:MAP_OFFSET + 64] = raw
            _fix_header_crc(data)
            variants.append(bytes(data))
        for data in variants:
            with self.subTest(raw=data[MAP_OFFSET:MAP_OFFSET + 64]):
                self.assert_wire_code(
                    contract.RLW_BAD_MAPNAME,
                    lambda data=data: runeio.decode_v3(data),
                )
        for name in ("", "bad/name", "a" * 64, "nul\x00name", "-leading"):
            with self.subTest(name=name):
                self.assert_wire_code(
                    contract.RLW_BAD_MAPNAME,
                    lambda name=name: runeio.encode_v3(
                        replace(self.identity, map_name=name), self.seeds, self.links
                    ),
                )

    def test_action_contract_and_physics_header_laws_reject(self):
        contract_crc = self.mutate_header(32, "<I", contract.CONTRACT_CRC32 ^ 1)
        self.assert_wire_code(
            contract.RLW_BAD_ACTION_CONTRACT,
            lambda: runeio.decode_v3(contract_crc),
        )
        physics_mutations = (
            (36, "<I", 1),
            (40, "<f", float("nan")),
            (40, "<f", 650.5),
            (40, "<f", 0.0),
            (44, "<f", float("inf")),
            (44, "<f", 1.0),
            (48, "<f", 799.0),
            (52, "<H", 20),
            (54, "<H", 50),
        )
        for offset, fmt, value in physics_mutations:
            with self.subTest(offset=offset, value=value):
                data = self.mutate_header(offset, fmt, value)
                self.assert_wire_code(
                    contract.RLW_BAD_PHYSICS_LAW,
                    lambda data=data: runeio.decode_v3(data),
                )
        no_host_identity = self.mutate_header(56, "<I", 0)
        self.assert_wire_code(
            contract.RLW_IDENTITY_UNAVAILABLE,
            lambda: runeio.decode_v3(no_host_identity),
        )
        for changes in (
            {"pmove_substep_ms": 25.0},
            {"server_frame_ms": 100.0},
            {"pmove_substep_ms": True},
            {"server_frame_ms": False},
        ):
            with self.subTest(changes=changes):
                identity = replace(self.identity, **changes)
                self.assert_wire_code(
                    contract.RLW_BAD_PHYSICS_LAW,
                    lambda identity=identity: runeio.encode_v3(
                        identity, self.seeds, self.links
                    ),
                )

    def test_seed_record_nan_inf_bounds_area_and_flags_reject(self):
        mutations = (
            (SEED0_OFFSET, "<f", float("nan")),
            (SEED0_OFFSET + 4, "<f", float("inf")),
            (SEED0_OFFSET + 8, "<f", 4096.0),
            (SEED0_OFFSET + 12, "<h", -1),
            (SEED0_OFFSET + 14, "<h", 4),
        )
        for offset, fmt, value in mutations:
            with self.subTest(offset=offset, value=value):
                data = self.mutate_payload(offset, fmt, value)
                self.assert_wire_code(
                    contract.RLW_BAD_SEED_RECORD,
                    lambda data=data: runeio.decode_v3(data),
                )

    def test_link_indices_self_ids_cost_floats_and_reserved_reject(self):
        mutations = (
            (LINK0_OFFSET + 4, "<I", 0),
            (LINK0_OFFSET, "<I", 2),
            (LINK0_OFFSET + 8, "<B", contract.RL_DOOR_HOOK + 1),
            (LINK0_OFFSET + 8, "<B", 255),
            (LINK0_OFFSET + 9, "<B", contract.RL_CONTRACTED + 1),
            (LINK0_OFFSET + 9, "<B", contract.RL_CONTRACTED),
            (LINK0_OFFSET + 14, "<h", 0),
            (LINK0_OFFSET + 16, "<f", float("nan")),
            (LINK0_OFFSET + 28, "<f", 1.0),
            (LINK0_OFFSET + 42, "<B", contract.RLCM_PREOPEN),
            (LINK0_OFFSET + 42, "<B", contract.RLCM_RIDE + 1),
            (LINK0_OFFSET + 42, "<B", 255),
            (LINK0_OFFSET + 43, "<B", 1),
        )
        for offset, fmt, value in mutations:
            with self.subTest(offset=offset, value=value):
                data = self.mutate_payload(offset, fmt, value)
                self.assert_wire_code(
                    contract.RLW_BAD_LINK_RECORD,
                    lambda data=data: runeio.decode_v3(data),
                )

    def test_endpoint_compound_sweep_and_mechanism_lattice_reject(self):
        mutations = []
        water_run = bytearray(self.golden)
        struct.pack_into("<h", water_run, SEED0_OFFSET + 14, runeio.RSF_WATER)
        _fix_payload_crc(water_run)
        mutations.append(water_run)
        for offset, fmt, value in (
            (LINK1_OFFSET + 40, "<H", 0),
            (LINK1_OFFSET + 40, "<H", 150),
            (LINK1_OFFSET + 40, "<H", 600),
            (LINK1_OFFSET + 28, "<f", float("nan")),
            (LINK1_OFFSET + 28, "<f", 112.1),
            (LINK1_OFFSET + 28, "<f", 4096.0),
            (LINK1_OFFSET + 42, "<B", contract.RLCM_NONE),
        ):
            mutations.append(bytearray(self.mutate_payload(offset, fmt, value)))
        for data in mutations:
            with self.subTest(link=data[LINK1_OFFSET:]):
                self.assert_wire_code(
                    contract.RLW_BAD_LINK_RECORD,
                    lambda data=bytes(data): runeio.decode_v3(data),
                )

    def test_duplicate_identity_is_source_destination_action(self):
        data = bytearray(self.golden)
        struct.pack_into("<I", data, LINK1_OFFSET, 0)
        struct.pack_into("<I", data, LINK1_OFFSET + 4, 1)
        struct.pack_into("<B", data, LINK1_OFFSET + 8, contract.RL_RUN)
        _fix_payload_crc(data)
        self.assert_wire_code(
            contract.RLW_DUPLICATE_LINK,
            lambda: runeio.decode_v3(data),
        )

    def test_tombstone_endpoints_and_live_outgoing_identity_reject(self):
        tombstone_endpoint = bytearray(self.golden)
        struct.pack_into(
            "<h", tombstone_endpoint, SEED0_OFFSET + 14, runeio.RSF_TOMBSTONE
        )
        _fix_payload_crc(tombstone_endpoint)
        self.assert_wire_code(
            contract.RLW_BAD_ROUTE_OWNERSHIP,
            lambda: runeio.decode_v3(tombstone_endpoint),
        )

        no_seed_zero_source = bytearray(self.golden)
        struct.pack_into("<I", no_seed_zero_source, LINK0_OFFSET, 1)
        struct.pack_into("<I", no_seed_zero_source, LINK0_OFFSET + 4, 0)
        _fix_payload_crc(no_seed_zero_source)
        self.assert_wire_code(
            contract.RLW_BAD_ROUTE_OWNERSHIP,
            lambda: runeio.decode_v3(no_seed_zero_source),
        )

        lone_tombstone = (runeio.RuneSeedV3(
            (0.0, 0.0, 0.0), 0, runeio.RSF_TOMBSTONE
        ),)
        encoded = runeio.encode_v3(self.identity, lone_tombstone, ())
        self.assertEqual(lone_tombstone, runeio.decode_v3(encoded).seeds)
        self.assert_wire_code(
            contract.RLW_BAD_ROUTE_OWNERSHIP,
            lambda: runeio.encode_v3(
                self.identity, (replace(lone_tombstone[0], flags=0),), ()
            ),
        )

    def test_runtime_unsupported_registry_actions_are_wire_known_only(self):
        dry_seeds = (
            runeio.RuneSeedV3((0.0, 0.0, 0.0)),
            runeio.RuneSeedV3((64.0, 0.0, 0.0)),
        )
        rj_links = tuple(
            runeio.RuneLinkV3(
                source, destination,
                contract.RL_ROCKETJUMP, contract.RL_PROVEN,
                0, 0, 0, 0, 100,
            )
            for source, destination in ((0, 1), (1, 0))
        )
        decoded = runeio.decode_v3(
            runeio.encode_v3(self.identity, dry_seeds, rj_links)
        )
        self.assertTrue(all(link.action == contract.RL_ROCKETJUMP
                            for link in decoded.links))
        self.assertFalse(contract.is_runtime_supported(contract.RL_ROCKETJUMP))

        for action, suffix_anchor in (
            (contract.RL_DOOR_SWIM, (0.0, 0.0, 0.0)),
            (contract.RL_DOOR_HOOK, (0.0, 0.0, 100.0)),
        ):
            with self.subTest(action=action):
                seeds = (
                    runeio.RuneSeedV3(
                        (0.0, 0.0, 0.0), 0, runeio.RSF_WATER
                    ),
                    runeio.RuneSeedV3((64.0, 0.0, 0.0)),
                )
                links = (
                    runeio.RuneLinkV3(
                        0, 1, action, contract.RL_CONTRACTED,
                        0, 0, 0, 0, 200, suffix_anchor,
                        (32.0, 0.0, 0.0), 100, contract.RLCM_PREOPEN,
                    ),
                    runeio.RuneLinkV3(
                        1, 0, contract.RL_SWIM, contract.RL_PROVEN,
                        0, 0, 0, 0, 100,
                    ),
                )
                encoded = runeio.encode_v3(self.identity, seeds, links)
                self.assertEqual(action, runeio.decode_v3(encoded).links[0].action)
                self.assertFalse(contract.is_runtime_supported(action))

    def test_encode_argument_count_and_negative_zero_tail_reject(self):
        self.assert_wire_code(
            contract.RLW_INVALID_ARGUMENT,
            lambda: runeio.encode_v3(None, self.seeds, self.links),
        )
        self.assert_wire_code(
            contract.RLW_BAD_COUNTS,
            lambda: runeio.encode_v3(self.identity, (), ()),
        )
        self.assert_wire_code(
            contract.RLW_BAD_COUNTS,
            lambda: runeio.encode_v3(
                self.identity,
                itertools.repeat(self.seeds[0]),
                (),
            ),
        )
        negative_zero = replace(
            self.links[0], mechanism_anchor=(-0.0, 0.0, 0.0)
        )
        self.assert_wire_code(
            contract.RLW_BAD_LINK_RECORD,
            lambda: runeio.encode_v3(
                self.identity, self.seeds, (negative_zero, self.links[1])
            ),
        )

    def test_read_v3_is_bounded_and_reports_io(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "golden.rune"
            path.write_bytes(self.golden)
            decoded = runeio.read_v3(path, expected_identity=self.identity)
            self.assertEqual(self.golden[HEADER_BYTES:], decoded.payload)
            self.assert_wire_code(
                contract.RLW_IO_ERROR,
                lambda: runeio.read_v3(Path(temporary) / "missing.rune"),
            )


if __name__ == "__main__":
    unittest.main()
