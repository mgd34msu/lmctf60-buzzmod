#!/usr/bin/env python3
"""Report narrative implementation comments that do not belong in source."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".h"}
EXCLUDED = {"sqlite3.c", "sqlite3.h"}
MAX_SLIPGATE_COMMENT_LINES = 12
RULES = (
    ("the-heading", re.compile(r"^\s*THE [A-Z][A-Z0-9' -]{3,}\s*:?\s*$")),
    ("owner-ruling", re.compile(r"owner(?:'s|s) ruling", re.IGNORECASE)),
    ("experiment-diary", re.compile(
        r"\b(?:comparison|found) observations\b|"
        r"\bobservations(?:\s+(?:analysis|case|census|finding|first|ledger|"
        r"read|show|taught|trace|verdict)|\s*[:;,)]|\s+--)",
        re.IGNORECASE,
    )),
    ("copy-history", re.compile(r"\bbody verbatim\b|\bstandards pass\b", re.IGNORECASE)),
    ("numbered-enhancement", re.compile(r"\benhancement\s+\d+\b", re.IGNORECASE)),
)


def comment_fragments(text: str) -> list[tuple[int, str]]:
    """Return comment text with its source line, ignoring strings and characters."""
    fragments: list[tuple[int, str]] = []
    in_block = False
    for line_number, line in enumerate(text.splitlines(), 1):
        index = 0
        while index < len(line):
            if in_block:
                end = line.find("*/", index)
                if end < 0:
                    fragments.append((line_number, line[index:].lstrip(" *")))
                    break
                fragments.append((line_number, line[index:end].lstrip(" *")))
                in_block = False
                index = end + 2
                continue

            quote = None
            escaped = False
            while index < len(line):
                char = line[index]
                if quote is not None:
                    if escaped:
                        escaped = False
                    elif char == "\\":
                        escaped = True
                    elif char == quote:
                        quote = None
                    index += 1
                    continue
                if char in {'"', "'"}:
                    quote = char
                    index += 1
                    continue
                if line.startswith("//", index):
                    fragments.append((line_number, line[index + 2 :].strip()))
                    index = len(line)
                    break
                if line.startswith("/*", index):
                    in_block = True
                    index += 2
                    break
                index += 1
    return fragments


def block_comments(text: str) -> list[tuple[int, int, str]]:
    """Return block-comment start lines, line counts, and normalized text."""
    blocks = []
    for match in re.finditer(r"/\*(.*?)\*/", text, re.DOTALL):
        start = text.count("\n", 0, match.start()) + 1
        body = match.group(1)
        lines = [line.strip(" *\t") for line in body.splitlines()]
        normalized = " ".join(line for line in lines if line)
        blocks.append((start, match.group(0).count("\n") + 1, normalized))
    return blocks


def python_tail_after_entrypoint(path: Path, text: str) -> tuple[int, str] | None:
    match = re.search(
        r"(?m)^if __name__ == ['\"]__main__['\"]:\s*\n\s+main\(\)\s*\n",
        text,
    )
    if not match or not text[match.end() :].strip():
        return None
    line = text.count("\n", 0, match.end()) + 1
    return line, "content follows the module entry point"


def tracked_source_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=ROOT,
        check=True,
        stdout=subprocess.PIPE,
    )
    paths = []
    for raw in result.stdout.split(b"\0"):
        if not raw:
            continue
        path = Path(raw.decode("utf-8"))
        absolute = ROOT / path
        if (
            path.suffix in SOURCE_SUFFIXES
            and path.as_posix() not in EXCLUDED
            and not absolute.is_symlink()
        ):
            paths.append(path)
    return paths


def main() -> int:
    findings = 0
    for relative in tracked_source_files():
        text = (ROOT / relative).read_text(encoding="utf-8")
        for line_number, line in comment_fragments(text):
            for name, pattern in RULES:
                if pattern.search(line):
                    print(f"{relative}:{line_number}: {name}: {line}")
                    findings += 1
        if relative.parts[0] == "slipgate" and ".generated." not in relative.name:
            for line_number, line_count, body in block_comments(text):
                if line_count > MAX_SLIPGATE_COMMENT_LINES:
                    print(
                        f"{relative}:{line_number}: oversized-comment: "
                        f"{line_count} lines: {body[:120]}"
                    )
                    findings += 1

    for relative in sorted((ROOT / "tools").glob("*.py")):
        text = relative.read_text(encoding="utf-8")
        finding = python_tail_after_entrypoint(relative, text)
        if finding:
            line_number, message = finding
            print(f"{relative.relative_to(ROOT)}:{line_number}: python-tail: {message}")
            findings += 1
    print(f"narrative comment findings: {findings}")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
