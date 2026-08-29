from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "sg_human_trace_hook_test.gnu"
PREFIX = "humantrace-tracehook-00000065-000000ca-"


def read_valid_prefix(path: Path) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    previous = "0" * 64
    for raw_line in path.read_bytes().splitlines():
        try:
            line = raw_line.decode("utf-8")
            record = json.loads(line)
        except (UnicodeDecodeError, json.JSONDecodeError):
            break
        marker = ',"prev_sha256":"'
        marker_at = line.index(marker)
        payload = line[:marker_at] + "}"
        expected = hashlib.sha256(
            previous.encode("ascii") + payload.encode("utf-8")
        ).hexdigest()
        assert record["prev_sha256"] == previous
        assert record["sha256"] == expected
        previous = expected
        records.append(record)
    return records


class HumanTraceV3IntegrationTest(unittest.TestCase):
    def test_two_sessions_restart_recovery_and_hash_chain(self) -> None:
        subprocess.run(
            ["make", "-f", "GNUmakefile", BINARY.name],
            cwd=ROOT,
            check=True,
            stdout=subprocess.DEVNULL,
        )
        with tempfile.TemporaryDirectory() as temporary:
            environment = os.environ.copy()
            environment["SG_HUMAN_TRACE_KEEP"] = "1"
            subprocess.run(
                [str(BINARY), temporary], cwd=ROOT, env=environment,
                check=True, stdout=subprocess.DEVNULL,
            )
            directory = Path(temporary)
            sessions = [
                read_valid_prefix(directory / f"{PREFIX}{index:06}.jsonl")
                for index in range(3)
            ]

        self.assertEqual(
            [record["kind"] for record in sessions[0]],
            ["header", "step", "hook-fire", "hook-attach",
             "hook-release", "hook-reset", "end"],
        )
        self.assertEqual(len(sessions[1]), 7)
        self.assertEqual(
            [record["kind"] for record in sessions[2]],
            ["header", "step", "end"],
        )
        first = sessions[0][1:]
        second = sessions[1][1:]
        self.assertTrue(all(record.get("client") == 1 for record in first[:-1]))
        self.assertTrue(all(
            record.get("spawn_generation") == 11 for record in first[:-1]
        ))
        self.assertTrue(all(record.get("frame") == 17 for record in first))
        self.assertTrue(all(record.get("client") == 2 for record in second[:-1]))
        self.assertTrue(all(
            record.get("spawn_generation") == 22 for record in second[:-1]
        ))
        self.assertTrue(all(record.get("frame") == 31 for record in second))
        self.assertEqual(
            [record["order"] for record in first], [1, 2, 3, 4, 5, 6]
        )


if __name__ == "__main__":
    unittest.main()
