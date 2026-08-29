#!/usr/bin/env python3
"""Exercise the authored-source size budget."""

import contextlib
import io
import json
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import deslop_audit
from tools.deslop_audit import (
    load_source_budget,
    source_budget_findings,
    source_review_recommended,
    stale_allowance_paths,
    tracked_authored_files,
)


class SourceBudgetTest(unittest.TestCase):
    @staticmethod
    def source_with_lines(line_count: int) -> str:
        return "x\n" * line_count

    def test_review_threshold_is_not_a_hard_limit(self) -> None:
        for line_count in (799, 800, 801):
            with self.subTest(line_count=line_count):
                self.assertEqual(
                    [],
                    source_budget_findings(
                        self.source_with_lines(line_count), 9999, 100, None
                    ),
                )
        self.assertFalse(
            source_review_recommended(self.source_with_lines(799), 800)
        )
        self.assertFalse(
            source_review_recommended(self.source_with_lines(800), 800)
        )
        self.assertTrue(
            source_review_recommended(self.source_with_lines(801), 800)
        )

    def test_absolute_source_line_limit_boundaries(self) -> None:
        self.assertEqual(
            [],
            source_budget_findings(
                self.source_with_lines(9999), 9999, 100, None
            ),
        )
        self.assertEqual(
            ["source-lines: 10000 > 9999"],
            source_budget_findings(
                self.source_with_lines(10000), 9999, 100, None
            ),
        )
        self.assertEqual(
            ["source-lines: 10001 > 9999"],
            source_budget_findings(
                self.source_with_lines(10001), 9999, 100, None
            ),
        )

    def test_non_lf_separators_do_not_create_physical_source_lines(self) -> None:
        for separator in ("\f", "\v", "\x85", "\u2028", "\u2029"):
            with self.subTest(separator=ascii(separator)):
                text = "x\n" * 9998 + f"x{separator}x"
                self.assertEqual(
                    [],
                    source_budget_findings(text, 9999, 100, None),
                )
                self.assertFalse(source_review_recommended(text, 9999))

    def test_main_preserves_cr_while_reading_authored_source(self) -> None:
        cases = (
            b"x\r" * 10000,
            b"x\r\n" * 9998 + b"x\rx",
        )
        for content in cases:
            with self.subTest(content=content[-8:]):
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    relative = Path("slipgate/source.c")
                    source = root / relative
                    source.parent.mkdir(parents=True)
                    source.write_bytes(content)
                    output = io.StringIO()
                    with (
                        mock.patch.object(deslop_audit, "ROOT", root),
                        mock.patch.object(
                            deslop_audit,
                            "tracked_files",
                            return_value=[relative],
                        ),
                        mock.patch.object(
                            deslop_audit,
                            "load_source_budget",
                            return_value=(9999, 800, 100000, {}),
                        ),
                        contextlib.redirect_stdout(output),
                    ):
                        self.assertEqual(0, deslop_audit.main())
                    self.assertIn("deslop findings: 0", output.getvalue())

    def test_main_does_not_count_crlf_cr_as_a_source_column(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            relative = Path("slipgate/source.c")
            source = root / relative
            source.parent.mkdir(parents=True)
            source.write_bytes(b"x" * 100 + b"\r\n")
            output = io.StringIO()
            with (
                mock.patch.object(deslop_audit, "ROOT", root),
                mock.patch.object(
                    deslop_audit, "tracked_files", return_value=[relative]
                ),
                mock.patch.object(
                    deslop_audit,
                    "load_source_budget",
                    return_value=(9999, 800, 100, {}),
                ),
                contextlib.redirect_stdout(output),
            ):
                self.assertEqual(0, deslop_audit.main())
            self.assertIn("deslop findings: 0", output.getvalue())

    def test_main_counts_bare_cr_as_a_source_column(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            relative = Path("slipgate/source.c")
            source = root / relative
            source.parent.mkdir(parents=True)
            source.write_bytes(b"x" * 100 + b"\r")
            output = io.StringIO()
            with (
                mock.patch.object(deslop_audit, "ROOT", root),
                mock.patch.object(
                    deslop_audit, "tracked_files", return_value=[relative]
                ),
                mock.patch.object(
                    deslop_audit,
                    "load_source_budget",
                    return_value=(9999, 800, 100, {}),
                ),
                contextlib.redirect_stdout(output),
            ):
                self.assertEqual(1, deslop_audit.main())
            self.assertIn(
                "slipgate/source.c: overlong-lines: 1 > 0 at 100 columns",
                output.getvalue(),
            )

    def test_source_size_and_line_length_are_independent(self) -> None:
        self.assertEqual(
            ["overlong-lines: 1 > 0 at 5 columns"],
            source_budget_findings("123456\n" + "x\n" * 800, 9999, 5, None),
        )
        self.assertEqual(
            [
                "source-lines: 10000 > 9999",
                "overlong-lines: 1 > 0 at 5 columns",
            ],
            source_budget_findings(
                "123456\n" + "x\n" * 9999, 9999, 5, None
            ),
        )

    def test_existing_overlong_line_debt_may_not_grow(self) -> None:
        self.assertEqual(
            [],
            source_budget_findings("123456\nb\nc\nd\n", 9999, 5, 1),
        )
        self.assertEqual(
            ["overlong-lines: 2 > 1 at 5 columns"],
            source_budget_findings(
                "123456\nabcdef\nb\nc\nd\n", 9999, 5, 1
            ),
        )

    def test_reduced_overlong_line_debt_requires_a_lower_budget(self) -> None:
        self.assertEqual(
            ["stale-overlong-lines-budget: lower 1 to 0"],
            source_budget_findings("a\nb\nc\n", 9999, 5, 1),
        )

    def test_deleted_and_renamed_allowance_paths_are_stale(self) -> None:
        self.assertEqual(
            ["slipgate/deleted.c"],
            stale_allowance_paths(
                [Path("slipgate/current.c")], {"slipgate/deleted.c": 1}
            ),
        )
        self.assertEqual(
            ["slipgate/old_name.c"],
            stale_allowance_paths(
                [Path("slipgate/new_name.c")], {"slipgate/old_name.c": 1}
            ),
        )
        self.assertEqual(
            ["overlong-lines: 1 > 0 at 5 columns"],
            source_budget_findings("123456\n", 9999, 5, None),
        )

    def test_non_authored_and_generated_files_are_excluded(self) -> None:
        paths = [
            Path("g_main.c"),
            Path("docs/example.py"),
            Path("slipgate/sg_authored.c"),
            Path("slipgate/sg_contract.generated.h"),
            Path("slipgate/regenerated_parser.c"),
            Path("tests/support/imported.c"),
            Path("tests/slipgate/escape.generated.c"),
            Path("tests/test_authored.py"),
            Path("tools/check.sh"),
            Path("tools/rune_contracts_generated.py"),
            Path("tools/slipgate/escape.generated.h"),
            Path("GNUmakefile"),
        ]
        self.assertEqual(
            [
                Path("slipgate/sg_authored.c"),
                Path("slipgate/regenerated_parser.c"),
                Path("tests/slipgate/escape.generated.c"),
                Path("tests/test_authored.py"),
                Path("tools/check.sh"),
                Path("tools/slipgate/escape.generated.h"),
                Path("GNUmakefile"),
            ],
            tracked_authored_files(paths),
        )

    def test_regenerated_name_does_not_escape_the_absolute_limit(self) -> None:
        relative = Path("slipgate/regenerated_parser.c")
        self.assertEqual([relative], tracked_authored_files([relative]))
        self.assertEqual(
            ["source-lines: 10001 > 9999"],
            source_budget_findings(
                self.source_with_lines(10001), 9999, 100, None
            ),
        )

    def test_tracked_authored_symlink_is_a_finding_without_following(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "repo"
            source = root / "slipgate" / "outside.c"
            source.parent.mkdir(parents=True)
            source.symlink_to(Path(directory) / "missing-outside.c")
            relative = Path("slipgate/outside.c")

            self.assertEqual([relative], tracked_authored_files([relative]))
            self.assertEqual(
                "tracked-symlink: authored source must be a regular file",
                deslop_audit.authored_file_finding(root, relative),
            )
            output = io.StringIO()
            with (
                mock.patch.object(deslop_audit, "ROOT", root),
                mock.patch.object(
                    deslop_audit, "tracked_files", return_value=[relative]
                ),
                mock.patch.object(
                    deslop_audit,
                    "load_source_budget",
                    return_value=(9999, 800, 100, {}),
                ),
                contextlib.redirect_stdout(output),
            ):
                self.assertEqual(1, deslop_audit.main())
            self.assertIn(
                f"{relative}: tracked-symlink: authored source must be a regular file",
                output.getvalue(),
            )

    def test_committed_policy_has_no_per_file_source_size_caps(self) -> None:
        self.assertEqual((9999, 800, 100), load_source_budget()[:3])

    def test_allowance_paths_must_be_normalized_repository_relative(self) -> None:
        invalid_paths = (
            "/tmp/source.c",
            "../source.c",
            "tools/../source.c",
            "C:/source.c",
            r"C:\source.c",
            r"..\source.c",
        )
        for invalid_path in invalid_paths:
            with self.subTest(invalid_path=invalid_path):
                with tempfile.TemporaryDirectory() as directory:
                    policy = Path(directory) / "source-size-budget.json"
                    policy.write_text(
                        json.dumps(
                            {
                                "format": "lmctf-authored-source-policy-v2",
                                "absolute_max_authored_source_lines": 9999,
                                "review_threshold_lines": 800,
                                "max_line_length": 100,
                                "overlong_files": {invalid_path: 1},
                            }
                        ),
                        encoding="utf-8",
                    )
                    with mock.patch.object(
                        deslop_audit, "SOURCE_BUDGET_PATH", policy
                    ):
                        with self.assertRaisesRegex(
                            ValueError,
                            "source budget paths must be normalized "
                            "repository-relative paths",
                        ):
                            load_source_budget()


if __name__ == "__main__":
    unittest.main()
