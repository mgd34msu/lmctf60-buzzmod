#!/usr/bin/env python3
"""Keep the completion plan complete, current, and reviewable."""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLAN = ROOT / "PROJECT-COMPLETION-PLAN.md"

REQUIRED_CATALOG_IDS = {
    *(f"BSP-{index}" for index in range(1, 8)),
    *(f"MOV-{index}" for index in range(1, 10)),
    *(f"NAV-{index}" for index in range(1, 7)),
    *(f"STR-{index}" for index in range(1, 3)),
    *(f"TAC-{index}" for index in range(1, 5)),
    *(f"BEL-{index}" for index in range(1, 6)),
    *(f"COM-{index}" for index in range(1, 5)),
    *(f"LRN-{index}" for index in range(1, 3)),
    "VIS-1",
    *(f"ART-{index}" for index in range(1, 10)),
    *(f"REL-{index}" for index in range(1, 3)),
}


class ProjectCompletionPlanTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.text = PLAN.read_text(encoding="utf-8")
        cls.flat = " ".join(cls.text.split())

    def test_plan_stays_compact_and_clean(self) -> None:
        self.assertLessEqual(len(self.text.splitlines()), 350)
        self.assertLessEqual(len(self.text.split()), 5500)
        self.assertEqual(1, len(re.findall(r"^# ", self.text, re.MULTILINE)))
        self.assertNotIn("/tmp/", self.text)
        self.assertLessEqual(
            len(re.findall(r"\b[0-9a-f]{12,40}\b", self.text)), 4
        )

    def test_catalog_has_every_required_item_exactly_once(self) -> None:
        found = re.findall(
            r"^\| ((?:BSP|MOV|NAV|STR|TAC|BEL|COM|LRN|VIS|ART|REL)-\d+) \|",
            self.text,
            re.MULTILINE,
        )
        self.assertEqual(REQUIRED_CATALOG_IDS, set(found))
        self.assertEqual(len(found), len(set(found)))
        for item in found:
            row = next(line for line in self.text.splitlines()
                       if line.startswith(f"| {item} |"))
            self.assertGreaterEqual(row.count("|"), 5)

    def test_foundational_rune_contract_is_explicit(self) -> None:
        for required in (
            "BSP and its entities are ground truth",
            "standing or crouching player hull fits",
            "full three-dimensional configuration space",
            "Every legal connection must be represented",
            "Physically disconnected valid regions remain represented",
            "does not prescribe a sequence of named movement actions",
            "destination at any valid point",
            "without regenerating the RUNE",
            "continuous/anisotropic field method",
            "update arbitrary, dropped, displaced, or moving destinations incrementally",
        ):
            self.assertIn(required, self.flat)

    def test_movement_and_map_physics_are_complete(self) -> None:
        for required in (
            "gravity, air acceleration, maximum velocity",
            "walking, crouching, ramps, steps, jumping, dropping, swimming",
            "teleports, doors, buttons, triggers, dwell waits, and hook traversal",
            "hook-bolt visibility from player-body motion",
            "fire from any valid 3D pose",
            "excluding sky",
            "pull, release, coast, air-control",
            "gravity 100",
            "without Dijkstra or human input",
            "no rocket/grenade-jump search",
            "physical hook lifecycle caps remain",
        ):
            self.assertIn(required, self.flat)

    def test_strategy_tactics_beliefs_and_weapons_are_separate(self) -> None:
        for required in (
            "typed queue of goals",
            "prerequisites, alternatives, priorities",
            "Tactics chooses legal movement execution",
            "movement mechanism that discovered connectivity does not own traversal",
            "per-team, per-player runtime probability distributions",
            "Keep all players and match state out of the static RUNE",
            "Sound creates diffuse or multimodal beliefs",
            "negative visual evidence",
            "weapon profiles/kernels for hitscan",
            "rockets and splash",
            "grenades and bounce/fuse",
            "predicted target probability and future weapon effect",
            "Team communication may reduce uncertainty",
            "wall/floor shots that outperform aiming at a belief mean",
            "cannot create permanent minima",
        ):
            self.assertIn(required, self.flat)

    def test_human_and_reusable_code_boundaries_are_binding(self) -> None:
        for required in (
            "ordinary human hook remains base-LMCTF behavior",
            "Preserve human playthroughs",
            "last-resort evidence and learning input",
            "may not create geometry",
            "Post-match learning is required",
            "live learning is optional",
            "Every **review/keep** or **reshape** item",
            "implementation and every production caller",
            "Do not use its existing tests as the sole oracle",
            "real integration proof",
        ):
            self.assertIn(required, self.flat)

    def test_generation_and_release_policy_is_preserved(self) -> None:
        for required in (
            "tools/rune-corpus-maps.txt",
            "sole 175-map authority",
            "tools/topmaps.txt",
            "180 durable BSPs",
            "lmctf05",
            "smap31",
            "xmap07",
            "xmap11",
            "xmap14",
            "12 isolated workers",
            "no generation or review timeout",
            "zero pre-rewrite RUNEs",
            "Do not push",
            "Do not publish remotely",
            "tagged locally as `v1.0.0`",
        ):
            self.assertIn(required, self.flat)

    def test_execution_order_and_invalidation_are_explicit(self) -> None:
        headings = [
            "### 1. Freeze the target specification",
            "### 2. Build the BSP configuration-space foundation",
            "### 3. Add movement capabilities and time cost",
            "### 4. Replace runtime navigation ownership",
            "### 5. Integrate beliefs, weapons, and learning",
            "### 6. Replace artifacts and audit all retained subsystems",
            "### 7. Prove real BSPs, performance, and determinism",
            "### 8. Freeze and generate all 175 RUNEs",
            "### 9. Install, observe, and release",
        ]
        offsets = [self.text.index(heading) for heading in headings]
        self.assertEqual(offsets, sorted(offsets))
        self.assertLess(
            self.text.index("### 7. Prove real BSPs"),
            self.text.index("### 8. Freeze and generate all 175 RUNEs"),
        )
        for required in (
            "invalidates the snapshot and every downstream artifact",
            "A RUNE change invalidates its derived fields",
            "Unit and fake-engine tests never replace real BSP",
            "abandon the freeze",
        ):
            self.assertIn(required, self.flat)


if __name__ == "__main__":
    unittest.main()
