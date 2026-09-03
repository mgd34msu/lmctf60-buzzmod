# Era-4 review: every slipgate unit, its origin, and what it still depends on (2026-09-02)

Lens: is the unit here because era 4 needs it, written for era 4, or is it
here so older code keeps working?  "Written" means composed in this
session from the design, the map format, the engine's method, or the game
and engine sources; "reproduced" means the previous unit's shape was
reproduced under a new name; "engine facts" means numbers read from the
engine or game source with the line named.

| Unit | Origin | Depends on | Verdict |
|---|---|---|---|
| `sg_rune_bsp` | written: Q2 BSP lump layouts, own arena and validation, file and entity CRCs | `sg_rune_crc` | era 4 |
| `sg_rune_trace` | written: the engine's clipping method (plane offset by extents, enter/leave fractions, near side first) in own code; poses and sweeps on top | `sg_rune_bsp` | era 4 |
| `sg_rune_entities` | written: entity text parser, kinds by classname, links by target name | `sg_rune_bsp` | era 4 |
| `sg_engine_facts.h` | engine facts: every number named to its source line | none | era 4 |
| `sg_rune_law` | written: the law record, hulls and speeds from the facts, the hook's pull bands, the rocket jump derived from the game's jump, muzzle, damage and knockback rules | facts, `sg_rune_crc` | era 4 |
| `sg_rune_crc` | written: table-driven CRC-32 | none | era 4 |
| `sg_configuration_cells`, `sg_configuration_semantics` | era-4 carve (earlier in this work), ported off the old collision authority onto the trace and the law; own types | bsp, trace, law | era 4 |
| `sg_rune_cx`, `sg_rune_cx_build` | era-4 complex, ported onto the new reader | bsp | era 4 |
| `sg_rune_movement`, `sg_rune_movement_build`, `sg_rune_flight`, `sg_rune_analytic` | era 4; movement now reads speeds, the jump and the rocket jump from the law | law | era 4 |
| `sg_rune_mechanisms` | era 4, its input side rewritten onto `sg_rune_entities` | entities, bsp | era 4 |
| `sg_rune_hook`, `sg_rune_vis`, `sg_rune_fire`, `sg_rune_fire_build` | era 4; hook speeds from the law, grenade and rocket numbers from the facts | law, facts | era 4 |
| `sg_rune_artifact` | era 4; identity is now map CRC, entity-text CRC, law CRC; law from `sg_rune_law` | law, crc | era 4 |
| `sg_rune_locate`, `sg_rune_field`, `sg_rune_generate`, `sg_rune_game_generate`, `sg_rune_game`, `sg_rune_level` | era 4; generator and level owner build law and identity themselves; the level owner keeps the live entity text | bsp, law | era 4 |
| `sg_tactic_controller` | era 4; own hook phase type; speeds and the rocket jump from the body's law | law | era 4 |
| `sg_bot_frame`, `sg_bot_roster`, `sg_bot_orders`, `sg_bot_callout`, `sg_bot_items` | era 4; roster owns bot identity for the chase cam and the client bridge | bridge, persona | era 4 |
| `sg_bot_combat` | era 4; weapon sections rewritten to the era-4 weapon record and the fire relations | weapons, level | era 4 |
| `sg_bot_weapons` | written: each weapon's reach in fire-relation terms, launch, hit, burst, spread, cadence, from the facts | facts, fire | era 4 |
| `sg_bot_host` | written: staged messages dropped for bots, prints silenced, sounds heard, command injection, client slots | game import table | era 4 |
| `sg_bot_persona` | written: names and leanings, occupancy-aware pick, binding by client | none | era 4 |
| `sg_bot_cvars` | reproduced from the old cvar unit's shape: the five console variables the user types, the X-macro, the init name | none | reproduced by decision: the names are the user's controls |
| `sg_bot_util` | reproduced in substance: two-way team lookups, a flat distance, two timer stamps | none | trivial helpers |

Removed with nothing carried: `sg_bsp_world`, `sg_bsp_entity_semantics` (+publication, audit), `sg_host_collision`, `sg_host_engine_pmove`, `sg_host_engine_runtime`, `sg_host_hook_law`, `sg_host_law_owner`, `sg_host_law_publication`, `sg_host_mechanism_law`, `sg_host_pmove`, `sg_host_rocket_jump_law.h`, `sg_weapon_host_constants.h`, `sg_identity`, `sg_rune_source_authority`, `sg_rune_model`, `sg_rune_v2_content_identity`, `sg_rune_v2_wire.h`, `sg_destination`, `sg_belief_contract.h`, `sg_weapon_contract.h`, `sg_action_contract.generated.h`, `sg_mechanism_kinds.h`, `sg_hooks`, `sg_net`, `sg_client_ownership`, `sg_pov_identity`, `sg_persona`, `sg_persona_assignment.h`, `sg_crc32`, `sg_cvars`, `sg_util`, the pickup headers, `sg_sound_policy.h`, the hook-visibility internal header, and every test of theirs.

Game code: the era-3 branches that routed the bots' pmove and hook through a law production are gone; bots move and hook through the same code as humans.  The numbers era 3 had pulled into a header are back as the game's own literals.

Windows: with the Linux-only library dropped from the Windows link, the module cross-compiles and links under MinGW (`make PLATFORM=Windows ARCH=x86_64 CC=x86_64-w64-mingw32-gcc`); it has not been run on Windows.
