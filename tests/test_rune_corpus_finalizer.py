#!/usr/bin/env python3
"""Focused publication tests for the final RUNE corpus authority."""

from __future__ import annotations

import json
import os
from pathlib import Path
import stat
import tempfile
import time
import unittest
from unittest import mock

from tools import rune_corpus_controller as controller
from tools import rune_corpus_finalizer as finalizer


class FakeLock:
    def __init__(self, _root, _fingerprint, *, publish_owner=True):
        self.publish_owner = publish_owner

    def __enter__(self):
        if self.publish_owner:
            raise AssertionError("finalization must not rewrite controller ownership")
        return self

    def __exit__(self, *_args):
        return None


class FakeApi:
    CorpusError = controller.CorpusError
    SUCCESS_CLASSIFICATIONS = controller.SUCCESS_CLASSIFICATIONS
    APPROVED_ROUTE_ONLY_MAPS = ("map1", "map2")
    POLICY_VERSION = 1
    ControllerLock = FakeLock
    canonical_json = staticmethod(controller.canonical_json)
    sha256_bytes = staticmethod(controller.sha256_bytes)
    regular_file_record = staticmethod(controller.regular_file_record)
    read_regular_bytes = staticmethod(controller.read_regular_bytes)
    _load_json_regular = staticmethod(controller._load_json_regular)
    atomic_write_bytes = staticmethod(controller.atomic_write_bytes)
    atomic_write_json = staticmethod(controller.atomic_write_json)
    freeze_tree = staticmethod(controller.freeze_tree)
    fsync_tree = staticmethod(controller.fsync_tree)
    reject_symlink_components = staticmethod(controller.reject_symlink_components)

    @staticmethod
    def preflight_python_runtime(_snapshot, **_kwargs):
        return {"classification": "PASS"}

    @staticmethod
    def verify_snapshot(snapshot):
        return {
            "by_role": {"map_manifest": {"path": "maps.txt"}},
            "adopted_runes": {},
        }

    @staticmethod
    def validate_manifest(path):
        return path.read_text(encoding="ascii").splitlines()

    @staticmethod
    def verify_fingerprint_document(_snapshot, document):
        if document.get("port_base") != 62000:
            raise controller.CorpusError("bad fake fingerprint")
        return "f" * 64

    @staticmethod
    def validate_terminal_result(
        result_path,
        *,
        run_root,
        map_name,
        fingerprint,
        stable_port,
        **_kwargs,
    ):
        try:
            value, raw = controller._load_json_regular(result_path)
            attempt = run_root / "runs" / map_name / (
                f"attempt-{value['attempt']:04d}"
            ) / "result.json"
            attempt_value, attempt_raw = controller._load_json_regular(attempt)
        except (OSError, KeyError, ValueError, json.JSONDecodeError):
            return None
        if (
            value != attempt_value
            or raw != attempt_raw
            or value.get("map") != map_name
            or value.get("fingerprint") != fingerprint
            or value.get("stable_port") != stable_port
        ):
            return None
        return value, raw


class HistoryFakeApi(FakeApi):
    history = []

    @classmethod
    def load_map_history(cls, *_args, **_kwargs):
        return cls.history


class RuneCorpusFinalizerTest(unittest.TestCase):
    def make_run(self, root: Path, *, noncandidate_route_only: bool = False):
        snapshot = root / "snapshot"
        run_root = root / "run"
        output = root / "archive"
        snapshot.mkdir(parents=True)
        run_root.mkdir()
        maps = ("map1", "map2", "map3")
        (snapshot / "maps.txt").write_text("\n".join(maps) + "\n", encoding="ascii")
        controller.atomic_write_json(snapshot / "input-manifest.json", {"fake": True})
        document = {
            "port_base": 62000,
            "controller_sha256": "a" * 64,
            "finalizer_sha256": "b" * 64,
            "route_only_policy_sha256": "c" * 64,
        }
        controller.atomic_write_json(run_root / "fingerprint-document.json", document)
        (run_root / "fingerprint.txt").write_text("f" * 64 + "\n", encoding="ascii")
        for index, map_name in enumerate(maps):
            route_only = map_name == ("map3" if noncandidate_route_only else "map2")
            value = {
                "map": map_name,
                "fingerprint": "f" * 64,
                "stable_port": 62000 + index,
                "attempt": 1,
                "attempt_kind": "generated_missing",
                "provenance": {"source_artifact": None, "rejection_result": None},
                "classification": "ROUTE_ONLY" if route_only else "PASS",
                "route_contract": "local_only" if route_only else "complete",
            }
            attempt = run_root / "runs" / map_name / "attempt-0001" / "result.json"
            pointer = run_root / "runs" / map_name / "result.json"
            controller.atomic_write_json(attempt, value)
            controller.atomic_write_json(pointer, value)
        return snapshot, run_root, output

    def thaw(self, root: Path):
        for directory, names, files in os.walk(root, topdown=False, followlinks=False):
            current = Path(directory)
            for name in names + files:
                path = current / name
                path.chmod(stat.S_IMODE(path.lstat().st_mode) | 0o700)
            current.chmod(stat.S_IMODE(current.lstat().st_mode) | 0o700)

    def test_finalizer_accepts_adopted_provenance_and_rejects_bad_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            snapshot, run_root, _output = self.make_run(root)
            candidate = {
                "path": "adopted-runes/map1.rune", "mode": 0o444,
                "size": 7, "sha256": "a" * 64, "role": "adopted_rune:map1",
            }
            class AdoptedApi(FakeApi):
                @staticmethod
                def verify_snapshot(_snapshot):
                    return {
                        "by_role": {"map_manifest": {"path": "maps.txt"}},
                        "adopted_runes": {
                            "map1": candidate,
                            **{f"adopted{index}": {} for index in range(155)},
                        },
                    }
            path = run_root / "runs/map1/attempt-0001/result.json"
            value, _raw = controller._load_json_regular(path)
            value["attempt_kind"] = "adopted_validation"
            value["provenance"] = {"source_artifact": candidate, "rejection_result": None}
            controller.atomic_write_json(path, value)
            controller.atomic_write_json(run_root / "runs/map1/result.json", value)
            identity = finalizer._identity(AdoptedApi, snapshot, run_root)
            entry, _result = finalizer._accepted_result(
                AdoptedApi, snapshot=snapshot, run_root=run_root, map_name="map1",
                stable_port=62000, fingerprint="f" * 64,
                document_bytes=identity["document_bytes"],
                result_path=run_root / "runs/map1/result.json",
                adopted_runes=identity["snapshot_verified"]["adopted_runes"],
            )
            self.assertEqual("adopted_validation", entry["attempt_kind"])
            value["provenance"]["source_artifact"] = None
            controller.atomic_write_json(path, value)
            controller.atomic_write_json(run_root / "runs/map1/result.json", value)
            with self.assertRaisesRegex(controller.CorpusError, "provenance"):
                finalizer._accepted_result(
                    AdoptedApi, snapshot=snapshot, run_root=run_root, map_name="map1",
                    stable_port=62000, fingerprint="f" * 64,
                    document_bytes=identity["document_bytes"],
                    result_path=run_root / "runs/map1/result.json",
                    adopted_runes=identity["snapshot_verified"]["adopted_runes"],
                )

    def test_finalizer_rejects_adopted_route_only_without_generation_proof(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            snapshot, run_root, _output = self.make_run(root)
            candidate = {
                "path": "adopted-runes/map2.rune", "mode": 0o444,
                "size": 7, "sha256": "a" * 64, "role": "adopted_rune:map2",
            }

            class AdoptedRouteApi(FakeApi):
                @staticmethod
                def verify_snapshot(_snapshot):
                    return {
                        "by_role": {"map_manifest": {"path": "maps.txt"}},
                        "adopted_runes": {
                            "map2": candidate,
                            **{f"adopted{index}": {} for index in range(155)},
                        },
                    }

            path = run_root / "runs/map2/attempt-0001/result.json"
            value, _raw = controller._load_json_regular(path)
            value["attempt_kind"] = "adopted_validation"
            value["provenance"] = {
                "source_artifact": candidate, "rejection_result": None,
            }
            controller.atomic_write_json(path, value)
            controller.atomic_write_json(run_root / "runs/map2/result.json", value)
            identity = finalizer._identity(AdoptedRouteApi, snapshot, run_root)
            with self.assertRaisesRegex(controller.CorpusError, "ROUTE_ONLY"):
                finalizer._accepted_result(
                    AdoptedRouteApi, snapshot=snapshot, run_root=run_root,
                    map_name="map2", stable_port=62001, fingerprint="f" * 64,
                    document_bytes=identity["document_bytes"],
                    result_path=run_root / "runs/map2/result.json",
                    adopted_runes=identity["snapshot_verified"]["adopted_runes"],
                )

    def test_finalizer_history_requires_one_authenticated_rejection(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            source = {"path": "adopted-runes/map1.rune"}
            accepted_result = {
                "attempt_kind": "generated_replacement", "classification": "PASS",
                "provenance": {
                    "source_artifact": source,
                    "rejection_result": "runs/map1/attempt-0001/result.json",
                },
            }
            rejected = {
                "attempt": 1, "path": run_root / "runs/map1/attempt-0001",
                "intent": {"kind": "adopted_validation"},
                "result": {"disposition": "artifact_rejected", "classification": "LINT_FAIL"},
            }
            replacement = {
                "attempt": 2, "path": run_root / "runs/map1/attempt-0002",
                "intent": {"kind": "generated_replacement"}, "result": accepted_result,
            }
            for item in (rejected, replacement):
                controller.atomic_write_json(item["path"] / "intent.json", item["intent"])
                controller.atomic_write_json(item["path"] / "result.json", item["result"])
            HistoryFakeApi.history = [rejected, replacement]
            finalizer._validate_history(
                HistoryFakeApi, run_root=run_root, map_name="map1", fingerprint="f" * 64,
                stable_port=62000, result=accepted_result,
            )
            third = {
                "attempt": 3, "path": run_root / "runs/map1/attempt-0003",
                "intent": {"kind": "generated_replacement"}, "result": accepted_result,
            }
            controller.atomic_write_json(third["path"] / "intent.json", third["intent"])
            controller.atomic_write_json(third["path"] / "result.json", third["result"])
            HistoryFakeApi.history = [rejected, replacement, third]
            with self.assertRaisesRegex(controller.CorpusError, "post-accept|repeats"):
                finalizer._validate_history(
                    HistoryFakeApi, run_root=run_root, map_name="map1", fingerprint="f" * 64,
                    stable_port=62000, result=accepted_result,
                )

    def test_finalizer_rejects_wrong_source_in_aborted_adoption(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            candidate = {
                "path": "adopted-runes/map1.rune", "mode": 0o444,
                "size": 7, "sha256": "a" * 64, "role": "adopted_rune:map1",
            }
            accepted = {
                "attempt_kind": "adopted_validation", "classification": "PASS",
                "disposition": "accepted",
                "provenance": {"source_artifact": candidate, "rejection_result": None},
            }
            aborted = {
                "attempt": 1, "path": run_root / "runs/map1/attempt-0001",
                "intent": {
                    "kind": "adopted_validation", "source_artifact": {
                        **candidate, "sha256": "b" * 64,
                    },
                },
                "result": None, "aborted": True,
            }
            terminal = {
                "attempt": 2, "path": run_root / "runs/map1/attempt-0002",
                "intent": {"kind": "adopted_validation", "source_artifact": candidate},
                "result": accepted,
            }
            for item in (aborted, terminal):
                controller.atomic_write_json(item["path"] / "intent.json", item["intent"])
            controller.atomic_write_json(terminal["path"] / "result.json", accepted)
            HistoryFakeApi.history = [aborted, terminal]
            with self.assertRaisesRegex(controller.CorpusError, "source changed"):
                finalizer._validate_history(
                    HistoryFakeApi, run_root=run_root, map_name="map1", fingerprint="f" * 64,
                    stable_port=62000, result=accepted, adopted_runes={"map1": candidate},
                )

    def test_finalizer_seed_failure_closes_review_ticker(self):
        publisher = mock.Mock()
        publisher.seed_terminals.side_effect = controller.CorpusError("seed failed")
        with self.assertRaisesRegex(controller.CorpusError, "seed failed"):
            finalizer._finish_review_heartbeat(publisher, ("map1",), True)
        publisher.close.assert_called_once()

    def test_finalizer_accepts_snapshot_defined_adoption_count(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot, run_root, _output = self.make_run(Path(temporary))
            class SnapshotDefinedApi(FakeApi):
                @staticmethod
                def verify_snapshot(_snapshot):
                    return {
                        "by_role": {"map_manifest": {"path": "maps.txt"}},
                        "adopted_runes": {},
                    }
            identity = finalizer._identity(SnapshotDefinedApi, snapshot, run_root)
            self.assertEqual(("map1", "map2", "map3"), identity["maps"])

    def test_bundle_binding_replays_each_present_snag_from_its_final_attempt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            snapshot, run_root, corpus_root = root / "snapshot", root / "run", root / "corpus"
            snapshot.mkdir()
            run_root.mkdir()
            corpus_root.mkdir()
            maps = ("map1", "map2")
            (snapshot / "maps.txt").write_text("map1\nmap2\n", encoding="ascii")

            def digest(payload):
                return controller.sha256_bytes(payload)

            module = b"module\n"
            engine = b"engine\n"
            roles = {
                "map_manifest": {"path": "maps.txt"},
                "module_primary": {"size": len(module), "sha256": digest(module)},
                "module_secondary": {"size": len(module), "sha256": digest(module)},
                "engine": {"size": len(engine), "sha256": digest(engine)},
            }
            bundle_roles = {
                "module-primary": dict(roles["module_primary"]),
                "module-secondary": dict(roles["module_secondary"]),
            }
            results = []
            for index, map_name in enumerate(maps):
                rune = f"rune-{map_name}\n".encode("ascii")
                bsp = f"bsp-{map_name}\n".encode("ascii")
                snag = (
                    run_root / "runs" / map_name / "attempt-0001" / "cold-load"
                    / "private" / "game" / "maps" / f"{map_name}.snag"
                )
                snag.parent.mkdir(parents=True)
                snag.write_bytes(f"snag-{map_name}\n".encode("ascii"))
                snag.chmod(0o444)
                result_path = run_root / "runs" / map_name / "attempt-0001" / "result.json"
                result = {
                    "map": map_name, "attempt": 1,
                    "artifact_sha256": digest(rune),
                    "artifact": {"size": len(rune), "sha256": digest(rune)},
                    "cold_load_snag_record": str(snag.relative_to(run_root)),
                }
                controller.atomic_write_json(result_path, result, mode=0o444)
                results.append({
                    "map": map_name, "stable_port": 62000 + index,
                    "classification": "ROUTE_ONLY" if index == 0 else "PASS",
                    "route_contract": "local_only" if index == 0 else "complete",
                    "attempt": 1, "attempt_kind": "generated_missing",
                    "provenance": {"source_artifact": None, "rejection_result": None},
                    "history": [],
                    "attempt_result": controller.regular_file_record(
                        result_path, require_unaliased=True
                    ),
                })
                roles[f"asset:{map_name}"] = {"size": len(bsp), "sha256": digest(bsp)}
                bundle_roles[f"rune:{map_name}"] = {"size": len(rune), "sha256": digest(rune)}
                bundle_roles[f"bsp:{map_name}"] = dict(roles[f"asset:{map_name}"])
                bundle_roles[f"snag:{map_name}"] = controller.regular_file_record(
                    snag, require_unaliased=True
                )
            authority_path = corpus_root / "corpus-authority.json"
            authority_path.write_text("{}\n", encoding="ascii")
            authority_path.chmod(0o444)
            verified = {
                "corpus_id": "f" * 64,
                "authority": controller.regular_file_record(
                    authority_path, require_unaliased=True
                ),
                "run_root": str(run_root.resolve()), "port_base": 62000,
                "results": results,
            }

            class BindingApi(FakeApi):
                @staticmethod
                def verify_snapshot(_snapshot):
                    return {"by_role": roles}

                @staticmethod
                def validate_manifest(_path):
                    return list(maps)

            controller_record = {"fixture": "controller"}
            finalizer_record = {"fixture": "finalizer"}
            with mock.patch.object(finalizer, "verify_final_corpus", return_value=verified):
                binding = finalizer.build_verified_final_corpus_binding(
                    BindingApi, snapshot=snapshot, corpus_root=corpus_root,
                    controller_record=controller_record, finalizer_record=finalizer_record,
                )
                checked = finalizer.validate_bundle_final_corpus_binding(
                    BindingApi, binding=binding, controller_record=controller_record,
                    finalizer_record=finalizer_record, bundle_roles=bundle_roles,
                    engine_record={"size": len(engine), "sha256": digest(engine)},
                )
            self.assertEqual(set(maps), set(checked))
            bundle_roles["snag:map1"]["sha256"] = "0" * 64
            with mock.patch.object(finalizer, "verify_final_corpus", return_value=verified):
                with self.assertRaisesRegex(controller.CorpusError, "snag:map1"):
                    finalizer.validate_bundle_final_corpus_binding(
                        BindingApi, binding=binding, controller_record=controller_record,
                        finalizer_record=finalizer_record, bundle_roles=bundle_roles,
                    )
            bundle_roles["snag:map1"] = controller.regular_file_record(
                run_root / "runs" / "map1" / "attempt-0001" / "cold-load" / "private"
                / "game" / "maps" / "map1.snag", require_unaliased=True,
            )
            result_path = run_root / "runs" / "map1" / "attempt-0001" / "result.json"
            result, _raw = controller._load_json_regular(result_path)
            result["cold_load_snag_record"] = str(
                (run_root / "runs" / "map2" / "attempt-0001" / "cold-load" / "private"
                 / "game" / "maps" / "map2.snag").relative_to(run_root)
            )
            controller.atomic_write_json(result_path, result, mode=0o444)
            with mock.patch.object(finalizer, "verify_final_corpus", return_value=verified):
                with self.assertRaisesRegex(controller.CorpusError, "SNAG record"):
                    finalizer.validate_bundle_final_corpus_binding(
                        BindingApi, binding=binding, controller_record=controller_record,
                        finalizer_record=finalizer_record, bundle_roles=bundle_roles,
                    )

    def test_publish_verify_and_repeat_ignore_mutable_pointer(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot, run_root, output = self.make_run(Path(temporary))
            published = finalizer.finalize_corpus(
                FakeApi, snapshot=snapshot, run_root=run_root, output_parent=output
            )
            corpus_root = output / published["corpus_id"]
            self.assertEqual(published, finalizer.verify_final_corpus(
                FakeApi, snapshot=snapshot, corpus_root=corpus_root
            ))
            (run_root / "runs" / "map1" / "result.json").write_text("pointer drift")
            repeated = finalizer.finalize_corpus(
                FakeApi, snapshot=snapshot, run_root=run_root, output_parent=output
            )
            self.assertEqual(published["corpus_id"], repeated["corpus_id"])
            for directory, _names, files in os.walk(corpus_root):
                self.assertFalse(stat.S_IMODE(Path(directory).stat().st_mode) & 0o222)
                for name in files:
                    self.assertFalse(
                        stat.S_IMODE((Path(directory) / name).stat().st_mode) & 0o222
                    )
            self.thaw(corpus_root)

    def test_noncandidate_route_only_is_rejected_before_seal(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot, run_root, output = self.make_run(
                Path(temporary), noncandidate_route_only=True
            )
            with self.assertRaisesRegex(controller.CorpusError, "not approved"):
                finalizer.finalize_corpus(
                    FakeApi, snapshot=snapshot, run_root=run_root,
                    output_parent=output,
                )
            self.assertFalse((run_root / finalizer.SEAL_FILENAME).exists())

    def test_finalizer_slow_replay_publishes_no_child_heartbeat(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot, run_root, output = self.make_run(Path(temporary))
            observed = []
            publishers = []

            class ReviewApi(FakeApi):
                @staticmethod
                def HeartbeatPublisher(root, fingerprint, total):
                    publisher = controller.HeartbeatPublisher(
                        root, fingerprint, total, ticker_interval=0.01
                    )
                    publishers.append(publisher)
                    return publisher

            original = finalizer._source_results

            def slow_source(*args, **kwargs):
                time.sleep(0.04)
                observed.append(json.loads((run_root / "heartbeat.json").read_text()))
                return original(*args, **kwargs)

            with mock.patch.object(finalizer, "_source_results", side_effect=slow_source):
                finalizer.finalize_corpus(
                    ReviewApi, snapshot=snapshot, run_root=run_root, output_parent=output
                )
            self.assertTrue(observed)
            active = observed[0]["active"]
            self.assertEqual("final_review", active[0]["stage"])
            self.assertNotIn("process", active[0])
            self.assertGreaterEqual(observed[0]["sequence"], 3)
            self.assertEqual(1, len(publishers))
            self.assertFalse(publishers[0].ticker.is_alive())

    def test_finalizer_failure_closes_review_heartbeat(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot, run_root, output = self.make_run(Path(temporary))
            publishers = []

            class ReviewApi(FakeApi):
                @staticmethod
                def HeartbeatPublisher(root, fingerprint, total):
                    publisher = controller.HeartbeatPublisher(
                        root, fingerprint, total, ticker_interval=0.01
                    )
                    publishers.append(publisher)
                    return publisher

            with mock.patch.object(
                    finalizer, "_source_results",
                    side_effect=controller.CorpusError("slow replay failed")):
                with self.assertRaisesRegex(controller.CorpusError, "slow replay"):
                    finalizer.finalize_corpus(
                        ReviewApi, snapshot=snapshot, run_root=run_root,
                        output_parent=output,
                    )
            self.assertEqual(1, len(publishers))
            self.assertFalse(publishers[0].ticker.is_alive())

    def test_final_replay_ticker_failure_tears_down_blocked_gate_child(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot, run_root, _output = self.make_run(Path(temporary))
            identity = finalizer._identity(FakeApi, snapshot, run_root)
            publisher = mock.Mock()

            def event(kind, *_args, **_kwargs):
                if kind == "beat":
                    raise controller.CorpusError("ticker write failed")

            publisher.event.side_effect = event

            class ReplayApi(FakeApi):
                @staticmethod
                def validate_terminal_result(*_args, heartbeat_check=None, **_kwargs):
                    controller._run_guarded_gate(
                        ("blocked", "gate"), cwd=run_root,
                        heartbeat_check=heartbeat_check,
                    )
                    raise AssertionError("blocked gate unexpectedly returned")

            process = mock.Mock()
            process.pid = 4242
            process.poll.return_value = None
            process.stdout.fileno.return_value = 7
            with mock.patch.object(controller, "require_pidfd_support"), \
                    mock.patch.object(controller.subprocess, "Popen", return_value=process), \
                    mock.patch.object(controller, "open_pidfd", return_value=91), \
                    mock.patch.object(controller, "shutdown_spawned_child") as shutdown, \
                    mock.patch.object(controller.os, "close"), \
                    mock.patch.object(controller.os, "set_blocking"), \
                    mock.patch.object(
                        controller.selectors, "DefaultSelector",
                        return_value=mock.Mock(),
                    ):
                with self.assertRaisesRegex(controller.CorpusError, "ticker write failed"):
                    finalizer._source_results(
                        ReplayApi, snapshot, run_root, identity, publisher
                    )
            shutdown.assert_called_once_with(process, 91)

    def test_attempt_tamper_invalidates_published_authority(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot, run_root, output = self.make_run(Path(temporary))
            published = finalizer.finalize_corpus(
                FakeApi, snapshot=snapshot, run_root=run_root, output_parent=output
            )
            attempt = run_root / "runs" / "map1" / "attempt-0001" / "result.json"
            attempt.write_text("tampered", encoding="ascii")
            with self.assertRaisesRegex(controller.CorpusError, "identity changed"):
                finalizer.verify_final_corpus(
                    FakeApi,
                    snapshot=snapshot,
                    corpus_root=output / published["corpus_id"],
                )
            self.thaw(output / published["corpus_id"])

    def test_sealed_interrupted_publish_recovers_same_authority(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot, run_root, output = self.make_run(Path(temporary))
            with mock.patch.object(
                finalizer, "_rename_noreplace", side_effect=OSError("interrupted")
            ):
                with self.assertRaisesRegex(OSError, "interrupted"):
                    finalizer.finalize_corpus(
                        FakeApi, snapshot=snapshot, run_root=run_root,
                        output_parent=output,
                    )
            self.assertTrue((run_root / finalizer.SEAL_FILENAME).is_file())
            published = finalizer.finalize_corpus(
                FakeApi, snapshot=snapshot, run_root=run_root, output_parent=output
            )
            self.assertTrue((output / published["corpus_id"]).is_dir())
            self.thaw(output / published["corpus_id"])

    def test_finalize_cli_has_no_generation_overrides(self):
        args = controller.build_parser().parse_args([
            "finalize", "--snapshot", "/freeze", "--run-root", "/run",
            "--output-parent", "/archive",
        ])
        self.assertEqual("finalize", args.command)
        self.assertFalse(hasattr(args, "jobs"))
        self.assertFalse(hasattr(args, "port_base"))


if __name__ == "__main__":
    unittest.main()
