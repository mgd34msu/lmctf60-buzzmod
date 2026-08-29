#!/usr/bin/env python3
"""Differential tests for the three independent RUNE v2 read paths."""
from __future__ import annotations

import binascii
import json
import math
import os
from pathlib import Path
import re
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
SOURCE_SET_ID = 0x5352435345543031
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


def _stable_id(domain: int, ordinal: int) -> bytes:
    return struct.pack("<QQQ", SOURCE_SET_ID, (domain << 32) | 7,
                       (ordinal << 32) | (ordinal + 11))


def _order(domain: int, ordinal: int) -> bytes:
    return struct.pack("<QIIII", SOURCE_SET_ID, domain, 7, ordinal,
                       ordinal + 11)


def _build_scale_fixture(base: bytes, cell_count: int) -> bytes:
    sections = [base[offset:offset + size]
                for offset, size in (_section(base, index)
                                     for index in range(SECTION_COUNT))]
    model = bytearray(sections[0])
    for offset, value in ((160, cell_count), (164, 0), (168, cell_count),
                          (172, 0), (228, cell_count), (232, 0)):
        struct.pack_into("<I", model, offset, value)

    cell_template = sections[5][:164]
    surface_template = sections[7][:132]
    cells = bytearray()
    surfaces = bytearray()
    for index in range(cell_count):
        cell = bytearray(cell_template)
        cell[0:24] = _stable_id(1, index)
        cell[24:48] = _order(1, index)
        struct.pack_into("<II", cell, 56, index, index)
        minimum = float(index * 2)
        struct.pack_into("<6f", cell, 64, minimum, 0.0, 0.0,
                         minimum + 1.0, 1.0, 1.0)
        for offset, first, count in (
                (88, 0, 4), (96, 0, 1), (104, index, 1),
                (112, 0, 0), (120, 0, 0), (128, 0, 0), (136, 0, 0)):
            struct.pack_into("<II", cell, offset, first, count)
        struct.pack_into("<III", cell, 144, index, index, index)
        cells.extend(cell)

        surface = bytearray(surface_template)
        surface[0:24] = _stable_id(6, index)
        surface[24:48] = _order(6, index)
        struct.pack_into("<II", surface, 56, index, index)
        surface[64:88] = _stable_id(1, index)
        surfaces.extend(surface)

    payloads = [bytes(model), sections[1], b"", sections[3][:136], b"",
                bytes(cells), b"", bytes(surfaces), b"", b"", b"", b"",
                sections[12]]
    record_bytes = (256, 64, 12, 136, 160, 164, 172, 132, 104, 332,
                    188, 160, 64)
    output = bytearray(HEADER_BYTES + SECTION_COUNT * ENTRY_BYTES)
    for index, payload in enumerate(payloads):
        while len(output) % 8:
            output.append(0)
        offset = len(output)
        output.extend(payload)
        entry = _entry(index)
        struct.pack_into("<HHIIIQQ", output, entry, index + 1, 1,
                         record_bytes[index], len(payload) // record_bytes[index],
                         _crc32(payload), offset, len(payload))
    while len(output) % 8:
        output.append(0)
    struct.pack_into("<IHHHHIIIQQII", output, 0, 0x324E5552, 2, 0x0102,
                     HEADER_BYTES, ENTRY_BYTES, SECTION_COUNT, 0, 4,
                     GENERATION, len(output), _crc32(output[HEADER_BYTES:]), 0)
    struct.pack_into("<I", output, 44, _crc32(output[:HEADER_BYTES]))
    return bytes(output)


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

    def test_c_lookup_comparisons_scale_logarithmically(self) -> None:
        counts = {}
        for cell_count in (1024, 8192):
            artifact = Path(self.directory.name) / f"scale-{cell_count}.rune"
            artifact.write_bytes(_build_scale_fixture(self.valid, cell_count))
            if cell_count == 1024:
                for reader in (self.codec_probe, PYTHON_READER):
                    validation = subprocess.run(self._command(reader, artifact),
                                                text=True, capture_output=True,
                                                check=False)
                    self.assertEqual(0, validation.returncode,
                                     validation.stderr)
            command = self._command(self.c_reader, artifact)
            command.insert(-1, "--comparison-count")
            result = subprocess.run(command, text=True, capture_output=True,
                                    check=False)
            self.assertEqual(0, result.returncode, result.stderr)
            match = re.fullmatch(r"lookup_comparisons=(\d+)\n", result.stderr)
            self.assertIsNotNone(match, result.stderr)
            comparisons = int(match.group(1))
            cell_lookup_bound = math.ceil(math.log2(cell_count)) + 1
            plane_lookup_bound = math.ceil(math.log2(8)) + 1
            maximum = cell_count * (2 * cell_lookup_bound +
                                    plane_lookup_bound)
            self.assertLessEqual(comparisons, maximum)
            counts[cell_count] = comparisons
        self.assertGreater(counts[8192], counts[1024])

    def test_sparse_file_over_wire_limit_is_rejected_before_allocation(
            self) -> None:
        artifact = Path(self.directory.name) / "oversize-sparse.rune"
        with artifact.open("wb") as output:
            output.truncate((1 << 32) + 1)
        for reader, expected_status in ((self.codec_probe, 1),
                                        (self.c_reader, 2),
                                        (PYTHON_READER, 1)):
            with self.subTest(reader=reader):
                result = subprocess.run(self._command(reader, artifact),
                                        text=True, capture_output=True,
                                        check=False)
                self.assertEqual(expected_status, result.returncode,
                                 result.stderr)

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

    def test_hostile_phase_transition_count(self) -> None:
        malformed = bytearray(self.valid)
        struct.pack_into("<I", malformed, _entry(4) + 8, 4_194_305)
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

    def test_transition_destination_cell_is_authenticated(self) -> None:
        # The fixture's stance transition is local. Pointing its explicit
        # destination ownership at the other valid cell must therefore make
        # the cross-cell relation inconsistent in every reader.
        transition_offset, _ = _section(self.valid, 4)
        destination_cell_offset = transition_offset + 136
        malformed = bytearray(self.valid)
        malformed[destination_cell_offset:destination_cell_offset + 24] = \
            self.valid[_section(self.valid, 5)[0] + 164:
                       _section(self.valid, 5)[0] + 164 + 24]
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

    def test_stance_transition_requires_equal_elapsed_interval(self) -> None:
        second_phase_elapsed = 136 + 120
        self.assert_rejected_by_all(_mutate_record(
            self.valid, 3, second_phase_elapsed, struct.pack("<f", 1.25)))

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
