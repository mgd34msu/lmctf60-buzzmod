#!/usr/bin/env python3
"""Keep the completion plan compact without weakening its release contract."""

import re
import runpy
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLAN = ROOT / "PROJECT-COMPLETION-PLAN.md"
ROUTE_ONLY_CANDIDATES = set(runpy.run_path(
    ROOT / "tools" / "rune_corpus_policy.py"
)["APPROVED_ROUTE_ONLY_MAPS"])


class ProjectCompletionPlanTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.text = PLAN.read_text(encoding="utf-8")
        cls.flat_text = " ".join(cls.text.split())

    def section(self, heading: str, next_heading: str) -> str:
        start = self.text.index(heading)
        end = self.text.index(next_heading, start)
        return " ".join(self.text[start:end].split())

    def test_plan_stays_compact_and_current(self) -> None:
        self.assertLessEqual(len(self.text.splitlines()), 450)
        self.assertLessEqual(len(self.text.split()), 4500)
        self.assertEqual(1, len(re.findall(r"^# ", self.text, re.MULTILINE)))
        self.assertNotIn("/tmp/", self.text)
        self.assertLessEqual(
            len(re.findall(r"\b[0-9a-f]{12,40}\b", self.text)),
            4,
            "keep only release-identifying hashes in the plan",
        )

    def test_corpus_authority_is_preserved(self) -> None:
        section = self.section("### Corpus authority", "### Configuration roles")
        for required in (
            "175 maps",
            "180 BSPs",
            "tools/rune-corpus-maps.txt",
            "tools/topmaps.txt",
            "no special completion or release authority",
            "unsuffixed base is out of scope",
            "Build the frozen BSP set by iterating the manifest",
            "lmctf05",
            "smap31",
            "xmap07",
            "xmap11",
            "xmap14",
        ):
            self.assertIn(required, section)

    def test_route_only_is_a_provisional_fallback(self) -> None:
        section = self.section(
            "## Route-only candidate inventory", "## Current pre-freeze work"
        )
        actual = set(re.findall(r"^\| `([^`]+)` \|", section, re.MULTILINE))
        if not actual:
            actual = set(re.findall(r"\| `([^`]+)` \|", section))
        self.assertEqual(ROUTE_ONLY_CANDIDATES, actual)
        manifest_path = ROOT / "tools" / "rune-corpus-maps.txt"
        manifest = {
            name for name in manifest_path.read_text(encoding="ascii").splitlines()
            if name
        }
        self.assertTrue(actual.issubset(manifest))
        for required in (
            "candidates, not a fixed release class",
            "complete-route contract before applying the route-only",
        ):
            self.assertIn(required, self.flat_text)
        for required in (
            "must identify the missing path",
            "replay-proved complete-route update",
            "does not block the initial release",
        ):
            self.assertIn(required, section)

        contract = self.section(
            "### Route-only release contract", "### Human learning"
        ).lower()
        for required in (
            "seedless recovery",
            "combat",
            "escort",
            "item behavior",
            "exactly two authenticated root markers",
            "spawned flag stands",
            "rejects arbitrary terminal sinks",
        ):
            self.assertIn(required, contract)

    def test_configuration_and_release_roles_are_distinct(self) -> None:
        configuration = self.section(
            "### Configuration roles", "### Distribution scope"
        )
        distribution = self.section(
            "### Distribution scope", "## RUNE acceptance rules"
        )
        for required in (
            "tools/rune.cfg",
            "generator configuration",
            "tools/route-only-match.cfg",
            "ordinary route-only matches",
        ):
            self.assertIn(required, configuration)
        for required in (
            "tracked `assets/lmctf6-buzzmod.pak`",
            "static scoreboard art and sounds",
            "Defer separate downloads of generated RUNEs",
            "generated corpus",
            "production server bundle",
        ):
            self.assertIn(required, distribution)
        installation = self.section(
            "### 4. Install and test the production bundle",
            "### 5. Collect real-match evidence",
        )
        self.assertIn("Assemble the authenticated bundle", installation)

    def test_learning_and_match_evidence_stay_complete(self) -> None:
        learning = self.section("### Human learning", "## Implemented systems")
        for required in (
            "Capture and importer tests",
            "exact-bound SNAG",
            "cold-load the staged bundle",
            "Install new sidecars before the RUNE commit point",
            "must never publish mixed graph and sidecar state",
        ):
            self.assertIn(required, learning)
        matches = self.section(
            "### 5. Collect real-match evidence", "### 6. Tag and verify the release"
        )
        for required in (
            "earned perception",
            "item pursuit and commitment retirement",
            "one terminal lifecycle",
            "spectator sound attribution",
        ):
            self.assertIn(required, matches)

    def test_freeze_and_invalidation_rules_remain_binding(self) -> None:
        landing = self.section(
            "### 1. Land the final source candidate", "### 2. Create the immutable freeze"
        ).lower()
        freeze = self.section(
            "### 2. Create the immutable freeze", "### 3. Generate and accept all 175 RUNEs"
        ).lower()
        generation = self.section(
            "### 3. Generate and accept all 175 RUNEs",
            "### 4. Install and test the production bundle",
        ).lower()
        matches = self.section(
            "### 5. Collect real-match evidence", "### 6. Tag and verify the release"
        ).lower()
        invalidation = self.section(
            "## Invalidation rules", "## Completion checklist"
        ).lower()
        self.assertIn("slipgate", landing)
        self.assertIn("main", landing)
        self.assertIn("exact-commit ci", landing)
        self.assertIn("do not commit source or documentation changes", freeze)
        self.assertLess(
            generation.index("complete-route contract first"),
            generation.index("approved `route_only`"),
        )
        for required in (
            "content-addressed corpus", "`finalize`", "`verify-final`"
        ):
            self.assertIn(required, generation)
        self.assertIn("abandon the freeze", matches)
        self.assertIn("zero to ten", matches)
        release = self.section(
            "### 6. Tag and verify the release", "## Invalidation rules"
        ).lower()
        self.assertIn("tag the unchanged frozen commit", release)
        self.assertIn("evidence-only plan update after the release", release)
        self.assertIn("do not rebuild the release", release)
        for required in (
            "synchronized `slipgate` and `main` branches",
            "one unchanged source commit",
        ):
            self.assertIn(required, self.flat_text.lower())
        for required in (
            "python runtime",
            "reader",
            "linter",
            "semantic checker",
            "bsp change",
            "a rune change invalidates its snag",
            "a bundle change invalidates installed-bundle and match evidence",
        ):
            self.assertIn(required, invalidation)


if __name__ == "__main__":
    unittest.main()
