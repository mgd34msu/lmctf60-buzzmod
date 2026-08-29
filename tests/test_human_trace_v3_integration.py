from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from tools import humantrace


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


def ensure_io_binary() -> Path:
    flavor = "make" if BINARY_NAME.endswith(".make") else "gnu"
    binary = ROOT / f"sg_human_trace_io_test.{flavor}"
    subprocess.run(
        ["make", "-f", "Makefile" if flavor == "make" else "GNUmakefile",
         binary.name],
        cwd=ROOT,
        check=True,
        stdout=subprocess.DEVNULL,
    )
    return binary


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


def write_rechained_v3(
    path: Path,
    records: list[dict[str, object]],
    previous: str = "0" * 64,
) -> list[dict[str, object]]:
    lines: list[str] = []
    rewritten: list[dict[str, object]] = []
    for original in records:
        record = dict(original)
        record["prev_sha256"] = previous
        record["sha256"] = "0" * 64
        encoded = json.dumps(record, separators=(",", ":"))
        marker = ',"prev_sha256":"'
        payload = encoded[:encoded.index(marker)] + "}"
        digest = hashlib.sha256(
            previous.encode("ascii") + payload.encode("utf-8")
        ).hexdigest()
        record["sha256"] = digest
        lines.append(json.dumps(record, separators=(",", ":")))
        rewritten.append(record)
        previous = digest
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return rewritten


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
                read_valid_prefix(directory / f"{PREFIX}{index:06d}.jsonl")
                for index in range(3)
            ]

        self.assertEqual(
            [record["kind"] for record in sessions[0]],
            ["header", "step", "hook-fire", "hook-attach", "step",
             "hook-release", "hook-reset", "step", "step",
             "hook-fire", "hook-attach", "step", "hook-release",
             "end"],
        )
        self.assertEqual(len(sessions[1]), 7)
        self.assertEqual(
            [record["kind"] for record in sessions[2]],
            ["header", "step", "end"],
        )
        first = sessions[0][1:8]
        incomplete = sessions[0][8:13]
        second = sessions[1][1:]
        self.assertTrue(all(record.get("client") == 1 for record in first))
        self.assertTrue(all(
            record.get("spawn_generation") == 11 for record in first
        ))
        self.assertEqual(
            [record.get("frame") for record in first],
            [17, 17, 17, 18, 19, 19, 20],
        )
        self.assertTrue(all(record.get("client") == 2 for record in incomplete))
        self.assertTrue(all(
            record.get("spawn_generation") == 22 for record in incomplete
        ))
        self.assertEqual(
            [record.get("frame") for record in incomplete],
            [25, 25, 25, 26, 27],
        )
        self.assertTrue(all(record.get("client") == 2 for record in second[:-1]))
        self.assertTrue(all(
            record.get("spawn_generation") == 22 for record in second[:-1]
        ))
        self.assertTrue(all(record.get("frame") == 31 for record in second))
        self.assertEqual(
            [record["order"] for record in first], [1, 2, 3, 4, 5, 6, 7]
        )
        self.assertEqual(
            [record["order"] for record in incomplete], [8, 9, 10, 11, 12]
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
            assembled = humantrace.read_sessions(files[1])
            assembled_from_directory = humantrace.read_sessions(
                Path(temporary)
            )
            forged_directory = Path(temporary) / "forged"
            forged_directory.mkdir()
            forged = forged_directory / files[1].name
            forged_records = [dict(record) for record in records[1]]
            forged_records[0]["session"] = 2
            write_rechained_v3(forged, forged_records)
            with self.assertRaisesRegex(ValueError, "invalid zero anchor"):
                humantrace.read_sessions(forged)

        self.assertEqual(len(records), 2)
        self.assertEqual(records[1][0]["session"], 1)
        self.assertEqual(assembled[0]["initial_trace_header"]["session"], 1)
        self.assertEqual(assembled[0]["initial_trace_header"]["segment"], 1)
        self.assertEqual(
            assembled_from_directory[0]["initial_trace_header"]["session"], 1
        )

    def test_directory_skips_an_unauthenticated_v3_collision(self) -> None:
        ensure_binary()
        with tempfile.TemporaryDirectory() as temporary:
            subprocess.run(
                [str(BINARY), temporary, "collision"], cwd=ROOT,
                check=True, stdout=subprocess.DEVNULL,
            )
            directory = Path(temporary)
            collision = directory / f"{PREFIX}000000.jsonl"
            collision.write_text(
                '{"format":"lmctf-human-trace-v3","kind":"header"}\n',
                encoding="utf-8",
            )
            sessions = humantrace.read_sessions(directory)

        self.assertEqual(sessions[0]["initial_trace_header"]["session"], 1)

    def test_variable_width_canonical_segment_suffixes_and_cost_sentinel(self) -> None:
        ensure_binary()
        with tempfile.TemporaryDirectory() as temporary:
            completed = subprocess.run(
                [str(BINARY), temporary, "segment-names"], cwd=ROOT,
                text=True, capture_output=True,
            )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(
            humantrace._v3_filename_segment(
                Path(f"{PREFIX}999999.jsonl")
            ),
            999999,
        )
        self.assertEqual(
            humantrace._v3_filename_segment(
                Path(f"{PREFIX}1000000.jsonl")
            ),
            1000000,
        )
        self.assertEqual(
            humantrace._v3_filename_segment(
                Path(f"{PREFIX}000001.jsonl")
            ),
            1,
        )
        self.assertIsNone(
            humantrace._v3_filename_segment(
                Path(f"{PREFIX}0.jsonl")
            )
        )
        self.assertIsNone(
            humantrace._v3_filename_segment(
                Path(f"{PREFIX}0000000.jsonl")
            )
        )
        self.assertEqual(humantrace.EFFECTIVE_COST_MAX,
                         humantrace.UINT64_MAX - 1)
        scope = {"client": 1, "spawn_generation": 1}
        session = {
            "trace_header": {"server_frame_ms": 2},
            "end": {"order": 7},
            "steps": [
                {"kind": "step", "order": 1, "frame": 0, "command": 1,
                 "ground": -1, "after": {"origin": [0, 0, 0]}, **scope},
                {"kind": "step", "order": 4, "frame": 1, "command": 2,
                 "ground": -1, "after": {"origin": [0, 0, 0]}, **scope},
                {"kind": "step", "order": 6,
                 "frame": humantrace.UINT32_MAX, "command": 3,
                 "ground": 0, "after": {"origin": [1, 2, 3]}, **scope},
            ],
            "hook_events": [
                {"kind": "hook-fire", "order": 2, "frame": 0,
                 "after_command": 1, "hook": 2, "hook_event": 1, **scope},
                {"kind": "hook-attach", "order": 3, "frame": 0,
                 "after_command": 1, "hook": 2, "hook_event": 2, **scope},
                {"kind": "hook-release", "order": 5, "frame": 1,
                 "after_command": 2, "hook": 2, "hook_event": 3, **scope},
            ],
        }
        observations = humantrace.derive_v3_learning_observations(
            session, 1, 1,
        )
        cost = next(item for item in observations
                    if item["kind"] == "hook-cost")["effective_cost_ms"]
        self.assertEqual(cost, humantrace.UINT32_MAX * 2)

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
            assembled = humantrace.read_sessions(
                directory / f"{PREFIX}000001.jsonl"
            )
            isolated = directory / "standalone"
            isolated.mkdir()
            continuation = directory / f"{PREFIX}000001.jsonl"
            copied = isolated / continuation.name
            copied.write_bytes(continuation.read_bytes())
            sessions = [first, second]

            with self.assertRaisesRegex(ValueError, "zero-rooted"):
                humantrace.read_sessions(copied)

        self.assertEqual(sessions[0][0]["gravity_bits"], 0x44480000)
        self.assertEqual(sessions[1][0]["gravity_bits"], 0x42C80000)
        self.assertEqual(sessions[1][0]["continuation"], 1)
        self.assertEqual(sessions[1][0]["session"], 0)
        self.assertEqual(sessions[1][0]["start_order"], 2)
        self.assertEqual(len(assembled), 1)
        self.assertEqual(assembled[0]["terminal_sha256"], second[-1]["sha256"])
        self.assertEqual(
            [path.name for path in assembled[0]["source_paths"]],
            [f"{PREFIX}000000.jsonl", f"{PREFIX}000001.jsonl"],
        )

    def test_segment_chain_crosses_six_digit_filename_boundary(self) -> None:
        ensure_binary()
        with tempfile.TemporaryDirectory() as temporary:
            subprocess.run(
                [str(BINARY), temporary, "physics"], cwd=ROOT,
                check=True, stdout=subprocess.DEVNULL,
            )
            directory = Path(temporary)
            old_root = directory / f"{PREFIX}000000.jsonl"
            old_continuation = directory / f"{PREFIX}000001.jsonl"
            root_records = read_valid_prefix(old_root)
            continuation_records = read_valid_prefix(
                old_continuation, str(root_records[-1]["sha256"]),
            )
            root_records[0]["session"] = 999999
            root_records[0]["segment"] = 999999
            continuation_records[0]["session"] = 999999
            continuation_records[0]["segment"] = 1000000
            old_root.unlink()
            old_continuation.unlink()
            root_path = directory / f"{PREFIX}999999.jsonl"
            continuation_path = directory / f"{PREFIX}1000000.jsonl"
            rewritten_root = write_rechained_v3(root_path, root_records)
            write_rechained_v3(
                continuation_path,
                continuation_records,
                str(rewritten_root[-1]["sha256"]),
            )
            assembled = humantrace.read_sessions(directory)

        self.assertEqual(
            [path.name for path in assembled[0]["source_paths"]],
            [f"{PREFIX}999999.jsonl", f"{PREFIX}1000000.jsonl"],
        )

    def test_durable_spools_reject_tampering_and_keep_root_fifo(self) -> None:
        ensure_binary()
        for mode in (
                "spool-order", "spool-truncated", "spool-tampered",
                "ack-truncated", "ack-tampered", "spool-quarantine"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temporary:
                completed = subprocess.run(
                    [str(BINARY), temporary, mode], cwd=ROOT,
                    text=True, capture_output=True,
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_streaming_spool_has_no_historical_hook_event_cap(self) -> None:
        ensure_binary()
        with tempfile.TemporaryDirectory() as temporary:
            completed = subprocess.run(
                [str(BINARY), temporary, "long-stream"], cwd=ROOT,
                text=True, capture_output=True,
            )

        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_learning_host_reuses_one_cursor_and_retries_receipts(self) -> None:
        ensure_binary()
        for mode in ("host-sequential", "host-receipt-failure"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temporary:
                completed = subprocess.run(
                    [str(BINARY), temporary, mode], cwd=ROOT,
                    text=True, capture_output=True,
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_receipts_deny_absent_and_typo_scopes(self) -> None:
        ensure_binary()
        with tempfile.TemporaryDirectory() as temporary:
            completed = subprocess.run(
                [str(BINARY), temporary, "accepted-scope-denial"], cwd=ROOT,
                text=True, capture_output=True,
            )
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_learning_host_commits_scopes_by_first_occurrence(self) -> None:
        ensure_binary()
        with tempfile.TemporaryDirectory() as temporary:
            completed = subprocess.run(
                [str(BINARY), temporary, "host-first-occurrence"], cwd=ROOT,
                text=True, capture_output=True,
            )
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_learning_host_stream_visit_count_is_linear(self) -> None:
        ensure_binary()
        with tempfile.TemporaryDirectory() as temporary:
            completed = subprocess.run(
                [str(BINARY), temporary, "host-linearity"], cwd=ROOT,
                text=True, capture_output=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)

    @unittest.skipUnless(shutil.which("strace"), "strace is required")
    def test_authenticated_collection_opens_each_rotated_file_once(self) -> None:
        binary = ensure_io_binary()
        measurements: list[tuple[int, int]] = []
        for count in (64, 128):
            with self.subTest(count=count), tempfile.TemporaryDirectory() as temporary:
                trace = Path(temporary) / "syscalls.txt"
                completed = subprocess.run(
                    ["strace", "-qq", "-e", "trace=openat,getdents64",
                     "-o", str(trace), str(binary), temporary,
                     f"host-io-{count}"],
                    cwd=ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)
                evidence = sorted(
                    path for path in Path(temporary).iterdir()
                    if path.suffix in (".jsonl", ".learning")
                )
                self.assertGreater(
                    sum(path.suffix == ".jsonl" for path in evidence), 1
                )
                syscalls = trace.read_text(encoding="utf-8")
                read_opens = 0
                for path in evidence:
                    occurrences = sum(
                        str(path) in line and "O_RDONLY" in line
                        and "O_DIRECTORY" not in line
                        for line in syscalls.splitlines()
                    )
                    self.assertEqual(occurrences, 1, path.name)
                    read_opens += occurrences
                directory_reads = len(re.findall(r"^getdents64\(", syscalls,
                                                   flags=re.MULTILINE))
                # Each scan issues one data read and one EOF read. This mode
                # scans once before capture and once after.
                self.assertLessEqual(directory_reads, 4)
                measurements.append((len(evidence), read_opens))
        self.assertLessEqual(measurements[1][0], measurements[0][0] * 2 + 2)
        self.assertEqual(measurements[0][0], measurements[0][1])
        self.assertEqual(measurements[1][0], measurements[1][1])

    @unittest.skipUnless(shutil.which("strace"), "strace is required")
    def test_authenticated_collection_opens_each_multi_root_file_once(self) -> None:
        binary = ensure_io_binary()
        with tempfile.TemporaryDirectory() as temporary:
            trace = Path(temporary) / "syscalls.txt"
            completed = subprocess.run(
                ["strace", "-qq", "-e", "trace=openat,getdents64",
                 "-o", str(trace), str(binary), temporary, "host-io-roots"],
                cwd=ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            evidence = sorted(
                path for path in Path(temporary).iterdir()
                if path.suffix in (".jsonl", ".learning")
            )
            self.assertEqual(
                sum(path.suffix == ".learning" for path in evidence), 8
            )
            self.assertGreater(
                sum(path.suffix == ".jsonl" for path in evidence), 8
            )
            syscalls = trace.read_text(encoding="utf-8")
            for path in evidence:
                occurrences = sum(
                    str(path) in line and "O_RDONLY" in line
                    and "O_DIRECTORY" not in line
                    for line in syscalls.splitlines()
                )
                self.assertEqual(occurrences, 1, path.name)
            directory_reads = len(re.findall(r"^getdents64\(", syscalls,
                                               flags=re.MULTILINE))
            self.assertLessEqual(directory_reads, 4)

    def test_learning_host_accepts_16385_event_postmatch(self) -> None:
        ensure_binary()
        with tempfile.TemporaryDirectory() as temporary:
            completed = subprocess.run(
                [str(BINARY), temporary, "host-long-postmatch"], cwd=ROOT,
                text=True, capture_output=True,
            )
        self.assertEqual(completed.returncode, 0, completed.stderr)

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
