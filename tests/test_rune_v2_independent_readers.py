#!/usr/bin/env python3
"""Differential tests for the three independent RUNE v2 read paths."""
from __future__ import annotations

import binascii
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PYTHON_READER = ROOT / "tools" / "runev2read.py"
BSP_ID = bytes(range(1, 33))
SCHEMA_ID = bytes(range(65, 97))
ARTIFACT_ID = bytes(range(129, 161))
OTHER_ID = bytes(range(161, 193))
GENERATION = 0xB0B1B2B3B4B5B6B7
HEADER_BYTES = 64
ENTRY_BYTES = 32
SECTION_COUNT = 13

EXPECTED_SUMMARY = {
    "affordances": 1,
    "bsp": BSP_ID.hex(),
    "cells": 2,
    "generation": GENERATION,
    "kernels": 1,
    "landmarks": 1,
    "mechanisms": 1,
    "phases": 3,
    "planes": 8,
    "portal_vertices": 3,
    "portals": 1,
    "schema": SCHEMA_ID.hex(),
    "surfaces": 1,
    "transitions": 1,
}


def _crc32(data: bytes | bytearray) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def _entry(section: int) -> int:
    return HEADER_BYTES + section * ENTRY_BYTES


def _section(data: bytes | bytearray, section: int) -> tuple[int, int]:
    entry = _entry(section)
    return struct.unpack_from("<QQ", data, entry + 16)


def _fix_checksums(data: bytearray) -> None:
    for section in range(SECTION_COUNT):
        offset, byte_count = _section(data, section)
        if offset <= len(data) and byte_count <= len(data) - offset:
            struct.pack_into("<I", data, _entry(section) + 12,
                             _crc32(data[offset:offset + byte_count]))
    struct.pack_into("<I", data, 40, _crc32(data[HEADER_BYTES:]))
    struct.pack_into("<I", data, 44, 0)
    struct.pack_into("<I", data, 44, _crc32(data[:HEADER_BYTES]))


def _mutate_record(data: bytes, section: int, relative_offset: int,
                   encoded: bytes) -> bytes:
    malformed = bytearray(data)
    offset, _ = _section(malformed, section)
    malformed[offset + relative_offset:offset + relative_offset + len(encoded)] = encoded
    _fix_checksums(malformed)
    return bytes(malformed)


class RuneV2IndependentReaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.c_reader = Path(os.environ["RUNE_V2_C_READER"])
        cls.codec_probe = Path(os.environ["RUNE_V2_CODEC_PROBE"])
        cls.fixture_writer = Path(os.environ["RUNE_V2_FIXTURE_WRITER"])
        cls.directory = tempfile.TemporaryDirectory()
        cls.fixture_path = Path(cls.directory.name) / "synthetic.rune"
        subprocess.run([cls.fixture_writer, cls.fixture_path], check=True)
        cls.valid = cls.fixture_path.read_bytes()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.directory.cleanup()

    def _command(self, reader: Path, artifact: Path, *, bsp: bytes = BSP_ID,
                 schema: bytes = SCHEMA_ID, artifact_id: bytes = ARTIFACT_ID,
                 exact_artifact_id: bytes = ARTIFACT_ID) -> list[str]:
        command = [str(reader)]
        if reader.suffix == ".py":
            command.insert(0, sys.executable)
        command.extend([
            "--generation", str(GENERATION),
            "--bsp-id", bsp.hex(),
            "--schema-id", schema.hex(),
            "--artifact-id", artifact_id.hex(),
            "--exact-artifact-id", exact_artifact_id.hex(),
            str(artifact),
        ])
        return command

    def _run_all(self, data: bytes, **identity: bytes) -> list[subprocess.CompletedProcess[str]]:
        artifact = Path(self.directory.name) / "candidate.rune"
        artifact.write_bytes(data)
        return [
            subprocess.run(self._command(reader, artifact, **identity),
                           text=True, capture_output=True, check=False)
            for reader in (self.codec_probe, self.c_reader, PYTHON_READER)
        ]

    def assert_rejected_by_all(self, data: bytes, **identity: bytes) -> None:
        results = self._run_all(data, **identity)
        self.assertEqual([1, 1, 1], [result.returncode for result in results],
                         [(result.stdout, result.stderr) for result in results])

    def test_valid_fixture_has_one_deterministic_summary(self) -> None:
        results = self._run_all(self.valid)
        self.assertEqual([0, 0, 0], [result.returncode for result in results],
                         [(result.stdout, result.stderr) for result in results])
        summaries = [json.loads(result.stdout) for result in results]
        self.assertEqual([EXPECTED_SUMMARY] * 3, summaries)

    def test_independently_reauthenticated_valid_variant(self) -> None:
        variant = _mutate_record(self.valid, 1, 60, struct.pack("<f", 12.5))
        results = self._run_all(variant)
        self.assertEqual([0, 0, 0], [result.returncode for result in results],
                         [(result.stdout, result.stderr) for result in results])
        self.assertEqual([EXPECTED_SUMMARY] * 3,
                         [json.loads(result.stdout) for result in results])

    def test_fresh_process_cold_reads_the_artifact(self) -> None:
        first = self._run_all(self.valid)
        second = self._run_all(self.valid)
        self.assertEqual([result.stdout for result in first],
                         [result.stdout for result in second])
        self.assertTrue(all(result.returncode == 0 for result in first + second))

    def test_truncation(self) -> None:
        self.assert_rejected_by_all(self.valid[:-1])

    def test_unknown_version(self) -> None:
        malformed = bytearray(self.valid)
        struct.pack_into("<H", malformed, 4, 3)
        _fix_checksums(malformed)
        self.assert_rejected_by_all(bytes(malformed))

    def test_hostile_count(self) -> None:
        malformed = bytearray(self.valid)
        struct.pack_into("<I", malformed, _entry(5) + 8, 1_048_577)
        _fix_checksums(malformed)
        self.assert_rejected_by_all(bytes(malformed))

    def test_count_size_disagreement(self) -> None:
        malformed = bytearray(self.valid)
        struct.pack_into("<I", malformed, _entry(5) + 8, 3)
        _fix_checksums(malformed)
        self.assert_rejected_by_all(bytes(malformed))

    def test_offset_overflow(self) -> None:
        malformed = bytearray(self.valid)
        struct.pack_into("<Q", malformed, _entry(6) + 16, (1 << 64) - 4)
        _fix_checksums(malformed)
        self.assert_rejected_by_all(bytes(malformed))

    def test_overlap(self) -> None:
        malformed = bytearray(self.valid)
        previous_offset, previous_bytes = _section(malformed, 5)
        struct.pack_into("<Q", malformed, _entry(6) + 16,
                         previous_offset + previous_bytes - 8)
        _fix_checksums(malformed)
        self.assert_rejected_by_all(bytes(malformed))

    def test_noncanonical_gap(self) -> None:
        malformed = bytearray(self.valid)
        insertion, _ = _section(malformed, 6)
        malformed[insertion:insertion] = b"\0" * 8
        for section in range(6, SECTION_COUNT):
            offset, _ = _section(malformed, section)
            struct.pack_into("<Q", malformed, _entry(section) + 16, offset + 8)
        struct.pack_into("<Q", malformed, 32, len(malformed))
        _fix_checksums(malformed)
        self.assert_rejected_by_all(bytes(malformed))

    def test_bad_last_record_reference(self) -> None:
        offset, _ = _section(self.valid, 11)
        malformed = bytearray(self.valid)
        low = struct.unpack_from("<Q", malformed, offset + 52 + 16)[0]
        struct.pack_into("<Q", malformed, offset + 52 + 16, low + 1)
        _fix_checksums(malformed)
        self.assert_rejected_by_all(bytes(malformed))

    def test_crc_drift(self) -> None:
        offset, _ = _section(self.valid, 1)
        malformed = bytearray(self.valid)
        malformed[offset + 48] ^= 1
        self.assert_rejected_by_all(bytes(malformed))

    def test_nonfinite_geometry(self) -> None:
        self.assert_rejected_by_all(_mutate_record(
            self.valid, 1, 48, struct.pack("<I", 0x7FC00000)))

    def test_capability_domain(self) -> None:
        self.assert_rejected_by_all(_mutate_record(
            self.valid, 9, 240, struct.pack("<I", 6)))

    def test_phase_transition_domain(self) -> None:
        self.assert_rejected_by_all(_mutate_record(
            self.valid, 4, 120, struct.pack("<I", 2)))

    def test_incomplete_evidence(self) -> None:
        self.assert_rejected_by_all(_mutate_record(
            self.valid, 0, 228, struct.pack("<I", 1)))

    def test_trailing_byte_even_with_reauthenticated_header(self) -> None:
        malformed = bytearray(self.valid)
        malformed.append(0)
        struct.pack_into("<Q", malformed, 32, len(malformed))
        _fix_checksums(malformed)
        self.assert_rejected_by_all(bytes(malformed))

    def test_explicit_identity_boundaries(self) -> None:
        self.assert_rejected_by_all(self.valid, bsp=OTHER_ID)
        self.assert_rejected_by_all(self.valid, schema=OTHER_ID)
        self.assert_rejected_by_all(self.valid, exact_artifact_id=OTHER_ID)
        self.assert_rejected_by_all(self.valid, artifact_id=bytes(32),
                                    exact_artifact_id=bytes(32))

    def test_zero_wire_identity(self) -> None:
        malformed = _mutate_record(self.valid, 12, 0, bytes(32))
        self.assert_rejected_by_all(malformed, bsp=bytes(32))


if __name__ == "__main__":
    unittest.main()
