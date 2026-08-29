from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY_NAME = os.environ.get(
    "SG_HUMAN_TRACE_TEST_BINARY", "sg_human_trace_hook_test.gnu"
)
BINARY = ROOT / BINARY_NAME
PREFIX = "humantrace-tracehook-00000065-000000ca-"


def ensure_binary() -> None:
    if BINARY.exists():
        return
    if BINARY_NAME != "sg_human_trace_hook_test.gnu":
        raise FileNotFoundError(BINARY)
    subprocess.run(
        ["make", "-f", "GNUmakefile", BINARY.name],
        cwd=ROOT,
        check=True,
        stdout=subprocess.DEVNULL,
    )


def read_valid_prefix(
    path: Path, previous: str = "0" * 64
) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
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
        ensure_binary()
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

        header = sessions[0][0]
        self.assertEqual(header["physics_id"], 0)
        self.assertEqual(header["host_physics_id"], 1)
        self.assertEqual(header["gravity_bits"], 0x44480000)
        self.assertEqual(header["airaccelerate_bits"], 0x3FC00000)
        self.assertEqual(header["maxvelocity_bits"], 0x44FA0000)
        self.assertEqual(header["pmove_substep_ms"], 25)
        self.assertEqual(header["server_frame_ms"], 100)
        self.assertEqual(header["physics_flags"], 0)

        step = sessions[0][1]
        self.assertEqual(step["snapinitial"], 1)
        self.assertEqual(step["cmd"], {
            "msec": 25,
            "buttons": 3,
            "angles": [1234, -2345, 3456],
            "forward": 400,
            "side": -300,
            "up": 200,
            "impulse": 17,
            "light": 91,
        })
        self.assertEqual(step["before"], {
            "type": 0,
            "origin": [8, -16, 24],
            "velocity": [80, -96, 112],
            "flags": 5,
            "time": 7,
            "gravity": 777,
            "delta_angles": [101, -202, 303],
        })
        self.assertEqual(step["after"], {
            "type": 0,
            "origin": [16, -24, 32],
            "velocity": [120, -136, 152],
            "flags": 9,
            "time": 11,
            "gravity": 333,
            "delta_angles": [404, -505, 606],
        })
        self.assertEqual(step["viewangles_bits"],
                         [0x3F8CCCCD, 0x400CCCCD, 0x40533333])
        self.assertEqual(step["viewheight_bits"], 0x41B00000)
        self.assertEqual(step["mins_bits"],
                         [0xC1800000, 0xC1800000, 0xC1C00000])
        self.assertEqual(step["maxs_bits"],
                         [0x41800000, 0x41800000, 0x42000000])
        self.assertEqual(step["ground"], 0)
        self.assertEqual(step["waterlevel"], 2)
        self.assertEqual(step["watertype"], 32)
        self.assertEqual(step["numtouch"], 2)
        self.assertEqual(step["touches"], [0, 2])
        self.assertEqual(sessions[1][1]["ground"], -1)

        hook_fire = sessions[0][2]
        self.assertEqual(hook_fire["origin_bits"][0], 0x3F8CCCCD)
        self.assertEqual(hook_fire["velocity_bits"][0], 0x400CCCCD)
        self.assertEqual(hook_fire["hook_origin_bits"][0], 0x42803333)
        self.assertEqual(hook_fire["hook_velocity_bits"][0], 0x40A66666)

    @unittest.skipIf(os.name == "nt", "RLIMIT_FSIZE is POSIX-only")
    def test_file_size_limit_disables_capture_without_sigxfsz(self) -> None:
        ensure_binary()
        with tempfile.TemporaryDirectory() as temporary:
            completed = subprocess.run(
                [str(BINARY), temporary, "fsize"], cwd=ROOT,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True,
            )

        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_exclusive_create_collision_retries_a_new_session(self) -> None:
        ensure_binary()
        with tempfile.TemporaryDirectory() as temporary:
            subprocess.run(
                [str(BINARY), temporary, "collision"], cwd=ROOT,
                check=True, stdout=subprocess.DEVNULL,
            )
            files = sorted(Path(temporary).glob(f"{PREFIX}*.jsonl"))
            records = [read_valid_prefix(path) for path in files]

        self.assertEqual(len(records), 2)
        self.assertEqual(records[1][0]["session"], 1)

    def test_physics_change_starts_an_exactly_bound_segment(self) -> None:
        ensure_binary()
        with tempfile.TemporaryDirectory() as temporary:
            subprocess.run(
                [str(BINARY), temporary, "physics"], cwd=ROOT,
                check=True, stdout=subprocess.DEVNULL,
            )
            directory = Path(temporary)
            first = read_valid_prefix(directory / f"{PREFIX}000000.jsonl")
            second = read_valid_prefix(
                directory / f"{PREFIX}000001.jsonl",
                str(first[-1]["sha256"]),
            )
            sessions = [first, second]

        self.assertEqual(sessions[0][0]["gravity_bits"], 0x44480000)
        self.assertEqual(sessions[1][0]["gravity_bits"], 0x42C80000)
        self.assertEqual(sessions[1][0]["continuation"], 1)
        self.assertEqual(sessions[1][0]["session"], 0)
        self.assertEqual(sessions[1][0]["start_order"], 2)

    @unittest.skipIf(os.name == "nt", "fork is POSIX-only")
    def test_concurrent_processes_claim_unique_sessions(self) -> None:
        ensure_binary()
        with tempfile.TemporaryDirectory() as temporary:
            subprocess.run(
                [str(BINARY), temporary, "concurrent"], cwd=ROOT,
                check=True, stdout=subprocess.DEVNULL,
            )
            files = sorted(Path(temporary).glob(f"{PREFIX}*.jsonl"))
            records = [read_valid_prefix(path) for path in files]

        self.assertEqual(len(records), 8)
        self.assertEqual(
            sorted(record[0]["session"] for record in records),
            list(range(8)),
        )


if __name__ == "__main__":
    unittest.main()
