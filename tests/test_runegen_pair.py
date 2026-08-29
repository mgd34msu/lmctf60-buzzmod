#!/usr/bin/env python3
"""Exercise RUNE staging, cold-load proof, and crash recovery."""

from __future__ import annotations

import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import threading
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import runegen_pair
import runeio
from tests.test_rune_artifact import (
    _build_rune,
    _fix_header_crc,
    _fix_payload_and_header_crc,
)


MAP = "runetest"


def _build_local_only_rune() -> bytes:
    encoded = bytearray(_build_rune())
    struct.pack_into("<H", encoded, 4, runeio.RUNE_ROUTE_CONTRACT_LOCAL_ONLY)
    struct.pack_into(
        "<H", encoded, runeio.RUNE_HEADER_BYTES + 14, runeio.RSF_OBJECTIVE
    )
    struct.pack_into(
        "<H",
        encoded,
        runeio.RUNE_HEADER_BYTES + runeio.RUNE_SEED_BYTES + 14,
        runeio.RSF_OBJECTIVE,
    )
    _fix_payload_and_header_crc(encoded)
    return bytes(encoded)


def _rune_for_map(map_name: str) -> bytes:
    encoded = bytearray(_build_rune())
    offset = runeio.HEADER_STRUCT.size - runeio.MAP_NAME_BYTES
    raw_name = map_name.encode("ascii")
    encoded[offset:offset + runeio.MAP_NAME_BYTES] = (
        raw_name + b"\0" * (runeio.MAP_NAME_BYTES - len(raw_name))
    )
    _fix_header_crc(encoded)
    return bytes(encoded)


class RunegenPairTest(unittest.TestCase):
    def test_owned_production_sources_are_rune_only(self):
        for relative in (
            "tools/rune_corpus_controller.py",
            "tools/runegen.sh",
            "tools/runegen_pair.py",
        ):
            with self.subTest(path=relative):
                self.assertNotRegex(
                    (ROOT / relative).read_text(encoding="utf-8"),
                    r"(?i)snag",
                )

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.stage_maps = self.root / "stage/maps"
        self.stage_maps.mkdir(parents=True)
        self.bsp = self.stage_maps / f"{MAP}.bsp"
        self.bsp.write_bytes(b"map-bsp")
        self.rune = self.stage_maps / f"{MAP}.rune"
        self.rune.write_bytes(_build_rune())
        self.engine = self.root / "q2ded"
        self.config = self.root / "rune.cfg"
        self.module_primary = self.root / "game.so"
        self.module_secondary = self.root / "gamex86_64.so"
        for path, payload in (
            (self.engine, b"engine"),
            (self.config, b"config"),
            (self.module_primary, b"module-primary"),
            (self.module_secondary, b"module-secondary"),
        ):
            path.write_bytes(payload)
        self.provenance = self.root / "provenance.json"
        self.manifest = self.root / "rune.json"
        runegen_pair.write_provenance(
            MAP,
            self.bsp,
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
        return runegen_pair.stage_rune(
            MAP, self.stage_maps, self.provenance, self.manifest
        )

    def ready_log(self, line: str | None = None) -> Path:
        if line is None:
            line = (
                f"slipgate: rune ready {MAP}, 2 seeds, 2 links, "
                "3 mechanism nodes, 1 plans, gravity 650, all fields up"
            )
        log = self.root / "cold.log"
        log.write_text(line + "\n", encoding="utf-8")
        return log

    def test_stage_writes_exact_rune_identity_and_provenance(self):
        accepted = self.stage()
        value = json.loads(self.manifest.read_text(encoding="ascii"))
        self.assertEqual(
            {
                "counts",
                "bsp",
                "fingerprint",
                "format",
                "frozen_inputs",
                "map",
                "provenance",
                "rune",
            },
            set(value),
        )
        self.assertEqual(runegen_pair.MANIFEST_FORMAT, value["format"])
        self.assertEqual(accepted.rune.sha256, value["rune"]["sha256"])
        self.assertEqual(accepted.fingerprint, value["fingerprint"])

    def test_stage_rejects_existing_manifest_and_aliased_rune(self):
        self.manifest.write_bytes(b"stale")
        with self.assertRaisesRegex(runegen_pair.PairError, "already exists"):
            self.stage()
        self.manifest.unlink()
        os.link(self.rune, self.root / "rune-alias")
        with self.assertRaisesRegex(runegen_pair.PairError, "unaliased"):
            self.stage()

    def test_stage_rejects_malformed_and_cross_map_rune_bytes(self):
        for payload, diagnostic in (
            (b"not a RUNE", "wire validation"),
            (_rune_for_map("othermap"), "embedded map identity"),
        ):
            with self.subTest(diagnostic=diagnostic):
                self.rune.write_bytes(payload)
                runegen_pair.write_provenance(
                    MAP,
                    self.bsp,
                    self.rune,
                    self.engine,
                    self.config,
                    (self.module_primary, self.module_secondary),
                    16,
                    {"seeds": 2, "links": 2, "mechanism_nodes": 3, "plans": 1},
                    self.provenance,
                )
                with self.assertRaisesRegex(runegen_pair.PairError, diagnostic):
                    self.stage()
                self.assertFalse(self.manifest.exists())

    def test_validate_rehashes_rune_provenance_and_frozen_inputs(self):
        self.stage()
        for path in (self.bsp, self.rune, self.provenance, self.module_primary):
            with self.subTest(path=path.name):
                before = path.read_bytes()
                path.chmod(0o644)
                path.write_bytes(before + b"changed")
                with self.assertRaises(runegen_pair.PairError):
                    runegen_pair.validate_rune(self.manifest)
                path.write_bytes(before)

    def test_validate_rejects_internally_consistent_cross_map_manifest(self):
        self.stage()
        self.rune.write_bytes(_rune_for_map("othermap"))
        rune_digest = runegen_pair._sha256(self.rune.read_bytes())
        provenance = json.loads(self.provenance.read_text(encoding="ascii"))
        provenance["rune_sha256"] = rune_digest
        provenance_payload = runegen_pair.canonical_json(provenance)
        self.provenance.write_bytes(provenance_payload)

        manifest = json.loads(self.manifest.read_text(encoding="ascii"))
        manifest["rune"]["sha256"] = rune_digest
        manifest["rune"]["size"] = self.rune.stat().st_size
        manifest["provenance"]["sha256"] = runegen_pair._sha256(
            provenance_payload
        )
        manifest["provenance"]["size"] = len(provenance_payload)
        manifest["fingerprint"] = runegen_pair._sha256(provenance_payload)
        self.manifest.chmod(0o644)
        self.manifest.write_bytes(runegen_pair.canonical_json(manifest))

        with self.assertRaisesRegex(runegen_pair.PairError, "embedded map identity"):
            runegen_pair.validate_rune(self.manifest)

    def test_verify_cold_load_requires_one_exact_readiness_record(self):
        self.stage()
        good = self.ready_log()
        runegen_pair.verify_cold_load(self.manifest, good)
        ready = good.read_text(encoding="utf-8").strip()
        cases = {
            "missing": "ordinary output\n",
            "duplicate": ready + "\n" + ready + "\n",
            "wrong-map": ready.replace(MAP, "other"),
            "wrong-count": ready.replace("2 seeds", "3 seeds"),
            "write": "rune: wrote forbidden\n" + ready,
            "failure": "rune: revalidation failed kind=test\n" + ready,
        }
        for name, contents in cases.items():
            with self.subTest(name=name):
                bad = self.root / f"{name}.log"
                bad.write_text(contents, encoding="utf-8")
                with self.assertRaises(runegen_pair.PairError):
                    runegen_pair.verify_cold_load(self.manifest, bad)

    def _live(self, rune: bytes | None = b"old-rune") -> Path:
        live = self.root / "live/maps"
        live.mkdir(parents=True, exist_ok=True)
        path = live / f"{MAP}.rune"
        if rune is None:
            path.unlink(missing_ok=True)
        else:
            path.write_bytes(rune)
        return live

    def test_install_atomically_replaces_rune_and_preserves_backup(self):
        accepted = self.stage()
        live = self._live()
        record = runegen_pair.install_rune(accepted, live, self.root / "backups")
        self.assertEqual(accepted.rune.path.read_bytes(), (live / f"{MAP}.rune").read_bytes())
        self.assertEqual("old-rune", (record.directory / "old.rune").read_text())
        backup = json.loads(record.manifest.read_text(encoding="utf-8"))
        self.assertEqual(runegen_pair.BACKUP_FORMAT, backup["format"])
        self.assertTrue(backup["rune"]["exists"])

    def test_complete_rune_replaces_local_only_rune(self):
        self.rune.write_bytes(_build_local_only_rune())
        runegen_pair.write_provenance(
            MAP,
            self.bsp,
            self.rune,
            self.engine,
            self.config,
            (self.module_primary, self.module_secondary),
            16,
            {"seeds": 2, "links": 2, "mechanism_nodes": 3, "plans": 1},
            self.provenance,
        )
        provisional = self.stage()
        live = self._live()
        runegen_pair.install_rune(provisional, live, self.root / "local-backup")
        self.assertEqual(
            runeio.RUNE_ROUTE_CONTRACT_LOCAL_ONLY,
            runeio.read(live / f"{MAP}.rune").header.route_contract,
        )

        complete_maps = self.root / "complete-stage/maps"
        complete_maps.mkdir(parents=True)
        complete_bsp = complete_maps / f"{MAP}.bsp"
        complete_bsp.write_bytes(self.bsp.read_bytes())
        complete_rune = complete_maps / f"{MAP}.rune"
        complete_rune.write_bytes(_build_rune())
        complete_provenance = self.root / "complete-provenance.json"
        complete_manifest = self.root / "complete-rune.json"
        runegen_pair.write_provenance(
            MAP,
            complete_bsp,
            complete_rune,
            self.engine,
            self.config,
            (self.module_primary, self.module_secondary),
            16,
            {"seeds": 2, "links": 2, "mechanism_nodes": 3, "plans": 1},
            complete_provenance,
        )
        complete = runegen_pair.stage_rune(
            MAP, complete_maps, complete_provenance, complete_manifest
        )
        backup = runegen_pair.install_rune(
            complete, live, self.root / "complete-backup"
        )
        self.assertEqual(
            runeio.RUNE_ROUTE_CONTRACT_COMPLETE,
            runeio.read(live / f"{MAP}.rune").header.route_contract,
        )
        self.assertEqual(
            provisional.rune.path.read_bytes(),
            (backup.directory / "old.rune").read_bytes(),
        )

    def test_install_failure_restores_old_bytes_or_old_absence(self):
        accepted = self.stage()
        for old_rune in (b"old-rune", None):
            for point in ("before-rune-replace", "after-rune-replace"):
                with self.subTest(old_rune=old_rune, point=point):
                    live = self._live(old_rune)
                    with mock.patch.dict(
                        os.environ, {"RUNEGEN_PAIR_FAULT": f"fail:{point}"}
                    ):
                        with self.assertRaises(runegen_pair.PairError):
                            runegen_pair.install_rune(
                                accepted, live, self.root / f"backups-{point}"
                            )
                    path = live / f"{MAP}.rune"
                    if old_rune is None:
                        self.assertFalse(path.exists())
                    else:
                        self.assertEqual(old_rune, path.read_bytes())

    def test_same_map_installers_serialize_complete_transactions(self):
        first = self.stage()
        second_maps = self.root / "second-stage/maps"
        second_maps.mkdir(parents=True)
        second_bsp = second_maps / f"{MAP}.bsp"
        second_bsp.write_bytes(self.bsp.read_bytes())
        second_rune = second_maps / f"{MAP}.rune"
        second_rune.write_bytes(_build_local_only_rune())
        second_provenance = self.root / "second-provenance.json"
        second_manifest = self.root / "second-manifest.json"
        runegen_pair.write_provenance(
            MAP,
            second_bsp,
            second_rune,
            self.engine,
            self.config,
            (self.module_primary, self.module_secondary),
            16,
            {"seeds": 2, "links": 2, "mechanism_nodes": 3, "plans": 1},
            second_provenance,
        )
        second = runegen_pair.stage_rune(
            MAP, second_maps, second_provenance, second_manifest
        )
        live = self._live()
        first_prepared = threading.Event()
        release_first = threading.Event()
        second_started = threading.Event()
        second_lock_attempted = threading.Event()
        second_inventory = threading.Event()
        failures: list[BaseException] = []
        backups: dict[str, runegen_pair.BackupRecord] = {}
        real_fault = runegen_pair._fault
        real_flock = runegen_pair.fcntl.flock
        real_inventory = runegen_pair._inventory_live

        def controlled_fault(point: str) -> None:
            if threading.current_thread().name == "first-installer" and (
                point == "before-rune-replace"
            ):
                first_prepared.set()
                release_first.wait()
            real_fault(point)

        def observed_flock(descriptor: int, operation: int):
            if threading.current_thread().name == "second-installer":
                second_lock_attempted.set()
            return real_flock(descriptor, operation)

        def observed_inventory(path: Path, label: str):
            if threading.current_thread().name == "second-installer":
                second_inventory.set()
            return real_inventory(path, label)

        def install(name: str, accepted: runegen_pair.AcceptedRune) -> None:
            try:
                if name == "second":
                    second_started.set()
                backups[name] = runegen_pair.install_rune(
                    accepted, live, self.root / f"{name}-backups"
                )
            except BaseException as error:
                failures.append(error)

        with mock.patch.object(runegen_pair, "_fault", controlled_fault), mock.patch.object(
            runegen_pair.fcntl, "flock", observed_flock
        ), mock.patch.object(runegen_pair, "_inventory_live", observed_inventory):
            first_thread = threading.Thread(
                target=install, args=("first", first), name="first-installer", daemon=True
            )
            second_thread = threading.Thread(
                target=install,
                args=("second", second),
                name="second-installer",
                daemon=True,
            )
            first_thread.start()
            first_prepared.wait()
            second_thread.start()
            second_started.wait()
            second_lock_attempted.wait()
            self.assertFalse(second_inventory.is_set())
            release_first.set()
            first_thread.join()
            second_thread.join()

        self.assertFalse(first_thread.is_alive())
        self.assertFalse(second_thread.is_alive())
        self.assertEqual([], failures)
        self.assertEqual(second.rune.path.read_bytes(), (live / f"{MAP}.rune").read_bytes())
        self.assertEqual(
            first.rune.path.read_bytes(),
            (backups["second"].directory / "old.rune").read_bytes(),
        )
        self.assertEqual(
            [f".runegen-{MAP}.lock"],
            sorted(path.name for path in live.glob(f".runegen-{MAP}.*")),
        )

    def test_map_install_lock_rejects_symlinks_and_hardlinks(self):
        live = self._live()
        lock = live / f".runegen-{MAP}.lock"
        target = self.root / "foreign-lock"
        target.write_bytes(b"lock")
        lock.symlink_to(target)
        with self.assertRaises(runegen_pair.PairError):
            runegen_pair.recover_interrupted_install(MAP, live)

        lock.unlink()
        os.link(target, lock)
        with self.assertRaisesRegex(runegen_pair.PairError, "unaliased"):
            runegen_pair.recover_interrupted_install(MAP, live)

    def test_recovery_rejects_foreign_transaction_paths_without_unlinking(self):
        accepted = self.stage()
        live = self._live()
        with mock.patch.dict(
            os.environ, {"RUNEGEN_PAIR_FAULT": "crash:after-rune-replace"}
        ):
            with self.assertRaises(runegen_pair.InjectedCrash):
                runegen_pair.install_rune(accepted, live, self.root / "backups")
        journal = live / f".runegen-{MAP}.transaction.json"
        lock = live / f".runegen-{MAP}.lock"
        lock_identity = lock.stat().st_dev, lock.stat().st_ino
        foreign = live / f".runegen-{MAP}.foreign-owned"
        foreign.write_bytes(b"foreign")
        value = json.loads(journal.read_text(encoding="utf-8"))
        value["state"] = "committed"
        value["old"]["rollback"] = lock.name
        value["new"]["temporary"] = foreign.name
        journal.write_bytes(runegen_pair.canonical_json(value))

        with self.assertRaisesRegex(runegen_pair.PairError, "transaction path"):
            runegen_pair.recover_interrupted_install(MAP, live)
        self.assertEqual(lock_identity, (lock.stat().st_dev, lock.stat().st_ino))
        self.assertEqual(b"foreign", foreign.read_bytes())
        self.assertTrue(journal.exists())

    def test_recovery_requires_one_transaction_token_and_valid_rollback(self):
        accepted = self.stage()
        for corruption in ("token", "rollback"):
            with self.subTest(corruption=corruption):
                live = self.root / f"live-{corruption}/maps"
                live.mkdir(parents=True)
                (live / f"{MAP}.rune").write_bytes(b"old-rune")
                with mock.patch.dict(
                    os.environ, {"RUNEGEN_PAIR_FAULT": "crash:after-rune-replace"}
                ):
                    with self.assertRaises(runegen_pair.InjectedCrash):
                        runegen_pair.install_rune(
                            accepted, live, self.root / f"backups-{corruption}"
                        )
                journal = live / f".runegen-{MAP}.transaction.json"
                value = json.loads(journal.read_text(encoding="utf-8"))
                value["state"] = "committed"
                rollback = live / value["old"]["rollback"]
                if corruption == "token":
                    replacement = live / f".runegen-{MAP}.old-rune-{'0' * 32}"
                    replacement.write_bytes(rollback.read_bytes())
                    value["old"]["rollback"] = replacement.name
                    journal.write_bytes(runegen_pair.canonical_json(value))
                    diagnostic = "one owner"
                else:
                    rollback.write_bytes(b"corrupt rollback")
                    diagnostic = "rollback bytes"

                with self.assertRaisesRegex(runegen_pair.PairError, diagnostic):
                    runegen_pair.recover_interrupted_install(MAP, live)
                self.assertTrue(journal.exists())
                self.assertTrue((live / f".runegen-{MAP}.lock").exists())

    def test_cleanup_durably_retires_journal_before_payloads(self):
        accepted = self.stage()
        live = self._live()
        with mock.patch.dict(
            os.environ, {"RUNEGEN_PAIR_FAULT": "crash:after-rune-replace"}
        ):
            with self.assertRaises(runegen_pair.InjectedCrash):
                runegen_pair.install_rune(accepted, live, self.root / "backups")
        journal = live / f".runegen-{MAP}.transaction.json"
        value = json.loads(journal.read_text(encoding="utf-8"))
        rollback = live / value["old"]["rollback"]

        with mock.patch.object(
            runegen_pair,
            "_unlink_recorded",
            side_effect=runegen_pair.InjectedCrash("payload-cleanup"),
        ):
            with self.assertRaises(runegen_pair.InjectedCrash):
                runegen_pair.recover_interrupted_install(MAP, live)

        self.assertFalse(journal.exists())
        self.assertTrue(rollback.exists())
        runegen_pair.recover_interrupted_install(MAP, live)
        runegen_pair.install_rune(accepted, live, self.root / "backups-after-crash")
        self.assertEqual(
            accepted.rune.path.read_bytes(), (live / f"{MAP}.rune").read_bytes()
        )

    def test_restart_converges_from_prepared_and_committed_journal(self):
        accepted = self.stage()
        for rewritten_state in ("prepared", "committed"):
            with self.subTest(state=rewritten_state):
                live = self._live()
                with mock.patch.dict(
                    os.environ, {"RUNEGEN_PAIR_FAULT": "crash:after-rune-replace"}
                ):
                    with self.assertRaises(runegen_pair.InjectedCrash):
                        runegen_pair.install_rune(
                            accepted, live, self.root / f"backups-{rewritten_state}"
                        )
                journal = live / f".runegen-{MAP}.transaction.json"
                value = json.loads(journal.read_text(encoding="utf-8"))
                value["state"] = rewritten_state
                journal.write_text(
                    runegen_pair.canonical_json(value).decode(), encoding="utf-8"
                )
                runegen_pair.recover_interrupted_install(MAP, live)
                self.assertEqual(
                    accepted.rune.path.read_bytes(),
                    (live / f"{MAP}.rune").read_bytes(),
                )
                self.assertFalse(journal.exists())

    def test_pre_replace_crash_rolls_back_and_forged_path_fails_closed(self):
        accepted = self.stage()
        live = self._live()
        with mock.patch.dict(
            os.environ, {"RUNEGEN_PAIR_FAULT": "crash:before-rune-replace"}
        ):
            with self.assertRaises(runegen_pair.InjectedCrash):
                runegen_pair.install_rune(accepted, live, self.root / "backups")
        journal = live / f".runegen-{MAP}.transaction.json"
        runegen_pair.recover_interrupted_install(MAP, live)
        self.assertEqual(b"old-rune", (live / f"{MAP}.rune").read_bytes())
        self.assertFalse(journal.exists())

        with mock.patch.dict(
            os.environ, {"RUNEGEN_PAIR_FAULT": "crash:before-rune-replace"}
        ):
            with self.assertRaises(runegen_pair.InjectedCrash):
                runegen_pair.install_rune(accepted, live, self.root / "backups-2")
        value = json.loads(journal.read_text(encoding="utf-8"))
        value["old"]["rollback"] = "../forged"
        journal.write_text(runegen_pair.canonical_json(value).decode(), encoding="utf-8")
        with self.assertRaisesRegex(runegen_pair.PairError, "transaction path"):
            runegen_pair.recover_interrupted_install(MAP, live)

    def test_recovery_is_idempotent_after_cleanup(self):
        accepted = self.stage()
        live = self._live()
        runegen_pair.install_rune(accepted, live, self.root / "backups")
        runegen_pair.recover_interrupted_install(MAP, live)
        runegen_pair.recover_interrupted_install(MAP, live)
        self.assertEqual(accepted.rune.path.read_bytes(), (live / f"{MAP}.rune").read_bytes())


if __name__ == "__main__":
    unittest.main()
