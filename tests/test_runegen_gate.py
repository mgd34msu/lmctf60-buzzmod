#!/usr/bin/env python3
"""Exercise the post-write runtime-acceptance gate in runegen."""

from __future__ import annotations

import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools/runegen.sh"
MAP = "gatecase"


class RunegenRuneGateTest(unittest.TestCase):
    def run_scenario(
        self, scenario: str, *, dry_run: bool = False, maxclients: str | None = None
    ):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            game_root = work / "quake"
            live_maps = game_root / "livegame/maps"
            fake_bin = work / "bin"
            engine_dir = work / "engine"
            logs = work / "logs"
            live_maps.mkdir(parents=True)
            fake_bin.mkdir()
            engine_dir.mkdir()
            (game_root / "livegame/pak1.pak").write_bytes(b"asset")
            # Deliberately conflict with both the default and explicit test
            # values. Quake's `exec` inserts this cfg before later command-line
            # commands, so only an authoritative post-exec/pre-map +set wins.
            (game_root / "livegame/rune.cfg").write_text(
                "set maxclients 7\n", encoding="utf-8"
            )
            deployed = live_maps / f"{MAP}.rune"
            deployed.write_bytes(b"deployed-old")
            stage_sentinel = game_root / ".runegen-stage.unrelated"
            portable_sentinel = engine_dir / ".runegen-stage.unrelated"
            stage_sentinel.mkdir()
            portable_sentinel.mkdir()
            (stage_sentinel / "keep").write_bytes(b"root sentinel")
            (portable_sentinel / "keep").write_bytes(b"portable sentinel")

            engine_real = engine_dir / "q2ded"
            engine_real.write_text(
                """#!/usr/bin/env bash
set -u
game=""
mapname=""
maxclients=""
maxclients_before_exec=0
maxclients_after_exec=0
exec_seen=0
map_seen=0
while [ "$#" -gt 0 ]; do
    if [ "$1" = "+set" ] && [ "${2:-}" = "game" ]; then
        game="${3:-}"
        shift 3
    elif [ "$1" = "+set" ] && [ "${2:-}" = "maxclients" ]; then
        if [ "$map_seen" -ne 0 ]; then
            echo "maxclients appeared after map" >&2
            exit 95
        fi
        maxclients="${3:-}"
        if [ "$exec_seen" -eq 0 ]; then
            maxclients_before_exec=1
        else
            maxclients_after_exec=1
        fi
        shift 3
    elif [ "$1" = "+exec" ]; then
        if [ "$maxclients_before_exec" -ne 1 ]; then
            echo "exec appeared before maxclients" >&2
            exit 95
        fi
        cfg="$PWD/$game/${2:-}"
        if [ -f "$cfg" ]; then
            cfg_maxclients="$(awk '
                ($1 == "set" || $1 == "seta") && $2 == "maxclients" {
                    value = $3
                }
                END { print value }
            ' "$cfg")"
            if [ -n "$cfg_maxclients" ]; then
                maxclients="$cfg_maxclients"
            fi
        fi
        exec_seen=1
        shift 2
    elif [ "$1" = "+map" ]; then
        if [ "$exec_seen" -ne 1 ] || [ "$maxclients_after_exec" -ne 1 ]; then
            echo "map appeared before authoritative post-exec maxclients" >&2
            exit 95
        fi
        map_seen=1
        mapname="${2:-}"
        shift 2
    else
        shift
    fi
done
if [ "$map_seen" -ne 1 ]; then
    echo "map command missing" >&2
    exit 95
fi
if [ "$maxclients" != "${RUNEGEN_EXPECT_MAXCLIENTS:-16}" ]; then
    echo "unexpected maxclients: $maxclients" >&2
    exit 96
fi
IFS= read -r query_command || exit 94
if [ "$query_command" != "maxclients" ]; then
    echo "expected maxclients query, got: $query_command" >&2
    exit 94
fi
IFS= read -r rune_command || exit 94
if [ "$rune_command" != "sv rune" ]; then
    echo "expected sv rune after maxclients query, got: $rune_command" >&2
    exit 94
fi
case "${RUNEGEN_FAKE_SCENARIO:-match}" in
    missing-maxclients-report)
        ;;
    wrong-maxclients-report)
        printf '"maxclients" is "8"\n'
        ;;
    *)
        printf '"maxclients" is "%s"\n' "$maxclients"
        ;;
esac
artifact="$PWD/$game/maps/$mapname.rune"
resolved_engine="$(readlink -f -- "$0")"
portable_root="$(cd "$(dirname "$resolved_engine")" && pwd -P)/$game"
mkdir -p "$portable_root/save" "$portable_root/scrnshot"
printf 'portable qconsole\\n' > "$portable_root/qconsole.log"
printf 'fresh-artifact\\n' > "$artifact"
if [ "${RUNEGEN_FAKE_SCENARIO:-match}" = "linger" ]; then
    printf 'ready\\n' > "$RUNEGEN_READY_FILE"
    sleep 30
    exit 0
fi
if [ "${RUNEGEN_FAKE_SCENARIO:-match}" = "prewrite-only" ]; then
    echo "slipgate: rune ready $mapname, 7 seeds, 9 links, 4 mechanism nodes, 5 plans, gravity 800, all fields up"
fi
case "${RUNEGEN_FAKE_SCENARIO:-match}" in
    equal-roots)
        echo "rune: objective roots red=1 blue=1"
        ;;
    out-of-range-roots)
        echo "rune: objective roots red=1 blue=7"
        ;;
    duplicate-roots)
        echo "rune: objective roots red=1 blue=2"
        echo "rune: objective roots red=1 blue=2"
        ;;
    postwrite-roots)
        ;;
    *)
        echo "rune: objective roots red=1 blue=2"
        ;;
esac
if [ "${RUNEGEN_FAKE_SCENARIO:-match}" = "zero-mechanisms" ]; then
    echo "rune: wrote $game/maps/$mapname.rune (7 seeds, 9 links, 0 mechanism nodes, 0 triggers, 0 inventory edges, 0 activation plans)"
else
    echo "rune: wrote $game/maps/$mapname.rune (7 seeds, 9 links, 4 mechanism nodes, 2 triggers, 3 inventory edges, 5 activation plans)"
fi
if [ "${RUNEGEN_FAKE_SCENARIO:-match}" = "postwrite-roots" ]; then
    echo "rune: objective roots red=1 blue=2"
fi
if [ "${RUNEGEN_FAKE_SCENARIO:-match}" = "duplicate-write" ]; then
    echo "rune: wrote $game/maps/$mapname.rune (7 seeds, 9 links, 4 mechanism nodes, 2 triggers, 3 inventory edges, 5 activation plans)"
fi
case "${RUNEGEN_FAKE_SCENARIO:-match}" in
    match|retarget-engine|acceptor-stales-during-server|duplicate-write|equal-roots|out-of-range-roots|duplicate-roots|postwrite-roots|artifact-count-mismatch|decoder-mismatch|accept-failed|inspect-failed|lint-failed|missing-maxclients-report|wrong-maxclients-report)
        echo "slipgate: rune ready $mapname, 7 seeds, 9 links, 4 mechanism nodes, 5 plans, gravity 800, all fields up"
        ;;
    mismatch)
        echo "slipgate: rune ready $mapname, 7 seeds, 9 links, 6 mechanism nodes, 5 plans, gravity 800, all fields up"
        ;;
    rejected)
        echo "rune: rejected $game/maps/$mapname.rune stage=mechanism-rebind reason=test"
        echo "slipgate: rune ready $mapname, 7 seeds, 9 links, 4 mechanism nodes, 5 plans, gravity 800, all fields up"
        ;;
    failed)
        echo "rune: FAILED: cleanup door restore reason=test; graph was not written"
        echo "slipgate: rune ready $mapname, 7 seeds, 9 links, 4 mechanism nodes, 5 plans, gravity 800, all fields up"
        ;;
    generation-refused)
        echo "rune: generation refused stage=test reason=test"
        echo "slipgate: rune ready $mapname, 7 seeds, 9 links, 4 mechanism nodes, 5 plans, gravity 800, all fields up"
        ;;
    revalidation-failed)
        echo "rune: revalidation failed kind=proof-law"
        echo "slipgate: rune ready $mapname, 7 seeds, 9 links, 4 mechanism nodes, 5 plans, gravity 800, all fields up"
        ;;
    install-failed)
        echo "rune: install failed status=1 reason=test"
        echo "slipgate: rune ready $mapname, 7 seeds, 9 links, 4 mechanism nodes, 5 plans, gravity 800, all fields up"
        ;;
    cleanup-unwritten)
        echo "rune: cleanup restored pending door scope; graph remains unwritten"
        echo "slipgate: rune ready $mapname, 7 seeds, 9 links, 4 mechanism nodes, 5 plans, gravity 800, all fields up"
        ;;
    zero-mechanisms)
        echo "slipgate: rune ready $mapname, 7 seeds, 9 links, 0 mechanism nodes, 0 plans, gravity 800, all fields up"
        ;;
    crash)
        echo "slipgate: rune ready $mapname, 7 seeds, 9 links, 4 mechanism nodes, 5 plans, gravity 800, all fields up"
        exit 91
        ;;
    timeout)
        echo "slipgate: rune ready $mapname, 7 seeds, 9 links, 4 mechanism nodes, 5 plans, gravity 800, all fields up"
        exit 124
        ;;
    missing|prewrite-only)
        ;;
    *)
        exit 91
        ;;
esac
if [ "${RUNEGEN_FAKE_SCENARIO:-match}" = "acceptor-stales-during-server" ]; then
    touch -d '2038-01-01 00:00:00 UTC' "$RUNEGEN_ACCEPTOR_SOURCE"
fi
""",
                encoding="utf-8",
            )
            engine_real.chmod(0o755)
            engine = fake_bin / "q2ded"
            engine.symlink_to(engine_real)
            engine_other = engine_dir / "q2ded-other"
            engine_other.write_text(
                "#!/usr/bin/env bash\n"
                "printf 'retargeted engine launched\\n' > \"$RUNEGEN_OTHER_MARKER\"\n"
                "exit 93\n",
                encoding="utf-8",
            )
            engine_other.chmod(0o755)

            ln_wrapper = fake_bin / "ln"
            ln_wrapper.write_text(
                "#!/usr/bin/env bash\n"
                "if [ \"${RUNEGEN_FAKE_SCENARIO:-match}\" = retarget-engine ] && "
                "[ ! -e \"$RUNEGEN_RETARGET_MARKER\" ]; then\n"
                "    : > \"$RUNEGEN_RETARGET_MARKER\"\n"
                "    /usr/bin/ln -sfn -- \"$RUNEGEN_OTHER_ENGINE\" "
                "\"$RUNEGEN_ENGINE_LINK\"\n"
                "fi\n"
                "exec /usr/bin/ln \"$@\"\n",
                encoding="utf-8",
            )
            ln_wrapper.chmod(0o755)

            acceptor_source = work / "acceptor-source"
            acceptor_source.write_text("frozen source\n", encoding="utf-8")
            acceptor = fake_bin / "runeaccept"
            acceptor.write_text(
                """#!/usr/bin/env bash
if [ "$#" -ne 1 ] || [[ "${1:-}" != *.rune ]]; then
    echo "runeaccept: expected one artifact" >&2
    exit 97
fi
if [ "${RUNEGEN_FAKE_SCENARIO:-match}" = "accept-failed" ]; then
    echo "runeaccept: rejected test artifact" >&2
    exit 23
fi
if [ "${RUNEGEN_FAKE_SCENARIO:-match}" = "zero-mechanisms" ]; then
    triggers=0
    inventory=0
    nodes=0
    plans=0
    plan_edges=0
    edges=0
elif [ "${RUNEGEN_FAKE_SCENARIO:-match}" = "artifact-count-mismatch" ]; then
    triggers=99
    inventory=98
    nodes=4
    plans=5
    plan_edges=5
    edges=8
else
    triggers=2
    inventory=3
    nodes=4
    plans=5
    plan_edges=5
    edges=8
fi
printf '{"edge_count":%s,"inventory_edge_count":%s,"link_count":9,"map_name":"gatecase","node_count":%s,"plan_count":%s,"plan_edge_count":%s,"seed_count":7,"trigger_count":%s}\\n' "$edges" "$inventory" "$nodes" "$plans" "$plan_edges" "$triggers"
""",
                encoding="utf-8",
            )
            acceptor.chmod(0o755)
            acceptor_makefile = work / "Acceptor.mk"
            acceptor_makefile.write_text(
                f"{acceptor}: {acceptor_source}\n\t@:\n",
                encoding="utf-8",
            )
            if scenario == "stale-acceptor":
                newer = acceptor.stat().st_mtime_ns + 1_000_000_000
                os.utime(acceptor_source, ns=(newer, newer))

            python_wrapper = fake_bin / "python3"
            python_wrapper.write_text(
                "#!/usr/bin/env bash\n"
                "case \"${1:-}\" in\n"
                "    */runelint.py)\n"
                "        if [ \"$#\" -ne 5 ] || [ \"${2:-}\" != \"--objective-roots\" ] || "
                "[ \"${3:-}\" != \"1\" ] || [ \"${4:-}\" != \"2\" ] || "
                "[[ \"${5:-}\" != *.rune ]]; then exit 98; fi\n"
                "        if [ \"${RUNEGEN_FAKE_SCENARIO:-match}\" = \"lint-failed\" ]; then exit 25; fi\n"
                "        exit 0\n"
                "        ;;\n"
                "    */runeio.py)\n"
                "        if [ \"$#\" -ne 2 ] || [[ \"${2:-}\" != *.rune ]]; then exit 97; fi\n"
                "        if [ \"${RUNEGEN_FAKE_SCENARIO:-match}\" = \"inspect-failed\" ]; then exit 24; fi\n"
                "        if [ \"${RUNEGEN_FAKE_SCENARIO:-match}\" = \"zero-mechanisms\" ]; then\n"
                "            triggers=0; inventory=0; nodes=0; plans=0; plan_edges=0; edges=0\n"
                "        elif [ \"${RUNEGEN_FAKE_SCENARIO:-match}\" = \"artifact-count-mismatch\" ]; then\n"
                "            triggers=99; inventory=98; nodes=4; plans=5; plan_edges=5; edges=8\n"
                "        elif [ \"${RUNEGEN_FAKE_SCENARIO:-match}\" = \"decoder-mismatch\" ]; then\n"
                "            triggers=2; inventory=3; nodes=4; plans=5; plan_edges=6; edges=8\n"
                "        else\n"
                "            triggers=2; inventory=3; nodes=4; plans=5; plan_edges=5; edges=8\n"
                "        fi\n"
                "        printf '{\"edge_count\":%s,\"inventory_edge_count\":%s,\"link_count\":9,\"map_name\":\"gatecase\",\"node_count\":%s,\"plan_count\":%s,\"plan_edge_count\":%s,\"seed_count\":7,\"trigger_count\":%s}\\n' \"$edges\" \"$inventory\" \"$nodes\" \"$plans\" \"$plan_edges\" \"$triggers\"\n"
                "        exit 0\n"
                "        ;;\n"
                "esac\n"
                f"exec {shlex.quote(sys.executable)} \"$@\"\n",
                encoding="utf-8",
            )
            python_wrapper.chmod(0o755)

            environment = os.environ.copy()
            environment.update(
                {
                    "PATH": f"{fake_bin}:{environment['PATH']}",
                    "Q2DED": str(engine),
                    "GAMEDIR_ROOT": str(game_root),
                    "GAME": "livegame",
                    "RUNE_LOG_DIR": str(logs),
                    "RUNE_BACKUP_DIR": str(logs / "backups"),
                    "RUNE_ACCEPT": str(acceptor),
                    "RUNE_ACCEPT_BUILD_FILE": str(acceptor_makefile),
                    "RUNE_ACCEPT_BUILD_TARGET": str(acceptor),
                    "RUNEGEN_ACCEPTOR_SOURCE": str(acceptor_source),
                    "RUNEGEN_FAKE_SCENARIO": scenario,
                    "RUNEGEN_READY_FILE": str(work / "ready"),
                    "RUNEGEN_ENGINE_LINK": str(engine),
                    "RUNEGEN_OTHER_ENGINE": str(engine_other),
                    "RUNEGEN_OTHER_MARKER": str(work / "other-engine-ran"),
                    "RUNEGEN_RETARGET_MARKER": str(work / "engine-retargeted"),
                    # The runner can be called from `make -B`; its nested
                    # freshness query must not inherit the forcing flags.
                "MAKEFLAGS": "-B",
                "MFLAGS": "-B",
                "GNUMAKEFLAGS": "-B",
                    "PORT_START": "58400",
                    "STARTUP_SLEEP": "0",
                    "GEN_BUDGET": "0",
                    "SHUTDOWN_MARGIN": "5",
                }
            )
            environment.pop("MAXCLIENTS", None)
            environment.pop("RUNEGEN_EXPECT_MAXCLIENTS", None)
            if maxclients is not None:
                environment["MAXCLIENTS"] = maxclients
                environment["RUNEGEN_EXPECT_MAXCLIENTS"] = maxclients
            arguments = [str(RUNNER)]
            if dry_run:
                arguments.append("--dry-run")
            arguments.append(MAP)
            if scenario == "linger":
                process = subprocess.Popen(
                    arguments,
                    cwd=ROOT,
                    env=environment,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    start_new_session=True,
                )
                for _ in range(100):
                    if (work / "ready").exists():
                        break
                    time.sleep(0.02)
                self.assertTrue((work / "ready").exists())
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
            self.assertEqual(b"root sentinel", (stage_sentinel / "keep").read_bytes())
            self.assertEqual(
                b"portable sentinel", (portable_sentinel / "keep").read_bytes()
            )
            self.assertEqual(
                [],
                sorted(
                    path.name
                    for path in engine_dir.glob(".runegen-stage.*")
                    if path != portable_sentinel
                ),
                completed.stdout + completed.stderr,
            )
            return (
                completed,
                deployed.read_bytes(),
                sorted(
                    path.name
                    for path in game_root.glob(".runegen-stage.*")
                    if path != stage_sentinel
                ),
                sorted(path.name for path in logs.glob("*.log")),
            )

    def test_matching_post_write_banner_installs(self):
        completed, deployed, stages, logs = self.run_scenario("match")
        self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(b"fresh-artifact\n", deployed)
        self.assertEqual([], stages)
        self.assertTrue(logs)
        self.assertIn("rune: installed", completed.stdout)

    def test_maxclients_is_explicit_and_overrideable(self):
        completed, deployed, stages, logs = self.run_scenario(
            "match", maxclients="30"
        )
        self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(b"fresh-artifact\n", deployed)
        self.assertEqual([], stages)
        self.assertTrue(logs)

    def test_runtime_must_report_the_authoritative_maxclients(self):
        for scenario in ("missing-maxclients-report", "wrong-maxclients-report"):
            with self.subTest(scenario=scenario):
                completed, deployed, stages, logs = self.run_scenario(scenario)
                self.assertEqual(
                    1, completed.returncode, completed.stdout + completed.stderr
                )
                self.assertEqual(b"deployed-old", deployed)
                self.assertEqual([], stages)
                self.assertTrue(logs)
                self.assertIn(
                    "running server did not confirm authoritative maxclients=16",
                    completed.stdout,
                )

    def test_invalid_maxclients_is_rejected_before_server_launch(self):
        for value in ("0", "257", "not-a-number"):
            with self.subTest(value=value):
                completed, deployed, stages, logs = self.run_scenario(
                    "match", maxclients=value
                )
                self.assertEqual(2, completed.returncode)
                self.assertEqual(b"deployed-old", deployed)
                self.assertEqual([], stages)
                self.assertEqual([], logs)
                self.assertIn("MAXCLIENTS must be", completed.stderr)

    def test_launch_is_bound_to_resolved_engine_if_symlink_retargets(self):
        completed, deployed, stages, logs = self.run_scenario("retarget-engine")
        self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(b"fresh-artifact\n", deployed)
        self.assertEqual([], stages)
        self.assertTrue(logs)

    def test_signal_cleans_both_exact_stage_directories(self):
        completed, deployed, stages, logs = self.run_scenario("linger")
        self.assertNotEqual(0, completed.returncode)
        self.assertEqual(b"deployed-old", deployed)
        self.assertEqual([], stages)
        self.assertTrue(logs)

    def test_stale_c_acceptor_is_rejected_before_server_launch(self):
        completed, deployed, stages, logs = self.run_scenario("stale-acceptor")
        self.assertEqual(1, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(b"deployed-old", deployed)
        self.assertEqual([], stages)
        self.assertEqual([], logs)
        self.assertIn("C artifact acceptor is stale", completed.stderr)

    def test_acceptor_that_becomes_stale_during_generation_is_rejected(self):
        completed, deployed, stages, logs = self.run_scenario(
            "acceptor-stales-during-server"
        )
        self.assertEqual(1, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(b"deployed-old", deployed)
        self.assertEqual([], stages)
        self.assertTrue(logs)
        self.assertIn("C artifact acceptor became stale", completed.stdout)
        self.assertIn("C artifact acceptor is stale", completed.stderr)

    def test_structurally_valid_zero_mechanism_artifact_is_installed(self):
        completed, deployed, stages, logs = self.run_scenario("zero-mechanisms")
        self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(b"fresh-artifact\n", deployed)
        self.assertEqual([], stages)
        self.assertTrue(logs)
        self.assertIn("rune: installed", completed.stdout)

    def test_prewrite_banner_does_not_authorize_install(self):
        completed, deployed, stages, logs = self.run_scenario("prewrite-only")
        self.assertEqual(1, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(b"deployed-old", deployed)
        self.assertEqual([], stages)
        self.assertTrue(logs)
        self.assertIn("acceptance banner missing or malformed after write", completed.stdout)

    def test_post_write_count_mismatch_does_not_authorize_install(self):
        completed, deployed, stages, logs = self.run_scenario("mismatch")
        self.assertEqual(1, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(b"deployed-old", deployed)
        self.assertEqual([], stages)
        self.assertTrue(logs)
        self.assertIn("acceptance counts disagree with write", completed.stdout)

    def test_post_write_rejection_overrides_matching_banner(self):
        completed, deployed, stages, logs = self.run_scenario("rejected")
        self.assertEqual(1, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(b"deployed-old", deployed)
        self.assertEqual([], stages)
        self.assertTrue(logs)
        self.assertIn("runtime rejected freshly written artifact", completed.stdout)

    def test_any_post_write_generator_failure_overrides_matching_banner(self):
        for scenario in (
            "failed",
            "generation-refused",
            "revalidation-failed",
            "install-failed",
            "cleanup-unwritten",
        ):
            with self.subTest(scenario=scenario):
                completed, deployed, stages, logs = self.run_scenario(scenario)
                self.assertEqual(
                    1, completed.returncode, completed.stdout + completed.stderr
                )
                self.assertEqual(b"deployed-old", deployed)
                self.assertEqual([], stages)
                self.assertTrue(logs)
                self.assertIn(
                    "generator/runtime failure occurred after write",
                    completed.stdout,
                )

    def test_duplicate_write_banners_do_not_authorize_install(self):
        completed, deployed, stages, logs = self.run_scenario("duplicate-write")
        self.assertEqual(1, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(b"deployed-old", deployed)
        self.assertEqual([], stages)
        self.assertTrue(logs)
        self.assertIn("expected exactly one write banner", completed.stdout)

    def test_objective_roots_must_be_distinct_and_in_range(self):
        for scenario in ("equal-roots", "out-of-range-roots"):
            with self.subTest(scenario=scenario):
                completed, deployed, stages, logs = self.run_scenario(scenario)
                self.assertEqual(
                    1, completed.returncode, completed.stdout + completed.stderr
                )
                self.assertEqual(b"deployed-old", deployed)
                self.assertEqual([], stages)
                self.assertTrue(logs)
                self.assertIn(
                    "objective roots must be distinct seed indexes",
                    completed.stdout,
                )

    def test_objective_root_record_must_be_unique_and_precede_write(self):
        expectations = (
            ("duplicate-roots", "exactly one authoritative objective-root line"),
            ("postwrite-roots", "objective-root line must precede the write banner"),
        )
        for scenario, expected in expectations:
            with self.subTest(scenario=scenario):
                completed, deployed, stages, logs = self.run_scenario(scenario)
                self.assertEqual(
                    1, completed.returncode, completed.stdout + completed.stderr
                )
                self.assertEqual(b"deployed-old", deployed)
                self.assertEqual([], stages)
                self.assertTrue(logs)
                self.assertIn(expected, completed.stdout)

    def test_all_six_write_counts_must_match_artifact_bytes(self):
        completed, deployed, stages, logs = self.run_scenario(
            "artifact-count-mismatch"
        )
        self.assertEqual(1, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(b"deployed-old", deployed)
        self.assertEqual([], stages)
        self.assertTrue(logs)
        self.assertIn("C/Python/write artifact counts disagree", completed.stdout)
        self.assertIn("artifact/write trigger_count mismatch", completed.stdout)

    def test_c_and_python_reports_must_agree(self):
        completed, deployed, stages, logs = self.run_scenario("decoder-mismatch")
        self.assertEqual(1, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(b"deployed-old", deployed)
        self.assertEqual([], stages)
        self.assertTrue(logs)
        self.assertIn("C/Python/write artifact counts disagree", completed.stdout)
        self.assertIn("C/Python plan_edge_count mismatch", completed.stdout)

    def test_each_artifact_gate_must_succeed_before_install(self):
        expectations = (
            ("accept-failed", "C artifact acceptance failed"),
            ("inspect-failed", "Python artifact inspection failed"),
            ("lint-failed", "quality gate failed"),
        )
        for scenario, expected in expectations:
            with self.subTest(scenario=scenario):
                completed, deployed, stages, logs = self.run_scenario(scenario)
                self.assertEqual(
                    1, completed.returncode, completed.stdout + completed.stderr
                )
                self.assertEqual(b"deployed-old", deployed)
                self.assertEqual([], stages)
                self.assertTrue(logs)
                self.assertIn(expected, completed.stdout)

    def test_nonzero_server_exit_overrides_matching_banner(self):
        for scenario, status in (("crash", 91), ("timeout", 124)):
            with self.subTest(scenario=scenario):
                completed, deployed, stages, logs = self.run_scenario(scenario)
                self.assertEqual(
                    1, completed.returncode, completed.stdout + completed.stderr
                )
                self.assertEqual(b"deployed-old", deployed)
                self.assertEqual([], stages)
                self.assertTrue(logs)
                self.assertIn(
                    f"server process exited nonzero status={status}",
                    completed.stdout,
                )

    def test_dry_run_describes_the_fail_closed_runtime_gate(self):
        completed, deployed, stages, _ = self.run_scenario("missing", dry_run=True)
        self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertEqual(b"deployed-old", deployed)
        self.assertEqual([], stages)
        self.assertIn("maxclients=16", completed.stdout)
        self.assertIn("+set maxclients 16", completed.stdout)
        self.assertIn(
            "require the running server to report exact authoritative "
            "maxclients=16",
            completed.stdout,
        )
        self.assertIn("require exactly one write banner", completed.stdout)
        self.assertIn("distinct in-range roots", completed.stdout)
        self.assertIn("clean server exit", completed.stdout)
        self.assertIn(
            "acceptor's explicit build target to be current before generation "
            "and again immediately before artifact decoding",
            completed.stdout,
        )
        self.assertIn("require a later exact 'slipgate: rune ready gatecase, ...'", completed.stdout)
        self.assertIn("matching seed/link/node/plan counts", completed.stdout)
        self.assertIn(
            "production C and Python structural acceptance, including exact "
            "action/plan binding",
            completed.stdout,
        )
        self.assertIn("compare C/Python artifact reports with all six", completed.stdout)

    def test_script_pins_line_order_and_all_six_artifact_counts(self):
        source = RUNNER.read_text(encoding="utf-8")
        self.assertEqual(3, source.count("check_rune_accept_freshness"))
        self.assertIn('"$RUNE_ACCEPT" "$staged_rune"', source)
        self.assertIn('python3 "$RUNE_IO" "$staged_rune"', source)
        self.assertGreaterEqual(source.count('+set maxclients "$MAXCLIENTS"'), 2)
        authoritative = (
            '+exec "$CFG" +set maxclients "$MAXCLIENTS" \\\n'
            '              +map "$map"'
        )
        self.assertIn(authoritative, source)
        self.assertIn('echo "maxclients"; echo "sv rune"', source)
        self.assertIn("running server did not confirm authoritative maxclients", source)
        self.assertIn(
            'python3 "$RUNE_LINT" --objective-roots "$red_root" "$blue_root"',
            source,
        )
        self.assertNotIn('--require-mechanisms "$staged_rune"', source)
        self.assertIn("NR > after && index($0, prefix) == 1", source)
        for pair in (
            '"$runtime_seeds" != "$seeds"',
            '"$runtime_links" != "$links"',
            '"$runtime_nodes" != "$mechanism_nodes"',
            '"$runtime_plans" != "$activation_plans"',
        ):
            self.assertIn(pair, source)
        for field in (
            "seed_count",
            "link_count",
            "node_count",
            "trigger_count",
            "inventory_edge_count",
            "plan_count",
        ):
            self.assertIn(f'"{field}"', source)


if __name__ == "__main__":
    unittest.main()
