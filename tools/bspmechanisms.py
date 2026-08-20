#!/usr/bin/env python3
"""Inventory deterministic Quake II BSP entities and mechanisms.

The tool resolves loose and packaged map assets through mapflags. It records
activation topology but makes no claim that a mechanism is bot-traversable.
JSON contains entity detail; TSV contains stable map summaries.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import csv
from dataclasses import dataclass
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import struct
import sys
from typing import Iterable, Sequence

import mapflags as MF
from corpusgraph import atomic_write_bytes


SCHEMA_VERSION = 1
BSP_MODEL_LUMP = 13
BSP_MODEL_BYTES = 48

DOOR_CLASSES = frozenset(("func_door", "func_door_rotating"))
ACTIVATOR_CLASSES = frozenset(
    ("trigger_multiple", "trigger_once", "func_button", "trigger_relay")
)
CONTEXTUAL_ACTIVATION_EVENTS = {
    "func_clock": "clock_pathtarget",
    "func_explosive": "damage_or_use",
    "func_timer": "periodic_timer",
    "misc_viper_bomb": "impact",
    "path_corner": "rider_pathtarget",
    "target_crosslevel_target": "crosslevel_think",
    "target_explosion": "explosion_completion",
    "trigger_always": "spawn_delay",
    "trigger_counter": "counted_use",
    "trigger_key": "inventory_use",
}
FLAG_TEAMS = {
    "info_flag_red": "red",
    "item_flag_team1": "red",
    "info_flag_blue": "blue",
    "item_flag_team2": "blue",
}
OBJECTIVE_CLASSES = frozenset(("target_goal", "target_secret"))


@dataclass(frozen=True)
class MapAsset:
    """One map resolved either through a game directory or a direct BSP."""

    map_name: str
    gamedir: Path | None = None
    bsp_path: Path | None = None
    source_root: Path | None = None

    @property
    def identity(self) -> tuple[str, str, str]:
        return (
            self.map_name.lower(),
            str(self.gamedir.resolve()) if self.gamedir else "",
            str(self.bsp_path.resolve()) if self.bsp_path else "",
        )


def _pak_map_names(gamedir: Path) -> set[str]:
    names: set[str] = set()
    for relpath in MF.game_package_names(str(gamedir)):
        match = re.fullmatch(r"maps/([^/]+)\.bsp", relpath, re.I)
        if match:
            names.add(match.group(1).lower())
    return names


def _game_assets(gamedir: Path) -> list[MapAsset]:
    names = {name.lower(): name for name in _pak_map_names(gamedir)}
    maps_dir = gamedir / "maps"
    if maps_dir.is_dir():
        for child in sorted(maps_dir.iterdir(), key=lambda entry: entry.name.lower()):
            if child.is_file() and child.suffix.lower() == ".bsp":
                # Discovery deduplicates names; MF resolves package precedence.
                names[child.stem.lower()] = child.stem
    return [MapAsset(names[key], gamedir=gamedir) for key in sorted(names)]


def discover_assets(inputs: Sequence[str | os.PathLike[str]]) -> list[MapAsset]:
    """Resolve BSP files/directories into a stable, duplicate-free map list."""

    found: list[MapAsset] = []
    for raw in inputs:
        path = Path(raw).expanduser().resolve()
        if path.is_file():
            if path.suffix.lower() != ".bsp":
                raise ValueError(f"{path}: expected a .bsp file")
            if path.parent.name.lower() == "maps":
                found.append(MapAsset(path.stem, gamedir=path.parent.parent))
            else:
                found.append(MapAsset(path.stem, bsp_path=path,
                                      source_root=path.parent))
            continue
        if not path.is_dir():
            raise FileNotFoundError(path)

        if (path / "maps").is_dir() or any(path.glob("*.pak")):
            found.extend(_game_assets(path))
            continue
        if path.name.lower() == "maps":
            found.extend(_game_assets(path.parent))
            continue

        direct = sorted(
            (candidate for candidate in path.rglob("*")
             if candidate.is_file() and candidate.suffix.lower() == ".bsp"),
            key=lambda candidate: str(candidate).lower(),
        )
        found.extend(MapAsset(candidate.stem, bsp_path=candidate,
                              source_root=path)
                     for candidate in direct)

    unique = {asset.identity: asset for asset in found}
    return sorted(unique.values(), key=lambda asset: asset.identity)


def _canonical_direct_source(path: Path, root: Path | None) -> str:
    """Return stable provenance without embedding the checkout/asset root."""

    if root is not None:
        try:
            return path.resolve().relative_to(root.resolve()).as_posix()
        except ValueError:
            pass
    return path.name


def _read_direct_ent(
        path: Path, root: Path | None,
        *, allow_truncated_zero_padding: bool = False,
) -> tuple[str, str, dict | None]:
    ent_path = path.with_suffix(".ent")
    if ent_path.exists():
        data = ent_path.read_bytes()
        if len(data) > MF.MAX_ENTITY_BYTES:
            source = _canonical_direct_source(ent_path, root)
            raise ValueError(f"{source}: entity text is unreasonably large")
        return (data.split(b"\0", 1)[0].decode("latin-1"),
                _canonical_direct_source(ent_path, root), None)
    data = path.read_bytes()
    source = _canonical_direct_source(path, root)
    text, recovery = MF.bsp_entities_with_provenance(
        data, source,
        allow_truncated_zero_padding=allow_truncated_zero_padding,
        recovery_validator=_parse_entity_text)
    return text, source, recovery


def load_asset(asset: MapAsset) -> tuple[str, str, bytes, str, dict | None]:
    """Return entity/BSP bytes and explicit recovery provenance."""

    if asset.gamedir is not None:
        gamedir = asset.gamedir
        allow_recovery = asset.map_name.lower() == "lmctf05"
        text, entity_source, recovery = MF.entity_text_with_provenance(
            str(gamedir), asset.map_name,
            allow_truncated_zero_padding=allow_recovery,
            recovery_validator=_parse_entity_text)
        bsp_rel = f"maps/{asset.map_name}.bsp"
        bsp_data, bsp_source = MF.read_game_file_with_source(
            str(gamedir), bsp_rel)
        if bsp_data is None:
            raise FileNotFoundError(f"{gamedir}: no {bsp_rel}")
        return text, entity_source, bsp_data, bsp_source or bsp_rel, recovery

    if asset.bsp_path is None:
        raise ValueError(f"{asset.map_name}: unresolved map asset")
    text, entity_source, recovery = _read_direct_ent(
        asset.bsp_path, asset.source_root,
        allow_truncated_zero_padding=asset.map_name.lower() == "lmctf05")
    bsp_source = _canonical_direct_source(asset.bsp_path, asset.source_root)
    return (text, entity_source, asset.bsp_path.read_bytes(), bsp_source,
            recovery)


def parse_bsp_models(data: bytes, source: str = "<BSP>") -> list[dict]:
    """Read Q2 dmodel bounds, validating the same IBSP v38 envelope."""

    MF.bsp_lumps(data, source)
    header_offset = 8 + BSP_MODEL_LUMP * 8
    offset, length = struct.unpack_from("<ii", data, header_offset)
    if (offset < MF.BSP_HEADER_SIZE or length < BSP_MODEL_BYTES or
            offset > len(data) or length > len(data) - offset or
            length % BSP_MODEL_BYTES):
        raise ValueError(f"{source}: model lump is outside the file")
    models = []
    for index in range(length // BSP_MODEL_BYTES):
        base = offset + index * BSP_MODEL_BYTES
        values = struct.unpack_from("<9f3i", data, base)
        # CM_LoadMap expands every inline dmodel by one unit before exposing
        # cmodel.mins/maxs to PF_setmodel.  Game edicts therefore receive
        # these expanded local bounds; SV_LinkEdict adds a second, separate
        # one-unit world-link fringe later.  Door size/travel is computed from
        # the expanded local bounds as well.
        mins = [value - 1.0 for value in values[0:3]]
        maxs = [value + 1.0 for value in values[3:6]]
        origin = list(values[6:9])
        if not all(math.isfinite(value) for value in mins + maxs + origin):
            raise ValueError(f"{source}: model {index} has non-finite bounds")
        if any(mins[axis] > maxs[axis] for axis in range(3)):
            raise ValueError(f"{source}: model {index} has inverted bounds")
        models.append({"index": index, "mins": mins, "maxs": maxs,
                       "origin": origin})
    return models


MAX_TOKEN_CHARS = 128
SPAWNFLAG_NOT_EASY = 0x00000100
SPAWNFLAG_NOT_MEDIUM = 0x00000200
SPAWNFLAG_NOT_HARD = 0x00000400
SPAWNFLAG_NOT_DEATHMATCH = 0x00000800
SPAWNFLAG_NOT_COOP = 0x00001000
SPAWNFLAG_MODE_MASK = (
    SPAWNFLAG_NOT_EASY | SPAWNFLAG_NOT_MEDIUM | SPAWNFLAG_NOT_HARD |
    SPAWNFLAG_NOT_DEATHMATCH | SPAWNFLAG_NOT_COOP
)
DEATHMATCH_SPAWN_FREES = frozenset(
    (
        "func_group",
        "info_player_coop",
        "info_null",
        "light",
        "misc_deadsoldier",
        "misc_explobox",
        "misc_actor",
        "misc_insane",
        "monster_berserk",
        "monster_gladiator",
        "monster_gunner",
        "monster_infantry",
        "monster_soldier_light",
        "monster_soldier",
        "monster_soldier_ss",
        "monster_tank",
        "monster_tank_commander",
        "monster_medic",
        "monster_flipper",
        "monster_chick",
        "monster_parasite",
        "monster_flyer",
        "monster_brain",
        "monster_floater",
        "monster_hover",
        "monster_mutant",
        "monster_supertank",
        "monster_boss2",
        "monster_boss3_stand",
        "monster_jorg",
        "point_combat",
        "target_goal",
        "target_help",
        "target_lightramp",
        "target_secret",
        "turret_driver",
    )
)

# ``angle`` and ``angles`` both write edict.s.angles.  ED_ParseField applies
# them in textual order, so a normal dictionary containing both keys is not
# enough to reconstruct the runtime value.  Editor-only underscore keys are
# discarded before this private key is installed, which makes collision with
# map data impossible.  The key is stripped from every public report.
_ANGLE_ASSIGNMENT = "__bspmechanisms_angle_assignment"
_SPAWN_CLASSNAME = "__bspmechanisms_spawn_classname"
_ENTITY_INITIALIZED = "__bspmechanisms_entity_initialized"

_LSTRING_FIELDS = frozenset((
    "classname", "model", "target", "targetname", "pathtarget",
    "deathtarget", "killtarget", "combattarget", "message", "team", "map",
    "noise", "item", "gravity", "sky", "nextmap",
))

_C_WHITESPACE = " \t\n\v\f\r"
_C_DECIMAL_MANTISSA = r"(?:(?:[0-9]+(?:\.[0-9]*)?)|(?:\.[0-9]+))"
_C_HEX_MANTISSA = (
    r"0[xX](?:(?:[0-9a-fA-F]+(?:\.[0-9a-fA-F]*)?)|"
    r"(?:\.[0-9a-fA-F]+))"
)
_C_DECIMAL_FLOAT = rf"{_C_DECIMAL_MANTISSA}(?:[eE][+-]?[0-9]+)?"
# C strtof/scanf accept a hexadecimal significand without an explicit binary
# exponent (for example ``0x1``).  When p/P is present, its exponent must be
# complete; the scanf helper below enforces that conversion-failure rule.
_C_HEX_FLOAT = rf"{_C_HEX_MANTISSA}(?:[pP][+-]?[0-9]+)?"
_C_SPECIAL_FLOAT = r"(?:[iI][nN][fF](?:[iI][nN][iI][tT][yY])?|[nN][aA][nN](?:\([^)]*\))?)"
_C_FLOAT_PREFIX = re.compile(
    rf"[+-]?(?:{_C_HEX_FLOAT}|{_C_DECIMAL_FLOAT}|{_C_SPECIAL_FLOAT})"
)
_C_DECIMAL_MANTISSA_PREFIX = re.compile(rf"[+-]?{_C_DECIMAL_MANTISSA}")
_C_HEX_MANTISSA_PREFIX = re.compile(rf"[+-]?{_C_HEX_MANTISSA}")
_C_SPECIAL_FLOAT_PREFIX = re.compile(rf"[+-]?{_C_SPECIAL_FLOAT}")

# SpawnItem and the four health wrappers can remove entities according to
# live server cvars.  The inventory has no server process from which to read
# those values, so it names that uncertainty instead of silently pretending
# all item entities survive.  Mechanism entities are still filtered exactly by
# the invariant and field-dependent spawn laws below.
SPAWN_PROFILE = {
    "deathmatch": 1,
    "coop": 0,
    "skill": "unspecified",
    "dmflags": "unresolved",
    "ctfflags": "unresolved",
    "disabled_weps": "unresolved",
}


def _ascii_fold(value: str) -> str:
    """Fold ASCII A-Z exactly, without Unicode ``casefold`` expansion."""

    return "".join(chr(ord(char) + 32) if "A" <= char <= "Z" else char
                   for char in value)


def _ed_new_string(value: str) -> str:
    """Apply this tree's ED_NewString transform to one F_LSTRING value."""

    output: list[str] = []
    index = 0
    while index < len(value):
        if value[index] == "\\" and index + 1 < len(value):
            index += 1
            output.append("\n" if value[index] == "n" else "\\")
        else:
            output.append(value[index])
        index += 1
    return "".join(output)


def _com_tokens(text: str) -> Iterable[str]:
    """Tokenize with the lexical laws of this tree's ``COM_Parse``.

    The C lexer truncates tokens to ``MAX_TOKEN_CHARS - 1`` while continuing
    to consume the complete token.  Unlike the runtime, this offline authority
    rejects unterminated quotes/comments instead of turning a damaged tail into
    a partial entity graph.
    """

    cursor = 0
    length = len(text)
    while True:
        while cursor < length and ord(text[cursor]) <= 32:
            cursor += 1
        if cursor >= length or text[cursor] == "\0":
            return
        if text.startswith("//", cursor):
            newline = text.find("\n", cursor + 2)
            if newline < 0:
                return
            cursor = newline + 1
            continue
        if text.startswith("/*", cursor):
            close = text.find("*/", cursor + 2)
            if close < 0:
                raise ValueError("entity text: EOF inside block comment")
            cursor = close + 2
            continue
        if text[cursor] == '"':
            cursor += 1
            close = text.find('"', cursor)
            nul = text.find("\0", cursor)
            if close < 0 or (nul >= 0 and nul < close):
                raise ValueError("entity text: EOF inside quoted token")
            token = text[cursor:close]
            cursor = close + 1
        else:
            start = cursor
            while (cursor < length and text[cursor] != "\0" and
                   ord(text[cursor]) > 32):
                cursor += 1
            token = text[start:cursor]
        yield token[:MAX_TOKEN_CHARS - 1]


def _parse_entity_text(text: str) -> tuple[list[dict], list[dict]]:
    """Parse the complete entity stream or reject it without partial output."""

    tokens = iter(_com_tokens(text))
    entities: list[dict] = []
    issues: list[dict] = []
    while True:
        try:
            opening = next(tokens)
        except StopIteration:
            break
        if opening != "{":
            raise ValueError(
                f"entity text: found {opening!r} while expecting '{{'"
            )
        entity: dict[str, str] = {}
        key_counts: Counter[str] = Counter()
        initialized = False
        while True:
            try:
                key = next(tokens)
            except StopIteration as error:
                raise ValueError(
                    "entity text: EOF without closing brace"
                ) from error
            if key == "}":
                break
            if key == "{":
                raise ValueError("entity text: nested opening brace")
            try:
                value = next(tokens)
            except StopIteration as error:
                raise ValueError(
                    "entity text: EOF without value or closing brace"
                ) from error
            if value == "}":
                raise ValueError("entity text: closing brace without data")
            if value == "{":
                raise ValueError("entity text: opening brace used as value")
            initialized = True
            # ED_ParseField compares field names through Q_stricmp.  Preserve
            # that runtime identity in the inventory and discard editor-only
            # underscore keys exactly as ED_ParseEdict does.
            canonical_key = _ascii_fold(key)
            if canonical_key.startswith("_"):
                continue
            key_counts[canonical_key] += 1
            entity[canonical_key] = (_ed_new_string(value)
                                     if canonical_key in _LSTRING_FIELDS else value)
            if canonical_key in ("angle", "angles"):
                entity[_ANGLE_ASSIGNMENT] = canonical_key
        duplicates = sorted(key for key, count in key_counts.items()
                            if count > 1)
        if duplicates:
            issues.append({"code": "ENTITY_DUPLICATE_KEYS",
                           "entity": len(entities), "keys": duplicates})
        entity[_ENTITY_INITIALIZED] = initialized
        entities.append(entity)
    return entities, issues


def _spawn_free_reason(entity: dict) -> str | None:
    """Return the synchronous CTF/deathmatch spawn removal, if any."""

    classname = entity.get("classname", "")
    if classname in DEATHMATCH_SPAWN_FREES:
        if classname in ("func_group", "info_null"):
            return "spawn_function_frees_entity"
        return "spawn_function_frees_in_deathmatch"

    # These tests intentionally use field *presence* for pointer-valued C
    # fields.  ED_NewString creates a non-NULL pointer even for an empty quoted
    # string, so Python truthiness would remove entities the engine retains.
    if classname == "path_corner" and "targetname" not in entity:
        return "spawn_function_missing_targetname"
    if classname == "func_explosive":
        speed, _ = _c_atof(entity.get("speed", "0"))
        if speed == 0.0:
            return "spawn_function_zero_speed"
    if classname in ("misc_viper", "misc_strogg_ship") and "target" not in entity:
        return "spawn_function_missing_target"
    if classname == "func_clock":
        flags, _ = _spawnflags(entity)
        count, _ = _c_atoi(entity.get("count", "0"))
        if "target" not in entity:
            return "spawn_function_missing_target"
        if flags & 2 and count == 0:
            return "spawn_function_missing_count"
    if classname == "misc_teleporter" and "target" not in entity:
        return "spawn_function_missing_target"
    if classname == "target_changelevel" and "map" not in entity:
        return "spawn_function_missing_map"
    if classname == "trigger_gravity" and "gravity" not in entity:
        return "spawn_function_missing_gravity"
    return None


def _profile_dependent_spawn_entities(
        entities: Sequence[dict | None]) -> list[dict]:
    """Name entities whose SpawnItem survival depends on unavailable cvars."""

    exact = {
        "item_adrenaline", "item_ancient_head", "item_breather",
        "item_enviro", "item_invulnerability", "item_power_screen",
        "item_power_shield", "item_quad", "item_silencer",
    }
    rows = []
    for index, entity in enumerate(entities):
        if entity is None:
            continue
        classname = entity.get("classname", "")
        if (classname in exact or classname.startswith("ammo_") or
                classname.startswith("weapon_") or
                classname.startswith("item_armor") or
                classname.startswith("item_health")):
            rows.append({"entity": index, "classname": classname,
                         "reason": "runtime_item_cvars_unavailable"})
    return rows


def _postspawn_deathmatch_entities(
        parsed: Sequence[dict]) -> tuple[list[dict | None], list[dict]]:
    """Apply the spawn-time removals that affect a CTF mechanism graph."""

    entities: list[dict | None] = [dict(entity) for entity in parsed]
    removed: list[dict] = []
    for index in range(1, len(entities)):
        entity = entities[index]
        assert entity is not None
        if not entity.get(_ENTITY_INITIALIZED, False):
            removed.append({"entity": index, "classname": "",
                            "reason": "empty_entity_block_clears_edict"})
            entities[index] = None
            continue
        if "classname" not in entity:
            entity["classname"] = "noclass"
        flags, _ = _spawnflags(entity)
        if flags & SPAWNFLAG_NOT_DEATHMATCH:
            removed.append({"entity": index,
                            "classname": entity.get("classname", ""),
                            "reason": "not_deathmatch"})
            entities[index] = None
            continue
        flags &= ~SPAWNFLAG_MODE_MASK
        if entity.get("classname") == "trigger_once" and flags & 1:
            # Compatibility rewrite performed by SP_trigger_once.
            flags = (flags & ~1) | 4
        if "spawnflags" in entity or flags:
            entity["spawnflags"] = str(flags)
        reason = _spawn_free_reason(entity)
        if reason is not None:
            removed.append({"entity": index,
                            "classname": entity.get("classname", ""),
                            "reason": reason})
            entities[index] = None
            continue
        spawn_classname = entity.get("classname")
        runtime_aliases = {
            "item_flag_team1": "info_flag_red",
            "item_flag_team2": "info_flag_blue",
            "info_player_team1": "info_player_red",
            "info_player_team2": "info_player_blue",
            "func_door_secret": "func_door",
        }
        if spawn_classname in runtime_aliases:
            # These spawn functions rewrite classname after ED_ParseEdict.
            # Preserve the dispatch identity privately so secret-door
            # callbacks are never mistaken for the ordinary door controller.
            entity[_SPAWN_CLASSNAME] = spawn_classname
            entity["classname"] = runtime_aliases[spawn_classname]
        if spawn_classname == "func_water":
            # SP_func_water installs door_use and rewrites classname so stock
            # translating-door movement callbacks are reused.  Keep its spawn
            # identity privately because it never spawns an automatic trigger.
            entity[_SPAWN_CLASSNAME] = "func_water"
            entity["classname"] = "func_door"
    return entities, removed


def _c_atoi(value: str) -> tuple[int, bool]:
    """Model ED_ParseField's F_INT ``atoi`` conversion.

    The engine accepts a numeric prefix and silently maps a missing prefix to
    zero; trailing text is not a parse error.  Values outside the signed-int
    domain remain unsupported because C ``atoi`` overflow is not portable.
    """

    match = re.match(r"^[ \t\n\v\f\r]*([+-]?[0-9]+)", value)
    if not match:
        return 0, True
    result = int(match.group(1))
    return result, -(1 << 31) <= result < (1 << 31)


def _c_atof(value: str) -> tuple[float, bool]:
    """Model an F_FLOAT/F_ANGLEHACK ``atof`` stored in a C float."""

    stripped = value.lstrip(_C_WHITESPACE)
    match = _C_FLOAT_PREFIX.match(stripped)
    if not match:
        return 0.0, True
    try:
        token = match.group(0)
        unsigned = token.lower().lstrip("+-")
        if unsigned.startswith("nan"):
            parsed = math.nan
        elif unsigned.startswith("inf"):
            parsed = -math.inf if token.startswith("-") else math.inf
        elif "0x" in token.lower():
            parsed = float.fromhex(token)
        else:
            parsed = float(token)
        result = struct.unpack("<f", struct.pack("<f", parsed))[0]
    except (OverflowError, struct.error, ValueError):
        return 0.0, False
    return result, math.isfinite(result)


def _scanf_float_prefix(value: str, position: int) -> tuple[float, int] | None:
    """Parse one glibc ``sscanf(..., "%f", ...)`` input item.

    ``scanf`` differs from ``atof`` at an introduced but incomplete exponent:
    ``1e`` and ``0x1p+`` fail the conversion instead of accepting the preceding
    mantissa.  A hexadecimal introducer also commits the conversion, so
    ``0x 2`` fails rather than falling back to decimal zero.
    """

    sign_position = position + (1 if (position < len(value) and
                                      value[position] in "+-") else 0)
    special = _C_SPECIAL_FLOAT_PREFIX.match(value, position)
    if special is not None:
        end = special.end()
        token = special.group(0)
    elif value[sign_position:sign_position + 2].lower() == "0x":
        mantissa = _C_HEX_MANTISSA_PREFIX.match(value, position)
        if mantissa is None:
            return None
        end = mantissa.end()
        if end < len(value) and value[end] in "pP":
            exponent = re.match(r"[pP][+-]?[0-9]+", value[end:])
            if exponent is None:
                return None
            end += exponent.end()
        token = value[position:end]
    else:
        mantissa = _C_DECIMAL_MANTISSA_PREFIX.match(value, position)
        if mantissa is None:
            return None
        end = mantissa.end()
        if end < len(value) and value[end] in "eE":
            exponent = re.match(r"[eE][+-]?[0-9]+", value[end:])
            if exponent is None:
                return None
            end += exponent.end()
        token = value[position:end]

    try:
        unsigned = token.lower().lstrip("+-")
        if unsigned.startswith("nan"):
            parsed = math.nan
        elif unsigned.startswith("inf"):
            parsed = -math.inf if token.startswith("-") else math.inf
        elif "0x" in unsigned:
            parsed = float.fromhex(token)
        else:
            parsed = float(token)
        component = struct.unpack("<f", struct.pack("<f", parsed))[0]
    except (OverflowError, struct.error, ValueError):
        return None
    return component, end


def _q_name(value: str) -> str:
    """Canonical key for names compared by G_Find/Q_stricmp."""

    return _ascii_fold(value)


def _vec3(value: str | None) -> tuple[list[float] | None, bool]:
    """Model ``sscanf(value, "%f %f %f", vec...)`` with zero fill.

    Quake II initializes the vector to zero and accepts any positive scanf
    conversion count.  Each conversion consumes only its numeric prefix, so
    one/two-component vectors are valid and suffix text after the final
    successful conversion is ignored.
    """

    if value is None:
        return [0.0, 0.0, 0.0], True
    result = [0.0, 0.0, 0.0]
    position = 0
    converted = 0
    for axis in range(3):
        while position < len(value) and value[position] in _C_WHITESPACE:
            position += 1
        scanned = _scanf_float_prefix(value, position)
        if scanned is None:
            break
        component, position = scanned
        if not math.isfinite(component):
            return None, False
        result[axis] = component
        converted += 1
    if converted == 0:
        return None, False
    return result, True


def _spawnflags(entity: dict) -> tuple[int, bool]:
    if "spawnflags" not in entity:
        return 0, True
    return _c_atoi(entity["spawnflags"])


def _entity_angles(entity: dict) -> tuple[list[float] | None, bool]:
    assignment = entity.get(_ANGLE_ASSIGNMENT)
    if assignment == "angles":
        return _vec3(entity["angles"])
    if assignment == "angle":
        yaw, valid = _c_atof(entity["angle"])
        return ([0.0, yaw, 0.0], True) if valid else (None, False)
    if "angles" in entity:
        return _vec3(entity["angles"])
    if "angle" in entity:
        yaw, valid = _c_atof(entity["angle"])
        return ([0.0, yaw, 0.0], True) if valid else (None, False)
    return [0.0, 0.0, 0.0], True


def _move_direction(angles: Sequence[float]) -> list[float]:
    if list(angles) == [0.0, -1.0, 0.0]:
        return [0.0, 0.0, 1.0]
    if list(angles) == [0.0, -2.0, 0.0]:
        return [0.0, 0.0, -1.0]
    yaw = math.radians(angles[1])
    pitch = math.radians(angles[0])
    return [math.cos(pitch) * math.cos(yaw),
            math.cos(pitch) * math.sin(yaw),
            -math.sin(pitch)]


def _entity_bounds(entity: dict, models: Sequence[dict]) -> tuple[list[list[float]] | None, str]:
    raw_model = entity.get("model", "")
    if not raw_model.startswith("*"):
        return None, "missing_brush_model"
    model_index, model_valid = _c_atoi(raw_model[1:])
    if not model_valid or model_index < 1 or model_index >= len(models):
        return None, "brush_model_out_of_range"
    origin, valid = _vec3(entity.get("origin"))
    if not valid or origin is None:
        return None, "malformed_origin"
    model = models[model_index]
    classname = entity.get("classname", "")
    spawn_classname = entity.get(_SPAWN_CLASSNAME, classname)
    flags, flags_valid = _spawnflags(entity)
    if not flags_valid:
        return None, "malformed_spawnflags"

    status = "bsp_model_with_link_fringe"
    rotated = False
    if classname == "func_door" and flags & 1:
        angles, angles_valid = _entity_angles(entity)
        if not angles_valid or angles is None:
            return None, "malformed_angles"
        movedir = _move_direction(angles)
        # ``lip`` is an F_INT spawn-temp field.  ED_ParseField therefore uses
        # atoi, including its numeric-prefix behavior, before SP_func_door
        # applies the zero default.
        lip, _ = _c_atoi(entity.get("lip", "0"))
        if lip == 0 and spawn_classname != "func_water":
            lip = 8
        size = [model["maxs"][axis] - model["mins"][axis]
                for axis in range(3)]
        distance = sum(abs(movedir[axis]) * size[axis]
                       for axis in range(3)) - lip
        origin = [origin[axis] + distance * movedir[axis]
                  for axis in range(3)]
        status = "bsp_model_start_open_with_link_fringe"
    elif classname == "func_door_rotating" and flags & 1:
        # ``distance`` is also F_INT/atoi, despite representing degrees after
        # parsing.  A non-numeric value becomes zero and receives stock 90.
        distance, _ = _c_atoi(entity.get("distance", "0"))
        if distance == 0:
            distance = 90
        # SP_func_door_rotating clears map angles, then START_OPEN installs
        # pos2 as the current angle.  A nonzero angle makes SV_LinkEdict use
        # its conservative radius cube rather than untranslated model bounds.
        rotated = distance != 0.0
        status = "bsp_model_rotated_radius_with_link_fringe"

    # SV_LinkEdict adds a one-unit conservative fringe before the generated
    # door trigger unions absmin/absmax and expands XY by another 60 units.
    if rotated:
        radius = max(abs(value)
                     for value in model["mins"] + model["maxs"])
        mins = [origin[axis] - radius - 1.0 for axis in range(3)]
        maxs = [origin[axis] + radius + 1.0 for axis in range(3)]
    else:
        mins = [origin[axis] + model["mins"][axis] - 1.0 for axis in range(3)]
        maxs = [origin[axis] + model["maxs"][axis] + 1.0 for axis in range(3)]
    return [mins, maxs], status


def _entity_rows(entities: Sequence[dict | None]) -> list[dict]:
    return [
        {"index": index, "classname": entity.get("classname", ""),
         "keys": {key: entity[key] for key in sorted(entity)
                  if not key.startswith("__bspmechanisms_")}}
        for index, entity in enumerate(entities) if entity is not None
    ]


def _target_graph(entities: Sequence[dict | None]) -> tuple[dict[str, list[int]], list[dict], list[dict]]:
    targetnames: dict[str, list[int]] = defaultdict(list)
    for index, entity in enumerate(entities):
        if entity is not None and "targetname" in entity:
            targetnames[_q_name(entity["targetname"])].append(index)

    references: list[dict] = []
    edges: list[dict] = []
    for source, entity in enumerate(entities):
        if entity is None:
            continue
        for kind in ("target", "killtarget"):
            if kind not in entity:
                continue
            name = entity[kind]
            destinations = list(targetnames.get(_q_name(name), ()))
            if not destinations:
                status = "missing"
            elif len(destinations) == 1:
                status = "self" if destinations[0] == source else "unique"
            else:
                status = "fanout"
            references.append({"source": source, "kind": kind, "name": name,
                               "destinations": destinations, "status": status})
            edges.extend({"source": source, "kind": kind, "name": name,
                          "destination": destination}
                         for destination in destinations)
    references.sort(key=lambda row: (row["source"], row["kind"], row["name"]))
    edges.sort(key=lambda row: (row["source"], row["kind"], row["destination"]))
    return dict(targetnames), references, edges


def _activation_mode(entity: dict) -> str:
    classname = entity.get("classname", "")
    if classname in ("trigger_multiple", "trigger_once"):
        flags, _ = _spawnflags(entity)
        if flags & 4:
            return ("enable_only_for_player" if flags & 2 else
                    "enable_then_touch")
        if flags & 2:
            return "remote_only_for_player"
        return "touch"
    if classname == "func_button":
        health, _ = _c_atoi(entity.get("health", "0"))
        if health:
            return "shoot"
        return "remote" if "targetname" in entity else "touch"
    return "remote"


def _activation_closures(entities: Sequence[dict | None], targetnames: dict[str, list[int]],
                         door_team_by_entity: dict[int, str],
                         door_teams: Sequence[dict]) -> list[dict]:
    door_team_by_id = {team["id"]: team for team in door_teams}
    closures: list[dict] = []
    for source, entity in enumerate(entities):
        if entity is None or entity.get("classname") not in ACTIVATOR_CLASSES:
            continue
        visited: set[int] = set()
        terminals: set[int] = set()
        enabled: set[int] = set()
        killed: set[int] = set()
        delayed: set[int] = set()
        missing: set[tuple[int, str]] = set()
        suppressed: set[tuple[int, str]] = set()
        cycles: set[tuple[int, ...]] = set()
        opened_door_masters: set[int] = set()
        available = {index for index, current in enumerate(entities)
                     if current is not None}
        operations = 0
        operation_limit = max(64, len(entities) * 8)

        def walk(node: int, path: tuple[int, ...], targeted_use: bool) -> None:
            nonlocal operations
            operations += 1
            if operations > operation_limit:
                missing.add((node, "<activation limit>"))
                return
            if node not in available:
                return
            visited.add(node)
            current = entities[node]
            assert current is not None
            current_class = current.get("classname", "")

            # trigger_enable is the initial use function for TRIGGERED
            # trigger_multiple/once.  Its first targeted use only links and
            # enables the field; it does not call G_UseTargets.
            flags, _ = _spawnflags(current)
            if (targeted_use and current_class in
                    ("trigger_multiple", "trigger_once") and flags & 4 and
                    node not in enabled):
                enabled.add(node)
                return

            # G_UseTargets schedules a DelayedUse and returns before either
            # killtarget deletion or target fanout.  Preserve that temporal
            # boundary instead of letting future deletion suppress siblings
            # that fire during the current fanout.  Delayed closures remain
            # explicitly unsupported until a timed activation plan models
            # their later event.
            delay, delay_valid = _c_atof(current.get("delay", "0"))
            if not delay_valid:
                missing.add((node, "<nonfinite delay>"))
                return
            if delay != 0.0:
                # G_UseTargets always allocates DelayedUse when delay is
                # nonzero, but that future callback is inert unless at least
                # one copied payload pointer exists.  Door motion has already
                # started before its member calls G_UseTargets, so an inert
                # delay must not downgrade an otherwise complete closure.
                payload_fields = (("target", "killtarget")
                                  if node in door_team_by_entity else
                                  ("message", "target", "killtarget"))
                if any(field in current for field in payload_fields):
                    delayed.add(node)
                return

            if "killtarget" in current:
                kill_name = current["killtarget"]
                victims = [destination for destination in
                           targetnames.get(_q_name(kill_name), ())
                           if destination in available]
                for victim in victims:
                    available.remove(victim)
                    killed.add(victim)
                    terminals.discard(victim)
                    enabled.discard(victim)
                    # G_UseTargets checks self->inuse after each G_FreeEdict
                    # found by G_Find.  If this source was one victim, later
                    # matches are never visited.
                    if victim == node:
                        return

            # A NULL target is a legal G_UseTargets no-op.  In particular,
            # killtarget-only relays finish successfully after deleting their
            # victims; only a declared target that resolves nowhere is broken.
            if "target" not in current:
                return
            target = current["target"]
            canonical_target = _q_name(target)
            declared_destinations = targetnames.get(canonical_target, ())
            if not declared_destinations:
                missing.add((node, target))
                return
            last_destination = -1
            fired = False
            while True:
                candidates = [destination for destination in declared_destinations
                              if destination in available and
                              destination > last_destination]
                if not candidates:
                    break
                destination = min(candidates)
                last_destination = destination
                fired = True
                # G_UseTargets warns and skips an exact self destination, but
                # continues the remaining targetname fanout.
                if destination == node:
                    continue
                if destination in path:
                    start = path.index(destination)
                    cycles.add(path[start:] + (destination,))
                    continue
                destination_entity = entities[destination]
                assert destination_entity is not None
                if destination in door_team_by_entity:
                    terminals.add(destination)
                    team = door_team_by_id[door_team_by_entity[destination]]
                    # door_use ignores direct uses of FL_TEAMSLAVE.  A master
                    # use opens each team member; the first door_go_up for each
                    # publishes that member's own target/killtarget chain.
                    if (destination == team["master_entity"] and
                            destination not in opened_door_masters):
                        opened_door_masters.add(destination)
                        for member in team["members"]:
                            if member not in available:
                                # G_FreeEdict clears this member's own
                                # teamchain link.  door_use reaches the dead
                                # slot from its predecessor, then stops; later
                                # members are no longer callable.
                                break
                            member_path = path + (destination,)
                            if member != destination:
                                member_path += (member,)
                            walk(member, member_path, False)
                            if member not in available:
                                # G_FreeEdict clears this member's teamchain;
                                # door_use cannot advance to the next member.
                                break
                elif destination_entity.get("classname") in ACTIVATOR_CLASSES:
                    walk(destination, path + (destination,), True)
                else:
                    terminals.add(destination)
                if node not in available:
                    return
            if not fired:
                suppressed.add((node, target))

        walk(source, (source,), False)
        terminals.intersection_update(available)
        if cycles and (missing or terminals or enabled or delayed):
            status = "partial"
        elif cycles:
            status = "cyclic"
        elif missing and (terminals or enabled or delayed):
            status = "partial"
        elif missing:
            status = "malformed"
        elif terminals and delayed:
            status = "partial"
        elif terminals:
            status = "resolved"
        elif delayed:
            status = "delayed"
        elif enabled:
            status = "gated"
        elif suppressed or killed:
            status = "suppressed"
        else:
            status = "empty"
        terminal_list = sorted(terminals)
        closures.append({
            "id": f"closure-{source:04d}",
            "source_entity": source,
            "source_class": entity.get("classname", ""),
            "activation_mode": _activation_mode(entity),
            "status": status,
            "activation_entities": sorted(visited),
            "terminal_entities": terminal_list,
            "enabled_entities": sorted(enabled),
            "killed_entities": sorted(killed),
            "delayed_entities": sorted(delayed),
            "door_team_ids": sorted({door_team_by_entity[index]
                                     for index in terminal_list
                                     if index in door_team_by_entity}),
            "missing_targets": [
                {"entity": index, "target": target}
                for index, target in sorted(missing)
            ],
            "suppressed_targets": [
                {"entity": index, "target": target}
                for index, target in sorted(suppressed)
            ],
            "cycles": [list(cycle) for cycle in sorted(cycles)],
        })
    return closures


def _ordinary_door_entity(entity: dict | None) -> bool:
    """True only for stock door classes admitted by this inventory."""

    return bool(entity is not None and
                entity.get("classname") in DOOR_CLASSES and
                entity.get(_SPAWN_CLASSNAME) != "func_door_secret")


def _door_teams(entities: Sequence[dict | None], models: Sequence[dict]) -> tuple[list[dict], dict[int, str], list[dict]]:
    team_members: dict[str, list[int]] = defaultdict(list)
    # G_FindTeams starts at edict 1; worldspawn (edict 0) never participates.
    for index, entity in enumerate(entities[1:], 1):
        if entity is not None and "team" in entity:
            team_members[entity["team"]].append(index)

    raw_groups: dict[tuple[str, str | int], list[int]] = {}
    for index, entity in enumerate(entities):
        if not _ordinary_door_entity(entity):
            continue
        if "team" in entity:
            key: tuple[str, str | int] = ("team", entity["team"])
            raw_groups[key] = list(team_members[entity["team"]])
        else:
            raw_groups[("entity", index)] = [index]

    ordered_groups = sorted(raw_groups.items(), key=lambda item: min(item[1]))
    teams: list[dict] = []
    by_entity: dict[int, str] = {}
    issues: list[dict] = []
    for ordinal, (group_key, members) in enumerate(ordered_groups):
        team_id = f"door-team-{ordinal:04d}"
        master = members[0]
        door_members = [index for index in members
                        if _ordinary_door_entity(entities[index])]
        for index in door_members:
            by_entity[index] = team_id
        master_entity = entities[master]
        assert master_entity is not None
        master_class = master_entity.get("classname", "")
        master_spawn_class = master_entity.get(_SPAWN_CLASSNAME, master_class)
        shootable_members = []
        for member in members:
            member_entity = entities[member]
            assert member_entity is not None
            member_health, _ = _c_atoi(member_entity.get("health", "0"))
            if (member_health and
                    member_entity.get(_SPAWN_CLASSNAME) != "func_water"):
                shootable_members.append(member)
        health, health_valid = _c_atoi(master_entity.get("health", "0"))
        if "health" in master_entity and not health_valid:
            issues.append({"code": "DOOR_MALFORMED_HEALTH", "entity": master,
                           "value": master_entity["health"]})
        if master_spawn_class == "func_water":
            activation = ("external" if "targetname" in master_entity else
                          "unaddressable")
        elif health:
            activation = "shoot"
        elif "targetname" in master_entity:
            activation = "external"
        else:
            activation = "automatic_touch"

        bounds: list[list[float]] | None = None
        bound_statuses: list[dict] = []
        for index in members:
            member_entity = entities[index]
            assert member_entity is not None
            member_bounds, status = _entity_bounds(member_entity, models)
            bound_statuses.append({"entity": index, "status": status})
            if member_bounds is None:
                bounds = None
                break
            if bounds is None:
                bounds = [list(member_bounds[0]), list(member_bounds[1])]
            else:
                for axis in range(3):
                    bounds[0][axis] = min(bounds[0][axis], member_bounds[0][axis])
                    bounds[1][axis] = max(bounds[1][axis], member_bounds[1][axis])

        automatic_trigger = None
        if (activation == "automatic_touch" and
                _ordinary_door_entity(master_entity) and
                master_spawn_class != "func_water"):
            trigger_bounds = None
            if bounds is not None:
                trigger_bounds = [list(bounds[0]), list(bounds[1])]
                trigger_bounds[0][0] -= 60.0
                trigger_bounds[0][1] -= 60.0
                trigger_bounds[1][0] += 60.0
                trigger_bounds[1][1] += 60.0
                # gi.linkentity adds the synthesized trigger edict's own
                # one-unit world fringe after its mins/maxs are assigned.
                for axis in range(3):
                    trigger_bounds[0][axis] -= 1.0
                    trigger_bounds[1][axis] += 1.0
            automatic_trigger = {
                "owner_entity": master,
                "member_entities": list(members),
                "bounds": trigger_bounds,
                "bounds_status": ("live_abs_bounds_with_member_and_trigger_link_fringe"
                                  if trigger_bounds is not None else
                                  "unresolved"),
                "xy_expansion": 60.0,
                "member_link_fringe": 1.0,
                "trigger_link_fringe": 1.0,
            }

        teams.append({
            "id": team_id,
            "team": group_key[1] if group_key[0] == "team" else None,
            "master_entity": master,
            "master_class": master_class,
            "master_spawn_class": master_spawn_class,
            "members": list(members),
            "door_members": door_members,
            "member_classes": [entities[index].get("classname", "")
                               for index in members if entities[index] is not None],
            "member_spawn_classes": [
                entities[index].get(_SPAWN_CLASSNAME,
                                    entities[index].get("classname", ""))
                for index in members if entities[index] is not None
            ],
            "canonical_master_is_door": _ordinary_door_entity(master_entity),
            "mixed_classes": any(entities[index] is None or
                                 not _ordinary_door_entity(entities[index])
                                 for index in members),
            "activation": activation,
            "shootable_members": shootable_members,
            "targetnames": [
                {"entity": index, "targetname": entities[index]["targetname"]}
                for index in members if entities[index] is not None and
                "targetname" in entities[index]
            ],
            "model_bounds": bounds,
            "member_bound_status": bound_statuses,
            "automatic_trigger_candidate": automatic_trigger,
            "activation_closure_ids": [],
        })
    return teams, by_entity, issues


def _train_route(entity_index: int, entity: dict, entities: Sequence[dict | None],
                 targetnames: dict[str, list[int]]) -> dict:
    target = entity.get("target")
    route: list[int] = []
    teleports: list[int] = []
    ambiguities: list[dict] = []
    malformed: list[dict] = []
    cycle: list[int] = []
    seen: dict[int, int] = {}
    status = "resolved"
    initial_target = True
    immediate_teleport = False
    if target is None:
        return {"status": "malformed", "path_corners": [],
                "waypoint_classes": [], "teleport_corners": [],
                "cycle": [], "ambiguities": [],
                "malformed": [{"entity": entity_index, "reason": "missing_target"}]}

    for _ in range(len(entities) + 1):
        matches = list(targetnames.get(_q_name(target), ()))
        if not matches:
            malformed.append({"entity": entity_index if not route else route[-1],
                              "reason": "missing_path_target", "target": target})
            status = "malformed"
            break
        if len(matches) > 1:
            ambiguities.append({"target": target, "entities": matches})
            status = "ambiguous"
            break
        corner = matches[0]
        if corner in seen:
            cycle = route[seen[corner]:] + [corner]
            repeated = entities[corner]
            if repeated is None:
                malformed.append({"entity": corner, "reason": "not_inuse"})
                status = "malformed"
                break
            repeated_flags, repeated_valid = _spawnflags(repeated)
            if not repeated_valid:
                malformed.append({"entity": corner,
                                  "reason": "malformed_spawnflags"})
                status = "malformed"
                break
            self_target = ("target" in repeated and
                           _q_name(repeated["target"]) == _q_name(target))
            if repeated_flags & 1 and (immediate_teleport or self_target):
                malformed.append({"entity": corner,
                                  "reason": "connected_teleport_waypoints"})
                status = "halted"
            else:
                # A revisit after a completed non-teleport move is the
                # intended perpetual func_train route.
                status = "looping"
            break
        corner_entity = entities[corner]
        if corner_entity is None:
            malformed.append({"entity": corner, "reason": "not_inuse"})
            status = "malformed"
            break
        seen[corner] = len(route)
        route.append(corner)
        flags, valid = _spawnflags(corner_entity)
        if not valid:
            malformed.append({"entity": corner,
                              "reason": "malformed_spawnflags"})
            status = "malformed"
            break
        if not initial_target and flags & 1:
            if immediate_teleport:
                malformed.append({"entity": corner,
                                  "reason": "connected_teleport_waypoints"})
                status = "halted"
                break
            teleports.append(corner)
            immediate_teleport = True
        else:
            # func_train_find places the train at its first target without
            # applying train_next's teleport flag.  Every non-teleport move
            # also ends the current train_next call and resets `first`.
            immediate_teleport = False
        initial_target = False
        target = corner_entity.get("target")
        if target is None:
            break
    else:
        malformed.append({"entity": entity_index, "reason": "route_limit"})
        status = "malformed"
    return {"status": status, "path_corners": route,
            "waypoint_classes": [
                entities[index].get("classname", "")
                for index in route if entities[index] is not None
            ],
            "teleport_corners": teleports, "cycle": cycle,
            "ambiguities": ambiguities, "malformed": malformed}


def _mechanisms(entities: Sequence[dict | None], targetnames: dict[str, list[int]]) -> dict:
    plats: list[dict] = []
    trains: list[dict] = []
    elevators: list[dict] = []
    pushes: list[dict] = []
    teleporters: list[dict] = []
    flags: list[dict] = []
    objectives: list[dict] = []
    activation_initiators: list[dict] = []

    inbound_targets: dict[int, list[int]] = defaultdict(list)
    for source, entity in enumerate(entities):
        if entity is not None and "target" in entity:
            for destination in targetnames.get(_q_name(entity["target"]), ()):
                inbound_targets[destination].append(source)

    for index, entity in enumerate(entities):
        if entity is None:
            continue
        classname = entity.get("classname", "")
        origin, origin_valid = _vec3(entity.get("origin"))
        initiator_event = CONTEXTUAL_ACTIVATION_EVENTS.get(classname)
        if (initiator_event is None and
                classname.startswith(("item_", "weapon_", "ammo_")) and
                "target" in entity):
            initiator_event = "item_pickup"
        if initiator_event is not None:
            references = []
            for field in ("target", "pathtarget"):
                if field not in entity:
                    continue
                references.append({
                    "field": field,
                    "name": entity[field],
                    "destinations": list(targetnames.get(
                        _q_name(entity[field]), ())),
                })
            activation_initiators.append({
                "entity": index,
                "classname": classname,
                "event": initiator_event,
                "references": references,
                "status": "unmodeled_contextual_activation",
            })
        if classname == "func_plat":
            plats.append({"entity": index,
                          "activation": ("external_then_center_touch"
                                         if "targetname" in entity else
                                         "center_touch"),
                          "targetname": entity.get("targetname"),
                          "origin": origin if origin_valid else None})
        elif classname == "func_train":
            trains.append({"entity": index, "target": entity.get("target"),
                           "targetname": entity.get("targetname"),
                           "route": _train_route(index, entity, entities,
                                                 targetnames)})
        elif classname == "trigger_elevator":
            if "target" not in entity:
                matches = []
                resolution = "missing_target"
            else:
                # Empty is not NULL: ED_NewString installs a real pointer and
                # G_PickTarget searches for an empty targetname.
                matches = list(targetnames.get(_q_name(entity["target"]), ()))
                if not matches:
                    resolution = "missing"
                elif len(matches) > 1:
                    resolution = "ambiguous"
                elif (entities[matches[0]] is None or
                      entities[matches[0]].get("classname") != "func_train"):
                    resolution = "not_train"
                else:
                    resolution = "resolved"
            callers = []
            for caller in sorted(inbound_targets.get(index, ())):
                caller_entity = entities[caller]
                assert caller_entity is not None
                pathtarget = caller_entity.get("pathtarget")
                destinations = (list(targetnames.get(_q_name(pathtarget), ()))
                                if pathtarget is not None else [])
                callers.append({"entity": caller, "pathtarget": pathtarget,
                                "destinations": destinations,
                                "status": ("missing_pathtarget" if pathtarget is None else
                                           "missing" if not destinations else
                                           "ambiguous" if len(destinations) > 1 else
                                           "resolved")})
            elevators.append({"entity": index, "target": entity.get("target"),
                              "train_entities": matches,
                              "resolution": resolution, "callers": callers})
        elif classname == "trigger_push":
            flags_value, flags_valid = _spawnflags(entity)
            pushes.append({"entity": index, "origin": origin if origin_valid else None,
                           "angles": entity.get("angles", entity.get("angle")),
                           "speed": entity.get("speed", "1000"),
                           "spawnflags": flags_value if flags_valid else None})
        elif classname == "misc_teleporter":
            destinations = list(targetnames.get(
                _q_name(entity.get("target", "")), ()))
            if not destinations:
                status = "missing"
            elif len(destinations) > 1:
                status = "ambiguous"
            else:
                status = "resolved"
            teleporters.append({"entity": index, "target": entity.get("target"),
                                "destinations": destinations,
                                "selected_destination": (destinations[0]
                                                         if len(destinations) == 1
                                                         else None),
                                "status": status,
                                "origin": origin if origin_valid else None})
        elif "teleport" in classname and classname != "misc_teleporter_dest":
            teleporters.append({"entity": index, "target": entity.get("target"),
                                "destinations": list(targetnames.get(
                                    _q_name(entity.get("target", "")), ())),
                                "status": "unknown_teleporter_class",
                                "origin": origin if origin_valid else None})

        if classname in FLAG_TEAMS:
            flags.append({"entity": index, "team": FLAG_TEAMS[classname],
                          "classname": classname,
                          "origin": origin if origin_valid else None,
                          "origin_status": "valid" if origin_valid else "malformed"})
        if classname in OBJECTIVE_CLASSES:
            objectives.append({"entity": index, "classname": classname,
                               "target": entity.get("target"),
                               "targetname": entity.get("targetname"),
                               "origin": origin if origin_valid else None})

    return {"plats": plats, "trains": trains,
            "trigger_elevators": elevators, "trigger_push": pushes,
            "teleporters": teleporters, "flags": flags,
            "objectives": objectives,
            "activation_initiators": activation_initiators}


def _unsupported_reasons(entities: Sequence[dict | None], door_teams: Sequence[dict],
                         closures: Sequence[dict], mechanisms: dict) -> list[dict]:
    grouped: dict[str, set[int]] = defaultdict(set)
    detail: dict[str, str] = {}

    def add(code: str, entity_ids: Iterable[int], explanation: str) -> None:
        grouped[code].update(entity_ids)
        detail[code] = explanation

    closure_by_team: dict[str, list[dict]] = defaultdict(list)
    for closure in closures:
        for team_id in closure.get("door_master_team_ids", ()):
            closure_by_team[team_id].append(closure)

    for team in door_teams:
        master = team["master_entity"]
        if not team["canonical_master_is_door"]:
            add("DOOR_TEAM_NON_DOOR_MASTER", team["members"],
                "G_FindTeams selects a non-door master for a door-containing team")
        if team["mixed_classes"]:
            add("DOOR_TEAM_MIXED_CLASSES", team["members"],
                "door teamchain contains a non-door entity")
        if team["activation"] == "shoot":
            add("SHOOTABLE_DOOR", [master],
                "door activation requires projectile/damage semantics")
        elif team["activation"] == "external":
            physical = [closure for closure in closure_by_team[team["id"]]
                        if closure["activation_mode"] in ("touch", "shoot")]
            if not physical:
                add("EXTERNAL_DOOR_NO_DIRECT_CLOSURE", [master],
                    "no touchable/shootable trigger_multiple or button closure reaches the master")
            elif any(closure["status"] != "resolved" for closure in physical):
                add("EXTERNAL_DOOR_PARTIAL_CLOSURE", [master],
                    "a physical activation closure is missing a target or contains a cycle")
        elif team["activation"] == "unaddressable":
            add("UNADDRESSABLE_WATER_DOOR", [master],
                "func_water has no targetname and never receives an automatic touch trigger")
        if team.get("shootable_members"):
            add("SHOOTABLE_DOOR_MEMBER", team["shootable_members"],
                "one or more door team members also admit damage activation")
        candidate = team["automatic_trigger_candidate"]
        if candidate is not None and candidate["bounds"] is None:
            add("AUTO_DOOR_TRIGGER_BOUNDS_UNRESOLVED", team["members"],
                "automatic door trigger geometry could not be reconstructed from BSP models")

    secret = [index for index, entity in enumerate(entities)
              if entity is not None and
              entity.get(_SPAWN_CLASSNAME) == "func_door_secret"]
    if secret:
        add("SECRET_DOOR", secret, "func_door_secret has distinct activation/motion laws")
    if mechanisms["plats"]:
        add("PLATFORM", (row["entity"] for row in mechanisms["plats"]),
            "func_plat requires center-trigger and bidirectional ride knowledge")
    if mechanisms["trains"]:
        add("TRAIN", (row["entity"] for row in mechanisms["trains"]),
            "func_train requires path-corner timing and ride knowledge")
    if mechanisms["trigger_elevators"]:
        add("TRIGGER_ELEVATOR",
            (row["entity"] for row in mechanisms["trigger_elevators"]),
            "trigger_elevator selects train stops through caller pathtarget")
    if mechanisms["trigger_push"]:
        add("TRIGGER_PUSH", (row["entity"] for row in mechanisms["trigger_push"]),
            "trigger_push changes player velocity on touch")
    if mechanisms["activation_initiators"]:
        add("UNMODELED_ACTIVATION_INITIATOR",
            (row["entity"] for row in mechanisms["activation_initiators"]),
            "conditional or timed G_UseTargets source is inventoried but has no controller law")
    if mechanisms["teleporters"]:
        add("TELEPORTER", (row["entity"] for row in mechanisms["teleporters"]),
            "teleporter traversal requires source/destination staging")

    teams_present = Counter(row["team"] for row in mechanisms["flags"])
    if not teams_present["red"]:
        add("MISSING_RED_FLAG", [], "no red CTF objective entity is declared")
    if not teams_present["blue"]:
        add("MISSING_BLUE_FLAG", [], "no blue CTF objective entity is declared")
    if teams_present["red"] > 1:
        add("AMBIGUOUS_RED_FLAG", (row["entity"] for row in mechanisms["flags"]
                                  if row["team"] == "red"),
            "multiple red CTF objective entities are declared")
    if teams_present["blue"] > 1:
        add("AMBIGUOUS_BLUE_FLAG", (row["entity"] for row in mechanisms["flags"]
                                   if row["team"] == "blue"),
            "multiple blue CTF objective entities are declared")

    return [{"code": code, "entities": sorted(grouped[code]),
             "detail": detail[code]} for code in sorted(grouped)]


def _graph_issues(entities: Sequence[dict | None], references: Sequence[dict],
                  closures: Sequence[dict], door_teams: Sequence[dict],
                  mechanisms: dict) -> list[dict]:
    issues: list[dict] = []
    for index, entity in enumerate(entities):
        if entity is None:
            continue
        if "classname" not in entity:
            issues.append({"code": "ENTITY_MISSING_CLASSNAME", "entity": index})
        for key in ("target", "targetname", "killtarget"):
            if key in entity and entity[key] == "":
                issues.append({"code": "ENTITY_EMPTY_REFERENCE_NAME",
                               "entity": index, "key": key})
    for reference in references:
        if reference["status"] == "missing":
            issues.append({"code": ("TARGET_MISSING" if reference["kind"] == "target"
                                    else "KILLTARGET_MISSING"),
                           "entity": reference["source"],
                           "name": reference["name"]})
        elif reference["status"] == "self":
            issues.append({"code": "TARGET_SELF_REFERENCE",
                           "entity": reference["source"],
                           "name": reference["name"]})
    for closure in closures:
        if closure.get("delayed_entities"):
            issues.append({"code": "ACTIVATION_CHAIN_DELAYED",
                           "entity": closure["source_entity"],
                           "delayed_entities": closure["delayed_entities"]})
        if closure["cycles"]:
            issues.append({"code": "ACTIVATION_CYCLE",
                           "entity": closure["source_entity"],
                           "cycles": closure["cycles"]})
        if closure["missing_targets"]:
            issues.append({"code": "ACTIVATION_CHAIN_MALFORMED",
                           "entity": closure["source_entity"],
                           "missing_targets": closure["missing_targets"]})
        opened_master_teams = set(closure.get("door_master_team_ids", ()))
        slave_terminals = [row for row in closure.get("door_terminals", ())
                           if row["role"] == "slave" and
                           row["door_team_id"] not in opened_master_teams]
        if slave_terminals:
            issues.append({"code": "ACTIVATION_TARGETS_DOOR_SLAVE",
                           "entity": closure["source_entity"],
                           "door_terminals": slave_terminals})
    for team in door_teams:
        if not team["canonical_master_is_door"]:
            issues.append({"code": "DOOR_TEAM_NON_DOOR_MASTER",
                           "entity": team["master_entity"],
                           "members": team["members"]})
        if team["mixed_classes"]:
            issues.append({"code": "DOOR_TEAM_MIXED_CLASSES",
                           "entity": team["master_entity"],
                           "members": team["members"]})
        for row in team["member_bound_status"]:
            if row["status"] not in (
                    "bsp_model_with_link_fringe",
                    "bsp_model_start_open_with_link_fringe",
                    "bsp_model_rotated_radius_with_link_fringe"):
                issues.append({"code": "DOOR_MODEL_BOUNDS_UNRESOLVED",
                               "entity": row["entity"], "reason": row["status"]})
    for train in mechanisms["trains"]:
        route = train["route"]
        if route["status"] in ("ambiguous", "malformed", "halted"):
            issues.append({"code": f"TRAIN_ROUTE_{route['status'].upper()}",
                           "entity": train["entity"]})
    for elevator in mechanisms["trigger_elevators"]:
        if elevator["resolution"] != "resolved":
            issues.append({"code": "ELEVATOR_TARGET_" +
                           elevator["resolution"].upper(),
                           "entity": elevator["entity"]})
        for caller in elevator["callers"]:
            if caller["status"] != "resolved":
                issues.append({"code": "ELEVATOR_CALLER_" +
                               caller["status"].upper(),
                               "entity": caller["entity"]})
    for teleporter in mechanisms["teleporters"]:
        if teleporter["status"] != "resolved":
            issues.append({"code": "TELEPORTER_TARGET_" +
                           teleporter["status"].upper(),
                           "entity": teleporter["entity"]})
    for flag in mechanisms["flags"]:
        if flag["origin_status"] != "valid":
            issues.append({"code": "FLAG_ORIGIN_MALFORMED",
                           "entity": flag["entity"]})
    return issues


def _map_counts(entities: Sequence[dict | None], edges: Sequence[dict],
                door_teams: Sequence[dict], closures: Sequence[dict],
                mechanisms: dict, issues: Sequence[dict],
                reasons: Sequence[dict]) -> dict:
    return {
        "entities": sum(entity is not None for entity in entities),
        "target_edges": sum(edge["kind"] == "target" for edge in edges),
        "killtarget_edges": sum(edge["kind"] == "killtarget" for edge in edges),
        "door_teams": len(door_teams),
        "door_members": sum(len(team["door_members"]) for team in door_teams),
        "automatic_door_triggers": sum(
            team["automatic_trigger_candidate"] is not None for team in door_teams),
        "external_door_teams": sum(team["activation"] == "external"
                                   for team in door_teams),
        "activation_closures": len(closures),
        "plats": len(mechanisms["plats"]),
        "trains": len(mechanisms["trains"]),
        "trigger_elevators": len(mechanisms["trigger_elevators"]),
        "trigger_push": len(mechanisms["trigger_push"]),
        "teleporters": len(mechanisms["teleporters"]),
        "red_flags": sum(flag["team"] == "red" for flag in mechanisms["flags"]),
        "blue_flags": sum(flag["team"] == "blue" for flag in mechanisms["flags"]),
        "other_objectives": len(mechanisms["objectives"]),
        "issues": len(issues),
        "unsupported_reasons": len(reasons),
    }


def inventory_asset(asset: MapAsset) -> dict:
    text, entity_source, bsp_data, bsp_source, recovery = load_asset(asset)
    models = parse_bsp_models(bsp_data, bsp_source)
    parsed_entities, structural_issues = _parse_entity_text(text)
    if not parsed_entities:
        structural_issues.append({"code": "ENTITY_LUMP_EMPTY"})
    entities, spawn_filtered = _postspawn_deathmatch_entities(parsed_entities)

    class_counts = Counter(entity.get("classname", "")
                           for entity in entities if entity is not None)
    key_counts = Counter(key for entity in entities if entity is not None
                         for key in entity
                         if not key.startswith("__bspmechanisms_"))
    profile_dependent = _profile_dependent_spawn_entities(entities)
    targetnames, references, edges = _target_graph(entities)
    door_teams, door_team_by_entity, door_issues = _door_teams(entities, models)
    closures = _activation_closures(entities, targetnames,
                                    door_team_by_entity, door_teams)
    team_master = {team["id"]: team["master_entity"] for team in door_teams}
    closures_by_team: dict[str, list[str]] = defaultdict(list)
    for closure in closures:
        door_terminals = [
            {"entity": entity, "door_team_id": door_team_by_entity[entity],
             "role": ("master" if entity == team_master[door_team_by_entity[entity]]
                      else "slave")}
            for entity in closure["terminal_entities"]
            if entity in door_team_by_entity
        ]
        closure["door_terminals"] = door_terminals
        closure["door_master_team_ids"] = sorted({
            row["door_team_id"] for row in door_terminals
            if row["role"] == "master"
        })
        for team_id in closure["door_master_team_ids"]:
            closures_by_team[team_id].append(closure["id"])
    for team in door_teams:
        team["activation_closure_ids"] = sorted(closures_by_team[team["id"]])
    mechanisms = _mechanisms(entities, targetnames)
    reasons = _unsupported_reasons(entities, door_teams, closures, mechanisms)
    issues = structural_issues + door_issues + _graph_issues(
        entities, references, closures, door_teams, mechanisms)
    issues.sort(key=lambda issue: json.dumps(issue, sort_keys=True,
                                             separators=(",", ":")))

    targetname_rows = [
        {"name": name, "entities": list(targetnames[name])}
        for name in sorted(targetnames)
    ]
    automatic = [
        {"door_team_id": team["id"], **team["automatic_trigger_candidate"]}
        for team in door_teams if team["automatic_trigger_candidate"] is not None
    ]
    record = {
        "map": asset.map_name,
        "status": "ok",
        "entity_source": entity_source,
        "entity_override": entity_source.lower().endswith(".ent"),
        "entity_lump_recovery": recovery,
        "bsp_source": bsp_source,
        "entity_sha256": hashlib.sha256(text.encode("latin-1")).hexdigest(),
        "bsp_sha256": hashlib.sha256(bsp_data).hexdigest(),
        "class_counts": dict(sorted(class_counts.items())),
        "key_counts": dict(sorted(key_counts.items())),
        "entities": _entity_rows(entities),
        "spawn_filtered_entities": spawn_filtered,
        "spawn_profile": dict(SPAWN_PROFILE),
        "spawn_profile_dependent_entities": profile_dependent,
        "targetnames": targetname_rows,
        "references": references,
        "edges": edges,
        "door_teams": door_teams,
        "automatic_door_trigger_candidates": automatic,
        "activation_closures": closures,
        **mechanisms,
        "issues": issues,
        "unsupported_mechanism_reasons": reasons,
    }
    record["counts"] = _map_counts(
        entities, edges, door_teams, closures, mechanisms, issues, reasons)
    return record


def _error_record(asset: MapAsset, error: Exception) -> dict:
    if asset.gamedir is not None:
        source = f"maps/{asset.map_name}.bsp"
    elif asset.bsp_path is not None:
        source = _canonical_direct_source(asset.bsp_path, asset.source_root)
    else:
        source = asset.map_name
    message = str(error)
    roots = []
    if asset.gamedir is not None:
        roots.append(asset.gamedir.resolve())
    if asset.source_root is not None:
        roots.append(asset.source_root.resolve())
    elif asset.bsp_path is not None:
        roots.append(asset.bsp_path.resolve().parent)
    for root in roots:
        prefix = str(root)
        message = message.replace(prefix + os.sep, "")
        message = message.replace(prefix, ".")
    validation_error = f"{type(error).__name__}: {message}"
    if (asset.map_name.lower() == "lmctf02" and
            "entity lump is outside the file" in message):
        return {
            "map": asset.map_name,
            "status": "retired",
            "source": source,
            "replacement_map": "lmctf02c",
            "disposition": {
                "status": "retired",
                "reason": "malformed_bsp_entity_lump",
                "replacement_map": "lmctf02c",
            },
            "validation_error": validation_error,
            "counts": {},
        }
    return {"map": asset.map_name, "status": "error", "source": source,
            "error": validation_error, "counts": {}}


def summarize(maps: Sequence[dict]) -> dict:
    totals: Counter[str] = Counter()
    class_counts: Counter[str] = Counter()
    issue_counts: Counter[str] = Counter()
    reason_counts: Counter[str] = Counter()
    for record in maps:
        totals.update(record.get("counts", {}))
        class_counts.update(record.get("class_counts", {}))
        issue_counts.update(issue["code"] for issue in record.get("issues", ()))
        reason_counts.update(reason["code"] for reason in
                             record.get("unsupported_mechanism_reasons", ()))
    return {
        "maps": len(maps),
        "maps_active": sum(record["status"] != "retired" for record in maps),
        "maps_ok": sum(record["status"] == "ok" for record in maps),
        "maps_error": sum(record["status"] == "error" for record in maps),
        "maps_retired": sum(record["status"] == "retired" for record in maps),
        "totals": dict(sorted(totals.items())),
        "class_counts": dict(sorted(class_counts.items())),
        "issue_counts": dict(sorted(issue_counts.items())),
        "unsupported_reason_counts": dict(sorted(reason_counts.items())),
    }


def inventory(inputs: Sequence[str | os.PathLike[str]]) -> dict:
    assets = discover_assets(inputs)
    if not assets:
        raise FileNotFoundError("no BSP maps found in the supplied inputs")
    maps = []
    for asset in assets:
        try:
            maps.append(inventory_asset(asset))
        except (OSError, ValueError, struct.error) as error:
            maps.append(_error_record(asset, error))
    maps.sort(key=lambda record: (record["map"].lower(),
                                  record.get("bsp_source", record.get("source", ""))))
    return {"schema": "lmctf-bsp-mechanism-inventory",
            "schema_version": SCHEMA_VERSION,
            "summary": summarize(maps), "maps": maps}


def render_json(report: dict) -> str:
    return json.dumps(report, allow_nan=False, indent=2, sort_keys=True) + "\n"


TSV_COUNT_COLUMNS = (
    "entities", "target_edges", "killtarget_edges", "door_teams",
    "door_members", "automatic_door_triggers", "external_door_teams",
    "activation_closures", "plats", "trains", "trigger_elevators",
    "trigger_push", "teleporters", "red_flags", "blue_flags",
    "other_objectives", "issues", "unsupported_reasons",
)


def render_tsv(report: dict) -> str:
    stream = io.StringIO(newline="")
    columns = ("row_type", "map", "status", "entity_source", "bsp_source",
               *TSV_COUNT_COLUMNS, "reason_codes")
    writer = csv.DictWriter(stream, fieldnames=columns, delimiter="\t",
                            lineterminator="\n", extrasaction="ignore")
    writer.writeheader()
    summary = report["summary"]
    summary_row = {"row_type": "summary", "map": "*",
                   "status": ("error" if summary["maps_error"] else "ok"),
                   "reason_codes": json.dumps(
                       summary["unsupported_reason_counts"], sort_keys=True,
                       separators=(",", ":"))}
    summary_row.update(summary["totals"])
    writer.writerow(summary_row)
    for record in report["maps"]:
        row = {"row_type": "map", "map": record["map"],
               "status": record["status"],
               "entity_source": record.get("entity_source", ""),
               "bsp_source": record.get("bsp_source", record.get("source", "")),
               "reason_codes": ";".join(
                   reason["code"] for reason in
                   record.get("unsupported_mechanism_reasons", ())) }
        row.update(record.get("counts", {}))
        writer.writerow(row)
    return stream.getvalue()


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Inventory BSP triggers, movers, activation chains, and CTF objectives.")
    parser.add_argument("inputs", nargs="+", metavar="BSP_OR_DIRECTORY")
    parser.add_argument("--format", choices=("json", "tsv"), default="json")
    parser.add_argument("--output", "-o", metavar="PATH",
                        help="atomically write output instead of stdout")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        report = inventory(args.inputs)
        output = render_json(report) if args.format == "json" else render_tsv(report)
        if args.output:
            atomic_write_bytes(args.output, output.encode("utf-8"))
        else:
            sys.stdout.write(output)
        return 1 if report["summary"]["maps_error"] else 0
    except (OSError, ValueError) as error:
        print(f"bspmechanisms: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
