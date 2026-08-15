#!/usr/bin/env python3
"""Wire-boundary tests shared by the three legacy RUNE readers."""

from __future__ import annotations

import contextlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import corpusgraph  # noqa: E402
import rune_contracts_generated as contract  # noqa: E402
import runelint  # noqa: E402
import runeview  # noqa: E402


EXPECTED_ACTION_NAMES = {
    0: "RUN",
    1: "JUMP",
    2: "DROP",
    3: "HOOK",
    4: "SWIM",
    5: "LIFT",
    6: "TELEPORT",
    7: "ROCKETJUMP",
    8: "DOOR",
}
EXPECTED_ACTION_SHORT_NAMES = [
    "RUN", "JUMP", "DROP", "HOOK", "SWIM", "LIFT", "TELE", "RJ", "DOOR"
]
EXPECTED_ACTION_COLORS = {
    0: "#9a9a9a",
    1: "#00c8d7",
    2: "#e0c000",
    3: "#ff8c1a",
    4: "#3d7dff",
    5: "#8f5cff",
    6: "#00d18a",
    7: "#ff3b30",
    8: "#ff66c4",
}
EXPECTED_PROVENANCE_NAMES = {
    0: "PROVEN",
    1: "OBSERVED",
    2: "ADJUSTED",
    3: "DECLARED",
}


def _link(source, destination, action, provenance, *, anchor=(0.0, 0.0, 0.0),
          marker=0):
    return (source, destination, action, provenance, 0, 0, marker, 0, 100,
            *anchor)


def _reciprocal_links(action, provenance, *, door_controls=False):
    if door_controls:
        return (
            _link(0, 1, action, provenance, anchor=(0.0, 0.0, 0.0),
                  marker=254),
            _link(1, 0, action, provenance, anchor=(128.0, 0.0, 0.0),
                  marker=254),
        )
    anchor = (0.0, 0.0, 1.0) if action == contract.RL_ROCKETJUMP else (
        0.0, 0.0, 0.0
    )
    return (
        _link(0, 1, action, provenance, anchor=anchor),
        _link(1, 0, action, provenance, anchor=anchor),
    )


def _write_rune(directory, version, links, mapname="wirecase"):
    path = Path(directory) / f"{mapname}.rune"
    seeds = (
        (0.0, 0.0, 0.0, 0, 0),
        (128.0, 0.0, 0.0, 0, 0),
    )
    raw_map = mapname.encode("ascii") + b"\0"
    raw_map += b"\0" * (64 - len(raw_map))
    payload = bytearray(struct.pack(
        corpusgraph.HEADER_FMT,
        corpusgraph.RUNE_MAGIC,
        version,
        len(seeds),
        len(links),
        raw_map,
    ))
    for seed in seeds:
        payload.extend(struct.pack(corpusgraph.SEED_FMT, *seed))
    for link in links:
        payload.extend(struct.pack(corpusgraph.LINK_FMT, *link))
    path.write_bytes(payload)
    return path


def _lint(path):
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        flaws = runelint.lint(path)
    return flaws, output.getvalue()


class LegacyRuneReaderTests(unittest.TestCase):
    def assert_all_readers_accept(self, path, version, action):
        decoded = corpusgraph.read_rune(path)
        self.assertEqual(version, decoded["version"])
        viewed = runeview.load_rune(path)
        self.assertEqual(version, viewed.version)
        self.assertTrue(all(link["action"] == action for link in viewed.links))
        flaws, output = _lint(path)
        self.assertEqual([], flaws, output)

    def assert_record_rejected(self, path, *, action=False, provenance=False):
        with self.assertRaises(ValueError):
            corpusgraph.read_rune(path)
        with self.assertRaises(ValueError):
            runeview.load_rune(path)
        flaws, output = _lint(path)
        if action:
            self.assertTrue(
                any("links with unknown action" in flaw for flaw in flaws), output
            )
        if provenance:
            self.assertTrue(
                any("links with unknown provenance" in flaw for flaw in flaws),
                output,
            )

    def test_display_metadata_is_generated_but_legacy_output_is_unchanged(self):
        self.assertEqual(EXPECTED_ACTION_SHORT_NAMES, runelint.ACTS)
        self.assertEqual(EXPECTED_ACTION_NAMES, runeview.ACTION_NAMES)
        self.assertEqual(EXPECTED_ACTION_COLORS, runeview.ACTION_COLORS)
        self.assertEqual(EXPECTED_PROVENANCE_NAMES, runeview.PROVENANCE_NAMES)
        self.assertEqual(
            EXPECTED_ACTION_NAMES,
            {action: contract.ACTION_NAMES[action] for action in range(9)},
        )
        self.assertNotIn(contract.RL_DOOR_DROP, runeview.ACTION_NAMES)

    def test_v1_action7_is_structural_but_action8_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            accepted = _write_rune(
                temporary,
                1,
                _reciprocal_links(contract.RL_ROCKETJUMP, contract.RL_PROVEN),
                "v1_action7",
            )
            with contextlib.redirect_stderr(io.StringIO()):
                self.assert_all_readers_accept(
                    accepted, 1, contract.RL_ROCKETJUMP
                )

            rejected = _write_rune(
                temporary,
                1,
                _reciprocal_links(contract.RL_DOOR, contract.RL_DECLARED),
                "v1_action8",
            )
            with contextlib.redirect_stderr(io.StringIO()):
                self.assert_record_rejected(rejected, action=True)

    def test_v2_action8_reaches_normal_validation_but_action9_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            accepted = _write_rune(
                temporary,
                2,
                _reciprocal_links(
                    contract.RL_DOOR, contract.RL_DECLARED, door_controls=True
                ),
                "v2_action8",
            )
            self.assert_all_readers_accept(accepted, 2, contract.RL_DOOR)

            malformed = _write_rune(
                temporary,
                2,
                _reciprocal_links(
                    contract.RL_DOOR, contract.RL_PROVEN, door_controls=True
                ),
                "v2_action8_bad_control",
            )
            with self.assertRaisesRegex(ValueError, "invalid action anchor/control"):
                corpusgraph.read_rune(malformed)
            with self.assertRaisesRegex(ValueError, "invalid action anchor/control"):
                runeview.load_rune(malformed)
            flaws, output = _lint(malformed)
            self.assertFalse(
                any("links with unknown action" in flaw for flaw in flaws), output
            )
            self.assertIn("v2 links with invalid action anchor/control: 2", flaws)

            rejected = _write_rune(
                temporary,
                2,
                _reciprocal_links(contract.RL_DOOR_DROP, contract.RL_DECLARED),
                "v2_action9",
            )
            self.assert_record_rejected(rejected, action=True)

    def test_v3_header_is_rejected_even_if_corpusgraph_caller_requests_it(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = _write_rune(temporary, 3, (), "v3_header")
            with self.assertRaisesRegex(ValueError, "unsupported rune version 3"):
                corpusgraph.read_rune(path)
            with self.assertRaisesRegex(ValueError, "unsupported rune version 3"):
                corpusgraph.read_rune(path, versions=(1, 2, 3))
            with self.assertRaisesRegex(ValueError, "unsupported rune version 3"):
                runeview.load_rune(path)
            flaws, output = _lint(path)
            self.assertTrue(any("BAD VERSION 3" in flaw for flaw in flaws), output)

    def test_provenance4_is_rejected_by_both_legacy_versions(self):
        for version in (1, 2):
            with self.subTest(version=version), tempfile.TemporaryDirectory() as temporary:
                path = _write_rune(
                    temporary,
                    version,
                    _reciprocal_links(contract.RL_RUN, contract.RL_CONTRACTED),
                    f"v{version}_provenance4",
                )
                if version == 1:
                    with contextlib.redirect_stderr(io.StringIO()):
                        self.assert_record_rejected(path, provenance=True)
                else:
                    self.assert_record_rejected(path, provenance=True)


if __name__ == "__main__":
    unittest.main()
