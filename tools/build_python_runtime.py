#!/usr/bin/env python3
"""Build the link-free Python runtime closure used by the corpus controller."""

from __future__ import annotations

import argparse
import filecmp
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from typing import Iterable

try:
    from tools import rune_corpus_controller as controller
except ImportError:
    import rune_corpus_controller as controller


REQUIRED_EXTENSIONS = (
    "_bz2",
    "_ctypes",
    "_hashlib",
    "_json",
    "_lzma",
    "_socket",
    "_struct",
    "array",
    "fcntl",
    "math",
    "select",
    "zlib",
)
OPTIONAL_EXTENSION_DEPENDENCY_ROOTS = ("_zstd",)
LIBRARY_LINE = re.compile(
    r"\s*(\S+)\s+=>\s+(/[^\s()]+)(?:\s+\([^)]*\))?\s*\Z"
)


class RuntimeBuildError(RuntimeError):
    pass


def parse_loader_listing(text: str) -> list[tuple[str, Path]]:
    libraries: list[tuple[str, Path]] = []
    for line in text.splitlines():
        match = LIBRARY_LINE.fullmatch(line)
        if match is None:
            continue
        soname, raw_path = match.groups()
        if Path(soname).name != soname:
            continue
        libraries.append((soname, Path(raw_path)))
    return libraries


def _run_json(interpreter: Path) -> dict[str, object]:
    completed = subprocess.run(
        [
            str(interpreter),
            "-I",
            "-S",
            "-B",
            "-c",
            (
                "import json,os,sys;"
                "print(json.dumps({'version':list(sys.version_info[:2]),"
                "'stdlib':os.path.dirname(os.__file__)}))"
            ),
        ],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    value = json.loads(completed.stdout)
    if not isinstance(value, dict):
        raise RuntimeBuildError("Python probe did not return an object")
    return value


def _loader_listing(loader: Path, target: Path) -> str:
    return subprocess.run(
        [str(loader), "--list", str(target)],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    ).stdout


def _has_extension(directory: Path, stem: str) -> bool:
    return any(
        path.is_file() and not path.is_symlink() and
        (path.name == f"{stem}.so" or path.name.startswith(f"{stem}.")) and
        path.name.endswith(".so")
        for path in directory.iterdir()
    )


def _extension_dependency_roots(directory: Path) -> list[Path]:
    stems = (*REQUIRED_EXTENSIONS, *OPTIONAL_EXTENSION_DEPENDENCY_ROOTS)
    return sorted(
        path for path in directory.iterdir()
        if path.is_file() and not path.is_symlink() and any(
            path.name == f"{stem}.so" or path.name.startswith(f"{stem}.")
            for stem in stems
        )
    )


def select_interpreter(candidates: Iterable[Path]) -> tuple[Path, Path, str, Path]:
    errors: list[str] = []
    for candidate in dict.fromkeys(candidates):
        try:
            interpreter = candidate.resolve(strict=True)
            details = _run_json(interpreter)
            raw_version = details["version"]
            if (
                not isinstance(raw_version, list)
                or len(raw_version) != 2
                or any(type(part) is not int for part in raw_version)
            ):
                raise RuntimeBuildError("Python probe returned an invalid version")
            version = ".".join(str(part) for part in raw_version)
            stdlib = Path(str(details["stdlib"])).resolve(strict=True)
            dynload = stdlib / "lib-dynload"
            if not dynload.is_dir() or not all(
                _has_extension(dynload, name) for name in REQUIRED_EXTENSIONS
            ):
                raise RuntimeBuildError("required extension modules are unavailable")
            loader = Path(controller.elf_interpreter(interpreter)).resolve(strict=True)
            if not any(
                soname.startswith("libpython")
                for soname, _path in parse_loader_listing(
                    _loader_listing(loader, interpreter)
                )
            ):
                raise RuntimeBuildError("interpreter is not dynamically linked to libpython")
            return interpreter, stdlib, version, loader
        except (
            OSError,
            KeyError,
            TypeError,
            ValueError,
            json.JSONDecodeError,
            subprocess.CalledProcessError,
            controller.CorpusError,
            RuntimeBuildError,
        ) as exc:
            errors.append(f"{candidate}: {exc}")
    raise RuntimeBuildError("no usable Python interpreter: " + "; ".join(errors))


def _copy_regular(source: Path, target: Path) -> None:
    source = source.resolve(strict=True)
    if not stat.S_ISREG(source.stat().st_mode):
        raise RuntimeBuildError(f"runtime input is not regular: {source}")
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        if not target.is_file() or target.is_symlink() or not filecmp.cmp(
            source, target, shallow=False
        ):
            raise RuntimeBuildError(f"runtime basename collision: {target}")
        return
    shutil.copyfile(source, target, follow_symlinks=False)
    target.chmod(0o755 if os.access(source, os.X_OK) else 0o644)


def _copy_stdlib(stdlib: Path, target: Path) -> None:
    for directory, names, files in os.walk(stdlib, followlinks=False):
        current = Path(directory)
        names[:] = [
            name for name in names
            if name not in ("__pycache__", "site-packages") and
            not (current / name).is_symlink()
        ]
        for name in files:
            source = current / name
            if name.endswith(".pyc") or not source.is_file():
                continue
            _copy_regular(source, target / source.relative_to(stdlib))


def _freeze(root: Path) -> None:
    for directory, names, files in os.walk(root, topdown=False):
        current = Path(directory)
        for name in files:
            path = current / name
            mode = stat.S_IMODE(path.stat().st_mode)
            path.chmod(0o555 if mode & 0o111 else 0o444)
        for name in names:
            (current / name).chmod(0o555)
        current.chmod(0o555)


def _remove_tree(root: Path) -> None:
    if not root.exists():
        return
    for directory, names, files in os.walk(root):
        current = Path(directory)
        current.chmod(0o700)
        for name in names + files:
            (current / name).chmod(0o700)
    shutil.rmtree(root)


def build_runtime(output: Path, python: Path | None = None) -> Path:
    output = output.absolute()
    if output.exists() or output.is_symlink():
        raise RuntimeBuildError(f"output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    candidates = (
        [python] if python is not None
        else [Path("/usr/bin/python3"), Path(sys.executable)]
    )
    interpreter, stdlib, version, loader = select_interpreter(candidates)
    staging = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    try:
        _copy_regular(interpreter, staging / f"bin/python{version}")
        _copy_regular(loader, staging / "lib" / loader.name)
        _copy_stdlib(stdlib, staging / "lib" / f"python{version}")

        dynload = staging / "lib" / f"python{version}" / "lib-dynload"
        queue = [
            staging / f"bin/python{version}",
            *_extension_dependency_roots(dynload),
        ]
        seen: set[Path] = set()
        while queue:
            candidate = queue.pop()
            if candidate in seen:
                continue
            seen.add(candidate)
            for soname, raw_path in parse_loader_listing(
                _loader_listing(loader, candidate)
            ):
                source = raw_path.resolve(strict=True)
                if source == loader:
                    continue
                primary = staging / "lib" / source.name
                _copy_regular(source, primary)
                _copy_regular(source, staging / "lib" / soname)
                queue.append(primary)

        _freeze(staging)
        staging.rename(output)
        directory_fd = os.open(output.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
        return output
    except BaseException:
        _remove_tree(staging)
        raise


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--python", type=Path)
    args = parser.parse_args(argv)
    try:
        built = build_runtime(args.output, args.python)
    except (OSError, RuntimeBuildError, subprocess.CalledProcessError) as exc:
        parser.error(str(exc))
    print(built)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
