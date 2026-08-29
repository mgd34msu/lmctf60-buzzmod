#!/usr/bin/env python3
"""Focused hostile-input proof for the Make-side independent RUNE v2 reader."""
from __future__ import annotations

import binascii
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


BSP_ID = bytes(range(1, 33))
SCHEMA_ID = bytes(range(65, 97))
PROBE_ARTIFACT_ID = bytes(range(129, 161))
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
    return struct.unpack_from("<QQ", data, _entry(section) + 16)


def _fix_checksums(data: bytearray) -> None:
    for section in range(SECTION_COUNT):
        offset, byte_count = _section(data, section)
        if offset <= len(data) and byte_count <= len(data) - offset:
            struct.pack_into("<I", data, _entry(section) + 12,
                             _crc32(data[offset:offset + byte_count]))
    struct.pack_into("<I", data, 40, _crc32(data[HEADER_BYTES:]))
    struct.pack_into("<I", data, 44, 0)
    struct.pack_into("<I", data, 44, _crc32(data[:HEADER_BYTES]))


class RuneV2MakeIndependentReaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.reader = Path(os.environ["RUNE_V2_MAKE_C_READER"])
        cls.corrupt_reader = Path(os.environ["RUNE_V2_MAKE_C_READER_CORRUPT"])
        cls.codec_probe = Path(os.environ["RUNE_V2_CODEC_PROBE"])
        cls.fixture_writer = Path(os.environ["RUNE_V2_FIXTURE_WRITER"])
        cls.directory = tempfile.TemporaryDirectory()
        cls.fixture_path = Path(cls.directory.name) / "synthetic.rune"
        subprocess.run([cls.fixture_writer, cls.fixture_path], check=True)
        cls.valid = cls.fixture_path.read_bytes()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.directory.cleanup()

    def _write(self, data: bytes) -> Path:
        artifact = Path(self.directory.name) / "candidate.rune"
        artifact.write_bytes(data)
        return artifact

    def _make_command(self, reader: Path, artifact: Path, data: bytes,
                      artifact_identity: bytes | None = None) -> list[str]:
        identity = artifact_identity or hashlib.sha256(data).digest()
        return [str(reader), "--generation", str(GENERATION), "--bsp-id",
                BSP_ID.hex(), "--schema-id", SCHEMA_ID.hex(), "--artifact-id",
                identity.hex(), "--exact-artifact-id", identity.hex(), str(artifact)]

    def _run_make(self, data: bytes, reader: Path | None = None,
                  artifact_identity: bytes | None = None) -> subprocess.CompletedProcess[str]:
        artifact = self._write(data)
        return subprocess.run(self._make_command(reader or self.reader, artifact, data,
                                                  artifact_identity), text=True,
                              capture_output=True, check=False)

    def _reject(self, data: bytes) -> None:
        result = self._run_make(data)
        self.assertEqual(1, result.returncode, (result.stdout, result.stderr))

    def _reject_like_production(self, data: bytes) -> None:
        artifact = self._write(data)
        production = subprocess.run(self._probe_command(artifact), text=True,
                                    capture_output=True, check=False)
        reader = subprocess.run(self._make_command(self.reader, artifact, data),
                                text=True, capture_output=True, check=False)
        self.assertEqual(1, production.returncode,
                         (production.stdout, production.stderr))
        self.assertEqual(1, reader.returncode, (reader.stdout, reader.stderr))

    def _probe_command(self, artifact: Path) -> list[str]:
        return [str(self.codec_probe), "--generation", str(GENERATION),
                "--bsp-id", BSP_ID.hex(), "--schema-id", SCHEMA_ID.hex(),
                "--artifact-id", PROBE_ARTIFACT_ID.hex(), "--exact-artifact-id",
                PROBE_ARTIFACT_ID.hex(), str(artifact)]

    def test_valid_fixture_binds_its_exact_owned_bytes(self) -> None:
        result = self._run_make(self.valid)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(EXPECTED_SUMMARY, json.loads(result.stdout))

    def test_hostile_wire_shapes_are_rejected(self) -> None:
        malformed = bytearray(self.valid)
        plane_offset, _ = _section(malformed, 1)
        malformed[plane_offset + 48] ^= 1
        self._reject(bytes(malformed))

        malformed = bytearray(self.valid)
        malformed[48] = 1
        _fix_checksums(malformed)
        self._reject(bytes(malformed))

        malformed = bytearray(self.valid)
        model_offset, _ = _section(malformed, 0)
        malformed[model_offset + 2] = 1
        _fix_checksums(malformed)
        self._reject(bytes(malformed))

        malformed = bytearray(self.valid)
        portal_offset, _ = _section(malformed, 6)
        struct.pack_into("<Q", malformed, _entry(6) + 16, portal_offset + 1)
        _fix_checksums(malformed)
        self._reject(bytes(malformed))

        malformed = bytearray(self.valid)
        cell_offset, cell_bytes = _section(malformed, 5)
        struct.pack_into("<Q", malformed, _entry(6) + 16,
                         cell_offset + cell_bytes - 8)
        _fix_checksums(malformed)
        self._reject(bytes(malformed))

        malformed = bytearray(self.valid)
        struct.pack_into("<I", malformed, _entry(5) + 8, 0xFFFFFFFF)
        _fix_checksums(malformed)
        self._reject(bytes(malformed))

        malformed = bytearray(self.valid)
        struct.pack_into("<I", malformed, _entry(5) + 8, 3)
        _fix_checksums(malformed)
        self._reject(bytes(malformed))

        malformed = bytearray(self.valid)
        struct.pack_into("<Q", malformed, _entry(6) + 16, (1 << 64) - 4)
        _fix_checksums(malformed)
        self._reject(bytes(malformed))

        malformed = bytearray(self.valid)
        malformed.append(0)
        struct.pack_into("<Q", malformed, 32, len(malformed))
        _fix_checksums(malformed)
        self._reject(bytes(malformed))

    def test_out_of_range_reference_and_identity_mismatch_are_rejected(self) -> None:
        malformed = bytearray(self.valid)
        portal_offset, _ = _section(malformed, 6)
        struct.pack_into("<QQQ", malformed, portal_offset + 64,
                         0x5352435345543031, (1 << 32) | 7,
                         (99 << 32) | 110)
        _fix_checksums(malformed)
        self._reject(bytes(malformed))

        wrong_identity = bytes(reversed(hashlib.sha256(self.valid).digest()))
        result = self._run_make(self.valid, artifact_identity=wrong_identity)
        self.assertEqual(1, result.returncode, (result.stdout, result.stderr))

    def test_rebound_private_semantic_mutants_match_production_rejection(self) -> None:
        malformed = bytearray(self.valid)
        portal_offset, _ = _section(malformed, 6)
        struct.pack_into("<I", malformed, portal_offset + 152, 99)
        _fix_checksums(malformed)
        self._reject_like_production(bytes(malformed))

        malformed = bytearray(self.valid)
        model_offset, _ = _section(malformed, 0)
        struct.pack_into("<I", malformed, model_offset + 236, 1)
        _fix_checksums(malformed)
        self._reject_like_production(bytes(malformed))

        malformed = bytearray(self.valid)
        planes_offset, _ = _section(malformed, 1)
        first = bytes(malformed[planes_offset:planes_offset + 64])
        second = bytes(malformed[planes_offset + 64:planes_offset + 128])
        malformed[planes_offset:planes_offset + 64] = second
        malformed[planes_offset + 64:planes_offset + 128] = first
        _fix_checksums(malformed)
        self._reject_like_production(bytes(malformed))

        malformed = bytearray(self.valid)
        kernel_offset, _ = _section(malformed, 9)
        malformed[kernel_offset + 72:kernel_offset + 96] = \
            malformed[kernel_offset + 48:kernel_offset + 72]
        _fix_checksums(malformed)
        self._reject_like_production(bytes(malformed))

    def test_corrupted_reader_disagrees_with_production_probe(self) -> None:
        malformed = bytearray(self.valid)
        malformed[48] = 1
        _fix_checksums(malformed)
        data = bytes(malformed)
        artifact = self._write(data)
        normal = subprocess.run(self._make_command(self.reader, artifact, data),
                                text=True, capture_output=True, check=False)
        production = subprocess.run(self._probe_command(artifact), text=True,
                                    capture_output=True, check=False)
        corrupted = subprocess.run(self._make_command(self.corrupt_reader, artifact, data),
                                    text=True, capture_output=True, check=False)
        self.assertEqual(1, normal.returncode, (normal.stdout, normal.stderr))
        self.assertEqual(1, production.returncode,
                         (production.stdout, production.stderr))
        self.assertEqual(0, corrupted.returncode,
                         (corrupted.stdout, corrupted.stderr))


if __name__ == "__main__":
    unittest.main()
