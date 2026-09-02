# Era-4 host-side rewrite: the units that were still pre-era-4 (2026-09-02)

The bot (carve, complex, movement, flights, artifact, locator, router, fire,
hook, mechanisms, generation, level owner, controller, driver, roster,
orders, combat, items, callouts) was rewritten in era 4.  These units were
not.  Ruling: everything is era 4.  Each row: what it does, who used it,
what replaces it.  Delete-first per unit as its replacement lands; the
module stays linkable at every step.

| Unit (lines) | What it does | Used by | Era-4 replacement |
|---|---|---|---|
| `sg_crc32` (73) | CRC-32 | artifact, identity | `sg_rune_crc`: own table-driven CRC-32 |
| `sg_bsp_world` (1,973) | Q2 BSP file reader (lumps, planes, nodes, leaves, brushes, sides, texinfo, models, visibility, entities) | carve, complex builder, mechanisms, hook, fire, visibility, generator, tools | `sg_rune_bsp`: own reader of exactly the lumps the RUNE uses, one struct, one load, one free |
| `sg_host_collision` (1,603) | Box traces against the world and inline models with a contents mask; pose classification | carve, complex builder, generator, hook, fire, tools | `sg_rune_trace`: own brush-hull trace (Q2 semantics: box expanded into brush planes, tree walk, contents mask, start solid, fraction, plane) |
| `sg_bsp_entity_semantics` + publication + audit (4,237) | Entity lump into records with kinds, activation, targets; audit and publication rituals | mechanisms | `sg_rune_entities`: own entity-lump parser (classname, keys, bmodel index, origin, angle, targets, spawnflags, speed, wait, lip, height, path corners) and mechanism kinds from classnames |
| `sg_host_engine_pmove`, `sg_host_pmove`, `sg_host_hook_law`, `sg_host_mechanism_law`, `sg_host_engine_runtime`, `sg_host_law_owner`, `sg_host_law_publication`, `sg_host_rocket_jump_law.h` (~6,900) | Host law owner: publishes engine facts (gravity, frame, substep, jump, hook pull, mover equations, rocket-jump launch), routes the bots' pmove and hook pull through a parity production, commits the level identity | p_client.c (bot pmove), p_weapon.c (bot hook pull), g_func.c/g_trigger.c (mover publication), g_main.c/g_spawn.c, the generator, level owner, driver, controller | `sg_rune_law`: the law values from the engine's constants and cvars, filled at level start and at generation; bots move through the same Pmove and the same hook pull as humans (the parity branches in the game code go) |
| `sg_identity`, `sg_rune_source_authority`, `sg_rune_v2_content_identity` (1,067) | Level identity (BSP checksum, entity CRC, physics), spawn records | g_main.c, g_save.c, g_spawn.c, generator | `sg_rune_identity`: own BSP file CRC, entity lump CRC, law hash; one struct in the artifact header |
| `sg_rune_model` (2,154) | Era-3 record vocabulary (types shared by collision, wire, entity semantics, destinations, weapon profiles) | the units above | gone with them |
| `sg_destination`, `sg_belief_contract.h`, `sg_weapon_contract.h`, `sg_rune_v2_wire.h`, `sg_action_contract.generated.h` | Era-3 contracts | law owner header, weapon profile | gone |
| `sg_weapon_effect_profile` (1,263) | Weapon profiles (family, speed, splash, spread, damage, cadence) with correction rituals | combat | `sg_bot_weapons`: own profile table from `sg_weapon_host_constants.h` (engine facts stay as constants) |
| `sg_persona` (348) | Names and trait scalars | roster, combat, callouts | `sg_bot_persona`: own table and lookup |
| `sg_client_ownership`, `sg_pov_identity` (113) | Which clients are bots; POV identity | roster, driver, hooks, p_view.c | the roster answers ownership; POV identity gone |
| `sg_hooks` (483) | `sg_host` function table over `gi` | combat, identity, persona, util | direct `gi` calls |
| `sg_net` (953) | Message and sound bridge (bprintf, sound events to the bots) | g_main.c, g_spawn.c, g_cmds.c, roster, driver | `sg_bot_net`: own sound and message taps, only what the bots hear |
| `sg_util` (155) | Team, flag, distance helpers | roster, orders, items, combat, callouts, driver | `sg_bot_util`: own helpers |
| `sg_cvars` (42) | Slipgate cvars | game code, bot units | `sg_bot_cvars`: own registration |
| `sg_hooks_test`, `sg_*_test.c`, fixtures under `slipgate/` | Era-1 to era-3 tests | nothing | deleted with their units; era-4 tests stay under `tests/` |

Order: crc, bsp, trace, entities, law (with the game-side parity branches removed), identity, weapons, persona and ownership, util and cvars, hooks and net, tests, project files.
