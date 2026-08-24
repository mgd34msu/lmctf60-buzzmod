#!/usr/bin/env python3
"""Exercise RUNE/SNAG staging, cold-load proof, and pair recovery."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import runegen_pair
import runeio
from tests.test_rune_artifact import _build_rune, _fix_payload_and_header_crc


MAP = "runetest"


def _build_local_only_rune() -> bytes:
    encoded = bytearray(_build_rune())
    struct.pack_into("<H", encoded, 4, runeio.RUNE_ROUTE_CONTRACT_LOCAL_ONLY)
    struct.pack_into(
        "<H", encoded, runeio.RUNE_HEADER_BYTES + 14, runeio.RSF_OBJECTIVE
    )
    struct.pack_into(
        "<H", encoded,
        runeio.RUNE_HEADER_BYTES + runeio.RUNE_SEED_BYTES + 14,
        runeio.RSF_OBJECTIVE,
    )
    _fix_payload_and_header_crc(encoded)
    return bytes(encoded)


class RunegenPairTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.stage_maps = self.root / "stage/maps"
        self.stage_maps.mkdir(parents=True)
        self.rune = self.stage_maps / f"{MAP}.rune"
        self.rune.write_bytes(_build_rune())
        self.engine = self.root / "q2ded"
        self.config = self.root / "rune.cfg"
        self.module_primary = self.root / "game.so"
        self.module_secondary = self.root / "gamex86_64.so"
        for path, payload in (
            (self.engine, b"engine"),
            (self.config, b"config"),
            (self.module_primary, b"module"),
            (self.module_secondary, b"module"),
        ):
            path.write_bytes(payload)
        self.provenance = self.root / "provenance.json"
        self.evidence = self.root / "evidence.json"
        self.manifest = self.root / "pair.json"
        runegen_pair.write_provenance(
            MAP,
            self.rune,
            self.engine,
            self.config,
            (self.module_primary, self.module_secondary),
            16,
            {"seeds": 2, "links": 2, "mechanism_nodes": 3, "plans": 1},
            self.provenance,
        )

    def tearDown(self):
        self.temporary.cleanup()

    def stage(self):
        return runegen_pair.stage_explicit_zero_pair(
            MAP,
            self.stage_maps,
            self.evidence,
            self.provenance,
            self.manifest,
        )

    def ready_log(self, *, snag_line: str | None = None) -> Path:
        pair = runegen_pair.validate_pair(self.manifest)
        if snag_line is None:
            snag_line = (
                f"slipgate: snag ready map={MAP} repairs=0 "
                f"rune_sha256={pair.rune.sha256} "
                f"evidence_sha256={pair.evidence.sha256} "
                f"snag_sha256={pair.snag.sha256}"
            )
        log = self.root / "cold.log"
        log.write_text(
            snag_line
            + "\n"
            + f"slipgate: rune ready {MAP}, 2 seeds, 2 links, "
            "3 mechanism nodes, 1 plans, gravity 650, all fields up\n",
            encoding="utf-8",
        )
        return log

    def test_stage_writes_canonical_evidence_and_exact_zero_snag(self):
        pair = self.stage()
        evidence = {
            "artifact_sha256": pair.rune.sha256,
            "classification": "NO_ACCEPTED_OBSERVATION",
            "fingerprint": pair.fingerprint,
            "format": "lmctf-snag-bootstrap-v1",
            "map": MAP,
        }
        self.assertEqual(
            json.dumps(evidence, sort_keys=True, separators=(",", ":")) + "\n",
            self.evidence.read_text(encoding="utf-8"),
        )
        fields = dict(
            line.split(" ", 1)
            for line in pair.snag.path.read_text(encoding="ascii").splitlines()
        )
        self.assertEqual("0", fields["repairs"])
        self.assertEqual(pair.rune.sha256, fields["rune_sha256"])
        self.assertEqual(pair.evidence.sha256, fields["evidence_sha256"])
        self.assertEqual(
            pair.snag.sha256,
            hashlib.sha256(pair.snag.path.read_bytes()).hexdigest(),
        )

    def test_stage_rejects_existing_or_aliased_artifacts(self):
        snag = self.stage_maps / f"{MAP}.snag"
        snag.write_bytes(b"stale")
        with self.assertRaisesRegex(runegen_pair.PairError, "already exists"):
            self.stage()
        snag.unlink()
        rune_alias = self.root / "rune-alias"
        os.link(self.rune, rune_alias)
        with self.assertRaisesRegex(runegen_pair.PairError, "unaliased"):
            self.stage()

    def test_validate_rejects_missing_or_changed_pair_member(self):
        self.stage()
        snag = self.stage_maps / f"{MAP}.snag"
        snag.unlink()
        with self.assertRaisesRegex(runegen_pair.PairError, "snag"):
            runegen_pair.validate_pair(self.manifest)
        self.assertFalse(self.evidence.is_symlink())

    def test_verify_cold_load_requires_ordered_exact_attestation(self):
        pair = self.stage()
        good = self.ready_log()
        runegen_pair.verify_cold_load(self.manifest, good)

        lines = good.read_text(encoding="utf-8").splitlines()
        cases = {
            "missing": lines[1:],
            "duplicate": [lines[0], lines[0], lines[1]],
            "reversed": [lines[1], lines[0]],
            "write": [lines[0], "rune: wrote bad", lines[1]],
            "wrong-rune": [lines[0].replace(pair.rune.sha256, "0" * 64), lines[1]],
            "wrong-evidence": [
                lines[0].replace(pair.evidence.sha256, "0" * 64), lines[1]
            ],
            "wrong-snag": [lines[0].replace(pair.snag.sha256, "0" * 64), lines[1]],
        }
        for name, contents in cases.items():
            with self.subTest(name=name):
                bad = self.root / f"{name}.log"
                bad.write_text("\n".join(contents) + "\n", encoding="utf-8")
                with self.assertRaises(runegen_pair.PairError):
                    runegen_pair.verify_cold_load(self.manifest, bad)

    def test_verify_cold_load_rehashes_pair_and_frozen_inputs(self):
        self.stage()
        good = self.ready_log()
        for path in (
            self.rune,
            self.stage_maps / f"{MAP}.snag",
            self.module_primary,
        ):
            with self.subTest(path=path.name):
                before = path.read_bytes()
                path.chmod(0o644)
                path.write_bytes(before + b"changed")
                with self.assertRaises(runegen_pair.PairError):
                    runegen_pair.verify_cold_load(self.manifest, good)
                path.write_bytes(before)

    def _live_pair(self, snag: bytes | None = b"old-snag") -> Path:
        live = self.root / "live/maps"
        live.mkdir(parents=True, exist_ok=True)
        (live / f"{MAP}.rune").write_bytes(b"old-rune")
        if snag is not None:
            (live / f"{MAP}.snag").write_bytes(snag)
        else:
            (live / f"{MAP}.snag").unlink(missing_ok=True)
        return live

    def test_install_commits_snag_then_rune_and_preserves_backup(self):
        pair = self.stage()
        live = self._live_pair()
        backup = self.root / "backups"
        order = []
        real_replace = os.replace

        def recording_replace(source, destination):
            if Path(destination).parent == live and Path(destination).suffix in {
                ".rune",
                ".snag",
            }:
                order.append(Path(destination).suffix)
            return real_replace(source, destination)

        with mock.patch.object(runegen_pair.os, "replace", recording_replace):
            record = runegen_pair.install_pair(pair, live, backup)
        self.assertEqual([".snag", ".rune"], order[-2:])
        self.assertEqual(pair.rune.path.read_bytes(), (live / f"{MAP}.rune").read_bytes())
        self.assertEqual(pair.snag.path.read_bytes(), (live / f"{MAP}.snag").read_bytes())
        backup_manifest = json.loads(record.manifest.read_text(encoding="utf-8"))
        self.assertEqual("old-rune", (record.directory / "old.rune").read_text())
        self.assertEqual("old-snag", (record.directory / "old.snag").read_text())
        self.assertTrue(backup_manifest["old"]["rune"]["exists"])
        self.assertTrue(backup_manifest["old"]["snag"]["exists"])

    def test_complete_pair_coherently_replaces_local_only_pair(self):
        self.rune.write_bytes(_build_local_only_rune())
        runegen_pair.write_provenance(
            MAP, self.rune, self.engine, self.config,
            (self.module_primary, self.module_secondary), 16,
            {"seeds": 2, "links": 2, "mechanism_nodes": 3, "plans": 1},
            self.provenance,
        )
        provisional = self.stage()
        live = self._live_pair()
        runegen_pair.install_pair(provisional, live, self.root / "local-backup")
        self.assertEqual(
            runeio.RUNE_ROUTE_CONTRACT_LOCAL_ONLY,
            runeio.read(live / f"{MAP}.rune").header.route_contract,
        )

        complete_maps = self.root / "complete-stage/maps"
        complete_maps.mkdir(parents=True)
        complete_rune = complete_maps / f"{MAP}.rune"
        complete_rune.write_bytes(_build_rune())
        complete_provenance = self.root / "complete-provenance.json"
        complete_evidence = self.root / "complete-evidence.json"
        complete_manifest = self.root / "complete-pair.json"
        runegen_pair.write_provenance(
            MAP, complete_rune, self.engine, self.config,
            (self.module_primary, self.module_secondary), 16,
            {"seeds": 2, "links": 2, "mechanism_nodes": 3, "plans": 1},
            complete_provenance,
        )
        complete = runegen_pair.stage_explicit_zero_pair(
            MAP, complete_maps, complete_evidence, complete_provenance,
            complete_manifest,
        )
        backup = runegen_pair.install_pair(
            complete, live, self.root / "complete-backup"
        )
        self.assertEqual(
            runeio.RUNE_ROUTE_CONTRACT_COMPLETE,
            runeio.read(live / f"{MAP}.rune").header.route_contract,
        )
        self.assertEqual(
            complete.snag.path.read_bytes(),
            (live / f"{MAP}.snag").read_bytes(),
        )
        self.assertEqual(
            provisional.rune.path.read_bytes(),
            (backup.directory / "old.rune").read_bytes(),
        )

    def test_install_failure_restores_old_pair_and_old_absence(self):
        pair = self.stage()
        for old_snag in (b"old-snag", None):
            for point in ("after-snag-replace", "after-rune-replace"):
                with self.subTest(old_snag=old_snag, point=point):
                    live = self._live_pair(old_snag)
                    with mock.patch.dict(
                        os.environ, {"RUNEGEN_PAIR_FAULT": f"fail:{point}"}
                    ):
                        with self.assertRaises(runegen_pair.PairError):
                            runegen_pair.install_pair(
                                pair, live, self.root / f"backups-{point}"
                            )
                    self.assertEqual(b"old-rune", (live / f"{MAP}.rune").read_bytes())
                    snag = live / f"{MAP}.snag"
                    if old_snag is None:
                        self.assertFalse(snag.exists())
                    else:
                        self.assertEqual(old_snag, snag.read_bytes())

    def test_recovery_uses_live_bytes_not_journal_state(self):
        pair = self.stage()
        live = self._live_pair()
        with mock.patch.dict(
            os.environ,
            {"RUNEGEN_PAIR_FAULT": "crash:after-rune-replace"},
        ):
            with self.assertRaises(runegen_pair.InjectedCrash):
                runegen_pair.install_pair(pair, live, self.root / "backups")
        journal = live / f".runegen-{MAP}.transaction.json"
        value = json.loads(journal.read_text(encoding="utf-8"))
        value["state"] = "prepared"
        journal.write_text(runegen_pair.canonical_json(value).decode(), encoding="utf-8")
        runegen_pair.recover_interrupted_install(MAP, live)
        self.assertEqual(pair.rune.path.read_bytes(), (live / f"{MAP}.rune").read_bytes())
        self.assertEqual(pair.snag.path.read_bytes(), (live / f"{MAP}.snag").read_bytes())
        self.assertFalse(journal.exists())

    def test_committed_recovery_does_not_require_consumed_rollbacks(self):
        pair = self.stage()
        live = self._live_pair()
        with mock.patch.dict(
            os.environ,
            {"RUNEGEN_PAIR_FAULT": "crash:after-rune-replace"},
        ):
            with self.assertRaises(runegen_pair.InjectedCrash):
                runegen_pair.install_pair(pair, live, self.root / "backups")
        journal = live / f".runegen-{MAP}.transaction.json"
        value = json.loads(journal.read_text(encoding="utf-8"))
        value["state"] = "committed"
        consumed = live / value["old"]["rune"]["rollback"]
        consumed.unlink()
        journal.write_text(runegen_pair.canonical_json(value).decode(), encoding="utf-8")
        runegen_pair.recover_interrupted_install(MAP, live)
        self.assertEqual(pair.rune.path.read_bytes(), (live / f"{MAP}.rune").read_bytes())
        self.assertEqual(pair.snag.path.read_bytes(), (live / f"{MAP}.snag").read_bytes())
        self.assertFalse(journal.exists())

    def test_recovery_restores_mixed_pair_and_rejects_forged_paths(self):
        pair = self.stage()
        live = self._live_pair()
        with mock.patch.dict(
            os.environ,
            {"RUNEGEN_PAIR_FAULT": "crash:after-snag-replace"},
        ):
            with self.assertRaises(runegen_pair.InjectedCrash):
                runegen_pair.install_pair(pair, live, self.root / "backups")
        journal = live / f".runegen-{MAP}.transaction.json"
        runegen_pair.recover_interrupted_install(MAP, live)
        self.assertEqual(b"old-rune", (live / f"{MAP}.rune").read_bytes())
        self.assertEqual(b"old-snag", (live / f"{MAP}.snag").read_bytes())
        self.assertFalse(journal.exists())

        live = self._live_pair()
        with mock.patch.dict(
            os.environ,
            {"RUNEGEN_PAIR_FAULT": "crash:after-snag-replace"},
        ):
            with self.assertRaises(runegen_pair.InjectedCrash):
                runegen_pair.install_pair(pair, live, self.root / "backups-2")
        value = json.loads(journal.read_text(encoding="utf-8"))
        value["old"]["rune"]["rollback"] = "../forged"
        journal.write_text(runegen_pair.canonical_json(value).decode(), encoding="utf-8")
        with self.assertRaisesRegex(runegen_pair.PairError, "transaction path"):
            runegen_pair.recover_interrupted_install(MAP, live)


if __name__ == "__main__":
    unittest.main()
