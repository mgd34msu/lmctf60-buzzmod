#!/usr/bin/env python3
"""Focused contract tests for the persistent production fleet runner."""

from __future__ import annotations

import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tests.test_server_bundle import BundleFixture


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools" / "fleet-runner.py"
TOPMAPS_PATH = ROOT / "tools" / "topmaps.txt"


def _load_runner():
    tools = str(RUNNER.parent)
    if tools not in sys.path:
        sys.path.insert(0, tools)
    spec = importlib.util.spec_from_file_location("fleet_runner", RUNNER)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load fleet runner")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _sha(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _json_bytes(value) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii") + b"\n"


def _write(path: Path, payload: bytes) -> dict:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    info = path.stat()
    return {
        "path": str(path.resolve()),
        "size": len(payload),
        "sha256": _sha(payload),
        "device": info.st_dev,
        "inode": info.st_ino,
    }


class FleetFixture:
    def __init__(self, root: Path, runner):
        self.root = root
        self.runner = runner
        self.state = root / "state"
        self.evidence = root / "evidence"
        self.inputs = root / "inputs"
        self.state.mkdir()
        self.evidence.mkdir()
        self.inputs.mkdir()
        self.runner_sha = _sha(RUNNER.read_bytes())
        self.topmaps_sha = _sha(TOPMAPS_PATH.read_bytes())
        bundle = BundleFixture(root, "bundle", b"module\n")
        self.bundle_id = "f" * 64
        self.final_corpus = {"fixture": "sealed-final-corpus"}
        self.active_bundle = {
            "bundle_id": self.bundle_id,
            "final_corpus": self.final_corpus,
            "files": [
                {"role": entry["role"], "size": Path(entry["source"]).stat().st_size,
                 "sha256": _sha(Path(entry["source"]).read_bytes())}
                for entry in bundle.entries
            ],
        }
        self.installed_bundle = _write(
            self.inputs / "install-state.json", _json_bytes({"fixture": "installed"})
        )
        self.bundle_verifier = runner._file_record(ROOT / "tools" / "server_bundle.py")
        self.engine = _write(self.inputs / "q2ded", b"engine\n")
        self.client = _write(self.inputs / "quake2", b"client\n")
        self.config = _write(self.inputs / "fleet.cfg", b"set dedicated 1\n")
        self.film = _write(self.inputs / "film.py", b"# pinned decoder\n")
        self.module_a = _write(self.inputs / "game.so", b"module\n")
        self.module_b = _write(self.inputs / "gamex86_64.so", b"module\n")
        self.runtime = runner._file_record(ROOT / "tools" / "fleet_runner_live.py")
        self.runes = {
            name: _write(self.inputs / "maps" / f"{name}.rune", b"rune\n")
            for name in runner.CANONICAL_TOPMAPS
        }
        self.bsps = {
            name: _write(self.inputs / "maps" / f"{name}.bsp", b"bsp\n")
            for name in runner.CANONICAL_TOPMAPS
        }
        self.snags = {
            name: _write(self.inputs / "maps" / f"{name}.snag", b"snag\n")
            for name in runner.CANONICAL_TOPMAPS
        }

    def verify_stopped(self):
        with mock.patch.object(
                self.runner, "_verify_installed_bundle",
                return_value=(self.active_bundle, self.bundle_verifier)), mock.patch.object(
                    self.runner, "_validate_installed_final_corpus", return_value={}):
            return self.runner.verify_stopped_residence_evidence(self.state, self.evidence)

    @staticmethod
    def _process(lane_index: int, engine: dict) -> dict:
        argv = [engine["path"], "+set", "port", str(28000 + lane_index)]
        return {
            "pid": 900_000_000 + lane_index,
            "boot_id": "00000000-0000-0000-0000-000000000001",
            "start_ticks": 1000 + lane_index,
            "executable": engine,
            "argv": argv,
            "command_sha256": _sha(b"\0".join(item.encode() for item in argv) + b"\0"),
            "pidfd_captured": True,
        }

    @staticmethod
    def _mode_tree(root: Path, writable: bool) -> None:
        for directory, names, files in os.walk(root, topdown=False):
            for name in files:
                os.chmod(Path(directory) / name, 0o600 if writable else 0o400)
            for name in names:
                os.chmod(Path(directory) / name, 0o700 if writable else 0o500)
            os.chmod(directory, 0o700 if writable else 0o500)

    def freeze(self) -> None:
        self._mode_tree(self.state, False)
        self._mode_tree(self.evidence, False)

    def thaw(self) -> None:
        self._mode_tree(self.state, True)
        self._mode_tree(self.evidence, True)

    def build(self) -> tuple[Path, Path]:
        processes = {
            lane: self._process(index, self.engine)
            for index, lane in enumerate(self.runner.LANES)
        }
        clients = {
            lane: self._process(index + 100, self.client)
            for index, lane in enumerate(self.runner.LANES)
        }
        maplists = {}
        for lane_index, lane in enumerate(self.runner.LANES):
            names = [
                self.runner.CANONICAL_TOPMAPS[(lane_index + step) % 20]
                for step in range(20)
            ]
            maplists[lane] = _write(
                self.inputs / "maplists" / f"{lane}.txt",
                ("\n".join(names) + "\n").encode(),
            )
        ledger = []
        previous = "0" * 64
        for lane_index, lane in enumerate(self.runner.LANES):
            for sequence in range(21):
                name = self.runner.CANONICAL_TOPMAPS[(lane_index + sequence) % 20]
                directory = self.evidence / "receipts" / lane / f"{sequence:02d}"
                demo = _write(directory / "serverrecord.dm2", b"demo:" + name.encode())
                pov = _write(directory / "pov.dm2", b"pov:" + name.encode())
                segment = _write(directory / "segments" / "console.log", b"console\n")
                players = []
                for player_index in range(10):
                    players.append({
                        "client": player_index + 1,
                        "instance": f"{lane}-{sequence}-{player_index}",
                        "name": f"[SG]Bot{player_index + 1:02d}",
                        "slot": player_index,
                        "team": 1 if player_index < 5 else 2,
                    })
                receipt = {
                    "format": "lmctf-fleet-residence-v1",
                    "fleet_id": "fixture-fleet",
                    "bundle_id": self.bundle_id,
                    "lane": lane,
                    "offset": lane_index,
                    "sequence": sequence,
                    "map": name,
                    "runner_sha256": self.runner_sha,
                    "topmaps_sha256": self.topmaps_sha,
                    "engine_generation": processes[lane],
                    "client_generation": clients[lane],
                    "bsp_file": self.bsps[name],
                    "rune_file": self.runes[name],
                    "rune_sha256": self.runes[name]["sha256"],
                    "snag_file": self.snags[name],
                    "sg_players": players,
                    "residence": {
                        "start_frame": 0,
                        "end_frame": 6000,
                        "red_flag_origin": [0.0, 1.0, 2.0],
                        "blue_flag_origin": [3.0, 4.0, 5.0],
                    },
                    "serverrecord": {
                        "demo_path": demo["path"],
                        "demo_sha256": demo["sha256"],
                        "demo_size": demo["size"],
                        "demo_frame_range": {"start": 1, "end_exclusive": 6001},
                    },
                    "console_segment": {
                        "path": "console.log",
                        "sha256": segment["sha256"],
                        "size": segment["size"],
                    },
                    "pov": {
                        "demo_path": pov["path"],
                        "demo_sha256": pov["sha256"],
                        "demo_size": pov["size"],
                        "spectator": "FleetObserver",
                        "target": "[SG]Bot01",
                        "start_confirmed": True,
                        "stop_confirmed": True,
                    },
                }
                receipt["receipt_hash"] = self.runner.receipt_hash(receipt)
                receipt_path = directory / "receipt.json"
                receipt_path.write_bytes(_json_bytes(receipt))
                entry = {
                    "format": "lmctf-fleet-ledger-entry-v1",
                    "index": len(ledger),
                    "previous_hash": previous,
                    "receipt_path": receipt_path.relative_to(self.evidence).as_posix(),
                    "receipt_hash": receipt["receipt_hash"],
                }
                entry["entry_hash"] = self.runner.ledger_entry_hash(entry)
                previous = entry["entry_hash"]
                ledger.append(entry)
        ledger_path = self.evidence / "evidence-ledger.jsonl"
        ledger_path.write_bytes(b"".join(_json_bytes(entry) for entry in ledger))
        lock_path = self.state / "fleet.lock"
        lock_path.write_bytes(b"")
        owner = {
            "format": "lmctf-fleet-owner-v2",
            "state": "SAFE_STOPPED",
            "fleet_id": "fixture-fleet",
            "bundle_id": self.bundle_id,
            "runner_sha256": self.runner_sha,
            "topmaps_sha256": self.topmaps_sha,
            "release_monotonic_ns": 123456,
            "processes": processes,
            "clients": clients,
            "final_corpus": self.final_corpus,
            "inputs": {
                "engine": self.engine,
                "client": self.client,
                "config": self.config,
                "film": self.film,
                "module_aliases": [self.module_a, self.module_b],
                "runtime": self.runtime,
                "installed_bundle": self.installed_bundle,
                "bundle_verifier": self.bundle_verifier,
            },
            "maplists": maplists,
            "ledger_entries": len(ledger),
            "ledger_tail_hash": previous,
            "lock_path": str(lock_path.resolve()),
        }
        (self.state / "fleet-owner.json").write_bytes(_json_bytes(owner))
        self.freeze()
        return self.state, self.evidence


class FleetRunnerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runner = _load_runner()

    def test_schedule_is_exact_rotation_and_wrap(self):
        self.assertEqual(self.runner.LANES, tuple(f"s{i:02d}" for i in range(1, 11)))
        self.assertEqual(self.runner.OFFSETS, tuple(range(10)))
        for lane_index, lane in enumerate(self.runner.LANES):
            got = [self.runner.expected_map(lane, sequence) for sequence in range(21)]
            expected = [
                self.runner.CANONICAL_TOPMAPS[(lane_index + sequence) % 20]
                for sequence in range(21)
            ]
            self.assertEqual(got, expected)
            self.assertEqual(got[20], got[0])

    def test_attested_film_module_executes_captured_bytes_after_path_swap(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "film.py"
            replacement = root / "replacement.py"
            source.write_text('MARKER = "accepted"\ndef walk_demo(*args, **kwargs): return {}\n')
            replacement.write_text(
                'MARKER = "replacement-after-attestation"\n'
                'def walk_demo(*args, **kwargs): return {}\n'
            )
            record = self.runner._file_record(source)
            original = self.runner.importlib.util.spec_from_file_location
            swapped = False

            def swap_after_capture(*args, **kwargs):
                nonlocal swapped
                replacement.replace(source)
                swapped = True
                return original(*args, **kwargs)

            with mock.patch.object(
                    self.runner.importlib.util, "spec_from_file_location",
                    side_effect=swap_after_capture):
                module = self.runner._load_film_module(record)
            self.assertTrue(swapped)
            self.assertEqual(module.MARKER, "accepted")

    def test_route_helpers_load_only_the_prevalidated_records(self):
        records = {
            name: {"sha256": str(index) * 64}
            for index, name in enumerate(
                ("runeio", "rolestat", "stallcensus"), start=1
            )
        }
        loaded = [object(), object(), object()]
        with mock.patch.object(
                self.runner, "_load_attested_module",
                side_effect=loaded) as loader:
            self.assertEqual(
                self.runner._load_route_helpers(records), tuple(loaded)
            )
        self.assertEqual(
            [call.args[1] for call in loader.call_args_list],
            [records["runeio"], records["rolestat"], records["stallcensus"]],
        )

    def test_live_runtime_loads_the_record_from_the_validated_spec(self):
        runtime = ROOT / "tools" / "fleet_runner_live.py"
        record = self.runner._file_record(runtime)
        module = mock.Mock(run_fleet=lambda *_args: None)
        with mock.patch.object(
                self.runner, "_validate_run_spec",
                return_value=({"runtime": record}, {})), mock.patch.object(
                    self.runner, "_load_attested_module",
                    return_value=module) as loader:
            self.assertIs(
                self.runner._load_live_runtime(Path("unused.json"), route_only=False),
                module,
            )
        self.assertEqual(loader.call_args.args[1], record)

    def test_one_engine_generation_owns_complete_native_cycle(self):
        process = FleetFixture._process(0, {
            "path": "/immutable/q2ded", "size": 1, "sha256": "1" * 64,
            "device": 1, "inode": 2,
        })
        cycle = self.runner.FleetCycle("s01", process)
        for sequence in range(22):
            cycle.map_committed(
                self.runner.expected_map("s01", sequence), process
            )
            if sequence < 21:
                cycle.level_exited(process)
        self.assertTrue(cycle.complete)
        self.assertEqual(cycle.completed_sequences, tuple(range(21)))
        changed = dict(process, start_ticks=process["start_ticks"] + 1)
        with self.assertRaisesRegex(ValueError, "generation"):
            cycle.map_committed(self.runner.expected_map("s01", 22), changed)

    def test_coordinated_launch_captures_ten_pinned_engine_generations(self):
        source = b"""
#include <signal.h>
#include <unistd.h>
static volatile sig_atomic_t done;
static void stop(int signal_number) { (void)signal_number; done = 1; }
int main(void) {
    signal(SIGTERM, stop);
    while (!done) pause();
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine_path = root / "q2ded"
            compile_result = subprocess.run(
                ["cc", "-x", "c", "-O2", "-o", str(engine_path), "-"],
                input=source, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            engine = _write(engine_path, engine_path.read_bytes())
            lanes = []
            for lane_index, lane in enumerate(self.runner.LANES):
                lane_root = root / lane
                lane_root.mkdir()
                maps = [
                    self.runner.CANONICAL_TOPMAPS[(lane_index + step) % 20]
                    for step in range(20)
                ]
                maplist = _write(
                    lane_root / "maplist.txt", ("\n".join(maps) + "\n").encode()
                )
                lanes.append({
                    "lane": lane,
                    "offset": lane_index,
                    "root": str(lane_root),
                    "maplist": maplist,
                    "argv": [str(engine_path), lane],
                })
            spec = root / "fleet.json"
            spec.write_bytes(_json_bytes({
                "format": "lmctf-fleet-run-spec-v1",
                "engine": engine,
                "lanes": lanes,
            }))
            identities = self.runner.launch_persistent_engines(spec)
            self.assertEqual(tuple(row["lane"] for row in identities), self.runner.LANES)
            self.assertEqual(len({row["pid"] for row in identities}), 10)
            self.assertEqual(len({row["release_monotonic_ns"] for row in identities}), 1)
            self.assertTrue(all(row["executable"] == engine for row in identities))

    def test_complete_stopped_authority_verifies(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = FleetFixture(Path(temporary), self.runner)
            state, evidence = fixture.build()
            try:
                receipts = fixture.verify_stopped()
                self.assertEqual(len(receipts), 210)
                generations = {}
                for _path, receipt in receipts:
                    generations.setdefault(receipt["lane"], set()).add(
                        (receipt["engine_generation"]["pid"],
                         receipt["engine_generation"]["start_ticks"])
                    )
                self.assertTrue(
                    all(len(values) == 1 for values in generations.values())
                )
            finally:
                fixture.thaw()

    def test_held_lock_and_ledger_tamper_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = FleetFixture(Path(temporary), self.runner)
            state, evidence = fixture.build()
            lock = os.open(state / "fleet.lock", os.O_RDONLY)
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                with self.assertRaisesRegex(ValueError, "lock"):
                    fixture.verify_stopped()
            finally:
                os.close(lock)
            try:
                fixture.thaw()
                ledger = evidence / "evidence-ledger.jsonl"
                payload = ledger.read_bytes()
                ledger.write_bytes(payload.replace(b'"index":1', b'"index":9', 1))
                fixture.freeze()
                with self.assertRaises(ValueError):
                    fixture.verify_stopped()
            finally:
                fixture.thaw()

    def test_runner_refuses_development_wavewatch(self):
        completed = subprocess.run(
            [sys.executable, "-B", str(RUNNER), "verify",
             "--state-root", "/tmp/no-such-fleet-state",
             "--evidence-root", "/tmp/no-such-fleet-evidence"],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, check=False,
            env={**os.environ, "LMCTF_WAVEWATCH_ACTIVE": "1"},
        )
        self.assertEqual(completed.returncode, 2)
        self.assertIn("wavewatch", completed.stderr.lower())

    def test_current_process_generation_is_not_stopped(self):
        line = Path(f"/proc/{os.getpid()}/stat").read_text()
        fields = line[line.rfind(")") + 2:].split()
        process = {
            "pid": os.getpid(),
            "boot_id": Path("/proc/sys/kernel/random/boot_id").read_text().strip(),
            "start_ticks": int(fields[19]),
        }
        self.assertTrue(self.runner._process_current(process))


if __name__ == "__main__":
    unittest.main()
