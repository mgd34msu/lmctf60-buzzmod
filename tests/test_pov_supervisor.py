"""Native-only process tests for the Linux POV supervisor."""
import hashlib
import os
import pathlib
import signal
import shutil
import subprocess
import tempfile
import textwrap
import time
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]

FAKE_SOURCE = r'''
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef NO_FLUSH
#define FLUSH() ((void)0)
#else
#define FLUSH() fflush(stdout)
#endif
static void ms(long n){struct timespec t={n/1000,(n%1000)*1000000};while(nanosleep(&t,&t)<0&&errno==EINTR){}}
static const char *base(const char *p){const char *s=strrchr(p,'/');return s?s+1:p;}
static int inventory(void){DIR*d=opendir("/proc/self/fd");struct dirent*e;int bad=0;if(!d)return 2;while((e=readdir(d))){char*x;long n=strtol(e->d_name,&x,10);if(*e->d_name&&!*x&&n>2&&n!=dirfd(d))bad=1;}closedir(d);puts(bad?"fd-inventory=bad":"fd-inventory=0,1,2");FLUSH();return bad;}
static const char *arg_after(int ac,char**av,const char*w){for(int i=1;i+1<ac;i++)if(!strcmp(av[i],w))return av[i+1];return NULL;}
#if !defined(ZERO_DEMO) || defined(REPLACE_CONFIG)
static int write_all(int fd,const char*data,size_t size){size_t done=0;while(done<size){ssize_t wrote=write(fd,data+done,size-done);if(wrote<0&&errno==EINTR)continue;if(wrote<=0)return -1;done+=(size_t)wrote;}return 0;}
#endif
#ifdef REPLACE_CONFIG
static void replace_config(int ac,char**av){const char*game=arg_after(ac,av,"game");const char*cfg=arg_after(ac,av,"+exec");char path[8192],backup[8192];int fd;if(!game)game="testgame";if(!cfg)cfg="waveflags-s3.cfg";if(snprintf(path,sizeof(path),"%s/%s",game,cfg)>=(int)sizeof(path)||snprintf(backup,sizeof(backup),"%s.attacked",path)>=(int)sizeof(backup))return;(void)rename(path,backup);fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);if(fd>=0){const char*evil="set sv_botfill 999\\n";if(write_all(fd,evil,strlen(evil))<0){close(fd);return;}close(fd);}}
static void emit_replacement(int ac,char**av){const char*cfg=arg_after(ac,av,"+exec");const char*game=arg_after(ac,av,"game");char path[8192],line[256];FILE*f;if(!cfg||!game||snprintf(path,sizeof(path),"%s/%s",game,cfg)>=(int)sizeof(path))return;f=fopen(path,"r");if(!f)return;while(fgets(line,sizeof(line),f)){printf("command=%s",line);FLUSH();}fclose(f);}
#endif
int main(int ac,char**av){
  if(!strcmp(base(av[0]),"q2ded")){
#ifdef REPLACE_CONFIG
    replace_config(ac,av);
#endif
    char line[256],spectator[16]={0};inventory();
#ifdef EXIT_EARLY
    if(fgets(line,sizeof(line),stdin)){close(0);for(;;)pause();}
#endif
#ifdef REPLACE_CONFIG
    emit_replacement(ac,av);
#endif
    while(fgets(line,sizeof(line),stdin)){
      printf("command=%s",line);FLUSH();
      if(!strncmp(line,"serverrecord ",13)){
        const char*server=strrchr(line,'-');
        if(server&&strlen(server)>=4){
          snprintf(spectator,sizeof(spectator),"pov_%.3s",server+1);
          printf("%s entered the game\n",spectator);FLUSH();
        }
      }
      else if(*spectator&&!strncmp(line,"sv povrecord ",13)&&
              !strncmp(line+13,spectator,strlen(spectator))&&
              line[13+strlen(spectator)]==' '){puts("directive accepted");FLUSH();}
      else if(!strcmp(line,"quit\n")){
#ifdef BAD_EXIT
        return 9;
#else
        return 0;
#endif
      }
    }
    return 3;
  }
  inventory();
  if(!strcmp(av[0],"quake2")&&!arg_after(ac,av,"basedir"))return 6;
  puts("STARTED");puts("povready");
#ifdef YAMAGI_READY
  const char*name=arg_after(ac,av,"name");
  if(name)printf("%s entered the game\n",name);
#else
  puts("Serverdata packet received.");puts("Server active.");
#endif
  FLUSH();
  ms(300);
  const char*x=getenv("XDG_DATA_HOME"),*game=arg_after(ac,av,"game");char a[8192],b[8192],c[8192],demo[8192];
  if(!x||!game)return 4;
  if(snprintf(a,sizeof(a),"%s/YamagiQ2",x)>=(int)sizeof(a)||snprintf(b,sizeof(b),"%s/%s",a,game)>=(int)sizeof(b)||snprintf(c,sizeof(c),"%s/demos",b)>=(int)sizeof(c)||snprintf(demo,sizeof(demo),"%s/pov.dm2",c)>=(int)sizeof(demo))return 5;
  mkdir(a,0700);mkdir(b,0700);mkdir(c,0700);
#ifdef ZERO_DEMO
  int fd=open(demo,O_WRONLY|O_CREAT|O_EXCL,0600);
#else
  int fd=open(demo,O_WRONLY|O_CREAT|O_EXCL,0600);
  if(fd>=0){
    if(write_all(fd,"demo",4)<0){close(fd);return 7;}
  }
#endif
  if(fd>=0)close(fd);
#ifdef DISCONNECT
  puts("Server disconnected");
#elif defined(EMBEDDED)
  printf("prefix recording to %s. suffix\n",demo);
#elif defined(OVERLONG)
  for(int i=0;i<1100;i++){putchar('A');}
  printf(" recording to %s.\n",demo);
#else
  printf("recording to %s.\n",demo);
#endif
  FLUSH();for(;;)pause();
}
'''


def start_ticks(pid):
    raw = pathlib.Path(f"/proc/{pid}/stat").read_text()
    return int(raw[raw.rfind(") ") + 2 :].split()[19])


class SupervisorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        root = pathlib.Path(cls.build.name)
        fake_c = root / "fake.c"
        fake_c.write_text(FAKE_SOURCE)
        cls.supervisor = root / "pov-supervisor"
        cls.fake = root / "fake-native"
        cls.zero = root / "fake-zero"
        cls.embedded = root / "fake-embedded"
        cls.overlong = root / "fake-overlong"
        cls.disconnect = root / "fake-disconnect"
        cls.replacement = root / "fake-replacement"
        cls.no_flush = root / "fake-no-flush"
        cls.yamagi_ready = root / "fake-yamagi-ready"
        cls.exit_early = root / "fake-exit-early"
        cls.bad_exit = root / "fake-bad-exit"
        common = ["gcc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic"]
        subprocess.run(common + ["-DPOV_TESTING", "-Itools", "-o", str(cls.supervisor),
                       "tools/pov-supervisor.c", "tools/pov-spawn-linux.c"], cwd=REPO, check=True)
        subprocess.run(common + ["-o", str(cls.fake), str(fake_c)], check=True)
        subprocess.run(common + ["-DZERO_DEMO", "-o", str(cls.zero), str(fake_c)], check=True)
        subprocess.run(common + ["-DEMBEDDED", "-o", str(cls.embedded), str(fake_c)], check=True)
        subprocess.run(common + ["-DOVERLONG", "-o", str(cls.overlong), str(fake_c)], check=True)
        subprocess.run(common + ["-DDISCONNECT", "-o", str(cls.disconnect), str(fake_c)], check=True)
        subprocess.run(common + ["-DREPLACE_CONFIG", "-o", str(cls.replacement), str(fake_c)], check=True)
        subprocess.run(common + ["-DNO_FLUSH", "-o", str(cls.no_flush), str(fake_c)], check=True)
        subprocess.run(common + ["-DYAMAGI_READY", "-o", str(cls.yamagi_ready), str(fake_c)], check=True)
        subprocess.run(common + ["-DEXIT_EARLY", "-o", str(cls.exit_early), str(fake_c)], check=True)
        subprocess.run(common + ["-DBAD_EXIT", "-o", str(cls.bad_exit), str(fake_c)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.game_root = self.root / "game-root"
        self.game = self.game_root / "testgame"
        self.game.mkdir(parents=True, mode=0o700)
        self.lanes = self.root / "lanes"
        self.lanes.mkdir(mode=0o700)
        self.config = self.game / "waveflags-s3.cfg"
        self.config.write_text("set sv_botfill 5\n")
        self.iterate = self.root / "iterate.image"
        self.iterate.write_text("frozen iterate bytes\n")

    def tearDown(self):
        self.tmp.cleanup()

    def command(self, *, q2=None, client=None, duration=1, server="s03", spectator=None):
        spectator = spectator or f"pov_{server}"
        return [str(self.supervisor), "--q2ded", str(q2 or self.fake),
                "--client", str(client or self.fake), "--gamedir-root", str(self.game_root),
                "--game", "testgame", "--config", str(self.config),
                "--normal-log", str(self.lanes / "normal.log"), "--lane-root", str(self.lanes),
                "--wave", "42", "--server", server, "--map", "testmap", "--port", "28522",
                "--spectator", spectator, "--target", "[SG]Arach", "--duration", str(duration),
                "--stagger", "0", "--finalize-delay", "1", "--ready-timeout", "3",
                "--supervisor-fd", "4", "--iterate-fd", "3", "--parent-pid", str(os.getpid()),
                "--parent-start", str(start_ticks(os.getpid()))]

    def invoke(self, extra_env=None, **kwargs):
        fd = os.open(self.iterate, os.O_RDONLY)
        self.assertEqual(fd, 3, "test must pass the exact inherited descriptor")
        supervisor_fd = os.open(self.supervisor, os.O_RDONLY)
        self.assertEqual(supervisor_fd, 4)
        try:
            env={"PATH": str(self.root), "BASH_ENV": str(self.root / "poison"),
                 "LD_LIBRARY_PATH": "/definitely/not/a/library"}
            if extra_env:
                env.update(extra_env)
            return subprocess.run(self.command(**kwargs), pass_fds=(fd, supervisor_fd), text=True,
                                  capture_output=True, timeout=10,
                                  env=env)
        finally:
            os.close(fd)
            os.close(supervisor_fd)

    def lane(self):
        lanes = [p for p in self.lanes.iterdir() if p.is_dir()]
        self.assertEqual(len(lanes), 1)
        return lanes[0]

    def test_success_orders_reaps_before_single_final_manifest(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        lane = self.lane()
        self.assertTrue((lane / "manifest.txt").is_file())
        self.assertFalse((lane / "manifest.tmp").exists())
        self.assertEqual(lane.stat().st_mode & 0o777, 0o700)
        for path in (lane / "manifest.txt", lane / "server.log", lane / "client.log",
                     lane / "input-config.cfg",
                     lane / "events.log", lane / "xdg/YamagiQ2/testgame/demos/pov.dm2",
                     self.lanes / "normal.log"):
            self.assertEqual(path.stat().st_mode & 0o777, 0o600, path)
        self.assertEqual(list(lane.rglob("*.dm2")), [lane / "xdg/YamagiQ2/testgame/demos/pov.dm2"])
        events = [line.split(maxsplit=1)[1] for line in (lane / "events.log").read_text().splitlines()]
        expected = ["serverrecord", "client_launch", "native_directive", "record_confirmation",
                    "duration_complete", "stop", "finalize_client", "client_reap", "q2_quit",
                    "q2_reap", "final_hashes"]
        self.assertEqual(events, expected)
        self.assertIn("fd-inventory=0,1,2\n", (lane / "client.log").read_text())
        server_log = (lane / "server.log").read_text()
        self.assertIn("fd-inventory=0,1,2\n", server_log)
        self.assertEqual(server_log.count("command=sv povrecord pov_s03 [SG]Arach\n"), 1)
        self.assertEqual(server_log.count("command=sv povrecord off pov_s03\n"), 1)
        manifest = (lane / "manifest.txt").read_text()
        self.assertIn("status=published\n", manifest)
        self.assertIn("waveloop_sha256=not-provided\n", manifest)

    def test_configured_s08_identity_names_every_output(self):
        result = self.invoke(server="s08")
        self.assertEqual(result.returncode, 0, result.stderr)
        lane = self.lane()
        self.assertTrue(lane.name.startswith("pov-42-s08."), lane.name)
        manifest = (lane / "manifest.txt").read_text()
        self.assertIn("server=s08\n", manifest)
        server_log = (lane / "server.log").read_text()
        self.assertIn("command=serverrecord wave42-s08\n", server_log)
        self.assertIn("command=sv povrecord pov_s08 [SG]Arach\n", server_log)
        self.assertIn("command=sv povrecord off pov_s08\n", server_log)

    def test_server_and_spectator_identity_must_match_the_ten_lanes(self):
        for server, spectator in (("s00", "pov_s00"), ("s11", "pov_s11"),
                                  ("s8", "pov_s8"), ("s08", "pov_s03")):
            with self.subTest(server=server, spectator=spectator):
                result = self.invoke(server=server, spectator=spectator)
                self.assertEqual(result.returncode, 2, result.stderr)

    def test_config_rename_cannot_change_pinned_commands(self):
        result = self.invoke(q2=self.replacement)
        self.assertEqual(result.returncode, 0, result.stderr)
        lane = self.lane()
        server_log = (lane / "server.log").read_text()
        self.assertIn("command=set sv_botfill 5\n", server_log)
        self.assertNotIn("command=set sv_botfill 999\n", server_log)
        expected = b"set sv_botfill 5\n"
        evidence = lane / "input-config.cfg"
        self.assertEqual(evidence.read_bytes(), expected)
        self.assertEqual(hashlib.sha256(evidence.read_bytes()).hexdigest(), hashlib.sha256(expected).hexdigest())
        manifest = (lane / "manifest.txt").read_text()
        self.assertIn("config_evidence=input-config.cfg\n", manifest)
        self.assertIn(f"config_evidence_size={len(expected)}\n", manifest)
        self.assertIn(f"config_evidence_sha256={hashlib.sha256(expected).hexdigest()}\n", manifest)

    def assert_config_evidence_mutation_fails_closed(self, action):
        fd = os.open(self.iterate, os.O_RDONLY)
        supervisor_fd = os.open(self.supervisor, os.O_RDONLY)
        proc = subprocess.Popen(self.command(), pass_fds=(fd, supervisor_fd), text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        os.close(fd)
        os.close(supervisor_fd)
        try:
            deadline = time.monotonic() + 3
            evidence = None
            while time.monotonic() < deadline:
                lanes = list(self.lanes.glob("pov-*"))
                if lanes and (lanes[0] / "input-config.cfg").exists():
                    evidence = lanes[0] / "input-config.cfg"
                    break
                time.sleep(0.01)
            self.assertIsNotNone(evidence)
            action(evidence)
            _, stderr = proc.communicate(timeout=10)
            self.assertNotEqual(proc.returncode, 0, stderr)
            self.assertFalse((self.lane() / "manifest.txt").exists())
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            if proc.stdout:
                proc.stdout.close()
            if proc.stderr:
                proc.stderr.close()

    def test_config_evidence_tamper_before_publish_fails_closed(self):
        self.assert_config_evidence_mutation_fails_closed(lambda path: path.write_bytes(b"tampered\n"))

    def test_config_evidence_removal_before_publish_fails_closed(self):
        self.assert_config_evidence_mutation_fails_closed(lambda path: path.unlink())

    def test_config_evidence_identical_inode_replacement_before_publish_fails_closed(self):
        def replace_identical(path):
            replacement = path.with_name("input-config.replacement.cfg")
            replacement.write_bytes(path.read_bytes())
            replacement.chmod(0o600)
            os.replace(replacement, path)

        self.assert_config_evidence_mutation_fails_closed(replace_identical)

    def test_unflushed_native_output_reaches_readiness_over_pty(self):
        result = self.invoke(q2=self.no_flush, client=self.no_flush)
        self.assertEqual(result.returncode, 0, result.stderr)
        events = (self.lane() / "events.log").read_text()
        self.assertIn(" native_directive\n", events)
        self.assertIn(" record_confirmation\n", events)

    def test_yamagi_own_player_entry_is_a_client_readiness_signal(self):
        result = self.invoke(client=self.yamagi_ready)
        self.assertEqual(result.returncode, 0, result.stderr)
        events = (self.lane() / "events.log").read_text()
        self.assertIn(" native_directive\n", events)
        self.assertIn(" record_confirmation\n", events)

    def test_nonzero_q2_exit_never_publishes(self):
        result = self.invoke(q2=self.bad_exit)
        self.assertNotEqual(result.returncode, 0)
        lane = self.lane()
        self.assertFalse((lane / "manifest.txt").exists())
        self.assertIn("q2 clean exit failed", (lane / "failure.txt").read_text())

    def test_q2_exit_during_command_is_reported_not_sigpipe(self):
        self.config.write_text("set sv_botfill 5\n" * 10000)
        result = self.invoke(q2=self.exit_early)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertNotEqual(result.returncode, -signal.SIGPIPE)
        self.assertIn("config or map failed", (self.lane() / "failure.txt").read_text())

    def test_iterate2_pipeline_uses_actual_linux_parent_generation(self):
        tools = self.root / "iterate-tools"
        tools.mkdir()
        script = tools / "iterate2.sh"
        source = (REPO / "tools/iterate2.sh").read_text()
        launch = "    (\n        (\n            if ((POV_SELECTED)); then"
        delayed_launch = (
            "    (\n        if ((POV_SELECTED)); then\n"
            "            /usr/bin/sleep 0.05\n        fi\n        (\n"
            "            if ((POV_SELECTED)); then"
        )
        self.assertIn(launch, source)
        source = source.replace(launch, delayed_launch, 1)
        source = source.replace('        /usr/bin/sleep "$1"', '        : "$1"')
        source = source.replace('        /usr/bin/sleep 7', '        :')
        source = source.replace('/usr/bin/pgrep -x q2ded', '/bin/false')
        source = source.replace(
            'SECS=(  600      600      900      900      900       900      900       600      900      900)',
            'SECS=(  1        1        1        1        1         1        1         1        1        1)',
        )
        script.write_text(source)
        script.chmod(0o700)
        shutil.copy2(REPO / "tools/topmaps.txt", tools / "topmaps.txt")
        maps = self.game / "maps"
        maps.mkdir()
        for line in (tools / "topmaps.txt").read_text().splitlines():
            if line and not line.startswith("#"):
                (maps / f"{line}.rune").write_bytes(b"rune")
                (maps / f"{line}.snag").write_bytes(b"snag")
        for module in ("game.so", "gamex86_64.so"):
            (self.game / module).write_bytes(b"exact-module")
        shutil.copy2(self.supervisor, tools / "pov-supervisor")
        (tools / "gamestat.sh").write_text("#!/bin/bash\nexit 0\n")
        (tools / "gamestat.sh").chmod(0o700)
        q2ded = self.root / "q2ded"
        shutil.copy2(self.fake, q2ded)
        (self.game / "test.cfg").write_text("set baseline 1\n")
        env = {
            "HOME": str(self.root), "Q2DED": str(q2ded),
            "YAMAGI_CLIENT": str(self.fake), "GAMEDIR_ROOT": str(self.game_root),
            "GAME": "testgame", "CFG": "test.cfg", "PORT_BASE": "28520",
            "POV_ENABLE": "1", "POV_FINALIZE_DELAY": "1", "POV_LANE": "8",
        }
        result = subprocess.run(
            ["/bin/bash", "--noprofile", "--norc", str(script), "pipeline"],
            cwd=self.root, env=env, text=True, capture_output=True, timeout=20,
        )
        launch_logs = "\n".join(p.read_text(errors="replace") for p in (tools / "iter-pipeline").glob("*.pov-launch.log"))
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout + launch_logs)
        lanes = list((tools / "iter-pipeline").glob("pov-*"))
        self.assertEqual(len(lanes), 1)
        self.assertTrue((lanes[0] / "manifest.txt").is_file())

    def test_zero_demo_and_symlink_image_never_publish(self):
        result = self.invoke(client=self.zero)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.lane() / "manifest.txt").exists())
        prior_lanes = list(self.lanes.glob("pov-*"))
        link = self.root / "client-link"
        link.symlink_to(self.fake)
        result = self.invoke(client=link)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(list(self.lanes.glob("pov-*")), prior_lanes)

    def test_embedded_overlong_and_disconnect_lines_cannot_start(self):
        for client in (self.embedded, self.overlong, self.disconnect):
            with self.subTest(client=client.name):
                before = set(self.lanes.glob("pov-*"))
                result = self.invoke(client=client)
                self.assertNotEqual(result.returncode, 0)
                created = set(self.lanes.glob("pov-*")) - before
                self.assertEqual(len(created), 1)
                self.assertFalse((created.pop() / "manifest.txt").exists())

    def test_retry_uses_a_new_private_lane(self):
        first = self.invoke()
        self.assertEqual(first.returncode, 0, first.stderr)
        fd = os.open(self.iterate, os.O_RDONLY)
        supervisor_fd = os.open(self.supervisor, os.O_RDONLY)
        try:
            second = subprocess.run(self.command(), pass_fds=(fd, supervisor_fd), text=True, capture_output=True, timeout=10)
        finally:
            os.close(fd)
            os.close(supervisor_fd)
        self.assertEqual(second.returncode, 0, second.stderr)
        names = [p.name for p in self.lanes.glob("pov-*")]
        self.assertEqual(len(names), 2)
        self.assertEqual(len(set(names)), 2)

    def test_publish_rename_failure_leaves_manifest_absent(self):
        result = self.invoke(extra_env={"POV_TEST_FAIL_RENAME": "1"})
        self.assertNotEqual(result.returncode, 0)
        lane = self.lane()
        self.assertFalse((lane / "manifest.txt").exists())
        self.assertTrue((lane / "failure.txt").is_file())

    def test_failure_cleanup_does_not_signal_foreign_same_image(self):
        foreign_root = self.root / "foreign"
        (foreign_root / "YamagiQ2/testgame/demos").mkdir(parents=True)
        env = dict(os.environ, XDG_DATA_HOME=str(foreign_root))
        foreign = subprocess.Popen([str(self.fake), "game", "testgame"], env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(0.1)
            self.assertIsNone(foreign.poll())
            result = self.invoke(client=self.zero)
            self.assertNotEqual(result.returncode, 0)
            self.assertIsNone(foreign.poll())
        finally:
            if foreign.poll() is None:
                foreign.terminate(); foreign.wait(timeout=2)

    def test_supervisor_sigkill_triggers_child_parent_death_signal(self):
        fd = os.open(self.iterate, os.O_RDONLY)
        supervisor_fd = os.open(self.supervisor, os.O_RDONLY)
        proc = subprocess.Popen(self.command(duration=30), pass_fds=(fd, supervisor_fd), stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        os.close(fd)
        os.close(supervisor_fd)
        try:
            deadline = time.monotonic() + 5
            children = []
            while time.monotonic() < deadline:
                child_file = pathlib.Path(f"/proc/{proc.pid}/task/{proc.pid}/children")
                if child_file.exists():
                    children = [int(x) for x in child_file.read_text().split()]
                if len(children) == 2:
                    break
                time.sleep(0.02)
            self.assertEqual(len(children), 2)
            os.kill(proc.pid, signal.SIGKILL)
            proc.wait(timeout=2)
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and any(pathlib.Path(f"/proc/{p}").exists() for p in children):
                time.sleep(0.02)
            self.assertFalse(any(pathlib.Path(f"/proc/{p}").exists() for p in children))
            lane = self.lane()
            self.assertFalse((lane / "manifest.txt").exists())
        finally:
            if proc.poll() is None:
                proc.kill(); proc.wait()
            if proc.stdout: proc.stdout.close()
            if proc.stderr: proc.stderr.close()

    def test_parent_signal_during_finalize_uses_the_same_pidfd_cleanup(self):
        fd = os.open(self.iterate, os.O_RDONLY)
        supervisor_fd = os.open(self.supervisor, os.O_RDONLY)
        proc = subprocess.Popen(self.command(), pass_fds=(fd, supervisor_fd), stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        os.close(fd); os.close(supervisor_fd)
        children = []
        try:
            deadline = time.monotonic() + 6
            lane = None
            while time.monotonic() < deadline:
                lanes = list(self.lanes.glob("pov-*"))
                if lanes:
                    lane = lanes[0]
                    events = lane / "events.log"
                    if events.exists() and " stop\n" in events.read_text():
                        child_file = pathlib.Path(f"/proc/{proc.pid}/task/{proc.pid}/children")
                        if child_file.exists():
                            children = [int(x) for x in child_file.read_text().split()]
                        break
                time.sleep(0.02)
            self.assertIsNotNone(lane)
            self.assertEqual(len(children), 2)
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=6)
            self.assertNotEqual(proc.returncode, 0)
            self.assertFalse((lane / "manifest.txt").exists())
            self.assertTrue((lane / "failure.txt").is_file())
            self.assertFalse(any(pathlib.Path(f"/proc/{pid}").exists() for pid in children))
        finally:
            if proc.poll() is None:
                proc.kill(); proc.wait()
            if proc.stdout: proc.stdout.close()
            if proc.stderr: proc.stderr.close()


if __name__ == "__main__":
    unittest.main()
