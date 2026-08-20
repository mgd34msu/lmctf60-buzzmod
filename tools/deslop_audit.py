#!/usr/bin/env python3
"""Report narrative implementation comments that do not belong in source."""

from __future__ import annotations

import ast
import io
import json
import re
import subprocess
import tokenize
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_BUDGET_PATH = ROOT / "tools" / "source-size-budget.json"
SOURCE_SUFFIXES = {".c", ".h"}
EXCLUDED = {"sqlite3.c", "sqlite3.h"}
AUTHORED_ROOTS = {"slipgate", "tests", "tools"}
AUTHORED_SUFFIXES = {".c", ".h", ".py", ".sh"}
AUTHORED_FILES = {"GNUmakefile", "Makefile"}
MAX_SLIPGATE_COMMENT_LINES = 12
MAX_PYTHON_MODULE_DOCSTRING_LINES = 12
MAX_PYTHON_MEMBER_DOCSTRING_LINES = 12
RULES = (
    ("branded-heading", re.compile(
        r"^\s*(?:THE [A-Z]|NO CAMPING THE PAD|SPREAD THE AXES|"
        r"ENTRY ENVELOPES|PROVENANCE for|EXPOSURE,)"
    )),
    ("owner-ruling", re.compile(r"owner(?:'s|s) ruling", re.IGNORECASE)),
    ("experiment-diary", re.compile(
        r"\b(?:comparison|found) observations\b|"
        r"\bobservations(?:\s+(?:analysis|case|census|finding|first|ledger|"
        r"read|show|taught|trace|verdict)|\s*[:;,)]|\s+--)",
        re.IGNORECASE,
    )),
    ("copy-history", re.compile(r"\bbody verbatim\b|\bstandards pass\b", re.IGNORECASE)),
    ("numbered-enhancement", re.compile(r"\benhancement\s+\d+\b", re.IGNORECASE)),
    ("dated-diary", re.compile(
        r"\bwave\s*\d+\b|\b20\d\d-[01]\d-[0-3]\d\b|"
        r"\brefutation review\b",
        re.IGNORECASE,
    )),
    ("result-diary", re.compile(
        r"\b(?:measurements?|census)\b.{0,80}\b(?:convicted|found|named|"
        r"proved|read|null|reduced|rejected|ruled|showed)\b|"
        r"\b(?:first cut|kept as history|owner's correction|owner's own FYI)\b",
        re.IGNORECASE,
    )),
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
                    start = index + 2
                    end = line.find("*/", start)
                    if end < 0:
                        fragments.append((line_number, line[start:].strip(" *\t")))
                        in_block = True
                        index = len(line)
                        break
                    fragments.append((line_number, line[start:end].strip(" *\t")))
                    index = end + 2
                    continue
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


def python_comments(text: str) -> list[tuple[int, str]]:
    return [
        (token.start[0], token.string[1:].strip())
        for token in tokenize.generate_tokens(io.StringIO(text).readline)
        if token.type == tokenize.COMMENT
    ]


def python_module_docstring_lines(text: str) -> int:
    tree = ast.parse(text)
    if not tree.body or not isinstance(tree.body[0], ast.Expr):
        return 0
    value = tree.body[0].value
    if not isinstance(value, ast.Constant) or not isinstance(value.value, str):
        return 0
    return value.value.count("\n") + 1


def oversized_python_member_docstrings(text: str) -> list[tuple[int, str, int]]:
    """Return functions and classes whose docstrings exceed the local limit."""
    findings = []
    tree = ast.parse(text)
    for node in ast.walk(tree):
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            continue
        docstring = ast.get_docstring(node, clean=False)
        if docstring is None:
            continue
        line_count = docstring.count("\n") + 1
        if line_count > MAX_PYTHON_MEMBER_DOCSTRING_LINES:
            findings.append((node.lineno, node.name, line_count))
    return findings


def narrative_tool_rule(line: str) -> str | None:
    if re.search(r"owner(?:'s|s) (?:ruling|order|format|metric|charge)", line,
                 re.IGNORECASE):
        return "owner-diary"
    if re.search(r"\b(?:adopted|struck|parked|trial|verdict|ledgered?)\b", line,
                 re.IGNORECASE) and re.search(r"\b(?:wave\s*\d+|20\d\d-)\b", line,
                                              re.IGNORECASE):
        return "experiment-diary"
    if re.search(r"\bborn\s+(?:wave\s*\d+|20\d\d-)", line, re.IGNORECASE):
        return "creation-diary"
    return None


def tracked_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=ROOT,
        check=True,
        stdout=subprocess.PIPE,
    )
    return [
        Path(raw.decode("utf-8"))
        for raw in result.stdout.split(b"\0")
        if raw
    ]


def tracked_source_files(paths: list[Path]) -> list[Path]:
    return [
        path for path in paths
        if path.suffix in SOURCE_SUFFIXES
        and path.as_posix() not in EXCLUDED
        and not (ROOT / path).is_symlink()
    ]


def tracked_authored_files(paths: list[Path]) -> list[Path]:
    authored = []
    for path in paths:
        name = path.as_posix()
        if name not in AUTHORED_FILES and (
                not path.parts or path.parts[0] not in AUTHORED_ROOTS):
            continue
        if name not in AUTHORED_FILES and path.suffix not in AUTHORED_SUFFIXES:
            continue
        if "generated" in path.name or path.parts[:2] == ("tests", "support"):
            continue
        if not (ROOT / path).is_symlink():
            authored.append(path)
    return authored


def source_budget_findings(text: str, default_max_lines: int,
        max_line_length: int, line_allowance: int | None,
        overlong_allowance: int | None) -> list[str]:
    lines = text.splitlines()
    line_count = len(lines)
    overlong = sum(
        len(line.expandtabs(8)) > max_line_length
        for line in lines
    )
    max_lines = line_allowance or default_max_lines
    max_overlong = overlong_allowance or 0

    findings = []
    if line_count > max_lines:
        findings.append(f"source-lines: {line_count} > {max_lines}")
    if overlong > max_overlong:
        findings.append(
            f"overlong-lines: {overlong} > {max_overlong} "
            f"at {max_line_length} columns"
        )
    if line_allowance is not None and line_count < max_lines:
        findings.append(
            f"stale-source-lines-budget: lower {max_lines} to {line_count}"
        )
    if overlong_allowance is not None and overlong < max_overlong:
        findings.append(
            f"stale-overlong-lines-budget: lower {max_overlong} to {overlong}"
        )
    return findings


def load_source_budget() -> tuple[int, int, dict[str, int], dict[str, int]]:
    document = json.loads(SOURCE_BUDGET_PATH.read_text(encoding="utf-8"))
    required = {
        "default_max_lines", "format", "max_line_length",
        "overlong_files", "oversized_files",
    }
    if set(document) != required:
        raise ValueError("source budget has unexpected top-level fields")
    if document["format"] != "lmctf-authored-source-budget-v1":
        raise ValueError("source budget format is not supported")

    for field in ("default_max_lines", "max_line_length"):
        value = document[field]
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
            raise ValueError(f"source budget {field} must be a positive integer")

    for field in ("oversized_files", "overlong_files"):
        allowances = document[field]
        if not isinstance(allowances, dict):
            raise ValueError(f"source budget {field} must be an object")
        for path, value in allowances.items():
            if not isinstance(path, str) or Path(path).as_posix() != path:
                raise ValueError("source budget paths must be normalized strings")
            if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
                raise ValueError(f"source budget {field} {path} is invalid")
    return (
        document["default_max_lines"],
        document["max_line_length"],
        document["oversized_files"],
        document["overlong_files"],
    )


def main() -> int:
    findings = 0
    tracked = tracked_files()
    for relative in tracked_source_files(tracked):
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
        doc_lines = python_module_docstring_lines(text)
        if doc_lines > MAX_PYTHON_MODULE_DOCSTRING_LINES:
            print(
                f"{relative.relative_to(ROOT)}:1: oversized-module-docstring: "
                f"{doc_lines} lines"
            )
            findings += 1
        for line_number, name, line_count in oversized_python_member_docstrings(text):
            print(
                f"{relative.relative_to(ROOT)}:{line_number}: "
                f"oversized-member-docstring: {name}: {line_count} lines"
            )
            findings += 1
        for line_number, line in python_comments(text):
            rule = narrative_tool_rule(line)
            if rule:
                print(f"{relative.relative_to(ROOT)}:{line_number}: {rule}: {line}")
                findings += 1
        finding = python_tail_after_entrypoint(relative, text)
        if finding:
            line_number, message = finding
            print(f"{relative.relative_to(ROOT)}:{line_number}: python-tail: {message}")
            findings += 1

    for relative in sorted((ROOT / "tools").glob("*.sh")):
        for line_number, line in enumerate(
                relative.read_text(encoding="utf-8").splitlines(), 1):
            stripped = line.lstrip()
            if not stripped.startswith("#") or stripped.startswith("#!"):
                continue
            rule = narrative_tool_rule(stripped[1:].strip())
            if rule:
                print(f"{relative.relative_to(ROOT)}:{line_number}: {rule}: {line.strip()}")
                findings += 1

    default_max_lines, max_line_length, oversized, overlong = load_source_budget()
    authored = tracked_authored_files(tracked)
    authored_names = {path.as_posix() for path in authored}
    budget_names = set(oversized) | set(overlong)
    for stale in sorted(budget_names - authored_names):
        print(f"{SOURCE_BUDGET_PATH.relative_to(ROOT)}: stale-path: {stale}")
        findings += 1
    for relative in authored:
        text = (ROOT / relative).read_text(encoding="utf-8")
        name = relative.as_posix()
        for message in source_budget_findings(
                text, default_max_lines, max_line_length,
                oversized.get(name), overlong.get(name)):
            print(f"{relative}: {message}")
            findings += 1
    print(f"deslop findings: {findings}")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
