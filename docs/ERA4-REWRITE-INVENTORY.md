# Era-4 rewrite inventory

Recorded 2026-09-02 before any further deletion. Every bot system the
module has or had, what it did, and what era 4 does about it. Status
words: **built** (era-4 unit exists and is tested), **to build** (era-4
replacement is owed), **dropped** (not carried, with the reason), **host**
(engine binding, not bot behaviour, stays as is).

## A. Systems whose legacy units are already deleted

| System | What the legacy unit did | Era 4 |
|---|---|---|
| Map knowledge (rune graph: seeds, links, codec, install, proof, topology, reverse boundary, seed game, door frontier, hook frontier, v2 codec/snapshot, file, stream, runtime, binding, authority game, update source) | A node/link graph proved offline, loaded and re-verified in game | **built**: cell complex, movement records, artifact (`sg_rune_cx*`, `sg_rune_movement*`, `sg_rune_artifact`), generation (`sg_rune_generate`, `sv rune`), level owner (`sg_rune_level`) |
| Cost fields over the graph (`sg_fields`, field projection, field key) | Per-objective potentials over seeds, refreshed per frame | **built**: per-destination field over (cell, stance) (`sg_rune_field`) |
| Route choice and descent (`sg_move` 9,999 lines, `sg_descend` 3,984, route dither, traversal transition, feeler probe, crowd pass, team collision) | Pick the next link, commit to it, unstick, dither lanes, avoid teammates | **built** for the route (step selection, executor, stuck hop in `sg_bot_frame`); **to build**: teammate avoidance and lane offset (small, in the driver) |
| Goals and objectives (`sg_goal`, `sg_price`, `sg_pickup_target`, item route, defense supply, intercept policy) | Frame goals: carry, recover, escort, defend, attack; item detours priced by role; defender sorties | **built** for flag goals (roles and destinations in `sg_bot_frame`); **to build**: item detours (health, armor, weapons, ammo, powerups) as destinations weighed against the goal |
| Team offense (`sg_strike`, strike adapter) | Once-per-frame team duty assignment: who carries, recovers, escorts, rallies | **to build**: team-level role assignment across bots (currently each bot decides alone) |
| Escort and defense (`sg_defense_shift`, escort dose, defense facing, `sg_tilt`, `sg_lead`) | Escort candidate scoring, stand defender shifting to threats, pressure memory, early item-pad returns | **to build**: defend post choice and escort spacing; **dropped**: tilt and lead as designed (graph-bound) |
| Danger memory (`sg_danger*`) | Graph-bound learned danger per team | **dropped**: learning is out of the RUNE and runtime by ruling; live threat handling belongs to combat |
| Hook rides (`sg_hook_game` 2,057, `sg_hook_live`, `sg_hook_oracle`, chain hook replay, hook diagnostics, hook visibility feasibility 7 units) | Proved ropes from graph links, replayed and witnessed live | **to build**: hook reach per cluster in the RUNE, hook capability records, executor already has fire/ride/release |
| Jumps, drops, swims, rocket jumps, pushes (`sg_drop_live`, `sg_swim_live`, `sg_rocketjump_*`, `sg_push_*`, compound drop/swim/hook, human speed adapter) | Per-link controllers with witnessed 25 ms proofs | **built**: jump, drop, rocket jump as traced flights plus executor laws; swim as records; **to build**: push (trigger_push) and water surface exit polish |
| Mechanisms (`sg_rune_mechanism_catalog/plan`, compound guard, mover lease, button live, mechanism timeline, delayed use ticket, declared door guard, door approach, relay walls, timed vaults, train station and gate, shoot door, oracle 9,742 lines, water forest) | Doors, buttons, lifts, trains, teleports, shootable doors, timed vaults, relay walls: planned, claimed, and executed through the engine's own callbacks | **to build**: era-4 mechanisms section in the RUNE (records bound to the portals they gate) and the runtime for doors, lifts, buttons, teleports, trains; until then bots route only through what is open at generation |
| Belief and perception (`sg_belief` 4,857, perception evidence, belief runtime, compact belief perception) | Where enemies probably are, from sight, sound, damage; carrier projection along routes | **to build**: perception in the era-4 combat runtime (seen, heard, hurt-by), simple and live; **dropped**: the belief-cell distributions |
| Human trace recorder, replay, action contract | Proof witnesses for the graph | **dropped** |
| Learning, sidecars | Post-match priors written beside the artifact | **dropped** by ruling |

## B. Systems still standing as legacy units (to be rewritten)

| Unit | What it does | Engine entry points that must keep existing | Era 4 |
|---|---|---|---|
| `sg_combat.c/.h` (4,820) **deleted 2026-09-02; era-4 `sg_bot_combat` built** | Enemy scan and engagement; live enemy, pursuit, lost-aim/lost-hold; weapon state (held, ammo, availability, splash-safe distance, survivable self-damage); team splash and hitscan safety by trace; range bands with deadband; doctrine ladders and posted-defender pre-hold; switch hysteresis; aim: tremor, Fitts swing, overshoot correction, wander, constrain to target box, exact per-weapon muzzle offsets, machinegun recoil, packed-view reconstruction, velocity-stable lead, ballistic solve, stepped grenade; item worth (health, mega, armor, weapon tiers, ammo, quad, runes); trigger discipline; idle; hit accounting; aim self-tests; era-3 belief and weapon-field attachments | `SG_CombatHit` (g_combat.c), `Combat_ResetClient` (p_client.c), `SG_CombatSkill` (sg_client.c), executor asks launcher-ready and request-launcher | **to build**: era-4 combat runtime over `sg_weapon_effect_profile` (host parity: 14 profiles, 10 families incl. hitscan, effect flags) and, when generated, the RUNE's per-cell weapon relations. Carry as fragments only the engine facts: muzzle offsets, recoil kick, packed view, grenade pitch law, splash radii, safety traces |
| `sg_chat.c/.h` (3,821) **deleted; era-4 `sg_bot_orders` built (orders in); callouts to build** | Team chat: callouts (items taken and timers, carrier seen, enemy seen near), radio, greetings, captures, deaths, level open/end, idle, hurt; parsing human orders (roles, escort, item calls) and replies; place naming from landmarks | `SG_ChatHear`, `SG_ChatDeath`, `SG_ChatLevelEnd`, `SG_ChatResetClient`, `SG_ChatFrame` (g_cmds.c, p_client.c, p_hud.c); driver uses `SG_ChatOrderedRole`, `SG_ChatEscortTarget` | **to build**: communication over era-4 state (orders in, callouts out); until then a small order parser so humans can order roles, and no callouts |
| `sg_caco.c` (2,578) **deleted; sight and hearing live in `sg_bot_combat`** | Perception noting: deaths, sounds, damage and its bearing, rail rhythm, item pickups; item belief rows with respawn clocks; enemy places; carrier projection along routes; human relay; hit sense | `SG_NoteDeath`, `SG_NoteDamage`, `SG_NoteSound`, `SG_NoteItemTaken`, `SG_NoteItemRejected`, `SG_NoteRailShot`, `Caco_ResetClient` (g_combat.c, g_items.c, p_client.c, p_weapon.c) | **to build**: folded into the era-4 combat runtime's perception (the note entry points stay, feeding it) |
| `sg_client.c` (594) **deleted; era-4 `sg_bot_roster` built** | Roster: add/remove/list/kick bots, botfill cadence, slot lifecycle, retire and disown, persona name choice | `SG_AddBot`, `SG_AddBotTeam`, `SG_RemoveBots`, `SG_RemoveBotNamed`, `SG_KickWorst`, `SG_ListBots`, `SG_RetireBotForClient`, `SG_DisownBot`, `SG_RosterStorageReset`, `Botfill_Reset`, `SG_InternalClientConnect`, `SG_OwnsBot` (g_svcmds.c, g_menu.c, p_client.c, g_save.c, g_main.c, g_cmds.c, p_weapon.c) | **to build**: `sg_bot_roster.c`, same command surface, over the new `sg_bot_t` |
| `sg_persona.c` (269) **vetted and kept: a data table** | Names and trait scalars (aggression, range bias, hook scale, camp scale, aim grade, banter) | none from the engine | **to build**: keep the table as a fragment; traits become inputs to combat and role choice |
| `sg_net.c` (905) | Bot voice and text through the engine message stream; `SG_BotClientCommand` (bots issue client commands); spawn and free of engineless client edicts | `SG_NetInstall`, `SG_NetNewLevel` (g_main.c, g_spawn.c), `SG_BotClientCommand` (ctf_sqlite_unidb.c), `SG_SpawnClientEdict`, `SG_FreeClientEdict` | **host**: keep (it is the bridge that makes a bot a client) |
| `sg_hooks.c` (338) | The host table: prints, traces, PVS, cvars, sounds, allocation behind function pointers | used everywhere via `sg_host` | **host**: keep |
| `sg_util.c` (533) **reduced to team, flag, sight, timer helpers** | Team helpers (`SG_EnemyTeam`, `SG_TeamIdx`), flag helpers (`SG_FlagCarrier`, `SG_OwnFlag`, `SG_EnemyFlag`, `SG_FlagStand`), `SG_CanSee`, timers; plus graph-era leftovers (declared command, escort terminal, lift wait/rider/rest, teleport approach, swim/supported arrived, hook and blaster aim angles) | driver uses `SG_EnemyTeam`, `SG_FlagCarrier` | **fragments**: keep team, flag, sight, timer helpers; delete the graph-era rest |
| `sg_clock.c` (192) **deleted** | Score posture from caps and time: defend shift, cover scale | `Clock_Frame` from the driver | **to build**: role assignment input (small) |
| `sg_weights.c` (272) **deleted** | Fitted role weight tables per map file | `SG_WeightsPrint/Reload` (g_svcmds.c) | **dropped** unless the era-4 role assignment wants tuned weights; the sv commands go with it |
| `sg_identity.c`, `sg_pov_identity.c`, `sg_client_ownership.c`, `sg_human_speed.c`, `sg_cvars.c`, `sg_rune_source_authority.c`, `sg_weapon_effect_profile.c`, `sg_action.c` | Level identity capture, POV identity, bot ownership, strafe-jump landing adapter, cvars, entity spawn records, weapon profiles, generated action metadata | `SG_LevelIdentity*`, `SG_BotPOV*`, `SG_OwnsBot`, `SG_HumanSpeed*`, `SG_CvarsInit`, `sg_cv.*`, `SG_RuneSourceAuthority*` | **host**: keep; `sg_action` dropped once nothing includes it |

## C. What a bot needs to play, and where each comes from

1. Know the map: RUNE (built).
2. Find where it is and where to go: locator, field, step (built).
3. Move: executor into the host's client think (built, untested in game).
4. Doors, lifts, buttons, teleports, trains: mechanisms (to build).
5. Hook: hook reach and records (to build).
6. Fight: perception, weapon choice, aim, fire, safety (to build, era-4
   combat runtime over the weapon profiles and the RUNE's weapon relations).
7. Pick up items: item destinations weighed against the goal (to build).
8. Play as a team: role assignment, escort, defend (to build; single-bot
   roles built).
9. Talk and take orders: communication (to build; order parsing first).
10. Join and leave: roster (to build; same command surface).

## C2. Built since (2026-09-02)

Roster (`sg_bot_roster`), orders (`sg_bot_orders`), combat
(`sg_bot_combat`: sight, hearing, weapon ranking by expected damage per
second from the profiles, led and arced aim, slewed view with tremor,
one trace per shot, splash safety), item detours priced by the goal field
(`sg_bot_items`), the team role pass (in `sg_bot_frame`). In-game
generation of bctf01 runs in 19 s through `sv rune`; the level owner loads
it; four bots join.

## D. Order

Roster, then combat (perception, choice, aim, fire, safety), then item
destinations, then team roles, then mechanisms, then hook, then
communication. Each replacement lands with its test; the legacy unit is
deleted in the same commit.
