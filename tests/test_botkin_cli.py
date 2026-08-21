#!/usr/bin/env python3
"""Exercise botkin's explicit output and strict demo boundary."""

from __future__ import annotations

import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BOTKIN = ROOT / "tools/botkin.py"
TRACKED_RAW = ROOT / "tools/botkin_raw.json"


def _message(payload: bytes) -> bytes:
    return struct.pack("<i", len(payload)) + payload


def _serverrecord(frames: int = 22, *, terminated: bool = True) -> bytes:
    serverdata = (
        bytes([12])
        + b"\0" * 9
        + b"game\0"
        + struct.pack("<H", 0xFFFF)
        + b"level\0"
    )
    map_name = bytes([13]) + struct.pack("<H", 33) + b"maps/botkin.bsp\0"
    skin = bytes([13]) + struct.pack("<H", 1312) + b"Arach\\male/rb-rm\0"
    empty_skin = bytes([13]) + struct.pack("<H", 1313) + b"\0"
    messages = [_message(serverdata + map_name + skin + empty_skin)]
    for frame in range(1, frames + 1):
        entity = (
            bytes([1, 1])
            + struct.pack("<h", frame * 8)
            + bytes([1, 17])
            + struct.pack("<h", frame * 16)
            + bytes([0, 0])
        )
        snapshot = bytes([20]) + struct.pack("<i", frame) + bytes([18]) + entity
        messages.append(_message(snapshot))
    if terminated:
        messages.append(struct.pack("<i", -1))
    return b"".join(messages)


class BotkinCliTest(unittest.TestCase):
    def run_cli(self, *arguments: str):
        return subprocess.run(
            [sys.executable, str(BOTKIN), *arguments],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
            check=False,
        )

    def test_output_selection_is_required_and_never_touches_tracked_raw(self):
        with tempfile.TemporaryDirectory() as temporary:
            demo = Path(temporary) / "complete.dm2"
            demo.write_bytes(_serverrecord())
            before = TRACKED_RAW.read_bytes()
            completed = self.run_cli(str(demo))
            self.assertEqual(2, completed.returncode)
            self.assertEqual(before, TRACKED_RAW.read_bytes())

    def test_stdout_is_clean_json_and_diagnostics_use_stderr(self):
        with tempfile.TemporaryDirectory() as temporary:
            demo = Path(temporary) / "complete.dm2"
            demo.write_bytes(_serverrecord())
            before = TRACKED_RAW.read_bytes()
            completed = self.run_cli("--strict", "--stdout", str(demo))
            self.assertEqual(0, completed.returncode, completed.stderr)
            rows = json.loads(completed.stdout)
            self.assertEqual(1, len(rows))
            self.assertEqual("complete.dm2:Arach", rows[0]["name"])
            self.assertIn("map=botkin", completed.stderr)
            self.assertEqual(before, TRACKED_RAW.read_bytes())

    def test_output_file_is_explicit_and_atomic(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            demo = root / "complete.dm2"
            output = root / "result.json"
            demo.write_bytes(_serverrecord())
            completed = self.run_cli(
                "--strict", "--output", str(output), str(demo)
            )
            self.assertEqual(0, completed.returncode, completed.stderr)
            self.assertEqual("", completed.stdout)
            self.assertEqual(1, len(json.loads(output.read_text(encoding="utf-8"))))
            self.assertEqual([], list(root.glob(f".{output.name}.*")))

    def test_strict_accepts_clean_serverrecord_eof_without_client_marker(self):
        with tempfile.TemporaryDirectory() as temporary:
            demo = Path(temporary) / "clean-eof.dm2"
            demo.write_bytes(_serverrecord(terminated=False))
            completed = self.run_cli("--strict", "--stdout", str(demo))
            self.assertEqual(0, completed.returncode, completed.stderr)
            self.assertEqual(1, len(json.loads(completed.stdout)))

    def test_strict_rejects_truncated_malformed_and_short_demos(self):
        fixtures = {
            "truncated.dm2": b"\x01\x02\x03",
            "malformed.dm2": _message(bytes([255])) + struct.pack("<i", -1),
            "short.dm2": _serverrecord(frames=3),
        }
        for name, payload in fixtures.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                demo = root / name
                output = root / "result.json"
                demo.write_bytes(payload)
                output.write_bytes(b"old-output")
                completed = self.run_cli(
                    "--strict", "--output", str(output), str(demo)
                )
                self.assertEqual(1, completed.returncode)
                self.assertEqual(b"old-output", output.read_bytes())
                self.assertIn(name, completed.stderr)

    def test_strict_batch_does_not_publish_partial_results(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            complete = root / "complete.dm2"
            broken = root / "broken.dm2"
            output = root / "result.json"
            complete.write_bytes(_serverrecord())
            broken.write_bytes(b"\x01")
            completed = self.run_cli(
                "--strict",
                "--output",
                str(output),
                str(complete),
                str(broken),
            )
            self.assertEqual(1, completed.returncode)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
