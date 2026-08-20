#!/usr/bin/env python3
"""Pin the fleet wrapper's no-foreign-process ownership boundary."""

import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]


class WaveloopProcessScopeTest(unittest.TestCase):
    def test_failed_wave_never_name_kills_servers(self):
        source = (ROOT / "tools/waveloop.sh").read_text(encoding="utf-8")

        self.assertNotIn("pkill", source)
        self.assertNotIn("killall", source)
        self.assertNotIn("kill -", source)
        self.assertIn("./iterate2.sh", source)
        launch = next(line for line in source.splitlines()
                      if line.lstrip().startswith("./iterate2.sh"))
        self.assertNotRegex(launch, r"&\s*$")

    def test_fast_failure_leaves_foreign_q2ded_alive(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            runner = work / "waveloop.sh"
            runner.write_bytes((ROOT / "tools/waveloop.sh").read_bytes())
            runner.chmod(0o755)
            (work / "iterate2.sh").write_text("#!/bin/sh\nexit 3\n", encoding="utf-8")
            (work / "iterate2.sh").chmod(0o755)
            (work / "deploy.sh").write_text("#!/bin/sh\nexit 99\n", encoding="utf-8")
            (work / "deploy.sh").chmod(0o755)

            foreign_binary = work / "q2ded"
            shutil.copy2("/bin/sleep", foreign_binary)
            foreign = subprocess.Popen([str(foreign_binary), "30"])

            # Sandbox every name-based lookup/kill spelling while still
            # returning the exact foreign PID. A regression may kill this
            # fixture, never a real server elsewhere on the host.
            shim_dir = work / "shim"
            shim_dir.mkdir()
            for command in ("pgrep", "pkill", "killall"):
                shim = shim_dir / command
                shim.write_text(
                    "#!/bin/sh\n"
                    "if [ \"$1\" = \"-x\" ]; then shift; fi\n"
                    "case \"${1:-}\" in\n"
                    "  q2ded)\n"
                    + (
                        "    echo \"$FOREIGN_PID\"\n"
                        if command == "pgrep"
                        else "    kill -TERM \"$FOREIGN_PID\"\n"
                    )
                    + "    exit 0;;\n"
                    "esac\n"
                    "exit 1\n",
                    encoding="utf-8",
                )
                shim.chmod(0o755)

            environment = os.environ.copy()
            environment["FOREIGN_PID"] = str(foreign.pid)
            environment["PATH"] = f"{shim_dir}:{environment['PATH']}"
            loop = subprocess.Popen(
                [str(runner), "9001"],
                cwd=work,
                env=environment,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            try:
                log = work / "waveloop.log"
                for _ in range(100):
                    if log.exists() and "FAILED" in log.read_text(encoding="utf-8"):
                        break
                    time.sleep(0.02)
                self.assertTrue(log.exists())
                self.assertIn("FAILED", log.read_text(encoding="utf-8"))
                self.assertIsNone(foreign.poll())
            finally:
                if loop.poll() is None:
                    os.killpg(loop.pid, signal.SIGTERM)
                    loop.wait(timeout=5)
                if foreign.poll() is None:
                    foreign.terminate()
                foreign.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
