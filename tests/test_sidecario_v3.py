#!/usr/bin/env python3
"""Golden, mutation, producer, and tombstone tests for v3 sidecars."""

from __future__ import annotations

import contextlib
import hashlib
import io
import itertools
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zlib


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import corpusgraph  # noqa: E402
import defbake  # noqa: E402
import demodefense  # noqa: E402
import demorune  # noqa: E402
import escapebake  # noqa: E402
import flaglivebake  # noqa: E402
import humanbake  # noqa: E402
import mapflags  # noqa: E402
import rune_contracts_generated as contract  # noqa: E402
import runeio  # noqa: E402
import sidecario  # noqa: E402


def _fixture(name):
    return bytes.fromhex(
        (ROOT / "tests" / "fixtures" / name).read_text(encoding="ascii")
    )


def _fix_header_crc(data):
    struct.pack_into("<I", data, sidecario.HEADER_CRC_OFFSET, 0)
    crc = zlib.crc32(data[:sidecario.HEADER_BYTES]) & 0xFFFFFFFF
    struct.pack_into("<I", data, sidecario.HEADER_CRC_OFFSET, crc)


def _identity(map_name):
    return runeio.RuneIdentityV3(
        map_name=map_name,
        bsp_checksum=0x12345678,
        entity_crc32=0x9ABCDEF0,
        gravity=650.0,
        airaccelerate=0.0,
        maxvelocity=2000.0,
        host_physics_id=1,
    )


def _rune_bytes(map_name="sidecartest", *, with_tombstone=False):
    if with_tombstone:
        seeds = (
            runeio.RuneSeedV3((0.0, 0.0, 0.0)),
            runeio.RuneSeedV3(
                (128.0, 0.0, 0.0), flags=runeio.RSF_TOMBSTONE
            ),
            runeio.RuneSeedV3((256.0, 0.0, 0.0)),
        )
        pairs = ((0, 2), (2, 0))
    else:
        seeds = (
            runeio.RuneSeedV3((0.0, 0.0, 0.0)),
            runeio.RuneSeedV3((128.0, 0.0, 0.0)),
        )
        pairs = ((0, 1), (1, 0))
    links = tuple(
        runeio.RuneLinkV3(
            source, destination, contract.RL_RUN, contract.RL_PROVEN,
            0, 0, 0, 0, 100,
        )
        for source, destination in pairs
    )
    return runeio.encode_v3(_identity(map_name), seeds, links)


class SidecarCodecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.golden_rune = _fixture("rune_v3_wire_golden.hex")
        cls.golden_hmn = _fixture("sidecar_v3_hmn_golden.hex")
        cls.binding = sidecario.RuneBindingV3(
            rune_version=3,
            num_seeds=2,
            num_links=2,
            rune_payload_crc32=0xE3D0AC5F,
            action_contract_crc32=contract.CONTRACT_CRC32,
            rune_header_crc32=0xDDC378D1,
        )

    def assert_symbol(self, symbol, operation):
        with self.assertRaises(sidecario.SidecarError) as caught:
            operation()
        self.assertEqual(symbol, caught.exception.symbol)
        self.assertEqual(symbol, caught.exception.diagnostic.symbol)
        self.assertEqual(caught.exception.message,
                         caught.exception.diagnostic.message)
        self.assertIn(symbol, str(caught.exception))

    def mutate_header(self, offset, fmt, value):
        data = bytearray(self.golden_hmn)
        struct.pack_into(fmt, data, offset, value)
        _fix_header_crc(data)
        return bytes(data)

    def decode_golden(self, data, **kwargs):
        kwargs.setdefault("expected_binding", self.binding)
        kwargs.setdefault("expected_kind", sidecario.HMN)
        return sidecario.decode_v3(data, **kwargs)

    def test_shared_golden_header_and_round_trip(self):
        self.assertEqual(48, sidecario.HEADER_STRUCT.size)
        self.assertEqual(50, len(self.golden_hmn))
        self.assertEqual(
            "d1fea1818e96c0e75ddf6f722abab22c4d2ea5523dcb5f09fe1faa20259faf81",
            hashlib.sha256(self.golden_hmn).hexdigest(),
        )
        self.assertEqual(b"HMN3", self.golden_hmn[:4])
        self.assertEqual(bytes((7, 200)), self.golden_hmn[48:])
        self.assertEqual(
            (
                0x334E4D48, 1, 48, 3, 1, 1, 0, 2, 2,
                0xE3D0AC5F, contract.CONTRACT_CRC32, 0xDDC378D1,
                2, 0x9B27CEBA, 0xD29491BD,
            ),
            sidecario.HEADER_STRUCT.unpack_from(self.golden_hmn),
        )
        self.assertEqual(
            self.golden_hmn,
            sidecario.encode_v3(sidecario.HMN, self.binding, bytes((7, 200))),
        )
        decoded = self.decode_golden(
            self.golden_hmn,
            expected_kind=sidecario.HMN,
        )
        self.assertEqual(self.binding, decoded.header.binding)
        self.assertEqual(bytes((7, 200)), decoded.payload)

    def test_diagnostic_domain_is_stable_and_ordered(self):
        expected = (
            "SCD_OK", "SCD_ABSENT", "SCD_INVALID_ARGUMENT",
            "SCD_PATH_TOO_LONG", "SCD_IO_ERROR", "SCD_BAD_MAGIC",
            "SCD_UNSUPPORTED_VERSION", "SCD_BAD_HEADER_SIZE",
            "SCD_BAD_RUNE_VERSION", "SCD_BAD_HEADER_CRC",
            "SCD_NONZERO_RESERVED", "SCD_BAD_SHAPE", "SCD_BAD_COUNTS",
            "SCD_BAD_PAYLOAD_SIZE", "SCD_BAD_FILE_SIZE",
            "SCD_RUNE_PAYLOAD_MISMATCH",
            "SCD_ACTION_CONTRACT_MISMATCH", "SCD_RUNE_HEADER_MISMATCH",
            "SCD_BAD_PAYLOAD_CRC", "SCD_BAD_PAYLOAD_VALUE",
            "SCD_ALLOCATION_FAILED", "SCD_TEMP_EXHAUSTED",
            "SCD_STATE_DRIFT", "SCD_INTERNAL_ERROR",
        )
        self.assertEqual(
            expected,
            tuple(diagnostic.symbol
                  for diagnostic in sidecario.SidecarDiagnostic),
        )
        self.assertEqual(
            tuple(range(len(expected))),
            tuple(int(diagnostic)
                  for diagnostic in sidecario.SidecarDiagnostic),
        )
        self.assertTrue(all(
            diagnostic.message
            for diagnostic in sidecario.SidecarDiagnostic
        ))
        self.assertEqual(len(expected), sidecario.SCD_DIAGNOSTIC_COUNT)

    def test_binding_can_be_derived_from_the_exact_rune_snapshot(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "lmctf07.rune"
            path.write_bytes(self.golden_rune)
            rune = corpusgraph.read_rune(path, "lmctf07", versions=(3,))
        self.assertEqual(self.binding, sidecario.binding_from_rune(rune))
        self.assert_symbol(
            "SCD_INVALID_ARGUMENT",
            lambda: sidecario.decode_v3(
                self.golden_hmn, expected_kind=sidecario.HMN
            ),
        )
        bad_magic = self.mutate_header(0, "<I", 0x01020304)
        self.assert_symbol(
            "SCD_INVALID_ARGUMENT",
            lambda: sidecario.decode_v3(
                bad_magic, expected_binding=self.binding,
                expected_kind="not-a-kind",
            ),
        )

    def test_header_and_payload_corruptions_fail_closed(self):
        bad_header_crc = bytearray(self.golden_hmn)
        bad_header_crc[sidecario.HEADER_CRC_OFFSET] ^= 1
        self.assert_symbol(
            "SCD_BAD_HEADER_CRC",
            lambda: self.decode_golden(bad_header_crc),
        )

        bad_payload_crc = bytearray(self.golden_hmn)
        bad_payload_crc[-1] ^= 1
        self.assert_symbol(
            "SCD_BAD_PAYLOAD_CRC",
            lambda: self.decode_golden(bad_payload_crc),
        )
        self.assert_symbol(
            "SCD_BAD_MAGIC",
            lambda: self.decode_golden(
                self.mutate_header(0, "<I", 0x01020304)
            ),
        )
        self.assert_symbol(
            "SCD_UNSUPPORTED_VERSION",
            lambda: self.decode_golden(self.mutate_header(4, "<H", 2)),
        )
        self.assert_symbol(
            "SCD_BAD_HEADER_SIZE",
            lambda: self.decode_golden(self.mutate_header(6, "<H", 47)),
        )
        self.assert_symbol(
            "SCD_BAD_SHAPE",
            lambda: self.decode_golden(self.mutate_header(10, "<H", 2)),
        )
        self.assert_symbol(
            "SCD_NONZERO_RESERVED",
            lambda: self.decode_golden(self.mutate_header(14, "<H", 1)),
        )
        self.assert_symbol(
            "SCD_BAD_RUNE_VERSION",
            lambda: self.decode_golden(self.mutate_header(8, "<H", 2)),
        )
        bad_rune_version = bytearray(self.golden_hmn)
        struct.pack_into("<H", bad_rune_version, 8, 2)
        self.assert_symbol(
            "SCD_BAD_RUNE_VERSION",
            lambda: self.decode_golden(bad_rune_version),
        )
        self.assert_symbol(
            "SCD_BAD_COUNTS",
            lambda: self.decode_golden(self.mutate_header(16, "<I", 0)),
        )
        self.assert_symbol(
            "SCD_BAD_COUNTS",
            lambda: self.decode_golden(self.mutate_header(20, "<I", 3)),
        )
        self.assert_symbol(
            "SCD_ACTION_CONTRACT_MISMATCH",
            lambda: self.decode_golden(self.mutate_header(28, "<I", 0)),
        )
        self.assert_symbol(
            "SCD_BAD_PAYLOAD_SIZE",
            lambda: self.decode_golden(self.mutate_header(36, "<I", 1)),
        )
        self.assert_symbol(
            "SCD_BAD_FILE_SIZE",
            lambda: self.decode_golden(self.golden_hmn + b"\0"),
        )
        self.assert_symbol(
            "SCD_BAD_FILE_SIZE",
            lambda: self.decode_golden(self.golden_hmn[:-1]),
        )
        self.assert_symbol(
            "SCD_BAD_HEADER_SIZE",
            lambda: self.decode_golden(self.golden_hmn[:47]),
        )

    def test_expected_binding_mismatch_is_rejected(self):
        other = sidecario.RuneBindingV3(
            3, 2, 2, 0x11111111, contract.CONTRACT_CRC32, 0xDDC378D1
        )
        self.assert_symbol(
            "SCD_RUNE_PAYLOAD_MISMATCH",
            lambda: self.decode_golden(
                self.golden_hmn, expected_binding=other
            ),
        )
        other = sidecario.RuneBindingV3(
            3, 2, 2, 0xE3D0AC5F, contract.CONTRACT_CRC32, 0x11111111
        )
        self.assert_symbol(
            "SCD_RUNE_HEADER_MISMATCH",
            lambda: self.decode_golden(
                self.golden_hmn, expected_binding=other
            ),
        )

    def test_dpo_rejects_nonzero_tombstone_plane(self):
        binding = sidecario.RuneBindingV3(
            3, 3, 2, 1, contract.CONTRACT_CRC32, 2
        )
        clean = bytes(12)
        encoded_clean = sidecario.encode_v3(
            sidecario.DPO, binding, clean, tombstone_indices=(1,)
        )
        sidecario.decode_v3(
            encoded_clean,
            expected_kind=sidecario.DPO,
            expected_binding=binding,
            tombstone_indices=(1,),
        )
        self.assert_symbol(
            "SCD_INVALID_ARGUMENT",
            lambda: sidecario.encode_v3(sidecario.DPO, binding, clean),
        )
        self.assert_symbol(
            "SCD_INVALID_ARGUMENT",
            lambda: sidecario.decode_v3(
                encoded_clean, expected_kind=sidecario.DPO,
                expected_binding=binding,
            ),
        )
        self.assert_symbol(
            "SCD_INVALID_ARGUMENT",
            lambda: sidecario.encode_v3(
                sidecario.DPO, binding, clean,
                tombstone_indices=itertools.repeat(0),
            ),
        )
        dirty = bytearray(clean)
        dirty[1 + 2 * binding.num_seeds] = 1
        self.assert_symbol(
            "SCD_BAD_PAYLOAD_VALUE",
            lambda: sidecario.encode_v3(
                sidecario.DPO, binding, dirty, tombstone_indices=(1,)
            ),
        )
        encoded_dirty = sidecario.encode_v3(
            sidecario.DPO, binding, dirty, tombstone_indices=()
        )
        self.assert_symbol(
            "SCD_BAD_PAYLOAD_VALUE",
            lambda: sidecario.decode_v3(
                encoded_dirty, expected_kind=sidecario.DPO,
                expected_binding=binding,
                tombstone_indices=(1,),
            ),
        )

    def test_danger_is_explicit_le_bounded_and_tombstone_safe(self):
        binding = sidecario.RuneBindingV3(
            3, 2, 0, 1, contract.CONTRACT_CRC32, 2
        )
        encoded = sidecario.encode_danger(
            binding, (0, 8000), (7, 9), tombstone_indices=()
        )
        self.assertEqual(
            struct.pack("<4i", 0, 8000, 7, 9), encoded[48:]
        )
        self.assertEqual(
            ((0, 8000), (7, 9)), sidecario.decode_danger(
                encoded, expected_binding=binding, tombstone_indices=()
            )
        )
        self.assert_symbol(
            "SCD_BAD_PAYLOAD_VALUE",
            lambda: sidecario.encode_danger(
                binding, (0, 8001), (0, 0), tombstone_indices=()
            ),
        )
        self.assert_symbol(
            "SCD_BAD_PAYLOAD_VALUE",
            lambda: sidecario.encode_danger(
                binding, (0, 1), (0, 0), tombstone_indices=(1,)
            ),
        )
        self.assert_symbol(
            "SCD_BAD_PAYLOAD_VALUE",
            lambda: sidecario.decode_danger(
                encoded, expected_binding=binding, tombstone_indices=(1,)
            ),
        )


class SidecarPipelineTests(unittest.TestCase):
    def _write_graph(self, root, map_name="sidecartest", **kwargs):
        maps = Path(root) / "maps"
        maps.mkdir(parents=True, exist_ok=True)
        path = maps / f"{map_name}.rune"
        path.write_bytes(_rune_bytes(map_name, **kwargs))
        rune = corpusgraph.read_rune(path, map_name, versions=(3,))
        return path, rune

    def _write_json(self, directory, name, document):
        directory = Path(directory)
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / name
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    def test_all_four_bakers_emit_bound_v3_sidecars(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rune_path, rune = self._write_graph(root)
            identity = corpusgraph.rune_identity_from_rune(rune)
            corpus_dir = root / "corpus"
            traffic = corpusgraph.stamp_corpus_identity(
                {"map": "sidecartest", "transitions": {"0>1": 9}},
                identity,
            )
            defense = corpusgraph.stamp_corpus_identity(
                {
                    "map": "sidecartest",
                    "dwell_seed": {"red": {"0": 9}, "blue": {}},
                    "intercept_seed": {"red": {}, "blue": {"1": 4}},
                },
                identity,
            )
            for suffix in ("human", "flaglive", "escape"):
                self._write_json(
                    corpus_dir, f"sidecartest.{suffix}.json", traffic
                )
            self._write_json(
                corpus_dir, "sidecartest.defense.json", defense
            )

            binding = sidecario.binding_from_rune(rune)
            operations = (
                (humanbake, sidecario.HMN, ".hmn"),
                (flaglivebake, sidecario.HML, ".hml"),
                (escapebake, sidecario.HME, ".hme"),
                (defbake, sidecario.DPO, ".dpo"),
            )
            with contextlib.redirect_stdout(io.StringIO()):
                for module, kind, extension in operations:
                    module.bake_map(str(root), str(corpus_dir), "sidecartest")
                    decoded = sidecario.decode_v3(
                        rune_path.with_suffix(extension).read_bytes(),
                        expected_kind=kind,
                        expected_binding=binding,
                        tombstone_indices=(() if kind.axis == "seed" else None),
                    )
                    self.assertTrue(any(decoded.payload))

    def test_unstamped_corpora_fail_before_replacing_existing_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rune_path, _ = self._write_graph(root)
            corpus_dir = root / "corpus"
            unstamped_traffic = {
                "map": "sidecartest", "transitions": {"0>1": 1}
            }
            unstamped_defense = {
                "map": "sidecartest",
                "dwell_seed": {"red": {}, "blue": {}},
                "intercept_seed": {"red": {}, "blue": {}},
            }
            cases = (
                (humanbake, "human", ".hmn", unstamped_traffic),
                (flaglivebake, "flaglive", ".hml", unstamped_traffic),
                (escapebake, "escape", ".hme", unstamped_traffic),
                (defbake, "defense", ".dpo", unstamped_defense),
            )
            for module, suffix, extension, document in cases:
                with self.subTest(kind=extension):
                    self._write_json(
                        corpus_dir, f"sidecartest.{suffix}.json", document
                    )
                    output = rune_path.with_suffix(extension)
                    output.write_bytes(b"preserve-me")
                    with self.assertRaisesRegex(ValueError, "re-mine"):
                        module.bake_map(
                            str(root), str(corpus_dir), "sidecartest"
                        )
                    self.assertEqual(b"preserve-me", output.read_bytes())

    def test_rune_drift_before_atomic_commit_preserves_existing_sidecar(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rune_path, rune = self._write_graph(root)
            corpus_dir = root / "corpus"
            document = corpusgraph.stamp_corpus_identity(
                {"map": "sidecartest", "transitions": {"0>1": 1}},
                corpusgraph.rune_identity_from_rune(rune),
            )
            self._write_json(
                corpus_dir, "sidecartest.human.json", document
            )
            output = rune_path.with_suffix(".hmn")
            output.write_bytes(b"preserve-me")

            def replace_rune_before_write(path, payload, *, precommit=None):
                rune_path.write_bytes(
                    _rune_bytes("sidecartest", with_tombstone=True)
                )
                return corpusgraph.atomic_write_bytes(
                    path, payload, precommit=precommit
                )

            with mock.patch.object(
                    humanbake, "atomic_write_bytes",
                    side_effect=replace_rune_before_write):
                with self.assertRaises(sidecario.SidecarError) as caught:
                    humanbake.bake_map(
                        str(root), str(corpus_dir), "sidecartest"
                    )
            self.assertEqual(
                sidecario.SCD_STATE_DRIFT, caught.exception.diagnostic
            )
            self.assertEqual(b"preserve-me", output.read_bytes())
            self.assertEqual([], list(output.parent.glob(".sidecartest.hmn.*.tmp")))

    def test_uncached_rollup_read_observes_rune_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            rune_path, original = self._write_graph(temporary)
            cached, _, _ = mapflags.load_graph_metadata(
                rune_path, "sidecartest"
            )
            self.assertEqual(
                corpusgraph.rune_identity_from_rune(original),
                corpusgraph.rune_identity_from_rune(cached),
            )
            rune_path.write_bytes(
                _rune_bytes("sidecartest", with_tombstone=True)
            )
            still_cached, _, _ = mapflags.load_graph_metadata(
                rune_path, "sidecartest"
            )
            refreshed, _, live = mapflags.read_graph_metadata(
                rune_path, "sidecartest"
            )
            self.assertIs(cached, still_cached)
            self.assertNotEqual(
                corpusgraph.rune_identity_from_rune(cached),
                corpusgraph.rune_identity_from_rune(refreshed),
            )
            self.assertEqual(frozenset((0, 2)), live)

    def test_tombstones_keep_indices_but_are_never_localized_or_baked(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rune_path, rune = self._write_graph(root, with_tombstone=True)
            origins, live, identity = demorune.load_seed_graph(
                rune_path, "sidecartest"
            )
            self.assertEqual((0, 2), live)
            self.assertEqual((128.0, 0.0, 0.0), origins[1])
            self.assertEqual(
                -1, demorune.SeedGrid(origins, live).nearest(origins[1])
            )
            _, map_origins, map_live = mapflags.load_graph_metadata(
                rune_path, "sidecartest"
            )
            self.assertEqual(frozenset((0, 2)), map_live)
            self.assertEqual(
                -1, mapflags.nearest(map_origins, origins[1], map_live)[0]
            )
            self.assertEqual(
                -1,
                demodefense.SeedGrid(origins, live).nearest(origins[1]),
            )

            corpus_dir = root / "corpus"
            defense = corpusgraph.stamp_corpus_identity(
                {
                    "map": "sidecartest",
                    "dwell_seed": {"red": {"1": 1}, "blue": {}},
                    "intercept_seed": {"red": {}, "blue": {}},
                },
                identity,
            )
            self._write_json(
                corpus_dir, "sidecartest.defense.json", defense
            )
            output = rune_path.with_suffix(".dpo")
            output.write_bytes(b"preserve-me")
            with self.assertRaisesRegex(
                    sidecario.SidecarError, "tombstone seed 1"):
                defbake.bake_map(str(root), str(corpus_dir), "sidecartest")
            self.assertEqual(b"preserve-me", output.read_bytes())

    def test_v3_corpus_identity_is_case_sensitive_and_world_bound(self):
        with tempfile.TemporaryDirectory() as temporary:
            _, rune = self._write_graph(temporary)
            identity = corpusgraph.rune_identity_from_rune(rune)
        document = corpusgraph.stamp_corpus_identity(
            {"map": "sidecartest"}, identity
        )
        corpusgraph.require_corpus_identity(document, "memory", identity)
        wrong_case = dict(document, map="SIDECARTEST")
        with self.assertRaisesRegex(ValueError, "map identity"):
            corpusgraph.require_corpus_identity(
                wrong_case, "memory", identity
            )
        missing_world = dict(document)
        del missing_world["rune_bsp_checksum"]
        with self.assertRaisesRegex(ValueError, "rune_bsp_checksum"):
            corpusgraph.require_corpus_identity(
                missing_world, "memory", identity
            )


if __name__ == "__main__":
    unittest.main()
