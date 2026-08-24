import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest

from tools import build_python_runtime
from tools import rune_corpus_controller as controller


class BuildPythonRuntimeTest(unittest.TestCase):
    def test_optional_zstd_extension_is_a_dependency_root_when_available(self):
        with tempfile.TemporaryDirectory() as temporary:
            dynload = Path(temporary)
            zstd = dynload / "_zstd.cpython-314-x86_64-linux-gnu.so"
            unrelated = dynload / "_sqlite3.cpython-314-x86_64-linux-gnu.so"
            zstd.write_bytes(b"zstd")
            unrelated.write_bytes(b"sqlite")

            roots = build_python_runtime._extension_dependency_roots(dynload)

            self.assertIn(zstd, roots)
            self.assertNotIn(unrelated, roots)

    def test_loader_listing_extracts_only_resolved_absolute_libraries(self):
        listing = """
\tlinux-vdso.so.1 (0x00007fff)
\tlibpython3.14.so.1.0 => /usr/lib/libpython3.14.so.1.0 (0x00007f00)
\tlibc.so.6 => /usr/lib/libc.so.6 (0x00007e00)
\t/lib64/ld-linux-x86-64.so.2 (0x00007d00)
"""
        self.assertEqual(
            [
                ("libpython3.14.so.1.0", Path("/usr/lib/libpython3.14.so.1.0")),
                ("libc.so.6", Path("/usr/lib/libc.so.6")),
            ],
            build_python_runtime.parse_loader_listing(listing),
        )

    def test_build_is_link_free_and_satisfies_controller_layout(self):
        with tempfile.TemporaryDirectory() as temporary:
            runtime = Path(temporary) / "python-runtime"
            build_python_runtime.build_runtime(runtime)

            self.assertFalse(any(path.is_symlink() for path in runtime.rglob("*")))
            self.assertFalse(any(path.name == "__pycache__" for path in runtime.rglob("*")))
            self.assertFalse(any(path.suffix == ".pyc" for path in runtime.rglob("*")))

            expanded = controller._expand_python_runtime_inputs(
                {"python-runtime": ("python_runtime", runtime)}
            )
            entries = []
            for logical, (role, source) in expanded.items():
                info = source.stat()
                entries.append(
                    {
                        "path": logical,
                        "role": role,
                        "mode": stat.S_IMODE(info.st_mode),
                        "size": info.st_size,
                        "sha256": controller.sha256_regular(source),
                    }
                )
            layout = controller._python_runtime_layout(entries)
            self.assertTrue(layout["version"])
            interpreter = runtime / layout["interpreter"]["path"].removeprefix("python-runtime/")
            loader = runtime / layout["loader"]["path"].removeprefix("python-runtime/")
            self.assertTrue(interpreter.is_file())
            self.assertTrue(loader.is_file())

            completed = subprocess.run(
                [
                    str(loader),
                    "--inhibit-cache",
                    "--library-path",
                    str(runtime / "lib"),
                    str(interpreter),
                    "-I",
                    "-S",
                    "-B",
                    "-c",
                    controller.PYTHON_RUNTIME_PROBE,
                ],
                cwd=runtime,
                env=dict(controller.PYTHON_ENVIRONMENT),
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            report = json.loads(completed.stdout)
            self.assertTrue(report["dont_write_bytecode"])
            runtime_root = str(runtime.resolve())
            runtime_prefix = runtime_root + os.sep
            for path in (
                report["executable"],
                report["prefix"],
                report["base_prefix"],
                *report["sys_path"],
            ):
                self.assertTrue(path == runtime_root or path.startswith(runtime_prefix), path)
            for path in [*report["modules"].values(), *report["loaded_libraries"]]:
                if path is not None:
                    self.assertTrue(path.startswith(runtime_prefix), path)


if __name__ == "__main__":
    unittest.main()
