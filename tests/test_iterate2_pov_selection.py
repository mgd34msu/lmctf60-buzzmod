"""Behavioral selection checks for iterate2's default-off contract."""
import os
import hashlib
import pathlib
import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]

MULTICALL = r'''
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char*base(const char*p){const char*s=strrchr(p,'/');return s?s+1:p;}
static int write_all(int fd,const char*data,size_t size){size_t done=0;while(done<size){ssize_t wrote=write(fd,data+done,size-done);if(wrote<0&&errno==EINTR)continue;if(wrote<=0)return -1;done+=(size_t)wrote;}return 0;}
static void logv(int ac,char**av){const char*t=getenv("TRACE");char b[8192];size_t u=0;if(!t)return;int f=open(t,O_WRONLY|O_CREAT|O_APPEND,0600);if(f<0)return;u+=(size_t)snprintf(b+u,sizeof(b)-u,"%s",base(av[0]));for(int i=1;i<ac&&u<sizeof(b);i++)u+=(size_t)snprintf(b+u,sizeof(b)-u," [%s]",av[i]);if(u<sizeof(b)-1)b[u++]='\n';if(write_all(f,b,u)<0){close(f);return;}close(f);}
int main(int ac,char**av){const char*n=base(av[0]);logv(ac,av);
 for(int i=1;i<ac;i++)if(!strcmp(av[i],"--q2ded")){int f=open("supervisor.called",O_WRONLY|O_CREAT|O_EXCL,0600);if(f<0)return 93;for(int j=1;j<ac;j++)dprintf(f,"[%s]\n",av[j]);close(f);return 0;}
 if(!strcmp(n,"dirname")){char b[PATH_MAX];snprintf(b,sizeof(b),"%s",av[1]);char*s=strrchr(b,'/');if(!s)puts(".");else{if(s==b)s[1]=0;else*s=0;puts(b);}return 0;}
 if(!strcmp(n,"mkdir")){const char*p=av[ac-1];return mkdir(p,0777)<0&&errno!=EEXIST;}
 if(!strcmp(n,"pgrep")||!strcmp(n,"sleep"))return !strcmp(n,"pgrep");
 if(!strcmp(n,"stdbuf")){execvp(av[3],av+3);return 90;}
 if(!strcmp(n,"timeout")){execv(av[2],av+2);return 91;}
 if(!strcmp(n,"fake-q2")){char b[512];while(read(0,b,sizeof(b))>0){}return 0;}
 if(!strcmp(n,"gamestat.sh"))return strstr(av[1],"s10-")?7:0;
 return 92;}
'''


class IterateSelectionTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.tools = self.root / "tools"
        self.tools.mkdir()
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.game_root = self.root / "games"
        (self.game_root / "testgame").mkdir(parents=True)
        for module in ("game.so", "gamex86_64.so"):
            (self.game_root / "testgame" / module).write_bytes(b"exact-module")
        maps = self.game_root / "testgame" / "maps"
        maps.mkdir()
        shutil.copy2(REPO / "tools/iterate2.sh", self.tools / "iterate2.sh")
        shutil.copy2(REPO / "tools/topmaps.txt", self.tools / "topmaps.txt")
        for line in (self.tools / "topmaps.txt").read_text().splitlines():
            if line and not line.startswith("#"):
                (maps / f"{line}.rune").write_bytes(b"rune")
                (maps / f"{line}.snag").write_bytes(b"snag")
        source = self.root / "multicall.c"
        source.write_text(MULTICALL)
        multicall = self.bin / "multicall"
        subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-Wpedantic", "-o", str(multicall), str(source)], check=True)
        for name in ("dirname", "mkdir", "pgrep", "sleep", "stdbuf", "timeout", "fake-q2"):
            (self.bin / name).symlink_to(multicall)
        (self.tools / "gamestat.sh").symlink_to(multicall)

    def tearDown(self):
        self.tmp.cleanup()

    def run_off(self, value, wave="fixture"):
        trace = self.root / "trace"
        trace.unlink(missing_ok=True)
        env = {"PATH": str(self.bin), "HOME": str(self.root), "TRACE": str(trace),
               "Q2DED": str(self.bin / "fake-q2"), "GAMEDIR_ROOT": str(self.game_root),
               "GAME": "testgame", "CFG": "test.cfg", "PORT_BASE": "28520"}
        if value is not None:
            env["POV_ENABLE"] = value
        result = subprocess.run(["/bin/bash", "--noprofile", "--norc", str(self.tools / "iterate2.sh"), wave],
                                env=env, text=True, capture_output=True, timeout=10)
        return result, trace.read_text().splitlines()

    def test_unset_empty_zero_and_malformed_match_frozen_default_fixture(self):
        frozen = None
        for value in (None, "", "0", "poisoned"):
            with self.subTest(value=value):
                result, trace = self.run_off(value)
                self.assertEqual(result.returncode, 1, result.stderr)
                normalized = sorted(line.replace(str(self.root), "$ROOT") for line in trace)
                if frozen is None:
                    frozen = normalized
                    self.assertEqual(sum(line == "sleep [7]" for line in frozen), 10)
                    q2 = [line for line in frozen if line.startswith("fake-q2 ")]
                    self.assertEqual(len(q2), 10)
                    ports = sorted(int(re.search(r"\[port\] \[(\d+)\]", line).group(1)) for line in q2)
                    self.assertEqual(ports, list(range(28520, 28530)))
                    self.assertEqual(sum(line.startswith("gamestat.sh ") for line in frozen), 10)
                else:
                    self.assertEqual(normalized, frozen)
                self.assertFalse(any(p.name.startswith("pov-") for p in self.tools.rglob("*")))

    def test_exact_opt_in_is_the_only_supervisor_selector(self):
        source = (REPO / "tools/iterate2.sh").read_text()
        self.assertIn("[[ ${POV_ENABLE-} == 1", source)
        self.assertIn('if ((POV_ENABLED)) && [[ $i == "$POV_SLOT" ]]', source)
        self.assertIn('exec "/proc/self/fd/$POV_SUPERVISOR_FD"', source)
        self.assertNotIn("pov-wave.sh", source)
        self.assertNotIn("pov-record.sh", source)

    def test_missing_route_artifact_refuses_to_launch_empty_fleet(self):
        (self.game_root / "testgame" / "maps" / "lmctf09.snag").unlink()
        result, trace = self.run_off(None, "0")
        self.assertEqual(result.returncode, 2)
        self.assertIn("refusing empty fleet", result.stderr)
        self.assertFalse(any(line.startswith("fake-q2 ") for line in trace))

    def test_wave_manifest_binds_full_module_hash_and_lane_schedule(self):
        result, _trace = self.run_off(None, "bound")
        self.assertEqual(result.returncode, 1, result.stderr)
        manifest = self.tools / "iter-bound" / "wave-manifest.tsv"
        rows = manifest.read_text().splitlines()
        digest = hashlib.sha256(b"exact-module").hexdigest()
        self.assertEqual(rows[0], "format\tlmctf-wave-manifest-1")
        self.assertIn(f"module_before_sha256\t{digest}", rows)
        self.assertIn(f"module_after_sha256\t{digest}", rows)
        self.assertEqual(sum(row.startswith("lane\t") for row in rows), 10)

    def test_mismatched_deployed_modules_refuse_to_launch(self):
        module = self.game_root / "testgame" / "gamex86_64.so"
        module.write_bytes(b"different-module")
        result, trace = self.run_off(None, "mismatch")
        self.assertEqual(result.returncode, 2)
        self.assertIn("module hashes disagree", result.stderr)
        self.assertFalse(any(line.startswith("fake-q2 ") for line in trace))

    def test_human_demo_top20_rotates_on_distinct_server_offsets(self):
        pool = [line for line in (self.tools / "topmaps.txt").read_text().splitlines()
                if line and not line.startswith("#")]
        self.assertEqual(len(pool), 20)

        schedules = []
        for wave in range(20):
            result, trace = self.run_off(None, str(wave))
            self.assertEqual(result.returncode, 1, result.stderr)
            by_slot = {}
            for line in trace:
                if not line.startswith("fake-q2 "):
                    continue
                port = int(re.search(r"\[port\] \[(\d+)\]", line).group(1))
                mapname = re.search(r"\[\+map\] \[([^]]+)\]", line).group(1)
                by_slot[port - 28520] = mapname
            self.assertEqual(sorted(by_slot), list(range(10)))
            schedules.append([by_slot[slot] for slot in range(10)])

        self.assertEqual(set(schedules[0]) | set(schedules[1]), set(pool))
        self.assertTrue(set(schedules[0]).isdisjoint(schedules[1]))
        for slot in range(10):
            expected = pool[slot * 2:] + pool[:slot * 2]
            self.assertEqual([schedule[slot] for schedule in schedules], expected)

        s08_cfg = (self.game_root / "testgame" / "waveflags-s8.cfg").read_text()
        self.assertIn('set sv_botfill "7"', s08_cfg)

    def test_exact_opt_in_selects_configured_lane_in_a_native_fast_fixture(self):
        script = self.tools / "iterate2.sh"
        source = script.read_text()
        source = source.replace('        /usr/bin/sleep "$1"', '        : "$1"')
        source = source.replace('        /usr/bin/sleep 7', '        :')
        source = source.replace('/usr/bin/pgrep -x q2ded', '/bin/false')
        script.write_text(source)
        supervisor = self.tools / "pov-supervisor"
        supervisor.symlink_to(self.bin / "multicall")
        gamestat = self.tools / "gamestat.sh"
        gamestat.unlink()
        gamestat.write_text("#!/bin/bash\nexit 0\n")
        gamestat.chmod(0o700)
        env = {"PATH": str(self.bin), "HOME": str(self.root),
               "Q2DED": str(self.bin / "fake-q2"), "YAMAGI_CLIENT": str(self.bin / "fake-q2"),
               "GAMEDIR_ROOT": str(self.game_root), "GAME": "testgame", "CFG": "test.cfg",
               "PORT_BASE": "28520", "POV_ENABLE": "1", "POV_FINALIZE_DELAY": "1",
               "POV_LANE": "5", "POV_TARGET": "[SG]Caco"}
        result = subprocess.run(["/bin/bash", "--noprofile", "--norc", str(script), "enabled"],
                                cwd=self.root, env=env, text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        call = (self.root / "supervisor.called").read_text()
        self.assertIn("[--server]\n[s05]\n", call)
        self.assertIn("[--port]\n[28524]\n", call)
        self.assertIn("[--spectator]\n[pov_s05]\n", call)
        self.assertIn("[--target]\n[[SG]Caco]\n", call)
        logs = list((self.tools / "iter-enabled").glob("*.log"))
        self.assertEqual(len(logs), 10)

        supervisor.unlink()
        supervisor.symlink_to("/bin/false")
        failed = subprocess.run(
            ["/bin/bash", "--noprofile", "--norc", str(script), "failed"],
            cwd=self.root, env=env, text=True, capture_output=True, timeout=10,
        )
        self.assertNotEqual(failed.returncode, 0)

    def test_changed_authority_files_use_only_current_observation_terms(self):
        forbidden = re.compile("|".join(("leg" + "acy", r"\bV[1-4]\b",
                                         "active" + r"[- ]RUNE",
                                         "forensic" + r"[- ]preservation")), re.IGNORECASE)
        paths = [REPO / "tools/iterate2.sh", REPO / "tools/pov-supervisor.c",
                 REPO / "tools/pov-spawn-linux.c", REPO / "tools/pov-spawn-linux.h",
                 REPO / "tests/pov_supervisor_unit.c", REPO / "tests/test_pov_supervisor.py",
                 pathlib.Path(__file__)]
        for path in paths:
            with self.subTest(path=path.name):
                self.assertIsNone(forbidden.search(path.read_text()))


if __name__ == "__main__":
    unittest.main()
