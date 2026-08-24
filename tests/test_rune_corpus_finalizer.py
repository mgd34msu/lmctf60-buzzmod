#!/usr/bin/env python3
"""Focused publication tests for the final RUNE corpus authority."""

from __future__ import annotations

import json
import os
from pathlib import Path
import stat
import tempfile
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
    def preflight_python_runtime(_snapshot):
        return {"classification": "PASS"}

    @staticmethod
    def verify_snapshot(snapshot):
        return {"by_role": {"map_manifest": {"path": "maps.txt"}}}

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
