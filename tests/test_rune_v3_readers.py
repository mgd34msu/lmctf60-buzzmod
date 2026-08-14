#!/usr/bin/env python3
"""Integration boundaries for the three primary Python RUNE readers."""

from __future__ import annotations

import contextlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import corpusgraph  # noqa: E402
import rune_contracts_generated as contract  # noqa: E402
import runeio  # noqa: E402
import runelint  # noqa: E402
import runeview  # noqa: E402


def _golden_bytes():
    text = (ROOT / "tests" / "fixtures" /
            "rune_v3_wire_golden.hex").read_text(encoding="ascii")
    return bytes.fromhex(text)


def _identity(map_name):
    return runeio.RuneIdentityV3(
        map_name=map_name,
        bsp_checksum=0x12345678,
        entity_crc32=0x9ABCDEF0,
        gravity=650.0,
        airaccelerate=0.0,
        maxvelocity=2000.0,
        host_physics_id=1,
    )


def _supported_graph(map_name):
    seeds = (
        runeio.RuneSeedV3((0.0, 0.0, 0.0)),
        runeio.RuneSeedV3((128.0, 0.0, 0.0)),
    )
    links = (
        runeio.RuneLinkV3(
            0, 1, contract.RL_RUN, contract.RL_PROVEN,
            0, 0, 0, 0, 100,
        ),
        runeio.RuneLinkV3(
            1, 0, contract.RL_RUN, contract.RL_PROVEN,
            0, 0, 0, 0, 100,
        ),
    )
    return runeio.encode_v3(_identity(map_name), seeds, links)


def _supported_action_graph(map_name, action, *, malformation=None):
    water = action == contract.RL_SWIM
    seeds = (
        runeio.RuneSeedV3(
            (0.0, 0.0, 0.0),
            flags=runeio.RSF_WATER if water else 0,
        ),
        runeio.RuneSeedV3((128.0, 0.0, 0.0)),
    )
    provenance = (contract.RL_DECLARED if action in (
        contract.RL_LIFT, contract.RL_TELEPORT, contract.RL_DOOR
    ) else contract.RL_PROVEN)
    heading_slack = {
        contract.RL_DROP: runelint.RUNE_DROP_CONTROL_MARKER,
        contract.RL_HOOK: runelint.RUNE_HOOK_CONTROL_SLACK,
        contract.RL_LIFT: runelint.RUNE_DECLARED_CONTROL_MARKER,
        contract.RL_TELEPORT: runelint.RUNE_DECLARED_CONTROL_MARKER,
        contract.RL_DOOR: runelint.RUNE_DECLARED_CONTROL_MARKER,
    }.get(action, 0)
    suffix_anchor = {
        contract.RL_RUN: (64.0, 0.0, 0.0),
        contract.RL_DROP: (64.0, 0.0, 8.0),
        contract.RL_HOOK: (0.0, 0.0, 128.0),
        contract.RL_LIFT: (64.0, 0.0, 0.0),
        contract.RL_TELEPORT: (64.0, 0.0, 0.0),
        contract.RL_DOOR: (64.0, 0.0, 0.0),
    }.get(action, (0.0, 0.0, 0.0))
    min_speed = 0
    heading = 0
    if malformation:
        if action == contract.RL_HOOK and malformation == "hook_control":
            suffix_anchor = (0.1, 0.0, 128.0)
        elif action in (contract.RL_JUMP, contract.RL_HOOK, contract.RL_LIFT):
            min_speed = 1
        elif action == contract.RL_DROP:
            heading_slack = 0
        elif action == contract.RL_SWIM:
            heading = 1
        elif action == contract.RL_TELEPORT:
            suffix_anchor = (256.0, 0.0, 0.0)
        elif action == contract.RL_DOOR:
            suffix_anchor = (400.0, 0.0, 0.0)
    first = runeio.RuneLinkV3(
        source=0,
        destination=1,
        action=action,
        provenance=provenance,
        min_speed=min_speed,
        heading=heading,
        heading_slack=heading_slack,
        exit_speed=0,
        cost_ms=100,
        suffix_anchor=suffix_anchor,
    )
    if water:
        reverse = runeio.RuneLinkV3(
            1, 0, contract.RL_SWIM, contract.RL_PROVEN,
            0, 0, 0, 0, 100,
        )
    else:
        reverse = runeio.RuneLinkV3(
            1, 0, contract.RL_RUN, contract.RL_PROVEN,
            0, 0, 0, 0, 100,
        )
    return runeio.encode_v3(_identity(map_name), seeds, (first, reverse))


def _legacy_graph(map_name):
    raw_name = map_name.encode("ascii") + b"\0"
    raw_name += b"\0" * (64 - len(raw_name))
    data = bytearray(struct.pack(
        corpusgraph.HEADER_FMT,
        corpusgraph.RUNE_MAGIC,
        2,
        2,
        2,
        raw_name,
    ))
    for origin in ((0.0, 0.0, 0.0), (128.0, 0.0, 0.0)):
        data.extend(struct.pack(corpusgraph.SEED_FMT, *origin, 0, 0))
    for source, destination in ((0, 1), (1, 0)):
        data.extend(struct.pack(
            corpusgraph.LINK_FMT,
            source,
            destination,
            contract.RL_RUN,
            contract.RL_PROVEN,
            0,
            0,
            0,
            0,
            100,
            0.0,
            0.0,
            0.0,
        ))
    return bytes(data)


def _disabled_graph(map_name, action):
    if action in (contract.RL_DOOR_SWIM, contract.RL_DOOR_HOOK):
        seeds = (
            runeio.RuneSeedV3(
                (0.0, 0.0, 0.0), flags=runeio.RSF_WATER
            ),
            runeio.RuneSeedV3((128.0, 0.0, 0.0)),
        )
        suffix_anchor = ((0.0, 0.0, 0.0)
                         if action == contract.RL_DOOR_SWIM
                         else (0.0, 0.0, 128.0))
        first = runeio.RuneLinkV3(
            0, 1, action, contract.RL_CONTRACTED,
            0, 0, 0, 0, 500, suffix_anchor, (64.0, 0.0, 0.0),
            100, contract.RLCM_PREOPEN,
        )
        second = runeio.RuneLinkV3(
            1, 0, contract.RL_SWIM, contract.RL_PROVEN,
            0, 0, 0, 0, 100,
        )
    else:
        seeds = (
            runeio.RuneSeedV3((0.0, 0.0, 0.0)),
            runeio.RuneSeedV3((128.0, 0.0, 0.0)),
        )
        if action == contract.RL_ROCKETJUMP:
            first = runeio.RuneLinkV3(
                0, 1, action, contract.RL_PROVEN,
                0, 0, 0, 0, 500,
            )
        else:
            first = runeio.RuneLinkV3(
                0, 1, action, contract.RL_CONTRACTED,
                0, 0, 0, 0, 500,
                (64.0, 0.0, 8.0), (64.0, 0.0, 0.0),
                100, contract.RLCM_PREOPEN,
            )
        second = runeio.RuneLinkV3(
            1, 0, contract.RL_RUN, contract.RL_PROVEN,
            0, 0, 0, 0, 100,
        )
    return runeio.encode_v3(_identity(map_name), seeds, (first, second))


def _lint(path, **kwargs):
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        flaws = runelint.lint(path, **kwargs)
    return flaws, output.getvalue()


class RuneV3ReaderTests(unittest.TestCase):
    def assert_all_v3_readers_reject(self, path, symbol):
        with self.assertRaisesRegex(ValueError, symbol):
            corpusgraph.read_rune(path, versions=(3,))
        with self.assertRaisesRegex(ValueError, symbol):
            runeview.load_rune(path)
        flaws, output = _lint(path, runtime_v3=True)
        self.assertTrue(any(symbol in flaw for flaw in flaws), output)

    def test_golden_is_inspectable_and_surfaces_header_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "lmctf07.rune"
            path.write_bytes(_golden_bytes())

            with self.assertRaisesRegex(ValueError, "unsupported rune version 3"):
                corpusgraph.read_rune(path)
            graph = corpusgraph.read_rune(path, versions=(1, 2, 3))
            self.assertEqual(3, graph["version"])
            self.assertEqual(0x12345678, graph["bsp_checksum"])
            self.assertEqual(0x9ABCDEF0, graph["entity_crc32"])
            self.assertEqual(contract.CONTRACT_CRC32,
                             graph["action_contract_crc32"])
            self.assertEqual(650.0, graph["physics"]["gravity"])
            self.assertEqual(1, graph["physics"]["host_physics_id"])
            self.assertEqual(_golden_bytes(), graph["data"])

            identity = corpusgraph.rune_identity(path, "lmctf07")
            self.assertEqual(3, identity["rune_version"])
            self.assertEqual(graph["payload_crc32"],
                             identity["rune_payload_crc32"])

            viewed = runeview.load_rune(path)
            self.assertEqual("lmctf07", viewed.header.map_name)
            self.assertEqual(contract.RL_DOOR_DROP,
                             viewed.links[1]["action"])
            self.assertEqual((112.0, 0.0, 0.0),
                             viewed.links[1]["mechanism_anchor"])

            loaded = runelint.load(path)
            self.assertEqual(8, len(loaded))
            flaws, output = _lint(path)
            self.assertEqual([], flaws, output)
            self.assertIn("D_DROP=1", output)
            self.assertIn("bsp=0x12345678", output)
            self.assertIn("entity=0x9abcdef0", output)
            self.assertIn("action_contract=0x769a7b8e", output)
            self.assertIn("gravity=650", output)

    def test_format_probe_and_decode_use_one_atomic_snapshot(self):
        readers = (
            (
                "corpusgraph",
                lambda path: corpusgraph.read_rune(
                    path, versions=(1, 2, 3)
                ),
                lambda result: result["version"],
            ),
            ("runelint", runelint.load, lambda result: result[1]),
            ("runeview", runeview.load_rune, lambda result: result.version),
        )
        formats = (
            ("legacy_to_v3", _legacy_graph("atomiccase"),
             _supported_graph("atomiccase"), 2),
            ("v3_to_legacy", _supported_graph("atomiccase"),
             _legacy_graph("atomiccase"), 3),
        )
        original_probe = runeio.looks_like_v3_prefix

        for reader_name, reader, version_of in readers:
            for direction, initial, replacement, expected_version in formats:
                with self.subTest(reader=reader_name, direction=direction), \
                        tempfile.TemporaryDirectory() as temporary:
                    path = Path(temporary) / "atomiccase.rune"
                    replacement_path = Path(temporary) / "replacement.rune"
                    path.write_bytes(initial)
                    replacement_path.write_bytes(replacement)
                    swapped = False

                    def swap_after_probe(prefix):
                        nonlocal swapped
                        result = original_probe(prefix)
                        if not swapped:
                            replacement_path.replace(path)
                            swapped = True
                        return result

                    with mock.patch.object(
                            runeio, "looks_like_v3_prefix",
                            side_effect=swap_after_probe):
                        decoded = reader(path)
                    self.assertTrue(swapped)
                    self.assertEqual(expected_version, version_of(decoded))

    def test_runtime_v3_accepts_only_supported_outer_actions(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "runtimecase.rune"
            path.write_bytes(_supported_graph("runtimecase"))
            flaws, output = _lint(
                path, runtime_v3=True, objective_root_indices=(0, 1)
            )
            self.assertEqual([], flaws, output)
            self.assertIn("clean", output)
            cli_output = io.StringIO()
            with contextlib.redirect_stdout(cli_output):
                status = runelint.main([
                    "--runtime-v3", "--objective-roots", "0", "1",
                    str(path),
                ])
            self.assertEqual(0, status, cli_output.getvalue())
            self.assertIn("TOTAL FLAWS: 0", cli_output.getvalue())

            disabled = (
                contract.RL_ROCKETJUMP,
                contract.RL_DOOR_DROP,
                contract.RL_DOOR_SWIM,
                contract.RL_DOOR_HOOK,
            )
            for action in disabled:
                map_name = f"disabled{action}"
                path = Path(temporary) / f"{map_name}.rune"
                path.write_bytes(_disabled_graph(map_name, action))
                inspection_flaws, inspection_output = _lint(path)
                flaws, output = _lint(
                    path, runtime_v3=True, objective_root_indices=(0, 1)
                )
                with self.subTest(action=action):
                    self.assertEqual([], inspection_flaws, inspection_output)
                    self.assertEqual(
                        ["runtime v3 unsupported actions: "
                         f"{contract.ACTION_SHORT_NAMES[action]}=1"],
                        flaws,
                        output,
                    )
                    self.assertIn(contract.ACTION_SHORT_NAMES[action], output)

    def test_runtime_v3_enforces_supported_controller_laws(self):
        malformed = (
            (contract.RL_JUMP, "controller",
             "v3 jumps with unsupported momentum envelope: 1"),
            (contract.RL_DROP, "controller",
             "v3 drops with invalid lip control: 1"),
            (contract.RL_HOOK, "controller",
             "v3 hooks with invalid provenance, speed, marker, or wet "
             "destination: 1"),
            (contract.RL_HOOK, "hook_control",
             "v3 hook controls with non-canonical pitch: 1"),
            (contract.RL_SWIM, "controller",
             "v3 swims with malformed exact control: 1"),
            (contract.RL_LIFT, "controller",
             "v3 links with invalid action anchor/control: 1"),
            (contract.RL_TELEPORT, "reach",
             "v3 links with invalid action anchor/control: 1"),
            (contract.RL_DOOR, "reach",
             "v3 links with invalid action anchor/control: 1"),
        )
        supported = tuple(range(contract.RL_RUN, contract.RL_TELEPORT + 1)) + (
            contract.RL_DOOR,
        )
        with tempfile.TemporaryDirectory() as temporary:
            for action in supported:
                map_name = f"validlaw{action}"
                path = Path(temporary) / f"{map_name}.rune"
                path.write_bytes(_supported_action_graph(map_name, action))
                flaws, output = _lint(
                    path, runtime_v3=True, objective_root_indices=(0, 1)
                )
                with self.subTest(valid_action=action):
                    self.assertEqual([], flaws, output)

            for index, (action, malformation, expected) in enumerate(malformed):
                map_name = f"badlaw{action}_{index}"
                path = Path(temporary) / f"{map_name}.rune"
                path.write_bytes(_supported_action_graph(
                    map_name, action, malformation=malformation
                ))
                inspection_flaws, inspection_output = _lint(path)
                flaws, output = _lint(
                    path, runtime_v3=True, objective_root_indices=(0, 1)
                )
                with self.subTest(malformed_action=action):
                    self.assertEqual([], inspection_flaws, inspection_output)
                    self.assertEqual([expected], flaws, output)

    def test_runtime_v3_rechecks_dry_controller_endpoints(self):
        seeds = [
            (0.0, 0.0, 0.0, 0, runeio.RSF_WATER),
            (128.0, 0.0, 0.0, 0, 0),
        ]
        for action in (contract.RL_RUN, contract.RL_JUMP, contract.RL_DOOR):
            provenance = (contract.RL_DECLARED if action == contract.RL_DOOR
                          else contract.RL_PROVEN)
            marker = (runelint.RUNE_DECLARED_CONTROL_MARKER
                      if action == contract.RL_DOOR else 0)
            suffix_anchor = ((0.0, 0.0, 0.0)
                             if action == contract.RL_JUMP
                             else (64.0, 0.0, 0.0))
            links = [
                (0, 1, action, provenance, 0, 0, marker, 0, 100,
                 *suffix_anchor),
                (1, 0, contract.RL_SWIM, contract.RL_PROVEN,
                 0, 0, 0, 0, 100, 0.0, 0.0, 0.0),
            ]
            loaded = (
                runelint.RUNE_MAGIC, contract.RUNE_V3_VERSION,
                "drycase", 2, 2, seeds, links, [],
            )
            with self.subTest(action=action), mock.patch.object(
                    runelint, "_load_with_metadata",
                    return_value=(loaded, None)):
                flaws, output = _lint(
                    "drycase.rune", runtime_v3=True,
                    objective_root_indices=(0, 1),
                )
                self.assertIn(
                    "v3 RUN/JUMP/DOOR links with water endpoint: 1",
                    flaws,
                    output,
                )

    def test_corruption_and_exact_map_identity_fail_in_all_readers(self):
        with tempfile.TemporaryDirectory() as temporary:
            # Before byte 8, a legacy int32-version-3 prefix and a v3 prefix
            # are byte-identical.  Once a fixed v3 size field is present, all
            # recognizable v3 truncations must take the runeio path.
            for length in (8, 12, 79, 80, 127):
                with self.subTest(truncated_v3_length=length):
                    truncated_path = Path(temporary) / "lmctf07.rune"
                    truncated_path.write_bytes(_golden_bytes()[:length])
                    self.assert_all_v3_readers_reject(
                        truncated_path, "RLW_BAD_FILE_SIZE"
                    )

            bad_header = bytearray(_golden_bytes())
            bad_header[6:8] = b"\x00\x00"
            header_path = Path(temporary) / "lmctf07.rune"
            header_path.write_bytes(bad_header)
            self.assert_all_v3_readers_reject(
                header_path, "RLW_BAD_HEADER_SIZE"
            )

            for length in (80, 127):
                with self.subTest(zero_header_size_truncated=length):
                    header_path.write_bytes(bad_header[:length])
                    self.assert_all_v3_readers_reject(
                        header_path, "RLW_BAD_FILE_SIZE"
                    )

            bad_version_and_size = bytearray(bad_header)
            bad_version_and_size[4:6] = b"\x02\x00"
            header_path.write_bytes(bad_version_and_size)
            self.assert_all_v3_readers_reject(
                header_path, "RLW_UNSUPPORTED_VERSION"
            )

            bad_header_and_seed_size = bytearray(bad_header)
            bad_header_and_seed_size[8:10] = b"\x0f\x00"
            header_path.write_bytes(bad_header_and_seed_size)
            self.assert_all_v3_readers_reject(
                header_path, "RLW_BAD_HEADER_SIZE"
            )

            oversized = bytearray(_golden_bytes())
            oversized[0] ^= 1
            oversized.extend(
                b"\0" * (runeio.MAX_V3_FILE_BYTES + 2 - len(oversized))
            )
            header_path.write_bytes(oversized)
            self.assert_all_v3_readers_reject(
                header_path, "RLW_BAD_FILE_SIZE"
            )

            corrupt = bytearray(_golden_bytes())
            corrupt[-1] ^= 1
            corrupt_path = Path(temporary) / "lmctf07.rune"
            corrupt_path.write_bytes(corrupt)
            with self.assertRaisesRegex(ValueError, "RLW_BAD_PAYLOAD_CRC"):
                corpusgraph.read_rune(corrupt_path, versions=(3,))
            with self.assertRaisesRegex(ValueError, "RLW_BAD_PAYLOAD_CRC"):
                runeview.load_rune(corrupt_path)
            flaws, output = _lint(corrupt_path)
            self.assertTrue(any("RLW_BAD_PAYLOAD_CRC" in flaw
                                for flaw in flaws), output)

            mismatch = Path(temporary) / "LMCTF07.rune"
            mismatch.write_bytes(_golden_bytes())
            with self.assertRaisesRegex(ValueError, "RLW_MAPNAME_MISMATCH"):
                corpusgraph.read_rune(
                    mismatch, expected_map="LMCTF07", versions=(3,)
                )
            with self.assertRaisesRegex(ValueError, "RLW_MAPNAME_MISMATCH"):
                runeview.load_rune(mismatch)
            flaws, output = _lint(mismatch)
            self.assertTrue(any("RLW_MAPNAME_MISMATCH" in flaw
                                for flaw in flaws), output)


if __name__ == "__main__":
    unittest.main()
