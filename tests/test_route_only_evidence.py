#!/usr/bin/env python3
"""Focused no-closure and tamper tests for route-only ordinary-match evidence."""

from __future__ import annotations

import importlib.util
import io
import json
import os
from pathlib import Path
import re
import sqlite3
import stat
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest import mock

from tests.test_server_bundle import BundleFixture
from tools import server_bundle


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools" / "fleet-runner.py"
RUNTIME = ROOT / "tools" / "fleet_runner_live.py"


def _runner():
    tools = str(RUNNER.parent)
    if tools not in sys.path:
        sys.path.insert(0, tools)
    spec = importlib.util.spec_from_file_location("route_only_evidence_runner", RUNNER)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load route-only runner")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _runtime():
    spec = importlib.util.spec_from_file_location("route_only_evidence_live", RUNTIME)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load route-only live runner")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _players():
    return [
        {"name": f"[SG]Bot{index:02d}", "team": 1 if index <= 5 else 2}
        for index in range(1, 11)
    ]


def _database(path: Path, *, extra_match: bool = False, combat: bool = True) -> None:
    connection = sqlite3.connect(path)
    try:
        connection.execute(
            "CREATE TABLE matches (match_id INTEGER PRIMARY KEY, mapname TEXT, "
            "red_caps INTEGER, blue_caps INTEGER)"
        )
        connection.execute(
            "CREATE TABLE sg_session_events (match_id INTEGER, client_name TEXT, "
            "is_bot INTEGER, team INTEGER, caps INTEGER, steals INTEGER, "
            "returns INTEGER, kills INTEGER, deaths INTEGER)"
        )
        # The all-zero objective counters model exactly the allowed local-only
        # condition.  Combat remains independently required.
        connection.execute("INSERT INTO matches VALUES (1, 'lmctf01', 0, 0)")
        for index, player in enumerate(_players(), 1):
            connection.execute(
                "INSERT INTO sg_session_events VALUES (?,?,?,?,?,?,?,?,?)",
                (1, player["name"], 1, player["team"], 0, 0, 0,
                 1 if combat and index == 1 else 0,
                 1 if combat and index == 6 else 0),
            )
        if extra_match:
            connection.execute("INSERT INTO matches VALUES (2, 'othermap', 0, 0)")
        connection.commit()
    finally:
        connection.close()


class RouteOnlyEvidenceTest(unittest.TestCase):
    def test_pov_wait_honors_campaign_deadline(self):
        live = _runtime()
        read_fd, write_fd = os.pipe()
        try:
            with os.fdopen(read_fd, "rb", buffering=0) as client_stdout:
                os.set_blocking(client_stdout.fileno(), False)
                run = SimpleNamespace(
                    lane="r01", pov_started=False, pov_stopped=False,
                    client=SimpleNamespace(stdout=client_stdout, poll=lambda: None),
                    client_buffer=bytearray(), client_log=io.BytesIO(),
                )
                started = time.monotonic()
                with self.assertRaisesRegex(ValueError, "did not confirm"):
                    live._await_pov_lifecycle({}, run, started + 0.05)
                self.assertLess(time.monotonic() - started, 0.5)
        finally:
            os.close(write_fd)

    @classmethod
    def setUpClass(cls):
        cls.runner = _runner()

    def test_finalizer_loader_requires_the_attested_sibling_bytes(self):
        record = self.runner._file_record(ROOT / "tools" / "rune_corpus_finalizer.py")
        finalizer = self.runner._load_route_finalizer(record)
        self.assertTrue(callable(finalizer.verify_final_corpus))
        mismatch = {**record, "sha256": "0" * 64}
        with self.assertRaisesRegex(ValueError, "bytes differ"):
            self.runner._load_route_finalizer(mismatch)

    def test_controller_loader_registers_dataclass_module_identity(self):
        record = self.runner._file_record(ROOT / "tools" / "rune_corpus_controller.py")
        loaded = self.runner._load_route_controller(record)
        self.assertTrue(callable(loaded.verify_snapshot))
        self.assertIs(loaded, sys.modules[loaded.__name__])

    def test_authority_exporter_uses_verified_finalizer_result(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            snapshot = root / "snapshot"
            corpus_root = root / ("e" * 64)
            snapshot.mkdir()
            corpus_root.mkdir()
            authority_path = corpus_root / "corpus-authority.json"
            authority_path.write_bytes(self.runner._canonical({"fixture": True}))
            authority_path.chmod(0o444)
            payload, info = self.runner._read_regular(authority_path)
            authority_record = {
                "path": str(authority_path), "mode": stat.S_IMODE(info.st_mode),
                "size": len(payload), "sha256": self.runner._hash(payload),
            }
            verified = {
                "corpus_id": corpus_root.name, "authority": authority_record,
                "run_root": str(root / "run"), "port_base": 62000, "results": [],
            }
            fake_finalizer = SimpleNamespace(
                verify_final_corpus=lambda *_args, **_kwargs: verified
            )
            with (
                mock.patch.object(
                    self.runner, "_load_route_controller", return_value=object()
                ),
                mock.patch.object(
                        self.runner, "_load_route_finalizer",
                        return_value=fake_finalizer
                ),
            ):
                exported = self.runner.build_route_controller_authority(
                    snapshot, corpus_root
                )
            self.assertEqual(verified["corpus_id"], exported["corpus_id"])
            self.assertEqual(verified["authority"], exported["corpus_authority"])
            self.assertEqual([], exported["results"])

    def test_final_controller_remainder_selects_only_ordered_candidates(self):
        maps = tuple(
            line.strip()
            for line in (ROOT / "tools" / "rune-corpus-maps.txt").read_text().splitlines()
            if line.strip()
        )
        results = {
            map_name: ("PASS", {"map": map_name})
            for map_name in maps
        }
        results["lmctf01"] = ("ROUTE_ONLY", {"map": "lmctf01"})
        results["lmctf15"] = ("ROUTE_ONLY", {"map": "lmctf15"})
        selected = self.runner._select_route_only_results(maps, results)
        self.assertEqual(("r01", "r04"),
                         self.runner._selected_route_only_lanes(selected))
        self.runner._validate_route_only_lane_selection(
            {"r01": {"map": "lmctf01"}, "r04": {"map": "lmctf15"}}, selected
        )
        with self.assertRaisesRegex(ValueError, "selection"):
            self.runner._validate_route_only_lane_selection(
                {"r01": {"map": "lmctf01"}, "r02": {"map": "lmctf06"}}, selected
            )
        non_candidate = next(name for name in maps if name not in self.runner.ROUTE_ONLY_MAPS)
        results[non_candidate] = ("ROUTE_ONLY", {"map": non_candidate})
        with self.assertRaisesRegex(ValueError, "non-candidate"):
            self.runner._select_route_only_results(maps, results)

    def test_final_controller_remainder_rejects_missing_result(self):
        maps = ("lmctf01", "lmctf06")
        with self.assertRaisesRegex(ValueError, "inventory"):
            self.runner._select_route_only_results(
                maps, {"lmctf01": ("ROUTE_ONLY", {"map": "lmctf01"})}
            )

    def test_route_telemetry_requires_every_bot_alive_at_least_once(self):
        names = [player["name"] for player in _players()]
        live = {name: 1 for name in names}
        self.runner._require_route_census_alive(names, live)
        with self.assertRaisesRegex(ValueError, "alive"):
            self.runner._require_route_census_alive(names, {name: 0 for name in names})
        live[names[-1]] = 0
        with self.assertRaisesRegex(ValueError, re.escape(names[-1])):
            self.runner._require_route_census_alive(names, live)

    def test_real_authority_loop_rejects_full_manifest_mutations(self):
        maps = tuple(
            line for line in (ROOT / "tools" / "rune-corpus-maps.txt").read_text().splitlines()
            if line
        )
        self.assertEqual(175, len(maps))
        self.assertEqual(175, len(set(maps)))

        def final_record(path: Path) -> dict:
            current = self.runner._file_record(path)
            return {
                "path": current["path"],
                "mode": stat.S_IMODE(path.stat().st_mode),
                "size": current["size"],
                "sha256": current["sha256"],
            }

        def build(root: Path):
            corpus_id = "e" * 64
            snapshot = root / "snapshot"
            run_root = root / "run"
            corpus_root = root / "published" / corpus_id
            snapshot.mkdir(parents=True)
            run_root.mkdir()
            corpus_root.mkdir(parents=True)
            manifest = snapshot / "rune-corpus-maps.txt"
            manifest.write_text("\n".join(maps) + "\n", encoding="ascii")
            engine = root / "q2ded"
            engine.write_bytes(b"engine\n")
            result_items = []
            for index, map_name in enumerate(maps):
                attempt = run_root / "runs" / map_name / "attempt-0001"
                result_path = attempt / "result.json"
                result_path.parent.mkdir(parents=True)
                classification = "ROUTE_ONLY" if map_name == "lmctf01" else "PASS"
                result_path.write_bytes(self.runner._canonical({
                    "map": map_name, "stable_port": 62000 + index,
                    "classification": classification, "attempt": 1,
                    "route_contract": (
                        "local_only" if classification == "ROUTE_ONLY" else "complete"
                    ),
                    "artifact_sha256": "a" * 64 if map_name == "lmctf01" else "c" * 64,
                }))
                result_path.chmod(0o444)
                pointer = run_root / "runs" / map_name / "result.json"
                pointer.write_bytes(self.runner._canonical({
                    "map": map_name, "classification": "GEN_FAIL",
                }))
                result_items.append({
                    "map": map_name, "stable_port": 62000 + index,
                    "classification": classification,
                    "route_contract": (
                        "local_only" if classification == "ROUTE_ONLY" else "complete"
                    ),
                    "attempt": 1,
                    "attempt_result": final_record(result_path),
                })
            corpus_authority = {
                "format": "lmctf-final-corpus-fixture-v1",
                "corpus_id": corpus_id,
            }
            corpus_authority_path = corpus_root / "corpus-authority.json"
            corpus_authority_path.write_bytes(self.runner._canonical(corpus_authority))
            corpus_authority_path.chmod(0o444)
            engine_record = self.runner._file_record(engine)
            authority = {
                "controller": self.runner._file_record(
                    ROOT / "tools" / "rune_corpus_controller.py"
                ),
                "finalizer": {"fixture": "hash-attested-finalizer"},
                "snapshot": str(snapshot), "run_root": str(run_root),
                "corpus_authority": final_record(corpus_authority_path),
                "corpus_id": corpus_id,
                "results": result_items,
            }
            snapshot_roles = {
                "engine": {"sha256": engine_record["sha256"]},
                "module_primary": {"sha256": "b" * 64},
                "module_secondary": {"sha256": "b" * 64},
                "map_manifest": {"path": manifest.name},
                "asset:lmctf01": {"sha256": "d" * 64},
            }
            bundle_roles = {
                "asset:lmctf01": {"sha256": "d" * 64},
                "bsp:lmctf01": {"sha256": "d" * 64},
                "rune:lmctf01": {"sha256": "a" * 64},
            }
            return (authority, bundle_roles, engine_record, snapshot_roles,
                    corpus_authority, json.loads(json.dumps(result_items)))

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (
                authority, roles, engine, snapshot_roles, corpus_authority,
                verified_results,
            ) = build(root)
            controller_path = ROOT / "tools" / "rune_corpus_controller.py"

            def invoke(value, role_records, engine_record, role_snapshot,
                       final_authority, final_results):
                # Finalizer tests own full revalidation. This outer call proves
                # only fleet binding to the sealed 175-map inventory.
                fixture = SimpleNamespace(
                    DEFAULT_PORT_BASE=62000,
                    verify_snapshot=lambda _snapshot: {"by_role": role_snapshot},
                    validate_manifest=lambda _manifest: list(maps),
                    _load_json_regular=lambda path: (
                        json.loads(path.read_text()), path.read_bytes()
                    ),
                )
                loader = SimpleNamespace(exec_module=lambda _module: None)
                module_spec = SimpleNamespace(loader=loader)
                original_spec = self.runner.importlib.util.spec_from_file_location
                original_module = self.runner.importlib.util.module_from_spec

                def spec_from_file_location(name, path):
                    if Path(path) == controller_path:
                        return module_spec
                    return original_spec(name, path)

                def module_from_spec(spec):
                    return fixture if spec is module_spec else original_module(spec)

                finalizer = SimpleNamespace(
                    verify_final_corpus=lambda _controller, *, snapshot, corpus_root: {
                        "corpus_id": final_authority["corpus_id"],
                        "authority": value["corpus_authority"],
                        "run_root": value["run_root"],
                        "port_base": 62000,
                        "results": final_results,
                    }
                )
                with mock.patch.object(
                        self.runner.importlib.util, "spec_from_file_location",
                        side_effect=spec_from_file_location), mock.patch.object(
                            self.runner.importlib.util, "module_from_spec",
                            side_effect=module_from_spec), mock.patch.object(
                                self.runner, "_load_route_finalizer", return_value=finalizer,
                                create=True):
                    return self.runner._validate_route_controller_authority(
                        value, role_records, engine_record, [{"sha256": "b" * 64}] * 2
                    )

            self.assertEqual(
                {"lmctf01"},
                set(invoke(
                    authority, roles, engine, snapshot_roles, corpus_authority,
                    verified_results,
                )),
            )

            mutation_index = 0

            def reject(mutator, expression):
                nonlocal mutation_index
                mutation_index += 1
                (
                    value, roles, engine, snapshot_roles, final_authority,
                    final_results,
                ) = build(root / f"mutation-{mutation_index}")
                mutator(value)
                with self.assertRaisesRegex(ValueError, expression):
                    invoke(
                        value, roles, engine, snapshot_roles, final_authority,
                        final_results,
                    )

            reject(lambda value: value.pop("corpus_authority"), "incomplete")
            def tamper_corpus_authority(value):
                path = Path(value["corpus_authority"]["path"])
                path.chmod(0o644)
                path.write_bytes(self.runner._canonical({"corpus_id": "a" * 64}))
            reject(tamper_corpus_authority, "identity drift")
            reject(lambda value: value.update(corpus_id="a" * 64), "content-addressed")
            reject(lambda value: value["results"].pop(), "inventory")
            reject(lambda value: value["results"].append(dict(value["results"][0])), "inventory")
            def reorder(value):
                value["results"][0], value["results"][1] = (
                    value["results"][1], value["results"][0]
                )
            reject(reorder, "drift")
            reject(lambda value: value["results"].__setitem__(0, value["results"][1]), "drift")
            reject(lambda value: value["results"].__setitem__(1, value["results"][0]), "drift")
            reject(lambda value: value["results"][0].update(stable_port=62001), "drift")
            reject(lambda value: value["results"][0].update(
                attempt_result=value["results"][1]["attempt_result"]
            ), "drift")

    def test_run_spec_rejects_duplicate_or_mismatched_candidate_lanes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine = root / "engine"
            engine.write_bytes(b"engine\n")
            game = root / "r01" / "game"
            game.mkdir(parents=True)
            record = self.runner._file_record(engine)
            lane = {
                "lane": "r01", "map": "lmctf01", "root": str(root / "r01"),
                "argv": [str(engine), "+set", "game", "game"],
                "client_argv": [str(engine)], "serverrecord_dir": str(game / "demos"),
                "pov_demo": str(root / "r01" / "pov.dm2"), "game_root": str(game),
                "statsdb_backup": str(game / "route-only-session.db"),
                "artifacts": {field: record for field in
                              ("bsp_file", "rune_file", "snag_file")},
            }
            common = {
                "format": self.runner.FORMAT_ROUTE_SPEC, "campaign_id": "fixture",
                "engine": record, "client": record, "route_config": record, "film": record,
                "runtime": record, "module_aliases": [], "spectator": "Observer",
                "target": "[SG]Bot01", "timeout_seconds": 60, "installed_bundle": record,
                "controller_authority": {}, "lanes": [],
            }
            spec = root / "spec.json"

            def reject(lanes, expression):
                spec.write_bytes(self.runner._canonical({**common, "lanes": lanes}))
                with self.assertRaisesRegex(ValueError, expression):
                    self.runner._validate_route_only_run_spec(spec)

            reject([{**lane, "map": "lmctf06"}], "fixed authority")
            reject([lane, dict(lane)], "ordered subset")
            reject([{**lane, "lane": "r11", "map": "arbitrary"}], "fixed authority")

    def test_run_spec_rejects_shared_route_or_game_roots(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine = root / "engine"
            engine.write_bytes(b"engine\n")
            record = self.runner._file_record(engine)
            common = {
                "format": self.runner.FORMAT_ROUTE_SPEC, "campaign_id": "fixture",
                "engine": record, "client": record, "route_config": record, "film": record,
                "runtime": record, "module_aliases": [], "spectator": "Observer",
                "target": "[SG]Bot01", "timeout_seconds": 60, "installed_bundle": record,
                "controller_authority": {}, "lanes": [],
            }

            def lane(name, map_name, lane_root, game_root):
                return {
                    "lane": name, "map": map_name, "root": str(lane_root),
                    "argv": [str(engine), "+set", "game",
                             game_root.relative_to(lane_root).as_posix()],
                    "client_argv": [str(engine)],
                    "serverrecord_dir": str(lane_root / f"{name}-demos"),
                    "pov_demo": str(lane_root / f"{name}.dm2"),
                    "game_root": str(game_root),
                    "statsdb_backup": str(game_root / "route-only-session.db"),
                    "artifacts": {field: record for field in
                                  ("bsp_file", "rune_file", "snag_file")},
                }

            def reject(lanes, expression):
                spec = root / f"{expression}.json"
                spec.write_bytes(self.runner._canonical({**common, "lanes": lanes}))
                with self.assertRaisesRegex(ValueError, expression):
                    self.runner._validate_route_only_run_spec(spec)

            shared = root / "shared"
            game = shared / "game"
            game.mkdir(parents=True)
            reject([
                lane("r01", "lmctf01", shared, game),
                lane("r02", "lmctf06", shared, game),
            ], "root")

            parent, child = root / "nested", root / "nested" / "child"
            child_game = child / "game"
            child_game.mkdir(parents=True)
            reject([
                lane("r01", "lmctf01", parent, child_game),
                lane("r02", "lmctf06", child, child_game),
            ], "game root")

    def test_zero_capture_steal_and_return_is_accepted_with_combat(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "session.db"
            _database(path)
            result = self.runner._route_session_database(path, _players(), "lmctf01")
        self.assertEqual(result["teams"]["red"]["captures"], 0)
        self.assertEqual(result["teams"]["blue"]["steals"], 0)
        self.assertEqual(result["teams"]["blue"]["returns"], 0)

    def test_session_database_tamper_or_missing_combat_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            multiple = root / "multiple.db"
            _database(multiple, extra_match=True)
            with self.assertRaisesRegex(ValueError, "fresh match"):
                self.runner._route_session_database(multiple, _players(), "lmctf01")
            no_combat = root / "no-combat.db"
            _database(no_combat, combat=False)
            with self.assertRaisesRegex(ValueError, "combat"):
                self.runner._route_session_database(no_combat, _players(), "lmctf01")

    def test_route_cycle_rejects_a_map_change_without_a_timed_exit(self):
        process = {
            "pid": 9, "boot_id": "boot", "start_ticks": 10,
            "command_sha256": "a" * 64,
        }
        cycle = self.runner.RouteOnlyCycle("r01", process)
        cycle.map_committed("lmctf01", process)
        with self.assertRaisesRegex(ValueError, "without a timed"):
            cycle.map_committed("nextmap", process)
        cycle.level_exited(process)
        cycle.map_committed("nextmap", process)
        self.assertTrue(cycle.complete)

    def test_console_origin_must_match_rounded_serverrecord_sample(self):
        players = [{"name": "[SG]Bot01", "client": 1, "team": 1}]
        telemetry = {"observations": [{
            "player": "[SG]Bot01", "frame": 42, "seed": 0, "link": -1,
            "action": -1, "origin": [1, -2, 3],
        }]}
        decoded = {
            "wire_framenums": [42],
            "tracks": {1: [(1, 1.4, -1.6, 3.49, 0.0)]},
        }
        self.runner._verify_route_demo_positions(telemetry, decoded, players)
        telemetry["observations"][0]["origin"][0] = 2
        with self.assertRaisesRegex(ValueError, "origin"):
            self.runner._verify_route_demo_positions(telemetry, decoded, players)

    def test_private_game_tree_must_match_supplied_bundle_artifacts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = BundleFixture(root, "bundle", b"module\n")
            archive, manifest = root / "bundle.tar", root / "bundle.json"
            server_bundle.build_bundle(fixture.write_spec(), archive, manifest)
            install = root / "installed"
            server_bundle.install_bundle(manifest, archive, install, expected_active=None)
            active, _verifier = self.runner._verify_installed_bundle(
                self.runner._file_record(install / "install-state.json")
            )
            roles = self.runner._bundle_role_records(active)
            lanes = {}
            for lane in self.runner.ROUTE_ONLY_LANES:
                map_name = self.runner.expected_route_only_map(lane)
                game = root / lane / "game"
                (game / "maps").mkdir(parents=True)
                records = {}
                for field, role, name in (
                        ("bsp_file", f"bsp:{map_name}", f"{map_name}.bsp"),
                        ("rune_file", f"rune:{map_name}", f"{map_name}.rune"),
                        ("snag_file", f"snag:{map_name}", f"{map_name}.snag")):
                    payload = Path(roles[role]["path"]).read_bytes()
                    (game / "maps" / name).write_bytes(payload)
                    supplied = root / "supplied" / lane / name
                    supplied.parent.mkdir(parents=True, exist_ok=True)
                    supplied.write_bytes(payload)
                    records[field] = self.runner._file_record(supplied)
                for name, role in (
                        ("game.so", "module-primary"),
                        ("gamex86_64.so", "module-secondary"),
                        ("route-only-maplist.txt", "route-only-maplist")):
                    (game / name).write_bytes(Path(roles[role]["path"]).read_bytes())
                lanes[lane] = {"game_root": game, "artifacts": records}
            (root / "r01" / "game" / "maps" / "lmctf01.bsp").write_bytes(b"tampered\n")
            with self.assertRaisesRegex(ValueError, "runtime BSP"):
                _runtime()._route_lane_inputs(
                    self.runner, lanes, roles, self.runner.ROUTE_ONLY_LANES
                )

    def test_backup_is_issued_in_intermission_before_immediate_next_identity(self):
        live = _runtime()
        process = {
            "pid": 9, "boot_id": "boot", "start_ticks": 10,
            "command_sha256": "a" * 64,
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            command_stream = io.BytesIO()
            engine = SimpleNamespace(
                identity=process,
                process=SimpleNamespace(stdin=command_stream),
            )
            server_log = (root / "server.log").open("wb")
            client_log = (root / "client.log").open("wb")
            try:
                run = live.LaneRun(
                    "r01",
                    {"map": "lmctf01", "serverrecord_dir": root,
                     "pov_demo": root / "pov.dm2",
                     "statsdb_backup": root / "route-only-session.db"},
                    engine, None, self.runner.RouteOnlyCycle("r01", process),
                    server_log, client_log,
                )
                spec = {"campaign_id": "fixture", "spectator": "Observer"}
                live._consume_route_engine(
                    self.runner, None, spec, run,
                    b"slipgate: rune identity committed map=lmctf01 bsp=1\n",
                    root, [], "b" * 64, {"lmctf01": {}}, float("inf"),
                )
                live._consume_route_engine(
                    self.runner, None, spec, run, b"Timelimit hit.\n",
                    root, [], "b" * 64, {"lmctf01": {}}, float("inf"),
                )
                self.assertIn(b"sv statsdb backup route-only-session.db\n",
                              command_stream.getvalue())
                live._consume_route_engine(
                    self.runner, None, spec, run,
                    b"EXITLEVEL frame=6000 time=600.0 changemap=nextmap\n",
                    root, [], "b" * 64, {"lmctf01": {}}, float("inf"),
                )
                self.assertTrue(run.cycle.pending_exit)
            finally:
                server_log.close()
                client_log.close()


if __name__ == "__main__":
    unittest.main()
