from __future__ import annotations

from hashlib import sha256
from pathlib import Path
from types import SimpleNamespace
import json
import math
import tempfile
import unittest

from tools import humantrace, runelearn


def fake_rune():
    header = SimpleNamespace(
        magic=0x454E5552,
        route_contract=1,
        map_name="lmctf01",
        bsp_checksum=123,
        entity_crc32=456,
        action_contract_crc32=55,
        physics_flags=0,
        gravity=800.0,
        airaccelerate=0.0,
        maxvelocity=2000.0,
        pmove_substep_ms=25,
        server_frame_ms=100,
        host_physics_id=1,
        payload_crc32=33,
        header_crc32=44,
        mechanism_contract_crc32=66,
        num_seeds=4,
        num_links=1,
        num_activation_nodes=0,
        num_activation_edges=0,
        num_inventory_edges=0,
        num_activation_plans=0,
        string_bytes=1,
    )
    seeds = (
        SimpleNamespace(origin=(0.0, 0.0, 0.0), flags=4),
        SimpleNamespace(origin=(128.0, 0.0, 0.0), flags=2),
        SimpleNamespace(origin=(256.0, 0.0, 0.0), flags=2),
        SimpleNamespace(origin=(384.0, 0.0, 0.0), flags=4),
    )
    links = (SimpleNamespace(source=2, destination=3),)
    return SimpleNamespace(header=header, seeds=seeds, links=links)


def state(origin: list[int], *, flags: int = 4) -> dict[str, object]:
    return {
        "type": 0,
        "origin": origin,
        "velocity": [80, 0, 0],
        "flags": flags,
        "time": 0,
        "gravity": 800,
        "delta_angles": [0, 0, 0],
    }


def step(seq: int, before: list[int], after: list[int], *,
         flags: int = 4, waterlevel: int = 0,
         touches: list[int] | None = None) -> dict[str, object]:
    return {
        "seq": seq,
        "client": 1,
        "frame": seq,
        "snapinitial": 0,
        "cmd": {
            "msec": 25, "buttons": 0, "angles": [0, 0, 0],
            "forward": 400, "side": 0, "up": 0, "impulse": 0,
            "light": 0,
        },
        "before": state(before, flags=flags),
        "after": state(after, flags=flags),
        "ground": 0,
        "waterlevel": waterlevel,
        "watertype": 0,
        "touches": [0] if touches is None else touches,
    }


def binding(source_sha: str) -> dict[str, object]:
    header = fake_rune().header
    return {
        "map": header.map_name,
        "bsp_checksum": header.bsp_checksum,
        "entity_crc32": header.entity_crc32,
        "physics_flags": header.physics_flags,
        "gravity": header.gravity,
        "airaccelerate": header.airaccelerate,
        "maxvelocity": header.maxvelocity,
        "pmove_substep_ms": header.pmove_substep_ms,
        "server_frame_ms": header.server_frame_ms,
        "host_physics_id": header.host_physics_id,
        "route_contract": header.route_contract,
        "payload_crc32": header.payload_crc32,
        "header_crc32": header.header_crc32,
        "action_contract_crc32": header.action_contract_crc32,
        "mechanism_contract_crc32": header.mechanism_contract_crc32,
        "num_seeds": header.num_seeds,
        "num_links": header.num_links,
        "num_mechanism_nodes": header.num_activation_nodes,
        "num_mechanism_edges": header.num_activation_edges,
        "num_inventory_edges": header.num_inventory_edges,
        "num_mechanism_plans": header.num_activation_plans,
        "string_bytes": header.string_bytes,
        "rune_sha256": source_sha,
        "start_sequence": 1,
        "frame": 0,
    }


def replay(source_sha: str) -> dict[str, object]:
    steps = [
        step(1, [0, 0, 0], [256, 256, 0]),
        step(2, [256, 256, 0], [512, 512, 0]),
        step(3, [512, 512, 0], [768, 256, 0]),
        step(4, [768, 256, 0], [1024, 0, 0]),
    ]
    return {
        "format": humantrace.EVIDENCE_FORMAT,
        "identity": {
            "map": "lmctf01", "bsp_checksum": 123,
            "entity_crc32": 456, "physics_id": 1,
            "module_revision": 1, "module_version": "test",
        },
        "source": {
            "basename": "humantrace-lmctf01.jsonl",
            "sha256": "a" * 64,
            "session": 1,
        },
        "client": 1,
        "frame_window": [1, 4],
        "rune_bindings": [binding(source_sha)],
        "segments": [{
            "start_index": 0, "end_exclusive": 4,
            "reason": "trace-start",
        }],
        "steps": steps,
    }


def pull_velocity(origin: tuple[int, int, int],
                  bite: tuple[int, int, int]) -> list[int]:
    delta = [bite[axis] - origin[axis] for axis in range(3)]
    length = math.sqrt(sum(value * value for value in delta))
    return [int(value * 6400.0 / length) for value in delta]


def legacy_step(seq: int, frame: int, before: tuple[int, int, int],
                after: tuple[int, int, int],
                before_velocity: list[int], *, grounded: bool = False
                ) -> dict[str, object]:
    value = step(seq, list(before), list(after),
                 flags=PMF_ON_GROUND if grounded else 0,
                 touches=[])
    value["frame"] = frame
    value["before"]["velocity"] = before_velocity
    value["after"]["velocity"] = [0, 0, 0]
    return value


PMF_ON_GROUND = 4


def legacy_journey(bites: tuple[tuple[int, int, int], ...],
                   *, frame_stride: int = 1) -> list[dict[str, object]]:
    values = [
        legacy_step(1, 1, (0, 0, 0), (0, 0, 0), [0, 0, 0],
                    grounded=True),
        legacy_step(2, 2, (0, 0, 0), (0, 0, 0), [0, 0, 0],
                    grounded=True),
    ]
    sequence = 3
    frame = 2
    position = (0, 0, 0)
    for rope, bite in enumerate(bites):
        if rope:
            frame += frame_stride
            next_position = (position[0] + 8, position[1], position[2])
            values.append(legacy_step(
                sequence, frame, position, next_position, [100, 0, 0]))
            sequence += 1
            position = next_position
        for sample in range(3):
            frame += 1
            y_step = (128, -256, 64)[sample]
            next_position = (
                position[0] + 64,
                position[1] + y_step,
                position[2],
            )
            values.append(legacy_step(
                sequence, frame, position, next_position,
                pull_velocity(position, bite)))
            sequence += 1
            position = next_position
    frame += 1
    values.append(legacy_step(
        sequence, frame, position, (1024, 0, 0), [200, 0, 0],
        grounded=True))
    values.append(legacy_step(
        sequence + 1, frame + 1, (1024, 0, 0), (1024, 0, 0),
        [0, 0, 0], grounded=True))
    return values


class RuneLearnTest(unittest.TestCase):

    def test_legacy_pull_detection_requires_exact_post_pmove_pull_law(self
                                                                      ) -> None:
        bite = (4096, 2048, 512)
        previous = legacy_step(
            1, 10, (0, 0, 0), (256, 0, 0), [0, 0, 0])
        current = legacy_step(
            2, 11, (256, 0, 0), (320, 0, 0),
            pull_velocity((256, 0, 0), bite))
        current["snapinitial"] = 1

        sample = runelearn._legacy_pull_sample(previous, current, 1)

        self.assertIsNotNone(sample)
        self.assertEqual(sample.origin_q8, (256, 0, 0))
        ordinary = json.loads(json.dumps(current))
        ordinary["before"]["velocity"] = [4000, 0, 0]
        self.assertIsNone(
            runelearn._legacy_pull_sample(previous, ordinary, 1))
        displaced = json.loads(json.dumps(current))
        displaced["before"]["origin"] = [257, 0, 0]
        self.assertIsNone(
            runelearn._legacy_pull_sample(previous, displaced, 1))

    def test_legacy_pull_group_triangulates_and_rejects_bad_rays(self) -> None:
        bite = (4096, 2048, 512)
        values = legacy_journey((bite,))

        groups = runelearn._legacy_pull_groups(values)

        self.assertEqual(len(groups), 1)
        self.assertLessEqual(max(abs(groups[0].bite_q8[axis] - bite[axis])
                                 for axis in range(3)), 8)
        bad_residual = json.loads(json.dumps(values))
        bad_residual[3]["before"]["velocity"] = [0, 6400, 0]
        self.assertEqual(runelearn._legacy_pull_groups(bad_residual), ())
        backward = json.loads(json.dumps(values))
        for value in backward[2:5]:
            value["before"]["velocity"] = [
                -component for component in value["before"]["velocity"]]
        self.assertEqual(runelearn._legacy_pull_groups(backward), ())

    def test_legacy_hook_nominations_are_bounded_to_one_or_two_ropes(
            self) -> None:
        source_sha = "b" * 64
        exact = binding(source_sha)
        exact["provenance"] = "posthoc-identity-exact"
        first_bite = (4096, 2048, 512)
        second_bite = (6144, -1024, 1024)

        one = runelearn.build_legacy_hook_candidates(
            fake_rune(), legacy_journey((first_bite,)), exact,
            humantrace.EVIDENCE_FORMAT_V1)
        two = runelearn.build_legacy_hook_candidates(
            fake_rune(), legacy_journey((first_bite, second_bite)), exact,
            humantrace.EVIDENCE_FORMAT_V1)
        three = runelearn.build_legacy_hook_candidates(
            fake_rune(), legacy_journey(
                (first_bite, second_bite, (8192, 2048, 1536))), exact,
            humantrace.EVIDENCE_FORMAT_V1)

        self.assertEqual(len(one), 1)
        self.assertEqual((one[0].source_from, one[0].source_to,
                          one[0].rope_count), (0, 1, 1))
        self.assertEqual(one[0].bite_q8[1], (0, 0, 0))
        self.assertEqual(len(two), 1)
        self.assertEqual(two[0].rope_count, 2)
        for recovered, expected in zip(two[0].bite_q8,
                                       (first_bite, second_bite)):
            self.assertLessEqual(max(abs(recovered[axis] - expected[axis])
                                     for axis in range(3)), 8)
        self.assertEqual(three, ())
        too_long = legacy_journey((first_bite,))
        too_long[-2]["frame"] += 400
        too_long[-1]["frame"] = too_long[-2]["frame"] + 1
        self.assertEqual(runelearn.build_legacy_hook_candidates(
            fake_rune(), too_long, exact,
            humantrace.EVIDENCE_FORMAT_V1), ())

    def test_legacy_hook_fallback_requires_v1_posthoc_identity(self) -> None:
        source_sha = "b" * 64
        exact = binding(source_sha)
        exact["provenance"] = "posthoc-identity-exact"
        values = legacy_journey(((4096, 2048, 512),))

        with self.assertRaisesRegex(ValueError, "v1"):
            runelearn.build_legacy_hook_candidates(
                fake_rune(), values, exact, humantrace.EVIDENCE_FORMAT_V2)
        exact.pop("provenance")
        with self.assertRaisesRegex(ValueError, "posthoc"):
            runelearn.build_legacy_hook_candidates(
                fake_rune(), values, exact, humantrace.EVIDENCE_FORMAT_V1)

    def test_legacy_learning_renders_hook_nominations_only(self) -> None:
        source_sha = "b" * 64
        payload = replay(source_sha)
        payload["steps"] = legacy_journey(((4096, 2048, 512),))
        payload["frame_window"] = [1, payload["steps"][-1]["frame"]]
        payload["segments"] = humantrace.replay_segments(payload["steps"])
        payload["rune_bindings"][0]["provenance"] = \
            "posthoc-identity-exact"

        text, hooks = runelearn.build_legacy_learning(
            fake_rune(), source_sha, payload, "c" * 64)

        self.assertEqual(len(hooks), 1)
        self.assertIn("candidates 0\n", text)
        self.assertIn("hook_candidates 1\n", text)
        payload["rune_bindings"][0]["rune_sha256"] = "d" * 64
        with self.assertRaisesRegex(ValueError, "binding"):
            runelearn.build_legacy_learning(
                fake_rune(), source_sha, payload, "c" * 64)

    def test_combined_legacy_learning_renders_dry_and_hook_nominations(
            self) -> None:
        source_sha = "b" * 64
        payload = replay(source_sha)
        payload["steps"] = legacy_journey(((4096, 2048, 512),))
        payload["frame_window"] = [1, payload["steps"][-1]["frame"]]
        payload["segments"] = humantrace.replay_segments(payload["steps"])
        payload["rune_bindings"][0]["provenance"] = \
            "posthoc-identity-exact"

        first = runelearn.build_combined_legacy_learning(
            fake_rune(), source_sha, payload, "c" * 64)
        second = runelearn.build_combined_legacy_learning(
            fake_rune(), source_sha, payload, "c" * 64)

        self.assertEqual(first, second)
        text, candidates, hooks = first
        self.assertEqual(len(candidates), 1)
        self.assertEqual(len(hooks), 1)
        self.assertIn("candidates 1\n", text)
        self.assertIn("hook_candidates 1\n", text)
        self.assertLess(text.index("candidate "),
                        text.index("hook_candidate "))

        payload["format"] = humantrace.EVIDENCE_FORMAT_V2
        payload["hook_events"] = []
        payload["rune_bindings"][0]["start_hook_event"] = 1
        with self.assertRaisesRegex(ValueError, "v1"):
            runelearn.build_combined_legacy_learning(
                fake_rune(), source_sha, payload, "c" * 64)

        payload["format"] = humantrace.EVIDENCE_FORMAT_V1
        payload.pop("hook_events")
        payload["rune_bindings"][0].pop("start_hook_event")
        payload["rune_bindings"][0].pop("provenance")
        with self.assertRaisesRegex(ValueError, "posthoc"):
            runelearn.build_combined_legacy_learning(
                fake_rune(), source_sha, payload, "c" * 64)

        payload["rune_bindings"][0]["provenance"] = \
            "posthoc-identity-exact"
        payload["rune_bindings"][0]["rune_sha256"] = "d" * 64
        with self.assertRaisesRegex(ValueError, "binding"):
            runelearn.build_combined_legacy_learning(
                fake_rune(), source_sha, payload, "c" * 64)

    def test_builds_one_deterministic_dry_run_waypoint_candidate(self) -> None:
        source_sha = "b" * 64
        payload = replay(source_sha)
        first = runelearn.build_learning(
            fake_rune(), source_sha, payload, "c" * 64)
        second = runelearn.build_learning(
            fake_rune(), source_sha, payload, "c" * 64)

        self.assertEqual(first, second)
        text, candidates = first
        self.assertEqual(len(candidates), 1)
        candidate = candidates[0]
        self.assertEqual((candidate.source_from, candidate.source_to), (0, 1))
        self.assertEqual(candidate.waypoint_q8, (512, 512, 0))
        self.assertTrue(candidate.has_waypoint)
        self.assertIn("rlearn_format 2\n", text)
        self.assertIn("candidates 1\n", text)
        self.assertIn("candidate 0 0 0 0 1 1024 0 0 1 1 512 512 0 1 3\n",
                      text)

    def test_unbound_or_mismatched_source_is_rejected(self) -> None:
        source_sha = "b" * 64
        payload = replay(source_sha)
        payload["rune_bindings"] = []
        with self.assertRaisesRegex(ValueError, "LOCAL_ONLY rune binding"):
            runelearn.build_learning(
                fake_rune(), source_sha, payload, "c" * 64)

    def test_recovers_unbound_replay_only_from_exact_identity(self) -> None:
        source_sha = "b" * 64
        payload = replay(source_sha)
        payload["rune_bindings"] = []

        recovered = runelearn.recover_replay(
            fake_rune(), source_sha, payload)

        self.assertEqual(payload["rune_bindings"], [])
        self.assertEqual(len(recovered["rune_bindings"]), 1)
        recovered_binding = recovered["rune_bindings"][0]
        self.assertEqual(recovered_binding["rune_sha256"], source_sha)
        self.assertEqual(recovered_binding["start_sequence"], 1)
        self.assertEqual(
            recovered_binding["provenance"], "posthoc-identity-exact")
        text, candidates = runelearn.build_learning(
            fake_rune(), source_sha, recovered, "c" * 64)
        self.assertEqual(len(candidates), 1)
        self.assertIn(f"rune_sha256 {source_sha}\n", text)

        mismatched = replay(source_sha)
        mismatched["rune_bindings"] = []
        mismatched["identity"]["bsp_checksum"] += 1
        with self.assertRaisesRegex(ValueError, "identity does not match"):
            runelearn.recover_replay(fake_rune(), source_sha, mismatched)

        payload = replay(source_sha)
        payload["rune_bindings"][0]["payload_crc32"] = 999
        with self.assertRaisesRegex(ValueError, "binding does not match"):
            runelearn.build_learning(
                fake_rune(), source_sha, payload, "c" * 64)

    def test_hook_water_and_mover_shaped_steps_nominate_nothing(self) -> None:
        source_sha = "b" * 64
        variants = (
            {"flags": 4 | 64},
            {"waterlevel": 1},
            {"touches": [0, 37]},
        )
        for replacement in variants:
            payload = replay(source_sha)
            payload["steps"] = [
                step(index + 1, value["before"]["origin"],
                     value["after"]["origin"], **replacement)
                for index, value in enumerate(payload["steps"])
            ]
            text, candidates = runelearn.build_learning(
                fake_rune(), source_sha, payload, "c" * 64)
            self.assertEqual(candidates, (), replacement)
            self.assertIn("candidates 0\n", text)

    def test_rejects_unbounded_touch_shape(self) -> None:
        source_sha = "b" * 64
        payload = replay(source_sha)
        payload["steps"][0]["touches"] = [0] * 33

        with self.assertRaisesRegex(ValueError, "at most 32"):
            runelearn.build_learning(
                fake_rune(), source_sha, payload, "c" * 64)

    def test_cli_writes_atomically_and_binds_replay_hash(self) -> None:
        artifact = fake_rune()
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            rune_path = directory / "source.rune"
            replay_path = directory / "replay.json"
            output = directory / "map.rlearn"
            rune_path.write_bytes(b"exact source rune bytes")
            source_sha = sha256(rune_path.read_bytes()).hexdigest()
            replay_value = replay(source_sha)
            replay_path.write_text(
                json.dumps(replay_value, sort_keys=True) + "\n",
                encoding="utf-8")
            expected_replay_sha = sha256(replay_path.read_bytes()).hexdigest()

            original = runelearn.runeio.decode_rune
            try:
                runelearn.runeio.decode_rune = lambda _: artifact
                count = runelearn.build_file(rune_path, replay_path, output)
            finally:
                runelearn.runeio.decode_rune = original

            result = output.read_text(encoding="ascii")

        self.assertEqual(count, 1)
        self.assertIn(f"rune_sha256 {source_sha}\n", result)
        self.assertIn(f"replay_sha256 {expected_replay_sha}\n", result)

    def test_combined_legacy_file_writes_one_source_bound_artifact(
            self) -> None:
        artifact = fake_rune()
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            rune_path = directory / "source.rune"
            replay_path = directory / "recovered-v1.json"
            output = directory / "combined.rlearn"
            rune_path.write_bytes(b"exact source rune bytes")
            source_sha = sha256(rune_path.read_bytes()).hexdigest()
            replay_value = replay(source_sha)
            replay_value["steps"] = legacy_journey(
                ((4096, 2048, 512),))
            replay_value["frame_window"] = [
                1, replay_value["steps"][-1]["frame"]]
            replay_value["segments"] = humantrace.replay_segments(
                replay_value["steps"])
            replay_value["rune_bindings"][0]["provenance"] = \
                "posthoc-identity-exact"
            replay_path.write_text(
                json.dumps(replay_value, sort_keys=True) + "\n",
                encoding="utf-8")
            expected_replay_sha = sha256(
                replay_path.read_bytes()).hexdigest()

            original = runelearn.runeio.decode_rune
            try:
                runelearn.runeio.decode_rune = lambda _: artifact
                count = runelearn.build_combined_legacy_file(
                    rune_path, replay_path, output)
            finally:
                runelearn.runeio.decode_rune = original

            result = output.read_text(encoding="ascii")

        self.assertEqual(count, 2)
        self.assertIn(f"rune_sha256 {source_sha}\n", result)
        self.assertIn(f"replay_sha256 {expected_replay_sha}\n", result)
        self.assertIn("candidates 1\n", result)
        self.assertIn("hook_candidates 1\n", result)

    def test_recover_file_writes_auditable_source_bound_replay(self) -> None:
        artifact = fake_rune()
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            rune_path = directory / "source.rune"
            replay_path = directory / "legacy.json"
            output = directory / "recovered.json"
            rune_path.write_bytes(b"exact source rune bytes")
            source_sha = sha256(rune_path.read_bytes()).hexdigest()
            replay_value = replay(source_sha)
            replay_value["rune_bindings"] = []
            replay_path.write_text(
                json.dumps(replay_value, sort_keys=True) + "\n",
                encoding="utf-8")

            original = runelearn.runeio.decode_rune
            try:
                runelearn.runeio.decode_rune = lambda _: artifact
                runelearn.recover_file(rune_path, replay_path, output)
            finally:
                runelearn.runeio.decode_rune = original

            recovered = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(
            recovered["rune_bindings"][0]["rune_sha256"], source_sha)
        self.assertEqual(
            recovered["rune_bindings"][0]["provenance"],
            "posthoc-identity-exact")

    def test_builds_one_and_two_rope_world_bite_nominations(self) -> None:
        source_sha = "b" * 64
        payload = replay(source_sha)
        payload["format"] = humantrace.EVIDENCE_FORMAT_V2
        payload["rune_bindings"][0]["start_hook_event"] = 1
        payload["steps"] = [
            step(1, [0, 0, 0], [0, 0, 0]),
            step(2, [0, 0, 0], [256, 0, 128], flags=0, touches=[]),
            step(3, [256, 0, 128], [768, 0, 128], flags=0, touches=[]),
            step(4, [768, 0, 128], [1024, 0, 0]),
            step(5, [1024, 0, 0], [1024, 0, 0]),
        ]
        payload["steps"][1]["before"] = payload["steps"][0]["after"]
        payload["steps"][3]["before"] = payload["steps"][2]["after"]
        payload["segments"] = [{
            "start_index": 0, "end_exclusive": 5,
            "reason": "trace-start",
        }]
        fire = {
            "kind": "hook-fire", "event": 1, "after_step": 1,
            "client": 1, "frame": 1, "hook": 10,
            "origin_q8": [0, 0, 0], "velocity_q8": [80, 0, 0],
            "view_short": [-2048, 8192], "hand": 0,
        }
        attach = {
            "kind": "hook-attach", "event": 2, "after_step": 1,
            "client": 1, "frame": 1, "hook": 10,
            "bite_q8": [0, 1024, 512], "target": 0, "world": 1,
        }
        release = {
            "kind": "hook-release", "event": 3, "after_step": 2,
            "client": 1, "frame": 2, "hook": 10,
            "origin_q8": [256, 0, 128], "velocity_q8": [600, 0, 200],
        }
        payload["hook_events"] = [fire, attach, release]

        one = runelearn.build_hook_candidates(
            fake_rune(), payload["steps"], payload["hook_events"],
            payload["rune_bindings"][0])
        self.assertEqual(len(one), 1)
        self.assertEqual(one[0].rope_count, 1)
        self.assertEqual(one[0].bite_q8[0], (0, 1024, 512))

        payload["hook_events"].extend([
            {**fire, "event": 4, "after_step": 2, "frame": 2,
             "hook": 11, "view_short": [1024, -8192]},
            {**attach, "event": 5, "after_step": 3, "frame": 3,
             "hook": 11, "bite_q8": [2048, 0, 768]},
        ])
        two = runelearn.build_hook_candidates(
            fake_rune(), payload["steps"], payload["hook_events"],
            payload["rune_bindings"][0])
        self.assertEqual(len(two), 1)
        self.assertEqual(two[0].rope_count, 2)
        self.assertEqual(two[0].bite_q8,
                         ((0, 1024, 512), (2048, 0, 768)))

        text, _ = runelearn.build_learning(
            fake_rune(), source_sha, payload, "c" * 64)
        self.assertIn("rlearn_format 2\n", text)
        self.assertIn("hook_candidates 1\n", text)
        self.assertIn(
            "hook_candidate 0 0 0 0 1 1024 0 0 2 ", text)


if __name__ == "__main__":
    unittest.main()
