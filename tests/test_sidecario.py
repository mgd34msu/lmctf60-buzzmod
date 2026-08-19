#!/usr/bin/env python3
"""Focused RUNE sidecar wire checks."""
from __future__ import annotations

from dataclasses import replace
from pathlib import Path
import struct
import sys
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / 'tools'
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from tests.test_rune_artifact import _build_rune
import rune_contracts_generated as contract
import runeio
import sidecario


def _header_crc(data):
    struct.pack_into('<I', data, sidecario.HEADER_CRC_OFFSET, 0)
    struct.pack_into('<I', data, sidecario.HEADER_CRC_OFFSET,
                     zlib.crc32(data[:sidecario.HEADER_BYTES]) & 0xffffffff)


class SidecarCodecTests(unittest.TestCase):
    def setUp(self):
        self.binding = sidecario.Binding(
            2, 2, 0x11223344, contract.RUNE_ACTION_CONTRACT_CRC32, 0x55667788)
        self.encoded = sidecario.encode(sidecario.HMN, self.binding, b'\x07\xc8')

    def test_header_slots_are_required_zero(self):
        fields = sidecario.HEADER_STRUCT.unpack_from(self.encoded)
        self.assertEqual((0, 0, 0), (fields[1], fields[3], fields[6]))
        header = sidecario.decode(
            self.encoded, expected_binding=self.binding, expected_kind=sidecario.HMN).header
        self.assertEqual(
            {
                "kind", "header_bytes", "element_bytes", "planes", "num_seeds",
                "num_links", "rune_payload_crc32", "action_contract_crc32",
                "rune_header_crc32", "payload_bytes", "payload_crc32", "header_crc32",
            },
            set(header.__dataclass_fields__),
        )

    def test_nonzero_reserved_slots_fail_closed(self):
        for offset in (4, 8, 14):
            with self.subTest(offset=offset):
                data = bytearray(self.encoded)
                data[offset] = 1
                _header_crc(data)
                with self.assertRaises(sidecario.SidecarError) as caught:
                    sidecario.decode(data, expected_binding=self.binding,
                                     expected_kind=sidecario.HMN)
                self.assertEqual('SCD_NONZERO_RESERVED', caught.exception.symbol)

    def test_artifact_binding_has_no_discriminator(self):
        artifact = runeio.decode(_build_rune())
        binding = sidecario.binding_from_rune(artifact)
        self.assertEqual(
            {
                "num_seeds", "num_links", "rune_payload_crc32",
                "action_contract_crc32", "rune_header_crc32",
            },
            set(binding.__dataclass_fields__),
        )
        self.assertEqual(artifact.header.num_seeds, binding.num_seeds)

    def test_payload_and_binding_still_authenticate(self):
        bad = bytearray(self.encoded)
        bad[-1] ^= 1
        with self.assertRaises(sidecario.SidecarError) as caught:
            sidecario.decode(bad, expected_binding=self.binding,
                             expected_kind=sidecario.HMN)
        self.assertEqual('SCD_BAD_PAYLOAD_CRC', caught.exception.symbol)
        wrong = replace(self.binding, num_links=1)
        with self.assertRaises(sidecario.SidecarError) as caught:
            sidecario.decode(self.encoded, expected_binding=wrong,
                             expected_kind=sidecario.HMN)
        self.assertEqual('SCD_BAD_COUNTS', caught.exception.symbol)


if __name__ == '__main__':
    unittest.main()
