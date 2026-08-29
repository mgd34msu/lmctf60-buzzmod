#!/usr/bin/env python3
"""Validate stable installed RUNE files before a fleet launch."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import stat
from typing import Sequence

try:
    import runeio
except ModuleNotFoundError:
    from tools import runeio


MAP_RE = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_-]{0,62}\Z")
_HAS_PINNED_PARENT = (
    hasattr(os, "O_DIRECTORY") and
    hasattr(os, "O_NOFOLLOW") and
    os.open in os.supports_dir_fd and
    os.stat in os.supports_dir_fd and
    os.stat in os.supports_follow_symlinks
)


def _file_id(info: os.stat_result) -> tuple[int, int] | None:
    device = getattr(info, "st_dev", None)
    inode = getattr(info, "st_ino", None)
    if (not isinstance(device, int) or isinstance(device, bool) or
            not isinstance(inode, int) or isinstance(inode, bool) or
            device < 0 or inode <= 0):
        return None
    return device, inode


def _has_multiple_links(info: os.stat_result) -> bool:
    link_count = getattr(info, "st_nlink", None)
    return (isinstance(link_count, int) and not isinstance(link_count, bool) and
            link_count > 1)


def _timestamp_ns(info: os.stat_result, name: str) -> int | float | None:
    value = getattr(info, f"{name}_ns", None)
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return value
    value = getattr(info, name, None)
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return value
    return None


def _portable_identity(info: os.stat_result) -> tuple[object, ...]:
    return (
        stat.S_IFMT(info.st_mode),
        info.st_size,
        _timestamp_ns(info, "st_mtime"),
        _timestamp_ns(info, "st_ctime"),
    )


def _check_regular(path: Path, info: os.stat_result) -> None:
    if not stat.S_ISREG(info.st_mode):
        raise ValueError(f"RUNE artifact is not a regular file: {path}")
    if _has_multiple_links(info):
        raise ValueError(f"RUNE artifact has multiple links: {path}")
    if info.st_size < 0 or info.st_size > runeio.MAX_RUNE_FILE_BYTES:
        raise ValueError(f"RUNE artifact exceeds the file-size limit: {path}")


def _check_parent_components(path: Path) -> None:
    absolute = Path(os.path.abspath(path))
    current = Path(absolute.anchor)
    for part in absolute.parts[1:-1]:
        current /= part
        try:
            info = os.lstat(current)
        except OSError as exc:
            raise ValueError(
                f"cannot stat RUNE artifact parent {current}: {exc}") from exc
        if stat.S_ISLNK(info.st_mode):
            raise ValueError(f"symlink path component is forbidden: {current}")
        if not stat.S_ISDIR(info.st_mode):
            raise ValueError(f"RUNE artifact parent is not a directory: {current}")


def _open_flags(*, directory: bool = False) -> int:
    flags = os.O_RDONLY
    flag_names = ["O_BINARY", "O_CLOEXEC", "O_NOINHERIT", "O_NOFOLLOW"]
    if directory:
        flag_names.append("O_DIRECTORY")
    else:
        flag_names.append("O_NONBLOCK")
    for flag_name in flag_names:
        flags |= getattr(os, flag_name, 0)
    return flags


def _pin_parent(path: Path) -> tuple[int | None, Path, str]:
    absolute = Path(os.path.abspath(path))
    if not _HAS_PINNED_PARENT:
        _check_parent_components(absolute)
        return None, absolute, absolute.name

    try:
        parent_fd = os.open(absolute.anchor, _open_flags(directory=True))
    except OSError as exc:
        raise ValueError(
            f"cannot open RUNE artifact parent {absolute.anchor}: {exc}") from exc
    try:
        if not stat.S_ISDIR(os.fstat(parent_fd).st_mode):
            raise ValueError(
                f"RUNE artifact parent is not a directory: {absolute.anchor}")
        current = Path(absolute.anchor)
        for part in absolute.parts[1:-1]:
            current /= part
            try:
                next_fd = os.open(
                    part, _open_flags(directory=True), dir_fd=parent_fd)
            except OSError as exc:
                try:
                    info = os.stat(part, dir_fd=parent_fd, follow_symlinks=False)
                except OSError:
                    info = None
                if info is not None and stat.S_ISLNK(info.st_mode):
                    raise ValueError(
                        f"symlink path component is forbidden: {current}") from exc
                raise ValueError(
                    f"cannot open RUNE artifact parent {current}: {exc}") from exc
            try:
                if not stat.S_ISDIR(os.fstat(next_fd).st_mode):
                    raise ValueError(
                        f"RUNE artifact parent is not a directory: {current}")
            except BaseException:
                os.close(next_fd)
                raise
            os.close(parent_fd)
            parent_fd = next_fd
        return parent_fd, absolute, absolute.name
    except BaseException:
        os.close(parent_fd)
        raise


def _read_fd(fd: int) -> bytes:
    payload = bytearray()
    remaining = runeio.MAX_RUNE_FILE_BYTES + 1
    while remaining:
        block = os.read(fd, min(1024 * 1024, remaining))
        if not block:
            break
        payload.extend(block)
        remaining -= len(block)
    if len(payload) > runeio.MAX_RUNE_FILE_BYTES:
        raise ValueError("RUNE artifact exceeds the file-size limit")
    return bytes(payload)


def _stat_leaf(path: Path, parent_fd: int | None, leaf_name: str):
    if parent_fd is None:
        return os.lstat(path)
    return os.stat(leaf_name, dir_fd=parent_fd, follow_symlinks=False)


def _open_leaf(path: Path, parent_fd: int | None, leaf_name: str) -> int:
    if parent_fd is None:
        return os.open(path, _open_flags())
    return os.open(leaf_name, _open_flags(), dir_fd=parent_fd)


def _read_named_at(
        path: Path, parent_fd: int | None,
        leaf_name: str) -> tuple[bytes, tuple[int, int] | None]:
    try:
        named_before = _stat_leaf(path, parent_fd, leaf_name)
    except OSError as exc:
        raise ValueError(f"cannot stat RUNE artifact {path}: {exc}") from exc
    _check_regular(path, named_before)

    try:
        fd = _open_leaf(path, parent_fd, leaf_name)
    except OSError as exc:
        raise ValueError(f"cannot open RUNE artifact {path}: {exc}") from exc
    try:
        opened = os.fstat(fd)
        _check_regular(path, opened)
        if _portable_identity(named_before) != _portable_identity(opened):
            raise ValueError(f"RUNE artifact was replaced while opening: {path}")
        named_id = _file_id(named_before)
        opened_id = _file_id(opened)
        if named_id is not None and opened_id is not None:
            if named_id != opened_id:
                raise ValueError(f"RUNE artifact was replaced while opening: {path}")
            comparable_id = opened_id
        else:
            comparable_id = None

        payload = _read_fd(fd)
        after = os.fstat(fd)
        _check_regular(path, after)
        if (_portable_identity(opened) != _portable_identity(after) or
                len(payload) != after.st_size):
            raise ValueError(f"RUNE artifact changed while reading: {path}")
    finally:
        os.close(fd)

    try:
        named_after = _stat_leaf(path, parent_fd, leaf_name)
    except OSError as exc:
        raise ValueError(f"RUNE artifact was replaced while reading: {path}") from exc
    _check_regular(path, named_after)
    if _portable_identity(after) != _portable_identity(named_after):
        raise ValueError(f"RUNE artifact changed or was replaced while reading: {path}")
    if comparable_id is not None and _file_id(named_after) != comparable_id:
        raise ValueError(f"RUNE artifact was replaced while reading: {path}")
    return payload, comparable_id


def _read_named_once(path: Path) -> tuple[bytes, tuple[int, int] | None]:
    parent_fd, absolute, leaf_name = _pin_parent(path)
    try:
        return _read_named_at(absolute, parent_fd, leaf_name)
    finally:
        if parent_fd is not None:
            os.close(parent_fd)


def _read_stable_rune(path: Path):
    """Read and decode one stable named RUNE file."""
    parent_fd, absolute, leaf_name = _pin_parent(path)
    try:
        payload, file_id = _read_named_at(absolute, parent_fd, leaf_name)
        named_payload, named_file_id = _read_named_at(
            absolute, parent_fd, leaf_name)
    finally:
        if parent_fd is not None:
            os.close(parent_fd)
    if payload != named_payload:
        raise ValueError(f"RUNE artifact content changed while reading: {absolute}")
    if (file_id is not None and named_file_id is not None and
            file_id != named_file_id):
        raise ValueError(f"RUNE artifact was replaced while reading: {absolute}")
    return runeio.decode_rune(payload), hashlib.sha256(payload).hexdigest()


def validate_runes(maps_dir: Path, map_names: Sequence[str]) -> None:
    if not map_names:
        raise ValueError("no maps selected")
    for map_name in map_names:
        if MAP_RE.fullmatch(map_name) is None:
            raise ValueError(f"invalid map name {map_name!r}")
        rune_path = maps_dir / f"{map_name}.rune"
        rune, _rune_sha256 = _read_stable_rune(rune_path)
        if rune.header.map_name != map_name:
            raise ValueError(
                f"{map_name}.rune authenticates map {rune.header.map_name!r}"
            )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--maps-dir", type=Path, required=True)
    parser.add_argument("maps", nargs="+")
    args = parser.parse_args(argv)
    try:
        validate_runes(args.maps_dir, args.maps)
    except ValueError as exc:
        parser.error(f"artifact preflight failed: {exc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
