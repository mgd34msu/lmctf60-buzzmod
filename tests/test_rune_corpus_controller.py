#!/usr/bin/env python3
"""Focused, fake-only tests for the durable RUNE corpus controller."""

from __future__ import annotations

import dataclasses
import contextlib
import io
import json
import mmap
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock
from typing import Mapping

from tools import build_python_runtime
from tools import lmctf58_rune_accept, runelint
from tools import rune_corpus_controller as controller
from tests.test_rune_artifact import _build_rune


ROOT = Path(__file__).resolve().parents[1]


def small_python_elf(loader_name: str) -> bytes:
    """Return a minimal ELF64 fixture whose PT_INTERP names ``loader_name``."""
    interpreter = f"/lib64/{loader_name}".encode("ascii") + b"\0"
    elf_header = b"\x7fELF\x02\x01\x01" + b"\0" * 9 + struct.pack(
        "<HHIQQQIHHHHHH", 3, 62, 1, 0, 64, 0, 0, 64, 56, 1, 0, 0, 0,
    )
    program_header = struct.pack(
        "<IIQQQQQQ", 3, 4, 120, 0, 0, len(interpreter), len(interpreter), 1,
    )
    return elf_header + program_header + interpreter


def report(map_name: str, *, seeds: int = 7, links: int = 9,
           nodes: int = 4, triggers: int = 2, inventory: int = 3,
           plan_edges: int = 6, plans: int = 5) -> bytes:
    return controller.canonical_json({
        "edge_count": inventory + plan_edges,
        "inventory_edge_count": inventory,
        "link_count": links,
        "map_name": map_name,
        "node_count": nodes,
        "plan_count": plans,
        "plan_edge_count": plan_edges,
        "route_contract": "complete",
        "seed_count": seeds,
        "trigger_count": triggers,
    })


def bootstrap_snag_text(
    map_name: str, artifact_sha256: str, evidence_sha256: str
) -> str:
    """Return canonical-shaped bootstrap bytes for fake controller tests."""
    return "\n".join((
        "snag_format 2",
        f"map {map_name}",
        "bsp_checksum 1",
        "entity_crc 2",
        "physics_flags 0",
        "gravity 800",
        "airaccelerate 0",
        "maxvelocity 2000",
        "pmove_ms 25",
        "frame_ms 100",
        "host_physics_id 1",
        "rune_payload_crc 3",
        "rune_header_crc 4",
        "rune_action_contract_crc 5",
        "rune_mechanism_contract_crc 6",
        "rune_num_seeds 7",
        "rune_num_links 9",
        f"rune_sha256 {artifact_sha256}",
        f"evidence_sha256 {evidence_sha256}",
        "repairs 0",
    )) + "\n"


class FakeGateRunner:
    def __init__(
        self, map_name: str, *, python_seeds: int | None = None,
        load_libzstd: bool = True,
    ):
        self.map_name = map_name
        self.python_seeds = python_seeds
        self.load_libzstd = load_libzstd
        self.commands: list[list[str]] = []
        self.kwargs: list[dict] = []

    def __call__(self, command, **kwargs):
        command = list(command)
        self.commands.append(command)
        self.kwargs.append(kwargs)
        if command[-1] == controller.PYTHON_RUNTIME_PROBE:
            runtime = Path(kwargs["cwd"])
            version = Path(command[4]).name.removeprefix("python")
            output = controller.canonical_json({
                "executable": str(Path(command[4]).resolve(strict=True)),
                "prefix": str(runtime.resolve(strict=True)),
                "base_prefix": str(runtime.resolve(strict=True)),
                "modules": {
                    "json": str((runtime / f"lib/python{version}/json/__init__.py").resolve(strict=True)),
                    "runpy": str((runtime / f"lib/python{version}/runpy.py").resolve(strict=True)),
                    "encodings": str((runtime / f"lib/python{version}/encodings/__init__.py").resolve(strict=True)),
                    "_json": str((runtime / f"lib/python{version}/lib-dynload/_json.so").resolve(strict=True)),
                    "hashlib": str((runtime / f"lib/python{version}/hashlib.py").resolve(strict=True)),
                    "_hashlib": str((runtime / f"lib/python{version}/lib-dynload/_hashlib.so").resolve(strict=True)),
                    "math": str((runtime / f"lib/python{version}/lib-dynload/math.so").resolve(strict=True)),
                    "zlib": str((runtime / f"lib/python{version}/lib-dynload/zlib.so").resolve(strict=True)),
                    "struct": str((runtime / f"lib/python{version}/struct.py").resolve(strict=True)),
                    "_struct": str((runtime / f"lib/python{version}/lib-dynload/_struct.so").resolve(strict=True)),
                },
                "loaded_libraries": sorted(str(path.resolve(strict=True)) for path in (
                    runtime / f"lib/libpython{version}.so.1.0",
                    runtime / f"lib/python{version}/lib-dynload/_json.so",
                    runtime / f"lib/python{version}/lib-dynload/_hashlib.so",
                    runtime / f"lib/python{version}/lib-dynload/math.so",
                    runtime / f"lib/python{version}/lib-dynload/zlib.so",
                    runtime / f"lib/python{version}/lib-dynload/_struct.so",
                    runtime / f"lib/python{version}/lib-dynload/_ctypes.so",
                    runtime / f"lib/python{version}/lib-dynload/_bz2.so",
                    runtime / f"lib/python{version}/lib-dynload/_lzma.so",
                    runtime / f"lib/python{version}/lib-dynload/_socket.so",
                    runtime / f"lib/python{version}/lib-dynload/array.so",
                    runtime / f"lib/python{version}/lib-dynload/fcntl.so",
                    runtime / f"lib/python{version}/lib-dynload/select.so",
                    runtime / "lib/libcrypto.so.3",
                    runtime / "lib/libbz2.so.1",
                    runtime / "lib/libffi.so.8",
                    runtime / "lib/liblzma.so.5",
                    runtime / "lib/libz.so.1",
                    *((runtime / "lib/libzstd.so.1",) if self.load_libzstd else ()),
                )),
                "sys_path": [str((runtime / f"lib/python{version}").resolve(strict=True))],
                "dont_write_bytecode": True,
            })
            return subprocess.CompletedProcess(command, 0, stdout=output)
        script = next(
            (Path(value).name for value in command if value.endswith(".py")), ""
        ) if "-c" in command else ""
        if script == "runeio.py":
            output = report(self.map_name, seeds=self.python_seeds or 7)
        elif script == "runelint.py":
            output = b""
        elif script.endswith("_rune_accept.py"):
            output = controller.canonical_json({"map_name": self.map_name})
        else:
            output = report(self.map_name)
        return subprocess.CompletedProcess(command, 0, stdout=output)


class RuneCorpusControllerTests(unittest.TestCase):
    def test_external_attempt_authorities_are_mutually_exclusive(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary) / "run"
            first = run_root / "runs/lmctf01/attempt-0001"
            first.mkdir(parents=True)
            controller.atomic_write_json(first / "intent.json", {"intent": 1})
            controller.atomic_write_json(first / "result.json", {"result": 1})
            controller._publish_attempt_abort(run_root, "lmctf01", first, 1)
            with self.assertRaisesRegex(controller.CorpusError, "conflicts"):
                controller._publish_attempt_commit(run_root, "lmctf01", first, 1)

            second = run_root / "runs/lmctf01/attempt-0002"
            second.mkdir()
            controller.atomic_write_json(second / "intent.json", {"intent": 2})
            controller.atomic_write_json(second / "result.json", {"result": 2})
            controller._publish_attempt_commit(run_root, "lmctf01", second, 2)
            with self.assertRaisesRegex(controller.CorpusError, "conflicts"):
                controller._publish_attempt_abort(run_root, "lmctf01", second, 2)

            abort = controller._attempt_abort_path(run_root, "lmctf01", 2)
            controller.atomic_write_json(abort, {
                "format": controller.ATTEMPT_ABORT_FORMAT,
                "attempt": 2,
                "intent": controller._relative_evidence_record(
                    second / "intent.json", run_root
                ),
                "pending_result": controller._relative_evidence_record(
                    second / "result.json", run_root
                ),
            }, mode=0o444)
            with self.assertRaisesRegex(controller.CorpusError, "conflicts"):
                controller._validate_attempt_commit(run_root, "lmctf01", second, 2)
            with self.assertRaisesRegex(controller.CorpusError, "conflicts"):
                controller._validate_attempt_abort(run_root, "lmctf01", second, 2)

    def test_real_runeaccept_exit_taxonomy(self) -> None:
        """The production C reader reserves rc 1 for bad bytes, not I/O."""
        subprocess.run(
            ["make", "-B", "runeaccept.gnu"], cwd=ROOT, check=True,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            valid = work / "valid.rune"
            valid.write_bytes(_build_rune())
            truncated = work / "truncated.rune"
            truncated.write_bytes(valid.read_bytes()[:-1])
            trailing = work / "trailing.rune"
            trailing.write_bytes(valid.read_bytes() + b"x")
            hardlink = work / "hardlink.rune"
            symlink = work / "symlink.rune"
            real_parent = work / "real-parent"
            parent_symlink = work / "parent-symlink"
            os.link(valid, hardlink)
            symlink.symlink_to(valid)
            real_parent.mkdir()
            os.link(valid, real_parent / "valid.rune")
            parent_symlink.symlink_to(real_parent, target_is_directory=True)
            cases = (
                (["runeaccept.gnu", valid], 0),
                (["runeaccept.gnu", hardlink], 0),
                (["runeaccept.gnu", symlink], 3),
                (["runeaccept.gnu", parent_symlink / "valid.rune"], 3),
                (["runeaccept.gnu", truncated], 1),
                (["runeaccept.gnu", trailing], 1),
                (["runeaccept.gnu", work / "missing.rune"], 3),
                (["runeaccept.gnu", work], 3),
                (["runeaccept.gnu"], 2),
            )
            for command, expected in cases:
                with self.subTest(command=command):
                    completed = subprocess.run(
                        [os.fspath(ROOT / command[0]),
                         *map(os.fspath, command[1:])],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    )
                    self.assertEqual(completed.returncode, expected)

    def make_snapshot(
        self,
        parent: Path,
        *,
        action_hash: bytes = b"a",
        runtime_json: bytes = b"private json",
        runtime_source: Path | None = None,
        runtime_native_fixture: tuple[str, bytes] | None = None,
        runtime_omissions: frozenset[str] = frozenset(),
        actual_tools: bool = False,
        acceptor_output: bytes | None = None,
        acceptor_action_hash: bytes | None = None,
        adopted_runes: Mapping[str, bytes] | None = None,
    ) -> Path:
        parent.mkdir(parents=True, exist_ok=True)
        sources = parent / "sources"
        sources.mkdir()
        files: dict[str, tuple[str, Path]] = {}

        def add(logical: str, role: str, data: bytes, mode: int = 0o644):
            source = sources / logical.replace("/", "_")
            source.write_bytes(data)
            source.chmod(mode)
            files[logical] = (role, source)

        add("bin/q2ded", "engine", b"#!/bin/sh\nexit 0\n", 0o755)
        runtime = runtime_source or sources / "python-runtime-source"
        version = f"{os.sys.version_info.major}.{os.sys.version_info.minor}"
        if runtime_native_fixture is None:
            loader = next(
                Path(line.rsplit(maxsplit=1)[-1]).resolve(strict=True)
                for line in Path("/proc/self/maps").read_text().splitlines()
                if "/ld-linux-" in line and line.rsplit(maxsplit=1)[-1].startswith("/")
            )
            interpreter_data = Path(os.sys.executable).resolve(strict=True).read_bytes()
            loader_data = loader.read_bytes()
        else:
            loader_name, interpreter_data = runtime_native_fixture
            loader = Path("/fixture") / loader_name
            loader_data = interpreter_data
        runtime_files = {
            f"bin/python{version}": interpreter_data,
            f"lib/{loader.name}": loader_data,
            f"lib/libpython{version}.so.1.0": b"private libpython",
            f"lib/python{version}/json/__init__.py": runtime_json,
            f"lib/python{version}/runpy.py": b"private runpy",
            f"lib/python{version}/encodings/__init__.py": b"private encodings",
            f"lib/python{version}/hashlib.py": b"private hashlib",
            f"lib/python{version}/struct.py": b"private struct",
            f"lib/python{version}/lib-dynload/_json.so": b"private _json",
            f"lib/python{version}/lib-dynload/_hashlib.so": b"private _hashlib",
            f"lib/python{version}/lib-dynload/math.so": b"private math",
            f"lib/python{version}/lib-dynload/zlib.so": b"private zlib extension",
            f"lib/python{version}/lib-dynload/_struct.so": b"private _struct",
            f"lib/python{version}/lib-dynload/_ctypes.so": b"private _ctypes",
            f"lib/python{version}/lib-dynload/_bz2.so": b"private _bz2",
            f"lib/python{version}/lib-dynload/_lzma.so": b"private _lzma",
            f"lib/python{version}/lib-dynload/_socket.so": b"private _socket",
            f"lib/python{version}/lib-dynload/array.so": b"private array",
            f"lib/python{version}/lib-dynload/fcntl.so": b"private fcntl",
            f"lib/python{version}/lib-dynload/select.so": b"private select",
            "lib/libcrypto.so.3": b"private libcrypto",
            "lib/libbz2.so.1": b"private libbz2",
            "lib/libffi.so.8": b"private libffi",
            "lib/liblzma.so.5": b"private liblzma",
            "lib/libz.so.1": b"private libz",
            "lib/libzstd.so.1": b"private libzstd",
        }
        if runtime_source is None:
            for relative, data in runtime_files.items():
                if relative in runtime_omissions:
                    continue
                path = runtime / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                if ".so" in path.name and not data.startswith(b"\x7fELF"):
                    data = b"\x7fELF" + data
                path.write_bytes(data)
                path.chmod(0o755 if relative.startswith("bin/") else 0o644)
        files["python-runtime"] = ("python_runtime", runtime)
        add("game/game.so", "module_primary", b"one-current-module")
        add("game/gamex86_64.so", "module_secondary", b"one-current-module")
        gate_report = (acceptor_output or report("lmctf01")).rstrip()
        if actual_tools:
            files["tools/runelint.py"] = ("runelint", ROOT / "tools/runelint.py")
            files["tools/runeio.py"] = ("runeio", ROOT / "tools/runeio.py")
            files["tools/snagrepair.py"] = (
                "snagrepair", ROOT / "tools/snagrepair.py",
            )
            files["tools/rune_contracts_generated.py"] = (
                "contracts", ROOT / "tools/rune_contracts_generated.py",
            )
            files["tools/lmctf58_rune_accept.py"] = (
                "semantic_checker:lmctf58",
                ROOT / "tools/lmctf58_rune_accept.py",
            )
        else:
            add("tools/runelint.py", "runelint", b"raise SystemExit(0)\n")
            add("tools/runeio.py", "runeio", b"def main(argv):\n print(" + repr(gate_report.decode()).encode() + b")\n return 0\n")
            add(
                "tools/snagrepair.py", "snagrepair",
                b"def main(argv):\n"
                b" import pathlib\n"
                b" target=pathlib.Path(argv[argv.index('--output')+1])\n"
                b" target.write_text('fake snag\\n',encoding='ascii')\n"
                b" return 0\n",
            )
            add(
                "tools/rune_contracts_generated.py",
                "contracts",
                b"RUNE_ACTION_CONTRACT_CRC32 = 0xe1731b32\n"
                b"RUNE_ACTION_CONTRACT_SHA256 = '" + action_hash * 64 + b"'\n"
                b"RUNE_MECHANISM_CONTRACT_CRC32 = 0x509691fa\n"
                b"RUNE_MECHANISM_CONTRACT_SHA256 = '" + b"b" * 64 + b"'\n",
            )
            add(
                "tools/lmctf58_rune_accept.py", "semantic_checker:lmctf58",
                b"def main(argv):\n print('{\"map_name\":\"lmctf58\"}')\n return 0\n",
            )
        if actual_tools:
            action_text, mechanism_text = controller._contract_hashes(
                ROOT / "tools/rune_contracts_generated.py"
            )
            acceptor_hash = action_text.encode("ascii")
            acceptor_mechanism_hash = mechanism_text.encode("ascii")
        else:
            acceptor_hash = (acceptor_action_hash or action_hash) * 64
            acceptor_mechanism_hash = b"b" * 64
        acceptor_script = (
            b"#!/bin/sh\n"
            b"if [ \"$1\" = --contracts ]; then\n"
            b" printf '%s\\n' '{\"action_contract_sha256\":\""
            + acceptor_hash
            + b"\",\"mechanism_contract_sha256\":\""
            + acceptor_mechanism_hash
            + b"\"}'\n"
            b"else\n printf '%s\\n' '" + gate_report + b"'\nfi\n"
        )
        add("runeaccept.gnu", "acceptor_gnu", acceptor_script, 0o755)
        add("runeaccept.make", "acceptor_make", acceptor_script, 0o755)
        add("game/rune.cfg", "generator_config", b"set dedicated 1\n")
        add(
            "tools/rune-semantic-checkers.json", "semantic_checker_manifest",
            (ROOT / "tools/rune-semantic-checkers.json").read_bytes(),
        )
        manifest_copy = sources / "rune-corpus-maps.txt"
        manifest_copy.write_bytes((ROOT / "tools/rune-corpus-maps.txt").read_bytes())
        files["tools/rune-corpus-maps.txt"] = ("map_manifest", manifest_copy)
        for map_name in controller.validate_manifest():
            add(f"assets/{map_name}.bsp", f"asset:{map_name}", map_name.encode("ascii"))
        for map_name, data in sorted((adopted_runes or {}).items()):
            add(
                f"adopted-runes/{map_name}.rune", f"adopted_rune:{map_name}", data
            )
        snapshot = parent / "snapshot"
        controller.create_input_snapshot(snapshot, files)
        return snapshot

    def thaw(self, path: Path) -> None:
        for directory, names, files in os.walk(path):
            Path(directory).chmod(0o700)
            for name in names + files:
                Path(directory, name).chmod(0o700)

    def test_linux_private_runtime_handshake_uses_private_loader_and_manifest_maps(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            runtime = build_python_runtime.build_runtime(work / "python-runtime")
            artifact = work / "runetest.rune"
            artifact.write_bytes(_build_rune())
            expected_report = subprocess.run(
                [os.fspath(Path(os.sys.executable)), os.fspath(ROOT / "tools/runeio.py"), os.fspath(artifact)],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True,
            ).stdout
            snapshot = self.make_snapshot(
                work / "snapshot-input", runtime_source=runtime, actual_tools=True,
                acceptor_output=expected_report,
            )
            verified = controller.verify_snapshot(snapshot)
            target = snapshot / verified["by_role"]["runeio"]["path"]
            captured = []
            original = controller._validate_verified_python_process

            def observe(*args):
                observed = original(*args)
                captured.append(observed[0])
                return observed

            with mock.patch.object(controller, "_validate_verified_python_process", side_effect=observe):
                try:
                    preflight = controller.preflight_python_runtime(snapshot)
                except controller.ProcessIntegrityError as exc:
                    self.assertEqual(
                        "cannot authenticate verified Python mapped backing", str(exc)
                    )
                    self.thaw(snapshot)
                    return
                rc, output, lifecycle = controller.run_verified_python(
                    snapshot, verified["python_runtime"], "integration", target, [str(artifact)]
                )
                gates = controller.run_gates(
                    artifact, "runetest",
                    {"seeds": 2, "links": 2, "mechanism_nodes": 3, "triggers": 2,
                     "inventory_edges": 1, "plans": 1},
                    acceptor_gnu=snapshot / verified["by_role"]["acceptor_gnu"]["path"],
                    acceptor_make=snapshot / verified["by_role"]["acceptor_make"]["path"],
                    python_interpreter=snapshot / verified["python_runtime"]["interpreter"]["path"],
                    runeio=target,
                    runelint=snapshot / verified["by_role"]["runelint"]["path"],
                    log_directory=work / "evidence",
                    fingerprint="integration-fingerprint",
                )
            self.assertIn("lifecycle", preflight)
            self.assertEqual(0, rc)
            self.assertEqual("READY", lifecycle["ready"]["phase"])
            self.assertEqual("DONE", lifecycle["done"]["phase"])
            self.assertEqual(json.loads(expected_report), json.loads(output))
            self.assertEqual(json.loads(expected_report)["seed_count"], gates["decoded_counts"]["seed_count"])
            for label in ("python", "lint"):
                integrity = json.loads((work / "evidence" / f"gate-{label}.integrity.json").read_text())
                self.assertEqual("integration-fingerprint", integrity["fingerprint"])
                self.assertEqual("READY", integrity["ready"]["phase"])
                self.assertEqual("DONE", integrity["done"]["phase"])
            self.assertGreaterEqual(len(captured), 8)
            self.assertTrue(all(
                Path(identity.executable) == (snapshot / verified["python_runtime"]["loader"]["path"]).resolve()
                for identity in captured
            ))
            self.thaw(snapshot)

    def test_manifest_and_all_175_stable_assignments(self):
        maps = controller.validate_manifest()
        assignments = controller.stable_assignments(maps)
        self.assertEqual(175, len(maps))
        self.assertEqual(175, len(assignments))
        self.assertEqual(list(range(175)), [item["index"] for item in assignments])
        self.assertEqual(list(range(62000, 62175)), [item["port"] for item in assignments])
        self.assertEqual(maps, [item["map"] for item in assignments])
        self.assertIn("lmctf02a", maps)
        self.assertIn("lmctf02c", maps)

    def test_manifest_variant_policy_excludes_unsuffixed_bases(self):
        maps = controller.validate_manifest()
        controller.validate_variant_scope(maps)
        excluded = {"lmctf02", "lmctf05", "smap31", "xmap07", "xmap11", "xmap14"}
        retained = {
            "lmctf02a", "lmctf02c", "lmctf05b", "smap31a", "xmap07a",
            "xmap11a", "xmap14a",
        }
        self.assertTrue(excluded.isdisjoint(maps))
        self.assertTrue(retained.issubset(maps))
        with self.assertRaisesRegex(
            controller.CorpusError,
            "unsuffixed map lmctf02 conflicts with variants lmctf02a, lmctf02c",
        ):
            controller.validate_variant_scope(
                ["lmctf01", "lmctf02", "lmctf02a", "lmctf02c"]
            )
        with self.assertRaisesRegex(
            controller.CorpusError,
            "unsuffixed map tw2ctf3 conflicts with variants tw2ctf3a",
        ):
            controller.validate_variant_scope(["tw2ctf3", "tw2ctf3a"])

    @unittest.skipUnless(sys.platform.startswith("linux"), "requires Linux process_vm_readv")
    def test_mapped_object_identity_outlives_named_path_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            mapped = work / "mapped-runtime"
            replacement = work / "replacement-runtime"
            restore = work / "restore-runtime"
            mapped.write_bytes(b"mapped runtime bytes")
            record = controller.regular_file_record(mapped)
            descriptor = os.open(mapped, os.O_RDONLY)
            record["descriptor"] = descriptor
            view = mmap.mmap(
                descriptor, 0, flags=mmap.MAP_PRIVATE,
                prot=mmap.PROT_READ | mmap.PROT_EXEC,
            )
            try:
                maps = Path("/proc/self/maps").read_text(encoding="utf-8")
                fields = next(
                    line.split()
                    for line in maps.splitlines()
                    if line.endswith(str(mapped))
                )
                address_range = fields[0]
                replacement.write_bytes(b"attacker replacement")
                os.replace(replacement, mapped)
                controller._validate_mapped_object(
                    os.getpid(), address_range, int(fields[2], 16), fields[1], record
                )
                attacker_fd = os.open(mapped, os.O_RDONLY)
                attacker_view = mmap.mmap(
                    attacker_fd, 0, flags=mmap.MAP_PRIVATE,
                    prot=mmap.PROT_READ | mmap.PROT_EXEC,
                )
                try:
                    maps = Path("/proc/self/maps").read_text(encoding="utf-8")
                    attacker_fields = next(
                        line.split()
                        for line in maps.splitlines()
                        if line.endswith(str(mapped))
                    )
                    attacker_range = attacker_fields[0]
                    restore.write_bytes(b"mapped runtime bytes")
                    os.replace(restore, mapped)
                    with self.assertRaisesRegex(
                        controller.ProcessIntegrityError, "mapped object differs"
                    ):
                        controller._validate_mapped_object(
                            os.getpid(), attacker_range,
                            int(attacker_fields[2], 16), attacker_fields[1], record,
                        )
                finally:
                    attacker_view.close()
                    os.close(attacker_fd)
            finally:
                view.close()
                os.close(descriptor)

    @unittest.skipUnless(sys.platform.startswith("linux"), "requires Linux maps")
    def test_readonly_mapped_object_identity_rejects_mismatch(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            trusted = work / "trusted-runtime"
            attacker = work / "attacker-runtime"
            trusted.write_bytes(small_python_elf("good-loader"))
            attacker.write_bytes(small_python_elf("evil-loader"))
            record = controller.regular_file_record(trusted)
            descriptor = os.open(trusted, os.O_RDONLY)
            record["descriptor"] = descriptor
            attacker_fd = os.open(attacker, os.O_RDONLY)
            view = mmap.mmap(
                attacker_fd, 0, flags=mmap.MAP_PRIVATE, prot=mmap.PROT_READ,
            )
            try:
                fields = next(
                    line.split()
                    for line in Path("/proc/self/maps").read_text(encoding="utf-8").splitlines()
                    if line.endswith(str(attacker))
                )
                self.assertNotIn("x", fields[1])
                with self.assertRaisesRegex(
                    controller.ProcessIntegrityError, "mapped object differs"
                ):
                    controller._validate_mapped_object(
                        os.getpid(), fields[0], int(fields[2], 16), fields[1], record,
                    )
            finally:
                view.close()
                os.close(attacker_fd)
                os.close(descriptor)

    @unittest.skipUnless(sys.platform.startswith("linux"), "requires Linux maps")
    def test_readonly_mapped_object_identity_honors_map_offset(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "runtime"
            page = mmap.PAGESIZE
            path.write_bytes(b"A" * page + b"B" * page)
            record = controller.regular_file_record(path)
            descriptor = os.open(path, os.O_RDONLY)
            record["descriptor"] = descriptor
            view = mmap.mmap(
                descriptor, page, flags=mmap.MAP_PRIVATE,
                prot=mmap.PROT_READ, offset=page,
            )
            try:
                fields = next(
                    line.split()
                    for line in Path("/proc/self/maps").read_text(encoding="utf-8").splitlines()
                    if line.endswith(str(path)) and int(line.split()[2], 16) == page
                )
                controller._validate_mapped_object(
                    os.getpid(), fields[0], int(fields[2], 16), fields[1], record,
                )
            finally:
                view.close()
                os.close(descriptor)

    @unittest.skipUnless(sys.platform.startswith("linux"), "requires Linux process_vm_readv")
    def test_process_memory_short_read_fails_closed(self):
        with mock.patch.object(controller, "_PROCESS_VM_READV", return_value=1):
            with self.assertRaisesRegex(
                controller.ProcessIntegrityError, "cannot read verified Python mapped bytes"
            ):
                controller._read_verified_process_memory(os.getpid(), 1, 2)

    def test_elf_writable_file_range_detects_relro_origin(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "runtime"
            page = mmap.PAGESIZE
            program_count = 1025
            program_size = struct.calcsize("<IIQQQQQQ")
            table_end = 64 + program_count * program_size
            segment_offset = (table_end + page - 1) // page * page
            header = struct.pack(
                "<16sHHIQQQIHHHHHH",
                b"\x7fELF\x02\x01\x01" + b"\0" * 9,
                3, 62, 1, 0, 64, 0, 0, 64, program_size, program_count, 0, 0, 0,
            )
            readonly_program = struct.pack(
                "<IIQQQQQQ", 1, 4, 0, 0, 0, 0, 0, page,
            )
            writable_program = struct.pack(
                "<IIQQQQQQ", 1, 6, segment_offset, segment_offset, 0,
                page, page, page,
            )
            path.write_bytes(
                (header + readonly_program * (program_count - 1) + writable_program)
                .ljust(segment_offset + page, b"\0")
            )
            descriptor = os.open(path, os.O_RDONLY)
            try:
                self.assertFalse(controller._elf_writable_file_range(descriptor, 0, page))
                self.assertTrue(
                    controller._elf_writable_file_range(
                        descriptor, segment_offset, page,
                    )
                )
            finally:
                os.close(descriptor)

    @unittest.skipUnless(sys.platform.startswith("linux"), "requires Linux maps")
    def test_writable_mapped_object_requires_backing_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            trusted = work / "trusted-runtime"
            attacker = work / "attacker-runtime"
            trusted.write_bytes(b"trusted runtime bytes")
            attacker.write_bytes(b"attacker runtime bytes")
            record = controller.regular_file_record(trusted)
            descriptor = os.open(trusted, os.O_RDONLY)
            record["descriptor"] = descriptor
            attacker_fd = os.open(attacker, os.O_RDONLY)
            view = mmap.mmap(
                attacker_fd, 0, flags=mmap.MAP_PRIVATE,
                prot=mmap.PROT_READ | mmap.PROT_WRITE,
            )
            try:
                fields = next(
                    line.split()
                    for line in Path("/proc/self/maps").read_text(encoding="utf-8").splitlines()
                    if line.endswith(str(attacker))
                )
                self.assertIn("w", fields[1])
                with self.assertRaisesRegex(
                    controller.ProcessIntegrityError, "mapped backing|cannot authenticate"
                ):
                    controller._validate_mapped_object(
                        os.getpid(), fields[0], int(fields[2], 16), fields[1], record,
                    )
            finally:
                view.close()
                os.close(attacker_fd)
                os.close(descriptor)

    def test_linux_map_preflight_accepts_zero_identity_fields(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot = self.make_snapshot(Path(temporary))
            verified = controller.verify_snapshot(snapshot)
            retained = controller._retained_snapshot_files(snapshot, verified)
            try:
                loader = snapshot / verified["python_runtime"]["loader"]["path"]
                record = retained.paths[str(loader)]
                command = [str(loader)]
                identity = controller.ProcessIdentity(
                    pid=4242, boot_id="00000000-0000-0000-0000-000000000000", start_ticks=12,
                    executable=str(loader), executable_sha256=record["sha256"],
                    cmdline_sha256=controller.sha256_bytes(controller._nul_argv(command)),
                )
                original_read_text = Path.read_text

                def validate(
                    pathname: str, dev: int, inode: int,
                    *, permissions: str = "r-xp", offset: int = 0,
                    same_mount_namespace: bool = True,
                ):
                    line = (
                        f"00400000-00401000 {permissions} {offset:08x} "
                        f"{os.major(dev):02x}:{os.minor(dev):02x} {inode} {pathname}\n"
                    )

                    def read_text(path, *args, **kwargs):
                        if str(path) == "/proc/4242/maps":
                            return line
                        return original_read_text(path, *args, **kwargs)

                    with mock.patch.object(Path, "read_text", new=read_text), \
                            mock.patch.object(controller, "_pidfd_is_live", return_value=True), \
                            mock.patch.object(controller, "_proc_start_ticks", return_value=12), \
                            mock.patch.object(
                                controller, "_same_mount_namespace",
                                return_value=same_mount_namespace,
                            ), \
                            mock.patch.object(
                                controller, "_validate_mapped_object",
                            ), \
                            mock.patch.object(controller, "capture_process_identity", return_value=identity):
                        return controller._validate_verified_python_process(
                            4242, 9, snapshot, verified, retained, command
                        )

                validate(record["canonical_path"], 0, 0)
                with self.assertRaisesRegex(
                    controller.ProcessIntegrityError, "mount namespace"
                ):
                    validate(
                        record["canonical_path"], 0, 0,
                        same_mount_namespace=False,
                    )
                for pseudo in controller.PSEUDO_MAP_ALLOWLIST:
                    validate(pseudo, 0, 0)
                for pathname, label in (
                    (str(Path("/host") / loader.name), "canonical"),
                    (
                        record["canonical_path"] + " (deleted)", "deleted",
                    ),
                    ("[stack:9]", "thread stack"),
                    ("[anon:host]", "named anon"),
                    ("/dev/zero", "devzero"),
                    ("/SYSV00000000", "sysv"),
                ):
                    pattern = "forbidden" if label in (
                        "deleted", "thread stack", "named anon", "devzero", "sysv"
                    ) else "canonical|manifested"
                    with self.assertRaisesRegex(controller.ProcessIntegrityError, pattern):
                        validate(pathname, 0, 0)
            finally:
                retained.close()
                self.thaw(snapshot)

    def test_retention_never_reads_mountinfo(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot = self.make_snapshot(Path(temporary))
            verified = controller.verify_snapshot(snapshot)
            original_read_text = Path.read_text

            def retain_read_text(path, *args, **kwargs):
                if str(path) == "/proc/self/mountinfo":
                    raise AssertionError("retention must not read mountinfo")
                return original_read_text(path, *args, **kwargs)

            with mock.patch.object(Path, "read_text", new=retain_read_text):
                retained = controller._retained_snapshot_files(snapshot, verified)
            try:
                loader = snapshot / verified["python_runtime"]["loader"]["path"]
                record = retained.paths[str(loader.resolve(strict=True))]
                self.assertEqual(str(loader), record["canonical_path"])
                self.assertEqual(
                    verified["python_runtime"]["loader"]["sha256"], record["sha256"]
                )
            finally:
                retained.close()
                self.thaw(snapshot)

    def test_nonlinux_runtime_preflight_stops_before_proc_access(self):
        for platform_name in ("win32", "darwin"):
            with self.subTest(platform=platform_name), \
                    mock.patch.object(controller.sys, "platform", platform_name), \
                    mock.patch.object(
                        controller, "verify_snapshot",
                        side_effect=AssertionError("preflight must stop before snapshot work"),
                    ):
                with self.assertRaisesRegex(controller.CorpusError, "only on Linux"):
                    controller.preflight_python_runtime(Path("/unused"))

    def test_handshake_pipe_deadlines_reject_silent_partial_and_oversized_frames(self):
        for payload, pattern in ((b"", "deadline"), (b"\0\0", "deadline"), (b"\0\1\0\1", "length")):
            control_r, control_w = os.pipe()
            stdout_r, stdout_w = os.pipe()
            try:
                os.set_blocking(control_r, False)
                os.set_blocking(stdout_r, False)
                if payload:
                    os.write(control_w, payload)
                started = time.monotonic()
                with self.assertRaisesRegex(controller.ProcessIntegrityError, pattern):
                    controller._read_frame(
                        control_r, stdout_r, time.monotonic() + 0.03, bytearray(), limit=1024,
                    )
                self.assertLess(time.monotonic() - started, 0.5)
            finally:
                for fd in (control_r, control_w, stdout_r, stdout_w):
                    os.close(fd)

    def test_handshake_stdout_flood_is_bounded_before_any_release(self):
        control_r, control_w = os.pipe()
        stdout_r, stdout_w = os.pipe()
        try:
            os.set_blocking(control_r, False)
            os.set_blocking(stdout_r, False)
            os.write(stdout_w, b"x" * 4096)
            with self.assertRaisesRegex(controller.ProcessIntegrityError, "stdout exceeds"):
                controller._read_frame(
                    control_r, stdout_r, time.monotonic() + 0.2, bytearray(), limit=1024,
                )
        finally:
            for fd in (control_r, control_w, stdout_r, stdout_w):
                os.close(fd)

    def test_retained_manifest_authority_accepts_hardlinked_runtime_input(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            verified = controller.verify_snapshot(snapshot)
            loader = snapshot / verified["python_runtime"]["loader"]["path"]
            os.link(loader, work / "loader-hardlink")
            retained = controller._retained_snapshot_files(
                snapshot, controller.verify_snapshot(snapshot)
            )
            retained.close()
            self.thaw(snapshot)

    def test_retained_manifest_authority_detects_content_mismatch_race(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            verified = controller.verify_snapshot(snapshot)
            retained = controller._retained_snapshot_files(snapshot, verified)
            try:
                loader = snapshot / verified["python_runtime"]["loader"]["path"]
                record = retained.paths[str(loader)]
                replacement = work / "replacement-loader"
                replacement.write_bytes(b"replacement loader bytes")
                replacement.chmod(record["mode"])
                loader.parent.chmod(0o700)
                os.replace(replacement, loader)
                with self.assertRaisesRegex(controller.ProcessIntegrityError, "changed"):
                    controller._validate_retained_path(snapshot, record)
            finally:
                retained.close()
                self.thaw(snapshot)

    def test_manifest_rejects_hash_change_before_name_checks(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "maps.txt"
            path.write_bytes((ROOT / "tools/rune-corpus-maps.txt").read_bytes() + b"bad\n")
            with self.assertRaisesRegex(controller.CorpusError, "hash mismatch"):
                controller.validate_manifest(path)

    def test_manifest_hash_and_parse_share_one_descriptor_during_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            path = work / "maps.txt"
            replacement = work / "replacement.txt"
            path.write_bytes((ROOT / "tools/rune-corpus-maps.txt").read_bytes())
            replacement.write_bytes(b"attacker replacement\n")
            original_open = controller._open_regular
            opened = False

            def replace_after_open(candidate):
                nonlocal opened
                fd, info = original_open(candidate)
                if Path(candidate) == path and not opened:
                    opened = True
                    os.replace(replacement, path)
                return fd, info

            with mock.patch.object(
                controller, "_open_regular", side_effect=replace_after_open
            ), mock.patch.object(
                Path, "read_bytes", side_effect=AssertionError("manifest reopened")
            ):
                maps = controller.validate_manifest(path)
            self.assertTrue(opened)
            self.assertEqual(175, len(maps))
            self.assertEqual(b"attacker replacement\n", path.read_bytes())

    def test_snapshot_rejects_symlink_and_detects_input_mutation(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            target = work / "target"
            target.write_bytes(b"bytes")
            link = work / "link"
            link.symlink_to(target)
            with self.assertRaisesRegex(controller.CorpusError, "regular file|safely open|symlink"):
                controller.regular_file_record(link)

            snapshot = self.make_snapshot(work)
            verified = controller.verify_snapshot(snapshot)
            engine = snapshot / verified["by_role"]["engine"]["path"]
            engine.chmod(0o755)
            engine.write_bytes(b"mutated")
            with self.assertRaisesRegex(controller.CorpusError, "mismatch"):
                controller.verify_snapshot(snapshot)
            self.thaw(snapshot)

    def test_snapshot_completes_partial_regular_file_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            real_write = os.write
            loader_name = "ld-linux-x86-64.so.2"
            native_fixture = small_python_elf(loader_name)
            write_sizes = []

            def partial_write(fd, payload):
                written = real_write(fd, payload[:17])
                write_sizes.append(written)
                return written

            with mock.patch.object(os, "write", side_effect=partial_write):
                snapshot = self.make_snapshot(
                    Path(temporary),
                    runtime_native_fixture=(loader_name, native_fixture),
                )
            controller.verify_snapshot(snapshot)
            self.assertGreater(len(native_fixture), 17)
            self.assertGreater(len(write_sizes), 1)
            self.assertTrue(all(0 < size <= 17 for size in write_sizes))
            self.thaw(snapshot)

    def test_snapshot_rejects_unmanifested_file_and_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            snapshot.chmod(0o700)
            extra_file = snapshot / "unmanifested-input"
            extra_file.write_bytes(b"not frozen")
            extra_file.chmod(0o444)
            extra_directory = snapshot / "unmanifested-directory"
            extra_directory.mkdir(mode=0o555)
            snapshot.chmod(0o555)
            with self.assertRaisesRegex(controller.CorpusError, "file/directory set"):
                controller.verify_snapshot(snapshot)
            self.thaw(snapshot)

    def test_snapshot_rejects_unmanifested_in_tree_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            snapshot.chmod(0o700)
            (snapshot / "unmanifested-link").symlink_to("bin/q2ded")
            snapshot.chmod(0o555)
            with self.assertRaisesRegex(controller.CorpusError, "contains a symlink"):
                controller.verify_snapshot(snapshot)
            self.thaw(snapshot)

    def test_python_runtime_closure_rejects_missing_extra_changed_and_pycache(self):
        version = f"{os.sys.version_info.major}.{os.sys.version_info.minor}"
        relative = Path(f"python-runtime/lib/python{version}/runpy.py")
        for mutation in ("missing", "extra", "changed"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                snapshot = self.make_snapshot(Path(temporary))
                self.thaw(snapshot)
                if mutation == "missing":
                    (snapshot / relative).unlink()
                elif mutation == "extra":
                    extra = snapshot / f"python-runtime/lib/python{version}/extra.py"
                    extra.write_bytes(b"host-selected extra")
                    extra.chmod(0o444)
                else:
                    (snapshot / relative).write_bytes(b"changed host stdlib")
                controller.freeze_tree(snapshot)
                with self.assertRaisesRegex(controller.CorpusError, "mismatch|file/directory set"):
                    controller.verify_snapshot(snapshot)
                self.thaw(snapshot)

        entries = [
            {"path": f"python-runtime/bin/python{version}", "role": f"python_runtime:bin/python{version}", "mode": 0o755},
            {"path": f"python-runtime/lib/libpython{version}.so.1.0", "role": f"python_runtime:lib/libpython{version}.so.1.0", "mode": 0o444},
            {"path": f"python-runtime/lib/python{version}/json/__init__.py", "role": f"python_runtime:lib/python{version}/json/__init__.py", "mode": 0o444},
            {"path": f"python-runtime/lib/python{version}/runpy.py", "role": f"python_runtime:lib/python{version}/runpy.py", "mode": 0o444},
            {"path": f"python-runtime/lib/python{version}/encodings/__init__.py", "role": f"python_runtime:lib/python{version}/encodings/__init__.py", "mode": 0o444},
            {"path": f"python-runtime/lib/python{version}/lib-dynload/_json.so", "role": f"python_runtime:lib/python{version}/lib-dynload/_json.so", "mode": 0o444},
            {"path": f"python-runtime/lib/python{version}/__pycache__/runpy.pyc", "role": f"python_runtime:lib/python{version}/__pycache__/runpy.pyc", "mode": 0o444},
        ]
        with self.assertRaisesRegex(controller.CorpusError, "bytecode cache"):
            controller._python_runtime_layout(entries)

    def test_python_runtime_closure_accepts_build_without_optional_libzstd(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot = self.make_snapshot(
                Path(temporary),
                runtime_omissions=frozenset({"lib/libzstd.so.1"}),
            )
            self.assertNotIn(
                "libzstd", controller.verify_snapshot(snapshot)["python_runtime"]
            )
            controller.preflight_python_runtime(
                snapshot, runner=FakeGateRunner("lmctf01", load_libzstd=False)
            )

    def test_private_runtime_probe_rejects_host_origins_and_ignores_host_env(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            good = FakeGateRunner("lmctf01")
            hostile = {
                "PATH": str(work / "host-bin"),
                "PYTHONPATH": str(work / "host-stdlib"),
                "LD_PRELOAD": str(work / "host-preload.so"),
                "LD_LIBRARY_PATH": str(work / "host-lib"),
                "MAKEFLAGS": "--eval=host", "MFLAGS": "-j99",
                "GNUMAKEFLAGS": "--include-dir=host",
            }
            with mock.patch.dict(os.environ, hostile, clear=False):
                controller.preflight_python_runtime(snapshot, runner=good)
            probe_env = good.kwargs[0]["env"]
            runtime_root = snapshot / "python-runtime"
            self.assertEqual(
                controller.python_child_environment(runtime_root), probe_env
            )
            self.assertNotIn("PATH", probe_env)
            self.assertNotIn("PYTHONPATH", probe_env)
            self.assertNotIn("LD_PRELOAD", probe_env)
            self.assertFalse(any(
                path.name == "__pycache__" or path.suffix == ".pyc"
                for path in snapshot.rglob("*")
            ))

            def host_probe(command, **kwargs):
                completed = good(command, **kwargs)
                value = json.loads(completed.stdout)
                value["modules"]["json"] = "/usr/lib/python-host/json/__init__.py"
                return subprocess.CompletedProcess(
                    command, 0, stdout=controller.canonical_json(value)
                )

            with self.assertRaisesRegex(controller.CorpusError, "escapes snapshot"):
                controller.preflight_python_runtime(snapshot, runner=host_probe)

            hostile_crypto = work / "host/libcrypto.so.3"
            hostile_crypto.parent.mkdir()
            hostile_crypto.write_bytes(b"mutable host crypto")

            def host_dependency_probe(command, **kwargs):
                completed = good(command, **kwargs)
                value = json.loads(completed.stdout)
                value["loaded_libraries"].append(
                    str(hostile_crypto.resolve(strict=True))
                )
                value["loaded_libraries"].sort()
                return subprocess.CompletedProcess(
                    command, 0, stdout=controller.canonical_json(value)
                )

            with self.assertRaisesRegex(controller.CorpusError, "unmanifested or host"):
                controller.preflight_python_runtime(
                    snapshot, runner=host_dependency_probe
                )
            self.thaw(snapshot)

    def test_fingerprint_contains_every_contract_field_and_is_canonical(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            document, fingerprint = controller.build_fingerprint_document(
                snapshot,
                startup_timeout=11,
                generation_timeout=22,
                cold_load_timeout=33,
                jobs=3,
                port_base=62000,
            )
            expected = {
                "input_manifest_sha256", "ordered_map_manifest_sha256",
                "engine_sha256", "python_runtime_version",
                "python_loader_path", "python_loader_sha256", "python_interpreter_path",
                "python_interpreter_sha256", "python_libpython_sha256",
                "python_runtime_manifest_sha256", "module_hashes", "action_contract_hash",
                "mechanism_contract_hash", "linter_sha256", "reader_sha256",
                "snagrepair_sha256",
                "acceptor_gnu_sha256", "acceptor_make_sha256",
                "semantic_checker_manifest_sha256", "semantic_checkers",
                "adoption_policy_version", "adopted_runes",
                "generation_timeout_seconds",
                "startup_timeout_seconds", "cold_load_timeout_seconds",
                "job_count", "port_base",
                "engine_arguments", "python_isolation_flags",
                "python_gate_bootstrap_sha256", "engine_environment",
                "guard_bootstrap_sha256",
                "python_loader_arguments", "python_handshake_sha256",
                "pseudo_map_allowlist", "python_environment", "acceptor_environment",
                "controller_sha256",
                "finalizer_sha256", "route_only_policy_sha256",
                "route_only_policy_version",
            }
            self.assertEqual(expected, set(document))
            self.assertNotIn("version", document)
            self.assertNotIn("format", document)
            self.assertEqual(33, document["cold_load_timeout_seconds"])
            self.assertEqual(controller.sha256_bytes(controller.canonical_json(document)), fingerprint)
            self.thaw(snapshot)

    def test_adopted_snapshot_and_replacement_planner_are_one_shot(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(
                work, adopted_runes={"lmctf01": b"frozen candidate"}
            )
            verified = controller.verify_snapshot(snapshot)
            candidate = verified["adopted_runes"]["lmctf01"]
            self.assertEqual("adopted-runes/lmctf01.rune", candidate["path"])
            document, _fingerprint = controller.build_fingerprint_document(
                snapshot, startup_timeout=1, generation_timeout=None,
                cold_load_timeout=1, jobs=1, port_base=62000,
            )
            self.assertEqual(1, document["adoption_policy_version"])
            self.assertEqual([candidate], document["adopted_runes"])
            run_root = work / "run"
            first = controller.decide_map_work(
                "lmctf01", [], run_root=run_root, adopted_runes=verified["adopted_runes"]
            )
            self.assertEqual("adopted_validation", first.kind)
            missing = controller.decide_map_work(
                "lmctf02", [], run_root=run_root, adopted_runes=verified["adopted_runes"]
            )
            self.assertEqual("generated_missing", missing.kind)
            number, attempt = controller.next_attempt_directory(run_root, "lmctf01")
            intent = controller.write_attempt_intent(
                run_root, attempt, fingerprint="f" * 64, map_name="lmctf01",
                stable_port=62000, attempt_number=number, work=first,
            )
            controller.atomic_write_json(attempt / "result.json", {
                "classification": "PROOF_REQUIRED",
                "disposition": "artifact_rejected",
            })
            controller._publish_attempt_commit(run_root, "lmctf01", attempt, number)
            with self.assertRaises(controller.CorpusError):
                controller.load_map_history(
                    run_root, "lmctf01", fingerprint="f" * 64, stable_port=62000,
                )
            with mock.patch.object(controller, "_validate_terminal_schema"):
                history = controller.load_map_history(
                    run_root, "lmctf01", fingerprint="f" * 64, stable_port=62000,
                )
            replacement = controller.decide_map_work(
                "lmctf01", history, run_root=run_root, adopted_runes=verified["adopted_runes"]
            )
            self.assertEqual("generated_replacement", replacement.kind)
            self.assertEqual(
                "runs/lmctf01/attempt-0001/result.json", replacement.rejection_result
            )
            number, attempt = controller.next_attempt_directory(run_root, "lmctf01")
            controller.write_attempt_intent(
                run_root, attempt, fingerprint="f" * 64, map_name="lmctf01",
                stable_port=62000, attempt_number=number, work=replacement,
            )
            with mock.patch.object(controller, "_validate_terminal_schema"):
                history = controller.load_map_history(
                    run_root, "lmctf01", fingerprint="f" * 64, stable_port=62000,
                )
            self.assertIsNone(controller.decide_map_work(
                "lmctf01", history, run_root=run_root, adopted_runes=verified["adopted_runes"]
            ))
            self.thaw(snapshot)

    def test_full_selection_accepts_snapshot_defined_adoption_count(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            with self.assertRaisesRegex(controller.CorpusError, "jobs >= 2"):
                controller.execute_selection(
                    snapshot=snapshot, run_root=work / "run",
                    selected_maps=controller.validate_manifest(),
                    port_base=62000, startup_timeout=1, generation_timeout=None,
                    cold_load_timeout=1, jobs=1,
                    engine_arguments=controller.DEFAULT_ENGINE_ARGUMENTS,
                )
            self.assertFalse((work / "run").exists())
            self.thaw(snapshot)

    def test_adopted_work_does_not_enter_the_generation_launcher(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(
                work, adopted_runes={"lmctf01": b"frozen candidate"}
            )
            candidate = controller.verify_snapshot(snapshot)["adopted_runes"]["lmctf01"]
            planned = controller.MapWork("adopted_validation", candidate, None)
            with mock.patch.object(
                controller, "run_adopted_map", return_value={"ok": True}
            ) as adopted, \
                 mock.patch.object(controller.subprocess, "Popen") as launch:
                result = controller.run_one_map(
                    work / "run", snapshot, "lmctf01", 62000, "f" * 64,
                    startup_timeout=1, generation_timeout=None, cold_load_timeout=1,
                    work=planned,
                )
            self.assertEqual({"ok": True}, result)
            launch.assert_not_called()
            adopted.assert_called_once()
            intent, _raw = controller._load_json_regular(
                work / "run/runs/lmctf01/attempt-0001/intent.json"
            )
            self.assertEqual("adopted_validation", intent["kind"])
            self.thaw(snapshot)

    def test_production_gate_snapshot_failure_is_infrastructure_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / "candidate.rune"
            artifact.write_bytes(b"candidate")
            with mock.patch.object(
                controller, "verify_snapshot", side_effect=controller.CorpusError("drift")
            ):
                with self.assertRaises(controller.GateIntegrityError):
                    controller.run_gates(
                        artifact, "lmctf01", None,
                        acceptor_gnu=artifact, acceptor_make=artifact.with_name("other"),
                        python_interpreter=artifact, runeio=artifact, runelint=artifact,
                        log_directory=None,
                    )

    def test_typed_c_gate_exits_and_malformed_success_are_not_equivalent(self):
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / "lmctf01.rune"
            artifact.write_bytes(b"candidate")

            def invoke(code, output=b"{}"):
                def runner(command, **_kwargs):
                    return subprocess.CompletedProcess(command, code, output)
                return runner

            kwargs = dict(
                acceptor_gnu=artifact, acceptor_make=artifact.with_name("make"),
                python_interpreter=artifact, runeio=artifact, runelint=artifact,
                log_directory=None,
            )
            with mock.patch.object(
                controller, "verify_snapshot",
                side_effect=controller.CorpusError("fake"),
            ):
                with self.assertRaises(controller.ArtifactRejectedError):
                    controller.run_gates(
                        artifact, "lmctf01", None, runner=invoke(1), **kwargs
                    )
                for code in (2, 3, -9):
                    with self.assertRaises(controller.GateIntegrityError):
                        controller.run_gates(
                            artifact, "lmctf01", None,
                            runner=invoke(code), **kwargs,
                        )
                with self.assertRaises(controller.GateIntegrityError):
                    controller.run_gates(
                        artifact, "lmctf01", None,
                        runner=invoke(0, b"not-json\n"), **kwargs,
                    )

    def test_python_gate_typed_structural_and_io_outcomes(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "lmctf58.rune"
            path.write_bytes(b"broken")
            with mock.patch.object(runelint.runeio, "read", side_effect=ValueError("bad wire")):
                self.assertEqual(1, runelint.main([str(path)]))
            with mock.patch.object(runelint.runeio, "read", side_effect=OSError("EIO")):
                self.assertEqual(3, runelint.main([str(path)]))
            with mock.patch.object(
                lmctf58_rune_accept.runeio, "read",
                side_effect=ValueError("bad wire"),
            ):
                self.assertEqual(3, lmctf58_rune_accept.main([
                    "--objective-roots", "1", "2", str(path),
                ]))
            with mock.patch.object(
                lmctf58_rune_accept.runeio, "read",
                side_effect=OSError("EIO"),
            ):
                self.assertEqual(3, lmctf58_rune_accept.main([
                    "--objective-roots", "1", "2", str(path),
                ]))

    def test_cold_loader_rejection_requires_selected_map_path(self):
        selected = "rune: rejected game/maps/lmctf01.rune stage=objective-core reason=bad"
        wrong = "rune: rejected game/maps/lmctf02.rune stage=objective-core reason=bad"
        terminal = (
            "slipgate: rune setup terminal map=lmctf01 source=autoload "
            "class=artifact stage=objective-core"
        )
        self.assertEqual(
            selected,
            controller.cold_loader_rejection([selected, terminal], "lmctf01"),
        )
        self.assertIsNone(controller.cold_loader_rejection([wrong, terminal], "lmctf01"))
        self.assertIsNone(controller.cold_loader_rejection([selected], "lmctf01"))
        self.assertIsNone(controller.cold_loader_rejection([
            "rune: rejected game/maps/lmctf01.rune stage=decode reason=bad"
        ], "lmctf01"))

    def test_cold_infrastructure_receipt_precedes_rejection_in_both_orders(self):
        rejected = (
            "rune: rejected game/maps/lmctf01.rune "
            "stage=objective-core reason=bad"
        )
        infra = "rune: infrastructure game/maps/lmctf01.rune stage=air reason=busy"
        for lines in ((rejected, infra), (infra, rejected)):
            with self.subTest(lines=lines):
                self.assertEqual(infra, controller.last_cold_load_failure(lines, "lmctf01"))
                self.assertIsNone(controller.cold_loader_rejection(lines, "lmctf01"))

    def test_setup_terminal_receipts_are_source_bound(self):
        autoload = (
            "slipgate: rune setup terminal map=lmctf01 source=autoload "
            "class=infra stage=missing"
        )
        write = (
            "slipgate: rune setup terminal map=lmctf01 source=write "
            "class=infra stage=fields"
        )
        self.assertEqual(
            ("infra", "missing", autoload),
            controller.setup_terminal_receipt((autoload, write), "lmctf01", "autoload"),
        )
        self.assertEqual(
            ("infra", "fields", write),
            controller.setup_terminal_receipt((autoload, write), "lmctf01", "write"),
        )
        with self.assertRaisesRegex(controller.CorpusError, "duplicate"):
            controller.setup_terminal_receipt((write, write), "lmctf01", "write")

    def test_cold_artifact_authority_requires_the_c_emitted_terminal_pair(self):
        diagnostic = (
            "rune: rejected game/maps/lmctf01.rune "
            "stage=objective-core reason=objective contract mismatch"
        )
        terminal = (
            "slipgate: rune setup terminal map=lmctf01 source=autoload "
            "class=artifact stage=objective-core"
        )
        self.assertEqual(
            diagnostic, controller.cold_loader_rejection((diagnostic, terminal), "lmctf01")
        )
        self.assertIsNone(controller.cold_loader_rejection((diagnostic,), "lmctf01"))
        self.assertIsNone(controller.cold_loader_rejection((
            diagnostic,
            terminal.replace("objective-core", "load"),
        ), "lmctf01"))

    def test_missing_runtime_root_receipt_retries_adoption(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(
                work, adopted_runes={"lmctf01": b"frozen candidate"}
            )
            run_root = work / "run"
            candidate = controller.verify_snapshot(snapshot)["adopted_runes"]["lmctf01"]
            planned = controller.MapWork("adopted_validation", candidate, None)
            number, attempt = controller.next_attempt_directory(run_root, "lmctf01")
            intent = controller.write_attempt_intent(
                run_root, attempt, fingerprint="f" * 64, map_name="lmctf01",
                stable_port=62000, attempt_number=number, work=planned,
            )
            reader = {
                "decoded_counts": {
                    "seed_count": 1, "link_count": 1, "node_count": 1,
                    "trigger_count": 0, "inventory_edge_count": 0,
                    "plan_edge_count": 0, "edge_count": 0, "plan_count": 1,
                },
                "route_contract": "complete",
            }
            heartbeat = mock.Mock()
            with mock.patch.object(controller, "preflight_python_runtime") as preflight, \
                 mock.patch.object(controller, "run_gates", return_value=reader), \
                 mock.patch.object(
                     controller, "run_fresh_cold_load",
                     side_effect=controller.CorpusError(
                         "fresh cold-load expected one runtime objective-root line, found 0"
                     ),
                 ) as cold_load:
                result = controller.run_adopted_map(
                    run_root, snapshot, "lmctf01", 62000, "f" * 64,
                    attempt, number, intent, planned, cold_load_timeout=1,
                    engine_arguments=controller.DEFAULT_ENGINE_ARGUMENTS,
                    heartbeat=heartbeat, gate_runner=subprocess.run,
                )
            self.assertEqual("INFRA_FAIL", result["classification"])
            self.assertEqual("infra_failed", result["disposition"])
            preflight_check = preflight.call_args.kwargs["heartbeat_check"]
            cold_load_check = cold_load.call_args.kwargs["heartbeat_check"]
            self.assertIsNotNone(preflight_check)
            self.assertIsNotNone(cold_load_check)
            preflight_check()
            cold_load_check()
            self.assertGreaterEqual(
                heartbeat.mock_calls.count(mock.call("beat", "lmctf01", None)), 2,
            )
            history = controller.load_map_history(
                run_root, "lmctf01", fingerprint="f" * 64, stable_port=62000,
            )
            self.assertEqual("adopted_validation", controller.decide_map_work(
                "lmctf01", history, run_root=run_root,
                adopted_runes=controller.verify_snapshot(snapshot)["adopted_runes"]
            ).kind)
            self.thaw(snapshot)

    def test_intent_only_recovery_retries_adoption_but_exhausts_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(
                work, adopted_runes={"lmctf01": b"frozen candidate"}
            )
            run_root = work / "run"
            candidate = controller.verify_snapshot(snapshot)["adopted_runes"]["lmctf01"]
            adopted = controller.MapWork("adopted_validation", candidate, None)
            number, attempt = controller.next_attempt_directory(run_root, "lmctf01")
            controller.write_attempt_intent(
                run_root, attempt, fingerprint="f" * 64, map_name="lmctf01",
                stable_port=62000, attempt_number=number, work=adopted,
            )
            assignments = {"lmctf01": {"map": "lmctf01", "port": 62000}}
            self.assertEqual(1, controller.recover_stale_attempts(
                run_root, ["lmctf01"], "f" * 64, assignments
            ))
            history = controller.load_map_history(
                run_root, "lmctf01", fingerprint="f" * 64, stable_port=62000,
            )
            self.assertEqual("adopted_validation", controller.decide_map_work(
                "lmctf01", history, run_root=run_root,
                adopted_runes=controller.verify_snapshot(snapshot)["adopted_runes"]
            ).kind)
            replacement = controller.MapWork(
                "generated_replacement", candidate,
                "runs/lmctf01/attempt-0001/result.json",
            )
            number, attempt = controller.next_attempt_directory(run_root, "lmctf01")
            controller.write_attempt_intent(
                run_root, attempt, fingerprint="f" * 64, map_name="lmctf01",
                stable_port=62000, attempt_number=number, work=replacement,
            )
            self.assertEqual(1, controller.recover_stale_attempts(
                run_root, ["lmctf01"], "f" * 64, assignments
            ))
            history = controller.load_map_history(
                run_root, "lmctf01", fingerprint="f" * 64, stable_port=62000,
            )
            self.assertIsNone(controller.decide_map_work(
                "lmctf01", history, run_root=run_root,
                adopted_runes=controller.verify_snapshot(snapshot)["adopted_runes"]
            ))
            self.thaw(snapshot)

    def test_recovery_removes_only_empty_preintent_crash_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            orphan = run_root / "runs/lmctf01/attempt-0001"
            orphan.mkdir(parents=True)
            assignments = {"lmctf01": {"map": "lmctf01", "port": 62000}}
            self.assertEqual(0, controller.recover_stale_attempts(
                run_root, ["lmctf01"], "f" * 64, assignments
            ))
            self.assertFalse(orphan.exists())
            number, attempt = controller.next_attempt_directory(run_root, "lmctf01")
            self.assertEqual(1, number)
            attempt.rmdir()
            orphan.mkdir()
            controller.atomic_write_bytes(orphan / "untrusted", b"data")
            with self.assertRaisesRegex(controller.CorpusError, "lacks its immutable intent"):
                controller.recover_stale_attempts(
                    run_root, ["lmctf01"], "f" * 64, assignments
                )

    def test_post_intent_rename_failure_is_promoted_and_consumes_attempt(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            work = controller.MapWork("generated_missing", None, None)
            with mock.patch.object(controller.os, "rename", side_effect=OSError("rename")):
                with self.assertRaises(OSError):
                    controller.create_attempt_with_intent(
                        run_root, fingerprint="f" * 64, map_name="lmctf01",
                        stable_port=62000, work=work,
                    )
            hidden = list((run_root / "runs/lmctf01").glob(".attempt-*-*"))
            self.assertEqual(1, len(hidden))
            controller.promote_pending_attempts(
                run_root, "lmctf01", fingerprint="f" * 64, stable_port=62000,
            )
            self.assertTrue((run_root / "runs/lmctf01/attempt-0001/intent.json").is_file())

    def test_pending_preintent_debris_is_removed_but_unexpected_content_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            root = run_root / "runs/lmctf01"
            debris = root / ".attempt-0001-race"
            debris.mkdir(parents=True)
            controller.atomic_write_bytes(debris / ".intent.json.partial", b"partial")
            controller.promote_pending_attempts(
                run_root, "lmctf01", fingerprint="f" * 64, stable_port=62000,
            )
            self.assertFalse(debris.exists())
            bad = root / ".attempt-0001-bad"
            bad.mkdir()
            controller.atomic_write_bytes(bad / "other", b"bad")
            with self.assertRaisesRegex(controller.CorpusError, "unexpected content"):
                controller.promote_pending_attempts(
                    run_root, "lmctf01", fingerprint="f" * 64, stable_port=62000,
                )

    def test_cold_load_timeout_changes_fingerprint(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            arguments = dict(
                startup_timeout=10, generation_timeout=900,
                cold_load_timeout=300, jobs=1, port_base=62000,
            )
            first_document, first_hash = controller.build_fingerprint_document(
                snapshot, **arguments
            )
            arguments["cold_load_timeout"] = 420
            second_document, second_hash = controller.build_fingerprint_document(
                snapshot, **arguments
            )
            self.assertEqual(300, first_document["cold_load_timeout_seconds"])
            self.assertEqual(420, second_document["cold_load_timeout_seconds"])
            self.assertNotEqual(first_hash, second_hash)
            self.thaw(snapshot)

    def test_contract_sha_change_changes_fingerprint(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            first = self.make_snapshot(work / "first", action_hash=b"a")
            second = self.make_snapshot(work / "second", action_hash=b"c")
            arguments = dict(
                startup_timeout=1, generation_timeout=2, cold_load_timeout=3,
                jobs=1, port_base=62000,
            )
            _first_document, first_hash = controller.build_fingerprint_document(first, **arguments)
            _second_document, second_hash = controller.build_fingerprint_document(second, **arguments)
            self.assertNotEqual(first_hash, second_hash)
            self.thaw(first)
            self.thaw(second)

    def test_snapshot_rejects_acceptor_built_for_stale_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(
                controller.CorpusError, "acceptor_gnu contract identity mismatch"
            ):
                self.make_snapshot(
                    Path(temporary), action_hash=b"a", acceptor_action_hash=b"c"
                )

    def test_acceptor_contract_probe_uses_guarded_runner(self):
        contracts = {
            "action_contract_sha256": "a" * 64,
            "mechanism_contract_sha256": "b" * 64,
        }
        with tempfile.TemporaryDirectory() as temporary:
            acceptor = Path(temporary) / "runeaccept"
            acceptor.write_text(
                "#!/bin/sh\n"
                "printf '%s\\n' '" + json.dumps(contracts) + "'\n",
                encoding="ascii",
            )
            acceptor.chmod(0o755)
            with mock.patch.object(
                controller,
                "_run_guarded_gate",
                return_value=subprocess.CompletedProcess(
                    ("guard",), 0, controller.canonical_json(contracts),
                ),
            ) as guarded:
                self.assertEqual(
                    ("a" * 64, "b" * 64),
                    controller._acceptor_contract_hashes(acceptor),
                )
            guarded.assert_called_once()
            command = guarded.call_args.args[0]
            self.assertEqual(
                os.fspath(Path(os.sys.executable).resolve(strict=True)), command[0]
            )
            self.assertEqual(
                [*controller.PYTHON_ISOLATION_FLAGS, "-c", controller.GUARD_BOOTSTRAP],
                command[1:6],
            )
            self.assertEqual([str(os.getpid()), "--"], command[6:8])
            self.assertRegex(command[8], r"^/proc/self/fd/[0-9]+$")
            self.assertEqual(
                ["--contracts"], command[9:],
            )
            self.assertEqual(acceptor.parent, guarded.call_args.kwargs["cwd"])
            self.assertIsNone(guarded.call_args.kwargs["heartbeat_check"])
            self.assertEqual(
                (int(command[8].rpartition("/")[2]),),
                guarded.call_args.kwargs["pass_fds"],
            )
            self.assertEqual(
                controller.ACCEPTOR_CONTRACT_PROBE_TIMEOUT,
                guarded.call_args.kwargs["deadline"],
            )

    def test_acceptor_contract_probe_bounds_output_and_tears_down_child(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            pid_record = work / "acceptor.pid"
            acceptor = work / "runeaccept"
            acceptor.write_text(
                f"#!{Path(os.sys.executable).resolve(strict=True)}\n"
                "import os\n"
                f"with open({os.fspath(pid_record)!r}, 'w', encoding='ascii') as stream:\n"
                "    stream.write(str(os.getpid()))\n"
                "while True:\n"
                "    os.write(1, b'x' * 65536)\n",
                encoding="ascii",
            )
            acceptor.chmod(0o755)
            with self.assertRaisesRegex(
                controller.CorpusError, "cannot read acceptor contract identity"
            ) as raised:
                controller._acceptor_contract_hashes(acceptor)
            self.assertIsInstance(
                raised.exception.__cause__, controller.GateIntegrityError
            )
            self.assertIn("stdout exceeds bounded evidence limit", str(
                raised.exception.__cause__
            ))
            pid = int(pid_record.read_text(encoding="ascii"))
            self.assertFalse(Path(f"/proc/{pid}").exists())

    def test_snapshot_rejects_acceptor_pathname_swap_after_contract_probe(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            original_snapshot = controller.create_input_snapshot
            original_probe = controller._acceptor_contract_hashes

            def snapshot_with_swap(output, inputs):
                swapped = False

                def probe(path, *args, **kwargs):
                    nonlocal swapped
                    result = original_probe(path, *args, **kwargs)
                    if path.name == "runeaccept.gnu" and not swapped:
                        swapped = True
                        replacement = path.with_name("runeaccept.replacement")
                        replacement.write_text(
                            "#!/bin/sh\nprintf '%s\\n' '{}\n",
                            encoding="ascii",
                        )
                        replacement.chmod(0o755)
                        os.replace(replacement, path)
                    return result

                with mock.patch.object(
                        controller, "_acceptor_contract_hashes", side_effect=probe):
                    return original_snapshot(output, inputs)

            with mock.patch.object(
                    controller, "create_input_snapshot", side_effect=snapshot_with_swap):
                with self.assertRaisesRegex(
                    controller.CorpusError, "input changed after contract probe"):
                    self.make_snapshot(work)
            self.assertFalse((work / "snapshot").exists())

    def test_snapshot_rejects_contract_source_swap_after_final_acceptor_probe(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            original_snapshot = controller.create_input_snapshot
            original_probe = controller._acceptor_contract_hashes

            def snapshot_with_swap(output, inputs):
                swapped = False

                def probe(path, *args, **kwargs):
                    nonlocal swapped
                    result = original_probe(path, *args, **kwargs)
                    if path.name == "runeaccept.make" and not swapped:
                        swapped = True
                        contracts = path.with_name("tools_rune_contracts_generated.py")
                        replacement = contracts.with_name("contracts.replacement")
                        replacement.write_text("changed\n", encoding="ascii")
                        os.replace(replacement, contracts)
                    return result

                with mock.patch.object(
                    controller, "_acceptor_contract_hashes", side_effect=probe
                ):
                    return original_snapshot(output, inputs)

            with mock.patch.object(
                controller, "create_input_snapshot", side_effect=snapshot_with_swap
            ):
                with self.assertRaisesRegex(
                    controller.CorpusError, "input changed after contract probe"
                ):
                    self.make_snapshot(work)
            self.assertFalse((work / "snapshot").exists())

    def test_private_runtime_byte_change_changes_full_fingerprint(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            first = self.make_snapshot(work / "first", runtime_json=b"private json v1")
            second = self.make_snapshot(work / "second", runtime_json=b"private json v2")
            arguments = dict(
                startup_timeout=1, generation_timeout=2, cold_load_timeout=3,
                jobs=1, port_base=62000,
            )
            first_document, first_hash = controller.build_fingerprint_document(
                first, **arguments
            )
            second_document, second_hash = controller.build_fingerprint_document(
                second, **arguments
            )
            self.assertNotEqual(
                first_document["python_runtime_manifest_sha256"],
                second_document["python_runtime_manifest_sha256"],
            )
            self.assertNotEqual(first_hash, second_hash)
            self.thaw(first)
            self.thaw(second)

    def test_snapshot_manifest_rejects_duplicate_role_and_semantic_discriminators(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            manifest_path = snapshot / "input-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertNotIn("version", manifest)
            self.assertNotIn("format", manifest)
            duplicate = dict(next(item for item in manifest["files"] if item["role"] == "engine"))
            duplicate["path"] = "bin/second-engine"
            snapshot.chmod(0o700)
            (snapshot / "bin").chmod(0o700)
            manifest_path.chmod(0o600)
            source_engine = snapshot / next(
                item["path"] for item in manifest["files"] if item["role"] == "engine"
            )
            second_engine = snapshot / duplicate["path"]
            second_engine.write_bytes(source_engine.read_bytes())
            second_engine.chmod(duplicate["mode"])
            manifest["files"].append(duplicate)
            controller.atomic_write_json(manifest_path, manifest, mode=0o444)
            (snapshot / "bin").chmod(0o555)
            snapshot.chmod(0o555)
            with self.assertRaisesRegex(controller.CorpusError, "roles"):
                controller.verify_snapshot(snapshot)

    def test_semantic_manifest_cannot_omit_lmctf58_authority(self):
        with tempfile.TemporaryDirectory() as temporary:
            manifest = Path(temporary) / "semantic.json"
            manifest.write_text('{"checkers":[]}\n', encoding="ascii")
            with self.assertRaisesRegex(
                controller.CorpusError, "required applicability"
            ):
                controller.load_semantic_checker_manifest(
                    manifest, controller.validate_manifest()
                )

    def test_private_staging_has_only_attempt_local_runtime_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            attempt = work / "run/runs/lmctf01/attempt-0001"
            attempt.mkdir(parents=True)
            private, engine, artifact, config = controller.stage_private_inputs(
                attempt, snapshot, "lmctf01"
            )
            self.assertEqual(attempt / "private", private)
            self.assertTrue(engine.is_file())
            self.assertFalse(engine.is_symlink())
            self.assertEqual("q2ded-rune-corpus", engine.name)
            self.assertEqual(controller.CORPUS_ENGINE_BASENAME, engine.name)
            self.assertNotEqual("q2ded", engine.name[:15])
            engine_role = controller.verify_snapshot(snapshot)["by_role"]["engine"]
            self.assertEqual("bin/q2ded", engine_role["path"])
            self.assertEqual(engine_role["sha256"], controller.sha256_regular(engine))
            self.assertFalse(artifact.exists())
            expected = {
                "game.so", "gamex86_64.so", config,
                "maps/lmctf01.bsp",
            }
            actual = {
                path.relative_to(private / "game").as_posix()
                for path in (private / "game").rglob("*") if path.is_file()
            }
            self.assertEqual(expected, actual)
            self.thaw(snapshot)

    def test_engine_argument_template_rejects_all_authority_overrides(self):
        unsafe = (
            controller.DEFAULT_ENGINE_ARGUMENTS + ("+set", "game", "../shared"),
            controller.DEFAULT_ENGINE_ARGUMENTS + ("+set", "game", "/tmp/shared"),
            controller.DEFAULT_ENGINE_ARGUMENTS + ("+exec", "../shared.cfg"),
            controller.DEFAULT_ENGINE_ARGUMENTS + ("+set", "port", "1"),
        )
        for arguments in unsafe:
            with self.subTest(arguments=arguments[-3:]):
                with self.assertRaisesRegex(controller.CorpusError, "closed private"):
                    controller.validate_engine_arguments(arguments)

    def test_pidfd_preflight_failure_occurs_before_any_launch(self):
        with mock.patch.object(controller.os, "pidfd_open", None), mock.patch.object(
            controller.subprocess, "Popen", side_effect=AssertionError("launch forbidden")
        ) as launch:
            with self.assertRaisesRegex(controller.CorpusError, "pidfd"):
                controller.require_pidfd_support()
        launch.assert_not_called()

    def test_verified_python_requires_pidfd_send_before_launch(self):
        with mock.patch.object(
            controller, "require_pidfd_support",
            side_effect=controller.CorpusError("pidfd open/send support is required before launch"),
        ), mock.patch.object(
            controller.subprocess, "Popen", side_effect=AssertionError("launch forbidden")
        ) as launch:
            with self.assertRaisesRegex(controller.CorpusError, "pidfd"):
                controller.run_verified_python(
                    Path("/not-opened"), {}, "lint", Path("/not-opened/rune"), ()
                )
        launch.assert_not_called()

    def test_blocked_guarded_c_gate_stops_on_heartbeat_failure(self):
        process = mock.Mock()
        process.pid = 4242
        process.poll.return_value = None
        process.stdout.fileno.return_value = 7
        with mock.patch.object(controller, "require_pidfd_support"), \
                mock.patch.object(controller.subprocess, "Popen", return_value=process), \
                mock.patch.object(controller, "open_pidfd", return_value=91), \
                mock.patch.object(controller, "shutdown_spawned_child") as shutdown, \
                mock.patch.object(controller.os, "close"), \
                mock.patch.object(controller.os, "set_blocking"), \
                mock.patch.object(
                    controller.selectors, "DefaultSelector",
                    return_value=mock.Mock(),
                ):
            with self.assertRaisesRegex(controller.CorpusError, "ticker write failed"):
                controller._run_guarded_gate(
                    ("guard", "accept"), cwd=ROOT,
                    heartbeat_check=lambda: (_ for _ in ()).throw(
                        controller.CorpusError("ticker write failed")
                    ),
                )
        shutdown.assert_called_once_with(process, 91)

    def test_contract_probe_deadline_tears_down_exact_child(self):
        process = mock.Mock()
        process.pid = 4242
        process.poll.return_value = None
        process.stdout.fileno.return_value = 7
        selector = mock.Mock()
        selector.get_map.return_value = {7: object()}
        with mock.patch.object(controller, "require_pidfd_support"), \
                mock.patch.object(controller.subprocess, "Popen", return_value=process), \
                mock.patch.object(controller, "open_pidfd", return_value=91), \
                mock.patch.object(controller, "shutdown_spawned_child") as shutdown, \
                mock.patch.object(controller.os, "close"), \
                mock.patch.object(controller.os, "set_blocking"), \
                mock.patch.object(controller.selectors, "DefaultSelector", return_value=selector), \
                mock.patch.object(controller.time, "monotonic", side_effect=(0.0, 300.0)):
            with self.assertRaisesRegex(
                    controller.GateIntegrityError, "did not exit by its deadline"):
                controller._run_guarded_gate(
                    ("guard", "accept"), cwd=ROOT, heartbeat_check=None,
                    deadline=controller.ACCEPTOR_CONTRACT_PROBE_TIMEOUT,
                )
        shutdown.assert_called_once_with(process, 91)

    def test_guarded_gate_drains_flooded_stdout_while_waiting(self):
        completed = controller._run_guarded_gate(
            (
                os.fspath(Path(os.sys.executable)), "-c",
                "import sys; sys.stdout.write('x' * 262144)",
            ),
            cwd=ROOT, heartbeat_check=None,
        )
        self.assertEqual(0, completed.returncode)
        self.assertEqual(262144, len(completed.stdout))

    def test_preflight_forwards_ticker_failure_to_verified_child(self):
        with mock.patch.object(
            controller, "verify_snapshot", return_value={
                "python_runtime": {
                    "root": "python-runtime", "interpreter": {"path": "bin/python"},
                }, "by_role": {"runeio": {"path": "runeio.py"}},
            },
        ), mock.patch.object(
            controller, "run_verified_python",
            side_effect=lambda *_args, **kwargs: kwargs["heartbeat_check"](),
        ) as verified:
            with self.assertRaisesRegex(controller.CorpusError, "ticker write failed"):
                controller.preflight_python_runtime(
                    Path("/snapshot"),
                    heartbeat_check=lambda: (_ for _ in ()).throw(
                        controller.CorpusError("ticker write failed")
                    ),
                )
        self.assertIsNotNone(verified.call_args.kwargs["heartbeat_check"])

    def test_recheck_pass_gates_forwards_heartbeat_to_preflight_and_gates(self):
        heartbeat_check = mock.Mock()
        checked = {
            "decoded_counts": {"seed_count": 1},
            "route_contract": "complete",
            "gate_output_sha256": {"c_gnu": "a" * 64},
            "semantic_gate_labels": [],
        }
        result = {
            "map": "lmctf01",
            "banner_counts": {"seeds": 1},
            "objective_roots": {"red": 1, "blue": 2},
            "fingerprint": "f" * 64,
            **checked,
        }
        verified = {
            "python_runtime": {"interpreter": {"path": "python-runtime/bin/python"}},
            "by_role": {
                "acceptor_gnu": {"path": "runeaccept.gnu"},
                "acceptor_make": {"path": "runeaccept.make"},
                "runeio": {"path": "tools/runeio.py"},
                "runelint": {"path": "tools/runelint.py"},
            },
            "semantic_checkers": [],
        }
        with mock.patch.object(controller, "preflight_python_runtime") as preflight, \
                mock.patch.object(controller, "verify_snapshot", return_value=verified), \
                mock.patch.object(controller, "run_gates", return_value=checked) as gates:
            controller._recheck_pass_gates(
                result,
                snapshot=Path("/snapshot"),
                artifact=Path("/artifact"),
                gate_runner=subprocess.run,
                heartbeat_check=heartbeat_check,
            )
        self.assertIs(heartbeat_check, preflight.call_args.kwargs["heartbeat_check"])
        self.assertIs(heartbeat_check, gates.call_args.kwargs["heartbeat_check"])

    def test_fake_engine_pass_failure_unbounded_and_timeout_lifecycle(self):
        class FakeInput:
            def __init__(self, process, output, private, ready, deferred,
                         failure_line, exit_failure_line):
                self.process = process
                self.output = output
                self.private = private
                self.ready = ready
                self.deferred = deferred
                self.failure_line = failure_line
                self.exit_failure_line = exit_failure_line

            def write(self, data):
                if data == b"sv rune\n":
                    output = self.output
                    if self.process.generation_ready_after_polls != 0:
                        self.output = io.BytesIO()
                    artifact = self.private / "game/maps/lmctf01.rune"
                    artifact.write_bytes(b"fresh fake artifact")
                    lines = (
                        "rune: topology status=0 contacts=10 contact_overflow=0 "
                        "initial_crossing_contacts=2 initial_crossing_directions=4 "
                        "owner_calls=4 proved_added=2 proved_present=0 "
                        "owner_rejected=2 owner_deferred=0 unexamined=0 "
                        "unresolved_contacts=0 unresolved_directions=0 "
                        "initial_sccs=3 final_sccs=1 scc_builds=2 added_links=2\n"
                        "rune: objective roots red=1 blue=2\n"
                        "rune: wrote game/maps/lmctf01.rune (7 seeds, 9 links, "
                        "4 mechanism nodes, 2 triggers, 3 inventory edges, "
                        "5 activation plans)\n"
                    )
                    if self.ready:
                        lines += (
                            "slipgate: rune ready lmctf01, 7 seeds, 9 links, "
                            "4 mechanism nodes, 5 plans, gravity 800, all fields up\n"
                        )
                    if self.deferred:
                        lines += (
                            "slipgate: snag declaration missing or invalid for map "
                            "lmctf01; fields rejected\n"
                            "slipgate: field setup failed (no flags?); disabled "
                            "until the next level\n"
                        )
                    if self.failure_line is not None:
                        lines += self.failure_line + "\n"
                    self.output.write(lines.encode())
                    self.output.flush()
                    if self.output is not output:
                        buffered = self.output.getvalue()
                        self.output = output

                        def publish_generation():
                            output.write(buffered)
                            output.flush()

                        self.process.generation_output = publish_generation
                elif data == b"quit\n":
                    if self.exit_failure_line is not None:
                        self.output.write((self.exit_failure_line + "\n").encode())
                        self.output.flush()
                    self.process.returncode = 0
                return len(data)

            def flush(self):
                return None

        class FakeProcess:
            def __init__(self, output, private, ready, deferred, failure_line,
                         exit_failure_line, pid, cold_output=None,
                         cold_ready_after_polls=None,
                         generation_ready_after_polls=0, immediate_exit=False):
                self.pid = pid
                self.returncode = 0 if immediate_exit else None
                self.poll_count = 0
                self.cold_output = cold_output
                self.cold_ready_after_polls = cold_ready_after_polls
                self.generation_output = None
                self.generation_poll_count = 0
                self.generation_ready_after_polls = generation_ready_after_polls
                self.stdin = FakeInput(
                    self, output, private, ready, deferred, failure_line,
                    exit_failure_line
                )

            def poll(self):
                self.poll_count += 1
                if self.generation_output is not None:
                    self.generation_poll_count += 1
                    if (
                        self.generation_ready_after_polls is not None
                        and self.generation_poll_count >= self.generation_ready_after_polls
                    ):
                        output = self.generation_output
                        self.generation_output = None
                        output()
                if (
                    self.cold_output is not None
                    and self.cold_ready_after_polls is not None
                    and self.poll_count >= self.cold_ready_after_polls
                ):
                    output = self.cold_output
                    self.cold_output = None
                    output()
                return self.returncode

            def wait(self, timeout):
                if self.returncode is None:
                    raise subprocess.TimeoutExpired("fake-q2ded", timeout)
                return self.returncode

        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            base_identity = controller.capture_process_identity(os.getpid())

            def run_scenario(
                run_root: Path,
                ready: bool,
                generation_timeout: int | None,
                deferred: bool = False,
                failure_line: str | None = None,
                exit_failure_line: str | None = None,
                cold_load_snag_failure: bool = False,
                cold_load_timeout: float = 0.25,
                cold_ready_after_polls: int | None = 1,
                generation_ready_after_polls: int | None = 0,
                heartbeat_events: list[str] | None = None,
                immediate_exit: bool = False,
            ):
                descriptors: list[int] = []
                launched_commands: list[list[str]] = []
                cold_load_timeouts: list[float] = []
                cold_processes: list[FakeProcess] = []
                original_cold_load = controller.run_fresh_cold_load

                def popen(command, **kwargs):
                    self.assertIn(controller.GUARD_BOOTSTRAP, command)
                    self.assertNotIn(str(ROOT / "tools/rune_corpus_controller.py"), command)
                    launched_commands.append(list(command))
                    launch_number = len(launched_commands)
                    verified = controller.verify_snapshot(snapshot)
                    interpreter = snapshot / verified["python_runtime"]["interpreter"]["path"]
                    self.assertEqual(str(snapshot / verified["python_runtime"]["loader"]["path"]), command[0])
                    self.assertEqual(
                        ["--inhibit-cache", "--library-path", str(snapshot / "python-runtime/lib")], command[1:4]
                    )
                    self.assertEqual(str(interpreter), command[4])
                    self.assertEqual(list(controller.PYTHON_ISOLATION_FLAGS), command[5:8])
                    self.assertEqual(
                        controller.PYTHON_ENVIRONMENT,
                        kwargs["env"],
                    )
                    separator = command.index("--")
                    launched_engine = Path(command[separator + 1])
                    self.assertEqual(
                        Path(kwargs["cwd"]) / controller.CORPUS_ENGINE_BASENAME,
                        launched_engine,
                    )
                    self.assertNotEqual("q2ded", launched_engine.name[:15])
                    cold_output = None
                    if launch_number == 2:
                        artifact = Path(kwargs["cwd"]) / "game/maps/lmctf01.rune"
                        snag = artifact.with_suffix(".snag")
                        evidence = Path(kwargs["cwd"]).parent / "snag-bootstrap-evidence.json"
                        def emit_cold_output():
                            if cold_load_snag_failure:
                                kwargs["stdout"].write(
                                    b"slipgate: snag declaration missing or invalid for "
                                    b"map lmctf01; fields rejected\n"
                                    b"slipgate: field setup failed (no flags?); disabled "
                                    b"until the next level\n"
                                    b"slipgate: rune setup terminal map=lmctf01 "
                                    b"source=autoload class=infra stage=fields\n"
                                )
                            else:
                                kwargs["stdout"].write(
                                    (
                                        "slipgate: snag ready map=lmctf01 repairs=0 "
                                        f"rune_sha256={controller.sha256_regular(artifact)} "
                                        f"evidence_sha256={controller.sha256_regular(evidence)} "
                                        f"snag_sha256={controller.sha256_regular(snag)}\n"
                                    ).encode("ascii")
                                )
                                kwargs["stdout"].write(
                                    b"slipgate: route contract complete\n"
                                )
                                kwargs["stdout"].write(
                                    b"slipgate: objective roots red=1 blue=2\n"
                                )
                                kwargs["stdout"].write(
                                    b"slipgate: rune ready lmctf01, 7 seeds, 9 links, "
                                    b"4 mechanism nodes, 5 plans, gravity 800, all fields up\n"
                                )
                            kwargs["stdout"].flush()
                        cold_output = emit_cold_output
                    fake = FakeProcess(
                        kwargs["stdout"], Path(kwargs["cwd"]), ready, deferred,
                        failure_line, exit_failure_line, 424241 + launch_number,
                        cold_output=cold_output,
                        cold_ready_after_polls=(
                            cold_ready_after_polls if launch_number == 2 else None
                        ),
                        generation_ready_after_polls=(
                            generation_ready_after_polls if launch_number == 1 else 0
                        ),
                        immediate_exit=immediate_exit and launch_number == 1,
                    )
                    if launch_number == 2:
                        cold_processes.append(fake)
                    return fake

                def identity_for_engine(_pid, executable, expected_argv=None, timeout=None):
                    return dataclasses.replace(
                        base_identity,
                        pid=_pid,
                        start_ticks=base_identity.start_ticks + _pid,
                        executable=str(Path(executable).resolve(strict=True)),
                        executable_sha256=controller.sha256_regular(Path(executable)),
                        cmdline_sha256=controller.sha256_bytes(expected_argv or b""),
                    )

                def pidfd(_pid):
                    descriptor = os.open("/dev/null", os.O_RDONLY)
                    descriptors.append(descriptor)
                    return descriptor

                def cold_load(*args, **kwargs):
                    cold_load_timeouts.append(kwargs["timeout"])
                    return original_cold_load(*args, **kwargs)

                def bootstrap_snag(_attempt, _snapshot, _map_name,
                                   artifact, _fingerprint, **_kwargs):
                    evidence = _attempt / "snag-bootstrap-evidence.json"
                    controller.atomic_write_json(evidence, {
                        "artifact_sha256": controller.sha256_regular(artifact),
                        "classification": "NO_ACCEPTED_OBSERVATION",
                        "fingerprint": _fingerprint,
                        "format": "lmctf-snag-bootstrap-v1",
                        "map": _map_name,
                    })
                    target = artifact.with_suffix(".snag")
                    target.write_text(
                        bootstrap_snag_text(
                            _map_name,
                            controller.sha256_regular(artifact),
                            controller.sha256_regular(evidence),
                        ),
                        encoding="ascii",
                    )
                    return {
                        "evidence": controller.regular_file_record(evidence),
                        "snag": controller.regular_file_record(target),
                    }

                def heartbeat(event, _map_name, _details):
                    assert heartbeat_events is not None
                    heartbeat_events.append(event)

                with mock.patch.object(controller.subprocess, "Popen", side_effect=popen), \
                        mock.patch.object(controller, "wait_for_exec_identity", side_effect=identity_for_engine), \
                        mock.patch.object(controller, "open_pidfd", side_effect=pidfd), \
                        mock.patch.object(controller, "stage_bootstrap_snag", side_effect=bootstrap_snag), \
                        mock.patch.object(controller, "run_fresh_cold_load", side_effect=cold_load):
                    result = controller.run_one_map(
                        run_root, snapshot, "lmctf01", 62000, "fingerprint",
                        startup_timeout=0.001,
                        generation_timeout=generation_timeout,
                        cold_load_timeout=cold_load_timeout,
                        heartbeat=(heartbeat if heartbeat_events is not None else None),
                        gate_runner=FakeGateRunner("lmctf01"),
                    )
                self.assertTrue(descriptors)
                deferred_write_terminal = (
                    deferred and failure_line == (
                        "slipgate: rune setup terminal map=lmctf01 source=write "
                        "class=infra stage=fields"
                    )
                )
                cold_load_expected = (
                    (ready or deferred)
                    and (failure_line is None or deferred_write_terminal)
                    and exit_failure_line is None
                )
                self.assertEqual(
                    2 if cold_load_expected else 1,
                    len(launched_commands),
                )
                self.assertEqual(
                    [cold_load_timeout] if cold_load_expected else [],
                    cold_load_timeouts,
                )
                return result, cold_processes

            pass_root = work / "pass-run"
            passed, cold_processes = run_scenario(
                pass_root, False, 1, deferred=True,
                cold_ready_after_polls=3,
            )
            self.assertEqual("PASS", passed["classification"])
            self.assertGreaterEqual(cold_processes[0].poll_count, 3)
            pass_result = json.loads(
                (pass_root / "runs/lmctf01/result.json").read_text()
            )
            owner = json.loads(
                (pass_root / pass_result["owner_record"]).read_text()
            )
            self.assertEqual(
                controller.CORPUS_ENGINE_BASENAME,
                Path(owner["process"]["executable"]).name,
            )
            self.assertNotEqual(
                "q2ded", Path(owner["process"]["executable"]).name[:15]
            )
            self.assertEqual(4, len(pass_result["gate_log_sha256"]))
            self.assertIsNotNone(pass_result["cold_load_owner_record"])
            self.assertIsNotNone(pass_result["cold_load_log_sha256"])
            self.assertIsNotNone(pass_result["cold_load_snag_record"])
            self.assertIsNotNone(pass_result["cold_load_snag_evidence_record"])
            attempt = pass_root / "runs/lmctf01/attempt-0001"
            self.assertEqual(
                attempt / "private/game/maps/lmctf01.rune",
                controller._validate_terminal_schema(
                    pass_result, run_root=pass_root, attempt=attempt,
                    map_name="lmctf01", fingerprint="fingerprint",
                    stable_port=62000,
                ),
            )
            forged_report = json.loads(json.dumps(pass_result))
            forged_report["generation_report"]["topology"][
                "unresolved_contacts"
            ] += 1
            with self.assertRaisesRegex(
                controller.CorpusError, "authenticated server log"
            ):
                controller._validate_terminal_schema(
                    forged_report, run_root=pass_root, attempt=attempt,
                    map_name="lmctf01", fingerprint="fingerprint",
                    stable_port=62000,
                )
            route_only_without_proof = json.loads(json.dumps(pass_result))
            route_only_without_proof["classification"] = "ROUTE_ONLY"
            route_only_without_proof["route_contract"] = "local_only"
            route_only_without_proof["normalized_signature"] = (
                controller.normalized_signature(
                    "ROUTE_ONLY", route_only_without_proof["detail"]
                )
            )
            with self.assertRaisesRegex(controller.CorpusError, "open-exhausted"):
                controller._validate_terminal_schema(
                    route_only_without_proof, run_root=pass_root,
                    attempt=attempt, map_name="lmctf01",
                    fingerprint="fingerprint", stable_port=62000,
                )
            self.assertEqual(0, stat.S_IMODE(attempt.stat().st_mode) & 0o222)
            self.assertEqual(0, stat.S_IMODE((attempt / "result.json").stat().st_mode) & 0o222)

            clock = [0.0]

            def monotonic():
                return clock[0]

            def sleep(seconds):
                clock[0] += 901.0 if seconds >= 0.1 else seconds

            heartbeat_events: list[str] = []
            with mock.patch.object(controller.time, "monotonic", side_effect=monotonic), \
                    mock.patch.object(controller.time, "sleep", side_effect=sleep):
                unbounded, _cold_processes = run_scenario(
                    work / "unbounded-run", True, None,
                    generation_ready_after_polls=2,
                    heartbeat_events=heartbeat_events,
                )
            self.assertEqual("PASS", unbounded["classification"])
            self.assertGreater(clock[0], 900.0)
            self.assertGreaterEqual(heartbeat_events.count("beat"), 2)

            write_infra = (
                "slipgate: rune setup terminal map=lmctf01 source=write "
                "class=infra stage=fields"
            )
            post_write_infra, _cold_processes = run_scenario(
                work / "post-write-infra-run", False, None,
                failure_line=write_infra,
            )
            self.assertEqual("INFRA_FAIL", post_write_infra["classification"])
            self.assertEqual(write_infra, post_write_infra["detail"])
            deferred_write_terminal, _cold_processes = run_scenario(
                work / "deferred-write-terminal-run", False, None,
                deferred=True, failure_line=write_infra,
            )
            self.assertEqual("PASS", deferred_write_terminal["classification"])

            immediate, _cold_processes = run_scenario(
                work / "immediate-exit-run", False, None, immediate_exit=True
            )
            self.assertEqual("GEN_FAIL", immediate["classification"])
            self.assertIn("before accepted generation completion", immediate["detail"])

            rejected_line = (
                "rune: rejected game/maps/lmctf01.rune stage=door-replay "
                "reason=invalid live declared-door replay index=9949"
            )
            rejected_root = work / "rejected-run"
            rejected, _cold_processes = run_scenario(
                rejected_root, False, 1, failure_line=rejected_line
            )
            self.assertEqual("GEN_FAIL", rejected["classification"])
            self.assertEqual(rejected_line, rejected["detail"])
            self.assertEqual(rejected_line, rejected["failure_line"])
            rejected_result = json.loads(
                (rejected_root / "runs/lmctf01/result.json").read_text()
            )
            self.assertEqual(rejected_line, rejected_result["detail"])
            self.assertEqual(rejected_line, rejected_result["failure_line"])
            self.assertEqual(
                controller.normalized_signature("GEN_FAIL", rejected_line),
                rejected_result["normalized_signature"],
            )

            exit_line = "rune: generation refused fast shutdown failure"
            fast_root = work / "fast-failure-run"
            fast, _cold_processes = run_scenario(
                fast_root, True, 1, exit_failure_line=exit_line
            )
            self.assertEqual("GEN_FAIL", fast["classification"])
            self.assertEqual(exit_line, fast["detail"])
            self.assertEqual(exit_line, fast["failure_line"])

            timeout_root = work / "timeout-run"
            timed_out, _cold_processes = run_scenario(timeout_root, False, 1)
            self.assertEqual("TIMEOUT", timed_out["classification"])
            self.assertFalse((timeout_root / "runs/lmctf01/attempt-0001/gate-c_gnu.log").exists())

            cold_timeout_root = work / "cold-timeout-run"
            cold_timed_out, cold_processes = run_scenario(
                cold_timeout_root, False, 1, deferred=True,
                cold_load_timeout=0.02, cold_ready_after_polls=None,
            )
            self.assertEqual("LINT_FAIL", cold_timed_out["classification"])
            self.assertEqual(
                "fresh cold-load exited or timed out before runtime-ready",
                cold_timed_out["detail"],
            )
            self.assertGreater(cold_processes[0].poll_count, 0)

            cold_failure_root = work / "cold-snag-failure-run"
            cold_failure, cold_processes = run_scenario(
                cold_failure_root, False, 1, deferred=True,
                cold_load_snag_failure=True, cold_load_timeout=5.0,
            )
            self.assertEqual("INFRA_FAIL", cold_failure["classification"])
            self.assertIn("source=autoload class=infra stage=fields", cold_failure["detail"])
            self.assertLess(cold_processes[0].poll_count, 10)
            self.thaw(snapshot)
            self.thaw(pass_root)
            self.thaw(rejected_root)
            self.thaw(fast_root)
            self.thaw(timeout_root)
            self.thaw(cold_timeout_root)
            self.thaw(cold_failure_root)
            self.thaw(work / "immediate-exit-run")
            self.thaw(work / "post-write-infra-run")

    def test_intermediate_symlink_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            real = work / "real"
            real.mkdir()
            (real / "input").write_bytes(b"bytes")
            (work / "alias").symlink_to(real, target_is_directory=True)
            with self.assertRaisesRegex(controller.CorpusError, "symlink path component"):
                controller.regular_file_record(work / "alias/input")

    def test_symlinked_attempt_parent_is_rejected_before_outside_mkdir(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            run_root = work / "run"
            outside = work / "outside"
            run_root.mkdir()
            outside.mkdir()
            (run_root / "runs").symlink_to(outside, target_is_directory=True)
            with self.assertRaisesRegex(controller.CorpusError, "symlink path component"):
                controller.next_attempt_directory(run_root, "lmctf01")
            self.assertEqual([], list(outside.iterdir()))

    def test_symlinked_snapshot_root_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            alias = work / "snapshot-alias"
            alias.symlink_to(snapshot, target_is_directory=True)
            with self.assertRaisesRegex(controller.CorpusError, "symlink path component"):
                controller.verify_snapshot(alias)
            self.thaw(snapshot)

    def test_misleading_module_and_asset_names_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            module_snapshot = self.make_snapshot(work / "module")
            module_manifest_path = module_snapshot / "input-manifest.json"
            module_manifest = json.loads(module_manifest_path.read_text())
            module_snapshot.chmod(0o700)
            (module_snapshot / "game").chmod(0o700)
            module_manifest_path.chmod(0o600)
            old_module = module_snapshot / "game/gamex86_64.so"
            new_module = module_snapshot / "game/misleading.so"
            old_module.rename(new_module)
            for item in module_manifest["files"]:
                if item["role"] == "module_secondary":
                    item["path"] = "game/misleading.so"
            controller.atomic_write_json(module_manifest_path, module_manifest, mode=0o444)
            (module_snapshot / "game").chmod(0o555)
            module_snapshot.chmod(0o555)
            with self.assertRaisesRegex(controller.CorpusError, "production names"):
                controller.verify_snapshot(module_snapshot)

            asset_snapshot = self.make_snapshot(work / "asset")
            asset_manifest_path = asset_snapshot / "input-manifest.json"
            asset_manifest = json.loads(asset_manifest_path.read_text())
            asset_snapshot.chmod(0o700)
            (asset_snapshot / "assets").chmod(0o700)
            asset_manifest_path.chmod(0o600)
            old_asset = asset_snapshot / "assets/lmctf01.bsp"
            new_asset = asset_snapshot / "assets/not-the-map.pak"
            old_asset.rename(new_asset)
            for item in asset_manifest["files"]:
                if item["role"] == "asset:lmctf01":
                    item["path"] = "assets/not-the-map.pak"
            controller.atomic_write_json(asset_manifest_path, asset_manifest, mode=0o444)
            (asset_snapshot / "assets").chmod(0o555)
            asset_snapshot.chmod(0o555)
            with self.assertRaisesRegex(controller.CorpusError, "non-extracted"):
                controller.verify_snapshot(asset_snapshot)
            self.thaw(module_snapshot)
            self.thaw(asset_snapshot)

    def test_dry_run_prints_fingerprint_and_every_assignment_without_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)

            def dry_run() -> list[str]:
                output = io.StringIO()
                with mock.patch.object(
                    controller.subprocess, "Popen",
                    side_effect=AssertionError("launch forbidden"),
                ), contextlib.redirect_stdout(output):
                    self.assertEqual(
                        0, controller.main(["dry-run", "--snapshot", str(snapshot)])
                    )
                return output.getvalue().splitlines()

            lines = dry_run()
            self.assertEqual(lines, dry_run())
            assignments = [line for line in lines if re.fullmatch(r"[0-9]{3}\t[^\t]+\t[0-9]+", line)]
            self.assertTrue(lines[0].startswith("fingerprint="))
            self.assertIsNone(json.loads(lines[1])["generation_timeout_seconds"])
            self.assertEqual(175, len(assignments))
            self.assertEqual("000\tlmctf01\t62000", assignments[0])
            self.assertEqual("174\txmap30\t62174", assignments[-1])
            self.thaw(snapshot)

    def test_cold_load_timeout_cli_default_and_override(self):
        parser = controller.build_parser()
        default = parser.parse_args(["dry-run", "--snapshot", "/tmp/snapshot"])
        overridden = parser.parse_args([
            "dry-run", "--snapshot", "/tmp/snapshot",
            "--cold-load-timeout", "420",
        ])
        self.assertEqual(300, default.cold_load_timeout)
        self.assertEqual(420, overridden.cold_load_timeout)

    def test_generation_timeout_defaults_to_none_and_rejects_nonpositive_values(self):
        parser = controller.build_parser()
        defaults = (
            parser.parse_args(["dry-run", "--snapshot", "/tmp/snapshot"]),
            parser.parse_args([
                "run", "--snapshot", "/tmp/snapshot",
                "--run-root", "/tmp/run",
            ]),
            parser.parse_args([
                "smoke", "--snapshot", "/tmp/snapshot",
                "--run-root", "/tmp/run", "lmctf01",
            ]),
        )
        overridden = parser.parse_args([
            "dry-run", "--snapshot", "/tmp/snapshot",
            "--generation-timeout", "420",
        ])
        self.assertTrue(all(item.generation_timeout is None for item in defaults))
        self.assertEqual(420, overridden.generation_timeout)
        with tempfile.TemporaryDirectory() as temporary:
            snapshot = self.make_snapshot(Path(temporary))
            arguments = {
                "startup_timeout": 10,
                "cold_load_timeout": 300,
                "jobs": 1,
                "port_base": 62000,
            }
            document, fingerprint = controller.build_fingerprint_document(
                snapshot, generation_timeout=None, **arguments
            )
            self.assertIsNone(document["generation_timeout_seconds"])
            self.assertEqual(
                fingerprint,
                controller.verify_fingerprint_document(snapshot, document),
            )
            for value in (0, -1):
                with self.subTest(generation_timeout=value):
                    with self.assertRaisesRegex(controller.CorpusError, "generation timeout"):
                        controller.build_fingerprint_document(
                            snapshot, generation_timeout=value, **arguments
                        )
                    invalid = dict(document)
                    invalid["generation_timeout_seconds"] = value
                    with self.assertRaisesRegex(controller.CorpusError, "fingerprint document"):
                        controller.verify_fingerprint_document(snapshot, invalid)
            self.thaw(snapshot)

    def test_atomic_publication_syncs_temp_published_file_and_parent(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "durable.json"
            synced: list[int] = []
            replaced: list[tuple[Path, Path]] = []

            def fsync(fd: int):
                synced.append(stat.S_IFMT(os.fstat(fd).st_mode))

            def replace(source, destination):
                replaced.append((Path(source), Path(destination)))
                os.replace(source, destination)

            controller.atomic_write_bytes(path, b"{}\n", fsync=fsync, replace=replace)
            self.assertEqual(b"{}\n", path.read_bytes())
            self.assertEqual(1, len(replaced))
            self.assertGreaterEqual(synced.count(stat.S_IFREG), 2)
            self.assertGreaterEqual(synced.count(stat.S_IFDIR), 1)

    def test_port_collision_is_fail_closed(self):
        occupied = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        occupied.bind(("127.0.0.1", 0))
        occupied.listen(1)
        port = occupied.getsockname()[1]
        try:
            with self.assertRaisesRegex(controller.CorpusError, "TCP port"):
                controller.preflight_ports([port])
        finally:
            occupied.close()

    def test_exact_pid_identity_includes_start_executable_and_command(self):
        identity = controller.capture_process_identity(os.getpid())
        self.assertTrue(controller.process_identity_matches(identity))
        self.assertFalse(controller.process_identity_matches(
            dataclasses.replace(identity, start_ticks=identity.start_ticks + 1)
        ))
        self.assertFalse(controller.process_identity_matches(
            dataclasses.replace(identity, cmdline_sha256="0" * 64)
        ))

    def test_exact_nul_argv_distinguishes_trailing_empty_reordered_and_non_ascii(self):
        commands = (
            ["engine", "+map", "runetest"],
            ["engine", "+map", "runetest", ""],
            ["engine", "runetest", "+map"],
            ["engine", "+map", "rüňetest"],
        )
        encoded = [controller._nul_argv(command) for command in commands]
        self.assertTrue(all(value.endswith(b"\0") for value in encoded))
        self.assertEqual(4, len(set(encoded)))
        self.assertEqual(4, len({controller.sha256_bytes(value) for value in encoded}))

    def test_ownership_mismatch_never_signals(self):
        identity = controller.capture_process_identity(os.getpid())
        with tempfile.TemporaryDirectory() as temporary:
            owner = Path(temporary) / "owner.json"
            controller.atomic_write_json(owner, {
                "process": dataclasses.replace(identity, start_ticks=identity.start_ticks + 1).as_dict()
            })
            calls: list[tuple[int, int]] = []
            sent = controller.signal_owned_child(
                identity, owner, signal.SIGTERM, pidfd=123,
                sender=lambda fd, sig: calls.append((fd, sig)),
            )
            self.assertFalse(sent)
            self.assertEqual([], calls)

    def test_matching_owner_uses_descriptor_signal_for_cleanup(self):
        identity = controller.capture_process_identity(os.getpid())
        with tempfile.TemporaryDirectory() as temporary:
            owner = Path(temporary) / "owner.json"
            controller.atomic_write_json(owner, {"process": identity.as_dict()})
            calls: list[tuple[int, int]] = []
            with mock.patch.object(controller, "process_identity_matches", return_value=True):
                sent = controller.signal_owned_child(
                    identity, owner, signal.SIGTERM, pidfd=321,
                    sender=lambda fd, sig: calls.append((fd, sig)),
                )
            self.assertTrue(sent)
            self.assertEqual([(321, signal.SIGTERM)], calls)

    def test_stubborn_exact_child_cleanup_escalates_on_same_descriptor(self):
        class FakeProcess:
            def __init__(self):
                self.waits = 0

            def poll(self):
                return None

            def wait(self, timeout):
                self.waits += 1
                if self.waits == 1:
                    raise subprocess.TimeoutExpired("fake", timeout)
                return -signal.SIGKILL

        identity = controller.capture_process_identity(os.getpid())
        sent: list[int] = []
        with mock.patch.object(
            controller,
            "signal_owned_child",
            side_effect=lambda _identity, _owner, sig, **_kwargs: sent.append(sig) or True,
        ):
            controller.shutdown_captured_child(
                FakeProcess(), identity, Path("owner.json"), 44
            )
        self.assertEqual([signal.SIGTERM, signal.SIGKILL], sent)

    def test_anchored_generator_grammar_and_count_mismatch(self):
        with tempfile.TemporaryDirectory() as temporary:
            attempt = Path(temporary)
            artifact = attempt / "game/maps/gatecase.rune"
            artifact.parent.mkdir(parents=True)
            artifact.write_bytes(b"artifact")
            good = (
                "rune: topology status=0 contacts=10 contact_overflow=0 "
                "initial_crossing_contacts=2 initial_crossing_directions=4 "
                "owner_calls=4 proved_added=2 proved_present=0 "
                "owner_rejected=2 owner_deferred=0 unexamined=0 "
                "unresolved_contacts=0 unresolved_directions=0 "
                "initial_sccs=3 final_sccs=1 scc_builds=2 added_links=2\n"
                "rune: objective roots red=1 blue=2\n"
                "rune: wrote game/maps/gatecase.rune (7 seeds, 9 links, "
                "4 mechanism nodes, 2 triggers, 3 inventory edges, 5 activation plans)\n"
                "slipgate: rune ready gatecase, 7 seeds, 9 links, 4 mechanism "
                "nodes, 5 plans, gravity 800, all fields up\n"
            )
            parsed = controller.parse_generation_log(good, "gatecase", artifact, attempt)
            self.assertEqual(5, parsed["counts"]["plans"])
            self.assertEqual(0, parsed["topology"]["unexamined"])
            route_only = good.replace(
                "rune: objective roots red=1 blue=2\n",
                "rune: late-path status=open-exhausted selectors=9 scheduled=514 "
                "pairs=10000 proofs=42 accepted=3 rejected=39 rebuilds=9 "
                "max_regions=20 links=3\n"
                "rune: objective roots red=1 blue=2\n",
            )
            parsed_route_only = controller.parse_generation_log(
                route_only, "gatecase", artifact, attempt
            )
            self.assertEqual("open-exhausted", parsed_route_only["late_path"]["status"])
            self.assertEqual(514, parsed_route_only["late_path"]["scheduled"])
            route_only_lines = route_only.splitlines()
            late_before_topology = "\n".join(
                [route_only_lines[1], route_only_lines[0], *route_only_lines[2:]]
            ) + "\n"
            with self.assertRaisesRegex(controller.CorpusError, "follow topology"):
                controller.parse_generation_log(
                    late_before_topology,
                    "gatecase", artifact, attempt,
                )
            with self.assertRaisesRegex(controller.CorpusError, "open-exhausted"):
                controller.parse_generation_log(
                    route_only.replace("open-exhausted", "open-budget"),
                    "gatecase", artifact, attempt,
                )
            deferred = good.replace(
                "slipgate: rune ready gatecase, 7 seeds, 9 links, 4 mechanism "
                "nodes, 5 plans, gravity 800, all fields up\n",
                "slipgate: snag declaration missing or invalid for map gatecase; "
                "fields rejected\n"
                "slipgate: field setup failed (no flags?); disabled until the "
                "next level\n",
            )
            parsed = controller.parse_generation_log(
                deferred, "gatecase", artifact, attempt
            )
            self.assertEqual(5, parsed["counts"]["plans"])
            self.assertTrue(controller.generation_deferred_publication_complete(
                deferred.splitlines(), "gatecase"
            ))
            interleaved = deferred.replace(
                "slipgate: snag declaration missing or invalid for map gatecase; "
                "fields rejected\n",
                "CBTWHY frames=0 seen=0 fire=0\n"
                "CBTSCAN unteamed=0 same=0 acquired=0\n"
                "slipgate: snag declaration missing or invalid for map gatecase; "
                "fields rejected\n",
            )
            self.assertTrue(controller.generation_deferred_publication_complete(
                interleaved.splitlines(), "gatecase"
            ))
            self.assertEqual(
                5,
                controller.parse_generation_log(
                    interleaved, "gatecase", artifact, attempt
                )["counts"]["plans"],
            )
            invalid_deferred = (
                deferred.replace("map gatecase", "map wrongmap"),
                deferred.replace(
                    "slipgate: snag declaration missing or invalid for map gatecase; "
                    "fields rejected\n"
                    "slipgate: field setup failed (no flags?); disabled until the "
                    "next level\n",
                    "slipgate: field setup failed (no flags?); disabled until the "
                    "next level\n"
                    "slipgate: snag declaration missing or invalid for map gatecase; "
                    "fields rejected\n",
                ),
                deferred.replace(
                    "rune: wrote game/maps/gatecase.rune (7 seeds, 9 links, "
                    "4 mechanism nodes, 2 triggers, 3 inventory edges, "
                    "5 activation plans)\n",
                    "",
                ),
            )
            for invalid in invalid_deferred:
                with self.subTest(invalid=invalid.splitlines()[-2:]):
                    self.assertFalse(
                        controller.generation_deferred_publication_complete(
                            invalid.splitlines(), "gatecase"
                        )
                    )
                    with self.assertRaisesRegex(
                        controller.CorpusError,
                        "generation completion|final write",
                    ):
                        controller.parse_generation_log(
                            invalid, "gatecase", artifact, attempt
                        )
            with self.assertRaisesRegex(controller.CorpusError, "objective-root"):
                controller.parse_generation_log(
                    good.replace(
                        "rune: objective roots red=1 blue=2",
                        "prefix rune: objective roots red=1 blue=2",
                    ),
                    "gatecase", artifact, attempt,
                )
            bad_later = good + "rune: generation refused malformed graph\n"
            with self.assertRaisesRegex(controller.CorpusError, "failure after write"):
                controller.parse_generation_log(bad_later, "gatecase", artifact, attempt)
            with self.assertRaisesRegex(controller.CorpusError, "count mismatch"):
                controller.validate_gate_agreement(
                    report("gatecase"), report("gatecase"),
                    report("gatecase", seeds=8),
                    "gatecase", parsed["counts"],
                )
            traversing = good.replace(
                "game/maps/gatecase.rune", "game/maps/../maps/gatecase.rune"
            )
            with self.assertRaisesRegex(controller.CorpusError, "safe relative"):
                controller.parse_generation_log(traversing, "gatecase", artifact, attempt)
            absolute = good.replace("game/maps/gatecase.rune", str(artifact))
            with self.assertRaisesRegex(controller.CorpusError, "safe relative"):
                controller.parse_generation_log(absolute, "gatecase", artifact, attempt)

    def test_exact_failure_record_and_systemic_signature(self):
        first = (
            "rune: rejected game/maps/lmctf03.rune stage=door-replay "
            "reason=invalid live declared-door replay index=9949"
        )
        second = (
            "rune: rejected game/maps/xmap19.rune stage=door-replay "
            "reason=invalid live declared-door replay index=30459"
        )
        route_core = "rune: FAILED: flag objectives share no closed route core"
        self.assertEqual(
            first,
            controller.last_anchored_failure(["unrelated", first]),
        )
        self.assertEqual(
            second,
            controller.last_anchored_failure([first, "progress", second]),
        )
        self.assertIsNone(controller.last_anchored_failure(["not rune: FAILED"]))
        self.assertEqual(
            controller.normalized_signature("GEN_FAIL", first),
            controller.normalized_signature("GEN_FAIL", second),
        )
        self.assertNotEqual(
            controller.normalized_signature("GEN_FAIL", first),
            controller.normalized_signature("GEN_FAIL", route_core),
        )

    def test_gate_cli_is_current_and_same_bytes_are_checked(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            artifact = work / "gatecase.rune"
            artifact.write_bytes(b"artifact")
            interpreter = work / "python-runtime/bin/python3.14"
            interpreter.parent.mkdir(parents=True)
            interpreter.write_bytes(b"private interpreter")
            interpreter.chmod(0o755)
            (work / "python-runtime/lib").mkdir()
            runner = FakeGateRunner("gatecase")
            hostile = {
                "PATH": str(work / "fake-bin"),
                "LD_PRELOAD": str(work / "evil.so"),
                "PYTHONPATH": str(work / "evil-python"),
                "MAKEFLAGS": "--eval=evil", "MFLAGS": "-j99",
                "GNUMAKEFLAGS": "--include-dir=evil",
            }
            with mock.patch.dict(os.environ, hostile, clear=False):
                result = controller.run_gates(
                    artifact, "gatecase",
                    {"seeds": 7, "links": 9, "mechanism_nodes": 4,
                     "triggers": 2, "inventory_edges": 3, "plans": 5},
                    acceptor_gnu=work / "runeaccept.gnu",
                    acceptor_make=work / "runeaccept.make",
                    python_interpreter=interpreter,
                    runeio=work / "runeio.py",
                    runelint=work / "runelint.py",
                    log_directory=work / "logs",
                    runner=runner,
                )
            self.assertEqual(str(interpreter), runner.commands[2][0])
            self.assertEqual(
                list(controller.PYTHON_ISOLATION_FLAGS), runner.commands[2][1:4]
            )
            self.assertEqual("-c", runner.commands[2][4])
            self.assertEqual(str(work), runner.commands[2][-3])
            self.assertEqual(str(work / "runeio.py"), runner.commands[2][-2])
            self.assertEqual(str(artifact), runner.commands[2][-1])
            self.assertEqual(str(work / "runelint.py"), runner.commands[3][-2])
            self.assertEqual(controller.CHILD_ENVIRONMENT, runner.kwargs[0]["env"])
            self.assertEqual(controller.CHILD_ENVIRONMENT, runner.kwargs[1]["env"])
            expected_python_env = controller.python_child_environment(
                work / "python-runtime"
            )
            self.assertEqual(expected_python_env, runner.kwargs[2]["env"])
            self.assertEqual(expected_python_env, runner.kwargs[3]["env"])
            forbidden = {
                "PATH", "LD_PRELOAD", "PYTHONPATH", "MAKEFLAGS", "MFLAGS",
                "GNUMAKEFLAGS",
            }
            self.assertTrue(all(forbidden.isdisjoint(kwargs["env"]) for kwargs in runner.kwargs))
            self.assertEqual(7, result["decoded_counts"]["seed_count"])
            self.assertEqual(4, len(result["gate_logs"]))

    def test_dual_c_readers_must_execute_and_agree(self):
        counts = {
            "seeds": 7, "links": 9, "mechanism_nodes": 4,
            "triggers": 2, "inventory_edges": 3, "plans": 5,
        }
        with self.assertRaisesRegex(controller.CorpusError, "GNU/Make C"):
            controller.validate_gate_agreement(
                report("gatecase"), report("gatecase", seeds=8),
                report("gatecase"), "gatecase", counts,
            )

    def test_applicable_semantic_checker_is_manifested_and_receives_roots(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            verified = controller.verify_snapshot(snapshot)
            artifact = work / "lmctf58.rune"
            artifact.write_bytes(b"artifact")
            checkers = controller.semantic_checkers_for_map(
                snapshot, verified, "lmctf58"
            )
            self.assertEqual(["lmctf58"], [name for name, _path in checkers])
            self.assertEqual([], controller.semantic_checkers_for_map(
                snapshot, verified, "lmctf01"
            ))
            runner = FakeGateRunner("lmctf58")
            result = controller.run_gates(
                artifact, "lmctf58",
                {"seeds": 7, "links": 9, "mechanism_nodes": 4,
                 "triggers": 2, "inventory_edges": 3, "plans": 5},
                acceptor_gnu=snapshot / verified["by_role"]["acceptor_gnu"]["path"],
                acceptor_make=snapshot / verified["by_role"]["acceptor_make"]["path"],
                python_interpreter=snapshot / verified["python_runtime"]["interpreter"]["path"],
                runeio=snapshot / verified["by_role"]["runeio"]["path"],
                runelint=snapshot / verified["by_role"]["runelint"]["path"],
                objective_roots={"red": 1, "blue": 2},
                semantic_checkers=checkers,
                log_directory=work / "logs",
                runner=runner,
                fingerprint="fingerprint",
            )
            self.assertEqual(["semantic-lmctf58"], result["semantic_gate_labels"])
            self.assertEqual(5, len(runner.commands))
            self.assertEqual(
                ["--objective-roots", "1", "2", str(artifact)],
                runner.commands[-1][-4:],
            )
            self.assertTrue((work / "logs/gate-semantic-lmctf58.integrity.json").is_file())
            self.thaw(snapshot)

    def test_semantic_coverage_diagnostic_cannot_reject_complete_artifact(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            verified = controller.verify_snapshot(snapshot)
            artifact = work / "lmctf58.rune"
            artifact.write_bytes(b"artifact")
            base_runner = FakeGateRunner("lmctf58")

            def runner(command, **kwargs):
                result = base_runner(command, **kwargs)
                if any(str(value).endswith("lmctf58_rune_accept.py")
                       for value in command):
                    return subprocess.CompletedProcess(
                        command, 1,
                        stdout=controller.canonical_json({
                            "map_name": "lmctf58",
                            "missing": ["optional-door"],
                        }),
                    )
                return result

            result = controller.run_gates(
                artifact, "lmctf58",
                {"seeds": 7, "links": 9, "mechanism_nodes": 4,
                 "triggers": 2, "inventory_edges": 3, "plans": 5},
                acceptor_gnu=snapshot / verified["by_role"]["acceptor_gnu"]["path"],
                acceptor_make=snapshot / verified["by_role"]["acceptor_make"]["path"],
                python_interpreter=snapshot / verified["python_runtime"]["interpreter"]["path"],
                runeio=snapshot / verified["by_role"]["runeio"]["path"],
                runelint=snapshot / verified["by_role"]["runelint"]["path"],
                objective_roots={"red": 1, "blue": 2},
                semantic_checkers=controller.semantic_checkers_for_map(
                    snapshot, verified, "lmctf58"
                ),
                log_directory=work / "logs",
                runner=runner,
                fingerprint="fingerprint",
            )
            self.assertEqual(["semantic-lmctf58"], result["semantic_gate_labels"])
            integrity = json.loads(
                (work / "logs/gate-semantic-lmctf58.integrity.json").read_text()
            )
            self.assertEqual(1, integrity["returncode"])
            self.thaw(snapshot)

    def test_semantic_diagnostic_process_failure_remains_infrastructure_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            verified = controller.verify_snapshot(snapshot)
            artifact = work / "lmctf58.rune"
            artifact.write_bytes(b"artifact")
            base_runner = FakeGateRunner("lmctf58")

            def runner(command, **kwargs):
                result = base_runner(command, **kwargs)
                if any(str(value).endswith("lmctf58_rune_accept.py")
                       for value in command):
                    return subprocess.CompletedProcess(command, 2, stdout=b"")
                return result

            with self.assertRaisesRegex(
                    controller.GateIntegrityError,
                    "semantic-lmctf58 diagnostic exited 2"):
                controller.run_gates(
                    artifact, "lmctf58",
                    {"seeds": 7, "links": 9, "mechanism_nodes": 4,
                     "triggers": 2, "inventory_edges": 3, "plans": 5},
                    acceptor_gnu=snapshot / verified["by_role"]["acceptor_gnu"]["path"],
                    acceptor_make=snapshot / verified["by_role"]["acceptor_make"]["path"],
                    python_interpreter=snapshot / verified["python_runtime"]["interpreter"]["path"],
                    runeio=snapshot / verified["by_role"]["runeio"]["path"],
                    runelint=snapshot / verified["by_role"]["runelint"]["path"],
                    objective_roots={"red": 1, "blue": 2},
                    semantic_checkers=controller.semantic_checkers_for_map(
                        snapshot, verified, "lmctf58"
                    ),
                    log_directory=None,
                    runner=runner,
                    fingerprint="fingerprint",
                )
            self.thaw(snapshot)

    def test_cold_load_grammar_rejects_generation_and_count_drift(self):
        ready = (
            "slipgate: objective roots red=1 blue=2\n"
            "slipgate: rune ready lmctf01, 7 seeds, 9 links, 4 mechanism "
            "nodes, 5 plans, gravity 800, all fields up\n"
        )
        counts = {"seeds": 7, "links": 9, "mechanism_nodes": 4, "plans": 5}
        self.assertEqual(
            {"counts": counts, "objective_roots": {"red": 1, "blue": 2}},
            controller.parse_cold_load_log(ready, "lmctf01", counts)
        )
        with self.assertRaisesRegex(controller.CorpusError, "unexpectedly generated"):
            controller.parse_cold_load_log(
                "rune: wrote game/maps/lmctf01.rune (7 seeds, 9 links, "
                "4 mechanism nodes, 2 triggers, 3 inventory edges, "
                "5 activation plans)\n" + ready,
                "lmctf01", counts,
            )
        with self.assertRaisesRegex(controller.CorpusError, "counts disagree"):
            controller.parse_cold_load_log(
                ready.replace("7 seeds", "8 seeds"), "lmctf01", counts
            )
        with self.assertRaisesRegex(
            controller.CorpusError, "snag declaration missing or invalid"
        ):
            controller.parse_cold_load_log(
                "slipgate: snag declaration missing or invalid for map lmctf01; "
                "fields rejected\n"
                "slipgate: field setup failed (no flags?); disabled until the "
                "next level\n",
                "lmctf01",
                counts,
            )

    def test_incremental_log_reader_rejects_replacement_and_truncation(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "server.log"
            path.write_bytes(b"one\npartial")
            reader = controller.IncrementalLineReader(path)
            self.assertEqual(["one"], reader.read_new())
            with path.open("ab") as stream:
                stream.write(b" two\n")
            self.assertEqual(["partial two"], reader.read_new())
            path.write_bytes(b"short\n")
            with self.assertRaisesRegex(controller.CorpusError, "truncated"):
                reader.read_new()
            replacement = Path(temporary) / "replacement.log"
            replacement.write_bytes(b"other\n")
            replacement.replace(path)
            with self.assertRaisesRegex(controller.CorpusError, "replaced"):
                reader.read_new()

    def test_incremental_log_reader_drains_short_reads_and_binds_final_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "server.log"
            path.write_bytes(b"one\ntwo\n")
            reader = controller.IncrementalLineReader(path)
            original_read = os.read
            with mock.patch.object(
                controller.os, "read",
                side_effect=lambda fd, count: original_read(fd, min(count, 1)),
            ):
                self.assertEqual(["one", "two"], reader.read_new())
            record = reader.bound_record()
            replacement = Path(temporary) / "replacement.log"
            replacement.write_bytes(b"one\ntwo\n")
            replacement.replace(path)
            with self.assertRaisesRegex(controller.CorpusError, "replaced"):
                reader.verify_named_record(record)
            reader.close()

    def test_incremental_log_reader_rejects_same_inode_rewrite_after_parse(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "server.log"
            path.write_bytes(b"one\n")
            reader = controller.IncrementalLineReader(path)
            self.assertEqual(["one"], reader.read_new())
            with path.open("r+b") as stream:
                stream.write(b"two\n")
                stream.flush()
                os.fsync(stream.fileno())
            with self.assertRaisesRegex(controller.CorpusError, "changed after it was parsed"):
                reader.bound_record()
            reader.close()

    def test_cold_load_bootstrap_snag_uses_frozen_tool_and_exact_artifact(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            attempt = work / "attempt"
            artifact = attempt / "private/game/maps/lmctf01.rune"
            artifact.parent.mkdir(parents=True)
            artifact.write_bytes(b"authenticated-rune-bytes")
            before = controller.regular_file_record(artifact)

            heartbeat_check = mock.Mock()

            def verified(_snapshot, _layout, label, target, arguments, **kwargs):
                self.assertEqual(label, "snag-bootstrap")
                self.assertIs(heartbeat_check, kwargs["heartbeat_check"])
                roles = controller.verify_snapshot(snapshot)["by_role"]
                self.assertEqual(
                    target, snapshot / roles["snagrepair"]["path"])
                self.assertEqual(arguments[:3], [
                    "--explicit-zero", "--map", "lmctf01"])
                self.assertEqual(arguments[3:5], ["--rune", str(artifact)])
                output = Path(arguments[arguments.index("--output") + 1])
                evidence = Path(
                    arguments[arguments.index("--evidence-manifest") + 1])
                output.write_text(
                    bootstrap_snag_text(
                        "lmctf01",
                        controller.sha256_regular(artifact),
                        controller.sha256_regular(evidence),
                    ),
                    encoding="ascii",
                )
                return 0, b"", {"ready": {}, "done": {}}

            with mock.patch.object(
                    controller, "run_verified_python", side_effect=verified):
                result = controller.stage_bootstrap_snag(
                    attempt, snapshot, "lmctf01", artifact, "f" * 64,
                    heartbeat_check=heartbeat_check,
                )
            self.assertEqual(controller.regular_file_record(artifact), before)
            self.assertEqual(result["snag"]["mode"], 0o444)
            self.assertEqual(result["evidence"]["mode"], 0o444)
            evidence = json.loads(
                (attempt / "snag-bootstrap-evidence.json").read_text())
            self.assertEqual(evidence, {
                "artifact_sha256": before["sha256"],
                "classification": "NO_ACCEPTED_OBSERVATION",
                "fingerprint": "f" * 64,
                "format": "lmctf-snag-bootstrap-v1",
                "map": "lmctf01",
            })
            (attempt / "snag-bootstrap-evidence.json").chmod(0o644)
            (attempt / "snag-bootstrap-evidence.json").write_text(
                '{"changed":true}\n', encoding="ascii")
            with self.assertRaisesRegex(
                    controller.GateIntegrityError, "changed its bootstrap"):
                controller._validate_retained_bootstrap_snag(
                    result,
                    artifact_sha256=before["sha256"],
                    fingerprint="f" * 64,
                    map_name="lmctf01",
                )
            self.thaw(snapshot)

    def test_cold_load_bootstrap_snag_observes_heartbeat_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            attempt = work / "attempt"
            artifact = attempt / "private/game/maps/lmctf01.rune"
            artifact.parent.mkdir(parents=True)
            artifact.write_bytes(b"authenticated-rune-bytes")
            with mock.patch.object(
                    controller,
                    "run_verified_python",
                    side_effect=lambda *_args, **kwargs: kwargs["heartbeat_check"](),
            ) as verified:
                with self.assertRaisesRegex(controller.CorpusError, "ticker write failed"):
                    controller.stage_bootstrap_snag(
                        attempt, snapshot, "lmctf01", artifact, "f" * 64,
                        heartbeat_check=lambda: (_ for _ in ()).throw(
                            controller.CorpusError("ticker write failed")
                        ),
                    )
            self.assertIsNotNone(verified.call_args.kwargs["heartbeat_check"])
            self.thaw(snapshot)

    def test_cold_load_bootstrap_snag_rejects_gate_and_output_drift(self):
        scenarios = (
            ("exit", controller.CorpusError),
            ("stdout", controller.CorpusError),
            ("missing", controller.GateIntegrityError),
            ("artifact", controller.GateIntegrityError),
            ("hardlink", controller.GateIntegrityError),
            ("wrong-evidence", controller.CorpusError),
        )
        for scenario, error_type in scenarios:
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as temporary:
                work = Path(temporary)
                snapshot = self.make_snapshot(work)
                attempt = work / "attempt"
                artifact = attempt / "private/game/maps/lmctf01.rune"
                artifact.parent.mkdir(parents=True)
                artifact.write_bytes(b"authenticated-rune-bytes")

                def verified(_snapshot, _layout, _label, _target, arguments, **_kwargs):
                    output = Path(arguments[arguments.index("--output") + 1])
                    evidence = Path(
                        arguments[arguments.index("--evidence-manifest") + 1])
                    if scenario == "exit":
                        return 7, b"", {"ready": {}, "done": {}}
                    if scenario == "missing":
                        return 0, b"", {"ready": {}, "done": {}}
                    if scenario == "hardlink":
                        os.link(artifact, output)
                    else:
                        evidence_hash = (
                            "0" * 64 if scenario == "wrong-evidence"
                            else controller.sha256_regular(evidence)
                        )
                        output.write_text(
                            bootstrap_snag_text(
                                "lmctf01",
                                controller.sha256_regular(artifact),
                                evidence_hash,
                            ),
                            encoding="ascii",
                        )
                    if scenario == "artifact":
                        artifact.write_bytes(b"changed-rune-bytes")
                    return (
                        0,
                        b"unexpected output" if scenario == "stdout" else b"",
                        {"ready": {}, "done": {}},
                    )

                with mock.patch.object(
                        controller, "run_verified_python", side_effect=verified):
                    with self.assertRaises(error_type):
                        controller.stage_bootstrap_snag(
                            attempt, snapshot, "lmctf01", artifact, "f" * 64)
                self.thaw(snapshot)

        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            attempt = work / "attempt"
            artifact = attempt / "private/game/maps/lmctf01.rune"
            artifact.parent.mkdir(parents=True)
            artifact.write_bytes(b"authenticated-rune-bytes")
            artifact.with_suffix(".snag").write_bytes(b"preexisting")
            with mock.patch.object(controller, "run_verified_python") as runner:
                with self.assertRaisesRegex(
                        controller.GateIntegrityError, "already exists"):
                    controller.stage_bootstrap_snag(
                        attempt, snapshot, "lmctf01", artifact, "f" * 64)
            runner.assert_not_called()
            self.thaw(snapshot)

    def test_contaminated_pass_is_not_resumable(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            run_root = work / "run"
            attempt = run_root / "runs/gatecase/attempt-0001"
            artifact = attempt / "private/game/maps/gatecase.rune"
            artifact.parent.mkdir(parents=True)
            artifact.write_bytes(b"artifact")
            engine = attempt / "private" / controller.CORPUS_ENGINE_BASENAME
            engine.write_bytes(b"fake engine")
            engine.chmod(0o755)
            owner = attempt / "owner.json"
            server = attempt / "server.log"
            controller.atomic_write_json(owner, {
                "fingerprint": "pending", "map": "gatecase", "attempt": 1,
                "created_at": controller.utc_now(), "process": {"pid": 9},
            })
            controller.atomic_write_bytes(server, b"terminal\n")
            for label in ("c_gnu", "c_make", "python", "lint"):
                controller.atomic_write_bytes(attempt / f"gate-{label}.log", label.encode())
            document, fingerprint = controller.build_fingerprint_document(
                snapshot,
                startup_timeout=10,
                generation_timeout=900,
                cold_load_timeout=300,
                jobs=1,
                port_base=62000,
            )
            document_bytes = controller.canonical_json(document)
            controller.atomic_write_bytes(run_root / "fingerprint-document.json", document_bytes)
            controller.atomic_write_json(owner, {
                "fingerprint": fingerprint, "map": "gatecase", "attempt": 1,
                "created_at": controller.utc_now(), "pidfd_captured": True,
                "command_sha256": "a" * 64,
                "process": dataclasses.replace(
                    controller.capture_process_identity(os.getpid()),
                    executable=str(engine.resolve(strict=True)),
                    executable_sha256=controller.sha256_regular(engine),
                    cmdline_sha256="a" * 64,
                ).as_dict(),
            })
            evidence = [
                controller._relative_evidence_record(path, run_root)
                for path in sorted(attempt.rglob("*")) if path.is_file()
            ]
            artifact_record = next(
                item for item in evidence if item["path"].endswith("gatecase.rune")
            )
            gate_logs = {
                label: next(
                    item["sha256"] for item in evidence
                    if item["path"].endswith(f"gate-{label}.log")
                )
                for label in ("c_gnu", "c_make", "python", "lint")
            }
            detail = "all generation and artifact gates passed"
            result = {
                "classification": "PASS",
                "fingerprint": fingerprint,
                "stable_port": 62000,
                "map": "gatecase",
                "attempt": 1,
                "started_at": controller.utc_now(),
                "ended_at": controller.utc_now(),
                "normalized_signature": controller.normalized_signature("PASS", detail),
                "detail": detail,
                "failure_line": None,
                "command_sha256": "a" * 64,
                "owner_record": str(owner.relative_to(run_root)),
                "artifact": artifact_record,
                "artifact_sha256": artifact_record["sha256"],
                "route_contract": "complete",
                "evidence": evidence,
                "server_log_sha256": controller.sha256_regular(server),
                "objective_roots": {"red": 1, "blue": 2},
                "banner_counts": {
                    "seeds": 7, "links": 9, "mechanism_nodes": 4,
                    "triggers": 2, "inventory_edges": 3, "plans": 5,
                },
                "decoded_counts": {
                    field: json.loads(report("gatecase"))[field]
                    for field in controller.REPORT_FIELDS
                },
                "gate_output_sha256": {
                    "c_gnu": controller.sha256_bytes(report("gatecase")),
                    "c_make": controller.sha256_bytes(report("gatecase")),
                    "python": controller.sha256_bytes(report("gatecase")),
                    "lint": controller.sha256_bytes(b""),
                },
                "gate_log_sha256": gate_logs,
                "semantic_gate_labels": [],
                "cold_load_owner_record": None,
                "cold_load_command_sha256": None,
                "cold_load_log_sha256": None,
                "cold_load_snag_record": None,
                "cold_load_snag_evidence_record": None,
            }
            result_path = controller.publish_result(run_root, "gatecase", result, attempt)
            runner = FakeGateRunner("gatecase")
            self.assertFalse(controller.validate_resumable_pass(
                result_path,
                run_root=run_root,
                fingerprint=fingerprint,
                fingerprint_document_bytes=document_bytes,
                stable_port=62000,
                snapshot=snapshot,
                gate_runner=runner,
            ))
            artifact.chmod(0o600)
            artifact.write_bytes(b"contaminated")
            self.assertFalse(controller.validate_resumable_pass(
                result_path,
                run_root=run_root,
                fingerprint=fingerprint,
                fingerprint_document_bytes=document_bytes,
                stable_port=62000,
                snapshot=snapshot,
                gate_runner=runner,
            ))
            self.thaw(snapshot)

    def test_incomplete_stale_pass_is_rejected_and_next_attempt_advances(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            snapshot = self.make_snapshot(work)
            run_root = work / "run"
            attempt = run_root / "runs/lmctf01/attempt-0001"
            attempt.mkdir(parents=True)
            incomplete = {
                "classification": "PASS", "fingerprint": "fingerprint",
                "stable_port": 62000, "map": "lmctf01", "attempt": 1,
            }
            controller.atomic_write_json(attempt / "result.json", incomplete)
            result_path = run_root / "runs/lmctf01/result.json"
            controller.atomic_write_json(result_path, incomplete)
            document, fingerprint = controller.build_fingerprint_document(
                snapshot, startup_timeout=10, generation_timeout=900,
                cold_load_timeout=300,
                jobs=1, port_base=62000,
            )
            document_bytes = controller.canonical_json(document)
            incomplete["fingerprint"] = fingerprint
            controller.atomic_write_json(attempt / "result.json", incomplete)
            controller.atomic_write_json(result_path, incomplete)
            controller.atomic_write_bytes(
                run_root / "fingerprint-document.json", document_bytes
            )
            self.assertFalse(controller.validate_resumable_pass(
                result_path,
                run_root=run_root,
                fingerprint=fingerprint,
                fingerprint_document_bytes=document_bytes,
                stable_port=62000,
                snapshot=snapshot,
                gate_runner=FakeGateRunner("lmctf01"),
            ))
            number, new_attempt = controller.next_attempt_directory(
                run_root, "lmctf01"
            )
            self.assertEqual(2, number)
            self.assertEqual("attempt-0002", new_attempt.name)
            self.thaw(snapshot)

    def test_bounded_jobs_enforces_parallel_ceiling_and_serial_control(self):
        assignments = [{"map": str(index)} for index in range(8)]

        def observed(jobs: int) -> int:
            lock = threading.Lock()
            active = 0
            maximum = 0

            def worker(item):
                nonlocal active, maximum
                with lock:
                    active += 1
                    maximum = max(maximum, active)
                time.sleep(0.02)
                with lock:
                    active -= 1
                return item["map"]

            results = controller.run_bounded(assignments, jobs, worker)
            self.assertEqual(8, len(results))
            return maximum

        self.assertEqual(1, observed(1))
        self.assertEqual(3, observed(3))

    def test_full_corpus_scheduler_stably_alternates_first_pass_work(self):
        def assignment(map_name: str, kind: str) -> dict:
            return {
                "map": map_name,
                "work": controller.MapWork(kind, None, None),
            }

        def names(items):
            return [item["map"] for item in items]

        cases = (
            (
                [
                    assignment("R1", "generated_replacement"),
                    assignment("A1", "adopted_validation"),
                    assignment("M1", "generated_missing"),
                    assignment("R2", "generated_replacement"),
                    assignment("A2", "adopted_validation"),
                    assignment("M2", "generated_missing"),
                ],
                ["A1", "M1", "A2", "M2", "R1", "R2"],
            ),
            (
                [
                    assignment("M1", "generated_missing"),
                    assignment("R1", "generated_replacement"),
                    assignment("A1", "adopted_validation"),
                    assignment("A2", "adopted_validation"),
                    assignment("M2", "generated_missing"),
                    assignment("A3", "adopted_validation"),
                ],
                ["A1", "M1", "A2", "M2", "A3", "R1"],
            ),
            (
                [
                    assignment("A1", "adopted_validation"),
                    assignment("M1", "generated_missing"),
                    assignment("R1", "generated_replacement"),
                    assignment("M2", "generated_missing"),
                    assignment("A2", "adopted_validation"),
                    assignment("M3", "generated_missing"),
                ],
                ["A1", "M1", "A2", "M2", "M3", "R1"],
            ),
            (
                [
                    assignment("R1", "generated_replacement"),
                    assignment("A1", "adopted_validation"),
                    assignment("R2", "generated_replacement"),
                    assignment("A2", "adopted_validation"),
                ],
                ["A1", "A2", "R1", "R2"],
            ),
            (
                [
                    assignment("R1", "generated_replacement"),
                    assignment("M1", "generated_missing"),
                    assignment("R2", "generated_replacement"),
                    assignment("M2", "generated_missing"),
                ],
                ["M1", "M2", "R1", "R2"],
            ),
        )
        for source, expected in cases:
            with self.subTest(expected=expected):
                ordered = controller._order_full_corpus_assignments(source)
                self.assertEqual(expected, names(ordered))
                self.assertEqual(sorted(names(source)), sorted(names(ordered)))

        for kind in (
            "adopted_validation", "generated_missing", "generated_replacement",
        ):
            source = [assignment(f"{kind}-{index}", kind) for index in range(2)]
            with self.subTest(kind=kind):
                self.assertEqual(
                    names(source),
                    names(controller._order_full_corpus_assignments(source)),
                )
        source = [
            assignment("lmctf12", "generated_missing"),
            assignment("M1", "generated_missing"),
            assignment("tomb05", "generated_missing"),
            assignment("M2", "generated_missing"),
        ]
        self.assertEqual(
            ["M1", "M2", "lmctf12", "tomb05"],
            names(controller._order_full_corpus_assignments(source)),
        )
        self.assertEqual([], controller._order_full_corpus_assignments([]))

    def test_full_corpus_jobs_one_fails_before_preflight_or_engine_launch(self):
        maps = controller.validate_manifest()
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary) / "run"
            snapshot = Path(temporary) / "snapshot"
            with mock.patch.object(
                controller, "verify_snapshot", return_value={"adopted_runes": {}}
            ), mock.patch.object(
                controller,
                "preflight_python_runtime",
                side_effect=AssertionError("preflight must not run"),
            ) as preflight, mock.patch.object(
                controller.subprocess,
                "Popen",
                side_effect=AssertionError("engine must not launch"),
            ) as launch:
                with self.assertRaisesRegex(
                    controller.CorpusError, "full corpus run requires jobs >= 2"
                ):
                    controller.execute_selection(
                        snapshot=snapshot,
                        run_root=run_root,
                        selected_maps=maps,
                        port_base=62000,
                        startup_timeout=1,
                        generation_timeout=None,
                        cold_load_timeout=1,
                        jobs=1,
                        engine_arguments=controller.DEFAULT_ENGINE_ARGUMENTS,
                    )
            preflight.assert_not_called()
            launch.assert_not_called()

    def test_smoke_jobs_one_reaches_preflight(self):
        maps = controller.validate_manifest()
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary) / "run"
            snapshot = Path(temporary) / "snapshot"
            reached = controller.CorpusError("smoke reached preflight")
            with mock.patch.object(
                controller, "verify_snapshot", return_value={"adopted_runes": {}}
            ), mock.patch.object(
                controller,
                "build_fingerprint_document",
                return_value=({}, "f" * 64),
            ), mock.patch.object(
                controller, "preflight_python_runtime", side_effect=reached
            ) as preflight:
                with self.assertRaisesRegex(controller.CorpusError, "smoke reached"):
                    controller.execute_selection(
                        snapshot=snapshot,
                        run_root=run_root,
                        selected_maps=[maps[0]],
                        port_base=62000,
                        startup_timeout=1,
                        generation_timeout=None,
                        cold_load_timeout=1,
                        jobs=1,
                        engine_arguments=controller.DEFAULT_ENGINE_ARGUMENTS,
                    )
            preflight.assert_called_once()

    def test_live_heartbeat_is_monotonic_and_finishes_empty(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            publisher = controller.HeartbeatPublisher(
                run_root, "fingerprint", controller.CORPUS_SIZE
            )
            initial = json.loads((run_root / "heartbeat.json").read_text())
            publisher.event("active", "lmctf01", {"attempt": 1, "process": {"pid": 9}})
            active = json.loads((run_root / "heartbeat.json").read_text())
            publisher.event("beat", "lmctf01")
            beat = json.loads((run_root / "heartbeat.json").read_text())
            publisher.event("terminal", "lmctf01")
            publisher.finish(1, False)
            final = json.loads((run_root / "heartbeat.json").read_text())
            self.assertLess(initial["sequence"], active["sequence"])
            self.assertLess(active["sequence"], beat["sequence"])
            self.assertEqual("lmctf01", active["active"][0]["map"])
            self.assertIn("heartbeat_at", beat["active"][0])
            self.assertEqual([], final["active"])
            self.assertEqual(1, final["terminal"])

    def test_heartbeat_ticker_advances_nonchild_reader_stage(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            publisher = controller.HeartbeatPublisher(
                run_root, "f" * 64, 1, ticker_interval=0.01
            )
            try:
                publisher.event("active", "lmctf01", {
                    "attempt": 1, "stage": "readers", "heartbeat_at": "old",
                })
                time.sleep(0.03)
                value = json.loads((run_root / "heartbeat.json").read_text())
                active = value["active"]
                self.assertEqual("readers", active[0]["stage"])
                self.assertNotEqual("old", active[0]["heartbeat_at"])
                self.assertNotIn("process", active[0])
            finally:
                publisher.close()

    def test_stale_identity_mismatch_records_infra_without_signal(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            attempt = run_root / "runs/lmctf01/attempt-0001"
            attempt.mkdir(parents=True)
            identity = controller.capture_process_identity(os.getpid())
            owner = {
                "fingerprint": "fingerprint",
                "map": "lmctf01",
                "attempt": 1,
                "created_at": controller.utc_now(),
                "process": dataclasses.replace(
                    identity, start_ticks=identity.start_ticks + 1
                ).as_dict(),
            }
            controller.atomic_write_json(attempt / "owner.json", owner)
            controller.atomic_write_bytes(attempt / "server.log", b"partial\n")
            controller.write_attempt_intent(
                run_root, attempt, fingerprint="fingerprint", map_name="lmctf01",
                stable_port=62000, attempt_number=1,
                work=controller.MapWork("generated_missing", None, None),
            )
            assignments = {"lmctf01": {"map": "lmctf01", "port": 62000}}
            with mock.patch.object(
                controller, "recover_stale_owned_child"
            ) as recovery:
                controller.recover_stale_attempts(
                    run_root, ["lmctf01"], "fingerprint", assignments
                )
            recovery.assert_not_called()
            result = json.loads(
                (run_root / "runs/lmctf01/result.json").read_text(encoding="utf-8")
            )
            self.assertEqual("INFRA_FAIL", result["classification"])
            self.assertIn("left untouched", result["detail"])
            self.thaw(run_root)

    def test_already_dead_stale_child_records_infra_without_signal(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            attempt = run_root / "runs/lmctf01/attempt-0001"
            attempt.mkdir(parents=True)
            identity = dataclasses.replace(
                controller.capture_process_identity(os.getpid()), pid=2_000_000_000
            )
            controller.atomic_write_json(attempt / "owner.json", {
                "fingerprint": "fingerprint", "map": "lmctf01", "attempt": 1,
                "created_at": controller.utc_now(), "process": identity.as_dict(),
            })
            controller.write_attempt_intent(
                run_root, attempt, fingerprint="fingerprint", map_name="lmctf01",
                stable_port=62000, attempt_number=1,
                work=controller.MapWork("generated_missing", None, None),
            )
            assignments = {"lmctf01": {"map": "lmctf01", "port": 62000}}
            with mock.patch.object(
                controller, "signal_owned_child"
            ) as sender:
                count = controller.recover_stale_attempts(
                    run_root, ["lmctf01"], "fingerprint", assignments
                )
            sender.assert_not_called()
            self.assertEqual(1, count)
            result = json.loads((run_root / "runs/lmctf01/result.json").read_text())
            self.assertEqual("INFRA_FAIL", result["classification"])
            self.thaw(run_root)

    def test_preidentity_stale_attempt_recovers_terminal_without_signal(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            attempt = run_root / "runs/lmctf01/attempt-0001"
            attempt.mkdir(parents=True)
            controller.atomic_write_json(attempt / "owner.json", {
                "fingerprint": "fingerprint", "map": "lmctf01", "attempt": 1,
                "created_at": controller.utc_now(), "process": None,
                "pidfd_captured": False, "command_sha256": "c" * 64,
            })
            controller.write_attempt_intent(
                run_root, attempt, fingerprint="fingerprint", map_name="lmctf01",
                stable_port=62000, attempt_number=1,
                work=controller.MapWork("generated_missing", None, None),
            )
            assignments = {"lmctf01": {"map": "lmctf01", "port": 62000}}
            with mock.patch.object(
                controller, "signal_owned_child"
            ) as sender, mock.patch.object(
                controller, "recover_stale_owned_child"
            ) as recovery:
                count = controller.recover_stale_attempts(
                    run_root, ["lmctf01"], "fingerprint", assignments
                )
            sender.assert_not_called()
            recovery.assert_not_called()
            self.assertEqual(1, count)
            result_path = run_root / "runs/lmctf01/result.json"
            result = json.loads(result_path.read_text())
            self.assertEqual("INFRA_FAIL", result["classification"])
            self.assertIn("before process identity capture", result["detail"])
            self.assertIsNotNone(controller.validate_terminal_result(
                result_path,
                run_root=run_root,
                map_name="lmctf01",
                fingerprint="fingerprint",
                stable_port=62000,
            ))
            self.thaw(run_root)

    def test_preidentity_cold_load_owner_is_retryable_without_signal(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            attempt = run_root / "runs/lmctf01/attempt-0001"
            attempt.mkdir(parents=True)
            controller.write_attempt_intent(
                run_root, attempt, fingerprint="fingerprint", map_name="lmctf01",
                stable_port=62000, attempt_number=1,
                work=controller.MapWork("generated_missing", None, None),
            )
            controller.atomic_write_json(attempt / "cold-load/owner.json", {
                "fingerprint": "fingerprint", "map": "lmctf01", "attempt": 1,
                "created_at": controller.utc_now(), "process": None,
                "pidfd_captured": False, "command_sha256": "c" * 64,
            })
            assignments = {"lmctf01": {"map": "lmctf01", "port": 62000}}
            with mock.patch.object(controller, "recover_owned_child_to_exit") as recovery:
                self.assertEqual(1, controller.recover_stale_attempts(
                    run_root, ["lmctf01"], "fingerprint", assignments
                ))
            recovery.assert_not_called()
            result = json.loads((run_root / "runs/lmctf01/result.json").read_text())
            self.assertEqual("INFRA_FAIL", result["classification"])
            self.thaw(run_root)

    def test_summary_rejects_175_incomplete_infra_terminal_records(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            maps = controller.validate_manifest()
            for index, map_name in enumerate(maps):
                attempt = run_root / "runs" / map_name / "attempt-0001"
                attempt.mkdir(parents=True)
                owner = attempt / "owner.json"
                server = attempt / "server.log"
                controller.atomic_write_json(owner, {"map": map_name})
                controller.atomic_write_bytes(server, b"terminal\n")
                evidence = [
                    controller._relative_evidence_record(owner, run_root),
                    controller._relative_evidence_record(server, run_root),
                ]
                result = {
                    "fingerprint": "fingerprint", "map": map_name,
                    "stable_port": 62000 + index, "attempt": 1,
                    "classification": "INFRA_FAIL",
                    "owner_record": str(owner.relative_to(run_root)),
                    "server_log_sha256": controller.sha256_regular(server),
                    "evidence": evidence, "artifact": None,
                }
                controller.publish_result(run_root, map_name, result, attempt)
            summary = controller.regenerate_reports(
                run_root, maps, "fingerprint", "start", publish_heartbeat=False
            )
            self.assertFalse(summary["complete"])
            self.assertEqual(0, len(summary["maps"]))
            self.thaw(run_root)


if __name__ == "__main__":
    unittest.main()
