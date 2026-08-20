#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FlagStateTest(unittest.TestCase):
    def test_dropped_flag_owner_is_only_a_self_touch_lockout(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sg-flag-state-") as raw:
            binary = Path(raw) / "sg_flag_state_test"
            (Path(raw) / "GitRevisionInfo.h").write_text(
                '#define LMCTF_REVISION 0\n#define LMCTF_VERSION "test"\n'
                '#define COPYRIGHT "test"\n',
                encoding="ascii",
            )
            command = [
                os.environ.get("CC", "cc"),
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wpedantic",
                "-ffunction-sections",
                "-fdata-sections",
                f"-I{raw}",
                "-I.",
                "tests/sg_flag_state_test.c",
                "slipgate/sg_util.c",
                "-Wl,--gc-sections",
                "-lm",
                "-o",
                str(binary),
            ]
            subprocess.run(command, cwd=ROOT, check=True)
            subprocess.run([binary], check=True)


if __name__ == "__main__":
    unittest.main()
