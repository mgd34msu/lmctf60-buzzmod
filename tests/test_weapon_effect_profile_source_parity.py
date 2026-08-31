#!/usr/bin/env python3
"""Structural parity checks between host fire code and weapon profiles."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(text: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", text, re.S)
    if match is None:
        raise AssertionError(f"missing function {name}")
    depth = 1
    index = match.end()
    while depth and index < len(text):
        depth += (text[index] == "{") - (text[index] == "}")
        index += 1
    if depth:
        raise AssertionError(f"unterminated function {name}")
    return text[match.start():index]


def profile_entry(catalog: str, profile_id: str) -> str:
    start = catalog.index(f".id = {profile_id}")
    next_entry = catalog.find("\n\t{\n\t\t.id =", start)
    return catalog[start: next_entry if next_entry >= 0 else len(catalog)]


def rail_parity(host: str, catalog: str) -> bool:
    body = function_body(host, "fire_rail")
    calls = re.findall(r"fire_lead\s*\([^;]+;", body)
    if len(calls) != 4:
        return False
    signs = {
        ("SG_HOST_RAILGUN_AUXILIARY_SPREAD", "SG_HOST_RAILGUN_AUXILIARY_SPREAD"),
        ("SG_HOST_RAILGUN_AUXILIARY_SPREAD", "-SG_HOST_RAILGUN_AUXILIARY_SPREAD"),
        ("-SG_HOST_RAILGUN_AUXILIARY_SPREAD", "SG_HOST_RAILGUN_AUXILIARY_SPREAD"),
        ("-SG_HOST_RAILGUN_AUXILIARY_SPREAD", "-SG_HOST_RAILGUN_AUXILIARY_SPREAD"),
    }
    seen = set()
    for call in calls:
        if "SG_HOST_RAILGUN_AUXILIARY_DAMAGE" not in call:
            return False
        match = re.search(
            r"TE_GUNSHOT,\s*(-?SG_HOST_RAILGUN_AUXILIARY_SPREAD),\s*"
            r"(-?SG_HOST_RAILGUN_AUXILIARY_SPREAD)", call)
        if match is None:
            return False
        seen.add(match.groups())
    entry = profile_entry(catalog, "SG_WEAPON_PROFILE_RAILGUN")
    required = (
        "SG_HOST_RAILGUN_AUXILIARY_TRACE_COUNT",
        "SG_HOST_RAILGUN_AUXILIARY_DAMAGE",
        "SG_HOST_RAILGUN_AUXILIARY_SPREAD",
    )
    return seen == signs and all(token in catalog for token in required) \
        and ".auxiliary_traces_penetrate" not in entry


def splash_parity(combat: str, weapon: str, catalog: str) -> bool:
    radius = function_body(combat, "T_RadiusDamage")
    bfg = function_body(weapon, "bfg_explode")
    return all(token in radius for token in (
        "damage - 0.5 * VectorLength (v)",
        "1 - (vlength / radius)",
        "points = damage * adjuster",
        "ent == attacker",
        "SG_HOST_RADIUS_SELF_SCALE",
    )) and all(token in bfg for token in (
        "if (ent == self->owner)",
        "continue;",
        "sqrt(dist/self->dmg_radius)",
    )) and all(token in catalog for token in (
        "SG_WEAPON_SPLASH_LINEAR_HALF_DISTANCE",
        "SG_WEAPON_SPLASH_NORMALIZED_LINEAR",
        "SG_WEAPON_SPLASH_SQRT_NORMALIZED",
        "SG_WEAPON_SPLASH_OWNER_EXCLUDED",
    ))


def plasma_parity(plasma: str, player: str, catalog: str) -> bool:
    ready = function_body(plasma, "Weapon_PLASMA_Generic")
    fire = function_body(player, "weapon_plasma_fire")
    debit = function_body(plasma, "fire_plasma")
    entry = profile_entry(catalog, "SG_WEAPON_PROFILE_PLASMA_REFLECT")
    return "pers.weapon->quantity" in ready \
        and "inventory[ent->client->ammo_index] < 1" in fire \
        and len(re.findall(r"PLASMA_CELLS_PER_SHOT\s*-\s*1", debit)) == 2 \
        and "quadmeister" in function_body(plasma, "plasma_reflect_touch") \
        and "AMMO_LAW(PLASMA_CELLS_PER_SHOT, 1U" in entry \
        and "PLASMA_CELLS_PER_SHOT - 1U" in entry \
        and "SG_WEAPON_DAMAGE_IMPACT_GLOBAL_QUAD" in entry


def hook_parity(player: str, catalog: str) -> bool:
    entry = profile_entry(catalog, "SG_WEAPON_PROFILE_HOOK")
    return all(token in player for token in (
        "#define GRAPPLE_FIRE_HOOK_SPEED        SG_HOST_HOOK_FIRE_SPEED",
        "#define GRAPPLE_PULL_SPEED             SG_HOST_HOOK_PULL_SPEED",
        "#define GRAPPLE_PULL_BALANCED_SPEED    SG_HOST_HOOK_PULL_SPEED",
    )) and all(token in entry for token in (
        ".projectile_speed = SG_HOST_HOOK_FIRE_SPEED",
        ".hook_pull_speed = SG_HOST_HOOK_PULL_SPEED",
        ".hook_initial_damage = SG_HOST_HOOK_INITIAL_DAMAGE",
        ".hook_attached_damage = SG_HOST_HOOK_ATTACHED_DAMAGE",
        ".hook_health = SG_HOST_HOOK_HEALTH",
    ))


class WeaponEffectProfileSourceParityTest(unittest.TestCase):
    def setUp(self) -> None:
        self.player = source("p_weapon.c")
        self.weapon = source("g_weapon.c")
        self.combat = source("g_combat.c")
        self.local = source("g_local.h")
        self.plasma = source("plasma.c")
        self.catalog = source("slipgate/sg_weapon_effect_profile.c")

    def test_each_host_weapon_has_exact_frame_law(self) -> None:
        expected = {
            "Weapon_Blaster": (4, 8, 52, 55, "5U, 5U"),
            "Weapon_Shotgun": (7, 18, 36, 39, "8U, 8U"),
            "Weapon_SuperShotgun": (6, 17, 57, 61, "7U, 7U"),
            "Weapon_Machinegun": (3, 5, 45, 49, "4U, 5U"),
            "Weapon_Chaingun": (4, 31, 61, 64, "5U, 21U"),
            "Weapon_GrenadeLauncher": (5, 16, 59, 64, "6U, 6U"),
            "Weapon_RocketLauncher": (4, 12, 50, 54, "5U, 5U"),
            "Weapon_HyperBlaster": (5, 20, 49, 53, "6U, 11U"),
            "Weapon_Railgun": (3, 18, 56, 61, "4U, 4U"),
            "Weapon_BFG": (8, 32, 55, 58, "SG_HOST_BFG_FIRE_FRAME"),
        }
        for function, (activate, fire, idle, deactivate, effect) in expected.items():
            with self.subTest(function=function):
                body = function_body(self.player, function)
                self.assertRegex(body, rf"Weapon_Generic\s*\(ent,\s*{activate},\s*"
                                      rf"{fire},\s*{idle},\s*{deactivate},")
                prefix = (f"TIMING_LAW({activate}U, {fire}U, {idle}U, "
                          f"{deactivate}U, {effect}")
                self.assertIn(prefix, self.catalog)
        plasma_body = function_body(self.player, "Weapon_Plasma")
        self.assertRegex(plasma_body,
                         r"Weapon_PLASMA_Generic\s*\(ent,\s*3,\s*11,\s*46,\s*51,")
        self.assertIn("TIMING_LAW_NO_FAST(3U, 11U, 46U, 51U, 4U, 4U",
                      self.catalog)
        hook_body = function_body(self.player, "Weapon_Hook")
        self.assertRegex(hook_body,
                         r"Weapon_Generic\s*\(ent,\s*9,\s*13,\s*34,\s*38,")
        self.assertIn("ps.gunframe+=1", hook_body)
        self.assertIn("ps.gunframe = 36", hook_body)
        self.assertIn("HOOK_TIMING_LAW", profile_entry(
            self.catalog, "SG_WEAPON_PROFILE_HOOK"))

    def test_rail_auxiliary_traces_are_separate(self) -> None:
        self.assertTrue(rail_parity(self.weapon, self.catalog))
        damaged = self.weapon.replace(
            "fire_lead (self, start, aimdir, SG_HOST_RAILGUN_AUXILIARY_DAMAGE",
            "fire_lead (self, start, aimdir, 0", 1)
        self.assertFalse(rail_parity(damaged, self.catalog))

    def test_splash_kernels_and_owner_laws_match(self) -> None:
        self.assertTrue(splash_parity(self.combat, self.weapon, self.catalog))
        damaged = self.weapon.replace("sqrt(dist/self->dmg_radius)",
                                      "dist/self->dmg_radius", 1)
        self.assertFalse(splash_parity(self.combat, damaged, self.catalog))

    def test_plasma_affordability_debit_and_quad_dependency_match(self) -> None:
        self.assertTrue(plasma_parity(self.plasma, self.player, self.catalog))
        damaged = self.player.replace(
            "inventory[ent->client->ammo_index] < 1",
            "inventory[ent->client->ammo_index] < PLASMA_CELLS_PER_SHOT")
        self.assertFalse(plasma_parity(self.plasma, damaged, self.catalog))

    def test_deathmatch_branches_use_shared_law_constants(self) -> None:
        checks = (
            ("Weapon_Blaster_Fire", "SG_HOST_BLASTER_NON_DM_DAMAGE"),
            ("Weapon_HyperBlaster_Fire", "SG_HOST_HYPERBLASTER_NON_DM_DAMAGE"),
            ("Chaingun_Fire", "SG_HOST_CHAINGUN_NON_DM_DAMAGE"),
            ("weapon_railgun_fire", "SG_HOST_RAILGUN_NON_DM_DAMAGE"),
            ("weapon_bfg_fire", "SG_HOST_BFG_NON_DM_DAMAGE"),
        )
        for function, constant in checks:
            with self.subTest(function=function):
                body = function_body(self.player, function)
                self.assertIn("deathmatch->value", body)
                self.assertIn(constant, body)
                self.assertIn(constant, self.catalog)
        self.assertIn("SG_HOST_BFG_NON_DM_PERIODIC_RAY_DAMAGE",
                      function_body(self.weapon, "bfg_think"))

    def test_hand_grenade_and_hook_have_physical_profiles(self) -> None:
        grenade = profile_entry(self.catalog, "SG_WEAPON_PROFILE_HAND_GRENADE")
        hook = profile_entry(self.catalog, "SG_WEAPON_PROFILE_HOOK")
        for constant in ("SG_HOST_HAND_GRENADE_DAMAGE",
                         "SG_HOST_HAND_GRENADE_MAX_SPEED",
                         "SG_HOST_HAND_GRENADE_COOK_MS"):
            self.assertIn(constant, self.player)
            self.assertIn(constant, grenade)
        self.assertIn("SG_HOST_HAND_GRENADE_MIN_SPEED", self.catalog)
        self.assertTrue(hook_parity(self.player, self.catalog))
        damaged = self.player.replace(
            "#define GRAPPLE_PULL_SPEED             SG_HOST_HOOK_PULL_SPEED",
            "#define GRAPPLE_PULL_SPEED             799", 1)
        self.assertFalse(hook_parity(damaged, self.catalog))

    def test_random_and_release_endpoints_are_not_clipped(self) -> None:
        rocket = profile_entry(self.catalog, "SG_WEAPON_PROFILE_ROCKET_LAUNCHER")
        grenade = profile_entry(self.catalog, "SG_WEAPON_PROFILE_HAND_GRENADE")
        rocket_fire = function_body(self.player, "Weapon_RocketLauncher_Fire")
        grenade_fire = function_body(self.player, "weapon_grenade_fire")
        grenade_state = function_body(self.player, "Weapon_Grenade")

        self.assertIn("(rand () & 0x7fff) / ((float)0x7fff)", self.local)
        self.assertIn("(int)(random() * SG_HOST_ROCKET_DAMAGE_RANDOM_SPAN)",
                      rocket_fire)
        self.assertIn("SG_HOST_ROCKET_DAMAGE_RANDOM_SPAN", rocket)
        self.assertNotIn("SG_HOST_ROCKET_DAMAGE_RANDOM_SPAN - 1", rocket)
        self.assertIn("level.time + GRENADE_TIMER + 0.2", grenade_state)
        self.assertIn("ent->client->ps.gunframe == 12", grenade_state)
        self.assertIn("HAND_GRENADE_FIRST_RELEASE_SPEED", grenade)
        self.assertIn("HAND_GRENADE_FIRST_RELEASE_FUSE_MS", grenade)
        self.assertIn("speed = GRENADE_MINSPEED + (GRENADE_TIMER - timer)",
                      grenade_fire)

    def test_catalog_has_no_tactical_area_denial_fact(self) -> None:
        self.assertNotIn("SG_WEAPON_EFFECT_AREA_DENIAL", self.catalog)
        header = source("slipgate/sg_weapon_effect_profile.h")
        self.assertNotIn("SG_WEAPON_EFFECT_AREA_DENIAL", header)
        self.assertNotIn("UINT32_C(1) << 6", header)


if __name__ == "__main__":
    unittest.main()
