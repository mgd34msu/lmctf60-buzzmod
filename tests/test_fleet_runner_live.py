#!/usr/bin/env python3
"""Executable persistent-cycle test with ten fake native engine processes."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tests.test_server_bundle import BundleFixture
from tests.test_rune_artifact import _build_rune, _fix_payload_and_header_crc
from tools import runeio, server_bundle


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools" / "fleet-runner.py"
RUNTIME = ROOT / "tools" / "fleet_runner_live.py"


def _load():
    spec = importlib.util.spec_from_file_location("fleet_runner_live_test_core", RUNNER)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _canonical(value) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode() + b"\n"


def _write(path: Path, payload: bytes) -> dict:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    info = path.stat()
    return {"path": str(path.resolve()), "size": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
            "device": info.st_dev, "inode": info.st_ino}


ENGINE_SOURCE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc, char **argv) {
    char maps[20][64], line[512], current[64] = "";
    int sequence = 0;
    FILE *list;
    if (argc != 4 || !(list = fopen(argv[1], "r"))) return 2;
    for (int i = 0; i < 20; i++) if (fscanf(list, "%63s", maps[i]) != 1) return 3;
    fclose(list); setvbuf(stdout, NULL, _IONBF, 0);
    while (fgets(line, sizeof(line), stdin)) {
        if (!strncmp(line, "map ", 4)) {
            sscanf(line + 4, "%63s", current); sequence = 0;
            printf("slipgate: rune identity committed map=%s bsp=1 entity_crc=2 physics=3\n",
                   current);
        } else if (!strncmp(line, "serverrecord ", 13)) {
            char name[256], path[1024]; FILE *demo;
            sscanf(line + 13, "%255s", name);
            snprintf(path, sizeof(path), "%s/%s.dm2", argv[2], name);
            demo = fopen(path, "wb"); if (!demo) return 4;
            fprintf(demo, "%s\n", current); fclose(demo);
        } else if (!strncmp(line, "sv sg list", 10)) {
            puts("FleetObserver entered the game");
            for (int i = 0; i < 10; i++)
                printf("%3d  [SG]Bot%02d            %s      0  1.00 attack      1\n",
                       i, i + 1, i < 5 ? "red" : "blue");
            puts("slipgate: 10 bots");
        } else if (!strncmp(line, "sv povrecord ", 13) && !strstr(line, " off ")) {
            char marker[1024]; FILE *go;
            snprintf(marker, sizeof(marker), "%s.go", argv[3]);
            go = fopen(marker, "wb"); if (!go) return 5; fclose(go);
            for (int i = 0; i < 10; i++)
                printf("SGCENSUS [SG]Bot%02d: frm=1 alive=1\n", i + 1);
            usleep(100000);
            printf("EXITLEVEL frame=1 time=0.1 changemap=%s\n", maps[(sequence + 1) % 20]);
            sequence++;
            strcpy(current, maps[sequence % 20]);
            printf("slipgate: rune identity committed map=%s bsp=1 entity_crc=2 physics=3\n",
                   current);
        }
    }
    return 0;
}
'''


CLIENT_SOURCE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc, char **argv) {
    char marker[1024];
    int delayed = 0;
    if (argc != 2) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    snprintf(marker, sizeof(marker), "%s.go", argv[1]);
    while (1) {
        struct stat info;
        if (lstat(marker, &info) == 0) {
            unlink(marker);
            FILE *out = fopen(argv[1], "wb"); if (!out) return 3;
            fputs("pov\n", out); fclose(out);
            if (!delayed) { usleep(200000); delayed = 1; }
            puts("recording to pov.dm2."); puts("Stopped demo.");
        }
        usleep(20000);
    }
    return 0;
}
'''


FILM_SOURCE = '''
from pathlib import Path
def walk_demo(path, strict=False):
    name = Path(path).read_text(encoding="ascii").strip()
    epochs = {index: [(1, f"[SG]Bot{index + 1:02d}" + chr(92) + "male/rb-rm")]
              for index in range(10)}
    return {"map": name, "svrecord": True, "wire_framenums": [1],
            "skin_epochs": epochs, "frames": 1, "terminated": True,
            "parse_complete": True}
'''


ROUTE_ENGINE_SOURCE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int write_database(const char *map) {
    char command[4096];
    int size = snprintf(command, sizeof(command),
        "python3 -c \"import sqlite3; c=sqlite3.connect('game/route-only-session.db'); "
        "c.execute('CREATE TABLE matches (match_id INTEGER PRIMARY KEY, mapname TEXT, "
        "red_caps INTEGER, blue_caps INTEGER)'); "
        "c.execute('CREATE TABLE sg_session_events (match_id INTEGER, client_name TEXT, "
        "is_bot INTEGER, team INTEGER, caps INTEGER, steals INTEGER, returns INTEGER, "
        "kills INTEGER, deaths INTEGER)'); "
        "c.execute('INSERT INTO matches VALUES (1, ?, 0, 0)', ('%s',)); "
        "[(c.execute('INSERT INTO sg_session_events VALUES (1, ?, 1, ?, 0, 0, 0, ?, ?)', "
        "(f'[SG]Bot{i:02d}', 1 if i<=5 else 2, 1 if i in (1,6) else 0, "
        "1 if i in (2,7) else 0))) for i in range(1,11)]; c.commit()\"",
        map);
    return size > 0 && size < (int)sizeof(command) && system(command) == 0;
}

int main(int argc, char **argv) {
    char line[512], current[64] = "", name[256], marker[1024], demo[1024];
    if (argc != 5) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    while (fgets(line, sizeof(line), stdin)) {
        if (!strncmp(line, "map ", 4)) {
            sscanf(line + 4, "%63s", current);
            printf("slipgate: rune identity committed map=%s "
                   "bsp=1 entity_crc=2 physics=3\n", current);
        } else if (!strncmp(line, "serverrecord ", 13)) {
            FILE *out;
            sscanf(line + 13, "%255s", name);
            snprintf(demo, sizeof(demo), "game/demos/%s.dm2", name);
            out = fopen(demo, "wb"); if (!out) return 3;
            fprintf(out, "%s\n", current); fclose(out);
        } else if (!strncmp(line, "sv sg list", 10)) {
            puts("RouteObserver entered the game");
            for (int i = 0; i < 10; i++)
                printf("%3d  [SG]Bot%02d            %s      0  1.00 attack      1\n",
                       i, i + 1, i < 5 ? "red" : "blue");
            puts("slipgate: 10 bots");
        } else if (!strncmp(line, "sv povrecord ", 13) && !strstr(line, " off ")) {
            FILE *out;
            snprintf(marker, sizeof(marker), "%s.go", argv[4]);
            out = fopen(marker, "wb"); if (!out) return 4;
            fprintf(out, "%s\n", current); fclose(out);
            puts("slipgate: route contract local-only");
            printf("slipgate: rune ready %s, route=local-only\n", current);
            for (int bot = 1; bot <= 10; bot++) {
                int role = (bot == 2 || bot == 7) ? 1 : 0;
                printf("SG [SG]Bot%02d: role=%d seed=0 goal=100 sgoal=100 spd=0 "
                       "org=(0 0 0) link=-1 act=-1 hp=0 dh=0 dl=0 st=0.0 "
                       "gnd=1 eng=0 frm=100\n", bot, role);
            }
            for (int frame = 10; frame <= 6000; frame += 10)
                for (int bot = 1; bot <= 10; bot++)
                    printf("SGCENSUS [SG]Bot%02d: frm=%d alive=1\n", bot, frame);
            usleep(200000);
            puts("Timelimit hit.");
        } else if (!strncmp(line, "sv statsdb backup route-only-session.db", 39)) {
            if (!write_database(current)) return 5;
            puts("statsdb: backed up to route-only-session.db");
            printf("EXITLEVEL frame=6000 time=600.0 changemap=after\n");
            puts("slipgate: rune identity committed map=after bsp=1 entity_crc=2 physics=3");
        } else if (!strncmp(line, "quit", 4)) {
            return 0;
        }
    }
    return 0;
}
'''


ROUTE_CLIENT_SOURCE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc, char **argv) {
    char marker[1024], map[64];
    if (argc != 2) return 2;
    snprintf(marker, sizeof(marker), "%s.go", argv[1]);
    setvbuf(stdout, NULL, _IONBF, 0);
    while (1) {
        struct stat info;
        if (lstat(marker, &info) == 0) {
            FILE *in = fopen(marker, "rb"), *out = fopen(argv[1], "wb");
            if (!in || !out || !fgets(map, sizeof(map), in)) return 3;
            fputs(map, out); fclose(in); fclose(out); unlink(marker);
            puts("recording to pov.dm2."); puts("Stopped demo.");
        }
        usleep(10000);
    }
}
'''


ROUTE_FILM_SOURCE = '''
from pathlib import Path
def walk_demo(path, strict=False):
    map_name = Path(path).read_text(encoding="ascii").strip()
    if Path(path).name != "serverrecord.dm2":
        return {"map": map_name, "svrecord": False, "frames": 1,
                "terminated": True, "parse_complete": True}
    epochs = {index: [(1, f"[SG]Bot{index + 1:02d}" + chr(92) + "male/rb-rm")]
              for index in range(10)}
    tracks = {entity: [(frame, 0.0, 0.0, 0.0, 0.0)
                        for frame in range(1, 6001)]
              for entity in range(1, 11)}
    return {"map": map_name, "svrecord": True, "wire_framenums": list(range(1, 6001)),
            "skin_epochs": epochs, "tracks": tracks, "frames": 6000,
            "terminated": True, "parse_complete": True}
'''


def _route_rune(map_name: str) -> bytes:
    encoded = bytearray(_build_rune())
    struct.pack_into("<H", encoded, 4, runeio.RUNE_ROUTE_CONTRACT_LOCAL_ONLY)
    struct.pack_into("<H", encoded, runeio.RUNE_HEADER_BYTES + 14, runeio.RSF_OBJECTIVE)
    struct.pack_into(
        "<H", encoded, runeio.RUNE_HEADER_BYTES + runeio.RUNE_SEED_BYTES + 14,
        runeio.RSF_OBJECTIVE,
    )
    raw_name = map_name.encode("ascii")
    encoded[64:128] = raw_name + b"\0" * (64 - len(raw_name))
    _fix_payload_and_header_crc(encoded)
    return bytes(encoded)


class FleetRunnerLiveTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core = _load()

    def test_ten_engines_complete_cycle_and_wrap_without_pid_churn(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine, client = root / "q2ded", root / "quake2"
            for output, source in ((engine, ENGINE_SOURCE), (client, CLIENT_SOURCE)):
                compiled = subprocess.run(
                    ["cc", "-x", "c", "-O2", "-o", str(output), "-"],
                    input=source.encode(), stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, check=False,
                )
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
            engine_record = _write(engine, engine.read_bytes())
            client_record = _write(client, client.read_bytes())
            bundle = BundleFixture(root, "bundle", b"module\n")
            archive, manifest = root / "bundle.tar", root / "bundle.json"
            server_bundle.build_bundle(bundle.write_spec(), archive, manifest)
            install = root / "installed"
            installed = server_bundle.install_bundle(
                manifest, archive, install, expected_active=None
            )
            installed_bundle = self.core._file_record(install / "install-state.json")
            config = _write(root / "fleet.cfg", b"set dedicated 1\n")
            film = _write(root / "film.py", FILM_SOURCE.encode())
            module_a = _write(root / "game.so", b"module\n")
            module_b = _write(root / "gamex86_64.so", b"module\n")
            runtime = self.core._file_record(RUNTIME)
            artifact_root = root / "artifacts"
            artifacts = {}
            for name in self.core.CANONICAL_TOPMAPS:
                artifacts[name] = {
                    "bsp_file": _write(artifact_root / f"{name}.bsp", b"bsp\n"),
                    "rune_file": _write(artifact_root / f"{name}.rune", b"rune\n"),
                    "snag_file": _write(artifact_root / f"{name}.snag", b"snag\n"),
                    "red_flag_origin": [0.0, 0.0, 0.0],
                    "blue_flag_origin": [1.0, 1.0, 1.0],
                }
            lanes = []
            for lane_index, lane in enumerate(self.core.LANES):
                lane_root = root / lane
                demos = lane_root / "demos"
                demos.mkdir(parents=True)
                map_names = [
                    self.core.CANONICAL_TOPMAPS[(lane_index + step) % 20]
                    for step in range(20)
                ]
                maplist = _write(
                    lane_root / "maplist.txt", ("\n".join(map_names) + "\n").encode()
                )
                lanes.append({
                    "lane": lane, "offset": lane_index, "root": str(lane_root),
                    "maplist": maplist,
                    "argv": [str(engine), str(lane_root / "maplist.txt"), str(demos),
                             str(lane_root / "pov.dm2")],
                    "client_argv": [str(client), str(lane_root / "pov.dm2")],
                    "serverrecord_dir": str(demos),
                    "pov_demo": str(lane_root / "pov.dm2"),
                    "artifacts": artifacts,
                })
            spec = root / "run.json"
            spec.write_bytes(_canonical({
                "format": "lmctf-fleet-run-spec-v1", "fleet_id": "live-fixture",
                "engine": engine_record, "client": client_record, "config": config,
                "film": film, "runtime": runtime,
                "module_aliases": [module_a, module_b],
                "installed_bundle": installed_bundle,
                "spectator": "FleetObserver", "target": "[SG]Bot01",
                "timeout_seconds": 60, "lanes": lanes,
            }))
            state, evidence = root / "state", root / "evidence"
            completed = subprocess.run(
                [sys.executable, "-B", str(RUNNER), "run", "--spec", str(spec),
                 "--state-root", str(state), "--evidence-root", str(evidence)],
                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, check=False, timeout=30,
            )
            try:
                self.assertEqual(completed.returncode, 0, completed.stderr)
                receipts = self.core.verify_stopped_residence_evidence(state, evidence)
                self.assertEqual(len(receipts), 210)
                for lane in self.core.LANES:
                    generations = {
                        (receipt["engine_generation"]["pid"],
                         receipt["engine_generation"]["start_ticks"])
                        for _path, receipt in receipts if receipt["lane"] == lane
                    }
                    self.assertEqual(len(generations), 1)
            finally:
                for directory, names, files in os.walk(root):
                    os.chmod(directory, stat.S_IRWXU)
                    for name in files:
                        os.chmod(Path(directory) / name, stat.S_IRUSR | stat.S_IWUSR)

    def test_route_only_cli_runs_mixed_remainder_and_verifies_zero_no_op(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine, client = root / "q2ded", root / "quake2"
            for output, source in ((engine, ROUTE_ENGINE_SOURCE),
                                   (client, ROUTE_CLIENT_SOURCE)):
                compiled = subprocess.run(
                    ["cc", "-x", "c", "-O2", "-o", str(output), "-"],
                    input=source.encode(), stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, check=False,
                )
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
            engine_record = _write(engine, engine.read_bytes())
            client_record = _write(client, client.read_bytes())
            bundle = BundleFixture(root, "bundle", b"module\n")
            for map_name in self.core.ROUTE_ONLY_MAPS:
                entry = next(item for item in bundle.entries
                             if item["role"] == f"rune:{map_name}")
                Path(entry["source"]).write_bytes(_route_rune(map_name))
            archive, manifest = root / "bundle.tar", root / "bundle.json"
            server_bundle.build_bundle(bundle.write_spec(), archive, manifest)
            install = root / "installed"
            server_bundle.install_bundle(manifest, archive, install, expected_active=None)
            installed_bundle = self.core._file_record(install / "install-state.json")
            active, _verifier = self.core._verify_installed_bundle(installed_bundle)
            roles = self.core._bundle_role_records(active)
            film = _write(root / "route-film.py", ROUTE_FILM_SOURCE.encode())
            selected_lanes = ("r01", "r04")
            lanes = []
            for lane in selected_lanes:
                map_name = self.core.expected_route_only_map(lane)
                lane_root, game = root / lane, root / lane / "game"
                (game / "maps").mkdir(parents=True)
                (game / "demos").mkdir()
                for filename, role in (
                        ("game.so", "module-primary"),
                        ("gamex86_64.so", "module-secondary"),
                        ("route-only-maplist.txt", "route-only-maplist"),
                        (f"maps/{map_name}.bsp", f"bsp:{map_name}"),
                        (f"maps/{map_name}.rune", f"rune:{map_name}"),
                        (f"maps/{map_name}.snag", f"snag:{map_name}")):
                    destination = game / filename
                    destination.write_bytes(Path(roles[role]["path"]).read_bytes())
                lanes.append({
                    "lane": lane, "map": map_name, "root": str(lane_root),
                    "argv": [str(engine), "+set", "game", "game",
                             str(lane_root / "pov.dm2")],
                    "client_argv": [str(client), str(lane_root / "pov.dm2")],
                    "serverrecord_dir": str(game / "demos"),
                    "pov_demo": str(lane_root / "pov.dm2"),
                    "game_root": str(game),
                    "statsdb_backup": str(game / "route-only-session.db"),
                    "artifacts": {
                        field: self.core._file_record(game / "maps" / f"{map_name}.{suffix}")
                        for field, suffix in (("bsp_file", "bsp"), ("rune_file", "rune"),
                                              ("snag_file", "snag"))
                    },
                })
            spec = root / "route-run.json"
            spec.write_bytes(_canonical({
                "format": "lmctf-route-only-run-spec-v2", "campaign_id": "route-fixture",
                "engine": engine_record, "client": client_record,
                "route_config": self.core._file_record(Path(roles["route-only-config"]["path"])),
                "film": film, "runtime": self.core._file_record(RUNTIME),
                "module_aliases": [
                    self.core._file_record(Path(roles["module-primary"]["path"])),
                    self.core._file_record(Path(roles["module-secondary"]["path"])),
                ],
                "spectator": "RouteObserver", "target": "[SG]Bot01",
                "timeout_seconds": 60, "installed_bundle": installed_bundle,
                "controller_authority": {"fixture": "mixed"}, "lanes": lanes,
            }))
            state, evidence = root / "state", root / "evidence"
            live_spec = importlib.util.spec_from_file_location("fleet_runner_live", RUNTIME)
            live = importlib.util.module_from_spec(live_spec)
            previous_live = sys.modules.get("fleet_runner_live")
            sys.modules["fleet_runner_live"] = live
            live_spec.loader.exec_module(live)

            def controller_fixture(authority, _roles, _engine, _aliases):
                if authority["fixture"] == "zero":
                    return {}
                return {self.core.expected_route_only_map(lane): {
                    "fixture": "route-controller",
                    "map": self.core.expected_route_only_map(lane),
                } for lane in selected_lanes}

            try:
                with mock.patch.object(
                        self.core, "_validate_route_controller_authority",
                        side_effect=controller_fixture):
                    self.assertEqual(0, self.core.main([
                        "route-only-run", "--spec", str(spec), "--state-root", str(state),
                        "--evidence-root", str(evidence),
                    ]))
                    receipts = self.core.verify_stopped_route_only_evidence(state, evidence)
                self.assertEqual(len(selected_lanes), len(receipts))
                owner = json.loads((state / "route-only-owner.json").read_text())
                self.assertEqual("SAFE_STOPPED", owner["state"])
                self.assertTrue(owner["graceful_quit"])
                self.assertEqual(len(selected_lanes), owner["ledger_entries"])
                self.assertEqual(list(selected_lanes), owner["selected_lanes"])
                self.assertEqual(len(selected_lanes), owner["selected_count"])
                self.assertEqual(tuple(selected_lanes),
                                 tuple(receipt["lane"] for _path, receipt in receipts))
                self.assertTrue(all(
                    receipt["behavior"]["session"]["teams"][team][counter] == 0
                    for _path, receipt in receipts for team in ("red", "blue")
                    for counter in ("captures", "steals", "returns")
                ))
                zero_spec = root / "route-zero.json"
                zero_spec.write_bytes(_canonical({
                    **json.loads(spec.read_text()), "campaign_id": "route-zero",
                    "controller_authority": {"fixture": "zero"}, "film": engine_record,
                    "lanes": [],
                }))
                zero_state, zero_evidence = root / "zero-state", root / "zero-evidence"
                with mock.patch.object(
                        self.core, "_validate_route_controller_authority",
                        side_effect=controller_fixture), mock.patch.object(
                            live, "_start", side_effect=AssertionError("zero selection launched")):
                    self.assertEqual(0, self.core.main([
                        "route-only-run", "--spec", str(zero_spec),
                        "--state-root", str(zero_state), "--evidence-root", str(zero_evidence),
                    ]))
                    self.assertEqual((), self.core.verify_stopped_route_only_evidence(
                        zero_state, zero_evidence
                    ))
                zero_owner = json.loads((zero_state / "route-only-owner.json").read_text())
                self.assertTrue(zero_owner["no_op"])
                self.assertEqual(0, zero_owner["selected_count"])
                self.assertEqual({}, zero_owner["processes"])
                self.assertEqual(b"", (zero_evidence / "route-only-ledger.jsonl").read_bytes())
            finally:
                if previous_live is None:
                    sys.modules.pop("fleet_runner_live", None)
                else:
                    sys.modules["fleet_runner_live"] = previous_live
                for directory, names, files in os.walk(root):
                    os.chmod(directory, stat.S_IRWXU)
                    for name in files:
                        os.chmod(Path(directory) / name, stat.S_IRUSR | stat.S_IWUSR)


if __name__ == "__main__":
    unittest.main()
