#!/usr/bin/env python3
"""Exercise runegen's two-engine RUNE/SNAG acceptance boundary."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tests.test_rune_artifact import _build_rune


RUNNER = ROOT / "tools/runegen.sh"
MAP = "runetest"


ENGINE = r'''#!/usr/bin/env bash
set -u
game=""
mapname=""
maxclients=""
exec_seen=0
map_seen=0
while [ "$#" -gt 0 ]; do
    if [ "$1" = "+set" ] && [ "${2:-}" = "game" ]; then
        game="${3:-}"
        shift 3
    elif [ "$1" = "+set" ] && [ "${2:-}" = "maxclients" ]; then
        maxclients="${3:-}"
        shift 3
    elif [ "$1" = "+exec" ]; then
        exec_seen=1
        shift 2
    elif [ "$1" = "+map" ]; then
        map_seen=1
        mapname="${2:-}"
        shift 2
    else
        shift
    fi
done
if [ "$exec_seen" -ne 1 ] || [ "$map_seen" -ne 1 ]; then
    exit 95
fi
count=0
if [ -f "$RUNEGEN_LAUNCH_COUNT" ]; then
    count="$(cat "$RUNEGEN_LAUNCH_COUNT")"
fi
count=$(( count + 1 ))
printf '%s\n' "$count" > "$RUNEGEN_LAUNCH_COUNT"
stage="$PWD/$game"
artifact="$stage/maps/$mapname.rune"
snag="$stage/maps/$mapname.snag"
IFS= read -r first || exit 94
IFS= read -r second || exit 94
IFS= read -r third || exit 94
if IFS= read -r extra; then
    echo "unexpected fourth command: $extra" >&2
    exit 94
fi
if [ "$first" != "maxclients" ] || [ "$third" != "quit" ]; then
    echo "invalid command envelope" >&2
    exit 94
fi
printf 'launch=%s:%s\n' "$count" "$second" >> "$RUNEGEN_COMMANDS"
printf '"maxclients" is "%s"\n' "$maxclients"
scenario="${RUNEGEN_SCENARIO:-success}"
if [ "$count" -eq 1 ]; then
    if [ "$second" != "sv rune" ]; then
        echo "generation received $second" >&2
        exit 94
    fi
    if [ -e "$artifact" ] || [ -L "$artifact" ] ||
            [ -e "$snag" ] || [ -L "$snag" ]; then
        echo "old pair leaked into generation stage" >&2
        exit 97
    fi
    cp "$RUNEGEN_FIXTURE" "$artifact"
    case "$scenario" in
        bad-roots)
            echo "rune: objective roots red=0 blue=0"
            ;;
        *)
            echo "rune: objective roots red=0 blue=1"
            ;;
    esac
    echo "rune: wrote $game/maps/$mapname.rune (2 seeds, 2 links, 3 mechanism nodes, 2 triggers, 1 inventory edges, 1 activation plans)"
    case "$scenario" in
        generation-readiness)
            echo "slipgate: rune ready $mapname, 2 seeds, 2 links, 3 mechanism nodes, 1 plans, gravity 650, all fields up"
            ;;
        generation-failure)
            echo "rune: revalidation failed kind=test"
            ;;
        generation-nonzero)
            exit 91
            ;;
        linger-generation)
            printf 'ready\n' > "$RUNEGEN_READY"
            sleep 30
            ;;
    esac
    exit 0
fi
if [ "$count" -ne 2 ] || [ "$second" != "sv sg add red" ]; then
    echo "cold load received $second at launch $count" >&2
    exit 94
fi
if [ ! -f "$artifact" ] || [ -L "$artifact" ] ||
        [ ! -f "$snag" ] || [ -L "$snag" ]; then
    echo "cold load did not receive independent pair files" >&2
    exit 98
fi
rune_sha="$(sha256sum "$artifact" | awk '{print $1}')"
snag_sha="$(sha256sum "$snag" | awk '{print $1}')"
declared_rune="$(awk '$1 == "rune_sha256" {print $2}' "$snag")"
evidence_sha="$(awk '$1 == "evidence_sha256" {print $2}' "$snag")"
repairs="$(awk '$1 == "repairs" {print $2}' "$snag")"
if [ "$repairs" != 0 ] || [ "$declared_rune" != "$rune_sha" ]; then
    echo "fake cold loader rejected actual SNAG bytes" >&2
    exit 98
fi
if ! find "$RUNEGEN_LOGS" -maxdepth 1 -name '*.snag-bootstrap-evidence.json' \
        -type f -print -quit | grep -q .; then
    echo "evidence file missing" >&2
    exit 98
fi
evidence_file="$(find "$RUNEGEN_LOGS" -maxdepth 1 \
    -name '*.snag-bootstrap-evidence.json' -type f -print -quit)"
actual_evidence_sha="$(sha256sum "$evidence_file" | awk '{print $1}')"
if [ "$actual_evidence_sha" != "$evidence_sha" ]; then
    echo "fake cold loader rejected evidence binding" >&2
    exit 98
fi
attestation="slipgate: snag ready map=$mapname repairs=0 rune_sha256=$rune_sha evidence_sha256=$evidence_sha snag_sha256=$snag_sha"
ready="slipgate: rune ready $mapname, 2 seeds, 2 links, 3 mechanism nodes, 1 plans, gravity 650, all fields up"
case "$scenario" in
    missing-attestation)
        echo "$ready"
        ;;
    duplicate-attestation)
        echo "$attestation"
        echo "$attestation"
        echo "$ready"
        ;;
    reversed-attestation)
        echo "$ready"
        echo "$attestation"
        ;;
    wrong-attestation)
        echo "${attestation/rune_sha256=$rune_sha/rune_sha256=$(printf '0%.0s' {1..64})}"
        echo "$ready"
        ;;
    cold-write)
        echo "$attestation"
        echo "rune: wrote forbidden"
        echo "$ready"
        ;;
    cold-nonzero)
        echo "$attestation"
        echo "$ready"
        exit 92
        ;;
    mutate-module)
        echo "$attestation"
        echo "$ready"
        printf 'changed\n' >> "$stage/game.so"
        ;;
    *)
        echo "$attestation"
        echo "$ready"
        ;;
esac
exit 0
'''


ACCEPTOR = r'''#!/usr/bin/env python3
import json
import os
import sys

if os.environ.get("RUNEGEN_SCENARIO") == "accept-failure":
    raise SystemExit(1)
print(json.dumps({
    "map_name": "runetest",
    "seed_count": 2,
    "link_count": 2,
    "node_count": 3,
    "trigger_count": 2,
    "inventory_edge_count": 1,
    "plan_edge_count": 1,
    "edge_count": 2,
    "plan_count": 1,
}, sort_keys=True))
'''


class RunegenGateTest(unittest.TestCase):
    def run_scenario(
        self,
        scenario: str,
        *,
        dry_run: bool = False,
        old_snag: bytes | None = b"old-snag",
        maxclients: str = "16",
        signal_run: bool = False,
    ):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            game_root = work / "quake"
            live = game_root / "livegame"
            maps = live / "maps"
            engine_dir = work / "engine"
            logs = work / "logs"
            backups = work / "backups"
            maps.mkdir(parents=True)
            engine_dir.mkdir()
            logs.mkdir()
            (live / "rune.cfg").write_text("set maxclients 7\n", encoding="utf-8")
            (live / "pak1.pak").write_bytes(b"asset")
            (live / "game.so").write_bytes(b"module")
            (live / "gamex86_64.so").write_bytes(b"module")
            old_rune_path = maps / f"{MAP}.rune"
            old_snag_path = maps / f"{MAP}.snag"
            old_rune_path.write_bytes(b"old-rune")
            if old_snag is not None:
                old_snag_path.write_bytes(old_snag)
            fixture = work / "fixture.rune"
            fixture.write_bytes(_build_rune())
            engine = engine_dir / "q2ded"
            engine.write_text(ENGINE, encoding="utf-8")
            engine.chmod(0o755)
            acceptor = work / "acceptor"
            acceptor.write_text(ACCEPTOR, encoding="utf-8")
            acceptor.chmod(0o755)
            build_file = work / "acceptor.mk"
            build_file.write_text("# freshness marker\n", encoding="utf-8")
            launch_count = work / "launch-count"
            commands = work / "commands"
            ready = work / "ready"
            sentinel_root = game_root / ".runegen-stage.unrelated"
            sentinel_engine = engine_dir / ".runegen-stage.unrelated"
            sentinel_root.mkdir()
            sentinel_engine.mkdir()
            (sentinel_root / "keep").write_bytes(b"root")
            (sentinel_engine / "keep").write_bytes(b"engine")
            environment = os.environ.copy()
            environment.update(
                {
                    "Q2DED": str(engine),
                    "GAMEDIR_ROOT": str(game_root),
                    "GAME": "livegame",
                    "CFG": "rune.cfg",
                    "MAXCLIENTS": maxclients,
                    "PORT_START": "58400",
                    "STARTUP_SLEEP": "0",
                    "GEN_BUDGET": "2",
                    "SHUTDOWN_MARGIN": "2",
                    "RUNE_LOG_DIR": str(logs),
                    "RUNE_BACKUP_DIR": str(backups),
                    "RUNE_ACCEPT": str(acceptor),
                    "RUNE_ACCEPT_BUILD_FILE": str(build_file),
                    "RUNE_ACCEPT_BUILD_TARGET": str(acceptor),
                    "RUNEGEN_SCENARIO": scenario,
                    "RUNEGEN_FIXTURE": str(fixture),
                    "RUNEGEN_LAUNCH_COUNT": str(launch_count),
                    "RUNEGEN_COMMANDS": str(commands),
                    "RUNEGEN_LOGS": str(logs),
                    "RUNEGEN_READY": str(ready),
                }
            )
            arguments = [str(RUNNER)]
            if dry_run:
                arguments.append("--dry-run")
            arguments.append(MAP)
            if signal_run:
                process = subprocess.Popen(
                    arguments,
                    cwd=ROOT,
                    env=environment,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    start_new_session=True,
                )
                for _ in range(200):
                    if ready.exists():
                        break
                    time.sleep(0.01)
                self.assertTrue(ready.exists())
                os.killpg(process.pid, signal.SIGTERM)
                stdout, stderr = process.communicate(timeout=5)
                completed = subprocess.CompletedProcess(
                    arguments, process.returncode, stdout, stderr
                )
            else:
                completed = subprocess.run(
                    arguments,
                    cwd=ROOT,
                    env=environment,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=15,
                    check=False,
                )
            remaining_stages = [
                path.name
                for path in game_root.glob(".runegen-stage.*")
                if path != sentinel_root
            ]
            remaining_portable = [
                path.name
                for path in engine_dir.glob(".runegen-stage.*")
                if path != sentinel_engine
            ]
            self.assertEqual([], remaining_stages, completed.stdout + completed.stderr)
            self.assertEqual([], remaining_portable, completed.stdout + completed.stderr)
            self.assertEqual(b"root", (sentinel_root / "keep").read_bytes())
            self.assertEqual(b"engine", (sentinel_engine / "keep").read_bytes())
            return {
                "completed": completed,
                "rune": old_rune_path.read_bytes(),
                "snag": old_snag_path.read_bytes() if old_snag_path.exists() else None,
                "launches": int(launch_count.read_text()) if launch_count.exists() else 0,
                "commands": commands.read_text() if commands.exists() else "",
                "logs": sorted(path.name for path in logs.glob("*.log")),
                "backups": [
                    json.loads(path.read_text(encoding="utf-8"))
                    for path in sorted(backups.glob("*/manifest.json"))
                ],
                "fixture": fixture.read_bytes(),
            }

    def test_success_uses_two_engines_and_installs_fresh_pair(self):
        result = self.run_scenario("success")
        completed = result["completed"]
        self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(2, result["launches"])
        self.assertEqual("launch=1:sv rune\nlaunch=2:sv sg add red\n", result["commands"])
        self.assertEqual(result["fixture"], result["rune"])
        fields = dict(
            line.split(" ", 1)
            for line in result["snag"].decode("ascii").splitlines()
        )
        self.assertEqual("0", fields["repairs"])
        self.assertEqual(
            hashlib.sha256(result["rune"]).hexdigest(), fields["rune_sha256"]
        )
        self.assertEqual(
            1, len([name for name in result["logs"] if name.endswith("-generation.log")])
        )
        self.assertEqual(
            1, len([name for name in result["logs"] if name.endswith("-cold.log")])
        )
        self.assertEqual(1, len(result["backups"]))
        backup = result["backups"][0]
        self.assertTrue(backup["old"]["rune"]["exists"])
        self.assertTrue(backup["old"]["snag"]["exists"])

    def test_generation_failures_never_launch_cold_or_change_live_pair(self):
        for scenario in (
            "generation-readiness",
            "generation-failure",
            "generation-nonzero",
            "bad-roots",
            "accept-failure",
        ):
            with self.subTest(scenario=scenario):
                result = self.run_scenario(scenario)
                self.assertEqual(1, result["completed"].returncode)
                self.assertEqual(1, result["launches"])
                self.assertEqual(b"old-rune", result["rune"])
                self.assertEqual(b"old-snag", result["snag"])

    def test_cold_attestation_failures_preserve_old_pair(self):
        for scenario in (
            "missing-attestation",
            "duplicate-attestation",
            "reversed-attestation",
            "wrong-attestation",
            "cold-write",
            "cold-nonzero",
            "mutate-module",
        ):
            with self.subTest(scenario=scenario):
                result = self.run_scenario(scenario)
                self.assertEqual(1, result["completed"].returncode)
                self.assertEqual(2, result["launches"])
                self.assertEqual(b"old-rune", result["rune"])
                self.assertEqual(b"old-snag", result["snag"])

    def test_failed_run_preserves_absent_old_snag(self):
        result = self.run_scenario("missing-attestation", old_snag=None)
        self.assertEqual(1, result["completed"].returncode)
        self.assertEqual(b"old-rune", result["rune"])
        self.assertIsNone(result["snag"])

    def test_invalid_maxclients_stops_before_launch(self):
        result = self.run_scenario("success", maxclients="0")
        self.assertEqual(2, result["completed"].returncode)
        self.assertEqual(0, result["launches"])
        self.assertEqual(b"old-rune", result["rune"])

    def test_signal_cleans_only_the_active_stage(self):
        result = self.run_scenario("linger-generation", signal_run=True)
        self.assertNotEqual(0, result["completed"].returncode)
        self.assertEqual(b"old-rune", result["rune"])
        self.assertEqual(b"old-snag", result["snag"])

    def test_dry_run_describes_two_engines_and_pair_transaction(self):
        result = self.run_scenario("success", dry_run=True)
        completed = result["completed"]
        self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(0, result["launches"])
        self.assertIn("generation launch sends only: maxclients, sv rune, quit", completed.stdout)
        self.assertIn("cold launch sends only: maxclients, sv sg add red, quit", completed.stdout)
        self.assertIn("omit runetest.rune and runetest.snag", completed.stdout)
        self.assertIn("repairs=0 SNAG", completed.stdout)
        self.assertIn("SNAG-first, RUNE-second", completed.stdout)


if __name__ == "__main__":
    unittest.main()
