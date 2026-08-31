#!/usr/bin/env python3
from __future__ import annotations

import binascii
import base64
import hashlib
import mmap
import os
from pathlib import Path
import struct
import stat
import subprocess
import sys
import tempfile
import tracemalloc
import unittest
from unittest import mock
import zlib

try:
    import resource
except ImportError:
    resource = None


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import runecompactread as reader  # noqa: E402


EXPECTED_MAGIC = b"SGRCW001"
EXPECTED_WIRE_VERSION = 2
EXPECTED_MODEL_VERSION = 2
EXPECTED_ANALYTIC_VERSION = 1
EXPECTED_MODEL_SCHEMA_TAG = 0x4D434E52
EXPECTED_FACET_POLYGON = 0
EXPECTED_FACET_CONSTRAINT_ONLY = 1
EXPECTED_FACET_KIND_COUNT = 2
EXPECTED_INDEX_NONE = 0xFFFFFFFF
EXPECTED_ALIGNMENT = 8
EXPECTED_HEADER_FIXED_BYTES = 48
EXPECTED_DESCRIPTOR_BYTES = 24
EXPECTED_HEADER_BYTES = 720
EXPECTED_CHECKSUM_OFFSET = 24
EXPECTED_IDENTITY_BYTES = 252
EXPECTED_SECTION_SPECS = (
    ("identity", 252, 1),
    ("cells", 80, 1_048_576),
    ("facets", 60, 4_194_304),
    ("incidences", 20, 8_388_608),
    ("cell_incidences", 4, 8_388_608),
    ("vertices", 12, 16_777_216),
    ("portals", 44, 2_097_152),
    ("movement_fields", 24, 4_194_304),
    ("weapon_regions", 20, 4_194_304),
    ("weapon_profiles", 8, 256),
    ("weapon_kernels", 20, 8_388_608),
    ("analytic_function_refs", 4, 33_554_432),
    ("analytic_functions", 20, 1_048_576),
    ("analytic_input_dimensions", 4, 16_777_216),
    ("analytic_constants", 4, 1_048_576),
    ("analytic_affines", 12, 1_048_576),
    ("analytic_affine_slopes", 4, 16_777_216),
    ("analytic_polynomials", 12, 1_048_576),
    ("analytic_polynomial_coefficients", 4, 33_554_432),
    ("analytic_ballistics", 12, 1_048_576),
    ("analytic_piecewise", 16, 1_048_576),
    ("analytic_piecewise_clauses", 16, 4_194_304),
    ("mechanisms", 100, 1_048_576),
    ("mechanism_edges", 16, 4_194_304),
    ("landmarks", 60, 4_194_304),
    ("landmark_cells", 4, 16_777_216),
    ("facet_annotations", 8, 4_194_304),
    ("portal_mechanisms", 15, 4_194_304),
)
EXPECTED_MAX_CANONICAL_IMAGE_BYTES = 2_319_453_136
EXPECTED_C_FIXTURE_BYTES = 3_048
EXPECTED_C_FIXTURE_CHECKSUM = 0x797CC49D
EXPECTED_C_FIXTURE_SHA256 = (
    "81731561750f4d97dae234a3f452a1e7482f3f4b857d1577f06b5190d78ef49f"
)
EXPECTED_C_FIXTURE_COUNTS = {
    "identity": 1,
    "cells": 2,
    "facets": 1,
    "incidences": 2,
    "cell_incidences": 2,
    "vertices": 4,
    "portals": 1,
    "movement_fields": 2,
    "weapon_regions": 2,
    "weapon_profiles": 12,
    "weapon_kernels": 26,
    "analytic_function_refs": 114,
    "analytic_functions": 7,
    "analytic_input_dimensions": 0,
    "analytic_constants": 7,
    "analytic_affines": 0,
    "analytic_affine_slopes": 0,
    "analytic_polynomials": 0,
    "analytic_polynomial_coefficients": 0,
    "analytic_ballistics": 0,
    "analytic_piecewise": 0,
    "analytic_piecewise_clauses": 0,
    "mechanisms": 2,
    "mechanism_edges": 1,
    "landmarks": 2,
    "landmark_cells": 2,
    "facet_annotations": 1,
    "portal_mechanisms": 1,
}
EXPECTED_C_FIXTURE_IDENTITY = base64.b85decode(
    b"S^xk50000000000000000000000000000000000000aO4000000RaF20s#O30s;U4000000|Nj600000F)#oC00000GB5xD00000"
    b"GcW)E00000G%x@F000001Oos7000001p@#8000001_J;9000001Oxy800000Qbj{mL{Cys0RR910{{R31ONa41poj50RR910RR91"
    b"AOHXWfdBvhfdBvhK>z>$fB*mhfB*mh00961fdBvhfdBvhK>z>$fB*mhfB*mhfB*mh0078B001CC004kL004kM004SK005Rm0000#"
    b"002lt2mk;80RR91"
)
C_CODEC_CANONICAL_FIXTURE = zlib.decompress(base64.b85decode(
    b"c-qxgJ&RL86rJ~#m*ge-H0v6pNU*TcBCA_iEcn($+`^9#6@yr;AR-pBSgBa#SBQ<^Pw*#LSZtYc%Psv0B0`+WJ2!jw`W{gb#DT-joOkbhoVjx+"
    b"cW;dP4|=^d-%R~8#+a{(nYRyL-oAF$w;0{--}1~s`(Yvdsh?ZdAHsjm2f20q4f5X!bL&2@kpCy}At3x7_{gGxZ?5g*N5$eIjh{xjjV;<VP6ocJ"
    b"=G_5b!?y<b_u%XB?|}aXKEYaEiYV3GG~geD-mT@fNo#j)T+i(h@Uh~L(pp@51wK{d2!E`^|9FXitHgf=b7=lV3BPLbg!B~tB>d%EGUQbo`5^v7"
    b"@Tb(d?}G0@+C{#BKMjq9-vj^9ENKwKvw^+?CJd0TYrWiFg1rp84jaNRf~~-Y7<Y8Ne}8aiH1v?;A#&FPx)zh!Z03+T>-xArMf@aJ<C=}!w7shq"
    b"nK5q$#=NsIw!bxCedI3sm|wSyU?@&oL-D>EFC(7`jpvns$K!bp^3v^V(KhLr7Wq)tyn?;+@2p_&S_kHWC3AI^HoUh$?b9CPETC^e!8)5ttES>u"
    b"d#R08<W!3Kw~hLd;oAiD%neY7hzUe(P_2?yA-!eZx;g`&Y8CI7>x-;!mADA#D9+7!mbeOVj<^^&j~H3H!v*4MK#yYUz&ddWa0MLa$_8;B<xZ&1"
    b"VL+T{utl6`aFaOEV5m5^-+RP~24mtxgGW?HM1#k~i3U$8Ry24{oM`Yuoq42wdn)h#b-(hP-2c}9Tl@Nx8Yy==DX3wMg6A?-T!eeCYe?%5Gq36P"
    b"3T0XTW_`5yY_Xs5-R4@D(p+7jr+mJ<|EMUW)A-PnzIZD<b>F#_&EYN4&o6n8652;d^NHqdpjt9_O7Sg<b8p8s#f~ZV8O3^qZ}_jT*iV2!VY>"
))


def _checksum(image: bytearray) -> None:
    struct.pack_into("<I", image, EXPECTED_CHECKSUM_OFFSET, 0)
    struct.pack_into("<I", image, EXPECTED_CHECKSUM_OFFSET,
                     binascii.crc32(image) & 0xFFFFFFFF)


def _descriptor(section: int) -> int:
    return EXPECTED_HEADER_FIXED_BYTES + section * EXPECTED_DESCRIPTOR_BYTES


def _section_offset(image: bytes | bytearray, section: int) -> int:
    return struct.unpack_from("<Q", image, _descriptor(section) + 16)[0]


def _identity(image: bytes | bytearray) -> bytes:
    offset = _section_offset(image, 0)
    return bytes(image[offset:offset + EXPECTED_IDENTITY_BYTES])


def _fixture(**counts: int) -> bytearray:
    image = bytearray(EXPECTED_HEADER_BYTES)
    image[:8] = EXPECTED_MAGIC
    struct.pack_into("<HHI", image, 8, EXPECTED_WIRE_VERSION,
                     EXPECTED_HEADER_BYTES, len(EXPECTED_SECTION_SPECS))
    struct.pack_into("<H", image, 32, EXPECTED_MODEL_VERSION)
    struct.pack_into("<I", image, 36, EXPECTED_MODEL_SCHEMA_TAG)
    struct.pack_into("<H", image, 40, EXPECTED_ANALYTIC_VERSION)
    offsets = []
    for number, (name, record_bytes, _) in enumerate(EXPECTED_SECTION_SPECS):
        while len(image) % EXPECTED_ALIGNMENT:
            image.append(0)
        offset = len(image)
        count = 1 if name == "identity" else counts.get(name, 0)
        image.extend(b"\0" * (count * record_bytes))
        struct.pack_into("<IIIIQ", image, _descriptor(number), number,
                         record_bytes, count, 0, offset)
        offsets.append(offset)
    while len(image) % EXPECTED_ALIGNMENT:
        image.append(0)
    struct.pack_into("<Q", image, 16, len(image))

    identity = offsets[0]
    image[identity:identity + 32] = bytes(range(32))
    struct.pack_into("<QII11Q", image, identity + 32,
                     0x0102030405060708, 0x10203040, 0x50607080,
                     *range(0x100, 0x10B))
    struct.pack_into("<7I", image, identity + 136, 1, 2, 3, 4, 5, 6, 7)
    struct.pack_into("<12i", image, identity + 164,
                     -1, -2, -3, 4, 5, 6, -7, -8, -9, 10, 11, 12)
    struct.pack_into("<10I", image, identity + 212, *range(10, 20))
    _checksum(image)
    return image


def _facet_fixture(kind: int, vertex_count: int, incidence_count: int,
                   portal: int) -> bytearray:
    image = _fixture(
        cells=1 if incidence_count else 0,
        facets=1,
        incidences=incidence_count,
        portals=1 if portal != EXPECTED_INDEX_NONE else 0,
        vertices=vertex_count,
    )
    facet = _section_offset(image, 2)
    struct.pack_into("<IIIII", image, facet + 36, 0, vertex_count,
                     0, incidence_count, portal)
    struct.pack_into("<I", image, facet + 56, kind)
    _checksum(image)
    return image


class RuneCompactReaderTests(unittest.TestCase):
    def setUp(self) -> None:
        self.image = _fixture(cells=2, vertices=3, analytic_constants=4)

    def assert_rejected(self, image: bytes | bytearray, code: str) -> None:
        with self.assertRaisesRegex(reader.RuneCompactError, f"^{code}$"):
            reader.inspect(image)

    def assert_expected_identity_rejected(self, expected_identity: object,
                                          code: str) -> None:
        with self.assertRaisesRegex(reader.RuneCompactError, f"^{code}$"):
            reader.inspect(self.image, expected_identity=expected_identity)

    def test_schema_facts_are_fixed(self) -> None:
        self.assertEqual(EXPECTED_MAGIC, reader.MAGIC)
        self.assertEqual(EXPECTED_WIRE_VERSION, reader.WIRE_VERSION)
        self.assertEqual(EXPECTED_MODEL_VERSION, reader.MODEL_VERSION)
        self.assertEqual(EXPECTED_ANALYTIC_VERSION, reader.ANALYTIC_VERSION)
        self.assertEqual(EXPECTED_MODEL_SCHEMA_TAG, reader.MODEL_SCHEMA_TAG)
        self.assertEqual(EXPECTED_FACET_POLYGON, reader.FACET_POLYGON)
        self.assertEqual(EXPECTED_FACET_CONSTRAINT_ONLY,
                         reader.FACET_CONSTRAINT_ONLY)
        self.assertEqual(EXPECTED_FACET_KIND_COUNT, reader.FACET_KIND_COUNT)
        self.assertEqual(EXPECTED_INDEX_NONE, reader.INDEX_NONE)
        self.assertEqual(EXPECTED_ALIGNMENT, reader.ALIGNMENT)
        self.assertEqual(EXPECTED_HEADER_FIXED_BYTES, reader.HEADER_FIXED_BYTES)
        self.assertEqual(EXPECTED_DESCRIPTOR_BYTES, reader.DESCRIPTOR_BYTES)
        self.assertEqual(EXPECTED_HEADER_BYTES, reader.HEADER_BYTES)
        self.assertEqual(EXPECTED_CHECKSUM_OFFSET, reader.CHECKSUM_OFFSET)
        self.assertEqual(EXPECTED_IDENTITY_BYTES, reader.IDENTITY_BYTES)
        self.assertEqual(EXPECTED_SECTION_SPECS, reader.SECTION_SPECS)
        self.assertEqual(EXPECTED_MAX_CANONICAL_IMAGE_BYTES,
                         reader.MAX_CANONICAL_IMAGE_BYTES)

    def test_fixed_schema_oracle_detects_facet_width_and_cap_drift(self) -> None:
        for record_bytes, limit in ((59, 4_194_304), (60, 4_194_303)):
            specs = list(EXPECTED_SECTION_SPECS)
            specs[2] = ("facets", record_bytes, limit)
            with self.subTest(record_bytes=record_bytes, limit=limit):
                with mock.patch.object(reader, "SECTION_SPECS", tuple(specs)):
                    self.assertNotEqual(EXPECTED_SECTION_SPECS,
                                        reader.SECTION_SPECS)

    def test_extracts_identity_descriptors_and_named_counts(self) -> None:
        original = bytes(self.image)
        result = reader.inspect(self.image)
        self.assertEqual(EXPECTED_HEADER_BYTES, result.sections[0].offset)
        self.assertEqual(80, result.sections[1].record_bytes)
        self.assertEqual({"identity": 1, "cells": 2, "facets": 0,
                          "incidences": 0, "cell_incidences": 0,
                          "vertices": 3, "portals": 0,
                          "movement_fields": 0, "weapon_regions": 0,
                          "weapon_profiles": 0, "weapon_kernels": 0,
                          "analytic_function_refs": 0,
                          "analytic_functions": 0,
                          "analytic_input_dimensions": 0,
                          "analytic_constants": 4,
                          "analytic_affines": 0,
                          "analytic_affine_slopes": 0,
                          "analytic_polynomials": 0,
                          "analytic_polynomial_coefficients": 0,
                          "analytic_ballistics": 0,
                          "analytic_piecewise": 0,
                          "analytic_piecewise_clauses": 0,
                          "mechanisms": 0, "mechanism_edges": 0,
                          "landmarks": 0, "landmark_cells": 0,
                          "facet_annotations": 0,
                          "portal_mechanisms": 0}, result.counts)
        self.assertEqual(bytes(range(32)), result.identity.bsp_sha256)
        self.assertEqual(0x0102030405060708, result.identity.bsp_bytes)
        self.assertEqual((1, 2, 3, 4, 5, 6, 7),
                         tuple(result.identity.source_counts.__dict__.values()))
        self.assertEqual((-1, -2, -3), result.identity.standing_hull.mins)
        self.assertEqual((10, 11, 12), result.identity.crouching_hull.maxs)
        self.assertEqual(19, result.identity.physics.substep_ms)
        self.assertEqual(original, bytes(self.image))

    def test_accepts_polygon_facet_kind(self) -> None:
        image = _facet_fixture(EXPECTED_FACET_POLYGON, 3, 0,
                               EXPECTED_INDEX_NONE)
        result = reader.inspect(image)
        self.assertEqual(60, result.sections[2].record_bytes)
        self.assertEqual(1, result.counts["facets"])

    def test_accepts_constraint_only_facet_kind(self) -> None:
        image = _facet_fixture(EXPECTED_FACET_CONSTRAINT_ONLY, 0, 1,
                               EXPECTED_INDEX_NONE)
        result = reader.inspect(image)
        self.assertEqual(1, result.counts["facets"])

    def test_rejects_unknown_or_malformed_facet_kind(self) -> None:
        cases = (
            (EXPECTED_FACET_KIND_COUNT, 3, 0, EXPECTED_INDEX_NONE),
            (EXPECTED_FACET_POLYGON, 2, 0, EXPECTED_INDEX_NONE),
            (EXPECTED_FACET_CONSTRAINT_ONLY, 1, 1, EXPECTED_INDEX_NONE),
            (EXPECTED_FACET_CONSTRAINT_ONLY, 0, 0, EXPECTED_INDEX_NONE),
            (EXPECTED_FACET_CONSTRAINT_ONLY, 0, 1, 0),
        )
        for kind, vertex_count, incidence_count, portal in cases:
            with self.subTest(kind=kind, vertex_count=vertex_count,
                              incidence_count=incidence_count,
                              portal=portal):
                self.assert_rejected(
                    _facet_fixture(kind, vertex_count, incidence_count,
                                   portal), "invalid-format")

    def test_accepts_fixed_canonical_c_codec_fixture(self) -> None:
        self.assertEqual(EXPECTED_C_FIXTURE_BYTES,
                         len(C_CODEC_CANONICAL_FIXTURE))
        self.assertEqual(EXPECTED_C_FIXTURE_SHA256,
                         hashlib.sha256(C_CODEC_CANONICAL_FIXTURE).hexdigest())
        self.assertEqual(EXPECTED_C_FIXTURE_IDENTITY,
                         C_CODEC_CANONICAL_FIXTURE[
                             EXPECTED_HEADER_BYTES:
                             EXPECTED_HEADER_BYTES + EXPECTED_IDENTITY_BYTES])

        result = reader.inspect(C_CODEC_CANONICAL_FIXTURE,
                                expected_identity=EXPECTED_C_FIXTURE_IDENTITY)

        self.assertEqual(EXPECTED_C_FIXTURE_BYTES, result.image_bytes)
        self.assertEqual(EXPECTED_C_FIXTURE_CHECKSUM, result.checksum)
        self.assertEqual(EXPECTED_C_FIXTURE_CHECKSUM,
                         reader._checksum(memoryview(C_CODEC_CANONICAL_FIXTURE)))
        self.assertEqual(EXPECTED_C_FIXTURE_COUNTS, result.counts)
        self.assertEqual(b"\x5a" + b"\0" * 31, result.identity.bsp_sha256)
        self.assertEqual(1_024, result.identity.bsp_bytes)
        self.assertEqual(0x101, result.identity.bsp_checksum)
        self.assertEqual(0x102, result.identity.entity_crc32)
        self.assertEqual(0x50524F4455434552, result.identity.producer_identity)

    def test_checksum_does_not_copy_the_image(self) -> None:
        image_bytes = 32 * 1024 * 1024
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "sparse.rune"
            with artifact.open("wb") as file:
                file.truncate(image_bytes)
            with artifact.open("rb") as file:
                mapped = mmap.mmap(file.fileno(), image_bytes,
                                   access=mmap.ACCESS_READ)
                image = memoryview(mapped)
                tracemalloc.start()
                try:
                    reader._checksum(image)
                    _, peak = tracemalloc.get_traced_memory()
                finally:
                    tracemalloc.stop()
                    image.release()
                    mapped.close()

        self.assertLess(peak, image_bytes // 64)

    def test_read_preflights_the_canonical_maximum_before_reading(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "oversized.rune"
            with artifact.open("wb") as file:
                file.truncate(EXPECTED_MAX_CANONICAL_IMAGE_BYTES + 1)
            with mock.patch.object(reader.Path, "read_bytes",
                                   side_effect=AssertionError("read_bytes called")):
                with self.assertRaisesRegex(reader.RuneCompactError,
                                            "^limit-exceeded$"):
                    reader.read(artifact)

    def test_read_uses_one_descriptor_when_path_is_replaced(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "artifact.rune"
            path_replacement = Path(directory) / "path-replacement.rune"
            descriptor_replacement = Path(directory) / "descriptor-replacement.rune"
            artifact.write_bytes(self.image)
            path_replacement.write_bytes(b"path replacement")
            descriptor_replacement.write_bytes(b"descriptor replacement")

            def replace_after_path_stat():
                info = os.stat(artifact)
                os.replace(path_replacement, artifact)
                return info

            original_fstat = reader.os.fstat
            replaced = False

            def replace_after_descriptor_stat(fd):
                nonlocal replaced
                info = original_fstat(fd)
                if not replaced:
                    os.replace(descriptor_replacement, artifact)
                    replaced = True
                return info

            with mock.patch.object(reader.Path, "stat",
                                   side_effect=replace_after_path_stat), \
                 mock.patch.object(reader.os, "fstat",
                                   side_effect=replace_after_descriptor_stat):
                result = reader.read(artifact)

        self.assertEqual(len(self.image), result.image_bytes)

    def test_read_rejects_growth_after_bounded_read(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "growing.rune"
            artifact.write_bytes(self.image)
            original_read = reader.os.read
            grew = False

            def append_after_read(fd, count):
                nonlocal grew
                payload = original_read(fd, count)
                if not grew:
                    with artifact.open("ab") as stream:
                        stream.write(b"growth")
                    grew = True
                return payload

            with mock.patch.object(reader.os, "read",
                                   side_effect=append_after_read):
                with self.assertRaisesRegex(reader.RuneCompactError,
                                            "^changed$"):
                    reader.read(artifact)

    def test_read_request_is_bounded_and_uses_the_fstat_descriptor(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "bounded.rune"
            artifact.write_bytes(self.image)
            with mock.patch.object(reader.os, "open",
                                   wraps=reader.os.open) as open_file, \
                 mock.patch.object(reader.os, "read",
                                   wraps=reader.os.read) as read, \
                 mock.patch.object(reader.os, "fstat",
                                   wraps=reader.os.fstat) as fstat:
                result = reader.read(artifact)

        self.assertEqual(len(self.image), result.image_bytes)
        self.assertEqual(1, len(open_file.call_args_list))
        self.assertEqual(
            os.O_RDONLY | getattr(os, "O_BINARY", 0) |
            getattr(os, "O_NONBLOCK", 0), open_file.call_args.args[1])
        self.assertGreaterEqual(len(read.call_args_list), 2)
        descriptor = read.call_args_list[0].args[0]
        requests = [call.args[1] for call in read.call_args_list]
        self.assertEqual(len(self.image) + 1, requests[0])
        self.assertEqual(1, requests[-1])
        self.assertTrue(all(request <= reader.READ_CHUNK_BYTES
                            for request in requests))
        self.assertEqual({descriptor},
                         {call.args[0] for call in fstat.call_args_list})

    def test_read_continues_after_legal_short_reads(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "short-reads.rune"
            artifact.write_bytes(self.image)
            original_read = reader.os.read
            requests = []

            def short_read(fd, count):
                requests.append(count)
                return original_read(fd, min(count, 1))

            with mock.patch.object(reader.os, "read", side_effect=short_read):
                result = reader.read(artifact)

        self.assertEqual(len(self.image), result.image_bytes)
        self.assertGreater(len(requests), 1)
        self.assertEqual(len(self.image) + 1, requests[0])
        self.assertTrue(all(0 < request <= reader.READ_CHUNK_BYTES
                            for request in requests))

    def test_read_rejects_eof_before_declared_size(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "early-eof.rune"
            artifact.write_bytes(self.image)
            requests = []

            def early_eof(_fd, count):
                requests.append(count)
                return b""

            with mock.patch.object(reader.os, "read", side_effect=early_eof):
                with self.assertRaisesRegex(reader.RuneCompactError,
                                            "^changed$"):
                    reader.read(artifact)

        self.assertEqual([len(self.image) + 1], requests)

    @unittest.skipUnless(resource is not None and
                         hasattr(resource, "RLIMIT_AS"),
                         "address-space limits require resource.RLIMIT_AS")
    def test_read_small_fixture_under_low_address_space(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "small.rune"
            artifact.write_bytes(self.image)
            limit = 256 * 1024 * 1024
            _, hard = resource.getrlimit(resource.RLIMIT_AS)
            if hard != resource.RLIM_INFINITY and hard < limit:
                self.skipTest("address-space hard limit is below probe limit")
            probe = (
                "import resource, sys; "
                "resource.setrlimit(resource.RLIMIT_AS, "
                f"({limit}, {hard})); "
                f"sys.path.insert(0, {str(ROOT / 'tools')!r}); "
                "import runecompactread; "
                "runecompactread.read(sys.argv[1])"
            )
            subprocess.run([sys.executable, "-c", probe, str(artifact)],
                           check=True)

    @unittest.skipUnless(hasattr(os, "mkfifo") and hasattr(os, "O_NONBLOCK"),
                         "FIFO requires POSIX nonblocking open")
    def test_read_rejects_fifo_without_waiting_for_payload(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "fifo.rune"
            os.mkfifo(artifact)
            with self.assertRaisesRegex(reader.RuneCompactError,
                                        "^unsupported-file$"):
                reader.read(artifact)

    def test_rejects_truncation_and_checksum_mismatch(self) -> None:
        self.assert_rejected(self.image[:-1], "truncated")
        corrupted = bytearray(self.image)
        corrupted[EXPECTED_HEADER_BYTES] ^= 1
        self.assert_rejected(corrupted, "checksum-mismatch")

    def test_rejects_noncanonical_offset_and_padding(self) -> None:
        bad_offset = bytearray(self.image)
        cell_offset = _descriptor(1)
        struct.pack_into("<Q", bad_offset, cell_offset + 16,
                         struct.unpack_from("<Q", bad_offset, cell_offset + 16)[0] + 8)
        _checksum(bad_offset)
        self.assert_rejected(bad_offset, "invalid-section")

        bad_padding = bytearray(self.image)
        identity_end = EXPECTED_HEADER_BYTES + EXPECTED_SECTION_SPECS[0][1]
        bad_padding[identity_end] = 1
        _checksum(bad_padding)
        self.assert_rejected(bad_padding, "nonzero-reserved")

        bad_record_reserved = bytearray(self.image)
        cells = (EXPECTED_HEADER_BYTES + EXPECTED_SECTION_SPECS[0][1] + 7) & -8
        bad_record_reserved[cells + 77] = 1
        _checksum(bad_record_reserved)
        self.assert_rejected(bad_record_reserved, "nonzero-reserved")

    def test_rejects_all_unknown_version_fields(self) -> None:
        for offset, encoded, value in ((8, "<H", 1), (32, "<H", 1),
                                       (36, "<I", 0), (40, "<H", 2)):
            with self.subTest(offset=offset):
                image = bytearray(self.image)
                struct.pack_into(encoded, image, offset, value)
                _checksum(image)
                self.assert_rejected(image, "unsupported-version")

    def test_rejects_invalid_span(self) -> None:
        image = bytearray(self.image)
        struct.pack_into("<II", image, _section_offset(image, 1) + 44, 0, 1)
        _checksum(image)
        self.assert_rejected(image, "invalid-span")

    def test_rejects_invalid_cross_section_reference(self) -> None:
        image = _fixture(cells=1, movement_fields=1)
        field = _section_offset(image, 7)
        struct.pack_into("<I", image, field + 4, 0xFFFFFFFF)
        _checksum(image)
        reader.inspect(image)

        struct.pack_into("<I", image, field, 1)
        _checksum(image)
        self.assert_rejected(image, "invalid-reference")

    def test_rejects_invalid_enum_and_mask(self) -> None:
        image = bytearray(self.image)
        image[_section_offset(image, 1) + 76] = 4
        _checksum(image)
        self.assert_rejected(image, "invalid-format")

    def test_rejects_bad_source_union_padding_and_reference(self) -> None:
        image = _facet_fixture(EXPECTED_FACET_POLYGON, 3, 0,
                               EXPECTED_INDEX_NONE)
        facet = _section_offset(image, 2)
        struct.pack_into("<I", image, facet + 52, EXPECTED_INDEX_NONE)
        _checksum(image)
        reader.inspect(image)

        image[facet + 12] = 1
        _checksum(image)
        self.assert_rejected(image, "invalid-reference")

        image = _facet_fixture(EXPECTED_FACET_POLYGON, 3, 0,
                               EXPECTED_INDEX_NONE)
        facet = _section_offset(image, 2)
        struct.pack_into("<III", image, facet, 3, 1, 0)
        struct.pack_into("<I", image, facet + 52, EXPECTED_INDEX_NONE)
        _checksum(image)
        self.assert_rejected(image, "invalid-reference")

    def test_analytic_definition_targets_its_declared_form_section(self) -> None:
        image = _fixture(analytic_functions=1, analytic_constants=1)
        function = _section_offset(image, 12)
        _checksum(image)
        reader.inspect(image)

        struct.pack_into("<I", image, function + 16, 1)
        _checksum(image)
        self.assert_rejected(image, "invalid-reference")

    def test_accepts_incomplete_multifamily_weapon_collection(self) -> None:
        image = _fixture(cells=1, weapon_regions=1, weapon_profiles=1,
                         weapon_kernels=1, analytic_function_refs=2,
                         analytic_functions=1, analytic_constants=1)
        region = _section_offset(image, 8)
        profile = _section_offset(image, 9)
        kernel = _section_offset(image, 10)
        struct.pack_into("<II", image, region + 12, 0, 1)
        struct.pack_into("<II", image, profile, 1, (1 << 2) | (1 << 7))
        struct.pack_into("<IIIII", image, kernel, 0, 0, 7, 0, 2)
        _checksum(image)

        result = reader.inspect(image)
        self.assertEqual(1, result.counts["weapon_kernels"])

    def test_rejects_invalid_weapon_profiles_and_kernels(self) -> None:
        image = _fixture(cells=1, weapon_regions=1, weapon_profiles=1,
                         weapon_kernels=1, analytic_function_refs=1,
                         analytic_functions=1, analytic_constants=1)
        region = _section_offset(image, 8)
        profile = _section_offset(image, 9)
        kernel = _section_offset(image, 10)
        struct.pack_into("<II", image, region + 12, 0, 1)
        struct.pack_into("<II", image, profile, 1, 1 << 2)
        struct.pack_into("<IIIII", image, kernel, 0, 0, 2, 0, 1)

        for source, mask in ((0, 1 << 2), (1, 0), (1, 1 << 12)):
            with self.subTest(source=source, mask=mask):
                corrupted = bytearray(image)
                struct.pack_into("<II", corrupted, profile, source, mask)
                _checksum(corrupted)
                self.assert_rejected(corrupted, "invalid-format")

        for family, code in ((12, "invalid-format"),
                             (7, "invalid-reference")):
            with self.subTest(family=family):
                corrupted = bytearray(image)
                struct.pack_into("<I", corrupted, kernel + 8, family)
                _checksum(corrupted)
                self.assert_rejected(corrupted, code)

        corrupted = bytearray(image)
        struct.pack_into("<II", corrupted, kernel + 12, 1, 1)
        _checksum(corrupted)
        self.assert_rejected(corrupted, "invalid-span")

        for offset in (kernel, kernel + 4):
            with self.subTest(offset=offset):
                corrupted = bytearray(image)
                struct.pack_into("<I", corrupted, offset, 1)
                _checksum(corrupted)
                self.assert_rejected(corrupted, "invalid-reference")

        corrupted = bytearray(image)
        function_ref = _section_offset(corrupted, 11)
        struct.pack_into("<I", corrupted, function_ref, 1)
        _checksum(corrupted)
        self.assert_rejected(corrupted, "invalid-reference")

    def test_optionally_binds_exact_identity(self) -> None:
        expected_identity = _identity(self.image)
        self.assertEqual(expected_identity, _identity(self.image))
        reader.inspect(self.image, expected_identity=bytearray(expected_identity))

        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "rune.compact"
            artifact.write_bytes(self.image)
            reader.read(artifact, expected_identity=expected_identity)
            with self.assertRaisesRegex(reader.RuneCompactError,
                                        "^invalid-argument$"):
                reader.read(artifact, expected_identity=expected_identity[:-1])

        self.assert_expected_identity_rejected(expected_identity[:-1],
                                               "invalid-argument")
        self.assert_expected_identity_rejected(expected_identity + b"\\0",
                                               "invalid-argument")
        self.assert_expected_identity_rejected("not-bytes", "invalid-argument")
        mismatch = bytearray(expected_identity)
        mismatch[0] ^= 1
        self.assert_expected_identity_rejected(mismatch, "identity-mismatch")


if __name__ == "__main__":
    unittest.main()
