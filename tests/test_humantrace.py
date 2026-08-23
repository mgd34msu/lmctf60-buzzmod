from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools import humantrace


def state(origin: list[int]) -> dict[str, object]:
    return {
        "type": 0,
        "origin": origin,
        "velocity": [80, 0, 0],
        "flags": 4,
        "time": 0,
        "gravity": 800,
        "delta_angles": [0, 0, 0],
    }


def step(sequence: int, frame: int, before: list[int], after: list[int],
         *, client: int = 1, snapinitial: int = 0) -> dict[str, object]:
    return {
        "format": humantrace.TRACE_FORMAT,
        "kind": "step",
        "seq": sequence,
        "client": client,
        "frame": frame,
        "snapinitial": snapinitial,
        "cmd": {
            "msec": 25,
            "buttons": 0,
            "angles": [0, 16384, 0],
            "forward": 400,
            "side": 0,
            "up": 0,
            "impulse": 0,
            "light": 90,
        },
        "before": state(before),
        "after": state(after),
        "ground": 0,
        "waterlevel": 0,
        "watertype": 0,
        "touches": [0, 37],
    }


def header(map_name: str = "lmctf01") -> dict[str, object]:
    return {
        "format": humantrace.TRACE_FORMAT,
        "kind": "header",
        "map": map_name,
        "bsp_checksum": 123,
        "entity_crc32": 456,
        "physics_id": 1,
        "module_revision": 1532,
        "module_version": "abc1234",
    }


class HumanTraceTest(unittest.TestCase):
    def write_trace(self, directory: Path,
                    records: list[dict[str, object]]) -> Path:
        path = directory / "humantrace-lmctf01.jsonl"
        path.write_text(
            "".join(json.dumps(record) + "\n" for record in records),
            encoding="utf-8")
        return path

    def test_import_preserves_commands_and_splits_external_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = self.write_trace(Path(temporary), [
                header(),
                step(1, 10, [0, 0, 192], [8, 0, 192]),
                step(2, 10, [8, 0, 192], [16, 0, 192]),
                step(3, 11, [40, 0, 192], [48, 0, 192]),
            ])
            session = humantrace.select_session(
                humantrace.read_sessions(path), "latest", "lmctf01")
            evidence = humantrace.build_evidence(
                path, session, 1, None, None)

        self.assertEqual(evidence["steps"][0]["cmd"]["forward"], 400)
        self.assertEqual(evidence["steps"][0]["cmd"]["angles"][1], 16384)
        self.assertEqual(evidence["steps"][0]["touches"], [0, 37])
        self.assertEqual(
            evidence["segments"],
            [
                {"start_index": 0, "end_exclusive": 2,
                 "reason": "trace-start"},
                {"start_index": 2, "end_exclusive": 3,
                 "reason": "authoritative-state-change"},
            ])
        self.assertEqual(len(evidence["source"]["sha256"]), 64)

    def test_latest_session_and_client_selection_are_explicit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = self.write_trace(Path(temporary), [
                header("oldmap"),
                step(1, 1, [0, 0, 0], [1, 0, 0]),
                header(),
                step(1, 2, [0, 0, 0], [1, 0, 0]),
                step(2, 2, [0, 0, 0], [1, 0, 0], client=2),
            ])
            session = humantrace.select_session(
                humantrace.read_sessions(path), "latest", None)
            with self.assertRaisesRegex(ValueError, "select one client"):
                humantrace.build_evidence(path, session, None, None, None)
            evidence = humantrace.build_evidence(
                path, session, 2, None, None)

        self.assertEqual(session["identity"]["map"], "lmctf01")
        self.assertEqual(evidence["client"], 2)
        self.assertEqual(len(evidence["steps"]), 1)

    def test_cli_writes_validated_evidence_atomically(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            trace = self.write_trace(directory, [
                header(),
                step(1, 10, [0, 0, 192], [8, 0, 192]),
            ])
            output = directory / "evidence.json"
            result = subprocess.run(
                [sys.executable, "tools/humantrace.py", str(trace),
                 "--output", str(output), "--map", "lmctf01",
                 "--client", "1"],
                cwd=Path(__file__).resolve().parents[1],
                text=True,
                capture_output=True,
                check=False,
            )
            payload = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("steps=1 segments=1", result.stdout)
        self.assertEqual(payload["format"], humantrace.EVIDENCE_FORMAT)

    def test_rejects_snapshot_shaped_data_without_commands(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            bad = step(1, 10, [0, 0, 0], [1, 0, 0])
            del bad["cmd"]
            path = self.write_trace(Path(temporary), [header(), bad])
            with self.assertRaisesRegex(ValueError, "cmd has the wrong fields"):
                humantrace.read_sessions(path)


if __name__ == "__main__":
    unittest.main()
