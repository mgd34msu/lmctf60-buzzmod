#!/usr/bin/env python3
"""Build, install, verify, and roll back an LMCTF server bundle."""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import importlib.abc
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import sys
import tarfile
from typing import Any, Mapping, Sequence
import uuid


BUILD_FORMAT = "lmctf-server-bundle-build-v2"
RELEASE_FORMAT = "lmctf-server-bundle-release-v2"
INSTALLED_FORMAT = "lmctf-installed-server-bundle-v2"
STATE_FORMAT = "lmctf-server-bundle-state-v2"
LANES = tuple(f"s{number:02d}" for number in range(1, 11))
MAX_JSON_BYTES = 16 * 1024 * 1024
MAX_FILE_BYTES = 2 * 1024 * 1024 * 1024
SHA_RE = re.compile(r"[0-9a-f]{64}")
FIXED_ROLES = {
    "module-primary": "game/game.so",
    "module-secondary": "game/gamex86_64.so",
    "pak": "game/lmctf6-buzzmod.pak",
    "config": "game/server.cfg",
    "route-only-config": "game/route-only-match.cfg",
    "route-only-maplist": "game/route-only-maplist.txt",
    "topmaps": "game/topmaps.txt",
    **{f"maplist:{lane}": f"game/maplists/{lane}.txt" for lane in LANES},
}


class BundleError(RuntimeError):
    pass


def _canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode("ascii")


def _hash(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _no_duplicate_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise BundleError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _identity(info: os.stat_result) -> tuple[int, ...]:
    return (
        info.st_dev, info.st_ino, info.st_size, stat.S_IMODE(info.st_mode),
        info.st_uid, info.st_gid, info.st_nlink, info.st_mtime_ns,
        info.st_ctime_ns,
    )


def _read_regular(path: Path, maximum: int = MAX_FILE_BYTES) -> tuple[bytes, os.stat_result]:
    _reject_symlink_components(path)
    try:
        before = path.lstat()
    except OSError as exc:
        raise BundleError(f"cannot stat required file {path}: {exc}") from exc
    if (not stat.S_ISREG(before.st_mode) or stat.S_ISLNK(before.st_mode) or
            before.st_nlink != 1 or before.st_size < 0 or before.st_size > maximum):
        raise BundleError(f"required path is not one bounded regular file: {path}")
    flags = os.O_RDONLY | os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(path, flags)
    except OSError as exc:
        raise BundleError(f"cannot open required file {path}: {exc}") from exc
    try:
        opened = os.fstat(fd)
        chunks = []
        remaining = maximum + 1
        while remaining:
            chunk = os.read(fd, min(1024 * 1024, remaining))
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        after = os.fstat(fd)
    finally:
        os.close(fd)
    payload = b"".join(chunks)
    if (len(payload) > maximum or _identity(before) != _identity(opened) or
            _identity(opened) != _identity(after)):
        raise BundleError(f"required file changed while reading: {path}")
    return payload, after


def _read_json(path: Path) -> tuple[dict, bytes]:
    payload, _info = _read_regular(path, MAX_JSON_BYTES)
    try:
        value = json.loads(
            payload.decode("ascii"), object_pairs_hook=_no_duplicate_object,
            parse_constant=lambda token: (_ for _ in ()).throw(
                BundleError(f"non-finite JSON number {token}")),
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise BundleError(f"invalid canonical JSON file {path}") from exc
    if not isinstance(value, dict) or payload != _canonical(value):
        raise BundleError(f"JSON file is not one canonical object: {path}")
    return value, payload


def _safe_relative(value: Any) -> str:
    if not isinstance(value, str):
        raise BundleError("bundle path is not a string")
    path = PurePosixPath(value)
    if (path.is_absolute() or not path.parts or
            any(part in {"", ".", ".."} for part in path.parts)):
        raise BundleError(f"unsafe bundle path: {value!r}")
    return path.as_posix()


def _reject_symlink_components(path: Path, *, allow_missing_final: bool = False) -> None:
    absolute = path.absolute()
    current = Path(absolute.anchor)
    for index, part in enumerate(absolute.parts[1:]):
        current /= part
        try:
            info = current.lstat()
        except FileNotFoundError:
            if allow_missing_final and index == len(absolute.parts[1:]) - 1:
                return
            raise BundleError(f"missing path component: {current}")
        if stat.S_ISLNK(info.st_mode):
            raise BundleError(f"symlink path component: {current}")


def _file_record(path: Path, *, resolved_path: Path | None = None) -> dict:
    payload, info = _read_regular(path)
    return {
        "path": str((resolved_path or path).absolute()),
        "size": len(payload), "sha256": _hash(payload),
        "device": info.st_dev, "inode": info.st_ino,
    }


class _AttestedSourceLoader(importlib.abc.Loader):
    """Execute the bytes authenticated before the module spec was created."""

    def __init__(self, path: Path, payload: bytes):
        self.path = path
        self.payload = payload

    def create_module(self, spec):
        return None

    def exec_module(self, module) -> None:
        code = compile(self.payload, str(self.path), "exec", dont_inherit=True)
        exec(code, module.__dict__)


def _verify_file_record(record: Any, label: str) -> Path:
    if not isinstance(record, dict) or set(record) != {
            "path", "size", "sha256", "device", "inode"}:
        raise BundleError(f"invalid {label} file identity")
    if (not isinstance(record["path"], str) or type(record["size"]) is not int or
            record["size"] < 0 or type(record["device"]) is not int or
            type(record["inode"]) is not int or SHA_RE.fullmatch(str(record["sha256"])) is None):
        raise BundleError(f"invalid {label} file identity fields")
    path = Path(record["path"])
    _reject_symlink_components(path)
    if _file_record(path) != record:
        raise BundleError(f"{label} file identity drift")
    return path


def _atomic_write(path: Path, payload: bytes, mode: int) -> None:
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}-{uuid.uuid4().hex}")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC
    fd = os.open(temporary, flags, mode)
    try:
        offset = 0
        while offset < len(payload):
            offset += os.write(fd, payload[offset:])
        os.fchmod(fd, mode)
        os.fsync(fd)
    finally:
        os.close(fd)
    os.replace(temporary, path)
    _fsync_directory(path.parent)


def _commit_state(root: Path, value: dict) -> None:
    path = root / "install-state.json"
    temporary = root / ".install-state.json.tmp"
    if temporary.exists() or temporary.is_symlink():
        _read_regular(temporary, MAX_JSON_BYTES)
        temporary.unlink()
    payload = _canonical(value)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC
    fd = os.open(temporary, flags, 0o444)
    try:
        offset = 0
        while offset < len(payload):
            offset += os.write(fd, payload[offset:])
        os.fchmod(fd, 0o444)
        os.fsync(fd)
    finally:
        os.close(fd)
    _fault("before-state-replace")
    os.replace(temporary, path)
    _fsync_directory(root)


def _fsync_directory(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def _maps() -> tuple[str, ...]:
    path = Path(__file__).with_name("rune-corpus-maps.txt")
    payload, _info = _read_regular(path, 1024 * 1024)
    maps = tuple(
        line for line in payload.decode("ascii").splitlines()
        if line and not line.startswith("#")
    )
    if len(maps) != 175 or len(set(maps)) != 175:
        raise BundleError("canonical map inventory is not exactly 175 unique maps")
    return maps


def _topmaps() -> tuple[str, ...]:
    path = Path(__file__).with_name("topmaps.txt")
    payload, _info = _read_regular(path, 1024 * 1024)
    maps = tuple(
        line for line in payload.decode("ascii").splitlines()
        if line and not line.startswith("#")
    )
    if len(maps) != 20 or len(set(maps)) != 20 or not set(maps).issubset(_maps()):
        raise BundleError("canonical topmap inventory is invalid")
    return maps


def _load_attested_module(path: Path, record: Mapping[str, Any], label: str):
    payload, info = _read_regular(path)
    current = {
        "path": str(path.absolute()),
        "size": len(payload), "sha256": _hash(payload),
        "device": info.st_dev, "inode": info.st_ino,
    }
    if record != current:
        raise BundleError(f"{label} bytes differ from final corpus binding")
    module_name = f"_lmctf_bundle_{label}_{record['sha256']}"
    module_spec = importlib.util.spec_from_file_location(
        module_name, path, loader=_AttestedSourceLoader(path, payload)
    )
    if module_spec is None or module_spec.loader is None:
        raise BundleError(f"cannot load {label} verifier")
    module = importlib.util.module_from_spec(module_spec)
    sys.modules[module_name] = module
    try:
        module_spec.loader.exec_module(module)
    except Exception:
        if sys.modules.get(module_name) is module:
            del sys.modules[module_name]
        raise
    return module


def _final_corpus_modules() -> tuple[dict, dict, Any, Any]:
    controller_path = Path(__file__).with_name("rune_corpus_controller.py")
    finalizer_path = Path(__file__).with_name("rune_corpus_finalizer.py")
    controller_record = _file_record(controller_path)
    finalizer_record = _file_record(finalizer_path)
    controller = _load_attested_module(controller_path, controller_record, "controller")
    finalizer = _load_attested_module(finalizer_path, finalizer_record, "finalizer")
    if not callable(getattr(controller, "verify_snapshot", None)):
        raise BundleError("final corpus controller lacks snapshot verification")
    for name in (
        "build_verified_final_corpus_binding",
        "validate_bundle_final_corpus_binding",
    ):
        if not callable(getattr(finalizer, name, None)):
            raise BundleError(f"final corpus finalizer lacks {name}")
    return controller_record, finalizer_record, controller, finalizer


def _build_final_corpus_binding(snapshot: Path, corpus_root: Path) -> dict:
    controller_record, finalizer_record, controller, finalizer = _final_corpus_modules()
    try:
        return finalizer.build_verified_final_corpus_binding(
            controller,
            snapshot=Path(snapshot),
            corpus_root=Path(corpus_root),
            controller_record=controller_record,
            finalizer_record=finalizer_record,
        )
    except Exception as exc:
        raise BundleError(f"final corpus binding is invalid: {exc}") from exc


def _validate_final_corpus_binding(
    binding: Any,
    files: Sequence[Mapping[str, Any]],
    *,
    engine_record: Mapping[str, Any] | None = None,
) -> dict[str, dict]:
    controller_record, finalizer_record, controller, finalizer = _final_corpus_modules()
    roles = {str(record.get("role")): record for record in files}
    try:
        return finalizer.validate_bundle_final_corpus_binding(
            controller,
            binding=binding,
            controller_record=controller_record,
            finalizer_record=finalizer_record,
            bundle_roles=roles,
            engine_record=engine_record,
        )
    except Exception as exc:
        raise BundleError(f"final corpus binding is invalid: {exc}") from exc


def _expected_roles() -> set[str]:
    maps = _maps()
    required = set(FIXED_ROLES)
    required.update(f"bsp:{name}" for name in maps)
    required.update(f"rune:{name}" for name in maps)
    return required


def _role_path(role: str) -> str:
    if role in FIXED_ROLES:
        return FIXED_ROLES[role]
    kind, separator, map_name = role.partition(":")
    if separator and kind in {"bsp", "rune"} and map_name in _maps():
        return f"game/maps/{map_name}.{kind}"
    raise BundleError(f"unknown bundle role: {role}")


def _role_payload(role: str) -> bytes | None:
    if role in {"route-only-config", "route-only-maplist"}:
        path = Path(__file__).with_name(
            "route-only-match.cfg" if role == "route-only-config"
            else "route-only-maplist.txt"
        )
        payload, _info = _read_regular(path, 1024 * 1024)
        return payload
    if role == "topmaps":
        payload, _info = _read_regular(
            Path(__file__).with_name("topmaps.txt"), 1024 * 1024
        )
        return payload
    if role.startswith("maplist:"):
        lane = role.removeprefix("maplist:")
        if lane not in LANES:
            raise BundleError("unknown maplist lane")
        offset = LANES.index(lane)
        names = _topmaps()[offset:] + _topmaps()[:offset]
        return ("\n".join(names) + "\n").encode("ascii")
    return None


def _validate_content(authority: Any) -> dict:
    if not isinstance(authority, dict) or set(authority) != {
            "final_corpus", "maps", "topmaps", "files"}:
        raise BundleError("bundle authority has unknown or missing fields")
    if authority["maps"] != list(_maps()) or authority["topmaps"] != list(_topmaps()):
        raise BundleError("bundle map authorities differ from the canonical lists")
    files = authority["files"]
    if not isinstance(files, list):
        raise BundleError("bundle file inventory is not a list")
    required = _expected_roles()
    seen_roles, seen_paths = set(), set()
    records = []
    for record in files:
        if not isinstance(record, dict) or set(record) != {
                "path", "role", "mode", "size", "sha256"}:
            raise BundleError("invalid bundle file record")
        path = _safe_relative(record["path"])
        role = record["role"]
        if (not isinstance(role, str) or role in seen_roles or path in seen_paths or
                role not in required or path != _role_path(role) or
                record["mode"] not in {0o444, 0o555} or
                type(record["size"]) is not int or record["size"] < 0 or
                SHA_RE.fullmatch(str(record["sha256"])) is None):
            raise BundleError("bundle file inventory contains invalid or duplicate authority")
        seen_roles.add(role)
        seen_paths.add(path)
        records.append(dict(record))
    if seen_roles != required:
        raise BundleError("bundle file role inventory is incomplete")
    if records != sorted(records, key=lambda item: item["path"]):
        raise BundleError("bundle file inventory is not path-sorted")
    by_role = {record["role"]: record for record in records}
    if by_role["module-primary"]["sha256"] != by_role["module-secondary"]["sha256"]:
        raise BundleError("production module aliases do not have identical bytes")
    if not isinstance(authority["final_corpus"], dict):
        raise BundleError("bundle final corpus binding is invalid")
    return {"final_corpus": authority["final_corpus"], "maps": list(_maps()),
            "topmaps": list(_topmaps()), "files": records}


def _read_build_spec(
    path: Path, *, snapshot: Path, corpus_root: Path,
) -> tuple[dict, list[tuple[dict, Path]]]:
    value, _payload = _read_json(path)
    if set(value) != {"format", "files"} or value["format"] != BUILD_FORMAT:
        raise BundleError("invalid bundle build specification")
    entries = value["files"]
    if not isinstance(entries, list):
        raise BundleError("bundle build file inventory is not a list")
    records = []
    sources = []
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != {"source", "path", "role"}:
            raise BundleError("invalid bundle build file entry")
        source = Path(entry["source"])
        _reject_symlink_components(source)
        payload, _info = _read_regular(source)
        role = entry["role"]
        required_payload = _role_payload(role) if isinstance(role, str) else None
        if required_payload is not None and payload != required_payload:
            noun = "rotation" if role.startswith("maplist:") else "content"
            raise BundleError(f"{role} {noun} is not canonical")
        path_value = _safe_relative(entry["path"])
        mode = 0o555 if role in {"module-primary", "module-secondary"} else 0o444
        record = {"path": path_value, "role": role, "mode": mode,
                  "size": len(payload), "sha256": _hash(payload)}
        records.append(record)
        sources.append((record, source))
    records.sort(key=lambda item: item["path"])
    source_by_path = {record["path"]: (record, source) for record, source in sources}
    if len(source_by_path) != len(sources):
        raise BundleError("duplicate bundle build path")
    authority = _validate_content({
        "final_corpus": {}, "maps": list(_maps()), "topmaps": list(_topmaps()),
        "files": records,
    })
    authority["final_corpus"] = _build_final_corpus_binding(snapshot, corpus_root)
    _validate_final_corpus_binding(authority["final_corpus"], authority["files"])
    return authority, [source_by_path[record["path"]] for record in records]


def _archive_record(path: Path) -> dict:
    payload, _info = _read_regular(path, MAX_FILE_BYTES)
    return {"name": path.name, "size": len(payload), "sha256": _hash(payload)}


def _verify_archive(authority: Mapping[str, Any], archive: Path) -> None:
    expected = {record["path"]: record for record in authority["files"]}
    seen = set()
    try:
        with tarfile.open(archive, mode="r:") as handle:
            for member in handle:
                path = _safe_relative(member.name)
                if (path in seen or path not in expected or not member.isfile() or
                        member.pax_headers or member.linkname):
                    raise BundleError("archive contains an unexpected or unsafe member")
                record = expected[path]
                if member.size != record["size"] or member.mode != record["mode"]:
                    raise BundleError(f"archive metadata drift for {path}")
                source = handle.extractfile(member)
                if source is None:
                    raise BundleError(f"archive member cannot be read: {path}")
                digest = hashlib.sha256()
                size = 0
                expected_payload = _role_payload(record["role"])
                semantic = bytearray()
                while True:
                    chunk = source.read(1024 * 1024)
                    if not chunk:
                        break
                    size += len(chunk)
                    digest.update(chunk)
                    if expected_payload is not None:
                        semantic.extend(chunk)
                if size != record["size"] or digest.hexdigest() != record["sha256"]:
                    raise BundleError(f"archive content drift for {path}")
                if expected_payload is not None and bytes(semantic) != expected_payload:
                    raise BundleError(f"archive {record['role']} content is not canonical")
                seen.add(path)
    except (tarfile.TarError, OSError) as exc:
        raise BundleError(f"cannot verify bundle archive: {exc}") from exc
    if seen != set(expected):
        raise BundleError("archive file inventory is incomplete")


def build_bundle(
    spec_path: Path,
    archive: Path,
    manifest: Path,
    *,
    snapshot: Path,
    corpus_root: Path,
) -> dict:
    spec_path, archive, manifest = Path(spec_path), Path(archive), Path(manifest)
    if archive.exists() or archive.is_symlink() or manifest.exists() or manifest.is_symlink():
        raise BundleError("bundle outputs must not already exist")
    for parent in (archive.parent, manifest.parent):
        _reject_symlink_components(parent)
        if not parent.is_dir():
            raise BundleError("bundle output parent is not a directory")
    authority, sources = _read_build_spec(
        spec_path, snapshot=Path(snapshot), corpus_root=Path(corpus_root),
    )
    temporary = archive.with_name(f".{archive.name}.tmp-{os.getpid()}-{uuid.uuid4().hex}")
    try:
        with tarfile.open(temporary, mode="w", format=tarfile.USTAR_FORMAT) as handle:
            for record, source in sources:
                payload, _info = _read_regular(source)
                if len(payload) != record["size"] or _hash(payload) != record["sha256"]:
                    raise BundleError(f"bundle input changed during assembly: {source}")
                info = tarfile.TarInfo(record["path"])
                info.size = len(payload)
                info.mode = record["mode"]
                info.mtime = info.uid = info.gid = 0
                info.uname = info.gname = ""
                handle.addfile(info, io.BytesIO(payload))
        _verify_archive(authority, temporary)
        archive_identity = _archive_record(temporary)
        archive_identity["name"] = archive.name
        bundle_id = _hash(_canonical(authority))
        release = {"format": RELEASE_FORMAT, "bundle_id": bundle_id,
                   "authority": authority, "archive": archive_identity}
        os.replace(temporary, archive)
        _fsync_directory(archive.parent)
        _atomic_write(manifest, _canonical(release), 0o444)
        return verify_release(manifest, archive)
    except BaseException:
        temporary.unlink(missing_ok=True)
        archive.unlink(missing_ok=True)
        manifest.unlink(missing_ok=True)
        raise


def verify_release(manifest: Path, archive: Path) -> dict:
    manifest, archive = Path(manifest), Path(archive)
    value, _payload = _read_json(manifest)
    if set(value) != {"format", "bundle_id", "authority", "archive"}:
        raise BundleError("release manifest has unknown or missing fields")
    if value["format"] != RELEASE_FORMAT:
        raise BundleError("invalid release manifest format")
    authority = _validate_content(value["authority"])
    _validate_final_corpus_binding(authority["final_corpus"], authority["files"])
    if value["bundle_id"] != _hash(_canonical(authority)):
        raise BundleError("release bundle identity drift")
    archive_record = value["archive"]
    if (not isinstance(archive_record, dict) or set(archive_record) != {
            "name", "size", "sha256"} or archive_record["name"] != archive.name or
            _archive_record(archive) != archive_record):
        raise BundleError("release archive identity drift")
    _verify_archive(authority, archive)
    return value


def _make_writable_then_remove(path: Path) -> None:
    if not path.exists() and not path.is_symlink():
        return
    if path.is_symlink() or not path.is_dir():
        raise BundleError(f"unsafe temporary generation: {path}")
    for directory, names, files in os.walk(path, topdown=False, followlinks=False):
        current = Path(directory)
        for name in names + files:
            child = current / name
            info = child.lstat()
            if stat.S_ISLNK(info.st_mode):
                raise BundleError(f"temporary generation contains a symlink: {child}")
            child.chmod(0o700)
        current.chmod(0o700)
    shutil.rmtree(path)


def _freeze_tree(root: Path) -> None:
    for directory, names, files in os.walk(root, topdown=False, followlinks=False):
        current = Path(directory)
        for name in names:
            child = current / name
            if child.is_symlink() or not child.is_dir():
                raise BundleError(f"generation contains an unsafe directory: {child}")
            child.chmod(0o555)
        for name in files:
            child = current / name
            info = child.lstat()
            if not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode):
                raise BundleError(f"generation contains an unsafe file: {child}")
            child.chmod(0o555 if stat.S_IMODE(info.st_mode) & 0o111 else 0o444)
        current.chmod(0o555)


def _physical_record(path: Path, relative: str, role: str,
                     *, resolved_path: Path | None = None) -> dict:
    record = _file_record(path, resolved_path=resolved_path)
    return {**record, "relative_path": relative, "role": role}


def _extract_generation(release: dict, archive: Path, root: Path) -> Path:
    bundle_id = release["bundle_id"]
    generations = root / "generations"
    generations.mkdir(mode=0o700, exist_ok=True)
    generations_info = generations.lstat()
    if stat.S_ISLNK(generations_info.st_mode) or not stat.S_ISDIR(generations_info.st_mode):
        raise BundleError("install generations path is not a physical directory")
    final = generations / bundle_id
    if final.exists() or final.is_symlink():
        _verify_generation(final, expected_bundle=bundle_id)
        return final
    temporary = generations / f".installing-{bundle_id}"
    _make_writable_then_remove(temporary)
    temporary.mkdir(mode=0o700)
    authority = release["authority"]
    expected = {record["path"]: record for record in authority["files"]}
    try:
        with tarfile.open(archive, mode="r:") as handle:
            for member in handle:
                relative = _safe_relative(member.name)
                record = expected.get(relative)
                if record is None or not member.isfile():
                    raise BundleError("archive extraction found an unsafe member")
                destination = temporary.joinpath(*PurePosixPath(relative).parts)
                destination.parent.mkdir(parents=True, exist_ok=True)
                source = handle.extractfile(member)
                if source is None:
                    raise BundleError(f"archive member cannot be extracted: {relative}")
                flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC
                fd = os.open(destination, flags, record["mode"])
                digest = hashlib.sha256()
                size = 0
                try:
                    while True:
                        chunk = source.read(1024 * 1024)
                        if not chunk:
                            break
                        digest.update(chunk)
                        size += len(chunk)
                        offset = 0
                        while offset < len(chunk):
                            offset += os.write(fd, chunk[offset:])
                    os.fchmod(fd, record["mode"])
                    os.fsync(fd)
                finally:
                    os.close(fd)
                if size != record["size"] or digest.hexdigest() != record["sha256"]:
                    raise BundleError(f"extracted bundle file drift: {relative}")
        manifest_path = temporary / "release-manifest.json"
        _atomic_write(manifest_path, _canonical(release), 0o444)
        files = []
        for record in authority["files"]:
            temporary_path = temporary.joinpath(*PurePosixPath(record["path"]).parts)
            final_path = final.joinpath(*PurePosixPath(record["path"]).parts)
            files.append(_physical_record(
                temporary_path, record["path"], record["role"], resolved_path=final_path
            ))
        manifest_record = _file_record(manifest_path, resolved_path=final / manifest_path.name)
        installed = {
            "format": INSTALLED_FORMAT, "bundle_id": bundle_id,
            "generation_root": str(final.absolute()),
            "release_manifest": manifest_record,
            "final_corpus": authority["final_corpus"], "files": files,
        }
        _atomic_write(temporary / "installed-bundle.json", _canonical(installed), 0o444)
        _freeze_tree(temporary)
        os.replace(temporary, final)
        _fsync_directory(generations)
        _verify_generation(final, expected_bundle=bundle_id)
        return final
    except BaseException:
        _make_writable_then_remove(temporary)
        raise


def _expected_directories(paths: set[str]) -> set[str]:
    result = set()
    for value in paths:
        parent = PurePosixPath(value).parent
        while parent != PurePosixPath("."):
            result.add(parent.as_posix())
            parent = parent.parent
    return result


def _verify_generation(generation: Path, *, expected_bundle: str | None = None) -> dict:
    _reject_symlink_components(generation)
    if not generation.is_dir() or stat.S_IMODE(generation.lstat().st_mode) & 0o222:
        raise BundleError("installed generation root is missing or mutable")
    identity_path = generation / "installed-bundle.json"
    installed, _payload = _read_json(identity_path)
    if set(installed) != {
            "format", "bundle_id", "generation_root", "release_manifest",
            "final_corpus", "files"} or installed["format"] != INSTALLED_FORMAT:
        raise BundleError("installed generation identity is invalid")
    bundle_id = installed["bundle_id"]
    if (SHA_RE.fullmatch(str(bundle_id)) is None or
            expected_bundle is not None and bundle_id != expected_bundle or
            Path(installed["generation_root"]) != generation.absolute()):
        raise BundleError("installed generation bundle identity drift")
    manifest_path = _verify_file_record(installed["release_manifest"], "release manifest")
    if manifest_path != generation / "release-manifest.json":
        raise BundleError("installed release manifest path drift")
    release, _release_payload = _read_json(manifest_path)
    if (set(release) != {"format", "bundle_id", "authority", "archive"} or
            release["format"] != RELEASE_FORMAT or release["bundle_id"] != bundle_id):
        raise BundleError("installed release authority drift")
    authority = _validate_content(release["authority"])
    if (bundle_id != _hash(_canonical(authority)) or
            installed["final_corpus"] != authority["final_corpus"]):
        raise BundleError("installed bundle content identity drift")
    _validate_final_corpus_binding(authority["final_corpus"], authority["files"])
    records = installed["files"]
    if not isinstance(records, list) or len(records) != len(authority["files"]):
        raise BundleError("installed generation file inventory is incomplete")
    actual_paths = set()
    by_relative = {}
    for expected, physical in zip(authority["files"], records, strict=True):
        if not isinstance(physical, dict) or set(physical) != {
                "path", "relative_path", "role", "size", "sha256", "device", "inode"}:
            raise BundleError("installed file identity has invalid fields")
        relative = expected["path"]
        if (physical["relative_path"] != relative or physical["role"] != expected["role"] or
                physical["size"] != expected["size"] or
                physical["sha256"] != expected["sha256"] or
                _verify_file_record({key: physical[key] for key in
                                     ("path", "size", "sha256", "device", "inode")},
                                    f"installed {relative}") !=
                generation.joinpath(*PurePosixPath(relative).parts)):
            raise BundleError(f"installed generation file drift: {relative}")
        by_relative[relative] = physical
        actual_paths.add(relative)
    expected_files = actual_paths | {"release-manifest.json", "installed-bundle.json"}
    expected_directories = _expected_directories(expected_files)
    found_files, found_directories = set(), set()
    for directory, names, files in os.walk(generation, followlinks=False):
        current = Path(directory)
        if stat.S_IMODE(current.lstat().st_mode) & 0o222:
            raise BundleError("installed generation contains a mutable directory")
        for name in names:
            child = current / name
            info = child.lstat()
            if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
                raise BundleError("installed generation contains an unsafe directory")
            found_directories.add(child.relative_to(generation).as_posix())
        for name in files:
            child = current / name
            info = child.lstat()
            if (stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode) or
                    stat.S_IMODE(info.st_mode) & 0o222):
                raise BundleError("installed generation contains an unsafe file")
            found_files.add(child.relative_to(generation).as_posix())
    if found_files != expected_files or found_directories != expected_directories:
        raise BundleError("installed generation tree differs from its manifest")
    return installed


def _state_reference(installed: dict) -> dict:
    identity_path = Path(installed["generation_root"]) / "installed-bundle.json"
    final_corpus = installed["final_corpus"]
    return {"bundle_id": installed["bundle_id"],
            "corpus_id": final_corpus["corpus_id"],
            "snapshot": final_corpus["snapshot"],
            "generation_root": installed["generation_root"],
            "identity": _file_record(identity_path)}


def _verify_state_reference(value: Any, label: str, generations: Path) -> dict:
    if not isinstance(value, dict) or set(value) != {
            "bundle_id", "corpus_id", "snapshot", "generation_root", "identity"}:
        raise BundleError(f"invalid {label} bundle reference")
    generation = Path(value["generation_root"])
    if generation.parent != generations.absolute():
        raise BundleError(f"{label} generation escapes the install root")
    installed = _verify_generation(generation, expected_bundle=value["bundle_id"])
    identity_path = _verify_file_record(value["identity"], f"{label} bundle identity")
    if identity_path != generation / "installed-bundle.json":
        raise BundleError(f"{label} bundle identity path drift")
    final_corpus = installed.get("final_corpus")
    if (
        not isinstance(final_corpus, dict)
        or value["corpus_id"] != final_corpus.get("corpus_id")
        or value["snapshot"] != final_corpus.get("snapshot")
    ):
        raise BundleError(f"{label} final corpus identity drift")
    return installed


def verify_state_file(path: Path) -> dict:
    path = Path(path)
    value, _payload = _read_json(path)
    if set(value) != {"format", "revision", "active", "rollback"}:
        raise BundleError("install state has unknown or missing fields")
    if (value["format"] != STATE_FORMAT or type(value["revision"]) is not int or
            value["revision"] < 1):
        raise BundleError("install state identity is invalid")
    generations = path.parent / "generations"
    active = _verify_state_reference(value["active"], "active", generations)
    rollback = None
    if value["rollback"] is not None:
        rollback = _verify_state_reference(value["rollback"], "rollback", generations)
        if rollback["bundle_id"] == active["bundle_id"]:
            raise BundleError("active and rollback bundle identities are equal")
    return {"format": STATE_FORMAT, "revision": value["revision"],
            "active": active, "rollback": rollback,
            "state_file": _file_record(path)}


def _state_on_disk(verified: dict) -> dict:
    return {"format": STATE_FORMAT, "revision": verified["revision"],
            "active": _state_reference(verified["active"]),
            "rollback": (_state_reference(verified["rollback"])
                         if verified["rollback"] is not None else None)}


def _prepare_install_root(root: Path) -> Path:
    root = root.absolute()
    if root.exists() or root.is_symlink():
        _reject_symlink_components(root)
        if not root.is_dir():
            raise BundleError("install root is not a directory")
    else:
        _reject_symlink_components(root, allow_missing_final=True)
        root.mkdir(mode=0o700)
        _fsync_directory(root.parent)
    return root


def _lock_install(root: Path):
    path = root / "install.lock"
    flags = os.O_RDWR | os.O_CREAT | os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    fd = os.open(path, flags, 0o600)
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        os.close(fd)
        raise BundleError("install lock is not one unaliased regular file")
    fcntl.flock(fd, fcntl.LOCK_EX)
    return fd


def _active_state(root: Path) -> dict | None:
    path = root / "install-state.json"
    if not path.exists() and not path.is_symlink():
        return None
    return verify_state_file(path)


def _require_expected(state: dict | None, expected: str | None) -> None:
    current = state["active"]["bundle_id"] if state is not None else None
    if current != expected:
        raise BundleError(f"expected active bundle {expected!r}, found {current!r}")


def _fault(label: str) -> None:
    if os.environ.get("LMCTF_BUNDLE_FAULT") == label:
        raise BundleError(f"injected bundle failure at {label}")


def install_bundle(manifest: Path, archive: Path, root: Path,
                   *, expected_active: str | None) -> dict:
    release = verify_release(manifest, archive)
    root = _prepare_install_root(Path(root))
    lock = _lock_install(root)
    try:
        state = _active_state(root)
        _require_expected(state, expected_active)
        if state is not None and state["active"]["bundle_id"] == release["bundle_id"]:
            return state
        generation = _extract_generation(release, Path(archive), root)
        installed = _verify_generation(generation, expected_bundle=release["bundle_id"])
        next_state = {
            "format": STATE_FORMAT,
            "revision": 1 if state is None else state["revision"] + 1,
            "active": _state_reference(installed),
            "rollback": (_state_reference(state["active"]) if state is not None else None),
        }
        _commit_state(root, next_state)
        return verify_state_file(root / "install-state.json")
    finally:
        fcntl.flock(lock, fcntl.LOCK_UN)
        os.close(lock)


def rollback_bundle(root: Path, *, expected_active: str, target_bundle: str) -> dict:
    if SHA_RE.fullmatch(expected_active) is None or SHA_RE.fullmatch(target_bundle) is None:
        raise BundleError("rollback bundle identity is invalid")
    root = _prepare_install_root(Path(root))
    lock = _lock_install(root)
    try:
        state = _active_state(root)
        _require_expected(state, expected_active)
        if state is None:
            raise BundleError("cannot roll back an empty install root")
        if target_bundle == expected_active:
            return state
        rollback = state["rollback"]
        if rollback is None or rollback["bundle_id"] != target_bundle:
            raise BundleError("requested rollback generation is unavailable")
        next_state = {"format": STATE_FORMAT, "revision": state["revision"] + 1,
                      "active": _state_reference(rollback),
                      "rollback": _state_reference(state["active"])}
        _commit_state(root, next_state)
        return verify_state_file(root / "install-state.json")
    finally:
        fcntl.flock(lock, fcntl.LOCK_UN)
        os.close(lock)


def _expected(value: str) -> str | None:
    if value == "none":
        return None
    if SHA_RE.fullmatch(value) is None:
        raise argparse.ArgumentTypeError("expected bundle must be 'none' or lowercase SHA-256")
    return value


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build")
    build.add_argument("--spec", required=True, type=Path)
    build.add_argument("--archive", required=True, type=Path)
    build.add_argument("--manifest", required=True, type=Path)
    build.add_argument("--snapshot", required=True, type=Path)
    build.add_argument("--corpus-root", required=True, type=Path)
    verify = commands.add_parser("verify")
    verify.add_argument("--manifest", required=True, type=Path)
    verify.add_argument("--archive", required=True, type=Path)
    install = commands.add_parser("install")
    install.add_argument("--manifest", required=True, type=Path)
    install.add_argument("--archive", required=True, type=Path)
    install.add_argument("--root", required=True, type=Path)
    install.add_argument("--expect-active", required=True, type=_expected)
    rollback = commands.add_parser("rollback")
    rollback.add_argument("--root", required=True, type=Path)
    rollback.add_argument("--expect-active", required=True, type=_expected)
    rollback.add_argument("--to", required=True, type=_expected)
    installed = commands.add_parser("verify-installed")
    installed.add_argument("--root", required=True, type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "build":
            value = build_bundle(
                args.spec, args.archive, args.manifest,
                snapshot=args.snapshot, corpus_root=args.corpus_root,
            )
        elif args.command == "verify":
            value = verify_release(args.manifest, args.archive)
        elif args.command == "install":
            value = install_bundle(
                args.manifest, args.archive, args.root, expected_active=args.expect_active
            )
        elif args.command == "rollback":
            if args.expect_active is None or args.to is None:
                raise BundleError("rollback bundle IDs cannot be 'none'")
            value = rollback_bundle(
                args.root, expected_active=args.expect_active, target_bundle=args.to
            )
        else:
            value = verify_state_file(args.root / "install-state.json")
        print(_canonical(value).decode("ascii"), end="")
        return 0
    except (BundleError, OSError) as exc:
        parser = _parser()
        parser.error(str(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
