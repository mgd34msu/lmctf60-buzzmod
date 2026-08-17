#!/usr/bin/env python3
"""Synthetic-fixture tests for the BSP mechanism inventory."""

from __future__ import annotations

import csv
import io
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import bspmechanisms as BM  # noqa: E402


def entity_text(*entities):
    blocks = []
    for entity in entities:
        lines = ["{"]
        lines.extend(f'"{key}" "{value}"' for key, value in entity.items())
        lines.append("}")
        blocks.append("\n".join(lines))
    return "\n".join(blocks) + "\n"


def bsp_bytes(text, model_count=12):
    header_size = BM.MF.BSP_HEADER_SIZE
    entities = text.encode("latin-1") + b"\0"
    model_offset = header_size + len(entities)
    models = bytearray()
    for index in range(model_count):
        extent = 16.0 + index
        models.extend(struct.pack(
            "<9f3i",
            -extent, -extent, 0.0,
            extent, extent, 64.0,
            0.0, 0.0, 0.0,
            index, 0, 0,
        ))
    header = bytearray(header_size)
    struct.pack_into("<4si", header, 0, b"IBSP", 38)
    struct.pack_into("<ii", header, 8, header_size, len(entities))
    struct.pack_into("<ii", header, 8 + BM.BSP_MODEL_LUMP * 8,
                     model_offset, len(models))
    return bytes(header) + entities + bytes(models)


def truncated_entity_padding_bsp(
        text, *, overrun=17, zero_padding=2, terminator=True,
        nonzero_after_nul=False, other_lump_outside=False):
    """Synthetic form of the historical lmctf05 final-lump defect."""

    header_size = BM.MF.BSP_HEADER_SIZE
    models = struct.pack(
        "<9f3i", -16.0, -16.0, 0.0, 16.0, 16.0, 64.0,
        0.0, 0.0, 0.0, 0, 0, 0)
    model_offset = header_size
    entity_offset = model_offset + len(models)
    entity_data = text.encode("latin-1")
    if terminator:
        entity_data += b"\0"
    entity_data += b"\0" * zero_padding
    if nonzero_after_nul:
        entity_data = entity_data[:-1] + b"X"
    header = bytearray(header_size)
    struct.pack_into("<4si", header, 0, b"IBSP", 38)
    struct.pack_into("<ii", header, 8, entity_offset,
                     len(entity_data) + overrun)
    struct.pack_into("<ii", header, 8 + BM.BSP_MODEL_LUMP * 8,
                     model_offset, len(models))
    if other_lump_outside:
        struct.pack_into("<ii", header, 8 + 1 * 8,
                         entity_offset + len(entity_data), 1)
    return bytes(header) + models + entity_data


def write_pak(path, files):
    body = bytearray()
    directory = bytearray()
    entries = []
    for name, data in files:
        offset = 12 + len(body)
        body.extend(data)
        entries.append((name, offset, len(data)))
    directory_offset = 12 + len(body)
    for name, offset, length in entries:
        encoded = name.encode("latin-1")
        if len(encoded) >= 56:
            raise ValueError(name)
        directory.extend(struct.pack("<56sii", encoded, offset, length))
    path.write_bytes(
        struct.pack("<4sii", b"PACK", directory_offset, len(directory)) +
        bytes(body) + bytes(directory)
    )


def write_zip(path, files):
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in files:
            archive.writestr(name, data)


def reason_codes(record):
    return {row["code"] for row in record["unsupported_mechanism_reasons"]}


def issue_codes(record):
    return {row["code"] for row in record["issues"]}


class BspMechanismInventoryTests(unittest.TestCase):
    def test_ent_override_full_inventory_and_deterministic_renderers(self):
        base_text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1"},
        )
        override = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1", "team": "alpha"},
            {"classname": "func_door", "model": "*2", "team": "alpha",
             "targetname": "slave_only", "origin": "64 0 0"},
            {"classname": "func_door", "model": "*3", "targetname": "gate"},
            {"classname": "trigger_multiple", "model": "*4", "target": "relay"},
            {"classname": "trigger_relay", "targetname": "relay", "target": "gate"},
            {"classname": "func_button", "model": "*5", "target": "gate"},
            {"classname": "func_plat", "model": "*6", "targetname": "plat"},
            {"classname": "path_corner", "targetname": "p1", "target": "p2"},
            {"classname": "path_corner", "targetname": "p2", "target": "p1"},
            {"classname": "func_train", "model": "*7", "target": "p1",
             "targetname": "train"},
            {"classname": "trigger_elevator", "target": "train",
             "targetname": "lift"},
            {"classname": "func_button", "model": "*8", "target": "lift",
             "pathtarget": "p2"},
            {"classname": "trigger_push", "model": "*9", "speed": "1200"},
            {"classname": "misc_teleporter", "target": "dest", "origin": "1 2 3"},
            {"classname": "misc_teleporter_dest", "targetname": "dest",
             "origin": "100 200 300"},
            {"classname": "info_flag_red", "origin": "10 20 30"},
            {"classname": "item_flag_team2", "origin": "40 50 60"},
            {"classname": "trigger_relay", "targetname": "loop_a",
             "target": "loop_b"},
            {"classname": "trigger_relay", "targetname": "loop_b",
             "target": "loop_a"},
            {"classname": "trigger_multiple", "model": "*10", "target": "loop_a"},
            {"classname": "target_goal", "targetname": "goal"},
            {"classname": "trigger_multiple", "model": "*11", "target": "gate",
             "killtarget": "goal"},
        )

        with tempfile.TemporaryDirectory() as temporary:
            gamedir = Path(temporary)
            maps = gamedir / "maps"
            maps.mkdir()
            (maps / "fixture.bsp").write_bytes(bsp_bytes(base_text))
            (maps / "fixture.ent").write_text(override, encoding="latin-1")

            report = BM.inventory([gamedir])
            record = report["maps"][0]

            self.assertEqual("ok", record["status"])
            self.assertTrue(record["entity_override"])
            self.assertEqual("maps/fixture.ent", record["entity_source"])
            self.assertEqual(22, record["counts"]["entities"])
            self.assertEqual(
                [{"entity": 21, "classname": "target_goal",
                  "reason": "spawn_function_frees_in_deathmatch"}],
                record["spawn_filtered_entities"],
            )
            self.assertEqual(2, record["counts"]["door_teams"])
            self.assertEqual(1, record["counts"]["automatic_door_triggers"])

            automatic_team = record["door_teams"][0]
            self.assertEqual([1, 2], automatic_team["members"])
            self.assertEqual(1, automatic_team["master_entity"])
            self.assertEqual("automatic_touch", automatic_team["activation"])
            self.assertIsNotNone(automatic_team["automatic_trigger_candidate"]["bounds"])
            self.assertEqual(
                automatic_team["automatic_trigger_candidate"]["bounds"],
                record["automatic_door_trigger_candidates"][0]["bounds"],
            )

            external_team = record["door_teams"][1]
            self.assertEqual("external", external_team["activation"])
            self.assertIn("closure-0004", external_team["activation_closure_ids"])
            self.assertIn("closure-0006", external_team["activation_closure_ids"])
            closure = next(row for row in record["activation_closures"]
                           if row["id"] == "closure-0004")
            self.assertEqual("resolved", closure["status"])
            self.assertEqual([3, 5],
                             [index for index in closure["activation_entities"]
                              if index != 4])
            self.assertEqual([3], closure["terminal_entities"])

            self.assertEqual("looping", record["trains"][0]["route"]["status"])
            self.assertEqual("resolved", record["trigger_elevators"][0]["resolution"])
            self.assertEqual("resolved", record["trigger_elevators"][0]["callers"][0]["status"])
            self.assertEqual("resolved", record["teleporters"][0]["status"])
            self.assertEqual({"red", "blue"}, {row["team"] for row in record["flags"]})
            self.assertIn("ACTIVATION_CYCLE", issue_codes(record))

            reasons = reason_codes(record)
            self.assertTrue({"PLATFORM", "TRAIN", "TRIGGER_ELEVATOR",
                             "TRIGGER_PUSH", "TELEPORTER"}.issubset(reasons))
            self.assertNotIn("EXTERNAL_DOOR_NO_DIRECT_CLOSURE", reasons)

            json_first = BM.render_json(report)
            self.assertEqual(json_first, BM.render_json(report))
            tsv_first = BM.render_tsv(report)
            self.assertEqual(tsv_first, BM.render_tsv(report))
            self.assertEqual(3, len(tsv_first.rstrip("\n").splitlines()))
            self.assertIn("\tsummary\t*\tok\t", "\t" + tsv_first.splitlines()[1])

    def test_yamagi_numbered_and_nonnumbered_packages_precede_loose(self):
        bsp_text = entity_text({"classname": "worldspawn"})
        early = entity_text({"classname": "worldspawn"},
                            {"classname": "target_speaker"})
        late = entity_text({"classname": "worldspawn"},
                           {"classname": "trigger_push"})
        newest = entity_text({"classname": "worldspawn"},
                             {"classname": "trigger_relay"})
        custom = entity_text({"classname": "worldspawn"},
                             {"classname": "func_button", "model": "*1"})
        loose = entity_text({"classname": "worldspawn"},
                            {"classname": "func_plat", "model": "*1"})

        with tempfile.TemporaryDirectory() as temporary:
            gamedir = Path(temporary)
            maps = gamedir / "maps"
            maps.mkdir()
            (maps / "packmap.bsp").write_bytes(bsp_bytes(bsp_text))
            write_pak(gamedir / "pak0.pak", [("maps/packmap.ent", early.encode("latin-1"))])
            write_pak(gamedir / "pak9.pak", [("maps/packmap.ent", late.encode("latin-1"))])
            write_pak(gamedir / "pak10.pak", [("maps/packmap.ent", newest.encode("latin-1"))])
            (maps / "packmap.ent").write_text(loose, encoding="latin-1")

            packed_record = BM.inventory([maps / "packmap.bsp"])["maps"][0]
            self.assertEqual(1, packed_record["class_counts"].get("trigger_relay"))
            self.assertNotIn("target_speaker", packed_record["class_counts"])
            self.assertNotIn("func_plat", packed_record["class_counts"])
            self.assertIn("pak10.pak!/maps/packmap.ent", packed_record["entity_source"])

            write_pak(gamedir / "custom.pak",
                      [("maps/packmap.ent", custom.encode("latin-1"))])
            custom_record = BM.inventory([gamedir])["maps"][0]
            self.assertEqual(1, custom_record["class_counts"].get("func_button"))
            self.assertEqual("custom.pak!/maps/packmap.ent",
                             custom_record["entity_source"])

            write_pak(gamedir / "other.pak",
                      [("maps/packmap.ent", early.encode("latin-1"))])
            ambiguous = BM.inventory([gamedir])["maps"][0]
            self.assertEqual("error", ambiguous["status"])
            self.assertIn("non-numbered package order is ambiguous",
                          ambiguous["error"])

    def test_yamagi_crc_qualified_ent_precedes_plain_override(self):
        bsp_text = entity_text({"classname": "worldspawn"})
        bsp = bsp_bytes(bsp_text)
        crc = BM.MF.entity_lump_crc16(bsp)
        qualified = entity_text({"classname": "worldspawn"},
                                {"classname": "target_speaker"})
        plain = entity_text({"classname": "worldspawn"},
                            {"classname": "trigger_push"})
        with tempfile.TemporaryDirectory() as temporary:
            gamedir = Path(temporary)
            maps = gamedir / "maps"
            maps.mkdir()
            (maps / "fixture.bsp").write_bytes(bsp)
            (maps / "fixture.ent").write_text(plain, encoding="latin-1")
            write_pak(gamedir / "pak0.pak", [
                (f"maps/fixture@{crc:04x}.ent", qualified.encode("latin-1")),
            ])
            write_pak(gamedir / "pak10.pak", [
                ("maps/fixture.ent", plain.encode("latin-1")),
            ])
            record = BM.inventory([gamedir])["maps"][0]

        self.assertEqual("ok", record["status"])
        self.assertEqual(1, record["class_counts"].get("target_speaker"))
        self.assertNotIn("trigger_push", record["class_counts"])
        self.assertEqual(f"pak0.pak!/maps/fixture@{crc:04x}.ent",
                         record["entity_source"])
        empty = bytearray(BM.MF.BSP_HEADER_SIZE)
        struct.pack_into("<4si", empty, 0, b"IBSP", 38)
        struct.pack_into("<ii", empty, 8, BM.MF.BSP_HEADER_SIZE, 0)
        self.assertEqual(0, BM.MF.entity_lump_crc16(bytes(empty)))

    def test_invalid_crc_ent_suppresses_plain_fallback_and_uses_bsp(self):
        bsp_text = entity_text({"classname": "worldspawn"},
                               {"classname": "target_speaker"})
        bsp = bsp_bytes(bsp_text)
        crc = BM.MF.entity_lump_crc16(bsp)
        plain = entity_text({"classname": "worldspawn"},
                            {"classname": "trigger_push"})
        with tempfile.TemporaryDirectory() as temporary:
            gamedir = Path(temporary)
            maps = gamedir / "maps"
            maps.mkdir()
            (maps / "fixture.bsp").write_bytes(bsp)
            (maps / "fixture.ent").write_text(plain, encoding="latin-1")
            write_pak(gamedir / "pak0.pak", [
                (f"maps/fixture@{crc:04x}.ent", b"x"),
            ])
            record = BM.inventory([gamedir])["maps"][0]

        self.assertEqual("ok", record["status"])
        self.assertEqual(1, record["class_counts"].get("target_speaker"))
        self.assertNotIn("trigger_push", record["class_counts"])
        self.assertEqual("maps/fixture.bsp", record["entity_source"])

    def test_yamagi_later_numbered_package_type_wins(self):
        bsp = bsp_bytes(entity_text({"classname": "worldspawn"}))
        pak_ent = entity_text({"classname": "worldspawn"},
                              {"classname": "target_speaker"})
        pk3_ent = entity_text({"classname": "worldspawn"},
                              {"classname": "trigger_push"})
        with tempfile.TemporaryDirectory() as temporary:
            gamedir = Path(temporary)
            maps = gamedir / "maps"
            maps.mkdir()
            (maps / "fixture.bsp").write_bytes(bsp)
            write_pak(gamedir / "pak99.pak", [
                ("maps/fixture.ent", pak_ent.encode("latin-1")),
            ])
            write_zip(gamedir / "pak0.pk3", [
                ("maps/fixture.ent", pk3_ent.encode("latin-1")),
            ])
            record = BM.inventory([gamedir])["maps"][0]

        self.assertEqual("ok", record["status"])
        self.assertEqual(1, record["class_counts"].get("trigger_push"))
        self.assertNotIn("target_speaker", record["class_counts"])
        self.assertEqual("pak0.pk3!/maps/fixture.ent",
                         record["entity_source"])

    def test_malformed_ambiguous_and_cyclic_chains_are_explicit(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1", "targetname": "gate"},
            {"classname": "misc_teleporter", "target": "dest"},
            {"classname": "misc_teleporter_dest", "targetname": "dest"},
            {"classname": "misc_teleporter_dest", "targetname": "dest"},
            {"classname": "trigger_multiple", "model": "*2", "target": "missing"},
            {"classname": "trigger_multiple", "model": "*3", "target": "relay_a"},
            {"classname": "trigger_relay", "targetname": "relay_a", "target": "relay_b"},
            {"classname": "trigger_relay", "targetname": "relay_b", "target": "relay_a"},
            {"classname": "func_train", "model": "*4", "target": "corner"},
            {"classname": "path_corner", "targetname": "corner"},
            {"classname": "path_corner", "targetname": "corner"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "broken.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        self.assertEqual("ambiguous", record["teleporters"][0]["status"])
        self.assertIsNone(record["teleporters"][0]["selected_destination"])
        self.assertEqual([3, 4], record["teleporters"][0]["destinations"])
        self.assertEqual("ambiguous", record["trains"][0]["route"]["status"])
        cyclic = next(row for row in record["activation_closures"]
                      if row["source_entity"] == 6)
        self.assertEqual("cyclic", cyclic["status"])
        codes = issue_codes(record)
        self.assertIn("TARGET_MISSING", codes)
        self.assertIn("ACTIVATION_CHAIN_MALFORMED", codes)
        self.assertIn("ACTIVATION_CYCLE", codes)
        self.assertIn("TELEPORTER_TARGET_AMBIGUOUS", codes)
        self.assertIn("TRAIN_ROUTE_AMBIGUOUS", codes)
        reasons = reason_codes(record)
        self.assertIn("EXTERNAL_DOOR_NO_DIRECT_CLOSURE", reasons)
        self.assertIn("MISSING_RED_FLAG", reasons)
        self.assertIn("MISSING_BLUE_FLAG", reasons)

    def test_com_parse_comments_quoted_braces_and_unclosed_input(self):
        text = r'''
// leading line comment
{
"ClassName" /* between key and value */ "worldspawn"
"message" "literal } brace // remains data"
}
/* between entities */
{
"classname" "trigger_relay"
"target" "gate"
}
'''
        entities, issues = BM._parse_entity_text(text)
        self.assertEqual([], issues)
        self.assertEqual("worldspawn", entities[0]["classname"])
        self.assertEqual("literal } brace // remains data",
                         entities[0]["message"])
        self.assertEqual("gate", entities[1]["target"])

        damaged = (
            '{\n"classname" "worldspawn"\n',
            '{\n"classname" "worldspawn\n}\n',
            '{\n"classname" /* unterminated',
        )
        for source in damaged:
            with self.subTest(source=source), self.assertRaises(ValueError):
                BM._parse_entity_text(source)

        empty_text = entity_text({"classname": "worldspawn"}) + "{\n}\n" + \
            entity_text({"_editor": "comment"})
        parsed, _ = BM._parse_entity_text(empty_text)
        postspawn, removed = BM._postspawn_deathmatch_entities(parsed)
        self.assertIsNone(postspawn[1])
        self.assertEqual("noclass", postspawn[2]["classname"])
        self.assertIn("empty_entity_block_clears_edict",
                      {row["reason"] for row in removed})

    def test_q_stricmp_target_resolution_and_killtarget_precedes_target(self):
        case_text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1",
             "targetname": "GaTe"},
            {"classname": "trigger_multiple", "model": "*2",
             "target": "gAtE"},
        )
        kill_text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1",
             "targetname": "GaTe"},
            {"classname": "trigger_multiple", "model": "*2",
             "killtarget": "GATE", "target": "gate"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case_path = root / "case.bsp"
            kill_path = root / "kill.bsp"
            case_path.write_bytes(bsp_bytes(case_text))
            kill_path.write_bytes(bsp_bytes(kill_text))
            case_record = BM.inventory([case_path])["maps"][0]
            kill_record = BM.inventory([kill_path])["maps"][0]

        case_closure = next(row for row in case_record["activation_closures"]
                            if row["source_entity"] == 2)
        self.assertEqual("resolved", case_closure["status"])
        self.assertEqual([1], case_closure["terminal_entities"])
        self.assertEqual(["door-team-0000"],
                         case_closure["door_master_team_ids"])

        kill_closure = next(row for row in kill_record["activation_closures"]
                            if row["source_entity"] == 2)
        self.assertEqual("suppressed", kill_closure["status"])
        self.assertEqual([1], kill_closure["killed_entities"])
        self.assertEqual([], kill_closure["terminal_entities"])
        self.assertEqual([], kill_closure["door_master_team_ids"])
        self.assertEqual([{"entity": 2, "target": "gate"}],
                         kill_closure["suppressed_targets"])

    def test_trigger_player_admission_disabled_use_and_once_legacy_bit(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1",
             "targetname": "gate"},
            {"classname": "trigger_multiple", "model": "*2",
             "spawnflags": "2", "target": "gate"},
            {"classname": "func_door", "model": "*3",
             "targetname": "gate2"},
            {"classname": "trigger_multiple", "model": "*4",
             "spawnflags": "4", "targetname": "dormant",
             "target": "gate2"},
            {"classname": "func_button", "model": "*5",
             "target": "DORMANT"},
            {"classname": "trigger_once", "model": "*6",
             "spawnflags": "1", "target": "gate"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "trigger-flags.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        closures = {row["source_entity"]: row
                    for row in record["activation_closures"]}
        self.assertEqual("remote_only_for_player",
                         closures[2]["activation_mode"])
        self.assertEqual("enable_then_touch",
                         closures[4]["activation_mode"])
        self.assertEqual("gated", closures[5]["status"])
        self.assertEqual([4], closures[5]["enabled_entities"])
        self.assertEqual([], closures[5]["terminal_entities"])
        self.assertEqual("enable_then_touch",
                         closures[6]["activation_mode"])
        once = next(row for row in record["entities"] if row["index"] == 6)
        self.assertEqual("4", once["keys"]["spawnflags"])

    def test_postspawn_deathmatch_inuse_filter_precedes_graph_and_teams(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "target_goal", "targetname": "gone_goal"},
            {"classname": "target_secret", "targetname": "gone_secret"},
            {"classname": "func_group", "team": "paired"},
            {"classname": "info_null", "targetname": "gone_null"},
            {"classname": "func_door", "model": "*1", "team": "paired"},
            {"classname": "trigger_multiple", "model": "*2",
             "spawnflags": "2048", "target": "gate"},
            {"classname": "func_door", "model": "*3",
             "targetname": "gate"},
            {"classname": "info_flag_red", "origin": "0 0 0"},
            {"classname": "info_flag_blue", "origin": "128 0 0"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "postspawn.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        self.assertEqual(5, record["counts"]["entities"])
        self.assertEqual([0, 5, 7, 8, 9],
                         [row["index"] for row in record["entities"]])
        self.assertEqual([5], record["door_teams"][0]["members"])
        self.assertFalse(record["door_teams"][0]["mixed_classes"])
        self.assertEqual([], record["objectives"])
        self.assertEqual([{"name": "gate", "entities": [7]}],
                         record["targetnames"])
        self.assertEqual({1, 2, 3, 4, 6},
                         {row["entity"]
                          for row in record["spawn_filtered_entities"]})

    def test_postspawn_field_removals_and_profile_uncertainty_are_explicit(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "target_help", "targetname": "gone"},
            {"classname": "trigger_multiple", "model": "*1",
             "target": "gone"},
            {"classname": "path_corner"},
            {"classname": "func_explosive", "speed": "0"},
            {"classname": "misc_viper"},
            {"classname": "misc_strogg_ship"},
            {"classname": "func_clock"},
            {"classname": "func_clock", "target": "", "spawnflags": "2",
             "count": "0"},
            {"classname": "misc_teleporter"},
            {"classname": "target_changelevel"},
            {"classname": "trigger_gravity"},
            {"classname": "misc_teleporter", "target": ""},
            {"classname": "trigger_gravity", "gravity": ""},
            {"classname": "weapon_railgun"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "spawn-filter.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        self.assertEqual(
            {1, 3, 4, 5, 6, 7, 8, 9, 10, 11},
            {row["entity"] for row in record["spawn_filtered_entities"]},
        )
        self.assertEqual([0, 2, 12, 13, 14],
                         [row["index"] for row in record["entities"]])
        closure = next(row for row in record["activation_closures"]
                       if row["source_entity"] == 2)
        self.assertEqual("malformed", closure["status"])
        self.assertEqual([], closure["terminal_entities"])
        self.assertEqual("unresolved", record["spawn_profile"]["dmflags"])
        self.assertEqual(
            [{"entity": 14, "classname": "weapon_railgun",
              "reason": "runtime_item_cvars_unavailable"}],
            record["spawn_profile_dependent_entities"],
        )

    def test_self_target_is_skipped_while_fanout_door_still_fires(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1",
             "targetname": "gate"},
            {"classname": "trigger_multiple", "model": "*2",
             "targetname": "gate", "target": "gate"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "self-fanout.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        closure = next(row for row in record["activation_closures"]
                       if row["source_entity"] == 2)
        self.assertEqual("resolved", closure["status"])
        self.assertEqual([1], closure["terminal_entities"])
        self.assertEqual([], closure["cycles"])
        self.assertEqual(["door-team-0000"],
                         closure["door_master_team_ids"])

    def test_absent_target_is_legal_after_optional_killtarget(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "target_speaker", "targetname": "victim",
             "noise": "misc/talk1"},
            {"classname": "trigger_relay", "killtarget": "victim"},
            {"classname": "trigger_multiple", "model": "*1"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "absent-target.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        closures = {row["source_entity"]: row
                    for row in record["activation_closures"]}
        self.assertEqual("suppressed", closures[2]["status"])
        self.assertEqual([1], closures[2]["killed_entities"])
        self.assertEqual([], closures[2]["missing_targets"])
        self.assertEqual("empty", closures[3]["status"])
        self.assertEqual([], closures[3]["missing_targets"])
        malformed = [issue for issue in record["issues"]
                     if issue["code"] == "ACTIVATION_CHAIN_MALFORMED"]
        self.assertEqual([], malformed)

    def test_elevator_distinguishes_null_and_empty_target_pointers(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_train", "model": "*1", "targetname": ""},
            {"classname": "trigger_elevator", "target": ""},
            {"classname": "trigger_elevator"},
            {"classname": "func_train", "model": "*2",
             "targetname": "train"},
            {"classname": "trigger_elevator", "targetname": "lift",
             "target": "train"},
            {"classname": "func_button", "model": "*3", "target": "lift",
             "pathtarget": ""},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "elevator-empty-pointers.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        elevators = {row["entity"]: row for row in record["trigger_elevators"]}
        self.assertEqual("resolved", elevators[2]["resolution"])
        self.assertEqual([1], elevators[2]["train_entities"])
        self.assertEqual("missing_target", elevators[3]["resolution"])
        self.assertEqual([], elevators[3]["train_entities"])
        self.assertEqual(
            [{"entity": 6, "pathtarget": "", "destinations": [1],
              "status": "resolved"}],
            elevators[5]["callers"],
        )

    def test_q_stricmp_does_not_apply_unicode_casefold_expansions(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1",
             "targetname": "STRASSE"},
            {"classname": "trigger_multiple", "model": "*2",
             "target": "straße"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "ascii-fold.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        closure = next(row for row in record["activation_closures"]
                       if row["source_entity"] == 2)
        self.assertEqual("malformed", closure["status"])
        self.assertEqual([], closure["terminal_entities"])
        self.assertIn("TARGET_MISSING", issue_codes(record))

    def test_start_open_and_rotated_link_bounds_feed_auto_trigger(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1",
             "spawnflags": "1", "angles": "0 90 0", "angle": "0"},
            {"classname": "func_door_rotating", "model": "*2",
             "spawnflags": "1", "distance": "90"},
            {"classname": "func_door", "model": "*3", "spawnflags": "1",
             "angle": "0", "lip": "1e1"},
            {"classname": "func_door_rotating", "model": "*4",
             "spawnflags": "1", "distance": "not-a-number"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "door-bounds.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        by_master = {row["master_entity"]: row for row in record["door_teams"]}
        self.assertEqual(
            [[-52.0, -80.0, -3.0], [108.0, 80.0, 67.0]],
            by_master[1]["automatic_trigger_candidate"]["bounds"],
        )
        self.assertEqual("bsp_model_start_open_with_link_fringe",
                         by_master[1]["member_bound_status"][0]["status"])
        self.assertEqual(
            [[-127.0, -127.0, -67.0], [127.0, 127.0, 67.0]],
            by_master[2]["automatic_trigger_candidate"]["bounds"],
        )
        self.assertEqual("bsp_model_rotated_radius_with_link_fringe",
                         by_master[2]["member_bound_status"][0]["status"])
        self.assertEqual(
            [[-43.0, -82.0, -3.0], [121.0, 82.0, 67.0]],
            by_master[3]["automatic_trigger_candidate"]["bounds"],
        )
        self.assertEqual("bsp_model_start_open_with_link_fringe",
                         by_master[3]["member_bound_status"][0]["status"])
        self.assertEqual(
            [[-127.0, -127.0, -67.0], [127.0, 127.0, 67.0]],
            by_master[4]["automatic_trigger_candidate"]["bounds"],
        )
        self.assertEqual("bsp_model_rotated_radius_with_link_fringe",
                         by_master[4]["member_bound_status"][0]["status"])

    def test_inline_model_uses_engine_atoi_suffix_and_rejects_world_model(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*+1suffix"},
            {"classname": "func_door", "model": "*0"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "inline-model-prefix.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        by_master = {row["master_entity"]: row for row in record["door_teams"]}
        self.assertEqual("bsp_model_with_link_fringe",
                         by_master[1]["member_bound_status"][0]["status"])
        self.assertEqual("brush_model_out_of_range",
                         by_master[2]["member_bound_status"][0]["status"])
        self.assertIn("DOOR_MODEL_BOUNDS_UNRESOLVED", issue_codes(record))

    def test_ed_parsefield_numeric_prefix_and_partial_vector_semantics(self):
        self.assertEqual((42, True), BM._c_atoi("\v\f 42suffix"))
        self.assertEqual((1.5, True), BM._c_atof("\v\f1.5suffix"))
        self.assertEqual(([1.0, 2.0, 3.0], True),
                         BM._vec3("\v1\f2\r3"))
        self.assertEqual((0, True), BM._c_atoi("\u00a042"))
        self.assertEqual((0.0, True), BM._c_atof("\u00a01.5"))
        self.assertEqual((None, False), BM._vec3("\u00a01 2 3"))
        self.assertEqual(([64.0, 0.0, 0.0], True), BM._vec3("64"))
        self.assertEqual(([64.0, 32.0, 0.0], True), BM._vec3("64 32"))
        self.assertEqual(([1.0, 2.0, 3.0], True), BM._vec3("1 2 3junk"))
        self.assertEqual(([1.0, 2.0, 3.0], True), BM._vec3("1 2 3 4"))
        self.assertEqual((None, False), BM._vec3("1e"))
        self.assertEqual(([4.0, 3.0, 4.0], True),
                         BM._vec3("0x1p2 3 4"))
        self.assertEqual(([1.0, 0.0, 0.0], True), BM._vec3("0x1"))
        self.assertEqual((None, False), BM._vec3("0x1p"))
        self.assertEqual((None, False), BM._vec3("0x1p+"))
        self.assertEqual((None, False), BM._vec3("0x 2"))
        self.assertEqual((None, False), BM._vec3("nan(foo) 2"))
        parsed_nan, valid_nan = BM._c_atof("nan(foo)")
        self.assertTrue(math.isnan(parsed_nan))
        self.assertFalse(valid_nan)
        self.assertEqual((float("inf"), False), BM._c_atof("INF"))

        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1",
             "origin": "64", "spawnflags": "1", "angle": "90degrees",
             "health": "0.000000"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "parsefield-prefix.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        self.assertNotIn("DOOR_MALFORMED_HEALTH", issue_codes(record))
        door = record["door_teams"][0]
        self.assertEqual("automatic_touch", door["activation"])
        self.assertEqual(
            [[-16.0, -52.0, -3.0], [144.0, 108.0, 67.0]],
            door["automatic_trigger_candidate"]["bounds"],
        )
        candidate = door["automatic_trigger_candidate"]
        self.assertEqual(1.0, candidate["member_link_fringe"])
        self.assertEqual(1.0, candidate["trigger_link_fringe"])
        self.assertEqual(
            "live_abs_bounds_with_member_and_trigger_link_fringe",
            candidate["bounds_status"])

    def test_delayed_fanout_and_door_target_side_effects_are_not_collapsed(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1",
             "targetname": "gate", "target": "next"},
            {"classname": "func_door", "model": "*2",
             "targetname": "next"},
            {"classname": "trigger_relay", "targetname": "late",
             "delay": "1", "killtarget": "gate"},
            {"classname": "trigger_relay", "targetname": "now",
             "target": "gate"},
            {"classname": "trigger_multiple", "model": "*3",
             "target": "late"},
            {"classname": "trigger_multiple", "model": "*4",
             "target": "both"},
            {"classname": "trigger_relay", "targetname": "both",
             "target": "late"},
            {"classname": "trigger_relay", "targetname": "both",
             "target": "now"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "timed-door-chain.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        direct = next(row for row in record["activation_closures"]
                      if row["source_entity"] == 4)
        self.assertEqual([1, 2], direct["terminal_entities"])
        fanout = next(row for row in record["activation_closures"]
                      if row["source_entity"] == 6)
        self.assertEqual("partial", fanout["status"])
        self.assertEqual([3], fanout["delayed_entities"])
        self.assertEqual([1, 2], fanout["terminal_entities"])
        self.assertIn("ACTIVATION_CHAIN_DELAYED", issue_codes(record))

    def test_killed_team_member_breaks_runtime_door_chain(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1", "team": "chain",
             "targetname": "master"},
            {"classname": "func_door", "model": "*2", "team": "chain",
             "targetname": "victim"},
            {"classname": "func_door", "model": "*3", "team": "chain",
             "target": "leaf"},
            {"classname": "func_door", "model": "*4",
             "targetname": "leaf"},
            {"classname": "trigger_multiple", "model": "*5",
             "killtarget": "victim", "target": "master"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "killed-door-chain.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        closure = next(row for row in record["activation_closures"]
                       if row["source_entity"] == 5)
        self.assertEqual([2], closure["killed_entities"])
        self.assertEqual([1], closure["terminal_entities"])

    def test_team_member_freed_by_own_walk_breaks_runtime_chain(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1", "team": "chain",
             "targetname": "master"},
            {"classname": "func_door", "model": "*2", "team": "chain",
             "targetname": "self", "killtarget": "self"},
            {"classname": "func_door", "model": "*3", "team": "chain",
             "target": "leaf"},
            {"classname": "func_door", "model": "*4",
             "targetname": "leaf"},
            {"classname": "trigger_multiple", "model": "*5",
             "target": "master"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "self-freed-door-chain.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        closure = next(row for row in record["activation_closures"]
                       if row["source_entity"] == 5)
        self.assertEqual([2], closure["killed_entities"])
        self.assertEqual([1], closure["terminal_entities"])
        self.assertNotIn(4, closure["activation_entities"])

    def test_killtarget_self_stops_before_later_same_name_victim(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "trigger_relay", "targetname": "kill",
             "killtarget": "kill"},
            {"classname": "func_door", "model": "*1",
             "targetname": "kill"},
            {"classname": "trigger_multiple", "model": "*2",
             "target": "kill"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "killtarget-self-order.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        closure = next(row for row in record["activation_closures"]
                       if row["source_entity"] == 3)
        self.assertEqual([1], closure["killed_entities"])
        self.assertEqual([2], closure["terminal_entities"])

    def test_door_delay_without_use_targets_payload_is_inert(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1",
             "targetname": "gate", "delay": "1"},
            {"classname": "trigger_multiple", "model": "*2",
             "target": "gate"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "inert-door-delay.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        closure = record["activation_closures"][0]
        self.assertEqual("resolved", closure["status"])
        self.assertEqual([], closure["delayed_entities"])
        self.assertNotIn("ACTIVATION_CHAIN_DELAYED", issue_codes(record))
        self.assertNotIn("EXTERNAL_DOOR_PARTIAL_CLOSURE",
                         reason_codes(record))

    def test_door_use_clears_message_before_delay_is_copied(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1",
             "targetname": "gate", "delay": "1", "message": "opened"},
            {"classname": "trigger_multiple", "model": "*2",
             "target": "gate"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "door-message-delay.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        closure = record["activation_closures"][0]
        self.assertEqual("resolved", closure["status"])
        self.assertEqual([], closure["delayed_entities"])

    def test_contextual_g_use_targets_sources_are_explicitly_inventoried(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1",
             "targetname": "gate"},
            {"classname": "func_timer", "target": "gate",
             "spawnflags": "1"},
            {"classname": "trigger_always", "target": "gate"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "contextual-initiators.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        initiators = {row["entity"]: row
                      for row in record["activation_initiators"]}
        self.assertEqual("periodic_timer", initiators[2]["event"])
        self.assertEqual([1], initiators[2]["references"][0]["destinations"])
        self.assertEqual("spawn_delay", initiators[3]["event"])
        self.assertIn("UNMODELED_ACTIVATION_INITIATOR", reason_codes(record))

    def test_stock_deathmatch_monster_spawns_are_filtered_exactly(self):
        freed = sorted(name for name in BM.DEATHMATCH_SPAWN_FREES
                       if name.startswith("monster_"))
        text = entity_text(
            {"classname": "worldspawn"},
            *({"classname": name, "targetname": f"gone{index}"}
              for index, name in enumerate(freed)),
            {"classname": "misc_actor"},
            {"classname": "misc_insane"},
            {"classname": "turret_driver"},
            {"classname": "monster_commander_body"},
            {"classname": "monster_custom_survivor"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "deathmatch-monsters.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        self.assertNotIn("misc_actor", record["class_counts"])
        self.assertNotIn("misc_insane", record["class_counts"])
        self.assertNotIn("turret_driver", record["class_counts"])
        self.assertTrue(all(name not in record["class_counts"] for name in freed))
        self.assertEqual(1, record["class_counts"]["monster_commander_body"])
        self.assertEqual(1, record["class_counts"]["monster_custom_survivor"])

    def test_func_water_postspawn_conversion_and_coop_removal(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "info_player_coop", "team": "waterteam"},
            {"classname": "func_water", "model": "*1",
             "team": "waterteam", "targetname": "water"},
            {"classname": "func_water", "model": "*2",
             "targetname": "solo", "health": "100"},
            {"classname": "trigger_multiple", "model": "*3",
             "target": "solo"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "func-water.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        self.assertNotIn("info_player_coop", record["class_counts"])
        self.assertEqual(2, record["class_counts"].get("func_door"))
        teams = {row["master_entity"]: row for row in record["door_teams"]}
        self.assertEqual("func_water", teams[2]["master_spawn_class"])
        self.assertEqual("external", teams[2]["activation"])
        self.assertIsNone(teams[2]["automatic_trigger_candidate"])
        self.assertEqual("func_water", teams[3]["master_spawn_class"])
        self.assertEqual([], teams[3]["shootable_members"])
        self.assertNotIn("SHOOTABLE_DOOR_MEMBER", reason_codes(record))
        closure = next(row for row in record["activation_closures"]
                       if row["source_entity"] == 4)
        self.assertEqual([3], closure["terminal_entities"])

    def test_postspawn_class_aliases_preserve_private_dispatch_identity(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "item_flag_team1", "origin": "1 2 3"},
            {"classname": "item_flag_team2", "origin": "4 5 6"},
            {"classname": "info_player_team1"},
            {"classname": "info_player_team2"},
            {"classname": "func_door_secret", "model": "*1"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "spawn-aliases.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        self.assertEqual(1, record["class_counts"]["info_flag_red"])
        self.assertEqual(1, record["class_counts"]["info_flag_blue"])
        self.assertEqual(1, record["class_counts"]["info_player_red"])
        self.assertEqual(1, record["class_counts"]["info_player_blue"])
        self.assertEqual(1, record["class_counts"]["func_door"])
        self.assertEqual({"red", "blue"},
                         {row["team"] for row in record["flags"]})
        self.assertEqual([], record["door_teams"])
        self.assertIn("SECRET_DOOR", reason_codes(record))

    def test_master_then_slave_door_fanout_is_not_a_false_warning(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1", "team": "pair",
             "targetname": "gate"},
            {"classname": "func_door", "model": "*2", "team": "pair",
             "targetname": "gate"},
            {"classname": "trigger_multiple", "model": "*3",
             "target": "gate"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "master-slave-fanout.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        closure = record["activation_closures"][0]
        self.assertEqual([1, 2], closure["terminal_entities"])
        self.assertEqual(["door-team-0000"],
                         closure["door_master_team_ids"])
        self.assertNotIn("ACTIVATION_TARGETS_DOOR_SLAVE",
                         issue_codes(record))

    def test_train_uses_generic_picktarget_waypoints_and_halts_double_teleport(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_train", "model": "*1", "target": "a"},
            {"classname": "info_notnull", "targetname": "a", "target": "b"},
            {"classname": "info_notnull", "targetname": "b", "target": "c",
             "spawnflags": "1"},
            {"classname": "info_notnull", "targetname": "c", "target": "d",
             "spawnflags": "1"},
            {"classname": "info_notnull", "targetname": "d"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "generic-train-waypoints.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        route = record["trains"][0]["route"]
        self.assertEqual("halted", route["status"])
        self.assertEqual([2, 3, 4], route["path_corners"])
        self.assertEqual(["info_notnull"] * 3, route["waypoint_classes"])
        self.assertEqual([3], route["teleport_corners"])
        self.assertEqual("connected_teleport_waypoints",
                         route["malformed"][0]["reason"])

    def test_train_self_targeted_teleport_halts_instead_of_looping(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_train", "model": "*1", "target": "a"},
            {"classname": "path_corner", "targetname": "a", "target": "a",
             "spawnflags": "1"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "self-teleport-train.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        route = record["trains"][0]["route"]
        self.assertEqual("halted", route["status"])
        self.assertEqual("connected_teleport_waypoints",
                         route["malformed"][0]["reason"])

    def test_lstring_newstring_transform_affects_target_identity(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1",
             "targetname": r"ga\te"},
            {"classname": "trigger_multiple", "model": "*2",
             "target": r"ga\xe"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "newstring.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        # ED_NewString maps both non-newline escapes to a literal backslash.
        closure = next(row for row in record["activation_closures"]
                       if row["source_entity"] == 2)
        self.assertEqual("resolved", closure["status"])
        self.assertEqual([1], closure["terminal_entities"])

    def test_bounded_final_entity_padding_recovery_is_explicit(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "info_flag_red", "origin": "0 0 0"},
            {"classname": "info_flag_blue", "origin": "128 0 0"},
        )
        data = truncated_entity_padding_bsp(text, overrun=17,
                                            zero_padding=2)
        with self.assertRaisesRegex(ValueError,
                                    "entity lump is outside the file"):
            BM.MF.bsp_entities(data, "synthetic.bsp")
        with self.assertRaisesRegex(ValueError,
                                    "requires full parsing"):
            BM.MF.bsp_entities_with_provenance(
                data, "synthetic.bsp",
                allow_truncated_zero_padding=True)

        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "lmctf05.bsp"
            path.write_bytes(data)
            report = BM.inventory([path])
        record = report["maps"][0]
        recovery = record["entity_lump_recovery"]
        self.assertEqual("ok", record["status"])
        self.assertEqual("truncated_zero_padding", recovery["kind"])
        self.assertEqual(17, recovery["overrun_bytes"])
        self.assertEqual(2, recovery["zero_padding_bytes"])
        self.assertTrue(recovery["all_other_nonempty_lumps_in_bounds"])
        self.assertTrue(recovery["complete_entity_parse"])
        self.assertIn('"entity_lump_recovery": {', BM.render_json(report))

    def test_lmctf02_profile_remains_strictly_retired_not_recovered(self):
        text = entity_text({"classname": "worldspawn"})
        data = truncated_entity_padding_bsp(text, overrun=16,
                                            zero_padding=3)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "lmctf02.bsp"
            path.write_bytes(data)
            report = BM.inventory([path])

        record = report["maps"][0]
        self.assertEqual("retired", record["status"])
        self.assertEqual("lmctf02c", record["replacement_map"])
        self.assertEqual("malformed_bsp_entity_lump",
                         record["disposition"]["reason"])
        self.assertIn("entity lump is outside the file",
                      record["validation_error"])
        self.assertEqual(1, report["summary"]["maps"])
        self.assertEqual(0, report["summary"]["maps_active"])
        self.assertEqual(0, report["summary"]["maps_ok"])
        self.assertEqual(1, report["summary"]["maps_retired"])

    def test_truncated_entity_padding_malformed_variants_reject(self):
        valid = entity_text({"classname": "worldspawn"})
        variants = {
            "invalid_offset": bytearray(truncated_entity_padding_bsp(valid)),
            "excessive_overrun": truncated_entity_padding_bsp(
                valid, overrun=BM.MF.MAX_ENTITY_PADDING_OVERRUN + 1),
            "missing_nul": truncated_entity_padding_bsp(
                valid, terminator=False, zero_padding=0),
            "nonzero_after_nul": truncated_entity_padding_bsp(
                valid, nonzero_after_nul=True),
            "other_lump_outside": truncated_entity_padding_bsp(
                valid, other_lump_outside=True),
            "malformed_entities": truncated_entity_padding_bsp(
                '{\n"classname" "worldspawn"\n'),
        }
        struct.pack_into("<i", variants["invalid_offset"], 8, 0)
        variants["invalid_offset"] = bytes(variants["invalid_offset"])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, data in variants.items():
                with self.subTest(name=name):
                    path = root / "lmctf05.bsp"
                    path.write_bytes(data)
                    record = BM.inventory([path])["maps"][0]
                    self.assertEqual("error", record["status"])

    @unittest.skipUnless(
        Path("/home/buzzkill/Games/Quake2/lmctf-buzzmod").is_dir(),
        "exact installed LMCTF assets unavailable")
    def test_exact_lmctf05_recovers_and_lmctf02_is_retired(self):
        gamedir = "/home/buzzkill/Games/Quake2/lmctf-buzzmod"
        exact = {}
        for map_name in ("lmctf02", "lmctf05"):
            exact[map_name] = BM.MF.read_game_file(
                gamedir, f"maps/{map_name}.bsp")
            self.assertIsNotNone(exact[map_name])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            records = {}
            for map_name, data in exact.items():
                path = root / f"{map_name}.bsp"
                path.write_bytes(data)
                records[map_name] = BM.inventory([path])["maps"][0]

        self.assertEqual("retired", records["lmctf02"]["status"])
        self.assertEqual("lmctf02c", records["lmctf02"]["replacement_map"])
        self.assertEqual("ok", records["lmctf05"]["status"])
        self.assertEqual(17, records["lmctf05"]
                         ["entity_lump_recovery"]["overrun_bytes"])
        self.assertEqual(2, records["lmctf05"]
                         ["entity_lump_recovery"]["zero_padding_bytes"])

    def test_report_paths_are_root_relative_and_tsv_summary_reports_error(self):
        valid = entity_text(
            {"classname": "worldspawn"},
            {"classname": "info_flag_red", "origin": "0 0 0"},
            {"classname": "info_flag_blue", "origin": "128 0 0"},
        )
        override = valid.replace("worldspawn", "worldspawn", 1)
        with (tempfile.TemporaryDirectory() as first_temporary,
              tempfile.TemporaryDirectory() as second_temporary):
            reports = []
            for temporary in (first_temporary, second_temporary):
                gamedir = Path(temporary)
                maps = gamedir / "maps"
                maps.mkdir()
                (maps / "fixture.bsp").write_bytes(bsp_bytes(valid))
                (maps / "fixture.ent").write_text(override,
                                                   encoding="latin-1")
                reports.append(BM.inventory([gamedir]))
            self.assertEqual(BM.render_json(reports[0]),
                             BM.render_json(reports[1]))
            self.assertEqual(BM.render_tsv(reports[0]),
                             BM.render_tsv(reports[1]))
            self.assertEqual("maps/fixture.ent",
                             reports[0]["maps"][0]["entity_source"])
            self.assertEqual("maps/fixture.bsp",
                             reports[0]["maps"][0]["bsp_source"])

            broken = Path(first_temporary) / "broken.bsp"
            broken.write_bytes(bsp_bytes('{\n"classname" "worldspawn"\n'))
            error_report = BM.inventory([broken])
            self.assertNotIn(first_temporary, BM.render_json(error_report))
        rows = list(csv.DictReader(io.StringIO(BM.render_tsv(error_report)),
                                   delimiter="\t"))
        self.assertEqual("error", rows[0]["status"])
        self.assertEqual("error", rows[1]["status"])

    def test_targeting_a_door_team_slave_is_not_a_valid_closure(self):
        text = entity_text(
            {"classname": "worldspawn"},
            {"classname": "func_door", "model": "*1", "team": "paired",
             "targetname": "master_gate"},
            {"classname": "func_door", "model": "*2", "team": "paired",
             "targetname": "slave_gate"},
            {"classname": "trigger_multiple", "model": "*3",
             "target": "slave_gate"},
            {"classname": "info_flag_red", "origin": "0 0 0"},
            {"classname": "info_flag_blue", "origin": "128 0 0"},
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "slave-target.bsp"
            path.write_bytes(bsp_bytes(text))
            record = BM.inventory([path])["maps"][0]

        closure = next(row for row in record["activation_closures"]
                       if row["source_entity"] == 3)
        self.assertEqual(
            [{"entity": 2, "door_team_id": "door-team-0000", "role": "slave"}],
            closure["door_terminals"],
        )
        self.assertEqual([], closure["door_master_team_ids"])
        self.assertEqual([], record["door_teams"][0]["activation_closure_ids"])
        self.assertIn("ACTIVATION_TARGETS_DOOR_SLAVE", issue_codes(record))
        self.assertIn("EXTERNAL_DOOR_NO_DIRECT_CLOSURE", reason_codes(record))


if __name__ == "__main__":
    unittest.main()
