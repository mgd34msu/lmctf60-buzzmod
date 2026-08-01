# WEAPONS.md — LMCTF 6 mechanics dossier and bot combat doctrine

Everything in Part 1 is read out of this repository. Every number carries a
`file:line`. Where a number could not be established it says **UNKNOWN** and
names where it was looked for. Part 2 derives bot rules; every rule points back
at a Part 1 number.

Repository root for all citations: `/home/buzzkill/Projects/lmctf6-stats`.

---

# PART 0 — THE THREE FACTS THAT CHANGE EVERYTHING

Read these before anything else. They invalidate a lot of what someone who
knows stock Quake II CTF would assume about this mod.

## 0.1 `CTF_WEAP_BALANCE` is dead code. All of it.

`q_shared.h:1058-1061`:

```c
// ctfflags->value flags
#ifdef WEAP_BALANCE_OK
#define CTF_WEAP_BALANCE		1
#endif
```

`WEAP_BALANCE_OK` is **never defined**. It appears exactly once in the entire
tree, in that `#ifdef` itself. Neither build system defines it:

* `Makefile:22` — `CFLAGS ?= -DVER='"$(VER)"' -std=c11 -O0 -fno-strict-aliasing -g -Wall -MMD $(INCLUDES)`
* `GNUmakefile:93` — `CFLAGS=-O3 -DARCH="$(ARCH)" -DSTDC_HEADERS -DVER='"$(VER)"'`
* `gravity.vcxproj` — `PreprocessorDefinitions>WIN32;NDEBUG;_WINDOWS;C_ONLY`

Every use of `CTF_WEAP_BALANCE` in `.c` files is itself wrapped in
`#ifdef WEAP_BALANCE_OK`. Verified for all of them:

| Site | Guard |
|---|---|
| `g_weapon.c:351` (blaster beam), `:737` (rail shrapnel), `:907` (BFG laser dmg) | `:348`, `:736`, `:906` |
| `p_weapon.c:797, 854, 884, 965, 1034, 1095, 1155, 1361, 1480, 1544, 1608, 1684, 1694, 1736, 2074` | each preceded by `#ifdef WEAP_BALANCE_OK` |
| `g_combat.c:233, 250, 272, 497, 721` | `:232, :249, :271, :496, :720` |
| `g_items.c:618, 764` | `:616, :762` |
| `g_cmds.c:1297` | `:1296` |

**Consequence: every weapon in this mod runs at stock id deathmatch values.**
The "surt/LM_Jorm rebalance" comments describe a build configuration that does
not exist. Anyone who tunes bots against those comments will be wrong on
railgun damage (82 vs 100), rocket speed (750 vs 650), rocket splash radius
(240 vs 120), hyperblaster damage (12 vs 15), SSG pellet count (26 vs 20),
shotgun pellets (14 vs 12), BFG damage (180 vs 200) and BFG speed (180 vs 400).

The one exception below — the *shotgun spread* — is a real LMCTF edit that sits
**outside** the `#ifdef` and therefore *is* live. See §1.6.

## 0.2 The test server's `ctfflags 16` is `CTF_OFFHAND_HOOK`, nothing else

`g_save.c:223`:
```c
ctfflags = gi.cvar("ctfflags", "16", CVAR_SERVERINFO);
```
`q_shared.h:1066`: `#define CTF_OFFHAND_HOOK 16`.

At 16 the following are **off**: `CTF_ALLOW_INVULN` (2), `CTF_TEAM_RESET` (4),
`CTF_TEAM_NOSWITCH` (8), `CTF_NO_GRAP_DAMAGE` (64), `CTF_TEAM_NOTEAMS` (128),
`CTF_FLAGS_NOFLAGS` (256), `CTF_SCORE_BALANCE` (512),
`CTF_TEAM_ARMOR_PROTECT` (1024), `CTF_DM_POWER_ARMOR_STRENGTH` (2048),
`CTF_RANDOM_MAPS` (4096), `CTF_RANDOM_QUAD` (8192),
`CTF_RANDOM_DEATH_MSG` (16384), `CTF_VOTEMENU_OFF` (32768)
(`q_shared.h:1063-1081`).

Downstream consequences that matter to a bot:

* **Invulnerability does not exist on the map.** `g_items.c:1360-1369`: with
  `CTF_ALLOW_INVULN` clear, any entity whose `item->use == Use_Invulnerability`
  is `G_FreeEdict`'d at spawn. `sg_fields.c:93` lists
  `"item_invulnerability"` in `SG_FC_POWERUP`; that half of the class will
  always be empty. The powerup field is a quad field.
* **The grapple does damage.** `CTF_NO_GRAP_DAMAGE` is clear, so
  `p_weapon.c:1828` takes the damaging branch.
* **Quad respawn is the fixed 60 s**, not the randomised 50–80 s
  (`g_items.c:1311-1312` is gated on `CTF_RANDOM_QUAD`).
* **The grapple is removed from the weapon cycle.** `g_cmds.c:885-887`
  (`Cmd_WeapPrev_f`) and `g_cmds.c:933-935` (`Cmd_WeapNext_f`) both `continue`
  past `FindItem("Grappling Hook")` when `CTF_OFFHAND_HOOK` is set. The only
  way to make the grapple `pers.weapon` is an explicit `use grappling hook` /
  `use hook` / `use grapple` through `Cmd_Use_f` (`g_cmds.c:659-662`).

## 0.3 Weapon think = exactly one gunframe per 100 ms

`Think_Weapon` (`p_weapon.c:287`) has exactly two call sites, both guarded by
`client->weapon_thunk` so it runs **once per server frame**:

* `p_client.c:2938-2942` — from `ClientThink`, sets `weapon_thunk = true`
* `p_client.c:3151-3154` — from `ClientEndServerFrame`, only `if (!client->weapon_thunk)`

`FRAMETIME` is `0.1f` (`g_local.h:150`), so a server frame is 100 ms and one
gunframe advance is 100 ms. `USE_FPS` variable-tickrate support exists
(`Makefile:141-143`, `ifdef CONFIG_VARIABLE_SERVER_FPS`) but is not enabled in
either default build.

### The refire formula

`Weapon_Generic` (`p_weapon.c:417-604`) with
`FRAME_FIRE_FIRST = FRAME_ACTIVATE_LAST + 1` (`:413`) and
`FRAME_IDLE_FIRST = FRAME_FIRE_LAST + 1` (`:414`):

1. On the think where `WEAPON_READY` sees `BUTTON_ATTACK` (`:501`), it sets
   `gunframe = FRAME_FIRE_FIRST` and `weaponstate = WEAPON_FIRING` (`:507-508`)
   and then **falls through** to the `WEAPON_FIRING` block (`:558`) — so the
   shot happens on that same think.
2. Each subsequent think advances one gunframe (`:574`).
3. `WEAPON_READY` is restored only when `gunframe == FRAME_IDLE_FIRST + 1`
   (`:588-589`), which is a think on which nothing fires.
4. The next think fires again.

```
refire_period = (FRAME_FIRE_LAST − FRAME_FIRE_FIRST + 2) × 100 ms
```

This is one frame longer than the commonly quoted id figures, because the
transition-to-READY think is not a firing think. Verified by hand-trace for
railgun, rocket launcher, shotgun and super shotgun.

`fastswitch` defaults to `"0"` (`g_save.c:235`); with it on, `Weapon_Generic`
skips the activate ramp (`:456-458`) and `ChangeWeapon`s immediately
(`:474-476`, `:575-585`).

---

# PART 1 — MECHANICS DOSSIER

## 1.1 Shared constants

| Constant | Value | Cite |
|---|---|---|
| `FRAMETIME` | `0.1f` | `g_local.h:150` |
| `DAMAGE_TIME` | `0.5f` | `g_local.h:116` |
| `DEFAULT_BULLET_HSPREAD` | 300 | `g_local.h:842` |
| `DEFAULT_BULLET_VSPREAD` | 500 | `g_local.h:843` |
| `DEFAULT_SHOTGUN_HSPREAD` | 1000 | `g_local.h:844` |
| `DEFAULT_SHOTGUN_VSPREAD` | 500 | `g_local.h:845` |
| `DEFAULT_DEATHMATCH_SHOTGUN_COUNT` | 12 | `g_local.h:846` |
| `DEFAULT_SHOTGUN_COUNT` | 12 | `g_local.h:847` |
| `DEFAULT_SSHOTGUN_COUNT` | 20 | `g_local.h:848` |
| `LM_QUAD_DEFAULT_TIME` | 60 | `g_local.h:1513` |
| `GRENADE_TIMER` / `MINSPEED` / `MAXSPEED` | 3.0 s / 400 / 800 | `p_weapon.c:615-617` |
| `GRAPPLE_FIRE_HOOK_SPEED` | 800 | `p_weapon.c:14` |
| `GRAPPLE_PULL_SPEED` | 800 | `p_weapon.c:15` |
| `sv_gravity` default | 800 | `g_save.c:175` |
| `sv_maxvelocity` default | 2000 | `g_save.c:174` |
| player mass | 200 | `p_client.c:1926` |
| player bbox mins | `{-16,-16,-24}` | `p_client.c:1833` |
| player bbox maxs | `{16,16,32}` | `p_client.c:1834` |
| player `viewheight` | 22 | `p_client.c:1923` |

### How spread becomes an angle

`fire_lead` (`g_weapon.c:121-257`) builds the endpoint as

```c
r = crandom()*hspread;                    // g_weapon.c:143
u = crandom()*vspread;                    // g_weapon.c:144
VectorMA (start, 8192, forward, end);     // g_weapon.c:145
VectorMA (end, r, right, end);            // g_weapon.c:146
VectorMA (end, u, up, end);               // g_weapon.c:147
```

So the deviation is a **uniform** offset over a 8192-unit baseline.
`crandom()` is uniform on [−1, 1].

```
max half-angle      = atan(spread / 8192)
lateral offset at d = crandom() * spread * d / 8192
```

| Spread | Max half-angle | Lateral half-width at distance d |
|---|---|---|
| 300 (bullet H) | 2.10° | 0.0366 d |
| 500 (bullet V, shotgun H&V) | 3.49° | 0.0610 d |
| 1000 (SSG H) | 6.97° | 0.1221 d |

Target half-width is 16 units, target half-height 28 units. Therefore the
distance at which the spread cone first exceeds the target:

| Weapon | Horizontal saturation | Vertical saturation |
|---|---|---|
| MG / CG (300/500) | 16·8192/300 = **437 u** | 28·8192/500 = **459 u** |
| Shotgun (500/500) | 16·8192/500 = **262 u** | **459 u** |
| SSG (1000/500) | 16·8192/1000 = **131 u** | **459 u** |

Inside those distances essentially every pellet/bullet lands on a stationary
torso; outside, expected hits fall off as roughly 1/d then 1/d².

### Radius damage

`T_RadiusDamage` (`g_combat.c:698-756`), with `CTF_WEAP_BALANCE` dead, uses the
id formula only — the live `#else` branch at `g_combat.c:742` (the identical
line at `:740` is inside the dead `#ifdef`):

```
points = damage − 0.5 × dist
if (ent == attacker) points *= 0.5           // g_combat.c:745-746
```

`dist` is measured from the inflictor origin to the target's **bbox centre**
(`g_combat.c:716-718`); for a standing player that is origin + (0,0,4).
`findradius` (`g_utils.c:60-83`) also uses bbox centres and **hard-culls**
anything past `radius` — so damage does not taper smoothly to zero, it stops
dead at the radius. `CanDamage` gates it on line of sight (`g_combat.c:749`).

**Self-damage** for any splash weapon is therefore

```
self_damage(d) = 0.5 × (damage − 0.5 d)   for d ≤ dmg_radius
               = 0                         for d >  dmg_radius
```

and self-knockback uses the rocket-jump multiplier (`g_combat.c:491-494`):

```c
if (targ->client && attacker == targ)
    VectorScale(dir, 1600.0 * (float)knockback / mass, kvel);   /* :492 */
else
    VectorScale(dir, 500.0 * (float)knockback / mass, kvel);    /* :494 */
```

With `mass = 200` (`p_client.c:1926`), self-knockback velocity =
`8 × knockback` units/s, and knockback for splash equals `(int)points`
(`g_combat.c:752`). Ground contact clamps upward kick to ≥0
(`g_combat.c:505-508`).

---

## 1.2 Blaster

| Property | Value | Cite |
|---|---|---|
| Damage (deathmatch) | 15 | `p_weapon.c:958-959` |
| Damage (single player) | 10 | `p_weapon.c:961` |
| Projectile speed | 1000 u/s | `p_weapon.c:939` → `g_weapon.c:416` |
| Movetype | `MOVETYPE_FLYMISSILE` (no gravity) | `g_weapon.c:417` |
| Clipmask | `MASK_SHOT` | `g_weapon.c:418` |
| Lifetime | 2.0 s (`nextthink = level.time + 2`) | `g_weapon.c:427` |
| Splash | **none** | `blaster_touch`, `g_weapon.c:297-334` |
| Damage flags | `DAMAGE_ENERGY` | `g_weapon.c:319` |
| Knockback | 1 | `g_weapon.c:319` |
| Ammo | none | `g_items.c:1623-1624` (`quantity` 0, `ammo` NULL) |
| Muzzle offset | `(24, 8, viewheight−8)` | `p_weapon.c:932` |
| View kick | `kick_origin = −2·forward`, `kick_angles[0] = −1` | `p_weapon.c:936-937` |
| Frames | `Weapon_Generic(ent, 4, 8, 52, 55)`, fire `{5}` | `p_weapon.c:989` |
| **Refire** | (8 − 5 + 2) × 0.1 = **0.5 s** | derived, §0.3 |
| **DPS** | 30 | derived |

Every client spawns holding it and it is never removable
(`p_client.c:1147-1151`).

## 1.3 Hyperblaster

Same projectile as the blaster (`Blaster_Fire`, `p_weapon.c:923-951`), fired on
every gunframe of a 6-frame loop.

| Property | Value | Cite |
|---|---|---|
| Damage (deathmatch) | 15 | `p_weapon.c:1028-1029` |
| Damage (single player) | 20 | `p_weapon.c:1031` |
| Projectile speed | 1000 u/s | `p_weapon.c:939` |
| Splash | none | — |
| Damage flags | `DAMAGE_ENERGY`, kb 1 | `g_weapon.c:319` |
| Ammo | Cells, 1 per bolt | `g_items.c:1808-1809`; `p_weapon.c:1040` |
| Frames | `Weapon_Generic(ent, 5, 20, 49, 53)`, fire `{6,7,8,9,10,11}` | `p_weapon.c:1073` |
| Loop | `gunframe++`; at 12 with ammo → back to 6 | `p_weapon.c:1055-1057` |
| **Rate** | 1 bolt / 100 ms while held | derived |
| **DPS / ammo rate** | **150 dps, 10 cells/s** | derived |
| Muzzle rotation | `offset = (−4 sin θ, 0, 4 cos θ)`, `θ = (gunframe−5)·2π/6` | `p_weapon.c:1019-1022` |

Releasing `BUTTON_ATTACK` mid-loop advances one frame per think out of the loop
(`p_weapon.c:1002-1005`). Highest sustained DPS of any energy weapon; also the
fastest cell drain in the mod after the BFG.

## 1.4 Machinegun

| Property | Value | Cite |
|---|---|---|
| Damage per bullet | 8 | `p_weapon.c:1090` |
| Kick | 2 | `p_weapon.c:1091` |
| Type | hitscan (`fire_bullet` → `fire_lead`) | `g_weapon.c:268-271` |
| Spread | H 300 / V 500 | `p_weapon.c:1160` |
| Damage flags | `DAMAGE_BULLET` (normal armour applies) | `g_weapon.c:216` |
| Ammo | Bullets, 1 per bullet | `g_items.c:1693-1694`; `p_weapon.c:1171` |
| Frames | `Weapon_Generic(ent, 3, 5, 45, 49)`, fire `{4,5}` | `p_weapon.c:1191` |
| Loop | gunframe toggles 4↔5 while held | `p_weapon.c:1109-1112` |
| **Rate** | 1 bullet / 100 ms | derived |
| **DPS** | **80** | derived |
| View kick | random ±0.35 origin / ±0.7 angles; `kick_angles[0] = machinegun_shots × −1.5` | `p_weapon.c:1134-1138` |
| Climb | `machinegun_shots` only increments **outside** deathmatch | `p_weapon.c:1141-1146` |

The muzzle climb that makes the machinegun hard to hold on target in single
player is disabled in deathmatch — `machinegun_shots` stays 0, so
`kick_angles[0]` stays 0. This is a stock id behaviour that surprises people.
The MG is a flat, no-climb, 80 dps hitscan out to ~437 units.

## 1.5 Chaingun

| Property | Value | Cite |
|---|---|---|
| Damage per bullet (deathmatch) | 6 | `p_weapon.c:1205-1206` |
| Damage per bullet (single player) | 8 | `p_weapon.c:1208` |
| Kick | 2 | `p_weapon.c:1203` |
| Type | hitscan | `p_weapon.c:1298` |
| Spread | H 300 / V 500 (`DEFAULT_BULLET_*`) | `p_weapon.c:1298` |
| Ammo | Bullets, `shots` per think | `g_items.c:1716-1717`; `p_weapon.c:1310` |
| Frames | `Weapon_Generic(ent, 4, 31, 61, 64)`, fire `{5…21}` | `p_weapon.c:1319` |
| Barrel offset | `r = 7 + crandom()·4`, `u = crandom()·4` | `p_weapon.c:1293-1295` |

Shot schedule (`p_weapon.c:1251-1261`), evaluated **after** the gunframe advance
(`p_weapon.c:1213-1227`):

| Post-advance gunframe | shots/think |
|---|---|
| ≤ 9 | 1 |
| 10–14 | 2 if `BUTTON_ATTACK` held, else 1 |
| ≥ 15 | 3 |

Sustained loop: `gunframe == 21` with attack + ammo resets to 15
(`p_weapon.c:1219-1223`), so the steady state is 7 thinks × 3 shots = **21
bullets per 700 ms = 30 bullets/s = 180 dps, 30 bullets/s ammo drain**.

Spin-up from a cold trigger: 4 thinks at 1 shot, then 5 thinks at 2 shots, then
3/think — about **0.9 s to reach full rate**, ~13 bullets spent getting there.
Spin-down: releasing at gunframe 14 jumps to 32 (`p_weapon.c:1213-1218`).

Highest raw DPS in the mod. Its ammo cost is brutal: a 50-bullet
`ammo_bullets` box is 1.7 s of sustained fire.

## 1.6 Shotgun — **the one live LMCTF rebalance**

| Property | Value | Cite |
|---|---|---|
| Damage per pellet | 4 | `p_weapon.c:1336` |
| Kick per pellet | 8 | `p_weapon.c:1337` |
| Pellet count (deathmatch) | `DEFAULT_DEATHMATCH_SHOTGUN_COUNT + 0` = **12** | `p_weapon.c:1369`, `g_local.h:846` |
| **Spread** | **H 500 / V 500** — hardcoded, *not* `DEFAULT_SHOTGUN_HSPREAD` | `p_weapon.c:1369, 1371` |
| Max damage | 48 | derived |
| Ammo | Shells, 1 | `g_items.c:1647-1648`; `p_weapon.c:1383` |
| Frames | `Weapon_Generic(ent, 7, 18, 36, 39)`, fire `{8,9}` | `p_weapon.c:1515` |
| Frame 9 | advances only, does not fire | `p_weapon.c:1340-1344` |
| **Refire** | (18 − 8 + 2) × 0.1 = **1.2 s** | derived |
| **DPS** | 40 at point blank | derived |
| View kick | `−2·forward`, `kick_angles[0] = −2` | `p_weapon.c:1348-1349` |

**This is the mod's real weapon change.** Stock Quake II passes
`DEFAULT_SHOTGUN_HSPREAD` (1000) to `fire_shotgun`; LMCTF hardcodes 500. The
`count` variable that would have added `+2` pellets is inside the dead
`#ifdef WEAP_BALANCE_OK` (`p_weapon.c:1360-1366`) and stays 0.

Net effect: **the shotgun's horizontal cone is half as wide as stock**, so its
useful range roughly doubles — full 48 damage out to ~262 units instead of
~131. It is a genuinely usable mid-range weapon here.

Expected damage on a stationary torso (uniform-spread model, §1.1):

| d | pellets on target | damage |
|---|---|---|
| ≤ 262 | 12 | 48 |
| 400 | ~7.9 | ~31 |
| 600 | ~4.3 | ~17 |
| 900 | ~1.8 | ~7 |

## 1.7 Super shotgun

| Property | Value | Cite |
|---|---|---|
| Damage per pellet | 6 | `p_weapon.c:1525` |
| Kick per pellet | 12 | `p_weapon.c:1526` |
| Pellet count | `DEFAULT_SSHOTGUN_COUNT/2 + 0` = **10 per barrel, 20 total** | `p_weapon.c:1555, 1558`; `g_local.h:848` |
| Spread | `DEFAULT_SHOTGUN_HSPREAD` 1000 / `DEFAULT_SHOTGUN_VSPREAD` 500 | `p_weapon.c:1555, 1558` |
| Barrel yaw split | `v[YAW] = v_angle[YAW] ∓ 5°` | `p_weapon.c:1552, 1556` |
| Max damage | **120** | derived |
| Ammo | Shells, **2** per shot | `g_items.c:1670-1671`; `p_weapon.c:1570` |
| Frames | `Weapon_Generic(ent, 6, 17, 57, 61)`, fire `{7}` | `p_weapon.c:1578` |
| **Refire** | (17 − 7 + 2) × 0.1 = **1.2 s** | derived |
| **DPS** | 100 at point blank | derived |

The `count = 12 / damage -= 3` rebalance is dead (`p_weapon.c:1543-1549`).

The ±5° split is what really governs SSG range. The two half-volleys' centres
are `2·d·tan(5°) = 0.175 d` apart. Against a 32-unit-wide torso:

| d | volley-centre separation | expected pellets | damage |
|---|---|---|---|
| 64 | 11 u | 20 | 120 |
| 128 | 22 u | ~13 | ~78 |
| 256 | 45 u | ~8 | ~48 |
| 400 | 70 u | ~6 | ~37 |

**SSG is a sub-200-unit weapon.** Past 256 units it is worse than the shotgun
per shell.

## 1.8 Grenade launcher

| Property | Value | Cite |
|---|---|---|
| Damage (direct) | 120 | `p_weapon.c:788` |
| `damage_radius` | `damage + 40` = **160** | `p_weapon.c:791` |
| Radius damage | 120 (`ent->dmg`) | `g_weapon.c:485` |
| Projectile speed | **600 u/s** forward | `p_weapon.c:812` |
| Added velocity | `+ (200 ± 10) · up`, `+ (±10) · right` | `g_weapon.c:556-557` |
| Movetype | `MOVETYPE_BOUNCE` — gravity applies | `g_weapon.c:559` |
| Angular velocity | `(300,300,300)` | `g_weapon.c:558` |
| Fuse | 2.5 s | `p_weapon.c:812` |
| Direct-hit bonus | `points = dmg − 0.5·|origin − targ_centre|`, applied as `T_Damage(..., DAMAGE_RADIUS)` **plus** the full `T_RadiusDamage` | `g_weapon.c:461-485` |
| Ammo | Grenades, 1 | `g_items.c:1762-1763`; `p_weapon.c:824` |
| Frames | `Weapon_Generic(ent, 5, 16, 59, 64)`, fire `{6}` | `p_weapon.c:832` |
| **Refire** | (16 − 6 + 2) × 0.1 = **1.2 s** | derived |
| View kick | `−2·forward`, `kick_angles[0] = −1` | `p_weapon.c:809-810` |
| Muzzle offset | `(8, 8, viewheight−8)` | `p_weapon.c:805` |

`Grenade_Touch` detonates immediately on any `takedamage` entity
(`g_weapon.c:536-537`); on world geometry it bounces and plays a sound
(`g_weapon.c:520-533`).

**Self-damage:** `0.5 × (120 − 0.5 d)` for `d ≤ 160`, then a hard cliff to 0.
At `d = 160` that is still **20 damage** — the grenade launcher does not fade
out, it stops. Min zero-self-damage range: **161 units**.

## 1.9 Hand grenades

| Property | Value | Cite |
|---|---|---|
| Damage | 125 | `p_weapon.c:624` |
| `damage_radius` | `damage + 40` = **165** | `p_weapon.c:629` |
| Speed | `400 + (3.0 − timer)·133.33`, clamped by hold time | `p_weapon.c:638`, consts `:615-617` |
| Added velocity | `+ (200 ± 10)·up`, `+ (±10)·right` | `g_weapon.c:593-594` |
| Fuse set at gunframe 11 | `grenade_time = level.time + 3.0 + 0.2` | `p_weapon.c:726` |
| Cook-off in hand | at `level.time >= grenade_time`, fires with `held = true` → `spawnflags = 3`, `MOD_HELD_GRENADE` | `p_weapon.c:731-736`; `g_weapon.c:610-613`, `:479-480` |
| Throw | gunframe 12 | `p_weapon.c:755-760` |
| Post-throw lockout | `grenade_time = level.time + 1.0` | `p_weapon.c:644` |
| Ammo | Grenades, 1; item quantity **5** on pickup | `g_items.c:1738-1740`; `p_weapon.c:642` |
| Weapon think | custom `Weapon_Grenade`, not `Weapon_Generic` | `p_weapon.c:668-773` |
| Activate | jumps straight to gunframe 16, `WEAPON_READY` | `p_weapon.c:676-681` |

Practical throw speed: pressing and releasing immediately gives `timer ≈ 3.1`,
so `speed ≈ 387 u/s` with a 3.1 s fuse. Cooking to `timer = 0` gives 800 u/s
and instant detonation. Speed and fuse are the **same knob**, inverted.

### Hand-grenade frame walk (`Weapon_Grenade`, `p_weapon.c:668-773`)

`Weapon_Grenade` is not `Weapon_Generic` and its cadence has to be walked by
hand:

| Think | gunframe at entry | what happens |
|---|---|---|
| T1 | idle | `WEAPON_READY` sees the button, sets `gunframe = 1`, `WEAPON_FIRING`, `grenade_time = 0`, and **returns** (`:690-703`) — no fire this think |
| T2–T11 | 1 → 10 | advance one per think (`:765`); pin sound at gunframe 5 (`:719-720`) |
| T12 | 11 | sets `grenade_time = level.time + 3.2` (`:726`); **if `BUTTON_ATTACK` is still held it returns here and cooks** (`:738-739`) |
| T13 | 12 | `weapon_grenade_fire(ent, false)` — the throw (`:755-760`) |
| T14–T16 | 13 → 15 | advance |
| T17 | 16 | `WEAPON_READY` restored (`:767-771`) |

**Refire, trigger tapped (released before T12): 1.5 s** (T13 → T28).
**Trigger-press to throw: 1.2 s.** A bot that holds the button never throws —
it cooks until `level.time >= grenade_time` and detonates in hand
(`:731-736`, `MOD_HELD_GRENADE`).

`weapon_grenade_fire` sets `grenade_time = level.time + 1.0` (`:644`), but the
normal throw path zeroes it one line later (`:759`), so the gunframe-15 wait at
`:762-763` only bites on the cook-off path.

`ChangeWeapon` force-throws a cooking grenade (`p_weapon.c:183-189`).

**Self-damage:** `0.5 × (125 − 0.5 d)` for `d ≤ 165`; 21 damage at the edge.
Min zero-self-damage range: **166 units**.

## 1.10 Rocket launcher

| Property | Value | Cite |
|---|---|---|
| Direct damage | `100 + rand()·20` → **100–119**, mean 109.5 | `p_weapon.c:851` |
| `radius_damage` | **120** | `p_weapon.c:865` (`#else` branch — the live one) |
| `damage_radius` | **120** | `p_weapon.c:866` |
| Projectile speed | **650 u/s** | `p_weapon.c:889` |
| Movetype | `MOVETYPE_FLYMISSILE` — **no gravity, straight line** | `g_weapon.c:695` |
| Lifetime | `8000 / speed` = 12.3 s | `g_weapon.c:704` |
| Direct hit dflags | `0` → **normal armour, not energy** | `g_weapon.c:653` |
| Direct hit knockback | **0** | `g_weapon.c:653` |
| Splash mod | `MOD_R_SPLASH`, knockback = `(int)points` | `g_weapon.c:669`; `g_combat.c:752` |
| Ammo | Rockets, 1 | `g_items.c:1785-1786`; `p_weapon.c:903` |
| Frames | `Weapon_Generic(ent, 4, 12, 50, 54)`, fire `{5}` | `p_weapon.c:911` |
| **Refire** | (12 − 5 + 2) × 0.1 = **0.9 s** | derived |
| View kick | `−2·forward`, `kick_angles[0] = −1` | `p_weapon.c:877-878` |
| Muzzle offset | `(8, 8, viewheight−8)` | `p_weapon.c:880` |

Note that the *direct* hit carries **zero knockback**; all the push comes from
`T_RadiusDamage`. A direct hit on a target at `d ≈ 0` therefore does
`100–119` (direct) `+ (120 − 0.5·d) ≈ 120` (splash) ≈ **220–240 damage** — a
one-shot kill on an unarmoured 100/0 player, and a near-kill through 100 body
armour.

**Self-damage:** `0.5 × (120 − 0.5 d)` for `d ≤ 120`, hard 0 past 120.

| d | self damage | self knockback velocity |
|---|---|---|
| 0 | 60 | 480 u/s |
| 40 | 50 | 400 u/s |
| 80 | 40 | 320 u/s |
| 119 | 30.25 | 242 u/s |
| 121 | **0** | 0 |

Min zero-self-damage range: **121 units**. Note this is *half* the 240 units
someone reading the dead balance code would compute.

## 1.11 Railgun

| Property | Value | Cite |
|---|---|---|
| Damage (deathmatch) | **100** | `p_weapon.c:1619` (`#else` branch) |
| Kick (deathmatch) | **200** | `p_weapon.c:1620` |
| Damage (single player) | 150 / kick 250 | `p_weapon.c:1626-1627` |
| Damage (`MATCH_RAILGUN_INPLAY`) | 5000 / kick 5000 | `p_weapon.c:1599-1603` |
| Type | hitscan, 8192-unit trace, **zero spread** | `g_weapon.c:747` |
| Penetration | keeps tracing through clients, monsters and any `SOLID_BBOX` | `g_weapon.c:752-775` |
| Damage flags | `0` → **normal armour** | `g_weapon.c:771` |
| Ammo | Slugs, 1 | `g_items.c:1831-1832`; `p_weapon.c:1655` |
| Frames | `Weapon_Generic(ent, 3, 18, 56, 61)`, fire `{4}` | `p_weapon.c:1664` |
| **Refire** | (18 − 4 + 2) × 0.1 = **1.6 s** | derived |
| **DPS** | 62.5 | derived |
| View kick | `−3·forward`, `kick_angles[0] = −3` | `p_weapon.c:1638-1639` |
| Muzzle offset | `(0, 7, viewheight−8)` | `p_weapon.c:1641` |

The four `fire_lead` shrapnel calls at `g_weapon.c:739-742` are inside the dead
`#ifdef`. **The railgun is a clean, spreadless, wall-piercing hitscan with a
knockback of 200** (self-hit impossible). It is the only weapon in the mod with
no range degradation whatsoever.

`MATCH_RAILGUN_INPLAY` is a rail-arena match mode: `ChangeWeapon` force-selects
the railgun (`p_weapon.c:175-179`), `Pickup_Weapon` refuses everything
(`p_weapon.c:117-118`), and `Cmd_WeapPrev_f` / `Cmd_WeapNext_f` return early
(`g_cmds.c:865-866`).

## 1.12 BFG10K

| Stage | Effect | Cite |
|---|---|---|
| Launch | `fire_bfg(ent, start, forward, 200, 400, 1000)` in deathmatch | `p_weapon.c:1688-1689`, `:1741` |
| Speed | **400 u/s**, `MOVETYPE_FLYMISSILE` | `g_weapon.c:985-986` |
| Lifetime | `8000/400` = 20 s | `g_weapon.c:995` |
| In-flight lasers | every 100 ms, everything within **256** with LOS takes **5** (deathmatch) `DAMAGE_ENERGY`, kb 1 | `g_weapon.c:901-902`, `:912`, `:943`, `:969` |
| Owner immune to lasers | `if (ent == self->owner) continue;` | `g_weapon.c:917` |
| Impact direct | 200, dflags 0, kb 0 | `g_weapon.c:867` |
| Impact splash | `T_RadiusDamage(self, owner, 200, other, 100)` | `g_weapon.c:868` |
| Detonation wave | after 100 ms, everything within `dmg_radius` 1000 with double `CanDamage`: `points = 200 · (1 − sqrt(dist/1000))`, `DAMAGE_ENERGY` | `g_weapon.c:814-842`, `:997-998` |
| Owner immune to wave | `if (ent == self->owner) continue;` | `g_weapon.c:822` |
| Ammo | Cells, **50** | `g_items.c:1854-1855`; `p_weapon.c:1749` |
| Frames | `Weapon_Generic(ent, 8, 32, 55, 58)`, fire `{9, 17}` | `p_weapon.c:1757` |
| Windup | frame 9 = muzzle flash only; frame 17 = actual launch → **0.8 s delay** | `p_weapon.c:1698-1710` |
| Abort | re-checks `< 50` cells at frame 17 and silently skips | `p_weapon.c:1714-1718` |
| **Cycle** | (32 − 9 + 2) × 0.1 = **2.5 s** | derived |
| View kick | `v_dmg_pitch = −40`, `v_dmg_roll = crandom()·8` | `p_weapon.c:1728-1730` |

The `damage = 180 / damage_radius = 1200 / speed = 180` rebalance is dead
(`p_weapon.c:1683-1696`, `:1735-1739`).

**Self-damage:** only the impact `T_RadiusDamage` can hit the owner
(`ignore = other`, not the owner). `0.5 × (200 − 0.5 d)` for `d ≤ 100` → up to
**100 self damage** at contact. The wave and the lasers both explicitly skip
the owner. Min zero-self-damage range: **101 units**.

## 1.13 Plasma Rifle (SKWiD MOD, LMCTF-specific)

Two firing modes toggled by `use plasma rifle` while it is already held
(`Use_PLASMA`, `plasma.c:353-372`), tracked in `client->plasma_mode`.

| Property | Bounce mode (`plasma_mode != 0`) | Spread mode (`plasma_mode == 0`) | Cite |
|---|---|---|---|
| Projectiles | 1 | 3 (centre, ±10° yaw) | `plasma.c:246-247`, `:286-293` |
| Damage on direct hit | **39** | **28 each** | `plasma.h:12`, `:11`; `plasma.c:54`, `:127` |
| Damage flags | `DAMAGE_ENERGY`, kb 1 | `DAMAGE_ENERGY`, kb 1 | `plasma.c:61`, `:138` |
| Speed | **1200 u/s** | **1200 u/s** | `plasma.h:25-26` |
| Movetype | `MOVETYPE_REFLECT` — no gravity, no friction, bounces off floors | `MOVETYPE_FLYMISSILE` | `plasma.c:212`, `:250-252` |
| Lifetime | 1.5 s | 3.0 s | `plasma.c:233`, `:302-304` |
| Bbox | `±12` (bounce), cleared for spread | | `plasma.c:327-328`, `:254-259` |
| Splash on **miss only** | `T_RadiusDamage(self, owner, 39, NULL, 39+70=109)` | `T_RadiusDamage(self, owner, 28, NULL, 28+70=98)` | `plasma.c:99`, `:158`; `plasma.h:13` |
| Quad | `× 4` via file-static `quadmeister` | same | `plasma.c:51-54`, `:124-127`, `:404-407` |
| Ammo | **10 cells** (`item->quantity = PLASMA_CELLS_PER_SHOT`) | same | `plasma.h:28`; `g_items.c:1877` |
| Ammo debit | 1 in `weapon_plasma_fire` (`p_weapon.c:2195`) + 9 in `fire_plasma` (`plasma.c:334`, `:340`) | same | |
| Frames | `Weapon_PLASMA_Generic(ent, 3, 11, 46, 51)`, fire `{4,5}`; only frame 4 fires | | `p_weapon.c:2225`, `:2176` |
| **Refire** | (11 − 4 + 2) × 0.1 = **0.9 s** | | derived |
| **DPS** | 43 (direct) | up to 93 (all three) | derived |

Three quirks worth flagging:

1. **`ignore` is `NULL` in both splash calls.** The owner is *not* excluded, so
   a plasma that misses can hurt you: `0.5 × (39 − 0.5 d)` up to **19.5** self
   damage in bounce mode, **14** in spread mode.
2. **Splash only fires when the goop misses.** A direct hit does 39 (or 28) and
   nothing else.
3. `quadmeister` is a **file-global `int`** (`plasma.c:26`) written by
   `Weapon_PLASMA_Generic` (`plasma.c:404-407`) — the last plasma-holder to
   think in a frame sets it for everyone. With multiple plasma users and one
   quad this misattributes the 4× multiplier. Recorded as-is; not a bot concern
   but it is a real bug.

`Weapon_PLASMA_Generic` (`plasma.c:401-575`) is a fork of `Weapon_Generic`
without the `fastswitch` handling and without the out-of-ammo auto-switch.

## 1.14 Grappling hook

Two entirely different code paths depending on whether the grapple is
`pers.weapon`.

### The projectile

| Property | Value | Cite |
|---|---|---|
| Speed | **800 u/s** | `p_weapon.c:14`, `:2033` |
| Movetype | `MOVETYPE_FLYMISSILE`, `MASK_SHOT` | `p_weapon.c:1923-1924` |
| Bolt HP | **59** — the hook can be shot down | `p_weapon.c:1937-1938`; `hook_die` `:1904-1908` |
| Lifetime | none — `Grapple_Bolt_Think` only plays sounds and reschedules | `p_weapon.c:1879-1902` |
| First-contact damage | **8** damage, **8** knockback, `DAMAGE_ENERGY`, `MOD_CTF_GRAPPLE` | `p_weapon.c:1849` |
| Sustained damage | **1** damage / **1** knockback every 7 frames (0.7 s) while attached to the same target | `p_weapon.c:1832-1839` |
| Damage gate | skipped if `ctfflags & CTF_NO_GRAP_DAMAGE` (64) — **not set at 16** | `p_weapon.c:1828` |
| Abort conditions | sky surface, teammate, dead target, non-player/non-world/non-func entity | `p_weapon.c:1787-1813` |
| Attach | records `hook_offset = origin − target->absmin`, becomes `SOLID_TRIGGER` and rides the target | `p_weapon.c:1860-1867`, `:2054-2055` |

### The pull

`Weapon_Hook_Fire` (`p_weapon.c:2001-2116`), `hookstate == 2`:

```
speed = |hook_origin − muzzle|      (also stored as client->hooklength)
speed > 120 → velocity = dir · 800  and SV_AddGravity(ent)   // :2071-2082
speed > 100 → velocity = dir · speed·5                        // :2083-2086
speed >  80 → velocity = dir · speed·4                        // :2087-2090
speed >  40 → velocity = dir · speed·3                        // :2091-2094
speed >  20 → velocity = dir · speed·2                        // :2095-2098
speed >  10 → velocity = dir · speed·1                        // :2099-2102
```

`oldvelocity` is overwritten each frame, which is what suppresses fall damage
(`p_weapon.c:2105`). While `hookstate == 2 && hooklength < 50`, pmove gravity is
forced to 0 (`p_client.c:2834-2837`).

### Offhand vs in-hand — the `BUTTON_ATTACK` conflict

`Cmd_Hook_f` (`g_cmds.c:1392-1437`), with `CTF_OFFHAND_HOOK` set:

```c
it = FindItem("Grappling Hook");
// Can't offhand your hook if it is your current weapon
if (ent->client->pers.weapon == it)     // g_cmds.c:1408
{
    ForceCommand(ent, "+attack\n");     // g_cmds.c:1410
    return;
}
...
Weapon_Hook_Fire(ent);                  // g_cmds.c:1419
```

`Cmd_Unhook_f` mirrors it with `-attack` (`g_cmds.c:1448-1450`).
`Weapon_Hook` itself aborts the rope the instant `BUTTON_ATTACK` is not held
(`p_weapon.c:2139-2142`), then hands off to
`Weapon_Generic(ent, 9, 13, 34, 38, ..., Weapon_Hook_Fire)` (`p_weapon.c:2144`).

**So: with the grapple as `pers.weapon`, `BUTTON_ATTACK` *is* the rope.**

**But** — and this is the load-bearing detail the doctrine turns on — the rope
is sustained by `ClientEndServerFrame`, not by the trigger:

```c
if (ent->client->hookstate)     // p_view.c:988
{
    Weapon_Hook_Fire(ent);      // p_view.c:990
}
```

So an **offhand** hook (fired via `Cmd_Hook_f` while the grapple is *not*
`pers.weapon`) keeps pulling every server frame with no button input at all,
which leaves `BUTTON_ATTACK` free for the held weapon. **A bot can grapple and
shoot at the same time, provided the grapple is never `pers.weapon`.**

`Cmd_Hook_f`'s offhand branch only calls `Weapon_Hook_Fire` when
`!ent->client->hook` (`g_cmds.c:1402`), so the "hook" command is a one-shot
launcher, not a held input. Release is `cmd unhook` → `ctf_hook_abort`
(`g_cmds.c:1453`, `g_ctffunc.c:1134-1172`), which frees the bolt, zeroes
`hookstate`/`hooklength`, and cancels fall damage if grounded.

Command dispatch: `"hook"` → `Cmd_Hook_f`, `"unhook"` → `Cmd_Unhook_f`
(`g_cmds.c:2782-2785`).

Item: `"weapon_hook"`, pickup name `"Grappling Hook"`, `IT_WEAPON`,
`weapmodel WEAP_HOOK`, no ammo, `weaponthink = Weapon_Hook`
(`g_items.c:2491-2511`). Granted to every client at spawn
(`p_client.c:1141-1144`).

## 1.15 Dead / unreachable weapon code

* **`fire_fieldgun` / `weapon_fieldgun_fire`** (`p_weapon.c:1386-1507`) — a
  hook-length-driven exploding shotgun. **Never called.** There is no
  `weapon_fieldgun` entry in `itemlist` and no `weaponthink` pointer to
  `weapon_fieldgun_fire`. Verified by `grep -rn fieldgun *.c *.h`.
* All `#ifdef WEAP_BALANCE_OK` blocks (§0.1).
* `CTF_NOVOICE` — guarded by an equally undefined `NOVOICE_OK`
  (`q_shared.h:1068-1070`).
* `MONSTERS_OK` — undefined; `check_dodge`'s body (`g_weapon.c:31-39`) and
  `M_ReactToDamage` (`g_combat.c:323-398`) compile out.

## 1.16 The engine's own weapon fallback ladder

`NoAmmoWeaponChange` (`p_weapon.c:239-278`), called whenever a weapon runs dry:

```
railgun (slugs) → hyperblaster (cells) → chaingun (bullets)
→ machinegun (bullets) → super shotgun (shells>1) → shotgun (shells)
→ blaster
```

Note what is **absent**: rocket launcher, grenade launcher, BFG, plasma, hand
grenades. Running a rocket launcher dry drops you to the railgun if you have
slugs, otherwise all the way down. A bot must not rely on this ladder to keep
its best weapon up.

`Weapon_Generic` also force-switches mid-burst the moment ammo drops below
`pers.weapon->quantity` (`p_weapon.c:592-601`, the "bat" block) — which for the
super shotgun means it bails at 1 shell, and for the BFG at 49 cells.

## 1.17 Armor

`g_items.c:37-39`:

```c
gitem_armor_t jacketarmor_info = {  25,  50, .30f, .00, ARMOR_JACKET };
gitem_armor_t combatarmor_info = {  50, 100, .60f, .30f, ARMOR_COMBAT };
gitem_armor_t bodyarmor_info   = { 100, 200, .80f, .60f, ARMOR_BODY   };
```

Fields are `{ base_count, max_count, normal_protection, energy_protection, armor }`.

| Item | base | max | normal | energy | respawn |
|---|---|---|---|---|---|
| Jacket Armor | 25 | 50 | 0.30 | **0.00** | 20 s |
| Combat Armor | 50 | 100 | 0.60 | 0.30 | 20 s |
| Body Armor | 100 | 200 | 0.80 | 0.60 | 20 s |
| Armor Shard | +2 (sets jacket to 2 if none held) | — | inherits carrier's class | | 20 s |

Respawn: `SetRespawn(ent, 20)` (`g_items.c:759`). Item entries at
`g_items.c:1467-1555`.

### Absorption math — `CheckArmor` (`g_combat.c:283-321`)

```c
if (dflags & DAMAGE_ENERGY)
    save = ceilf(((gitem_armor_t *)armor->info)->energy_protection * damage);
else
    save = ceilf(((gitem_armor_t *)armor->info)->normal_protection * damage);
if (save >= client->pers.inventory[index])
    save = client->pers.inventory[index];
client->pers.inventory[index] -= save;
```

So armour absorbs `save` and is consumed 1:1 by `save`. Total damage a pool of
`A` armour can absorb is exactly `A`; while it lasts, health loss is
`damage × (1 − p)`.

Order in `T_Damage`: Damage rune (`g_combat.c:476`) → knockback (`:479-514`) →
godmode (`:517-524`) → invincibility (`:525-535`) → `CheckPowerArmor` (`:537`)
→ Resist rune (`:541`) → `CheckArmor` (`:550`).

Effective survivability against **normal** damage, starting from 100 health:

| Armour | Total damage survived | Health lost while armour lasts |
|---|---|---|
| 200 body (0.80) | 100 + 200 = **300** | 0.25 per armour point (50 for the full 200) |
| 100 combat (0.60) | 100 + 100 = **200** | 0.67 per armour point |
| 50 jacket (0.30) | 100 + 50 = **150** | 2.33 per armour point |

Against **energy** damage (blaster, hyperblaster, plasma, BFG lasers/wave,
grapple — `DAMAGE_ENERGY` at `g_weapon.c:319`, `:841`, `:943`; `plasma.c:61`,
`:138`; `p_weapon.c:1837`, `:1849`):

**Jacket armour has `energy_protection = 0.00`. It absorbs literally nothing.**
A jacket-armoured player is, against a hyperblaster, an unarmoured player.
`ArmorIndex` (`g_items.c:656-671`) checks jacket **first**, so a player holding
2 shard-points of jacket armour and no other armour is fully exposed to energy.

**Rockets and railgun are normal damage** (`dflags = 0`, `g_weapon.c:653`,
`:771`), so body armour's 0.80 applies at full strength to both.

### Pickup / upgrade rules — `Pickup_Armor` (`g_items.c:673-773`)

* Shards: `+2` to whatever class is held, or set jacket to exactly `2`
  (`:688-694`).
* No armour held: take `base_count` outright (`:697-700`).
* Better class than held (`newinfo->normal_protection > oldinfo->normal_protection`):
  `newcount = new.base_count + (old.normal/new.normal) × old_count`, capped at
  `new.max_count`; old class zeroed (`:713-727`).
* Worse or equal class: `newcount = old_count + (new.normal/old.normal) × new.base_count`,
  capped at `old.max_count`; **refused entirely if already at or above that**
  (`:730-743`).

The `> max_health × 2` clamp at `:762-770` is dead code.

## 1.18 Power armor

Items exist in `itemlist` and are not inhibited at `ctfflags 16`:

| Item | quantity (= respawn) | Cite |
|---|---|---|
| Power Screen | 60 s | `g_items.c:1560-1579` |
| Power Shield | 60 s | `g_items.c:1583-1602` |

`Pickup_PowerArmor` (`g_items.c:901-930`) auto-uses on first pickup in
deathmatch. Whether either appears on the LMCTF map rotation is **UNKNOWN** —
looked in `g_spawn.c`, `g_replace.c` and `g_items.c`; entity placement is per-BSP
and no `.bsp` or `.ent` files were inspected.

`CheckPowerArmor` (`g_combat.c:190-281`), with the dead balance branches
removed:

| Type | Gate | Damage reduction | Cells per point |
|---|---|---|---|
| Power **Screen** | frontal only: `DotProduct(normalize(point − origin), forward) > 0.3` (`:227-228`) | `damage / 3` (`:238`) | 1 (`:230`) |
| Power **Shield** | omnidirectional | `(2 × damage) / 3` (`:255`) | 1, or 2 if `CTF_DM_POWER_ARMOR_STRENGTH` — **not set at 16** (`:244-246`) |

`save = power × damagePerCell`, clamped to the reduced `damage` (`:260-264`);
cells are debited `save / damagePerCell` (`:269`, `:277`). Power armour is
consumed from the **Cells** pool, competing directly with hyperblaster / BFG /
plasma ammo. Applied **before** normal armour (`g_combat.c:536-537`).

## 1.19 Health

| Entity | `count` | `style` | Respawn | Cite |
|---|---|---|---|---|
| `item_health_small` | **2** | `HEALTH_IGNORE_MAX` | 30 s | `g_items.c:2663-2676` |
| `item_health` | **10** | 0 | 30 s | `g_items.c:2647-2659` |
| `item_health_large` | **25** | 0 | 30 s | `g_items.c:2680-2692` |
| `item_health_mega` | **100** | `HEALTH_IGNORE_MAX \| HEALTH_TIMED` | see below | `g_items.c:2696-2709` |

`HEALTH_IGNORE_MAX = 1`, `HEALTH_TIMED = 2` (`g_items.c:47-48`).

`Pickup_Health` (`g_items.c:600-652`):

* Without `HEALTH_IGNORE_MAX`: refused if `health >= max_health`, and the result
  is clamped to `max_health` (`:602-612`). So `item_health` and
  `item_health_large` can never overheal and are worthless at 100.
* With `HEALTH_IGNORE_MAX`: no clamp at all. Stimpacks (+2) and the mega (+100)
  stack **without limit** — the `2 × max_health` cap at `:616-624` is dead code.
* Non-timed items: `SetRespawn(ent, 30)` (`:647-648`).

**Mega health countdown** — `MegaHealth_think` (`g_items.c:569-598`):

* Pickup sets `nextthink = level.time + 5` and hides the entity (`:628-633`).
* Each tick: if `owner->health > owner->max_health`, `health -= 1` and
  `nextthink = level.time + 1` — **1 hp/s decay** (`:586-591`).
* If the owner holds the **Regen rune**, the threshold becomes
  `max_health + 25` and the tick is **2 s** (`:574-583`) — a Regen carrier keeps
  125 health indefinitely and decays at half rate above it.
* Only when the decay finishes does `SetRespawn(self, 20)` run (`:594-595`).

So a mega taken at 100 health gives 200, decays for 100 s, then respawns 20 s
later: a **120-second cycle**, and it is unavailable for 100 s of that even
though nobody is holding it. This is the longest denial clock on the map.

`max_health` is 100 at spawn (`p_client.c:1154`). `Pickup_Adrenaline`
(`g_items.c:225-237`) only raises it outside deathmatch. `Pickup_AncientHead`
(`g_items.c:239-247`) gives `+2 max_health` unconditionally.

## 1.20 Ammo

| Item | `quantity` (pickup) | Carry max | Cite |
|---|---|---|---|
| Shells | 10 | 100 | `g_items.c:1906`; `p_client.c:1157` |
| Bullets | 50 | 200 | `g_items.c:1929`; `p_client.c:1156` |
| Cells | 50 | 200 | `g_items.c:1952`; `p_client.c:1160` |
| Rockets | 5 | 50 | `g_items.c:1975`; `p_client.c:1158` |
| Slugs | 10 | 50 | `g_items.c:1998`; `p_client.c:1161` |
| Grenades | 5 | 50 | `g_items.c:1739`; `p_client.c:1159` |

All ammo respawns in **30 s** (`SetRespawn(ent, 30)`, `g_items.c:537`).
Weapons respawn in **30 s** (`SetRespawn(ent, 30)`, `p_weapon.c:147`).

Picking up a weapon grants `ammo->quantity` of its ammo type
(`p_weapon.c:134-138`): shotgun→10 shells, SSG→10 shells, MG/CG→50 bullets,
GL→5 grenades, RL→5 rockets, HB/BFG/plasma→50 cells, railgun→10 slugs.

Bandolier raises maxes to 250/150/250/75 bullets/shells/cells/slugs
(`g_items.c:249-285`); Ammo Pack to 300/200/100/100/300/100
(`g_items.c:287-363`). Both refill bullets/shells (and the pack also cells,
grenades, rockets, slugs) by one item `quantity` each.

Players spawn with **zero ammo** — only the Grappling Hook and the Blaster
(`p_client.c:1141-1151`).

## 1.21 Powerups

`Pickup_Powerup` (`g_items.c:169-208`) increments inventory and
`SetRespawn(ent, ent->item->quantity)` — the `quantity` field **is** the respawn
delay in seconds. It does **not** auto-activate unless `DF_INSTANT_ITEMS`
(`:199-204`); the holder must `use` it.

| Powerup | `quantity` = respawn | Duration | Cite |
|---|---|---|---|
| **Quad Damage** | `LM_QUAD_DEFAULT_TIME` = **60 s** | 300 frames = **30 s** | `g_items.c:2025`, `g_local.h:1513`; `Use_Quad` `g_items.c:381-387` |
| Invulnerability | 300 s | 300 frames = 30 s | `g_items.c:2048`; `g_items.c:430-432` |
| Silencer | 60 s | 30 shots | `g_items.c:2071`; `g_items.c:443` |
| Rebreather | 60 s | 300 frames = 30 s | `g_items.c:2094`; `g_items.c:400-402` |
| Environment Suit | 60 s | 300 frames = 30 s | `g_items.c:2117`; `g_items.c:416-417` |

Stacking: `Use_Quad` **adds** 300 frames if already active
(`g_items.c:384-387`); same pattern for breather / enviro / invuln.
A quad dropped by a dying player carries its remaining time via
`quad_drop_timeout_hack` (`g_items.c:199-204`, `:374-378`).

`is_quad` is `client->quad_framenum > level.framenum` (`p_weapon.c:289`), and
quad multiplies **damage ×4** everywhere plus **kick ×4** for MG, CG, shotgun,
SSG and railgun (`p_weapon.c:1128-1129`, `:1279-1280`, `:1356-1357`,
`:1538-1539`, `:1632-1633`).

**Invulnerability is removed from the map at `ctfflags 16`** (§0.2) —
`g_items.c:1360-1369`.

`CTF_RANDOM_QUAD` (8192, not set) would set the quad respawn to
`50 + rand()%31` on every item spawn (`g_items.c:1311-1312`).

## 1.22 Runes

Spawn: `SpawnRune` is called once per enabled bit at level start
(`g_spawn.c:1036-1045`). `runes` defaults to `"15"` (`g_save.c:215`), so the map
carries **exactly one each of Damage, Haste, Resist and Regen — four runes, no
Vampire** (`RUNE_VAMP` = 16, `q_shared.h:1096`).

`q_shared.h:1092-1096`: `RUNE_DAMAGE 1`, `RUNE_RESIST 2`, `RUNE_HASTE 4`,
`RUNE_REGEN 8`, `RUNE_VAMP 16`.

Runes **relocate every 30 s** — `RUNETHINKTIME 30` (`g_runes.c:10`),
`Rune_Think` (`g_runes.c:271-350`, relocate test at `:338`) tosses the rune to a
random `item_health_small` spawn point (`SelectRuneSpawnPoint`,
`g_runes.c:79-...`), falling back to `redflag`. A rune is never in the same
place for long, which is exactly why `sg_fields.c:266-274` keys the
powerup/rune per-item field rebuild on CACO's belief signature rather than an
entity walk.

`SpawnRune` (`g_runes.c:356-425`) creates exactly one entity per call.

One rune per player: `Pickup_Rune` refuses if `other->client->rune` is set
(`g_runes.c:444-450`). Runes drop on death (`p_client.c:1008-1011`), on
disconnect (`p_client.c:2584-2586`) and on the `drop rune` command
(`g_cmds.c:1095`).

### Rune effects

| Rune | Effect | Magnitude | Where |
|---|---|---|---|
| **Damage** | outgoing `damage *= 1.75f` | ×1.75 (comment says the original was ×2) | `DamageRuneHook`, `g_runes.c:716-728`; called `g_combat.c:476` |
| **Resist** | incoming `damage /= 1.75f` | ÷1.75 | `ResistRuneHook`, `g_runes.c:730-743`; called `g_combat.c:541` |
| **Haste** | `pers.weapon->weaponthink(ent)` is called a **second time** in the same server frame | **exactly 2× rate of fire and 2× ammo drain** | `RuneWeaponThinkHook`, `g_runes.c:800-818`; called `p_weapon.c:306` |
| **Regen** | health and armour tick up | **≈3.33 hp/s and ≈3.33 armour/s** — see below | `RuneThinkHook`, `g_runes.c:745-798`; called `p_client.c:3116` |
| Vampire | on hitting a player: `health = min(250, health + take/2)`; on a corpse `take/4` | not spawned at `runes 15` | `g_combat.c:598-621` |

**Damage vs Resist ordering.** Damage rune multiplies at `g_combat.c:476`,
before power armour and armour. Resist divides at `g_combat.c:541`, after power
armour and before normal armour. They exactly cancel: 1.75 / 1.75 = 1.0.

**Regen precisely** (`g_runes.c:751-793`):

```c
heartrate = ent->health / 5;
if (heartrate <  5) heartrate =  5;
if (heartrate > 25) heartrate = 25;
if (level.framenum < ent->client->regentime + heartrate) return;
ent->client->regentime = level.framenum;
if (ent->health < ent->max_health + 25)
    ent->health += (heartrate / 3.0f);          // capped at max_health+25
...
    ent->client->pers.inventory[old_armor_index] += heartrate / 3.0f;  // capped at 200
```

`heartrate` is **both** the interval in frames **and** the amount is
`heartrate/3` — so the rate is `(heartrate/3) / (heartrate × 0.1 s)` =
**3.33 per second, independent of current health**. Health ceiling is
`max_health + 25` = **125**; armour ceiling is **200** in whatever class is
held, and with no armour it *sets* jacket armour to `heartrate/4`
(`g_runes.c:777-781`).

Regen also changes the mega-health decay (§1.19).

---

# PART 2 — DOCTRINE

Everything below is stated as implementable rules. Where a number comes from
Part 1 it is named. Where a number is a tuning choice it says so.

## 2.0 The switching entry point

**`Cmd_Use_f` (`g_cmds.c:649-687`) is the path.** A bot switches weapons by
running the same sequence a player's `use <name>` runs. In-process, that is:

```c
gitem_t *it = FindItem("Super Shotgun");        /* g_cmds.c:667 */
if (!it || !it->use)            return false;   /* g_cmds.c:668-678 */
if (!ent->client->pers.inventory[ITEM_INDEX(it)]) return false; /* :679-685 */
it->use(ent, it);                               /* g_cmds.c:687  -> Use_Weapon */
```

`Cmd_Use_f` also aliases `"hook"` and `"grapple"` to `"grappling hook"` and
`"flag"` to `"Enemy Flag"` before the lookup (`g_cmds.c:659-662`).

`Use_Weapon` (`p_weapon.c:345-377`) does **not** switch immediately. It
validates ammo against `item->quantity` (`:367-372`) and then sets
`ent->client->newweapon = item` (`:376`). The actual change happens in
`ChangeWeapon` (`p_weapon.c:171-232`) once the current weapon's
`FRAME_DEACTIVATE_LAST` is reached (`p_weapon.c:427-433`).

**Switch cost.** From `Weapon_Generic`, dropping takes
`FRAME_DEACTIVATE_LAST − FRAME_IDLE_LAST` thinks and raising takes
`FRAME_ACTIVATE_LAST` thinks (`:454-469`, `:471-497`):

| Weapon | drop thinks | raise thinks | total |
|---|---|---|---|
| Blaster | 3 | 4 | 0.7 s |
| Shotgun | 3 | 7 | 1.0 s |
| Super Shotgun | 4 | 6 | 1.0 s |
| Machinegun | 4 | 3 | 0.7 s |
| Chaingun | 3 | 4 | 0.7 s |
| Grenade Launcher | 5 | 5 | 1.0 s |
| Rocket Launcher | 4 | 4 | 0.8 s |
| Hyperblaster | 4 | 5 | 0.9 s |
| Railgun | 5 | 3 | 0.8 s |
| BFG | 3 | 8 | 1.1 s |

**Rule S1.** A weapon switch costs 700–1100 ms of not-shooting. Never switch
inside 800 ms of an engagement you are already winning. Rate-limit switch
requests to one per 500 ms (the existing `sg_combat.c:309-311`
`switch_next` guard is already correct; keep it).

**Rule S2.** Poll `client->newweapon`. While it is non-NULL a switch is in
flight and the trigger must stay off (already enforced at
`sg_combat.c:296-297`, `:305-306`).

**Rule S3.** Never `use grappling hook`. Do not let the grapple become
`pers.weapon` under any circumstance. §1.14: it turns `BUTTON_ATTACK` into the
rope, and it breaks `Cmd_Hook_f`'s offhand path (`g_cmds.c:1408-1411`). With
`CTF_OFFHAND_HOOK` set the weapon-cycle commands already skip it
(`g_cmds.c:885-887`, `:933-935`); only an explicit `use` can get you there, so
just never issue it.

## 2.1 (a) Weapon selection ladder

Range bands: **contact** < 128, **close** 128–400, **mid** 400–900,
**long** > 900. Distance is eye-to-target-bbox-centre, the same measure
`sg_combat.c:387-389` already computes.

### Ammo-availability predicate

A weapon is *available* iff it is in inventory **and**
`inventory[ammo_index] >= item->quantity` — the same test `Use_Weapon`
(`p_weapon.c:367-372`) and `Weapon_Generic` (`p_weapon.c:504-505`) apply.
`item->quantity` is 1 for everything except **Super Shotgun = 2**
(`g_items.c:1670`), **BFG = 50** (`g_items.c:1854`) and
**Plasma = 10** (`g_items.c:1877`).

### Ladders

**Contact (< 128 u).** Splash is suicide here; §1.10 says a rocket at `d = 0`
costs the shooter 60 health, §1.8 says a grenade costs 60.

| Rank | Weapon | Justification |
|---|---|---|
| 1 | **Super Shotgun** (≥2 shells) | 120 damage/shot at this range (§1.7); the ±5° barrel split is still inside a torso below 128 u |
| 2 | **Chaingun** (≥3 bullets, already spun) | 180 dps sustained (§1.5) |
| 3 | **Hyperblaster** (≥1 cell) | 150 dps, no windup (§1.3) |
| 4 | **Shotgun** (≥1 shell) | 48 damage/shot, full pellet count (§1.6) |
| 5 | **Machinegun** | 80 dps, no climb in DM (§1.4) |
| 6 | **Railgun** | 100/1.6 s = 62.5 dps; poor here but never misses |
| 7 | **Plasma, bounce mode** | 39/0.9 s; self-splash 19.5 if it misses (§1.13) |
| 8 | **Blaster** | 30 dps, free |
| — | **RL / GL / hand grenade / BFG** | **forbidden**, see rule R1 |

**Close (128–400 u).**

| Rank | Weapon | Justification |
|---|---|---|
| 1 | **Rocket Launcher** if `d ≥ 121` | 220–240 on a direct hit (§1.10); splash forgives near-misses inside 120 u of the impact point |
| 2 | **Chaingun** | 180 dps, spread saturates the target out to 437 u (§1.1) |
| 3 | **Hyperblaster** | 150 dps, 1000 u/s projectile = 0.13–0.4 s flight |
| 4 | **Super Shotgun** below 256 u | ~48–78 damage/shot (§1.7 table) |
| 5 | **Shotgun** | full 48 damage out to 262 u (§1.6) — this is the LMCTF spread edit paying off |
| 6 | **Railgun** | 62.5 dps, no lead, no spread |
| 7 | **Machinegun** | 80 dps, saturating out to 437 u |
| 8 | **Grenade Launcher** if `d ≥ 161` | arcing; use only when line of sight is blocked and the arc is not |
| 9 | **Plasma** | spread mode fans ±10°, hits at close range |
| 10 | **Blaster** | |

**Mid (400–900 u).** This is where spread starts costing real damage
(§1.1 saturation table: MG/CG 437 u, shotgun 262 u, SSG 131 u).

| Rank | Weapon | Justification |
|---|---|---|
| 1 | **Railgun** | the only weapon with zero degradation; 100 damage guaranteed on a hit |
| 2 | **Rocket Launcher** | 0.6–1.4 s flight at 650 u/s; splash still lands full value inside 120 u of impact |
| 3 | **Hyperblaster** | 0.4–0.9 s flight at 1000 u/s; still 150 dps if the lead is right |
| 4 | **Chaingun** | falls to ~45 dps at 900 u (spread × 1/d²) |
| 5 | **Machinegun** | ~20 dps at 900 u |
| 6 | **Shotgun** | ~17 dps at 600 u, ~7 at 900 u |
| 7 | **BFG** if `d ≥ 101` and ≥50 cells | 0.8 s windup + `d/400` flight = 1.8–3.1 s; only against a stationary or predictable target |
| 8 | **Blaster** | 0.4–0.9 s flight |
| — | **Super Shotgun** | **do not use past 400 u** — ~37 damage for 2 shells |

**Long (> 900 u).**

| Rank | Weapon | Justification |
|---|---|---|
| 1 | **Railgun** | the only correct answer; trace runs to 8192 u (`g_weapon.c:747`) |
| 2 | **Hyperblaster** | ≥0.9 s flight; volume fire only, against a corridor |
| 3 | **Blaster** | free ammo; keeps pressure |
| — | everything else | **disengage instead**; `SG_ENGAGE_RANGE` is already 2000 (`sg_combat.c:68`) |

### Rule R1 — no-self-splash minimum ranges

From §1.1's `self_damage(d) = 0.5 × (damage − 0.5 d)` for `d ≤ dmg_radius`, and
`findradius`'s hard cull at `dmg_radius` (`g_utils.c:77-78`):

```
d_safe(weapon) = dmg_radius + 1
```

| Weapon | `damage` | `dmg_radius` | Self damage at contact | **d_safe** | Self damage at the cliff |
|---|---|---|---|---|---|
| Rocket Launcher | 120 | 120 | 60 | **121 u** | 30 |
| Grenade Launcher | 120 | 160 | 60 | **161 u** | 20 |
| Hand grenade | 125 | 165 | 62.5 | **166 u** | 21 |
| BFG (impact only) | 200 | 100 | 100 | **101 u** | 50 |
| Plasma bounce (miss only) | 39 | 109 | 19.5 | **110 u** | ~2 |
| Plasma spread (miss only) | 28 | 98 | 14 | **99 u** | ~2 |

Note the **cliff**, not a taper: at `d_safe − 1` you still eat 20–50 damage.
There is no gradual fade to hide behind.

**R1.** Do not select or fire a splash weapon when the predicted impact point is
within `d_safe` of the bot's own bbox centre. Predict the impact point with the
same `gi.trace(eye, NULL, NULL, endp, self, MASK_SHOT)` the combat file already
runs (`sg_combat.c:457`) — `tr.endpos` **is** the impact point.

```c
/* pseudo, in the weapon-choice function */
if (weapon_has_splash(w)) {
    trace_t tr = gi.trace(eye, NULL, NULL, endp, self, MASK_SHOT);
    vec3_t  me; VectorAdd(self->absmin, self->absmax, me); VectorScale(me, 0.5f, me);
    vec3_t  v;  VectorSubtract(tr.endpos, me, v);
    if (VectorLength(v) < d_safe[w])
        reject(w);
}
```

**R1b — the health override.** Splash self-damage is proportional, so it is
survivable when you are healthy and lethal when you are not. Forbid the rocket
launcher outright when `health + armor_absorbable < 90` (60 self damage at
contact plus margin), and the grenade launcher when
`health + armor_absorbable < 90`. Use the railgun or chaingun instead. The
"absorbable" term is just the armour count (§1.17: a pool of `A` armour absorbs
exactly `A` damage).

**R1c — the grapple override.** While `client->hookstate == 2`, the bot is being
pulled toward the anchor at up to 800 u/s (§1.14). `client->hooklength` **is**
the distance to the anchor (`p_weapon.c:2062-2068`). Forbid splash weapons when
`hooklength < d_safe(w) + 80` (80 u ≈ one server frame of pull at 800 u/s).
This is a one-field test; no extra tracing needed.

### Rule R2 — ammo floor and step-down

Define per weapon a **floor** = the ammo count below which the weapon stops
being the right tool, in units of "seconds of fire remaining":

| Weapon | Drain | Floor (≈3 s of fire) | `item->quantity` hard floor |
|---|---|---|---|
| Chaingun | 30 bullets/s (§1.5) | **90 bullets** | 1 |
| Machinegun | 10 bullets/s (§1.4) | **30 bullets** | 1 |
| Hyperblaster | 10 cells/s (§1.3) | **30 cells** | 1 |
| Super Shotgun | 1.67 shells/s | **6 shells** | **2** |
| Shotgun | 0.83 shells/s | **3 shells** | 1 |
| Rocket Launcher | 1.11 rockets/s | **4 rockets** | 1 |
| Grenade Launcher | 0.83 grenades/s | **3 grenades** | 1 |
| Railgun | 0.63 slugs/s | **2 slugs** | 1 |
| Plasma | 11.1 cells/s | **34 cells** | **10** |
| BFG | 20 cells/s | **50 cells** | **50** |

**R2a.** Choose the highest-ranked weapon in the band whose ammo is **above its
floor**. If none is, choose the highest-ranked one above its hard floor. If none
is, the blaster (30 dps, infinite).

**R2b.** Never let a burst run a weapon to zero mid-fight: `Weapon_Generic`'s
own mid-burst switch (`p_weapon.c:592-601`) drops you into
`NoAmmoWeaponChange`'s ladder, which does **not** include the rocket launcher,
grenade launcher, BFG or plasma (§1.16). Losing the RL to an empty magazine
costs a 0.8 s re-raise *plus* whatever `NoAmmoWeaponChange` picked. Step down
one rung *before* hitting the hard floor.

**R2c — cells are contested.** Cells feed the hyperblaster (10/s), the BFG (50 a
shot), the plasma (10 a shot) **and** power armour (1 per point absorbed,
§1.18). With 200 max cells (`p_client.c:1160`), never fire the BFG below 100
cells if a hyperblaster is also held — one BFG shot is 5 s of hyperblaster.

**R2d — shells are the scarcest.** Max 100 (`p_client.c:1157`), pickups of 10
(`g_items.c:1906`), and the SSG spends 2 a shot. A full 100-shell load is 50 SSG
shots = 60 s of firing. Treat shells as the reason to prefer the shotgun over
the SSG past 256 u even when both are available — same 48 damage, half the cost.

## 2.2 (b) Firing solutions

The bot knows `enemy->velocity` directly. The existing two-pass refinement in
`sg_combat.c:387-397` is the right shape; generalise the speed.

### Straight-line projectiles (no gravity)

`MOVETYPE_FLYMISSILE` and `MOVETYPE_REFLECT` do not apply gravity
(`g_weapon.c:417`, `:695`, `:986`; `plasma.c:212`, `:250-252`).

```c
/* two-pass intercept, exactly as sg_combat.c:387-397 */
VectorSubtract(target_center, muzzle, delta);
d      = VectorLength(delta);
flight = d / SPEED;
VectorMA(target_center, flight, enemy->velocity, lead);
VectorSubtract(lead, muzzle, delta);
d      = VectorLength(delta);
flight = d / SPEED;
VectorMA(target_center, flight, enemy->velocity, lead);   /* final aim point */
```

| Weapon | `SPEED` | Cite |
|---|---|---|
| Blaster | 1000 | `p_weapon.c:939` |
| Hyperblaster | 1000 | `p_weapon.c:939` (shared `Blaster_Fire`) |
| Plasma (both modes) | 1200 | `plasma.h:25-26` |
| Rocket | **650** | `p_weapon.c:889` |
| BFG | **400** | `p_weapon.c:1741` |
| Grapple hook | 800 | `p_weapon.c:14` |

Flight times to know by heart:

| d | Blaster/HB (1000) | Plasma (1200) | Rocket (650) | BFG (400) |
|---|---|---|---|---|
| 128 | 0.13 s | 0.11 s | 0.20 s | 0.32 s |
| 400 | 0.40 s | 0.33 s | 0.62 s | 1.00 s |
| 900 | 0.90 s | 0.75 s | 1.38 s | 2.25 s |

**BFG total time-to-impact is `0.8 + d/400` seconds** — the 0.8 s is the
gunframe-9→17 windup (`p_weapon.c:1698-1710`, §1.12). At 400 u that is 1.8 s of
prediction, which no lead formula can carry against a strafing player.

**Rule F1.** Only fire a projectile weapon when
`flight × |enemy->velocity| < 96` (three-quarters of a player bbox width) **or**
the enemy's velocity has been stable in direction for ≥ 0.3 s. Otherwise the
lead is a guess. At `|v| = 320` (walk speed) this caps the blaster at 300 u, the
rocket at 195 u, and the BFG at 120 u before the windup is even counted — which
is the real reason the BFG is rank 7 in §2.1.

**Rule F2 — aim at the bbox centre, not the origin.** `Combat_Center`
(`sg_combat.c:129-133`) already does this; keep it. The player origin is 24
units below the bbox top and 4 below centre (`p_client.c:1833`).

**Rule F3 — the muzzle is not the eye.** Each fire function projects from its
own offset via `P_ProjectSource` (`p_weapon.c:26-36`), which is `static`:

| Weapon | offset (fwd, right, up-from-eye) | Cite |
|---|---|---|
| Blaster / Hyperblaster | `(24, 8, −8)` | `p_weapon.c:932` |
| Machinegun | `(0, 8, −8)` | `p_weapon.c:1151` |
| Chaingun | `(0, 7+crandom()·4, −8+crandom()·4)` | `p_weapon.c:1293-1295` |
| Shotgun / SSG | `(0, 8, −8)` | `p_weapon.c:1351`, `:1534` |
| GL / RL / BFG / plasma / hook | `(8, 8, −8)` | `p_weapon.c:805`, `:880`, `:1732`, `:2181`, `:2022` |
| Railgun | `(0, 7, −8)` | `p_weapon.c:1641` |

`P_ProjectSource` mirrors the `right` term for left-handed clients
(`p_weapon.c:31-32`). Trace from the **eye** as `sg_combat.c` already does: the
eye is behind and above every muzzle, so an eye-clear shot is a muzzle-clear
shot. Do not try to reconstruct the muzzle.

### Ballistic projectiles (gravity applies)

Grenades are `MOVETYPE_BOUNCE` (`g_weapon.c:559`, `:597`), so `sv_gravity` 800
applies (`g_save.c:175`).

Launch velocity for the **grenade launcher** (`p_weapon.c:812`,
`g_weapon.c:555-557`):

```
v = 600·forward  +  (200 ± 10)·up  +  (±10)·right
```

For the **hand grenade** (`p_weapon.c:638`, `g_weapon.c:592-594`):

```
speed = 400 + (3.0 − timer)·133.33,   timer = grenade_time − level.time
v     = speed·forward + (200 ± 10)·up + (±10)·right
```

Flat-ground solve for a target at horizontal range `R` and height difference `h`
(target minus shooter), with `vh` the horizontal launch speed and `vu` the extra
up component:

```
t  = R / (vh · cos(pitch))                          horizontal
h  = (vh·sin(pitch) + vu)·t − 400·t²                 vertical, g/2 = 400
```

For the grenade launcher fired level (`pitch = 0`, `vh = 600`, `vu = 200`):

```
t_apex   = 200/800  = 0.25 s      apex height = 25 u    at R = 150 u
t_ground = 400/800  = 0.50 s      range to shooter's own plane R = 300 u
```

So **a level GL shot lands 300 units away after 0.5 s**, having risen only 25
units. It bounces from there (fuse 2.5 s, `p_weapon.c:812`), and
`Grenade_Touch` detonates on contact with any `takedamage` entity
(`g_weapon.c:536-537`).

**Rule F4 — GL pitch solve.** To hit ground at range `R` with `vh = 600`,
`vu = 200`, solve for `pitch`:

```
θ ≈ atan( (400·R/600² − 200/600) )      /* small-angle, level ground */
    = atan( R/900 − 1/3 )
```

Check: `R = 300` → `atan(0) = 0°`, matching the level-fire result above.
`R = 600` → `atan(0.333) = 18.4°`. `R = 150` → `atan(−0.167) = −9.5°` (aim
down). For a bot, tabulate `pitch(R)` at 50-unit steps rather than solving live.

**Rule F5 — do not lead a grenade at a moving target.** Fuse 2.5 s and a bounce
mean the impact point is not predictable past first contact. Use the GL only for
(i) area denial at a chokepoint the enemy must cross, and (ii) shots at a
stationary or cornered target. Both cases want the *impact point*, not the
target, as the aim solution.

### Hitscan — spread-driven range caps

From §1.1: expected fraction of pellets/bullets on a 32×56 torso at distance `d`
is `min(1, 16·8192/(H·d)) × min(1, 28·8192/(V·d))`.

**Rule F6.** Cap each hitscan weapon at the range where its expected damage
falls below the blaster's 30 dps:

| Weapon | Full-value range | 30-dps-equivalent cap | Cite |
|---|---|---|---|
| Machinegun (300/500) | 437 u | **~740 u** | `p_weapon.c:1160` |
| Chaingun (300/500) | 437 u | **~1100 u** | `p_weapon.c:1298` |
| Shotgun (500/500) | 262 u | **~560 u** | `p_weapon.c:1369` |
| Super Shotgun (1000/500 + ±5°) | 131 u | **~400 u** | `p_weapon.c:1555, 1558` |
| Railgun (no spread) | ∞ | **none** | `g_weapon.c:747` |

**Rule F7 — the railgun does not need a spread model, and it pierces.** The
trace continues through every client, monster and `SOLID_BBOX` entity it hits
(`g_weapon.c:764-767`), damaging each. Two enemies in a line is 200 damage for
one slug. When the pre-fire trace reports a *teammate* first, the shot still
reaches the enemy behind — but it will also hit the teammate, and
`OnSameTeam` + `DF_NO_FRIENDLY_FIRE` decides whether that costs anything
(`g_combat.c:433-448`). Do not take the shot unless friendly fire is off.

## 2.3 (c) Item-need weighting

The surface already prices detours (`sg_arach.c:379-406`):

```c
v = w->objective * (float)goal_field[seed];
for (c = 0; c < SG_FIELD_CLASSES; c++)
    if (w->item[c] > 0.0f)
        v -= 1500.0f * Detour_Value(seed, c, goal_field, w->item[c]);
```

with (`sg_arach.c:231-290`)

```
value = worth / (1 + max(0, detour_ms) / 1500)
detour_ms = cost_to_item + item_to_goal − direct
```

So **a `worth` of `w` on an item lying exactly on the road subtracts `1500·w`
milliseconds from the surface.** That is the unit everything below is stated in.

### The detour budget identity

With `objective` weight 1.0, a detour of `D` ms is accepted while

```
1500·w / (1 + D/1500)  >  D
```

which solves to

```
D_max(w) = ( sqrt(2.25e6 + 9e6·w) − 1500 ) / 2      milliseconds
```

| `w` | `D_max` |
|---|---|
| 0.05 | 72 ms |
| 0.10 | 140 ms |
| 0.15 | 199 ms |
| 0.20 | 254 ms |
| 0.30 | 355 ms |
| 0.40 | 447 ms |
| 0.50 | 533 ms |
| 0.80 | 771 ms |
| 1.00 | 918 ms |
| 1.20 | 1057 ms |
| 1.60 | 1291 ms |
| 2.00 | 1500 ms |

Replace the static `sg_weight_table` entries (`sg_arach.c:204-221`) with
`w = role_weight × state_multiplier`, computed once per second alongside
`Fields_Refresh`.

### Health — `SG_FC_HEALTH`

Justified by §1.19: `item_health` (10) and `item_health_large` (25) are
**refused outright at `health >= max_health`** (`g_items.c:602-604`), so their
worth must go to zero at 100. Only `item_health_small` (+2) and
`item_health_mega` (+100) overheal (`HEALTH_IGNORE_MAX`).

```c
static float Worth_Health(edict_t *e)
{
    int h = e->health;
    if (h >= 100) return 0.05f;   /* stimpacks and mega only; 72 ms budget   */
    if (h >=  75) return 0.20f;   /* 254 ms                                  */
    if (h >=  50) return 0.45f;   /* 505 ms                                  */
    if (h >=  25) return 0.90f;   /* 859 ms                                  */
                  return 1.60f;   /* 1291 ms — below one rocket splash (60)  */
}
```

Breakpoints chosen against damage-per-hit facts: at `h < 25` a single shotgun
blast (48, §1.6) or any rocket splash (30–60, §1.10) kills; at `h < 50` a direct
rocket (100–119) kills; at `h < 75` an SSG blast at contact (120) kills.

**H1 — the mega is worth a special case.** §1.19: it gives +100 *over* max, then
denies itself for 100 s of decay plus 20 s of respawn. When
`Caco_ItemBelievedUp` reports a live `item_health_mega`, multiply
`Worth_Health` by **2.5** and clamp to 2.0 (a 1500 ms budget). Taking a mega at
full health is still worth ~1500 ms of detour because it doubles effective HP
*and* denies the enemy for two minutes.

**H2 — Regen rune interaction.** If the bot holds the Regen rune
(`client->rune->runetype == RUNE_REGEN`), it recovers 3.33 hp/s to a ceiling of
125 (§1.22). Multiply `Worth_Health` by **0.35** for `h ≥ 50` — waiting is
cheaper than detouring, since 50 hp is 15 s of standing still. Keep the full
weight below 50, because 15 s is a long time in a firefight.

### Armour — `SG_FC_ARMOR`

Justified by §1.17: a pool of `A` armour absorbs exactly `A` damage. So armour
deficit is measured in absorbable damage, and the ceiling is 200 (body).

```c
static float Worth_Armor(edict_t *e)
{
    int idx = ArmorIndex(e);                 /* g_items.c:656-671 */
    int A   = idx ? e->client->pers.inventory[idx] : 0;

    if (A ==   0) return 1.00f;   /*  918 ms */
    if (A <   25) return 0.80f;   /*  771 ms */
    if (A <   50) return 0.60f;   /*  644 ms */
    if (A <  100) return 0.40f;   /*  447 ms */
    if (A <  150) return 0.20f;   /*  254 ms */
                  return 0.05f;   /*   72 ms */
}
```

**A1 — the jacket-energy hole.** §1.17: `jacketarmor_info.energy_protection` is
**0.00**. `ArmorIndex` checks jacket first, so any jacket armour at all makes
the bot fully exposed to hyperblasters, plasma, BFG lasers and the grapple. If
`ArmorIndex(e) == jacket_armor_index`, multiply `Worth_Armor` by **1.6** — a
jacket-armoured bot wants a combat or body upgrade more than a bare bot wants a
jacket. This is invisible in stock-Q2 intuition and worth the extra ~350 ms of
budget.

**A2 — the refusal case.** `Pickup_Armor` **returns false** when the held count
already meets or exceeds the salvage-computed value (`g_items.c:738-739`). At
`A ≥ 150` in body armour, most shards and jackets on the map will be refused
outright. The 0.05 floor above is what keeps the bot from walking to items it
cannot pick up.

### Weapons — `SG_FC_WEAPON`

Justified by §2.1's ladder and the fact that a bot spawns with **only** the
blaster and the hook (`p_client.c:1141-1151`).

```c
/* tier of the best available weapon, using the R2 availability predicate */
static int Weapon_Tier(edict_t *e)
{
    if (avail(e, "rocket launcher") || avail(e, "chaingun")) return 5;
    if (avail(e, "super shotgun") || avail(e, "railgun")
        || avail(e, "hyperblaster"))                        return 4;
    if (avail(e, "machinegun") || avail(e, "grenade launcher")
        || avail(e, "plasma rifle"))                        return 3;
    if (avail(e, "shotgun"))                                return 2;
    return 1;                                               /* blaster only */
}

static const float worth_weapon[6] = { 0, 1.20f, 0.80f, 0.50f, 0.30f, 0.15f };
```

Budgets: tier 1 → **1057 ms**, tier 2 → 771 ms, tier 3 → 533 ms, tier 4 →
355 ms, tier 5 → 199 ms.

Justification for tier 1 being worth a full second of detour: a blaster-only bot
does 30 dps (§1.2) against a chaingun's 180 (§1.5) — a 6:1 disadvantage. Nothing
else in the item table moves the needle that far.

**W1 — weapons respawn in 30 s** (`p_weapon.c:147`) and `DF_WEAPONS_STAY` is off
by default (`dmflags` default `"0"`, `g_save.c:198`). A weapon just taken by
someone else is gone for 30 s; `Caco_ItemBelievedUp` already handles this
(`sg_fields.c:115`, `:144`).

### Ammo — `SG_FC_AMMO`

Priced off the R2 floors. Let `f_w` be the floor for the bot's currently-held
weapon and `n` its ammo count.

```c
static float Worth_Ammo(edict_t *e)
{
    float r;                       /* fraction of floor remaining */
    if (Weapon_Tier(e) == 1) return 0.10f;   /* blaster needs nothing */
    r = (float)ammo_count(e) / (float)floor_for(e->client->pers.weapon);
    if (r >= 2.0f) return 0.05f;   /*  72 ms */
    if (r >= 1.0f) return 0.20f;   /* 254 ms */
    if (r >= 0.5f) return 0.50f;   /* 533 ms */
    if (r >  0.0f) return 0.85f;   /* 810 ms */
                   return 1.10f;   /* 990 ms — the weapon is dead weight */
}
```

**M1 — cell contention.** §1.18 and §2.1-R2c: cells feed hyperblaster, BFG,
plasma **and** power armour. If the bot holds any power armour
(`PowerArmorType(e) != POWER_ARMOR_NONE`, `g_items.c:777-...`), multiply
`Worth_Ammo` by 1.3 whenever the held weapon's ammo is Cells.

**M2 — shells.** Max 100, pickups of 10 (§1.20). A bot holding the SSG at
6 shells has 3 shots. Shells hit the 0.85 tier fast; that is correct.

### Powerups — `SG_FC_POWERUP` (quad only, §0.2)

Per-item fields exist for this class (`sg_fields.c:130-133`), so the detour
triangle is exact.

```c
static float Worth_Quad(edict_t *e)
{
    if (e->client->quad_framenum > level.framenum) return 0.0f; /* already on */
    /* base: quad is 4x damage for 30 s (g_items.c:381-387, p_weapon.c:289) */
    return 1.80f * (float)Weapon_Tier(e) / 5.0f + 0.40f;
}
```

Range: tier 1 → 0.76 (**744 ms**), tier 5 → 2.20 (**capped at 2.00 → 1500 ms**).

Justification: quad multiplies damage ×4 (§1.21). A quad on a chaingun is
720 dps for 30 s; a quad on a blaster is 120 dps. The weapon tier is what makes
the quad worth contesting, so it scales the worth.

**Q1 — the respawn clock.** Quad respawn is a fixed **60 s**
(`LM_QUAD_DEFAULT_TIME`, `g_local.h:1513`, `g_items.c:2025`). CACO knows when
the quad was last seen taken. If `time_until_respawn > 4000 ms`, set worth to
**0** — the field should not pull a bot toward an empty pedestal. If
`0 < time_until_respawn ≤ 4000 ms`, keep full worth: arriving 4 s early to
camp a 60 s item is correct.

### Runes — `SG_FC_RUNE`

Per-item fields exist, and there are exactly four runes on the map at
`runes 15` (§1.22). One rune per player (`g_runes.c:444-450`).

```c
static float Worth_Rune(edict_t *e)
{
    if (e->client->rune) return 0.0f;   /* one per player, hard rule */
    return 0.55f;                       /* 566 ms, before per-type scaling */
}
```

Per-type multipliers, from §1.22's measured effects:

| Rune | Multiplier | Justification |
|---|---|---|
| **Haste** | **1.60** (→ 0.88, 830 ms) | doubles rate of fire *exactly* (`g_runes.c:807-811`); the single largest combat multiplier available, and it costs no ammo *rate* the bot cannot afford at tier ≥3 |
| **Damage** | 1.45 (→ 0.80, 771 ms) | ×1.75 outgoing (`g_runes.c:721-724`) |
| **Resist** | 1.45 (→ 0.80, 771 ms) | ÷1.75 incoming (`g_runes.c:735-739`); exactly cancels an enemy Damage rune |
| **Regen** | 1.20 (→ 0.66, 690 ms) | 3.33 hp/s + 3.33 armour/s to 125/200 (`g_runes.c:751-793`); strategic, not tactical |

**RU1 — Haste doubles ammo drain too.** `RuneWeaponThinkHook` runs the whole
`weaponthink` a second time, so a Haste chaingun burns 60 bullets/s. If
`Weapon_Tier(e) <= 2`, drop the Haste multiplier to 1.20 — doubling a blaster is
60 dps, not worth 830 ms.

**RU2 — runes move every 30 s** (`RUNETHINKTIME`, `g_runes.c:10`;
`Rune_Think`, `g_runes.c:338-348`). The per-item field must be rebuilt from
belief, which `sg_fields.c:266-274` already does. Do not let a bot chase a rune
across more than one refresh interval; if the detour exceeds 1500 ms, drop it.

### Assembling the weights

```c
w->item[SG_FC_HEALTH ] = role.health  * Worth_Health(e);
w->item[SG_FC_ARMOR  ] = role.armor   * Worth_Armor(e);
w->item[SG_FC_WEAPON ] = role.weapon  * worth_weapon[Weapon_Tier(e)];
w->item[SG_FC_AMMO   ] = role.ammo    * Worth_Ammo(e);
w->item[SG_FC_POWERUP] = role.powerup * Worth_Quad(e);
w->item[SG_FC_RUNE   ] = role.rune    * Worth_Rune(e) * rune_type_mult;
```

`role.*` is the existing `sg_weight_table` row (`sg_arach.c:204-221`), now read
as a role *bias* rather than an absolute. Keep the carrier row's health bias of
0.50 and the defender's armour bias of 0.50 — they are already the right shape.

**Clamp every final weight to `[0, 2.0]`** — 2.0 is a 1500 ms budget, which is
the same `1500.0f` scale constant the detour decay uses (`sg_arach.c:273`). A
weight above 2.0 makes the item term dominate the objective term outright, which
is never correct for a CTF bot.

## 2.4 (d) Situational phase-space

### D1 — Enemy carrier seen: intercept

The carrier is running a known route (CACO's belief seeds the intercept field,
`sg_fields.c:292-303`) and is usually grappling, i.e. moving at up to 800 u/s in
a straight line toward an anchor (§1.14). That is a *predictable* velocity, which
is exactly what Rule F1 requires.

**Weapon.** Rocket launcher if available and `d ≥ 121` (R1). Justification: the
carrier is moving fast, so a hitscan needs a perfect track; the rocket's 120-unit
splash radius (§1.10) forgives 120 units of lead error, and 220–240 damage on a
direct hit ends the run in one shot. Second choice railgun (no lead needed,
100 damage, `g_weapon.c:747`), third chaingun.

**Engage range.** Open at **400–700 units**. Below 400 the carrier's grapple pull
crosses the gap in under 0.5 s and you get one shot; above 900 the rocket's
1.4 s flight against an 800 u/s target needs 1120 units of lead, which Rule F1
rejects.

**Body positioning.** Take the shot **across** the carrier's motion, not along
it. A rocket fired down the carrier's axis has the full 800 u/s closing/receding
error on the flight time; a rocket fired perpendicular has only the lateral
component, and the 120-unit splash absorbs it. Concretely: prefer a seed whose
direction to the believed carrier position has
`|DotProduct(to_carrier, carrier_velocity_normalized)| < 0.5`.

**Aim at the floor under the carrier when the direct trace is blocked.** §1.10:
splash is `120 − 0.5·d` from the *impact point*, so a rocket into the floor
2 metres ahead of a grappling carrier still lands 90+ damage. This is the one
case where deliberately missing is correct.

**Do not use the grapple to chase.** §1.14: an offhand hook that lands on the
carrier deals 8 on contact and 1 per 0.7 s — negligible — but it hands the bot's
velocity to the *carrier's* motion, which is exactly backwards for an intercept.

### D2 — We are carrying: flee

**The rope-vs-attack rule, precisely.** From §1.14 and `sg_combat.c:236-271`:

* If `pers.weapon == FindItem("Grappling Hook")`, `BUTTON_ATTACK` fires the rope
  (`p_weapon.c:2139-2144`) and `Cmd_Hook_f` degenerates to `ForceCommand("+attack")`
  (`g_cmds.c:1408-1411`). Shooting is impossible.
* If `pers.weapon` is anything else, `Cmd_Hook_f` reaches `Weapon_Hook_Fire`
  directly (`g_cmds.c:1419`) and the rope is thereafter sustained every frame by
  `ClientEndServerFrame` (`p_view.c:988-990`) **with no button input**.
  `BUTTON_ATTACK` is free.

**Therefore: a carrier can grapple and shoot simultaneously.** This is the single
most exploitable fact in the file, and it is only true because of `p_view.c:990`.

**Doctrine.**

1. **Never** issue `use grappling hook` (rule S3). The grapple must stay offhand.
2. Hold a weapon that works while moving fast and does not require lead:
   **railgun** first (hitscan, no spread, 100 damage, `g_weapon.c:747`), then
   **chaingun** (180 dps, saturates to 437 u), then **super shotgun** for
   anything that gets inside 256 u.
3. **Splash weapons are forbidden while `hookstate == 2`** unless
   `hooklength ≥ d_safe + 80` (rule R1c). A carrier being pulled at 800 u/s
   toward a wall it just rocketed will eat the full 60.
4. Fire the hook with `cmd hook`, release with `cmd unhook`
   (`g_cmds.c:2782-2785`). Both are one-shot commands; `cmd hook` does nothing
   while `client->hook` is non-NULL (`g_cmds.c:1402`).
5. **Health and armour weights get the carrier role's bias** — `sg_arach.c:208`
   already sets health 0.50 and armour 0.45 for `SG_ROLE_CARRY`, which with the
   §2.3 multipliers at 50 health gives `0.50 × 0.45 = 0.225` → a 280 ms budget.
   That is correct: a carrier detours for a *nearby* health box, never a distant
   one.
6. **Weapon weight drops to near zero while carrying.** `sg_arach.c:208` sets
   0.10; with tier-1 worth 1.20 that is still 0.12 → 170 ms. Acceptable. Do not
   raise it.

### D3 — Defending a stand: hold

The right weapon at a post is a function of the **sightline length** at that
post, because §1.1's spread saturation distances are hard numbers.

Measure the post's sightline once, at role assignment, as the longest clear
`gi.trace(eye, NULL, NULL, eye + 2000·dir, self, MASK_OPAQUE)` over the approach
directions the rune's links say enemies arrive from.

| Sightline `L` | Pre-held weapon | Justification |
|---|---|---|
| `L < 160` | **Super Shotgun** | 120 damage inside 128 u (§1.7); splash forbidden by R1 anyway |
| `160 ≤ L < 300` | **Rocket Launcher** | above `d_safe` 121 (R1); 220–240 on a direct hit, splash covers the corridor width |
| `300 ≤ L < 500` | **Chaingun** | 180 dps and full spread saturation to 437 u (§1.1) |
| `500 ≤ L < 900` | **Railgun** or **Chaingun** | railgun if the approach is a single line (no spread, pierces, `g_weapon.c:764-767`); chaingun if it is a wide room |
| `L ≥ 900` | **Railgun** | the only weapon with no degradation |

**D3a — pre-spin the chaingun is not possible.** §1.5: the spin-up costs 0.9 s
and ~13 bullets, and releasing at gunframe 14 jumps to 32
(`p_weapon.c:1213-1218`). A defender who hears an approach should start the
trigger *before* line of sight, accepting the 13 wasted bullets, if
`ammo ≥ floor + 13`.

**D3b — pre-select, do not pre-fire, the rocket launcher.** Switch cost is
0.8 s (§2.0), refire is 0.9 s (§1.10). Holding it costs nothing; raising it
mid-contact costs a full rocket cycle.

**D3c — grenade launcher for blind approaches.** §1.8: a level GL shot lands
300 u away after 0.5 s having risen 25 u, and the fuse is 2.5 s. That is a
usable area denial down a corridor the defender cannot see into. Rule F5 applies:
aim at the *impact point*, not a target.

**D3d — armour is the defender's item.** `sg_arach.c:207` already gives
`SG_ROLE_DEFEND` an armour bias of 0.50; with §2.3's zero-armour worth of 1.00
that is 0.50 → **533 ms** of detour budget for armour near home. Correct: §1.17
says 200 body armour triples effective HP against rockets and rails.

### D4 — Quad and rune contests

The question is always the same: *is the detour cheaper than the value, given
the respawn clock?*

**Quad.** Respawn 60 s fixed (§1.21), duration 30 s. So the quad is *live* for
at most half the time and, once taken, denied for 60 s.

* **Contest it** when `Worth_Quad × 1500 / (1 + D/1500) > D`, i.e. `D < D_max`
  from the §2.3 table. At tier 5 (`w = 2.0` after clamping) that is
  **1500 ms of detour** — a bot with a chaingun should walk 1.5 s out of its way
  for a quad. At tier 1 (`w = 0.76`) it is **744 ms**.
* **Camp it** only inside the last 4000 ms of the respawn clock (rule Q1). A bot
  standing on a pedestal for 20 s is a bot not playing CTF.
* **Never contest a quad an enemy already holds.** `is_quad` is not observable
  from outside; what *is* observable is `EF_QUAD`-class rendering, which CACO
  does not model. Treat a quad whose respawn clock has not started as taken and
  set worth to 0 (Q1 handles this).
* **Quad changes the ladder, and it changes R1.** With
  `quad_framenum > level.framenum` the chaingun is 720 dps and the SSG is 480 a
  shot; the §2.1 ordering does not change. What *does* change is self-damage,
  and it does not change uniformly — each weapon scales differently because of
  where in the fire function `is_quad` is applied:

  | Weapon | quad radius damage | quad `dmg_radius` | `d_safe` | self dmg at contact | self dmg 1 u inside `d_safe` | Cite |
  |---|---|---|---|---|---|---|
  | Rocket Launcher | `120 × 4 = 480` | **120, unchanged** | 121 u | **240** | 210 (at 119 u) | `p_weapon.c:869-873` |
  | Grenade Launcher | `120 × 4 = 480` | **160, unchanged** — `radius` is computed at `:791` *before* the quad multiply at `:792-793` | 161 u | **240** | 200 (at 159 u) | `p_weapon.c:791-793` |
  | Hand grenade | `125 × 4 = 500` | **165, unchanged** — same ordering | 166 u | **250** | 209 (at 164 u) | `p_weapon.c:629-631` |
  | BFG impact | **200, NOT scaled** — hardcoded literal, not the quad'd `damage` | 100 | 101 u | 100 | 75 (at 99 u) | `g_weapon.c:868` |
  | Plasma bounce | `39 × 4 = 156` | **`damage + 70` = 226** — radius is computed *from* the quad'd damage | **227 u** | **78** | 21 (at 225 u) | `plasma.c:51-54`, `:99` |
  | Plasma spread | `28 × 4 = 112` | **`damage + 70` = 182** | **183 u** | **56** | 21 (at 181 u) | `plasma.c:124-127`, `:158` |

  Two rules follow:

  1. **The rocket/grenade `d_safe` values do not move under quad** — the radius
     is unchanged, so past 121/161/166 units you still take exactly zero. But
     being one unit inside is now 200–240 damage, i.e. instant death from full
     health with 100 armour. Add a **60-unit safety margin** to `d_safe` for RL,
     GL and hand grenades whenever `is_quad` is true: 181 u / 221 u / 226 u.
     Sixty units is 0.1 s of rocket flight (§2.2) and roughly the aim error the
     bot's own tremor model admits (`sg_combat.c:75-76`).
  2. **The plasma's `d_safe` nearly doubles under quad**, to 227 u (bounce) and
     183 u (spread), because `PLASMA_SPLASH_RADIUS` is added to the *already
     quadded* damage (`plasma.c:99`, `:158`). A quad plasma is far more
     dangerous to its owner than a quad rocket is. The bot must recompute
     `d_safe` for the plasma from `is_quad`, not read it from a table.

  The BFG's detonation wave and in-flight lasers still skip the owner entirely
  (`g_weapon.c:822`, `:917`), so the BFG is the *only* splash weapon whose
  self-danger is unchanged by quad.

**Runes.** Four on the map, they relocate every 30 s (§1.22), one per player.

* A rune the bot does not hold is worth 566–830 ms of detour (§2.3). A rune the
  bot *does* hold makes every other rune worth **0** — `Pickup_Rune` refuses.
* **Haste is the contest worth having** at weapon tier ≥ 3: exactly 2× rate of
  fire (`g_runes.c:807-811`). A Haste chaingun is 360 dps.
* **Resist cancels Damage exactly** (1.75 both ways, §1.22). If CACO believes an
  enemy holds the Damage rune, raise the Resist multiplier from 1.45 to 1.80
  (→ 0.99, **910 ms**).
* **Do not chase a rune across a refresh boundary** (rule RU2). `Rune_Think`
  moves it every 30 s to a random `item_health_small` spawn; a 1500 ms detour to
  a position that is up to 1 s stale is the practical limit.

---

## Appendix — quick reference

### Damage per trigger pull (deathmatch, no quad, no rune)

| Weapon | Best case | Refire | DPS | Ammo/shot |
|---|---|---|---|---|
| Blaster | 15 | 0.5 s | 30 | — |
| Shotgun | 48 (12 × 4) | 1.2 s | 40 | 1 shell |
| Super Shotgun | 120 (20 × 6) | 1.2 s | 100 | 2 shells |
| Machinegun | 8 | 0.1 s | 80 | 1 bullet |
| Chaingun | 18 (3 × 6) | 0.1 s | 180 | 3 bullets |
| Hyperblaster | 15 | 0.1 s | 150 | 1 cell |
| Grenade Launcher | 120 + splash | 1.2 s | 100 | 1 grenade |
| Hand grenade | 125 + splash | 1.5 s tapped (1.2 s press-to-throw) | — | 1 grenade |
| Rocket Launcher | 100–119 + 120 | 0.9 s | ~240 | 1 rocket |
| Railgun | 100 | 1.6 s | 62.5 | 1 slug |
| BFG | 200 + 200 + wave | 2.5 s | — | 50 cells |
| Plasma (bounce) | 39 | 0.9 s | 43 | 10 cells |
| Plasma (spread) | 84 (3 × 28) | 0.9 s | 93 | 10 cells |
| Grapple (first hit) | 8 | — | — | — |
| Grapple (sustained) | 1 | 0.7 s | 1.4 | — |

### Splash minimums

| Weapon | `d_safe` | Self damage at contact |
|---|---|---|
| Rocket Launcher | 121 u | 60 |
| Grenade Launcher | 161 u | 60 |
| Hand grenade | 166 u | 62.5 |
| BFG (impact) | 101 u | 100 |
| Plasma bounce (on miss) | 110 u | 19.5 |
| Plasma spread (on miss) | 99 u | 14 |

### Splash minimums under quad (see §2.4-D4 for the derivation)

| Weapon | `d_safe` under quad | + recommended margin | Self dmg at contact |
|---|---|---|---|
| Rocket Launcher | 121 u (unchanged) | **181 u** | 240 |
| Grenade Launcher | 161 u (unchanged) | **221 u** | 240 |
| Hand grenade | 166 u (unchanged) | **226 u** | 250 |
| BFG (impact) | 101 u (unchanged, splash literal is not quadded) | 101 u | 100 |
| Plasma bounce | **227 u** (radius grows with damage) | 227 u | 78 |
| Plasma spread | **183 u** (radius grows with damage) | 183 u | 56 |

### Respawn clocks

| Class | Delay | Cite |
|---|---|---|
| Weapons | 30 s | `p_weapon.c:147` |
| Ammo | 30 s | `g_items.c:537` |
| Armour (all) | 20 s | `g_items.c:759` |
| Health (small/med/large) | 30 s | `g_items.c:648` |
| Mega health | 100 s decay + 20 s | `g_items.c:586-591`, `:594-595` |
| Quad | 60 s | `g_local.h:1513`, `g_items.c:2025` |
| Power screen / shield | 60 s | `g_items.c:1572`, `:1595` |
| Runes | never (relocate every 30 s) | `g_runes.c:10`, `:338-348` |
| Invulnerability | **removed from map at `ctfflags 16`** | `g_items.c:1360-1369` |
