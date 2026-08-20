#!/usr/bin/env python3
"""Exercise the authored-source size budget."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.deslop_audit import source_budget_findings


class SourceBudgetTest(unittest.TestCase):
    def test_new_source_must_fit_default_limits(self) -> None:
        self.assertEqual(
            ["source-lines: 4 > 3"],
            source_budget_findings("a\nb\nc\nd\n", 3, 5, None, None),
        )
        self.assertEqual(
            ["overlong-lines: 1 > 0 at 5 columns"],
            source_budget_findings("123456\n", 3, 5, None, None),
        )

    def test_existing_debt_may_not_grow(self) -> None:
        self.assertEqual(
            [],
            source_budget_findings("123456\nb\nc\nd\n", 3, 5, 4, 1),
        )
        self.assertEqual(
            [
                "source-lines: 5 > 4",
                "overlong-lines: 2 > 1 at 5 columns",
            ],
            source_budget_findings(
                "123456\nabcdef\nb\nc\nd\n", 3, 5, 4, 1
            ),
        )

    def test_reduced_debt_requires_a_lower_budget(self) -> None:
        self.assertEqual(
            [
                "stale-source-lines-budget: lower 4 to 3",
                "stale-overlong-lines-budget: lower 1 to 0",
            ],
            source_budget_findings("a\nb\nc\n", 3, 5, 4, 1),
        )


if __name__ == "__main__":
    unittest.main()
