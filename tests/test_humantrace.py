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
    def test_trace_remains_a_read_only_diagnostic_input(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = self.write_trace(Path(temporary), [
                header(), step(1, 10, [0, 0, 0], [8, 0, 0]),
            ])
            session = humantrace.select_session(
                humantrace.read_sessions(path), "latest", None)
            evidence = humantrace.build_evidence(
                path, session, 1, None, None)

        self.assertNotIn("rune_bindings", evidence)

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

    def test_v2_exports_source_bound_ordered_hook_events(self) -> None:
        trace_format = humantrace.TRACE_FORMAT_V2
        records = [
            {**header(), "format": trace_format},
            {**step(1, 10, [0, 0, 0], [8, 0, 0]),
             "format": trace_format},
            {
                "format": trace_format, "kind": "hook-fire", "event": 1,
                "after_step": 1, "client": 1, "frame": 10, "hook": 7,
                "origin_q8": [8, 0, 0], "velocity_q8": [80, 0, 0],
                "view_short": [-2048, 8192], "hand": 0,
            },
            {
                "format": trace_format, "kind": "hook-attach", "event": 2,
                "after_step": 1, "client": 1, "frame": 10, "hook": 7,
                "bite_q8": [1200, -2300, 440], "target": 0, "world": 1,
            },
            {**step(2, 11, [8, 0, 0], [16, 0, 0]),
             "format": trace_format},
            {
                "format": trace_format, "kind": "hook-release", "event": 3,
                "after_step": 2, "client": 1, "frame": 11, "hook": 7,
                "origin_q8": [16, 0, 0], "velocity_q8": [400, 0, 40],
            },
        ]
        with tempfile.TemporaryDirectory() as temporary:
            path = self.write_trace(Path(temporary), records)
            session = humantrace.read_sessions(path)[0]
            evidence = humantrace.build_evidence(
                path, session, 1, None, None)

        self.assertEqual(evidence["format"], humantrace.EVIDENCE_FORMAT_V2)
        self.assertEqual(
            [event["kind"] for event in evidence["hook_events"]],
            ["hook-fire", "hook-attach", "hook-release"],
        )
        self.assertEqual(evidence["hook_events"][1]["bite_q8"],
                         [1200, -2300, 440])

    def test_v2_rejects_bad_hook_event_order_and_after_step(self) -> None:
        trace_format = humantrace.TRACE_FORMAT_V2
        fire = {
            "format": trace_format, "kind": "hook-fire", "event": 2,
            "after_step": 1, "client": 1, "frame": 10, "hook": 7,
            "origin_q8": [8, 0, 0], "velocity_q8": [80, 0, 0],
            "view_short": [-2048, 8192], "hand": 0,
        }
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            path = self.write_trace(directory, [
                {**header(), "format": trace_format},
                {**step(1, 10, [0, 0, 0], [8, 0, 0]),
                 "format": trace_format},
                fire,
                {
                    "format": trace_format, "kind": "hook-release",
                    "event": 2, "after_step": 1, "client": 1,
                    "frame": 10, "hook": 7, "origin_q8": [8, 0, 0],
                    "velocity_q8": [80, 0, 0],
                },
            ])
            with self.assertRaisesRegex(ValueError, "hook event order"):
                humantrace.read_sessions(path)

            path = self.write_trace(directory, [
                {**header(), "format": trace_format},
                {**fire, "event": 1, "after_step": 9},
            ])
            with self.assertRaisesRegex(ValueError, "after_step"):
                humantrace.read_sessions(path)

    def test_v2_rejects_a_world_attachment_lie(self) -> None:
        trace_format = humantrace.TRACE_FORMAT_V2
        with tempfile.TemporaryDirectory() as temporary:
            path = self.write_trace(Path(temporary), [
                {**header(), "format": trace_format},
                {
                    "format": trace_format, "kind": "hook-attach",
                    "event": 1, "after_step": 0, "client": 1,
                    "frame": 10, "hook": 7, "bite_q8": [0, 0, 0],
                    "target": 37, "world": 1,
                },
            ])
            with self.assertRaisesRegex(ValueError, "world target mismatch"):
                humantrace.read_sessions(path)


if __name__ == "__main__":
    unittest.main()
