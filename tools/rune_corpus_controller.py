#!/usr/bin/env python3
"""Durable controller for the fixed 181-map RUNE corpus.

The controller is deliberately fail-closed.  ``dry-run`` never starts an
engine.  ``smoke`` selects exactly one map; ``run`` selects the fixed corpus.
Every real launch requires a read-only, byte-verified input snapshot.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import ctypes
import dataclasses
import datetime as _datetime
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import select
import selectors
import shutil
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import time
from typing import Any, Callable, Iterable, Mapping, Sequence


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "tools/rune-corpus-maps.txt"
EXPECTED_MANIFEST_SHA256 = (
    "9bc55cb287f0b9d99fccf54cc1339e65fba30459e49b0b77cf1b67896c125452"
)
CORPUS_SIZE = 181
DEFAULT_PORT_BASE = 62000
CORPUS_ENGINE_BASENAME = "q2ded-rune-corpus"
MAP_NAME_RE = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_-]{0,62}\Z")
ROOT_RE = re.compile(r"^rune: objective roots red=([0-9]+) blue=([0-9]+)$")
WRITE_RE = re.compile(
    r"^rune: wrote ([^\r\n]+) \(([0-9]+) seeds, ([0-9]+) links, "
    r"([0-9]+) mechanism nodes, ([0-9]+) triggers, ([0-9]+) inventory "
    r"edges, ([0-9]+) activation plans\)$"
)
READY_RE = re.compile(
    r"^slipgate: rune ready ([A-Za-z0-9_][A-Za-z0-9_-]{0,62}), "
    r"([0-9]+) seeds, ([0-9]+) links, ([0-9]+) mechanism nodes, "
    r"([0-9]+) plans, gravity -?[0-9]+, all fields up$"
)
FAILURE_RE = re.compile(
    r"^rune: (?:rejected .+|FAILED(?::| |$).*|generation refused .+|"
    r"revalidation failed .+|install failed .+|"
    r"cleanup restored pending door scope;.*)$"
)


def last_anchored_failure(lines: Sequence[str]) -> str | None:
    """Return the last exact fail-closed generator/runtime record, if any."""
    for line in reversed(lines):
        if FAILURE_RE.fullmatch(line):
            return line
    return None
REPORT_FIELDS = (
    "seed_count",
    "link_count",
    "node_count",
    "trigger_count",
    "inventory_edge_count",
    "plan_edge_count",
    "edge_count",
    "plan_count",
)
TERMINAL_CLASSIFICATIONS = frozenset(
    {"PASS", "LINT_FAIL", "GEN_FAIL", "TIMEOUT", "INFRA_FAIL"}
)
TERMINAL_RESULT_FIELDS = frozenset(
    {
        "fingerprint", "map", "stable_port", "attempt", "started_at", "ended_at",
        "classification", "normalized_signature", "detail", "failure_line",
        "command_sha256", "owner_record", "evidence", "server_log_sha256",
        "artifact", "artifact_sha256", "objective_roots", "banner_counts",
        "decoded_counts", "gate_output_sha256", "gate_log_sha256",
        "semantic_gate_labels", "cold_load_owner_record",
        "cold_load_command_sha256", "cold_load_log_sha256",
    }
)
REQUIRED_SNAPSHOT_ROLES = frozenset(
    {
        "engine",
        "module_primary",
        "module_secondary",
        "runelint",
        "runeio",
        "contracts",
        "acceptor_gnu",
        "acceptor_make",
        "generator_config",
        "map_manifest",
        "semantic_checker_manifest",
    }
)
SEMANTIC_CHECKER_ROLE_PREFIX = "semantic_checker:"
SEMANTIC_CHECKER_NAME_RE = re.compile(r"[a-z0-9][a-z0-9_-]{0,62}\Z")
REQUIRED_SEMANTIC_CHECKERS = {"lmctf58": ("lmctf58",)}
BASE_GATE_LABELS = frozenset({"c_gnu", "c_make", "python", "lint"})
PYTHON_RUNTIME_INPUT_ROLE = "python_runtime"
PYTHON_RUNTIME_ROLE_PREFIX = "python_runtime:"
PYTHON_RUNTIME_ROOT = PurePosixPath("python-runtime")
PYTHON_ISOLATION_FLAGS = ("-I", "-S", "-B")
PYTHON_GATE_BOOTSTRAP = (
    "import json,os,runpy,struct,sys;"
    "control,release,mode,target=int(sys.argv[1]),int(sys.argv[2]),sys.argv[3],sys.argv[4];"
    "sys.path.insert(0,os.path.dirname(target));"
    "namespace=runpy.run_path(target,run_name='__rune_gate__');"
    "main=namespace.get('main');"
    "assert callable(main),'target has no callable main';"
    "emit=lambda value:os.write(control,struct.pack('!I',len(json.dumps(value,sort_keys=True,separators=(',',':')).encode('ascii')))+json.dumps(value,sort_keys=True,separators=(',',':')).encode('ascii'));"
    "emit({'phase':'READY','mode':mode,'argv_sha256':__import__('hashlib').sha256(b'\\0'.join(os.fsencode(x) for x in sys.argv[5:])+b'\\0').hexdigest()});"
    "assert os.read(release,1)==b'R','release denied';"
    "rc=(exec(sys.argv[5],namespace) if mode=='preflight' else main(sys.argv[5:]));rc=0 if rc is None else int(rc);"
    "emit({'phase':'DONE','mode':mode,'rc':rc});"
    "assert os.read(release,1)==b'R','final release denied';"
    "raise SystemExit(rc)"
)
GUARD_BOOTSTRAP = (
    "import ctypes,os,signal,sys;"
    "parent=int(sys.argv[1]);command=sys.argv[3:];"
    "libc=ctypes.CDLL(None,use_errno=True);"
    "assert command and libc.prctl(1,signal.SIGKILL,0,0,0)==0 and os.getppid()==parent;"
    "os.environ.clear();os.environ.update({'LANG':'C','LC_ALL':'C','TZ':'UTC'});"
    "os.execv(command[0],command)"
)
ENGINE_ENVIRONMENT = {
    "LANG": "C",
    "LC_ALL": "C",
    "TZ": "UTC",
}
ACCEPTOR_ENVIRONMENT = dict(ENGINE_ENVIRONMENT)
PYTHON_ENVIRONMENT = dict(ENGINE_ENVIRONMENT)
# Compatibility name for consumers of the previous public helper.  It has no
# dynamic-loader authority: every Python invocation names its loader directly.
CHILD_ENVIRONMENT = ENGINE_ENVIRONMENT
PSEUDO_MAP_ALLOWLIST = frozenset({"[heap]", "[stack]", "[vdso]", "[vvar]", "[vvar_vclock]", "[vsyscall]"})
PYTHON_RUNTIME_PROBE = r"""
import _hashlib, _json, _struct, argparse, array, collections, concurrent.futures, ctypes, dataclasses, encodings, fcntl, hashlib, json, math, os, re, runpy, select, shutil, signal, socket, struct, sys, tempfile, threading, typing, zlib
def physical(path):
    return os.path.realpath(path) if path else None
loaded_libraries = []
with open('/proc/self/maps', encoding='ascii') as stream:
    for line in stream:
        candidate = line.rstrip().split()[-1]
        if candidate.startswith('/') and ('.so' in os.path.basename(candidate) or os.path.basename(candidate).startswith('ld-linux-')):
            resolved = physical(candidate)
            if resolved not in loaded_libraries:
                loaded_libraries.append(resolved)
print(json.dumps({
    'executable': physical(sys.executable),
    'prefix': physical(sys.prefix),
    'base_prefix': physical(sys.base_prefix),
    'modules': {
        'json': physical(json.__file__),
        'runpy': physical(runpy.__file__),
        'encodings': physical(encodings.__file__),
        '_json': physical(_json.__file__),
        'hashlib': physical(hashlib.__file__),
        '_hashlib': physical(_hashlib.__file__),
        'math': physical(math.__file__),
        'zlib': physical(zlib.__file__),
        'struct': physical(struct.__file__),
        '_struct': physical(_struct.__file__),
    },
    'loaded_libraries': sorted(loaded_libraries),
    'sys_path': list(sys.path),
    'dont_write_bytecode': sys.dont_write_bytecode,
}, sort_keys=True))
""".strip()
DEFAULT_ENGINE_ARGUMENTS = (
    "-portable",
    "+set", "game", "game",
    "+set", "dedicated", "1",
    "+set", "port", "{port}",
    "+exec", "{config}",
    "+map", "{map}",
)


class CorpusError(RuntimeError):
    """An invariant failed and the controller must not continue."""


class GateIntegrityError(CorpusError):
    """A gate launch or immutable input invariant failed (never a lint result)."""


class ProcessIntegrityError(GateIntegrityError):
    """The parent could not prove a live child and all of its mappings."""


def reject_symlink_components(path: Path) -> None:
    """Reject a link in any existing lexical component of *path*."""
    absolute = Path(os.path.abspath(path))
    current = Path(absolute.anchor)
    for part in absolute.parts[1:]:
        current = current / part
        try:
            info = current.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode):
            raise CorpusError(f"symlink path component is forbidden: {current}")


def utc_now() -> str:
    return _datetime.datetime.now(_datetime.timezone.utc).isoformat(
        timespec="microseconds"
    ).replace("+00:00", "Z")


def canonical_json(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("utf-8") + b"\n"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _open_regular(path: Path) -> tuple[int, os.stat_result]:
    reject_symlink_components(path)
    try:
        before = path.lstat()
    except OSError as exc:
        raise CorpusError(f"cannot stat input {path}: {exc}") from exc
    if not stat.S_ISREG(before.st_mode):
        raise CorpusError(f"not a regular file: {path}")
    flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
    try:
        fd = os.open(path, flags)
    except OSError as exc:
        raise CorpusError(f"cannot safely open regular file {path}: {exc}") from exc
    after = os.fstat(fd)
    if not stat.S_ISREG(after.st_mode) or (before.st_dev, before.st_ino) != (
        after.st_dev,
        after.st_ino,
    ):
        os.close(fd)
        raise CorpusError(f"input changed while opening: {path}")
    return fd, after


def read_regular_bytes(
    path: Path, *, logical_path: str | None = None
) -> tuple[bytes, dict[str, Any]]:
    """Read and describe one regular file through exactly one open descriptor."""
    fd, info = _open_regular(path)
    digest = hashlib.sha256()
    chunks: list[bytes] = []
    try:
        while True:
            chunk = os.read(fd, 1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
            chunks.append(chunk)
        final = os.fstat(fd)
    finally:
        os.close(fd)
    data = b"".join(chunks)
    if (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns) != (
        final.st_dev,
        final.st_ino,
        final.st_size,
        final.st_mtime_ns,
    ) or len(data) != final.st_size:
        raise CorpusError(f"input changed while hashing: {path}")
    return data, {
        "path": logical_path if logical_path is not None else str(path),
        "mode": stat.S_IMODE(final.st_mode),
        "size": len(data),
        "sha256": digest.hexdigest(),
    }


def regular_file_record(path: Path, *, logical_path: str | None = None) -> dict[str, Any]:
    _data, record = read_regular_bytes(path, logical_path=logical_path)
    return record


def sha256_regular(path: Path) -> str:
    return str(regular_file_record(path)["sha256"])


def atomic_write_bytes(
    path: Path,
    data: bytes,
    *,
    mode: int = 0o644,
    fsync: Callable[[int], None] = os.fsync,
    replace: Callable[[str | bytes | os.PathLike[str] | os.PathLike[bytes],
                      str | bytes | os.PathLike[str] | os.PathLike[bytes]], None] = os.replace,
) -> None:
    """Replace *path*, then sync both the published file and its directory."""
    reject_symlink_components(path.parent)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary_path = Path(temporary)
    try:
        with os.fdopen(fd, "wb", closefd=True) as stream:
            stream.write(data)
            stream.flush()
            os.fchmod(stream.fileno(), mode)
            fsync(stream.fileno())
        replace(temporary_path, path)
        published = os.open(
            path,
            os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0),
        )
        try:
            fsync(published)
        finally:
            os.close(published)
        directory = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            fsync(directory)
        finally:
            os.close(directory)
    finally:
        try:
            temporary_path.unlink()
        except FileNotFoundError:
            pass


def atomic_write_json(path: Path, value: Any, **kwargs: Any) -> None:
    atomic_write_bytes(path, canonical_json(value), **kwargs)


def fsync_tree(root: Path) -> None:
    """Sync regular files and directories bottom-up without following links."""
    for directory, names, files in os.walk(root, topdown=False, followlinks=False):
        current = Path(directory)
        for name in names + files:
            candidate = current / name
            info = candidate.lstat()
            if stat.S_ISLNK(info.st_mode):
                raise CorpusError(f"link found in evidence tree: {candidate}")
            if stat.S_ISREG(info.st_mode):
                fd = os.open(candidate, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
                try:
                    os.fsync(fd)
                finally:
                    os.close(fd)
            elif not stat.S_ISDIR(info.st_mode):
                raise CorpusError(f"unsupported evidence type: {candidate}")
        fd = os.open(current, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


def freeze_tree(root: Path) -> None:
    for directory, names, files in os.walk(root, topdown=False, followlinks=False):
        current = Path(directory)
        for name in files:
            path = current / name
            info = path.lstat()
            if not stat.S_ISREG(info.st_mode):
                raise CorpusError(f"unsupported snapshot file type: {path}")
            path.chmod(stat.S_IMODE(info.st_mode) & ~0o222)
        for name in names:
            path = current / name
            info = path.lstat()
            if not stat.S_ISDIR(info.st_mode):
                raise CorpusError(f"unsupported snapshot directory type: {path}")
            path.chmod(stat.S_IMODE(info.st_mode) & ~0o222)
        current.chmod(stat.S_IMODE(current.lstat().st_mode) & ~0o222)


def validate_manifest(path: Path = DEFAULT_MANIFEST) -> list[str]:
    data, record = read_regular_bytes(path)
    if record["sha256"] != EXPECTED_MANIFEST_SHA256:
        raise CorpusError(
            f"map manifest hash mismatch: {record['sha256']} != "
            f"{EXPECTED_MANIFEST_SHA256}"
        )
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as exc:
        raise CorpusError("map manifest is not ASCII") from exc
    maps = text.splitlines()
    if len(maps) != CORPUS_SIZE:
        raise CorpusError(f"map manifest has {len(maps)} names, expected {CORPUS_SIZE}")
    if len(set(maps)) != len(maps):
        raise CorpusError("map manifest contains duplicate names")
    for name in maps:
        if not MAP_NAME_RE.fullmatch(name):
            raise CorpusError(f"unsafe map name in manifest: {name!r}")
    if "lmctf02" not in maps or "lmctf02c" not in maps:
        raise CorpusError("manifest must retain both lmctf02 and lmctf02c")
    return maps


def stable_assignments(maps: Sequence[str], port_base: int = DEFAULT_PORT_BASE) -> list[dict[str, Any]]:
    if len(maps) != CORPUS_SIZE:
        raise CorpusError("stable assignments require the entire fixed corpus")
    if not (1 <= port_base <= 65535 - len(maps) + 1):
        raise CorpusError("port range is outside 1..65535")
    return [
        {"index": index, "map": name, "port": port_base + index}
        for index, name in enumerate(maps)
    ]


def _safe_logical_path(value: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if path.is_absolute() or not path.parts or any(part in ("", ".", "..") for part in path.parts):
        raise CorpusError(f"unsafe snapshot path: {value!r}")
    return path


def _expand_python_runtime_inputs(
    inputs: Mapping[str, tuple[str, Path]],
) -> dict[str, tuple[str, Path]]:
    runtime_items = [
        (logical, source)
        for logical, (role, source) in inputs.items()
        if role == PYTHON_RUNTIME_INPUT_ROLE
    ]
    if len(runtime_items) != 1 or runtime_items[0][0] != PYTHON_RUNTIME_ROOT.as_posix():
        raise CorpusError("snapshot requires one python_runtime@python-runtime prefix")
    _logical, runtime = runtime_items[0]
    reject_symlink_components(runtime)
    try:
        root_info = runtime.lstat()
    except OSError as exc:
        raise CorpusError(f"cannot stat Python runtime prefix {runtime}: {exc}") from exc
    if not stat.S_ISDIR(root_info.st_mode):
        raise CorpusError("Python runtime prefix is not a directory")
    expanded = {
        logical: value
        for logical, value in inputs.items()
        if value[0] != PYTHON_RUNTIME_INPUT_ROLE
    }
    runtime_files = 0
    for directory, names, files in os.walk(runtime, followlinks=False):
        current = Path(directory)
        for name in names:
            candidate = current / name
            if not stat.S_ISDIR(candidate.lstat().st_mode):
                raise CorpusError(f"Python runtime contains a link or special directory: {candidate}")
        for name in files:
            candidate = current / name
            if not stat.S_ISREG(candidate.lstat().st_mode):
                raise CorpusError(f"Python runtime contains a link or special file: {candidate}")
            relative = candidate.relative_to(runtime).as_posix()
            logical = (PYTHON_RUNTIME_ROOT / relative).as_posix()
            if logical in expanded:
                raise CorpusError(f"duplicate snapshot logical path: {logical}")
            expanded[logical] = (PYTHON_RUNTIME_ROLE_PREFIX + relative, candidate)
            runtime_files += 1
    if runtime_files == 0:
        raise CorpusError("Python runtime prefix is empty")
    return expanded


def load_semantic_checker_manifest(
    path: Path, maps: Sequence[str]
) -> list[dict[str, Any]]:
    """Load the closed map-to-checker authority from one regular file."""
    data, _record = read_regular_bytes(path)
    try:
        value = json.loads(data)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise CorpusError("semantic checker manifest is not valid JSON") from exc
    if not isinstance(value, dict) or set(value) != {"checkers"} or not isinstance(
        value["checkers"], list
    ):
        raise CorpusError("semantic checker manifest has an invalid schema")
    known_maps = set(maps)
    checkers: list[dict[str, Any]] = []
    seen_names: set[str] = set()
    for item in value["checkers"]:
        if not isinstance(item, dict) or set(item) != {"name", "maps", "role"}:
            raise CorpusError("semantic checker manifest entry has an invalid schema")
        name, role, checker_maps = item["name"], item["role"], item["maps"]
        if not isinstance(name, str) or SEMANTIC_CHECKER_NAME_RE.fullmatch(name) is None:
            raise CorpusError("semantic checker name is unsafe")
        if role != SEMANTIC_CHECKER_ROLE_PREFIX + name:
            raise CorpusError("semantic checker role/name mismatch")
        if name in seen_names:
            raise CorpusError("semantic checker names are not unique")
        if (
            not isinstance(checker_maps, list)
            or not checker_maps
            or checker_maps != sorted(set(checker_maps))
            or any(not isinstance(map_name, str) or map_name not in known_maps
                   for map_name in checker_maps)
        ):
            raise CorpusError("semantic checker map list is invalid")
        seen_names.add(name)
        checkers.append({"name": name, "maps": checker_maps, "role": role})
    if checkers != sorted(checkers, key=lambda item: item["name"]):
        raise CorpusError("semantic checker manifest is not name-sorted")
    by_name = {item["name"]: tuple(item["maps"]) for item in checkers}
    if any(by_name.get(name) != checker_maps for name, checker_maps in
           REQUIRED_SEMANTIC_CHECKERS.items()):
        raise CorpusError("semantic checker manifest omits required applicability")
    return checkers


def create_input_snapshot(
    output: Path,
    inputs: Mapping[str, tuple[str, Path]],
) -> dict[str, Any]:
    """Copy named regular inputs into a new immutable snapshot.

    ``inputs`` maps a unique logical name to ``(role, source)``.  Asset roles
    are spelled ``asset:<map>`` and must cover the fixed manifest exactly.
    """
    reject_symlink_components(output.parent)
    if output.exists() or output.is_symlink():
        raise CorpusError(f"snapshot output already exists: {output}")
    role_list = [role for role, _source in inputs.values() if not role.startswith("asset:")]
    semantic_roles = {
        role for role in role_list if role.startswith(SEMANTIC_CHECKER_ROLE_PREFIX)
    }
    fixed_role_list = [role for role in role_list if role not in semantic_roles]
    roles = set(fixed_role_list)
    required_inputs = REQUIRED_SNAPSHOT_ROLES | {PYTHON_RUNTIME_INPUT_ROLE}
    if roles != required_inputs or len(fixed_role_list) != len(required_inputs):
        raise CorpusError(
            "snapshot roles mismatch: missing="
            f"{sorted(required_inputs - roles)} extra="
            f"{sorted(roles - required_inputs)}"
        )
    maps = validate_manifest(
        next(source for role, source in inputs.values() if role == "map_manifest")
    )
    asset_maps = [role.partition(":")[2] for role, _ in inputs.values() if role.startswith("asset:")]
    if sorted(asset_maps) != sorted(maps) or len(asset_maps) != CORPUS_SIZE:
        raise CorpusError("snapshot assets must cover every manifest map exactly once")
    semantic_manifest = load_semantic_checker_manifest(
        next(source for role, source in inputs.values()
             if role == "semantic_checker_manifest"),
        maps,
    )
    required_semantic_roles = {item["role"] for item in semantic_manifest}
    if semantic_roles != required_semantic_roles or len(semantic_roles) != sum(
        role.startswith(SEMANTIC_CHECKER_ROLE_PREFIX) for role in role_list
    ):
        raise CorpusError("snapshot semantic checker roles do not match their manifest")
    for logical_name, (role, _source) in inputs.items():
        if role.startswith("asset:"):
            map_name = role.partition(":")[2]
            if PurePosixPath(logical_name).name != f"{map_name}.bsp":
                raise CorpusError(
                    f"asset {map_name} must be one extracted regular {map_name}.bsp"
                )
    role_paths = {
        role: PurePosixPath(logical).name
        for logical, (role, _source) in inputs.items()
        if role in ("module_primary", "module_secondary")
    }
    if role_paths != {
        "module_primary": "game.so",
        "module_secondary": "gamex86_64.so",
    }:
        raise CorpusError("production module snapshot names must be game.so and gamex86_64.so")
    acceptor_paths = {
        role: (PurePosixPath(logical).name, source.name)
        for logical, (role, source) in inputs.items()
        if role in ("acceptor_gnu", "acceptor_make")
    }
    if acceptor_paths != {
        "acceptor_gnu": ("runeaccept.gnu", "runeaccept.gnu"),
        "acceptor_make": ("runeaccept.make", "runeaccept.make"),
    }:
        raise CorpusError(
            "GNU and Make acceptors must be distinct runeaccept.gnu/runeaccept.make inputs"
        )
    for logical, (role, _source) in inputs.items():
        if role.startswith(SEMANTIC_CHECKER_ROLE_PREFIX):
            name = role.removeprefix(SEMANTIC_CHECKER_ROLE_PREFIX)
            if PurePosixPath(logical).name != f"{name}_rune_accept.py":
                raise CorpusError("semantic checker role/path mismatch")
    inputs = _expand_python_runtime_inputs(inputs)
    output.mkdir(parents=True, mode=0o700)
    entries: list[dict[str, Any]] = []
    try:
        for logical_name in sorted(inputs):
            role, source = inputs[logical_name]
            logical = _safe_logical_path(logical_name)
            destination = output.joinpath(*logical.parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            source_fd, source_info = _open_regular(source)
            try:
                digest = hashlib.sha256()
                copied_size = 0
                target_fd = os.open(
                    destination,
                    os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_CLOEXEC", 0),
                    stat.S_IMODE(source_info.st_mode),
                )
                try:
                    while True:
                        chunk = os.read(source_fd, 1024 * 1024)
                        if not chunk:
                            break
                        digest.update(chunk)
                        copied_size += len(chunk)
                        os.write(target_fd, chunk)
                    os.fchmod(target_fd, stat.S_IMODE(source_info.st_mode) & ~0o222)
                    os.fsync(target_fd)
                finally:
                    os.close(target_fd)
                source_final = os.fstat(source_fd)
            finally:
                os.close(source_fd)
            if (source_final.st_dev, source_final.st_ino, source_final.st_size, source_final.st_mtime_ns) != (
                source_info.st_dev, source_info.st_ino, source_info.st_size, source_info.st_mtime_ns
            ) or copied_size != source_info.st_size:
                raise CorpusError(f"input changed while copying: {source}")
            source_record = {
                "mode": stat.S_IMODE(source_info.st_mode),
                "size": copied_size,
                "sha256": digest.hexdigest(),
            }
            copied = regular_file_record(destination, logical_path=logical.as_posix())
            if copied["sha256"] != source_record["sha256"] or copied["size"] != source_record["size"]:
                raise CorpusError(f"snapshot copy mismatch: {source}")
            copied["role"] = role
            entries.append(copied)
        primary = next(entry for entry in entries if entry["role"] == "module_primary")
        secondary = next(entry for entry in entries if entry["role"] == "module_secondary")
        if primary["sha256"] != secondary["sha256"]:
            raise CorpusError("production module files do not have identical bytes")
        manifest = {"files": entries}
        atomic_write_json(output / "input-manifest.json", manifest, mode=0o444)
        freeze_tree(output)
        verify_snapshot(output)
        return manifest
    except BaseException:
        if output.exists():
            for directory, names, files in os.walk(output, topdown=False):
                for name in names + files:
                    Path(directory, name).chmod(0o700)
            output.chmod(0o700)
            shutil.rmtree(output)
        raise


def _load_json_regular(path: Path) -> tuple[Any, bytes]:
    fd, _info = _open_regular(path)
    try:
        with os.fdopen(fd, "rb", closefd=False) as stream:
            data = stream.read()
    finally:
        os.close(fd)
    try:
        return json.loads(data), data
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise CorpusError(f"invalid JSON in {path}: {exc}") from exc


def _elf_identity(path: Path) -> dict[str, int]:
    """Read the small fixed ELF header through the safe regular-file path."""
    data, _record = read_regular_bytes(path)
    if len(data) < 20 or data[:4] != b"\x7fELF" or data[5] != 1:
        raise CorpusError(f"runtime native file is not a little-endian ELF: {path}")
    if data[4] not in (1, 2):
        raise CorpusError(f"runtime native ELF class is unsupported: {path}")
    machine = int.from_bytes(data[18:20], "little")
    if machine not in (3, 62, 183):
        raise CorpusError(f"runtime native ELF architecture is unsupported: {path}")
    return {"class": data[4], "machine": machine}


def elf_interpreter(path: Path) -> str:
    """Return PT_INTERP using only the ELF program-header table."""
    data, _record = read_regular_bytes(path)
    identity = _elf_identity(path)
    if identity["class"] == 1:
        phoff = int.from_bytes(data[28:32], "little")
        phentsize = int.from_bytes(data[42:44], "little")
        phnum = int.from_bytes(data[44:46], "little")
        offset_at = 4
    else:
        phoff = int.from_bytes(data[32:40], "little")
        phentsize = int.from_bytes(data[54:56], "little")
        phnum = int.from_bytes(data[56:58], "little")
        offset_at = 8
    if phentsize <= offset_at + 8 or phoff + phentsize * phnum > len(data):
        raise CorpusError("ELF program headers are malformed")
    for index in range(phnum):
        header = data[phoff + index * phentsize:phoff + (index + 1) * phentsize]
        if int.from_bytes(header[:4], "little") == 3:
            offset = int.from_bytes(header[offset_at:offset_at + (4 if identity["class"] == 1 else 8)], "little")
            end = data.find(b"\0", offset)
            if offset >= len(data) or end < 0:
                raise CorpusError("ELF PT_INTERP is malformed")
            try:
                return data[offset:end].decode("ascii")
            except UnicodeDecodeError as exc:
                raise CorpusError("ELF PT_INTERP is not ASCII") from exc
    raise CorpusError("ELF has no PT_INTERP")


def _python_runtime_layout(entries: Sequence[Mapping[str, Any]]) -> dict[str, Any]:
    runtime_entries = [
        entry for entry in entries
        if str(entry.get("role", "")).startswith(PYTHON_RUNTIME_ROLE_PREFIX)
    ]
    if not runtime_entries:
        raise CorpusError("snapshot has no Python runtime closure")
    by_relative: dict[str, Mapping[str, Any]] = {}
    for entry in runtime_entries:
        logical = _safe_logical_path(str(entry.get("path", "")))
        try:
            relative = logical.relative_to(PYTHON_RUNTIME_ROOT).as_posix()
        except ValueError as exc:
            raise CorpusError("Python runtime entry is outside its canonical prefix") from exc
        if entry.get("role") != PYTHON_RUNTIME_ROLE_PREFIX + relative:
            raise CorpusError("Python runtime role/path mismatch")
        parts = PurePosixPath(relative).parts
        if relative.endswith(".pyc") or "__pycache__" in parts:
            raise CorpusError("Python runtime closure contains bytecode cache authority")
        if not (
            re.fullmatch(r"bin/python[0-9]+\.[0-9]+", relative)
            or re.fullmatch(r"lib/ld-linux-[A-Za-z0-9_-]+\.so\.[0-9]+", relative)
            or re.fullmatch(r"lib/(?:libpython[^/]+|[^/]+\.so(?:\.[0-9]+)*)", relative)
            or (len(parts) >= 3 and re.fullmatch(r"python[0-9]+\.[0-9]+", parts[1])
                and parts[0] == "lib")
        ):
            raise CorpusError(f"Python runtime has an unapproved closure path: {relative}")
        by_relative[relative] = entry
    interpreters = [
        (match.group(1), entry)
        for relative, entry in by_relative.items()
        if (match := re.fullmatch(r"bin/python([0-9]+\.[0-9]+)", relative))
    ]
    if len(interpreters) != 1:
        raise CorpusError("Python runtime requires one canonical versioned interpreter")
    version, interpreter = interpreters[0]
    loaders = [
        entry for relative, entry in by_relative.items()
        if re.fullmatch(r"lib/ld-linux-[A-Za-z0-9_-]+\.so\.[0-9]+", relative)
    ]
    if len(loaders) != 1:
        raise CorpusError("Python runtime requires exactly one private ELF loader")
    loader = loaders[0]
    required = {
        "libpython": f"lib/libpython{version}.so.1.0",
        "json": f"lib/python{version}/json/__init__.py",
        "runpy": f"lib/python{version}/runpy.py",
        "encodings": f"lib/python{version}/encodings/__init__.py",
        "hashlib": f"lib/python{version}/hashlib.py",
        "struct": f"lib/python{version}/struct.py",
    }
    missing = [name for name, relative in required.items() if relative not in by_relative]
    extension_entries: dict[str, Mapping[str, Any]] = {}
    extension_counts: dict[str, int] = {}
    for name in (
        "_bz2", "_ctypes", "_hashlib", "_json", "_lzma", "_socket", "_struct",
        "array", "fcntl", "math", "select", "zlib",
    ):
        matches = [
            entry for relative, entry in by_relative.items()
            if re.fullmatch(
                rf"lib/python{re.escape(version)}/lib-dynload/{name}(?:\.[^/]*)?\.so",
                relative,
            )
        ]
        extension_counts[name] = len(matches)
        if len(matches) == 1:
            extension_entries[name] = matches[0]
    library_entries: dict[str, Mapping[str, Any]] = {}
    library_counts: dict[str, int] = {}
    for name, pattern in (
        ("libbz2", r"lib/libbz2\.so(?:\.[0-9]+)*"),
        ("libcrypto", r"lib/libcrypto\.so(?:\.[0-9]+)*"),
        ("libffi", r"lib/libffi\.so(?:\.[0-9]+)*"),
        ("liblzma", r"lib/liblzma\.so(?:\.[0-9]+)*"),
        ("libz", r"lib/libz\.so(?:\.[0-9]+)*"),
    ):
        matches = [entry for relative, entry in by_relative.items() if re.fullmatch(pattern, relative)]
        library_counts[name] = len(matches)
        if matches:
            library_entries[name] = min(
                matches,
                key=lambda entry: (PurePosixPath(str(entry["path"])).name.count("."), str(entry["path"])),
            )
    if missing or any(count != 1 for count in extension_counts.values()) or any(
        count < 1 for count in library_counts.values()
    ) or len(library_entries) != len(library_counts):
        raise CorpusError(
            "Python runtime closure markers missing="
            f"{missing} extensions={extension_counts} libraries={library_counts}"
        )
    if not (int(interpreter["mode"]) & stat.S_IXUSR):
        raise CorpusError("Python runtime interpreter is not executable")
    return {
        "root": PYTHON_RUNTIME_ROOT.as_posix(),
        "version": version,
        "loader": loader,
        "interpreter": interpreter,
        **{name: by_relative[relative] for name, relative in required.items()},
        **{f"extension_{name.removeprefix('_')}": entry for name, entry in extension_entries.items()},
        **library_entries,
        "files": runtime_entries,
    }


def verify_snapshot(snapshot: Path) -> dict[str, Any]:
    reject_symlink_components(snapshot)
    if stat.S_IMODE(snapshot.lstat().st_mode) & 0o222:
        raise CorpusError("snapshot root is writable")
    if stat.S_IMODE((snapshot / "input-manifest.json").lstat().st_mode) & 0o222:
        raise CorpusError("input-manifest is writable")
    manifest, manifest_bytes = _load_json_regular(snapshot / "input-manifest.json")
    if not isinstance(manifest, dict) or set(manifest) != {"files"} or not isinstance(manifest.get("files"), list):
        raise CorpusError("invalid input-manifest structure")
    entries = manifest["files"]
    seen_paths: set[str] = set()
    seen_roles: list[str] = []
    for expected in entries:
        if not isinstance(expected, dict):
            raise CorpusError("invalid input-manifest entry")
        logical = _safe_logical_path(str(expected.get("path", ""))).as_posix()
        if logical in seen_paths:
            raise CorpusError(f"duplicate snapshot path: {logical}")
        seen_paths.add(logical)
        seen_roles.append(str(expected.get("role", "")))
        actual = regular_file_record(snapshot / logical, logical_path=logical)
        for key in ("path", "mode", "size", "sha256"):
            if actual[key] != expected.get(key):
                raise CorpusError(f"snapshot {key} mismatch for {logical}")
    expected_directories: set[str] = set()
    for logical in seen_paths:
        parent = PurePosixPath(logical).parent
        while parent != PurePosixPath("."):
            expected_directories.add(parent.as_posix())
            parent = parent.parent
    actual_paths: set[str] = set()
    actual_directories: set[str] = set()
    for directory, names, files in os.walk(snapshot, followlinks=False):
        current = Path(directory)
        if stat.S_IMODE(current.lstat().st_mode) & 0o222:
            raise CorpusError(f"snapshot directory is writable: {current}")
        for name in names + files:
            candidate = current / name
            info = candidate.lstat()
            if stat.S_ISLNK(info.st_mode):
                raise CorpusError(f"snapshot contains a symlink: {candidate}")
            if stat.S_ISREG(info.st_mode):
                relative = candidate.relative_to(snapshot).as_posix()
                if relative != "input-manifest.json":
                    actual_paths.add(relative)
            elif stat.S_ISDIR(info.st_mode):
                actual_directories.add(candidate.relative_to(snapshot).as_posix())
            else:
                raise CorpusError(f"snapshot contains an unsupported file type: {candidate}")
    if actual_paths != seen_paths or actual_directories != expected_directories:
        raise CorpusError("snapshot file/directory set differs from input-manifest")
    non_asset_roles = [
        role for role in seen_roles
        if not role.startswith("asset:")
        and not role.startswith(PYTHON_RUNTIME_ROLE_PREFIX)
    ]
    semantic_roles = {
        role for role in non_asset_roles
        if role.startswith(SEMANTIC_CHECKER_ROLE_PREFIX)
    }
    fixed_roles = [role for role in non_asset_roles if role not in semantic_roles]
    roles = set(fixed_roles)
    if roles != REQUIRED_SNAPSHOT_ROLES or len(fixed_roles) != len(REQUIRED_SNAPSHOT_ROLES):
        raise CorpusError("verified snapshot has incorrect required roles")
    maps = validate_manifest(snapshot / next(
        str(entry["path"]) for entry in entries if entry["role"] == "map_manifest"
    ))
    assets = [role.partition(":")[2] for role in seen_roles if role.startswith("asset:")]
    if sorted(assets) != sorted(maps) or len(assets) != CORPUS_SIZE:
        raise CorpusError("verified snapshot has incomplete map assets")
    semantic_manifest_entry = next(
        entry for entry in entries if entry["role"] == "semantic_checker_manifest"
    )
    semantic_manifest = load_semantic_checker_manifest(
        snapshot / str(semantic_manifest_entry["path"]), maps
    )
    required_semantic_roles = {item["role"] for item in semantic_manifest}
    if semantic_roles != required_semantic_roles or len(semantic_roles) != sum(
        role.startswith(SEMANTIC_CHECKER_ROLE_PREFIX) for role in non_asset_roles
    ):
        raise CorpusError("verified semantic checker roles do not match their manifest")
    for entry in entries:
        role = str(entry["role"])
        if role.startswith("asset:"):
            map_name = role.partition(":")[2]
            if PurePosixPath(str(entry["path"])).name != f"{map_name}.bsp":
                raise CorpusError("verified snapshot contains a non-extracted map asset")
    primary = next(entry for entry in entries if entry["role"] == "module_primary")
    secondary = next(entry for entry in entries if entry["role"] == "module_secondary")
    if primary["sha256"] != secondary["sha256"]:
        raise CorpusError("verified module hashes differ")
    if PurePosixPath(str(primary["path"])).name != "game.so" or PurePosixPath(
        str(secondary["path"])
    ).name != "gamex86_64.so":
        raise CorpusError("verified module snapshot names are not production names")
    for role, basename in (
        ("acceptor_gnu", "runeaccept.gnu"),
        ("acceptor_make", "runeaccept.make"),
    ):
        if PurePosixPath(str(next(
            entry["path"] for entry in entries if entry["role"] == role
        ))).name != basename:
            raise CorpusError("verified GNU/Make acceptor names are not distinct")
    for item in semantic_manifest:
        checker = next(entry for entry in entries if entry["role"] == item["role"])
        if PurePosixPath(str(checker["path"])).name != f"{item['name']}_rune_accept.py":
            raise CorpusError("verified semantic checker role/path mismatch")
    python_runtime = _python_runtime_layout(entries)
    native_runtime_entries = [python_runtime["interpreter"]] + [
        entry for entry in python_runtime["files"]
        if re.search(r"\.so(?:\.[0-9]+)*\Z", PurePosixPath(str(entry["path"])).name)
    ]
    for entry in native_runtime_entries:
        data, record = read_regular_bytes(
            snapshot / entry["path"], logical_path=str(entry["path"])
        )
        if record["sha256"] != entry["sha256"] or not data.startswith(b"\x7fELF"):
            raise CorpusError("Python runtime native input is not the manifested ELF")
    loader_path = snapshot / python_runtime["loader"]["path"]
    interpreter_path = snapshot / python_runtime["interpreter"]["path"]
    loader_elf = _elf_identity(loader_path)
    interpreter_elf = _elf_identity(interpreter_path)
    if loader_elf["machine"] != interpreter_elf["machine"] or loader_elf["class"] != interpreter_elf["class"]:
        raise CorpusError("private loader and interpreter architecture mismatch")
    if Path(elf_interpreter(interpreter_path)).name != loader_path.name:
        raise CorpusError("private loader name does not match interpreter PT_INTERP")
    return {
        "manifest": manifest,
        "manifest_sha256": sha256_bytes(manifest_bytes),
        "by_role": {entry["role"]: entry for entry in entries},
        "python_runtime": python_runtime,
        "semantic_checkers": semantic_manifest,
    }


def _contract_hashes(path: Path) -> tuple[str, str]:
    text = path.read_text(encoding="ascii")
    action = re.search(
        r"^RUNE_ACTION_CONTRACT_SHA256 = ['\"]([0-9a-fA-F]{64})['\"]$",
        text,
        re.MULTILINE,
    )
    mechanism = re.search(
        r"^RUNE_MECHANISM_CONTRACT_SHA256 = ['\"]([0-9a-fA-F]{64})['\"]$",
        text,
        re.MULTILINE,
    )
    if action is None or mechanism is None:
        raise CorpusError("generated contract hashes are missing or malformed")
    return action.group(1).lower(), mechanism.group(1).lower()


def build_fingerprint_document(
    snapshot: Path,
    *,
    startup_timeout: int,
    generation_timeout: int,
    jobs: int,
    port_base: int,
    engine_arguments: Sequence[str] = DEFAULT_ENGINE_ARGUMENTS,
    controller_source: Path | None = None,
) -> tuple[dict[str, Any], str]:
    if startup_timeout <= 0 or generation_timeout <= 0 or jobs <= 0:
        raise CorpusError("timeouts and job count must be positive")
    validate_engine_arguments(engine_arguments)
    verified = verify_snapshot(snapshot)
    by_role = verified["by_role"]
    python_runtime = verified["python_runtime"]
    contracts_path = snapshot / by_role["contracts"]["path"]
    action_hash, mechanism_hash = _contract_hashes(contracts_path)
    controller = controller_source or Path(__file__).resolve()
    document = {
        "input_manifest_sha256": verified["manifest_sha256"],
        "ordered_map_manifest_sha256": by_role["map_manifest"]["sha256"],
        "engine_sha256": by_role["engine"]["sha256"],
        "python_runtime_version": python_runtime["version"],
        "python_loader_path": python_runtime["loader"]["path"],
        "python_loader_sha256": python_runtime["loader"]["sha256"],
        "python_interpreter_path": python_runtime["interpreter"]["path"],
        "python_interpreter_sha256": python_runtime["interpreter"]["sha256"],
        "python_libpython_sha256": python_runtime["libpython"]["sha256"],
        "python_runtime_manifest_sha256": sha256_bytes(canonical_json([
            {
                key: entry[key] for key in ("path", "mode", "size", "sha256", "role")
            }
            for entry in python_runtime["files"]
        ])),
        "module_hashes": [
            by_role["module_primary"]["sha256"],
            by_role["module_secondary"]["sha256"],
        ],
        "action_contract_hash": action_hash,
        "mechanism_contract_hash": mechanism_hash,
        "linter_sha256": by_role["runelint"]["sha256"],
        "reader_sha256": by_role["runeio"]["sha256"],
        "acceptor_gnu_sha256": by_role["acceptor_gnu"]["sha256"],
        "acceptor_make_sha256": by_role["acceptor_make"]["sha256"],
        "semantic_checker_manifest_sha256": by_role[
            "semantic_checker_manifest"
        ]["sha256"],
        "semantic_checkers": [
            {
                **item,
                "sha256": by_role[item["role"]]["sha256"],
            }
            for item in verified["semantic_checkers"]
        ],
        "generation_timeout_seconds": generation_timeout,
        "startup_timeout_seconds": startup_timeout,
        "job_count": jobs,
        "port_base": port_base,
        "engine_arguments": list(engine_arguments),
        "python_isolation_flags": list(PYTHON_ISOLATION_FLAGS),
        "python_gate_bootstrap_sha256": sha256_bytes(
            PYTHON_GATE_BOOTSTRAP.encode("utf-8")
        ),
        "guard_bootstrap_sha256": sha256_bytes(GUARD_BOOTSTRAP.encode("utf-8")),
        "python_loader_arguments": ["--inhibit-cache", "--library-path", "{snapshot}/python-runtime/lib"],
        "python_handshake_sha256": sha256_bytes(b"READY/DONE:length-prefixed:canonical-json:release"),
        "pseudo_map_allowlist": sorted(PSEUDO_MAP_ALLOWLIST),
        "engine_environment": dict(ENGINE_ENVIRONMENT),
        "python_environment": dict(PYTHON_ENVIRONMENT),
        "acceptor_environment": dict(ACCEPTOR_ENVIRONMENT),
        "controller_sha256": sha256_regular(controller),
    }
    encoded = canonical_json(document)
    return document, sha256_bytes(encoded)


def verify_fingerprint_document(snapshot: Path, expected: Mapping[str, Any]) -> str:
    try:
        rebuilt, fingerprint = build_fingerprint_document(
            snapshot,
            startup_timeout=int(expected["startup_timeout_seconds"]),
            generation_timeout=int(expected["generation_timeout_seconds"]),
            jobs=int(expected["job_count"]),
            port_base=int(expected["port_base"]),
            engine_arguments=tuple(expected["engine_arguments"]),
        )
    except (KeyError, TypeError, ValueError) as exc:
        raise CorpusError("fingerprint document has invalid fields") from exc
    if canonical_json(rebuilt) != canonical_json(dict(expected)):
        raise CorpusError("frozen inputs no longer match the fingerprint document")
    return fingerprint


def python_child_environment(runtime_root: Path) -> dict[str, str]:
    """The fixed Python environment intentionally has no loader variables."""
    del runtime_root
    return dict(PYTHON_ENVIRONMENT)


def _private_python_command(
    snapshot: Path,
    layout: Mapping[str, Any],
    program: str,
    *arguments: str,
) -> list[str]:
    loader = snapshot / str(layout["loader"]["path"])
    interpreter = snapshot / str(layout["interpreter"]["path"])
    library = snapshot / "python-runtime" / "lib"
    return [
        str(loader), "--inhibit-cache", "--library-path", str(library),
        str(interpreter), *PYTHON_ISOLATION_FLAGS, "-c", program, *arguments,
    ]


def _nul_argv(command: Sequence[str]) -> bytes:
    return b"\0".join(os.fsencode(value) for value in command) + b"\0"


def _sha256_fd(fd: int) -> str:
    os.lseek(fd, 0, os.SEEK_SET)
    digest = hashlib.sha256()
    while chunk := os.read(fd, 1024 * 1024):
        digest.update(chunk)
    os.lseek(fd, 0, os.SEEK_SET)
    return digest.hexdigest()


def _read_frame(
    control_fd: int, stdout_fd: int, deadline: float, output: bytearray, *, limit: int,
) -> Mapping[str, Any]:
    """Read one length-prefixed frame while continuously draining gate stdout."""
    header = bytearray()
    payload = bytearray()
    selector = selectors.DefaultSelector()
    selector.register(control_fd, selectors.EVENT_READ, "control")
    selector.register(stdout_fd, selectors.EVENT_READ, "stdout")
    try:
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProcessIntegrityError("verified Python handshake deadline expired")
            events = selector.select(remaining)
            if not events:
                raise ProcessIntegrityError("verified Python handshake deadline expired")
            for key, _event in events:
                try:
                    chunk = os.read(key.fd, 65536)
                except BlockingIOError:
                    continue
                if key.data == "stdout":
                    if not chunk:
                        selector.unregister(key.fd)
                        continue
                    output.extend(chunk)
                    if len(output) > limit:
                        raise ProcessIntegrityError("verified Python stdout exceeds bounded evidence limit")
                    continue
                if not chunk:
                    raise ProcessIntegrityError("verified Python handshake closed before frame completion")
                view = memoryview(chunk)
                while view:
                    if len(header) < 4:
                        need = 4 - len(header)
                        header.extend(view[:need]); view = view[need:]
                        if len(header) < 4:
                            continue
                        length = int.from_bytes(header, "big")
                        if length <= 0 or length > 65536:
                            raise ProcessIntegrityError("verified Python handshake length is invalid")
                    need = length - len(payload)
                    payload.extend(view[:need]); view = view[need:]
                    if len(payload) == length:
                        if view:
                            raise ProcessIntegrityError("verified Python sent multiple frames before release")
                        try:
                            value = json.loads(bytes(payload))
                        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                            raise ProcessIntegrityError("verified Python handshake is not JSON") from exc
                        if not isinstance(value, dict) or canonical_json(value).rstrip() != bytes(payload):
                            raise ProcessIntegrityError("verified Python handshake is not canonical")
                        return value
    finally:
        selector.close()


@dataclasses.dataclass
class RetainedSnapshot:
    records: dict[tuple[int, int], dict[str, Any]]
    descriptors: list[int]

    def close(self) -> None:
        for fd in self.descriptors:
            try:
                os.close(fd)
            except OSError:
                pass
        self.descriptors.clear()


def _retained_snapshot_files(snapshot: Path, verified: Mapping[str, Any]) -> RetainedSnapshot:
    retained = RetainedSnapshot({}, [])
    try:
        for entry in verified["manifest"]["files"]:
            logical = str(entry["path"])
            path = snapshot / logical
            fd, info = _open_regular(path)
            retained.descriptors.append(fd)
            if info.st_nlink != 1:
                raise ProcessIntegrityError(f"manifested input has a hardlink: {logical}")
            if _sha256_fd(fd) != entry["sha256"] or info.st_size != entry["size"] or stat.S_IMODE(info.st_mode) != entry["mode"]:
                raise ProcessIntegrityError("retained snapshot input does not match manifest")
            key = (info.st_dev, info.st_ino)
            if key in retained.records:
                raise ProcessIntegrityError("two manifested inputs alias one inode")
            retained.records[key] = {
                **dict(entry), "dev": info.st_dev, "ino": info.st_ino,
                "canonical_path": str(path.resolve(strict=True)),
            }
        return retained
    except BaseException:
        retained.close()
        raise


_PROC_MAP_RE = re.compile(
    r"^(?P<range>[0-9a-f]+-[0-9a-f]+)\s+\S+\s+\S+\s+(?P<major>[0-9a-f]+):(?P<minor>[0-9a-f]+)\s+(?P<inode>[0-9]+)(?:\s+(?P<path>.*))?$"
)


def _pidfd_is_live(pidfd: int) -> bool:
    poller = select.poll()
    poller.register(pidfd, select.POLLIN | select.POLLHUP | select.POLLERR)
    return not poller.poll(0)


def _validate_retained_path(snapshot: Path, record: Mapping[str, Any]) -> None:
    path = snapshot / str(record["path"])
    fd, info = _open_regular(path)
    try:
        if (info.st_dev, info.st_ino, info.st_size, stat.S_IMODE(info.st_mode), info.st_nlink) != (
            record["dev"], record["ino"], record["size"], record["mode"], 1
        ) or _sha256_fd(fd) != record["sha256"]:
            raise ProcessIntegrityError(f"manifest path changed during child validation: {record['path']}")
    finally:
        os.close(fd)


def _validate_verified_python_process(
    pid: int, pidfd: int, snapshot: Path, verified: Mapping[str, Any],
    retained: RetainedSnapshot, command: Sequence[str],
) -> tuple[ProcessIdentity, str]:
    if not _pidfd_is_live(pidfd):
        raise ProcessIntegrityError("verified Python exited before parent validation")
    start_before = _proc_start_ticks(pid)
    layout = verified["python_runtime"]
    loader = snapshot / str(layout["loader"]["path"])
    identity = capture_process_identity(pid)
    if (Path(identity.executable) != loader.resolve(strict=True)
            or identity.executable_sha256 != str(layout["loader"]["sha256"])
            or identity.cmdline_sha256 != sha256_bytes(_nul_argv(command))):
        raise ProcessIntegrityError("verified Python executable or argv identity mismatch")
    try:
        map_text = Path(f"/proc/{pid}/maps").read_text(encoding="utf-8")
    except OSError as exc:
        raise ProcessIntegrityError(f"cannot read verified Python maps: {exc}") from exc
    for line in map_text.splitlines():
        match = _PROC_MAP_RE.fullmatch(line)
        if match is None:
            raise ProcessIntegrityError("malformed verified Python map")
        pathname = match.group("path") or ""
        if not pathname:
            continue
        if pathname in PSEUDO_MAP_ALLOWLIST:
            continue
        if (pathname.startswith("[") or pathname.endswith(" (deleted)")
                or pathname.startswith("/dev/") or pathname.startswith("/SYSV")
                or pathname.startswith("/memfd:")):
            raise ProcessIntegrityError(f"forbidden named mapping: {pathname}")
        if not pathname.startswith("/"):
            raise ProcessIntegrityError(f"forbidden non-filesystem mapping: {pathname}")
        dev = os.makedev(int(match.group("major"), 16), int(match.group("minor"), 16))
        inode = int(match.group("inode"))
        if inode == 0:
            raise ProcessIntegrityError(f"filesystem mapping has zero inode: {pathname}")
        record = retained.records.get((dev, inode))
        if record is None:
            raise ProcessIntegrityError(f"mapped device/inode is not manifested: {pathname}")
        if pathname != record["canonical_path"]:
            raise ProcessIntegrityError(f"mapped path is not the canonical manifested path: {pathname}")
        _validate_retained_path(snapshot, record)
    if _proc_start_ticks(pid) != start_before or capture_process_identity(pid) != identity:
        raise ProcessIntegrityError("verified Python identity changed during map validation")
    if not _pidfd_is_live(pidfd):
        raise ProcessIntegrityError("verified Python exited during parent validation")
    return identity, sha256_bytes(map_text.encode("utf-8"))


def _drain_until_exit(process: subprocess.Popen[bytes], stdout_fd: int, output: bytearray, deadline: float, *, limit: int) -> None:
    selector = selectors.DefaultSelector()
    selector.register(stdout_fd, selectors.EVENT_READ)
    try:
        while process.poll() is None or selector.get_map():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProcessIntegrityError("verified Python did not exit by its deadline")
            for key, _event in selector.select(min(remaining, 0.1)):
                try:
                    chunk = os.read(key.fd, 65536)
                except BlockingIOError:
                    continue
                if not chunk:
                    selector.unregister(key.fd)
                else:
                    output.extend(chunk)
                    if len(output) > limit:
                        raise ProcessIntegrityError("verified Python stdout exceeds bounded evidence limit")
    finally:
        selector.close()


def run_verified_python(
    snapshot: Path, layout: Mapping[str, Any], mode: str, target: Path,
    argv: Sequence[str], *, runner: Callable[..., subprocess.CompletedProcess[bytes]] = subprocess.run,
    timeout: float = 30.0, stdout_limit: int = 8 * 1024 * 1024,
) -> tuple[int, bytes, dict[str, Any]]:
    """Run a gate only after parent verification at READY and DONE."""
    command_prefix = _private_python_command(snapshot, layout, PYTHON_GATE_BOOTSTRAP)
    if runner is not subprocess.run:
        command = [*command_prefix, "-1", "-1", mode, str(target), *argv]
        completed = runner(command, cwd=snapshot / "python-runtime", stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False, env=dict(PYTHON_ENVIRONMENT))
        expected_command_sha256 = sha256_bytes(_nul_argv(command))
        loader = snapshot / str(layout["loader"]["path"])
        parent_identity = capture_process_identity(os.getpid())
        identity = dataclasses.replace(
            parent_identity, executable=str(loader.resolve(strict=True)),
            executable_sha256=sha256_regular(loader), cmdline_sha256=expected_command_sha256,
        ).as_dict()
        return completed.returncode, bytes(completed.stdout or b""), {
            "ready": {"phase": "READY", "mode": mode, "argv_sha256": sha256_bytes(_nul_argv(argv))},
            "done": {"phase": "DONE", "mode": mode, "rc": completed.returncode},
            "ready_identity": identity, "done_identity": identity,
            "ready_maps_sha256": sha256_bytes(b"fake verified map evidence"),
            "done_maps_sha256": sha256_bytes(b"fake verified map evidence"),
            "expected_command_sha256": expected_command_sha256,
        }
    control_r, control_w = os.pipe()
    release_r, release_w = os.pipe()
    process: subprocess.Popen[bytes] | None = None
    pidfd: int | None = None
    retained: RetainedSnapshot | None = None
    output = bytearray()
    try:
        verified = verify_snapshot(snapshot)
        retained = _retained_snapshot_files(snapshot, verified)
        command = [*command_prefix, str(control_w), str(release_r), mode, str(target), *argv]
        process = subprocess.Popen(command, cwd=snapshot / "python-runtime", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   close_fds=True, pass_fds=(control_w, release_r), env=dict(PYTHON_ENVIRONMENT))
        pidfd = open_pidfd(process.pid)
        if pidfd is None:
            raise ProcessIntegrityError("verified Python pidfd is unavailable")
        os.close(control_w); control_w = -1
        os.close(release_r); release_r = -1
        assert process.stdout is not None
        stdout_fd = process.stdout.fileno()
        os.set_blocking(control_r, False); os.set_blocking(stdout_fd, False)
        deadline = time.monotonic() + timeout
        ready = _read_frame(control_r, stdout_fd, deadline, output, limit=stdout_limit)
        if (ready.get("phase") != "READY" or ready.get("mode") != mode
                or ready.get("argv_sha256") != sha256_bytes(_nul_argv(argv))):
            raise ProcessIntegrityError("verified Python READY report is invalid")
        ready_identity, ready_maps = _validate_verified_python_process(
            process.pid, pidfd, snapshot, verified, retained, command
        )
        if time.monotonic() > deadline:
            raise ProcessIntegrityError("verified Python READY validation exceeded its deadline")
        os.write(release_w, b"R")
        done = _read_frame(control_r, stdout_fd, deadline, output, limit=stdout_limit)
        if done.get("phase") != "DONE" or done.get("mode") != mode or type(done.get("rc")) is not int:
            raise ProcessIntegrityError("verified Python DONE report is invalid")
        done_identity, done_maps = _validate_verified_python_process(
            process.pid, pidfd, snapshot, verified, retained, command
        )
        if time.monotonic() > deadline:
            raise ProcessIntegrityError("verified Python DONE validation exceeded its deadline")
        os.write(release_w, b"R")
        _drain_until_exit(process, stdout_fd, output, deadline, limit=stdout_limit)
        process.wait(timeout=max(0.0, deadline - time.monotonic()))
        if process.returncode != done["rc"]:
            raise ProcessIntegrityError("verified Python exit status disagrees with DONE")
        verify_snapshot(snapshot)
        for record in retained.records.values():
            _validate_retained_path(snapshot, record)
        return process.returncode, bytes(output), {
            "ready": ready, "done": done,
            "ready_identity": ready_identity.as_dict(), "done_identity": done_identity.as_dict(),
            "ready_maps_sha256": ready_maps, "done_maps_sha256": done_maps,
            "expected_command_sha256": sha256_bytes(_nul_argv(command)),
        }
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        if pidfd is not None:
            os.close(pidfd)
        if retained is not None:
            retained.close()
        if process is not None and process.stdout is not None:
            process.stdout.close()
        for fd in (control_r, control_w, release_r, release_w):
            if fd >= 0:
                try:
                    os.close(fd)
                except OSError:
                    pass


def _runtime_physical_path(
    value: Any, *, runtime_root: Path, label: str
) -> Path:
    if not isinstance(value, str) or not Path(value).is_absolute():
        raise CorpusError(f"Python runtime probe has invalid {label}")
    path = Path(value)
    reject_symlink_components(path)
    try:
        resolved = path.resolve(strict=True)
        resolved.relative_to(runtime_root.resolve(strict=True))
    except (OSError, ValueError) as exc:
        raise CorpusError(f"Python runtime probe {label} escapes snapshot") from exc
    return resolved


def preflight_python_runtime(
    snapshot: Path,
    *,
    runner: Callable[..., subprocess.CompletedProcess[bytes]] = subprocess.run,
) -> dict[str, Any]:
    """Execute and prove the complete private Python authority before use."""
    before = verify_snapshot(snapshot)
    layout = before["python_runtime"]
    runtime_root = snapshot / layout["root"]
    interpreter = snapshot / layout["interpreter"]["path"]
    target = snapshot / before["by_role"]["runeio"]["path"]
    completed_returncode, output, lifecycle = run_verified_python(
        snapshot, layout, "preflight", target, [PYTHON_RUNTIME_PROBE], runner=runner,
    )
    if completed_returncode != 0:
        raise CorpusError(
            f"private Python runtime probe exited {completed_returncode}: "
            f"{output.decode('utf-8', errors='replace').strip()}"
        )
    try:
        report = json.loads(output)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise CorpusError("private Python runtime probe did not emit one JSON report") from exc
    if not isinstance(report, dict) or set(report) != {
        "executable", "prefix", "base_prefix", "modules", "loaded_libraries",
        "sys_path", "dont_write_bytecode",
    }:
        raise CorpusError("private Python runtime probe report schema mismatch")
    expected_root = runtime_root.resolve(strict=True)
    if _runtime_physical_path(
        report["executable"], runtime_root=runtime_root, label="executable"
    ) != interpreter.resolve(strict=True):
        raise CorpusError("private Python probe executed the wrong interpreter")
    for field in ("prefix", "base_prefix"):
        if _runtime_physical_path(
            report[field], runtime_root=runtime_root, label=field
        ) != expected_root:
            raise CorpusError(f"private Python {field} is not the runtime prefix")
    sys_path = report["sys_path"]
    if not isinstance(sys_path, list) or not sys_path:
        raise CorpusError("private Python sys.path report is invalid")
    for value in sys_path:
        if not isinstance(value, str) or not Path(value).is_absolute():
            raise CorpusError("private Python sys.path contains ambient authority")
        candidate = Path(value)
        reject_symlink_components(candidate)
        try:
            candidate.resolve(strict=False).relative_to(snapshot.resolve(strict=True))
        except ValueError as exc:
            raise CorpusError("private Python sys.path escapes snapshot") from exc
    modules = report["modules"]
    expected_modules = {
        "json": layout["json"],
        "runpy": layout["runpy"],
        "encodings": layout["encodings"],
        "_json": layout["extension_json"],
        "hashlib": layout["hashlib"],
        "_hashlib": layout["extension_hashlib"],
        "math": layout["extension_math"],
        "zlib": layout["extension_zlib"],
        "struct": layout["struct"],
        "_struct": layout["extension_struct"],
    }
    if not isinstance(modules, dict) or set(modules) != set(expected_modules):
        raise CorpusError("private Python module origin report mismatch")
    for name, entry in expected_modules.items():
        if _runtime_physical_path(
            modules[name], runtime_root=runtime_root, label=f"module {name}"
        ) != (snapshot / entry["path"]).resolve(strict=True):
            raise CorpusError(f"private Python module {name} resolved to wrong bytes")
    loaded = report["loaded_libraries"]
    if not isinstance(loaded, list) or not loaded or len(loaded) != len(set(loaded)):
        raise CorpusError("private Python loaded-library report is invalid")
    runtime_manifest_paths = {
        (snapshot / entry["path"]).resolve(strict=True) for entry in layout["files"]
    }
    loaded_paths: set[Path] = set()
    for index, value in enumerate(loaded):
        if not isinstance(value, str) or not Path(value).is_absolute():
            raise CorpusError("private Python loaded-library path is invalid")
        path = Path(value)
        resolved = path.resolve(strict=True)
        if path != resolved:
            raise CorpusError("private Python loaded-library report is not physical")
        loaded_paths.add(resolved)
        if resolved not in runtime_manifest_paths:
            raise CorpusError("private Python loaded an unmanifested or host library")
    required_loaded = {
        (snapshot / layout[name]["path"]).resolve(strict=True)
        for name in (
            "libpython", "libbz2", "libcrypto", "libffi", "liblzma", "libz",
            *(f"extension_{name}" for name in (
                "bz2", "ctypes", "hashlib", "json", "lzma", "socket", "struct",
                "array", "fcntl", "math", "select", "zlib",
            )),
        )
    }
    if not required_loaded.issubset(loaded_paths):
        raise CorpusError("private Python did not load its complete shared-library closure")
    if report["dont_write_bytecode"] is not True:
        raise CorpusError("private Python runtime did not disable bytecode writes")
    after = verify_snapshot(snapshot)
    if before["manifest_sha256"] != after["manifest_sha256"]:
        raise CorpusError("private Python runtime changed during its probe")
    return {
        "command": _private_python_command(snapshot, layout, PYTHON_GATE_BOOTSTRAP),
        "output_sha256": sha256_bytes(output), "lifecycle": lifecycle,
    }


def boot_id() -> str:
    value = Path("/proc/sys/kernel/random/boot_id").read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[0-9a-fA-F-]{36}", value):
        raise CorpusError("invalid kernel boot ID")
    return value.lower()


def _proc_start_ticks(pid: int) -> int:
    data = Path(f"/proc/{pid}/stat").read_text(encoding="ascii")
    close = data.rfind(")")
    if close < 0:
        raise CorpusError(f"malformed process stat for PID {pid}")
    fields_after_comm = data[close + 2 :].split()
    if len(fields_after_comm) < 20:
        raise CorpusError(f"incomplete process stat for PID {pid}")
    return int(fields_after_comm[19])


def _proc_cmdline(pid: int) -> bytes:
    return Path(f"/proc/{pid}/cmdline").read_bytes()


@dataclasses.dataclass(frozen=True)
class ProcessIdentity:
    pid: int
    boot_id: str
    start_ticks: int
    executable: str
    executable_sha256: str
    cmdline_sha256: str

    def as_dict(self) -> dict[str, Any]:
        return dataclasses.asdict(self)


def capture_process_identity(pid: int) -> ProcessIdentity:
    first_ticks = _proc_start_ticks(pid)
    executable = Path(f"/proc/{pid}/exe").resolve(strict=True)
    executable_hash = sha256_regular(executable)
    cmdline_hash = sha256_bytes(_proc_cmdline(pid))
    last_ticks = _proc_start_ticks(pid)
    if first_ticks != last_ticks:
        raise CorpusError("process start ticks changed during identity capture")
    return ProcessIdentity(
        pid=pid,
        boot_id=boot_id(),
        start_ticks=first_ticks,
        executable=str(executable),
        executable_sha256=executable_hash,
        cmdline_sha256=cmdline_hash,
    )


def process_identity_matches(identity: ProcessIdentity) -> bool:
    try:
        return capture_process_identity(identity.pid) == identity
    except (CorpusError, OSError, ProcessLookupError, PermissionError):
        return False


def open_pidfd(pid: int) -> int | None:
    opener = getattr(os, "pidfd_open", None)
    if opener is None:
        return None
    try:
        return opener(pid, 0)
    except OSError as exc:
        if exc.errno in (errno.ENOSYS, errno.EINVAL):
            return None
        raise


def require_pidfd_support() -> None:
    opener = getattr(os, "pidfd_open", None)
    sender = getattr(signal, "pidfd_send_signal", None)
    if opener is None or sender is None:
        raise CorpusError("pidfd open/send support is required before launch")
    try:
        descriptor = opener(os.getpid(), 0)
    except OSError as exc:
        raise CorpusError(f"pidfd preflight failed: {exc}") from exc
    os.close(descriptor)


def signal_owned_child(
    identity: ProcessIdentity,
    owner_record: Path,
    sig: int,
    *,
    pidfd: int | None,
    sender: Callable[[int, int], None] | None = None,
) -> bool:
    """Signal only a still-matching process through its captured descriptor."""
    if pidfd is None:
        return False
    try:
        owner, _raw = _load_json_regular(owner_record)
    except CorpusError:
        return False
    if not isinstance(owner, dict) or owner.get("process") != identity.as_dict():
        return False
    if not process_identity_matches(identity):
        return False
    if sender is None:
        sender = getattr(signal, "pidfd_send_signal", None)
    if sender is None:
        return False
    try:
        sender(pidfd, sig)
    except ProcessLookupError:
        return True
    return True


def shutdown_captured_child(
    process: subprocess.Popen[bytes],
    identity: ProcessIdentity,
    owner_record: Path,
    pidfd: int | None,
) -> None:
    """Bounded TERM/KILL cleanup through one verified captured descriptor."""
    if process.poll() is not None:
        return
    if not signal_owned_child(identity, owner_record, signal.SIGTERM, pidfd=pidfd):
        raise CorpusError("child shutdown ownership could not be proven")
    try:
        process.wait(timeout=5)
        return
    except subprocess.TimeoutExpired:
        pass
    if not signal_owned_child(identity, owner_record, signal.SIGKILL, pidfd=pidfd):
        raise CorpusError("child remained live and KILL ownership could not be proven")
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired as exc:
        raise CorpusError("owned child did not stop after descriptor KILL") from exc


def parent_death_guard(expected_parent: int) -> Callable[[], None]:
    """Return a pre-exec hook that arms Linux PR_SET_PDEATHSIG."""
    def arm() -> None:
        arm_parent_death(expected_parent)
    return arm


def arm_parent_death(expected_parent: int) -> None:
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(1, signal.SIGKILL, 0, 0, 0) != 0 or os.getppid() != expected_parent:
        os._exit(125)


def _guard_exec(arguments: Sequence[str]) -> int:
    if len(arguments) < 3 or arguments[1] != "--":
        raise CorpusError("invalid guarded-exec invocation")
    expected_parent = int(arguments[0])
    command = list(arguments[2:])
    if not command:
        raise CorpusError("guarded-exec command is empty")
    arm_parent_death(expected_parent)
    os.environ.clear()
    os.environ.update(ENGINE_ENVIRONMENT)
    os.execv(command[0], command)
    return 125


def wait_for_exec_identity(
    pid: int, executable: Path, expected_argv: bytes | None = None, timeout: float = 5.0
) -> ProcessIdentity:
    expected_hash = sha256_regular(executable)
    expected_path = executable.resolve(strict=True)
    deadline = time.monotonic() + timeout
    last_error: BaseException | None = None
    while time.monotonic() < deadline:
        try:
            identity = capture_process_identity(pid)
            if (
                Path(identity.executable) == expected_path
                and identity.executable_sha256 == expected_hash
                and (expected_argv is None or identity.cmdline_sha256 == sha256_bytes(expected_argv))
            ):
                return identity
        except (CorpusError, OSError, ProcessLookupError) as exc:
            last_error = exc
        time.sleep(0.01)
    raise CorpusError(f"child did not exec the frozen engine: {last_error or 'timeout'}")


class ControllerLock:
    def __init__(self, run_root: Path, fingerprint: str):
        self.run_root = run_root
        self.fingerprint = fingerprint
        self.fd: int | None = None

    def __enter__(self) -> "ControllerLock":
        reject_symlink_components(self.run_root.parent)
        self.run_root.mkdir(parents=True, exist_ok=True)
        reject_symlink_components(self.run_root)
        self.fd = os.open(self.run_root / "controller.lock", os.O_RDWR | os.O_CREAT, 0o600)
        try:
            fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            os.close(self.fd)
            self.fd = None
            raise CorpusError("another corpus controller holds the run lock") from exc
        identity = capture_process_identity(os.getpid())
        atomic_write_json(
            self.run_root / "controller-owner.json",
            {"fingerprint": self.fingerprint, "process": identity.as_dict(), "started_at": utc_now()},
            mode=0o600,
        )
        return self

    def __exit__(self, exc_type: Any, exc: Any, traceback: Any) -> None:
        if self.fd is not None:
            fcntl.flock(self.fd, fcntl.LOCK_UN)
            os.close(self.fd)
            self.fd = None


def preflight_ports(ports: Iterable[int]) -> None:
    reservations: list[socket.socket] = []
    try:
        for port in ports:
            for kind in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
                sock = socket.socket(socket.AF_INET, kind)
                try:
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 0)
                    sock.bind(("0.0.0.0", port))
                    if kind == socket.SOCK_STREAM:
                        sock.listen(1)
                except OSError as exc:
                    sock.close()
                    protocol = "TCP" if kind == socket.SOCK_STREAM else "UDP"
                    raise CorpusError(f"{protocol} port {port} is unavailable: {exc}") from exc
                reservations.append(sock)
    finally:
        for sock in reservations:
            sock.close()


def next_attempt_directory(run_root: Path, map_name: str) -> tuple[int, Path]:
    if not MAP_NAME_RE.fullmatch(map_name):
        raise CorpusError(f"unsafe map name: {map_name!r}")
    reject_symlink_components(run_root)
    map_root = run_root / "runs" / map_name
    reject_symlink_components(map_root)
    map_root.mkdir(parents=True, exist_ok=True)
    reject_symlink_components(map_root)
    attempts = []
    for child in map_root.iterdir():
        match = re.fullmatch(r"attempt-([0-9]{4,})", child.name)
        if match:
            if not child.is_dir() or child.is_symlink():
                raise CorpusError(f"invalid attempt entry: {child}")
            attempts.append(int(match.group(1)))
    number = max(attempts, default=0) + 1
    attempt = map_root / f"attempt-{number:04d}"
    attempt.mkdir(mode=0o700)
    return number, attempt


def parse_generation_log(text: str, map_name: str, artifact: Path, attempt: Path) -> dict[str, Any]:
    roots: list[tuple[int, int, int]] = []
    writes: list[tuple[int, re.Match[str]]] = []
    failures: list[tuple[int, str]] = []
    ready: list[tuple[int, re.Match[str]]] = []
    lines = text.splitlines()
    for index, line in enumerate(lines):
        root_match = ROOT_RE.fullmatch(line)
        if root_match:
            roots.append((index, int(root_match.group(1)), int(root_match.group(2))))
        write_match = WRITE_RE.fullmatch(line)
        if write_match:
            writes.append((index, write_match))
        if FAILURE_RE.fullmatch(line):
            failures.append((index, line))
        ready_match = READY_RE.fullmatch(line)
        if ready_match:
            ready.append((index, ready_match))
    if len(roots) != 1:
        raise CorpusError(f"expected one objective-root line, found {len(roots)}")
    if len(writes) != 1:
        raise CorpusError(f"expected one final write line, found {len(writes)}")
    write_index, match = writes[0]
    root_index, red, blue = roots[0]
    if root_index >= write_index:
        raise CorpusError("objective-root line does not precede write line")
    banner_path = Path(match.group(1))
    if banner_path.is_absolute() or any(part in ("", ".", "..") for part in banner_path.parts):
        raise CorpusError("write banner path is not a safe relative attempt path")
    resolved_banner = (attempt / banner_path).resolve()
    expected_artifact = artifact.resolve()
    try:
        expected_artifact.relative_to(attempt.resolve())
    except ValueError as exc:
        raise CorpusError("artifact is outside its attempt directory") from exc
    if resolved_banner != expected_artifact or banner_path.name != f"{map_name}.rune":
        raise CorpusError("write banner does not name the requested attempt artifact")
    later = [line for index, line in failures if index > write_index]
    if later:
        raise CorpusError(f"failure after write: {later[-1]}")
    counts = {
        "seeds": int(match.group(2)),
        "links": int(match.group(3)),
        "mechanism_nodes": int(match.group(4)),
        "triggers": int(match.group(5)),
        "inventory_edges": int(match.group(6)),
        "plans": int(match.group(7)),
    }
    if red == blue or red >= counts["seeds"] or blue >= counts["seeds"]:
        raise CorpusError("objective roots are not distinct in-range seed indexes")
    matching_ready = [
        (index, item) for index, item in ready
        if index > write_index and item.group(1) == map_name
    ]
    if len(matching_ready) != 1:
        raise CorpusError(
            f"expected one post-write runtime-ready line, found {len(matching_ready)}"
        )
    _ready_index, ready_match = matching_ready[0]
    ready_counts = tuple(int(ready_match.group(index)) for index in range(2, 6))
    if ready_counts != (
        counts["seeds"], counts["links"], counts["mechanism_nodes"], counts["plans"]
    ):
        raise CorpusError("runtime-ready counts disagree with generator counts")
    return {"objective_roots": {"red": red, "blue": blue}, "counts": counts}


def _gate_report(data: bytes, label: str) -> dict[str, Any]:
    try:
        value = json.loads(data)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise CorpusError(f"{label} did not emit one JSON report") from exc
    if not isinstance(value, dict):
        raise CorpusError(f"{label} report is not an object")
    for field in REPORT_FIELDS:
        field_value = value.get(field)
        if type(field_value) is not int or field_value < 0:
            raise CorpusError(f"{label} has invalid {field}")
    if not isinstance(value.get("map_name"), str):
        raise CorpusError(f"{label} has invalid map_name")
    return value


def validate_gate_agreement(
    c_gnu_output: bytes,
    c_make_output: bytes,
    python_output: bytes,
    map_name: str,
    banner_counts: Mapping[str, int],
) -> dict[str, int]:
    gnu_report = _gate_report(c_gnu_output, "GNU C gate")
    make_report = _gate_report(c_make_output, "Make C gate")
    py_report = _gate_report(python_output, "Python gate")
    if any(report["map_name"] != map_name for report in (
        gnu_report, make_report, py_report
    )):
        raise CorpusError("gate report map mismatch")
    for field in REPORT_FIELDS:
        if gnu_report[field] != make_report[field]:
            raise CorpusError(f"GNU/Make C {field} count mismatch")
        if gnu_report[field] != py_report[field]:
            raise CorpusError(f"C/Python {field} count mismatch")
    banner_mapping = {
        "seed_count": "seeds",
        "link_count": "links",
        "node_count": "mechanism_nodes",
        "trigger_count": "triggers",
        "inventory_edge_count": "inventory_edges",
        "plan_count": "plans",
    }
    for report_name, banner_name in banner_mapping.items():
        if py_report[report_name] != banner_counts.get(banner_name):
            raise CorpusError(f"generator/{report_name} count mismatch")
    return {field: py_report[field] for field in REPORT_FIELDS}


def semantic_checkers_for_map(
    snapshot: Path, verified: Mapping[str, Any], map_name: str
) -> list[tuple[str, Path]]:
    """Resolve the immutable, ordered semantic gates applicable to one map."""
    roles = verified["by_role"]
    return [
        (str(item["name"]), snapshot / str(roles[item["role"]]["path"]))
        for item in verified["semantic_checkers"]
        if map_name in item["maps"]
    ]


def run_gates(
    artifact: Path,
    map_name: str,
    banner_counts: Mapping[str, int],
    *,
    acceptor_gnu: Path,
    acceptor_make: Path,
    python_interpreter: Path,
    runeio: Path,
    runelint: Path,
    objective_roots: Mapping[str, int] | None = None,
    semantic_checkers: Sequence[tuple[str, Path]] = (),
    log_directory: Path | None,
    runner: Callable[..., subprocess.CompletedProcess[bytes]] = subprocess.run,
    fingerprint: str | None = None,
) -> dict[str, Any]:
    before = regular_file_record(artifact)
    snapshot = python_interpreter.parents[2]
    try:
        verified = verify_snapshot(snapshot)
    except CorpusError:
        if runner is subprocess.run:
            raise
        # Focused fake-only callers may supply isolated paths without a full
        # snapshot.  Production calls cannot take this branch.
        verified = None
        layout = None
    else:
        layout = verified["python_runtime"]
    if acceptor_gnu == acceptor_make:
        raise GateIntegrityError("GNU and Make C acceptors are not distinct paths")
    commands = (
        ("c_gnu", [str(acceptor_gnu), str(artifact)]),
        ("c_make", [str(acceptor_make), str(artifact)]),
    )
    outputs: dict[str, bytes] = {}
    log_records: dict[str, dict[str, Any]] = {}

    def write_integrity(label: str, lifecycle: Mapping[str, Any] | None, rc: int | None, error: str | None, started: str) -> None:
        if log_directory is None:
            return
        state = dict(lifecycle or {})
        document = {
            "fingerprint": fingerprint, "mode": label, "started_at": started,
            "ended_at": utc_now(), "ready": state.get("ready"), "done": state.get("done"),
            "ready_identity": state.get("ready_identity"), "done_identity": state.get("done_identity"),
            "ready_maps_sha256": state.get("ready_maps_sha256"), "done_maps_sha256": state.get("done_maps_sha256"),
            "expected_command_sha256": state.get("expected_command_sha256"),
            "returncode": rc, "error": error,
        }
        atomic_write_json(log_directory / f"gate-{label}.integrity.json", document)
    for label, command in commands:
        completed = runner(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            env=dict(ACCEPTOR_ENVIRONMENT),
        )
        output = bytes(completed.stdout or b"")
        outputs[label] = output
        if log_directory is not None:
            path = log_directory / f"gate-{label}.log"
            atomic_write_bytes(path, output)
            log_records[label] = regular_file_record(path)
        if completed.returncode != 0:
            raise CorpusError(f"{label} gate exited {completed.returncode}")
        if regular_file_record(artifact) != before:
            raise GateIntegrityError(f"artifact changed during {label} gate")
    python_gates: list[tuple[str, Path, list[str]]] = [
        ("python", runeio, [str(artifact)]),
        ("lint", runelint, [str(artifact)]),
    ]
    semantic_labels: list[str] = []
    if semantic_checkers:
        if objective_roots is None or set(objective_roots) != {"red", "blue"}:
            raise GateIntegrityError("semantic gates require exact objective roots")
        for name, target in semantic_checkers:
            if SEMANTIC_CHECKER_NAME_RE.fullmatch(name) is None:
                raise GateIntegrityError("semantic checker has an unsafe name")
            label = f"semantic-{name}"
            semantic_labels.append(label)
            python_gates.append((
                label,
                target,
                ["--objective-roots", str(objective_roots["red"]),
                 str(objective_roots["blue"]), str(artifact)],
            ))
    if semantic_labels != sorted(set(semantic_labels)):
        raise GateIntegrityError("semantic checker execution plan is not unique and sorted")
    for label, target, target_arguments in python_gates:
        gate_started = utc_now()
        lifecycle: Mapping[str, Any] | None = None
        try:
            if layout is None:
                completed = runner(
                    [str(python_interpreter), *PYTHON_ISOLATION_FLAGS, "-c", PYTHON_GATE_BOOTSTRAP,
                     str(target.parent), str(target), *target_arguments],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
                    env=python_child_environment(python_interpreter.parent.parent),
                )
                rc, output, lifecycle = completed.returncode, bytes(completed.stdout or b""), {"ready": None, "done": None}
            else:
                rc, output, lifecycle = run_verified_python(
                    snapshot, layout, label, target, target_arguments, runner=runner
                )
        except CorpusError as exc:
            write_integrity(label, lifecycle, None, str(exc), gate_started)
            raise GateIntegrityError(str(exc)) from exc
        outputs[label] = output
        if log_directory is not None:
            path = log_directory / f"gate-{label}.log"
            atomic_write_bytes(path, output)
            log_records[label] = regular_file_record(path)
        write_integrity(label, lifecycle, rc, None, gate_started)
        if rc != 0:
            raise CorpusError(f"{label} gate exited {rc}")
        if regular_file_record(artifact) != before:
            raise GateIntegrityError(f"artifact changed during {label} gate")
        if label.startswith("semantic-"):
            try:
                semantic_report = json.loads(output)
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise CorpusError(f"{label} did not emit one JSON report") from exc
            if not isinstance(semantic_report, dict) or semantic_report.get("map_name") != map_name:
                raise CorpusError(f"{label} report map mismatch")
    decoded = validate_gate_agreement(
        outputs["c_gnu"], outputs["c_make"], outputs["python"],
        map_name, banner_counts,
    )
    return {
        "decoded_counts": decoded,
        "gate_output_sha256": {key: sha256_bytes(value) for key, value in outputs.items()},
        "gate_logs": log_records,
        "semantic_gate_labels": semantic_labels,
    }


def _relative_evidence_record(path: Path, run_root: Path) -> dict[str, Any]:
    resolved_root = run_root.resolve()
    resolved = path.resolve()
    try:
        relative = resolved.relative_to(resolved_root).as_posix()
    except ValueError as exc:
        raise CorpusError(f"evidence is outside run root: {path}") from exc
    return regular_file_record(resolved, logical_path=relative)


def _validate_evidence_record(run_root: Path, record: Mapping[str, Any]) -> Path:
    if not isinstance(record, Mapping) or set(record) != {"path", "mode", "size", "sha256"}:
        raise CorpusError("invalid evidence record schema")
    logical = _safe_logical_path(str(record.get("path", ""))).as_posix()
    path = run_root / logical
    actual = regular_file_record(path, logical_path=logical)
    for key in ("path", "mode", "size", "sha256"):
        if actual[key] != record.get(key):
            raise CorpusError(f"contaminated evidence: {logical} ({key})")
    return path


def _is_sha256(value: Any) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def _is_timestamp(value: Any) -> bool:
    if not isinstance(value, str) or not value.endswith("Z"):
        return False
    try:
        _datetime.datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError:
        return False
    return True


def _exact_nonnegative_ints(value: Any, fields: Iterable[str]) -> bool:
    expected = set(fields)
    return (
        isinstance(value, dict)
        and set(value) == expected
        and all(type(value[field]) is int and value[field] >= 0 for field in expected)
    )


def _validate_gate_integrity_evidence(
    run_root: Path, records: Sequence[Mapping[str, Any]], *, fingerprint: str, label: str,
) -> None:
    matching = [
        record for record in records
        if PurePosixPath(str(record.get("path", ""))).name == f"gate-{label}.integrity.json"
    ]
    if len(matching) != 1:
        raise CorpusError(f"PASS lacks one gate-{label}.integrity evidence record")
    path = _validate_evidence_record(run_root, matching[0])
    value, _raw = _load_json_regular(path)
    fields = {
        "fingerprint", "mode", "started_at", "ended_at", "ready", "done",
        "ready_identity", "done_identity", "ready_maps_sha256", "done_maps_sha256",
        "expected_command_sha256", "returncode", "error",
    }
    if not isinstance(value, dict) or set(value) != fields:
        raise CorpusError("gate integrity evidence schema is invalid")
    if value["fingerprint"] != fingerprint or value["mode"] != label or value["error"] is not None:
        raise CorpusError("gate integrity evidence identity is invalid")
    if not _is_timestamp(value["started_at"]) or not _is_timestamp(value["ended_at"]):
        raise CorpusError("gate integrity evidence timestamps are invalid")
    if type(value["returncode"]) is not int or value["returncode"] != 0:
        raise CorpusError("gate integrity evidence return code is invalid")
    if not _is_sha256(value["ready_maps_sha256"]) or not _is_sha256(value["done_maps_sha256"]) or not _is_sha256(value["expected_command_sha256"]):
        raise CorpusError("gate integrity evidence hashes are invalid")
    for phase in ("ready", "done"):
        report = value[phase]
        identity = value[f"{phase}_identity"]
        if not isinstance(report, dict) or report.get("phase") != phase.upper() or not isinstance(identity, dict):
            raise CorpusError("gate integrity evidence lifecycle is invalid")
        try:
            parsed = ProcessIdentity(**identity)
        except (TypeError, ValueError) as exc:
            raise CorpusError("gate integrity process identity is malformed") from exc
        if not _is_sha256(parsed.cmdline_sha256) or parsed.cmdline_sha256 != value["expected_command_sha256"]:
            raise CorpusError("gate integrity command identity disagrees with expected argv")


def _validate_terminal_schema(
    value: Mapping[str, Any],
    *,
    run_root: Path,
    attempt: Path,
    map_name: str,
    fingerprint: str,
    stable_port: int,
) -> Path:
    if set(value) != TERMINAL_RESULT_FIELDS:
        raise CorpusError("terminal result has an incomplete or extra field set")
    if value["map"] != map_name or value["fingerprint"] != fingerprint:
        raise CorpusError("terminal result identity mismatch")
    if value["stable_port"] != stable_port:
        raise CorpusError("terminal result stable port mismatch")
    attempt_number = value["attempt"]
    if type(attempt_number) is not int or attempt_number <= 0:
        raise CorpusError("terminal result has invalid attempt")
    if attempt.name != f"attempt-{attempt_number:04d}":
        raise CorpusError("terminal result attempt path mismatch")
    classification = value["classification"]
    if classification not in TERMINAL_CLASSIFICATIONS:
        raise CorpusError("terminal result has invalid classification")
    if not _is_timestamp(value["started_at"]) or not _is_timestamp(value["ended_at"]):
        raise CorpusError("terminal result has invalid lifecycle timestamps")
    if not isinstance(value["detail"], str) or not value["detail"]:
        raise CorpusError("terminal result has invalid detail")
    if value["normalized_signature"] != normalized_signature(classification, value["detail"]):
        raise CorpusError("terminal result normalized signature mismatch")
    if not _is_sha256(value["command_sha256"]):
        raise CorpusError("terminal result has invalid command hash")

    records = value["evidence"]
    if not isinstance(records, list) or not records:
        raise CorpusError("terminal result has no evidence")
    paths = [_validate_evidence_record(run_root, record) for record in records]
    if len(paths) != len(set(paths)):
        raise CorpusError("terminal result repeats evidence")
    expected_paths = {
        path
        for path in attempt.rglob("*")
        if path.is_file() and not path.is_symlink() and path.name != "result.json"
    }
    if set(paths) != expected_paths:
        raise CorpusError("terminal result evidence set is incomplete")

    owner = attempt / "owner.json"
    if value["owner_record"] != str(owner.relative_to(run_root)) or owner not in paths:
        raise CorpusError("terminal result owner record mismatch")
    owner_value, _owner_raw = _load_json_regular(owner)
    owner_fields = {
        "fingerprint", "map", "attempt", "created_at", "process",
        "pidfd_captured", "command_sha256",
    }
    if not isinstance(owner_value, dict) or set(owner_value) != owner_fields or (
        owner_value.get("fingerprint") != fingerprint
        or owner_value.get("map") != map_name
        or owner_value.get("attempt") != attempt_number
    ):
        raise CorpusError("terminal result owner identity mismatch")
    if (
        not _is_timestamp(owner_value["created_at"])
        or type(owner_value["pidfd_captured"]) is not bool
        or not _is_sha256(owner_value["command_sha256"])
        or owner_value["command_sha256"] != value["command_sha256"]
    ):
        raise CorpusError("terminal result owner lifecycle mismatch")
    process_value = owner_value["process"]
    if process_value is None:
        if classification != "INFRA_FAIL" or owner_value["pidfd_captured"]:
            raise CorpusError("only pre-identity INFRA_FAIL may lack a process")
    else:
        try:
            identity = ProcessIdentity(**process_value)
        except (TypeError, ValueError) as exc:
            raise CorpusError("terminal owner process identity is malformed") from exc
        if (
            type(identity.pid) is not int or identity.pid <= 0
            or type(identity.start_ticks) is not int or identity.start_ticks < 0
            or re.fullmatch(r"[0-9a-f]{8}-[0-9a-f-]{27}", identity.boot_id) is None
            or not Path(identity.executable).is_absolute()
            or not _is_sha256(identity.executable_sha256)
            or not _is_sha256(identity.cmdline_sha256)
        ):
            raise CorpusError("terminal owner process identity has invalid fields")
        expected_engine = (attempt / "private" / CORPUS_ENGINE_BASENAME).resolve(strict=True)
        if (
            Path(identity.executable) != expected_engine
            or identity.executable_sha256 != sha256_regular(expected_engine)
        ):
            raise CorpusError("terminal owner process identity disagrees with evidence")
        if classification == "PASS" and identity.cmdline_sha256 != value["command_sha256"]:
            raise CorpusError("PASS owner observed command differs from expected command")

    server_log = attempt / "server.log"
    if server_log not in paths or not _is_sha256(value["server_log_sha256"]):
        raise CorpusError("terminal result lacks a server log")
    if value["server_log_sha256"] != sha256_regular(server_log):
        raise CorpusError("terminal result server log hash mismatch")

    artifact_record = value["artifact"]
    if artifact_record is None:
        if value["artifact_sha256"] is not None:
            raise CorpusError("terminal result has an orphan artifact hash")
        artifact = server_log
    else:
        artifact = _validate_evidence_record(run_root, artifact_record)
        if artifact not in paths or value["artifact_sha256"] != artifact_record["sha256"]:
            raise CorpusError("terminal result artifact mismatch")

    roots = value["objective_roots"]
    banner = value["banner_counts"]
    if (roots is None) != (banner is None):
        raise CorpusError("terminal result has partial generation report fields")
    if roots is not None and not _exact_nonnegative_ints(roots, ("red", "blue")):
        raise CorpusError("terminal result has invalid objective roots")
    banner_fields = (
        "seeds", "links", "mechanism_nodes", "triggers", "inventory_edges", "plans"
    )
    if banner is not None and not _exact_nonnegative_ints(banner, banner_fields):
        raise CorpusError("terminal result has invalid banner counts")

    if classification == "PASS":
        if artifact_record is None or roots is None or banner is None:
            raise CorpusError("PASS lacks artifact or generation reports")
        if value["failure_line"] is not None:
            raise CorpusError("PASS contains a failure line")
        if not _exact_nonnegative_ints(value["decoded_counts"], REPORT_FIELDS):
            raise CorpusError("PASS has invalid decoded counts")
        semantic_labels = value["semantic_gate_labels"]
        if (
            not isinstance(semantic_labels, list)
            or semantic_labels != sorted(set(semantic_labels))
            or any(
                not isinstance(label, str)
                or not label.startswith("semantic-")
                or SEMANTIC_CHECKER_NAME_RE.fullmatch(
                    label.removeprefix("semantic-")
                ) is None
                for label in semantic_labels
            )
        ):
            raise CorpusError("PASS has invalid semantic gate labels")
        expected_gate_labels = BASE_GATE_LABELS | set(semantic_labels)
        for field in ("gate_output_sha256", "gate_log_sha256"):
            hashes = value[field]
            if not isinstance(hashes, dict) or set(hashes) != expected_gate_labels or not all(
                _is_sha256(item) for item in hashes.values()
            ):
                raise CorpusError(f"PASS has invalid {field}")
        for label, expected_hash in value["gate_log_sha256"].items():
            matching = [
                record for record in records
                if PurePosixPath(str(record["path"])).name == f"gate-{label}.log"
            ]
            if len(matching) != 1 or matching[0]["sha256"] != expected_hash:
                raise CorpusError("PASS gate log evidence mismatch")
        for label in ("python", "lint", *semantic_labels):
            _validate_gate_integrity_evidence(
                run_root, records, fingerprint=fingerprint, label=label,
            )
        decoded = value["decoded_counts"]
        banner_mapping = {
            "seed_count": "seeds",
            "link_count": "links",
            "node_count": "mechanism_nodes",
            "trigger_count": "triggers",
            "inventory_edge_count": "inventory_edges",
            "plan_count": "plans",
        }
        if any(decoded[report] != banner[banner_name] for report, banner_name in banner_mapping.items()):
            raise CorpusError("PASS stored gate counts disagree with banner counts")
        if decoded["edge_count"] != decoded["inventory_edge_count"] + decoded["plan_edge_count"]:
            raise CorpusError("PASS stored edge counts are inconsistent")

        cold_owner = attempt / "cold-load" / "owner.json"
        cold_log = attempt / "cold-load" / "server.log"
        cold_artifact = (
            attempt / "cold-load" / "private" / "game" / "maps"
            / f"{map_name}.rune"
        )
        if (
            value["cold_load_owner_record"]
            != str(cold_owner.relative_to(run_root))
            or cold_owner not in paths
            or cold_log not in paths
            or cold_artifact not in paths
        ):
            raise CorpusError("PASS lacks complete fresh cold-load evidence")
        if (
            not _is_sha256(value["cold_load_command_sha256"])
            or not _is_sha256(value["cold_load_log_sha256"])
            or value["cold_load_log_sha256"] != sha256_regular(cold_log)
            or sha256_regular(cold_artifact) != value["artifact_sha256"]
        ):
            raise CorpusError("PASS fresh cold-load hashes disagree")
        cold_owner_value, _cold_owner_raw = _load_json_regular(cold_owner)
        if (
            not isinstance(cold_owner_value, dict)
            or set(cold_owner_value) != owner_fields
            or cold_owner_value.get("fingerprint") != fingerprint
            or cold_owner_value.get("map") != map_name
            or cold_owner_value.get("attempt") != attempt_number
            or cold_owner_value.get("pidfd_captured") is not True
            or cold_owner_value.get("command_sha256")
            != value["cold_load_command_sha256"]
            or not _is_timestamp(cold_owner_value.get("created_at"))
        ):
            raise CorpusError("PASS fresh cold-load owner identity is invalid")
        try:
            cold_identity = ProcessIdentity(**cold_owner_value["process"])
        except (TypeError, ValueError) as exc:
            raise CorpusError("PASS fresh cold-load process identity is malformed") from exc
        cold_engine = (
            attempt / "cold-load" / "private" / CORPUS_ENGINE_BASENAME
        ).resolve(strict=True)
        if (
            type(cold_identity.pid) is not int
            or cold_identity.pid <= 0
            or type(cold_identity.start_ticks) is not int
            or cold_identity.start_ticks < 0
            or re.fullmatch(
                r"[0-9a-f]{8}-[0-9a-f-]{27}", cold_identity.boot_id
            ) is None
            or not Path(cold_identity.executable).is_absolute()
            or not _is_sha256(cold_identity.executable_sha256)
            or not _is_sha256(cold_identity.cmdline_sha256)
            or Path(cold_identity.executable) != cold_engine
            or cold_identity.executable_sha256 != sha256_regular(cold_engine)
            or cold_identity.cmdline_sha256 != value["cold_load_command_sha256"]
            or (cold_identity.pid, cold_identity.boot_id, cold_identity.start_ticks)
            == (identity.pid, identity.boot_id, identity.start_ticks)
        ):
            raise CorpusError("PASS fresh cold-load is not a distinct authenticated process")
        try:
            cold_text = cold_log.read_text(encoding="utf-8", errors="strict")
        except UnicodeDecodeError as exc:
            raise CorpusError("PASS fresh cold-load log is not valid UTF-8") from exc
        parse_cold_load_log(cold_text, map_name, banner)
    else:
        if any(value[field] is not None for field in (
            "decoded_counts", "gate_output_sha256", "gate_log_sha256",
            "semantic_gate_labels", "cold_load_owner_record",
            "cold_load_command_sha256", "cold_load_log_sha256",
        )):
            raise CorpusError("non-PASS result contains successful gate reports")
        failure_line = value["failure_line"]
        if failure_line is not None and (
            classification != "GEN_FAIL"
            or failure_line != value["detail"]
            or FAILURE_RE.fullmatch(failure_line) is None
        ):
            raise CorpusError("terminal result has invalid failure line")
    return artifact


def _recheck_pass_gates(
    result: Mapping[str, Any],
    *,
    snapshot: Path,
    artifact: Path,
    gate_runner: Callable[..., subprocess.CompletedProcess[bytes]],
    runtime_preflighted: bool = False,
) -> None:
    # Resume acceptance is a fresh authority decision, never a cached probe.
    preflight_python_runtime(snapshot, runner=gate_runner)
    verified = verify_snapshot(snapshot)
    roles = verified["by_role"]
    interpreter = snapshot / verified["python_runtime"]["interpreter"]["path"]
    checked = run_gates(
        artifact,
        str(result["map"]),
        result["banner_counts"],
        acceptor_gnu=snapshot / roles["acceptor_gnu"]["path"],
        acceptor_make=snapshot / roles["acceptor_make"]["path"],
        python_interpreter=interpreter,
        runeio=snapshot / roles["runeio"]["path"],
        runelint=snapshot / roles["runelint"]["path"],
        objective_roots=result["objective_roots"],
        semantic_checkers=semantic_checkers_for_map(
            snapshot, verified, str(result["map"])
        ),
        log_directory=None,
        runner=gate_runner,
        fingerprint=str(result["fingerprint"]),
    )
    if (
        checked["decoded_counts"] != result["decoded_counts"]
        or checked["gate_output_sha256"] != result["gate_output_sha256"]
        or checked["semantic_gate_labels"] != result["semantic_gate_labels"]
    ):
        raise CorpusError("rechecked PASS gates disagree with stored reports")


def validate_resumable_pass(
    result_path: Path,
    *,
    run_root: Path,
    fingerprint: str,
    fingerprint_document_bytes: bytes,
    stable_port: int,
    snapshot: Path,
    gate_runner: Callable[..., subprocess.CompletedProcess[bytes]] = subprocess.run,
    runtime_preflighted: bool = False,
) -> bool:
    if not result_path.exists():
        return False
    try:
        result, _raw = _load_json_regular(result_path)
        if not isinstance(result, dict) or not isinstance(result.get("map"), str):
            return False
        if set(result) != TERMINAL_RESULT_FIELDS:
            return False
        if result.get("classification") != "PASS":
            return False
        stored_document = run_root / "fingerprint-document.json"
        _value, stored_bytes = _load_json_regular(stored_document)
        if stored_bytes != fingerprint_document_bytes:
            raise GateIntegrityError("stored fingerprint document changed for a PASS result")
        try:
            expected_document = json.loads(fingerprint_document_bytes)
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise CorpusError("invalid expected fingerprint document") from exc
        if verify_fingerprint_document(snapshot, expected_document) != fingerprint:
            raise GateIntegrityError("frozen fingerprint changed for a PASS result")
        validated = validate_terminal_result(
            result_path,
            run_root=run_root,
            map_name=result["map"],
            fingerprint=fingerprint,
            stable_port=stable_port,
            snapshot=snapshot,
            fingerprint_document_bytes=fingerprint_document_bytes,
            gate_runner=gate_runner,
            recheck_pass_gates=True,
            runtime_preflighted=runtime_preflighted,
        )
        if validated is None:
            raise GateIntegrityError("stored PASS result failed integrity validation")
        return True
    except GateIntegrityError:
        raise
    except (CorpusError, KeyError, OSError, TypeError, ValueError) as exc:
        raise GateIntegrityError(f"stored PASS result has an integrity failure: {exc}") from exc


def normalized_signature(classification: str, detail: str) -> str:
    normalized = detail.strip().lower()
    if classification == "GEN_FAIL" and FAILURE_RE.fullmatch(detail):
        # The artifact path and failing record index identify the individual
        # map, not the systemic failure class.  Preserve the exact line in the
        # result while making equivalent replay failures group together.
        normalized = re.sub(
            r"^rune: rejected .+? stage=", "rune: rejected <artifact> stage=",
            normalized,
        )
    normalized = re.sub(r"\b[0-9]+\b", "#", normalized)
    return sha256_bytes(f"{classification}:{normalized}".encode("utf-8"))[:24]


def publish_result(run_root: Path, map_name: str, result: Mapping[str, Any], attempt: Path) -> Path:
    if result.get("classification") not in TERMINAL_CLASSIFICATIONS:
        raise CorpusError("cannot publish a nonterminal classification")
    published = json.loads(json.dumps(result))
    for record in published.get("evidence", []):
        if isinstance(record, dict) and type(record.get("mode")) is int:
            record["mode"] &= ~0o222
    artifact_record = published.get("artifact")
    if isinstance(artifact_record, dict) and type(artifact_record.get("mode")) is int:
        artifact_record["mode"] &= ~0o222
    # Preserve the complete terminal record with the immutable attempt as well
    # as at the per-map resume pointer.  A later attempt may advance the
    # pointer, but can never erase the prior record.
    atomic_write_json(attempt / "result.json", published)
    freeze_tree(attempt)
    fsync_tree(attempt)
    result_path = run_root / "runs" / map_name / "result.json"
    atomic_write_json(result_path, published)
    return result_path


def validate_terminal_result(
    result_path: Path,
    *,
    run_root: Path,
    map_name: str,
    fingerprint: str,
    stable_port: int,
    snapshot: Path | None = None,
    fingerprint_document_bytes: bytes | None = None,
    gate_runner: Callable[..., subprocess.CompletedProcess[bytes]] = subprocess.run,
    recheck_pass_gates: bool = True,
    runtime_preflighted: bool = False,
) -> tuple[dict[str, Any], bytes] | None:
    """Validate one terminal pointer and every immutable referenced byte."""
    try:
        value, raw = _load_json_regular(result_path)
        if not isinstance(value, dict):
            return None
        attempt_number = value.get("attempt")
        if type(attempt_number) is not int or attempt_number <= 0:
            return None
        attempt = run_root / "runs" / map_name / f"attempt-{attempt_number:04d}"
        reject_symlink_components(attempt)
        attempt_value, attempt_raw = _load_json_regular(attempt / "result.json")
        if attempt_raw != raw or attempt_value != value:
            return None
        artifact = _validate_terminal_schema(
            value,
            run_root=run_root,
            attempt=attempt,
            map_name=map_name,
            fingerprint=fingerprint,
            stable_port=stable_port,
        )
        if value["classification"] == "PASS":
            if snapshot is None or fingerprint_document_bytes is None:
                return None
            stored_document = run_root / "fingerprint-document.json"
            _stored, stored_bytes = _load_json_regular(stored_document)
            if stored_bytes != fingerprint_document_bytes:
                return None
            expected_document = json.loads(fingerprint_document_bytes)
            if verify_fingerprint_document(snapshot, expected_document) != fingerprint:
                return None
            if recheck_pass_gates:
                _recheck_pass_gates(
                    value,
                    snapshot=snapshot,
                    artifact=artifact,
                    gate_runner=gate_runner,
                    runtime_preflighted=runtime_preflighted,
                )
        return value, raw
    except (CorpusError, OSError, KeyError, TypeError, ValueError, json.JSONDecodeError):
        return None


def regenerate_reports(
    run_root: Path,
    maps: Sequence[str],
    fingerprint: str,
    started_at: str,
    *,
    ended_at: str | None = None,
    publish_heartbeat: bool = True,
    snapshot: Path | None = None,
    fingerprint_document_bytes: bytes | None = None,
    port_base: int = DEFAULT_PORT_BASE,
    gate_runner: Callable[..., subprocess.CompletedProcess[bytes]] = subprocess.run,
    recheck_pass_gates: bool = True,
    runtime_preflighted: bool = False,
) -> dict[str, Any]:
    results: list[dict[str, Any]] = []
    counts: dict[str, int] = {}
    for name in maps:
        path = run_root / "runs" / name / "result.json"
        validated = validate_terminal_result(
            path,
            run_root=run_root,
            map_name=name,
            fingerprint=fingerprint,
            stable_port=port_base + maps.index(name),
            snapshot=snapshot,
            fingerprint_document_bytes=fingerprint_document_bytes,
            gate_runner=gate_runner,
            recheck_pass_gates=recheck_pass_gates,
            runtime_preflighted=runtime_preflighted,
        )
        if validated is None:
            continue
        value, raw = validated
        classification = str(value.get("classification", ""))
        if classification not in TERMINAL_CLASSIFICATIONS:
            continue
        counts[classification] = counts.get(classification, 0) + 1
        results.append(
            {
                "map": name,
                "classification": classification,
                "result": str(path.relative_to(run_root)),
                "result_sha256": sha256_bytes(raw),
            }
        )
    complete = (
        len(results) == CORPUS_SIZE
        and len(maps) == CORPUS_SIZE
        and recheck_pass_gates
        and _is_timestamp(started_at)
        and _is_timestamp(ended_at)
    )
    summary = {
        "fingerprint": fingerprint,
        "total": CORPUS_SIZE,
        "counts": counts,
        "maps": results,
        "started_at": started_at,
        "ended_at": ended_at if complete else None,
        "complete": complete,
    }
    atomic_write_json(run_root / "summary.json", summary)
    rows = ["map\tclassification\tresult_sha256"]
    rows.extend(f"{item['map']}\t{item['classification']}\t{item['result_sha256']}" for item in results)
    atomic_write_bytes(run_root / "summary.tsv", ("\n".join(rows) + "\n").encode("utf-8"))
    if publish_heartbeat:
        heartbeat = {
            "fingerprint": fingerprint,
            "updated_at": utc_now(),
            "terminal": len(results),
            "total": CORPUS_SIZE,
            "active": [],
            "complete": complete,
        }
        atomic_write_json(run_root / "heartbeat.json", heartbeat)
    return summary


class HeartbeatPublisher:
    """Thread-safe atomic live heartbeat for bounded concurrent attempts."""

    def __init__(self, run_root: Path, fingerprint: str, total: int):
        self.run_root = run_root
        self.fingerprint = fingerprint
        self.total = total
        self.controller_process = capture_process_identity(os.getpid()).as_dict()
        self.lock = threading.Lock()
        self.sequence = 0
        self.terminal = 0
        self.active: dict[str, dict[str, Any]] = {}
        self.publish()

    def _document(self, complete: bool = False) -> dict[str, Any]:
        return {
            "fingerprint": self.fingerprint,
            "controller_process": self.controller_process,
            "sequence": self.sequence,
            "updated_at": utc_now(),
            "terminal": self.terminal,
            "total": self.total,
            "active": [self.active[name] for name in sorted(self.active)],
            "complete": complete,
        }

    def publish(self, *, complete: bool = False) -> None:
        with self.lock:
            self.sequence += 1
            atomic_write_json(self.run_root / "heartbeat.json", self._document(complete))

    def event(self, event: str, map_name: str, details: Mapping[str, Any] | None = None) -> None:
        with self.lock:
            if event == "active":
                self.active[map_name] = {"map": map_name, **dict(details or {})}
            elif event == "beat":
                if map_name in self.active:
                    self.active[map_name]["heartbeat_at"] = utc_now()
            elif event == "inactive":
                self.active.pop(map_name, None)
            elif event == "terminal":
                self.active.pop(map_name, None)
                self.terminal += 1
            else:
                raise CorpusError(f"unknown heartbeat event: {event}")
            self.sequence += 1
            atomic_write_json(self.run_root / "heartbeat.json", self._document(False))

    def finish(self, terminal: int, complete: bool) -> None:
        with self.lock:
            self.terminal = terminal
            self.active.clear()
            self.sequence += 1
            atomic_write_json(self.run_root / "heartbeat.json", self._document(complete))


def run_bounded(
    assignments: Sequence[Mapping[str, Any]],
    jobs: int,
    worker: Callable[[Mapping[str, Any]], Any],
    on_terminal: Callable[[Mapping[str, Any], Any], None] | None = None,
) -> list[Any]:
    """Run assignments with a hard concurrency ceiling."""
    if jobs <= 0:
        raise CorpusError("job count must be positive")
    results: list[Any] = []
    if jobs == 1:
        for assignment in assignments:
            result = worker(assignment)
            results.append(result)
            if on_terminal is not None:
                on_terminal(assignment, result)
        return results
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
        pending = {executor.submit(worker, item): item for item in assignments}
        for future in concurrent.futures.as_completed(pending):
            assignment = pending[future]
            result = future.result()
            results.append(result)
            if on_terminal is not None:
                on_terminal(assignment, result)
    return results


def recover_stale_owned_child(
    owner_record: Path,
    *,
    pidfd_opener: Callable[[int], int | None] = open_pidfd,
    sender: Callable[[int, int], None] | None = None,
) -> bool:
    """Recover one stale child only when the complete durable identity matches."""
    owner, _raw = _load_json_regular(owner_record)
    try:
        identity = ProcessIdentity(**owner["process"])
    except (KeyError, TypeError) as exc:
        raise CorpusError(f"malformed stale owner record: {owner_record}") from exc
    if not process_identity_matches(identity):
        raise CorpusError("stale child ownership cannot be proven; process left untouched")
    pidfd = pidfd_opener(identity.pid)
    try:
        if not signal_owned_child(identity, owner_record, signal.SIGTERM, pidfd=pidfd, sender=sender):
            raise CorpusError("stale child descriptor unavailable; process left untouched")
    finally:
        if pidfd is not None:
            os.close(pidfd)
    return True


def recover_stale_attempts(
    run_root: Path,
    selected_maps: Sequence[str],
    fingerprint: str,
    assignments: Mapping[str, Mapping[str, Any]],
) -> int:
    """Recover unfinished attempts and durably record fail-closed outcomes."""
    recovered = 0
    for map_name in selected_maps:
        map_root = run_root / "runs" / map_name
        if not map_root.exists():
            continue
        for attempt in sorted(map_root.glob("attempt-*")):
            owner = attempt / "owner.json"
            terminal = attempt / "result.json"
            if owner.exists() and not terminal.exists():
                owner_value, _owner_bytes = _load_json_regular(owner)
                if not isinstance(owner_value, dict):
                    raise CorpusError(f"malformed stale owner record: {owner}")
                process_value = owner_value.get("process")
                identity: ProcessIdentity | None
                if process_value is None:
                    identity = None
                else:
                    try:
                        identity = ProcessIdentity(**process_value)
                    except (TypeError, ValueError) as exc:
                        raise CorpusError(f"malformed stale owner record: {owner}") from exc
                attempt_match = re.fullmatch(r"attempt-([0-9]+)", attempt.name)
                attempt_number = int(attempt_match.group(1)) if attempt_match else 0
                owner_matches = (
                    owner_value.get("fingerprint") == fingerprint
                    and owner_value.get("map") == map_name
                    and owner_value.get("attempt") == attempt_number
                )
                matched = (
                    owner_matches
                    and identity is not None
                    and process_identity_matches(identity)
                )
                recovery_detail = (
                    "stale attempt ended before process identity capture; no process signaled"
                    if identity is None
                    else "stale child was already gone or its PID identity no longer matched; "
                    "nonmatching process left untouched"
                )
                if matched:
                    assert identity is not None
                    recover_stale_owned_child(owner)
                    deadline = time.monotonic() + 5.0
                    while time.monotonic() < deadline and process_identity_matches(identity):
                        time.sleep(0.05)
                    if process_identity_matches(identity):
                        pidfd = open_pidfd(identity.pid)
                        try:
                            if not signal_owned_child(
                                identity, owner, signal.SIGKILL, pidfd=pidfd
                            ):
                                raise CorpusError(
                                    "stale child remained live and ownership could not be proven"
                                )
                        finally:
                            if pidfd is not None:
                                os.close(pidfd)
                        deadline = time.monotonic() + 5.0
                        while time.monotonic() < deadline and process_identity_matches(identity):
                            time.sleep(0.05)
                        if process_identity_matches(identity):
                            raise CorpusError("stale owned child remained live after descriptor KILL")
                    recovery_detail = "stale owned child was stopped through verified descriptors"
                server_log = attempt / "server.log"
                if not server_log.exists():
                    atomic_write_bytes(server_log, b"")
                evidence = [
                    _relative_evidence_record(path, run_root)
                    for path in sorted(attempt.rglob("*"))
                    if path.is_file() and not path.is_symlink()
                ]
                command_hash = owner_value.get("command_sha256")
                if not _is_sha256(command_hash):
                    engine = attempt / "private" / CORPUS_ENGINE_BASENAME
                    configs = sorted((attempt / "private" / "game").glob("*.cfg")) if engine.exists() else []
                    if engine.is_file() and len(configs) == 1:
                        expected_command = [str(engine)] + [
                            value.format(port=int(assignments[map_name]["port"]), map=map_name, config=configs[0].name)
                            for value in DEFAULT_ENGINE_ARGUMENTS
                        ]
                        command_hash = sha256_bytes(_nul_argv(expected_command))
                        recovery_detail += "; reconstructed expected q2 command from immutable attempt inputs"
                    else:
                        command_hash = sha256_bytes(b"missing-expected-q2-command")
                        recovery_detail += "; INFRA missing expected q2 command hash"
                    owner_value["command_sha256"] = command_hash
                    owner_value.setdefault("pidfd_captured", False)
                    atomic_write_json(owner, owner_value, mode=0o600)
                started_at = owner_value.get("created_at")
                if not _is_timestamp(started_at):
                    started_at = utc_now()
                result = {
                    "fingerprint": fingerprint,
                    "map": map_name,
                    "stable_port": int(assignments[map_name]["port"]),
                    "attempt": attempt_number,
                    "started_at": started_at,
                    "ended_at": utc_now(),
                    "classification": "INFRA_FAIL",
                    "normalized_signature": normalized_signature("INFRA_FAIL", recovery_detail),
                    "detail": recovery_detail,
                    "failure_line": None,
                    "command_sha256": command_hash,
                    "owner_record": str(owner.relative_to(run_root)),
                    "evidence": evidence,
                    "server_log_sha256": sha256_regular(server_log) if server_log.exists() else None,
                    "artifact": None,
                    "artifact_sha256": None,
                    "objective_roots": None,
                    "banner_counts": None,
                    "decoded_counts": None,
                    "gate_output_sha256": None,
                    "gate_log_sha256": None,
                    "semantic_gate_labels": None,
                    "cold_load_owner_record": None,
                    "cold_load_command_sha256": None,
                    "cold_load_log_sha256": None,
                }
                publish_result(run_root, map_name, result, attempt)
                recovered += 1
    return recovered


def _copy_snapshot_file(snapshot: Path, entry: Mapping[str, Any], destination: Path) -> None:
    source = snapshot / str(entry["path"])
    regular_file_record(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination, follow_symlinks=False)
    destination.chmod(int(entry["mode"]))
    if sha256_regular(destination) != entry["sha256"]:
        raise CorpusError(f"private input copy mismatch: {destination}")


def validate_engine_arguments(arguments: Sequence[str]) -> None:
    values = tuple(arguments)
    if values != DEFAULT_ENGINE_ARGUMENTS:
        raise CorpusError("engine arguments must equal the closed private launch template")


def parse_cold_load_log(
    text: str, map_name: str, banner_counts: Mapping[str, int]
) -> dict[str, int]:
    """Accept exactly one ordinary-load READY record and no generation write."""
    lines = text.splitlines()
    failure = last_anchored_failure(lines)
    if failure is not None:
        raise CorpusError(f"fresh cold-load rejected artifact: {failure}")
    if any(WRITE_RE.fullmatch(line) is not None for line in lines):
        raise CorpusError("fresh cold-load unexpectedly generated an artifact")
    ready = [match for line in lines if (match := READY_RE.fullmatch(line))]
    matching = [match for match in ready if match.group(1) == map_name]
    if len(ready) != 1 or len(matching) != 1:
        raise CorpusError(
            f"fresh cold-load expected one runtime-ready line, found {len(matching)}"
        )
    counts = {
        "seeds": int(matching[0].group(2)),
        "links": int(matching[0].group(3)),
        "mechanism_nodes": int(matching[0].group(4)),
        "plans": int(matching[0].group(5)),
    }
    if any(counts[name] != banner_counts.get(name) for name in counts):
        raise CorpusError("fresh cold-load counts disagree with generator counts")
    return counts


def stage_private_inputs(
    attempt: Path,
    snapshot: Path,
    map_name: str,
) -> tuple[Path, Path, Path, str]:
    """Create one attempt's private engine/game tree from frozen inputs."""
    roles = verify_snapshot(snapshot)["by_role"]
    private = attempt / "private"
    if CORPUS_ENGINE_BASENAME[:15] == "q2ded":
        raise CorpusError("corpus engine basename collides with the fleet process name")
    engine = private / CORPUS_ENGINE_BASENAME
    game = private / "game"
    artifact = game / "maps" / f"{map_name}.rune"
    _copy_snapshot_file(snapshot, roles["engine"], engine)
    engine.chmod(engine.stat().st_mode | stat.S_IXUSR)
    _copy_snapshot_file(snapshot, roles["module_primary"], game / "game.so")
    _copy_snapshot_file(snapshot, roles["module_secondary"], game / "gamex86_64.so")
    asset = roles[f"asset:{map_name}"]
    _copy_snapshot_file(snapshot, asset, game / "maps" / f"{map_name}.bsp")
    config_name = Path(str(roles["generator_config"]["path"])).name
    _copy_snapshot_file(snapshot, roles["generator_config"], game / config_name)
    return private, engine, artifact, config_name


def run_fresh_cold_load(
    attempt: Path,
    snapshot: Path,
    map_name: str,
    stable_port: int,
    fingerprint: str,
    attempt_number: int,
    source_artifact: Path,
    banner_counts: Mapping[str, int],
    generation_identity: ProcessIdentity,
    *,
    timeout: float,
    engine_arguments: Sequence[str] = DEFAULT_ENGINE_ARGUMENTS,
) -> dict[str, Any]:
    """Load gated bytes in a second private q2ded and return frozen evidence."""
    source_before = regular_file_record(source_artifact)
    cold_root = attempt / "cold-load"
    private, engine, artifact, config_name = stage_private_inputs(
        cold_root, snapshot, map_name
    )
    shutil.copyfile(source_artifact, artifact, follow_symlinks=False)
    artifact.chmod(0o444)
    if regular_file_record(source_artifact) != source_before:
        raise GateIntegrityError("artifact changed while staging fresh cold-load")
    staged = regular_file_record(artifact)
    if staged["sha256"] != source_before["sha256"] or staged["size"] != source_before["size"]:
        raise GateIntegrityError("fresh cold-load artifact copy mismatch")

    command = [str(engine)] + [
        value.format(port=stable_port, map=map_name, config=config_name)
        for value in engine_arguments
    ]
    command_hash = sha256_bytes(_nul_argv(command))
    owner_path = cold_root / "owner.json"
    log_path = cold_root / "server.log"
    process: subprocess.Popen[bytes] | None = None
    identity: ProcessIdentity | None = None
    pidfd: int | None = None
    shutdown_error: CorpusError | None = None
    log_stream = log_path.open("wb")
    try:
        atomic_write_json(owner_path, {
            "fingerprint": fingerprint, "attempt": attempt_number,
            "map": map_name, "process": None, "pidfd_captured": False,
            "command_sha256": command_hash, "created_at": utc_now(),
        }, mode=0o600)
        verified = verify_snapshot(snapshot)
        guarded_command = _private_python_command(
            snapshot, verified["python_runtime"], GUARD_BOOTSTRAP,
            str(os.getpid()), "--", *command,
        )
        process = subprocess.Popen(
            guarded_command, cwd=private, stdin=subprocess.PIPE,
            stdout=log_stream, stderr=subprocess.STDOUT, close_fds=True,
            env=dict(PYTHON_ENVIRONMENT),
        )
        pidfd = open_pidfd(process.pid)
        if pidfd is None:
            raise GateIntegrityError("fresh cold-load engine has no pidfd")
        identity = wait_for_exec_identity(process.pid, engine, _nul_argv(command))
        if (
            identity.pid, identity.boot_id, identity.start_ticks
        ) == (
            generation_identity.pid, generation_identity.boot_id,
            generation_identity.start_ticks,
        ):
            raise GateIntegrityError("fresh cold-load reused the generation process identity")
        atomic_write_json(owner_path, {
            "fingerprint": fingerprint, "attempt": attempt_number,
            "map": map_name, "process": identity.as_dict(),
            "pidfd_captured": True, "command_sha256": command_hash,
            "created_at": utc_now(),
        }, mode=0o600)
        deadline = time.monotonic() + timeout
        accepted = False
        while time.monotonic() < deadline and process.poll() is None:
            log_stream.flush()
            try:
                current = log_path.read_text(encoding="utf-8", errors="strict")
            except UnicodeDecodeError as exc:
                raise GateIntegrityError("fresh cold-load log is not valid UTF-8") from exc
            lines = current.splitlines()
            if last_anchored_failure(lines) is not None:
                break
            if any(
                (match := READY_RE.fullmatch(line)) is not None
                and match.group(1) == map_name
                for line in lines
            ):
                accepted = True
                time.sleep(0.25)
                break
            time.sleep(min(0.1, max(0.0, deadline - time.monotonic())))
        if process.poll() is None:
            assert process.stdin is not None
            process.stdin.write(b"quit\n")
            process.stdin.flush()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                shutdown_captured_child(process, identity, owner_path, pidfd)
        log_stream.flush()
        os.fsync(log_stream.fileno())
        try:
            final_log = log_path.read_text(encoding="utf-8", errors="strict")
        except UnicodeDecodeError as exc:
            raise GateIntegrityError("fresh cold-load log is not valid UTF-8") from exc
        if not accepted:
            raise CorpusError("fresh cold-load exited or timed out before runtime-ready")
        parse_cold_load_log(final_log, map_name, banner_counts)
        if process.returncode != 0:
            raise CorpusError(f"fresh cold-load engine exited with status {process.returncode}")
        if regular_file_record(source_artifact) != source_before:
            raise GateIntegrityError("artifact changed during fresh cold-load")
        if regular_file_record(artifact) != staged:
            raise GateIntegrityError("fresh cold-load changed its staged artifact")
        return {
            "owner": owner_path,
            "log": log_path,
            "command_sha256": command_hash,
        }
    finally:
        if process is not None and process.poll() is None:
            if identity is None or pidfd is None:
                shutdown_error = GateIntegrityError(
                    "fresh cold-load child identity was never captured"
                )
            else:
                try:
                    shutdown_captured_child(process, identity, owner_path, pidfd)
                except CorpusError as exc:
                    shutdown_error = exc
        if pidfd is not None:
            os.close(pidfd)
        log_stream.close()
        if shutdown_error is not None:
            raise shutdown_error


def run_one_map(
    run_root: Path,
    snapshot: Path,
    map_name: str,
    stable_port: int,
    fingerprint: str,
    *,
    startup_timeout: int,
    generation_timeout: int,
    engine_arguments: Sequence[str] = DEFAULT_ENGINE_ARGUMENTS,
    heartbeat: Callable[[str, str, Mapping[str, Any] | None], None] | None = None,
    gate_runner: Callable[..., subprocess.CompletedProcess[bytes]] = subprocess.run,
    _runtime_preflighted: bool = False,
) -> dict[str, Any]:
    """Launch one private engine attempt.  Tests replace this function."""
    validate_engine_arguments(engine_arguments)
    verified = verify_snapshot(snapshot)
    roles = verified["by_role"]
    python_interpreter = snapshot / verified["python_runtime"]["interpreter"]["path"]
    attempt_number, attempt = next_attempt_directory(run_root, map_name)
    started_at = utc_now()
    private, engine, artifact, config_name = stage_private_inputs(
        attempt, snapshot, map_name
    )
    command = [str(engine)] + [
        value.format(port=stable_port, map=map_name, config=config_name)
        for value in engine_arguments
    ]
    command_hash = sha256_bytes(b"\0".join(os.fsencode(value) for value in command) + b"\0")
    log_path = attempt / "server.log"
    log_stream = log_path.open("wb")
    process: subprocess.Popen[bytes] | None = None
    pidfd: int | None = None
    owner_path = attempt / "owner.json"
    classification = "INFRA_FAIL"
    detail = "attempt did not launch"
    parsed: dict[str, Any] | None = None
    gate: dict[str, Any] | None = None
    cold_load: dict[str, Any] | None = None
    failure_line: str | None = None
    shutdown_error: CorpusError | None = None
    try:
        atomic_write_json(
            owner_path,
            {
                "fingerprint": fingerprint,
                "attempt": attempt_number,
                "map": map_name,
                "process": None,
                "pidfd_captured": False,
                "command_sha256": command_hash,
                "created_at": started_at,
            },
            mode=0o600,
        )
        preflight_started = utc_now()
        try:
            preflight = preflight_python_runtime(snapshot, runner=gate_runner)
        except CorpusError as exc:
            atomic_write_json(attempt / "runtime-preflight.json", {
                "fingerprint": fingerprint, "started_at": preflight_started,
                "ended_at": utc_now(), "classification": "INFRA_FAIL",
                "error": str(exc), "lifecycle": None, "command": None,
                "parent_process": capture_process_identity(os.getpid()).as_dict(),
            })
            raise
        atomic_write_json(attempt / "runtime-preflight.json", {
            "fingerprint": fingerprint, "started_at": preflight_started,
            "ended_at": utc_now(), "classification": "PASS", "error": None,
            "command": preflight["command"], "output_sha256": preflight["output_sha256"],
            "lifecycle": preflight["lifecycle"],
            "parent_process": capture_process_identity(os.getpid()).as_dict(),
        })
        guarded_command = _private_python_command(
            snapshot, verified["python_runtime"], GUARD_BOOTSTRAP,
            str(os.getpid()), "--", *command,
        )
        process = subprocess.Popen(
            guarded_command,
            cwd=private,
            stdin=subprocess.PIPE,
            stdout=log_stream,
            stderr=subprocess.STDOUT,
            close_fds=True,
            env=dict(PYTHON_ENVIRONMENT),
        )
        pidfd = open_pidfd(process.pid)
        if pidfd is None:
            raise CorpusError("launched engine has no pidfd")
        identity = wait_for_exec_identity(process.pid, engine, _nul_argv(command))
        atomic_write_json(
            owner_path,
            {
                "fingerprint": fingerprint,
                "attempt": attempt_number,
                "map": map_name,
                "process": identity.as_dict(),
                "pidfd_captured": pidfd is not None,
                "command_sha256": command_hash,
                "created_at": utc_now(),
            },
            mode=0o600,
        )
        if pidfd is None:
            raise CorpusError("captured engine has no pidfd; attempt left unfinished")
        if heartbeat is not None:
            heartbeat(
                "active",
                map_name,
                {
                    "attempt": attempt_number,
                    "stable_port": stable_port,
                    "started_at": started_at,
                    "process": identity.as_dict(),
                    "heartbeat_at": utc_now(),
                },
            )
        deadline = time.monotonic() + startup_timeout
        while time.monotonic() < deadline and process.poll() is None:
            time.sleep(min(0.1, max(0.0, deadline - time.monotonic())))
        ready_seen = False
        failure_seen = False
        deadline_expired = False
        if process.poll() is None:
            assert process.stdin is not None
            process.stdin.write(b"sv rune\n")
            process.stdin.flush()
            deadline = time.monotonic() + generation_timeout
            next_heartbeat = time.monotonic()
            while time.monotonic() < deadline and process.poll() is None:
                if heartbeat is not None and time.monotonic() >= next_heartbeat:
                    heartbeat("beat", map_name, None)
                    next_heartbeat = time.monotonic() + 5.0
                log_stream.flush()
                try:
                    current_log = log_path.read_text(encoding="utf-8", errors="strict")
                except UnicodeDecodeError as exc:
                    raise CorpusError("server log is not valid UTF-8") from exc
                lines = current_log.splitlines()
                failure_line = last_anchored_failure(lines)
                failure_seen = failure_line is not None
                ready_seen = any(
                    (match := READY_RE.fullmatch(line)) is not None
                    and match.group(1) == map_name
                    for line in lines
                )
                if failure_seen or ready_seen:
                    if ready_seen:
                        # Allow buffered post-ready diagnostics to arrive before quit.
                        time.sleep(0.25)
                    break
                time.sleep(min(0.1, max(0.0, deadline - time.monotonic())))
            deadline_expired = not ready_seen and not failure_seen and process.poll() is None
        if process.poll() is None:
            assert process.stdin is not None
            process.stdin.write(b"quit\n")
            process.stdin.flush()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                shutdown_captured_child(process, identity, owner_path, pidfd)
        # The child has exited (or bounded shutdown completed).  This is the
        # authoritative last read: diagnostics written during exit must not be
        # lost to an earlier polling snapshot.
        log_stream.flush()
        os.fsync(log_stream.fileno())
        try:
            final_log = log_path.read_text(encoding="utf-8", errors="strict")
        except UnicodeDecodeError as exc:
            raise CorpusError("server log is not valid UTF-8") from exc
        final_failure = last_anchored_failure(final_log.splitlines())
        if final_failure is not None:
            failure_line = final_failure
            failure_seen = True
            deadline_expired = False
        if failure_seen:
            classification = "GEN_FAIL"
            if failure_line is None:
                raise CorpusError("anchored failure classification lost its record")
            detail = failure_line
        elif deadline_expired:
            classification = "TIMEOUT"
            detail = "generation timeout before runtime-ready acceptance"
        elif not ready_seen:
            classification = "GEN_FAIL"
            detail = "engine exited before runtime-ready acceptance"
        if classification not in ("TIMEOUT", "GEN_FAIL"):
            if process.returncode != 0:
                classification = "GEN_FAIL"
                detail = f"engine exited with status {process.returncode}"
            else:
                classification = "GEN_FAIL"
                detail = "generation output did not satisfy the acceptance grammar"
                if not artifact.exists() or artifact.is_symlink() or not artifact.is_file():
                    raise CorpusError("generator did not create one regular artifact")
                try:
                    parsed = parse_generation_log(
                        final_log,
                        map_name,
                        artifact,
                        private,
                    )
                except CorpusError as exc:
                    classification = "GEN_FAIL"
                    detail = str(exc)
                    parsed = None
                if parsed is None:
                    raise CorpusError(detail)
                try:
                    gate = run_gates(
                        artifact,
                        map_name,
                        parsed["counts"],
                        acceptor_gnu=snapshot / roles["acceptor_gnu"]["path"],
                        acceptor_make=snapshot / roles["acceptor_make"]["path"],
                        python_interpreter=python_interpreter,
                        runeio=snapshot / roles["runeio"]["path"],
                        runelint=snapshot / roles["runelint"]["path"],
                        objective_roots=parsed["objective_roots"],
                        semantic_checkers=semantic_checkers_for_map(
                            snapshot, verified, map_name
                        ),
                        log_directory=attempt,
                        runner=gate_runner,
                        fingerprint=fingerprint,
                    )
                    cold_load = run_fresh_cold_load(
                        attempt, snapshot, map_name, stable_port, fingerprint,
                        attempt_number, artifact, parsed["counts"], identity,
                        timeout=generation_timeout,
                        engine_arguments=engine_arguments,
                    )
                    classification = "PASS"
                    detail = (
                        "generation, dual readers, semantic gates, and fresh "
                        "cold-load passed"
                    )
                except GateIntegrityError as exc:
                    classification = "INFRA_FAIL"
                    detail = str(exc)
                    gate = None
                    cold_load = None
                except CorpusError as exc:
                    classification = "LINT_FAIL"
                    detail = str(exc)
                    gate = None
                    cold_load = None
    except (CorpusError, OSError, subprocess.SubprocessError) as exc:
        classification = "INFRA_FAIL" if classification == "INFRA_FAIL" else classification
        detail = str(exc)
    finally:
        if process is not None and process.poll() is None:
            try:
                identity
            except UnboundLocalError:
                shutdown_error = CorpusError("live child identity was never captured")
            else:
                try:
                    shutdown_captured_child(process, identity, owner_path, pidfd)
                except CorpusError as exc:
                    shutdown_error = exc
        if pidfd is not None:
            os.close(pidfd)
        log_stream.close()
        if heartbeat is not None:
            heartbeat("inactive", map_name, None)
    if process is not None and process.poll() is None:
        raise shutdown_error or CorpusError("child remains live; attempt left unfinished")
    if shutdown_error is not None:
        raise shutdown_error
    ended_at = utc_now()
    evidence = []
    for path in sorted(attempt.rglob("*")):
        if path.is_file() and not path.is_symlink():
            evidence.append(_relative_evidence_record(path, run_root))
    artifact_record = _relative_evidence_record(artifact, run_root) if artifact.exists() else None
    result = {
        "fingerprint": fingerprint,
        "map": map_name,
        "stable_port": stable_port,
        "attempt": attempt_number,
        "started_at": started_at,
        "ended_at": ended_at,
        "classification": classification,
        "normalized_signature": normalized_signature(classification, detail),
        "detail": detail,
        "failure_line": failure_line if classification == "GEN_FAIL" else None,
        "command_sha256": command_hash,
        "owner_record": str(owner_path.relative_to(run_root)),
        "evidence": evidence,
        "server_log_sha256": sha256_regular(log_path),
        "artifact": artifact_record,
        "artifact_sha256": artifact_record["sha256"] if artifact_record else None,
        "objective_roots": parsed["objective_roots"] if parsed else None,
        "banner_counts": parsed["counts"] if parsed else None,
        "decoded_counts": gate["decoded_counts"] if gate else None,
        "gate_output_sha256": gate["gate_output_sha256"] if gate else None,
        "gate_log_sha256": (
            {name: record["sha256"] for name, record in gate["gate_logs"].items()}
            if gate else None
        ),
        "semantic_gate_labels": gate["semantic_gate_labels"] if gate else None,
        "cold_load_owner_record": (
            str(cold_load["owner"].relative_to(run_root)) if cold_load else None
        ),
        "cold_load_command_sha256": (
            cold_load["command_sha256"] if cold_load else None
        ),
        "cold_load_log_sha256": (
            sha256_regular(cold_load["log"]) if cold_load else None
        ),
    }
    publish_result(run_root, map_name, result, attempt)
    return result


def prepare_run(
    run_root: Path,
    fingerprint_document: Mapping[str, Any],
    fingerprint: str,
) -> bytes:
    document_bytes = canonical_json(fingerprint_document)
    path = run_root / "fingerprint-document.json"
    if path.exists() or path.is_symlink():
        _old, old_bytes = _load_json_regular(path)
        if old_bytes != document_bytes:
            raise CorpusError("stored fingerprint document differs byte-for-byte")
    else:
        atomic_write_bytes(path, document_bytes)
    atomic_write_bytes(run_root / "fingerprint.txt", (fingerprint + "\n").encode("ascii"))
    return document_bytes


def execute_selection(
    *,
    snapshot: Path,
    run_root: Path,
    selected_maps: Sequence[str],
    port_base: int,
    startup_timeout: int,
    generation_timeout: int,
    jobs: int,
    engine_arguments: Sequence[str],
) -> dict[str, Any]:
    reject_symlink_components(run_root.parent)
    if run_root.exists() or run_root.is_symlink():
        reject_symlink_components(run_root)
    maps = validate_manifest()
    assignments = stable_assignments(maps, port_base)
    lookup = {item["map"]: item for item in assignments}
    if any(name not in lookup for name in selected_maps):
        raise CorpusError("selection contains a map outside the fixed corpus")
    document, fingerprint = build_fingerprint_document(
        snapshot,
        startup_timeout=startup_timeout,
        generation_timeout=generation_timeout,
        jobs=jobs,
        port_base=port_base,
        engine_arguments=engine_arguments,
    )
    with ControllerLock(run_root, fingerprint):
        preflight_started = utc_now()
        try:
            preflight = preflight_python_runtime(snapshot)
        except CorpusError as exc:
            atomic_write_json(run_root / "runtime-preflight.json", {
                "fingerprint": fingerprint, "started_at": preflight_started,
                "ended_at": utc_now(), "classification": "INFRA_FAIL", "error": str(exc),
                "command": None, "output_sha256": None, "lifecycle": None,
                "parent_process": capture_process_identity(os.getpid()).as_dict(),
            })
            raise
        atomic_write_json(run_root / "runtime-preflight.json", {
            "fingerprint": fingerprint, "started_at": preflight_started,
            "ended_at": utc_now(), "command": preflight["command"],
            "output_sha256": preflight["output_sha256"], "classification": "PASS", "error": None,
            "lifecycle": preflight["lifecycle"],
            "parent_process": capture_process_identity(os.getpid()).as_dict(),
        })
        document_bytes = prepare_run(run_root, document, fingerprint)
        metadata_path = run_root / "run-metadata.json"
        if metadata_path.exists():
            metadata, _metadata_bytes = _load_json_regular(metadata_path)
            if not isinstance(metadata, dict) or metadata.get("fingerprint") != fingerprint or not isinstance(
                metadata.get("started_at"), str
            ):
                raise CorpusError("stored run metadata is invalid")
            started_at = metadata["started_at"]
        else:
            started_at = utc_now()
            atomic_write_json(
                metadata_path,
                {"fingerprint": fingerprint, "started_at": started_at},
            )
        if verify_fingerprint_document(snapshot, document) != fingerprint:
            raise CorpusError("prepared fingerprint does not match frozen inputs")
        stale_terminals = recover_stale_attempts(
            run_root, selected_maps, fingerprint, lookup
        )
        if stale_terminals:
            regenerate_reports(
                run_root,
                maps,
                fingerprint,
                started_at,
                publish_heartbeat=True,
                snapshot=snapshot,
                fingerprint_document_bytes=document_bytes,
                port_base=port_base,
                recheck_pass_gates=False,
            )
        require_pidfd_support()
        preflight_ports(item["port"] for item in assignments if item["map"] in selected_maps)
        heartbeat = HeartbeatPublisher(run_root, fingerprint, CORPUS_SIZE)
        pending_assignments: list[dict[str, Any]] = []
        for map_name in selected_maps:
            port = int(lookup[map_name]["port"])
            result_path = run_root / "runs" / map_name / "result.json"
            if result_path.exists() and validate_resumable_pass(
                result_path,
                run_root=run_root,
                fingerprint=fingerprint,
                fingerprint_document_bytes=document_bytes,
                stable_port=port,
                snapshot=snapshot,
                runtime_preflighted=True,
            ):
                regenerate_reports(
                    run_root,
                    maps,
                    fingerprint,
                    started_at,
                    publish_heartbeat=False,
                    snapshot=snapshot,
                    fingerprint_document_bytes=document_bytes,
                    port_base=port_base,
                    recheck_pass_gates=False,
                )
                continue
            pending_assignments.append(dict(lookup[map_name]))

        def worker(assignment: Mapping[str, Any]) -> dict[str, Any]:
            return run_one_map(
                run_root,
                snapshot,
                str(assignment["map"]),
                int(assignment["port"]),
                fingerprint,
                startup_timeout=startup_timeout,
                generation_timeout=generation_timeout,
                engine_arguments=engine_arguments,
                heartbeat=heartbeat.event,
                _runtime_preflighted=False,
            )

        def terminal(_assignment: Mapping[str, Any], _result: Any) -> None:
            if verify_fingerprint_document(snapshot, document) != fingerprint:
                raise CorpusError("frozen inputs changed during the run")
            regenerate_reports(
                run_root,
                maps,
                fingerprint,
                started_at,
                publish_heartbeat=False,
                snapshot=snapshot,
                fingerprint_document_bytes=document_bytes,
                port_base=port_base,
                recheck_pass_gates=False,
            )
            heartbeat.event("terminal", str(_assignment["map"]), None)

        run_bounded(pending_assignments, jobs, worker, terminal)
        if verify_fingerprint_document(snapshot, document) != fingerprint:
            raise CorpusError("frozen inputs changed before final summary")
        summary = regenerate_reports(
            run_root,
            maps,
            fingerprint,
            started_at,
            ended_at=utc_now(),
            publish_heartbeat=False,
            snapshot=snapshot,
            fingerprint_document_bytes=document_bytes,
            port_base=port_base,
            runtime_preflighted=True,
        )
        heartbeat.finish(len(summary["maps"]), bool(summary["complete"]))
        return summary


def _parse_named_file(value: str) -> tuple[str, Path]:
    name, separator, raw_path = value.partition("=")
    if not separator or not name or not raw_path:
        raise argparse.ArgumentTypeError("expected NAME=PATH")
    _safe_logical_path(name)
    return name, Path(raw_path)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    snapshot = subparsers.add_parser("snapshot", help="create a frozen input snapshot")
    snapshot.add_argument("--output", type=Path, required=True)
    snapshot.add_argument("--file", action="append", type=_parse_named_file, required=True, metavar="ROLE@NAME=PATH")
    for name in ("dry-run", "run", "smoke"):
        command = subparsers.add_parser(name)
        command.add_argument("--snapshot", type=Path, required=True)
        command.add_argument("--run-root", type=Path, required=name != "dry-run")
        command.add_argument("--port-base", type=int, default=DEFAULT_PORT_BASE)
        command.add_argument("--startup-timeout", type=int, default=10)
        command.add_argument("--generation-timeout", type=int, default=900)
        command.add_argument("--jobs", type=int, default=1)
        command.add_argument("--engine-argument", action="append", dest="engine_arguments")
        if name == "smoke":
            command.add_argument("map")
    return parser


def _snapshot_cli(entries: Sequence[tuple[str, Path]], output: Path) -> None:
    inputs: dict[str, tuple[str, Path]] = {}
    for specification, source in entries:
        role, separator, logical = specification.partition("@")
        if not separator:
            raise CorpusError("snapshot --file names must be ROLE@LOGICAL=PATH")
        if logical in inputs:
            raise CorpusError(f"duplicate snapshot logical path: {logical}")
        inputs[logical] = (role, source)
    create_input_snapshot(output, inputs)


def main(argv: Sequence[str] | None = None) -> int:
    raw_arguments = list(sys.argv[1:] if argv is None else argv)
    if raw_arguments and raw_arguments[0] == "_guard-exec":
        return _guard_exec(raw_arguments[1:])
    args = build_parser().parse_args(raw_arguments)
    try:
        if args.command == "snapshot":
            _snapshot_cli(args.file, args.output)
            print(f"snapshot={args.output}")
            return 0
        maps = validate_manifest()
        engine_arguments = tuple(args.engine_arguments or DEFAULT_ENGINE_ARGUMENTS)
        document, fingerprint = build_fingerprint_document(
            args.snapshot,
            startup_timeout=args.startup_timeout,
            generation_timeout=args.generation_timeout,
            jobs=args.jobs,
            port_base=args.port_base,
            engine_arguments=engine_arguments,
        )
        if args.command == "dry-run":
            print(f"fingerprint={fingerprint}")
            print(canonical_json(document).decode("ascii").rstrip())
            for item in stable_assignments(maps, args.port_base):
                print(f"{item['index']:03d}\t{item['map']}\t{item['port']}")
            return 0
        selected = maps if args.command == "run" else [args.map]
        summary = execute_selection(
            snapshot=args.snapshot,
            run_root=args.run_root,
            selected_maps=selected,
            port_base=args.port_base,
            startup_timeout=args.startup_timeout,
            generation_timeout=args.generation_timeout,
            jobs=args.jobs,
            engine_arguments=engine_arguments,
        )
        selected_results = {
            item["map"]: item["classification"]
            for item in summary["maps"]
            if item["map"] in selected
        }
        return 0 if len(selected_results) == len(selected) and all(
            selected_results.get(name) == "PASS" for name in selected
        ) else 1
    except CorpusError as exc:
        print(f"rune-corpus: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
