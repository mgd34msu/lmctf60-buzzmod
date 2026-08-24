#!/usr/bin/env python3
"""Executable persistent-cycle test with ten fake native engine processes."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest

from tests.test_server_bundle import BundleFixture
from tools import server_bundle


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
    if (argc != 2) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    snprintf(marker, sizeof(marker), "%s.go", argv[1]);
    while (1) {
        struct stat info;
        if (lstat(marker, &info) == 0) {
            unlink(marker);
            FILE *out = fopen(argv[1], "wb"); if (!out) return 3;
            fputs("pov\n", out); fclose(out);
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


if __name__ == "__main__":
    unittest.main()
