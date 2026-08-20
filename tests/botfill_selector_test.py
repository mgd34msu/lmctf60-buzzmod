from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BotfillSelectorTest(unittest.TestCase):
    def test_real_selector_preserves_automatic_carriers(self) -> None:
        compiler = os.environ.get("CC", "cc")
        with tempfile.TemporaryDirectory(prefix="sg-botfill-") as temp:
            object_path = Path(temp) / "sg_client.o"
            binary_path = Path(temp) / "sg_botfill_selector_test"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-ffunction-sections",
                    "-fdata-sections",
                    "-DSG_BOTFILL_TEST_API",
                    "-I.",
                    "-c",
                    "slipgate/sg_client.c",
                    "-o",
                    str(object_path),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wl,--gc-sections",
                    "-I.",
                    "tests/sg_botfill_selector_test.c",
                    str(object_path),
                    "-lm",
                    "-ldl",
                    "-o",
                    str(binary_path),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(binary_path)], cwd=ROOT, check=True)
