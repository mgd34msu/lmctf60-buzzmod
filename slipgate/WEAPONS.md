# SLIPGATE weapon doctrine

This file records the current combat contracts implemented in
`slipgate/sg_combat.c`. Game source remains authoritative for weapon damage,
ammo use, projectile physics, armor, powerups, and runes.

## 0. Build and ruleset facts

### 0.1 Weapon balance

Default builds do not define `WEAP_BALANCE_OK`, so guarded
`CTF_WEAP_BALANCE` branches are inactive. Live values come from the normal
deathmatch branches. The shotgun spread change outside that guard remains live.

### 0.2 CTF flags

The default `ctfflags` value is 16, `CTF_OFFHAND_HOOK`.

- The grapple remains out of the normal weapon cycle.
- Grapple contact damage is enabled.
- Invulnerability entities are removed at spawn.
- Quad uses its fixed respawn rule unless configuration changes the flags.

Combat code reads live cvars and inventory. It must not assume a development
server configuration when the game exposes the actual state.

### 0.3 Timing

Default game simulation advances at 100 ms per frame. Weapon state advances
through the normal `Think_Weapon` and `Weapon_Generic` path. SLIPGATE requests a
weapon or attack button; it does not advance gun frames directly.

## 1. Physical weapon model

`sg_weapons` is the selection and lead table:

| Weapon | Projectile speed | Windup | Range cap | Ammo floor |
|-|-:|-:|-:|-:|
| Blaster | 1000 | 0 | none | 0 |
| Shotgun | hitscan | 0 | 560 | 3 |
| Super Shotgun | hitscan | 0 | 400 | 6 |
| Machinegun | hitscan | 0 | 740 | 30 |
| Chaingun | hitscan | 0 | 1100 | 90 |
| Grenade Launcher | 600 | 0 | none | 3 |
| Rocket Launcher | 650 | 0 | none | 4 |
| HyperBlaster | 1000 | 0 | none | 30 |
| Railgun | hitscan | 0 | none | 2 |
| BFG10K | 400 | 0.8 s | none | 50 |
| Plasma Rifle | 1200 | 0 | none | 34 |

Range caps and floors are preferences used by selection. The host weapon code
still decides whether a requested shot can fire.

### 1.13 Plasma

Plasma splash radius depends on mode and damage. Quad multiplies plasma damage,
so `Combat_SplashSafeDistance` recomputes the safe radius from the live mode and
quad state.

### 1.14 Grapple

The hook is movement equipment, never an ordinary `pers.weapon` choice.
Combat splash safety accounts for one frame of an active 800 u/s pull toward
the anchor.

### 1.17 Armor and survivability

Splash admission uses health plus armor that can absorb incoming damage.
Rocket and grenade selection is refused below a survivable pool of 90. Exact
damage and absorption remain in `g_combat.c`.

## 2. Combat doctrine

### 2.1 Range bands and ladders

Distance is measured from the bot eye to the target bounding-box center.

| Band | Distance | Preferred order |
|-|-:|-|
| Contact | `<128` | SSG, chaingun, hyperblaster, shotgun, machinegun, rail, plasma, blaster |
| Close | `128..399` | rocket, chaingun, hyperblaster, SSG, shotgun, rail, machinegun, grenade, plasma, blaster |
| Mid | `400..899` | rocket, rail, hyperblaster, chaingun, machinegun, shotgun, BFG, blaster |
| Long | `>=900` | rail, hyperblaster, blaster |

`Combat_WalkLadder` first requires the configured ammo floor, then retries with
any shootable ammo before falling back to the blaster. Weapon commitment and
switch hysteresis prevent a small range change from causing a switch ritual.
A dry held weapon bypasses the delay.

### 2.2 Firing solutions

#### R1: splash safety

- Rocket safe distance: 121 units.
- Grenade safe distance: 161 units.
- BFG impact safe distance: 101 units.
- Quad adds 60 units to rocket and grenade safety margins.
- Plasma uses its computed live radius.
- A splash shot is rejected if it threatens the bot or a live teammate.
- Rocket and grenade shots are rejected below the 90-point survivability floor.

Selection applies the same safety gates as firing so the bot does not raise a
weapon it will refuse to use.

#### F1: linear projectile lead

The solver uses projectile speed, windup, target velocity, and one refinement
pass. A moving-target lead must remain within the configured tolerance or show
stable target direction. Hitscan uses the current target point.

#### F4: grenade arc

Grenades use the live launch speed, vertical impulse, gravity, and fuse in a
stepped ballistic solve. The resulting arc must clear the world and remain
team-safe.

#### F5: unpredictable bounce

Grenade-launcher fire is limited to nearly static targets because behavior after
the first bounce cannot be predicted from the current trace.

The final shot command is rebuilt from the real weapon muzzle offset and
handedness. Eye clearance alone is not sufficient.

### 2.3 Item value

Weapon acquisition tiers are:

| Tier | Weapons |
|-:|-|
| 5 | rocket launcher, chaingun |
| 4 | SSG, railgun, hyperblaster |
| 3 | machinegun, grenade launcher, plasma |
| 2 | shotgun |
| 1 | blaster |

Route value depends on the best available tier, held-weapon ammo relative to
its floor, health, armor, power armor cell competition, public quad belief, and
rune ownership. A bot already holding a rune does not price another rune.
Role multipliers may change the value of an item, not the physical pickup rules.

### 2.4 Situational doctrine

#### D1: enemy carrier

Between 400 and 700 units, interception prefers rocket, then rail, then
chaingun. Normal range and splash gates still apply.

#### D2: own carrier

A carrier prefers weapons usable while moving: rail, chaingun, then SSG for a
close pursuer. Carry routing and combat remain separate authorities.

#### D3: posted defender

The defender pre-holds a weapon from its sightline length:

| Sightline | Weapon |
|-:|-|
| `<160` | SSG |
| `160..299` | rocket launcher |
| `300..499` | chaingun |
| `>=500` | railgun |

If the exact doctrine weapon is unavailable, the normal stocked band ladder is
used. Pre-holding is an idle decision; contact still uses normal target and
safety gates.

#### D4: quad and runes

Quad increases the rocket and grenade safety margin and expands plasma safety
through its damage-derived radius. Quad value scales with the bot's stocked
weapon tier and only exists when public item belief says the pad is routeable.
Rune value uses the live rune type, role, threat, and current inventory.

## Verification

Weapon changes must exercise the production selection, lead, muzzle, trace,
team-safety, and trigger paths. Source-text assertions are not combat tests.
Relevant host suites include combat aim, strike integration, item commitment,
defense combat, and the C reducers wired by both Make dialects.
