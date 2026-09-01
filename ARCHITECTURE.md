# LMCTF BuzzMod architecture

This document maps the current source tree and its runtime boundaries. It is a
reference for changing the code, not a roadmap or an experiment log. Remaining
work and execution order live in
[`PROJECT-COMPLETION-PLAN.md`](PROJECT-COMPLETION-PLAN.md).

The RUNE subsystem is in a foundational migration. Sections explicitly marked
**current** describe code that still runs; sections marked **target** define the
replacement contract. A target contract is not evidence that its implementation
exists.

## Shipped and server-only boundaries

The project has three distinct artifact classes:

- **Public release assets:** one platform game module plus
  `assets/lmctf6-buzzmod.pak`, `VERSION`, and `SHA256SUMS`.
- **Server runtime data:** the two module aliases, pak, production config,
  BSP/RUNE pairs for all supported maps, the ordered fleet map list, and any
  applicable sidecars. These belong to one authenticated server-bundle
  manifest even when they are not public download assets.
- **Development inputs and evidence:** generators, readers, tests, human/demo
  corpora, reports, and retained run receipts. These do not belong in the game
  directory merely because they are tracked by Git.

The compact RUNE builder is an unshipped Linux-only generator game module. It
may link the offline configuration solver; the public and server runtime game
modules only load accepted RUNEs and remain solver-free. Corpus staging uses
the generator module for `sv rune`, then the runtime module for cold load.

Code that moves an artifact across one of these boundaries must name the exact
source and destination, verify content identity, and fail without partially
publishing a replacement.

## Game module lifecycle

`g_main.c:GetGameAPI` exports the Quake II game-module interface. The important
runtime sequence is:

1. `InitGame` registers cvars and initializes process-lifetime state.
2. `SpawnEntities` loads one map, initializes LMCTF entities, and establishes
   SLIPGATE level identity and map-local state.
3. `G_RunFrame` advances entities, game rules, SLIPGATE, and client end-frame
   presentation.
4. `ExitLevel` closes map-local authorities and queues `gamemap`; the engine
   loads the next map without terminating `q2ded`.
5. module shutdown flushes process-lifetime resources.

The configured non-random map list is read in `g_save.c`. Its file order is
preserved. Startup aligns the current map with the sequential cursor, and
`EndDMLevel` advances and wraps that cursor. The random-map compile-time branch
has separate selection behavior and does not consume the sequential cursor.

## LMCTF host layer

The root `g_*.c` and `p_*.c` files own CTF rules, entities, weapons, clients,
HUD/layouts, map transitions, logging, and persistent statistics. This layer is
the authority for player-visible outcomes:

- `Touch_Item` and `ctf_flagtouch` decide flag pickup, return, and capture;
- `T_Damage`, `Killed`, and `player_die` decide damage and deaths;
- `p_stats.c`, StdLog, and the SQLite backends record authoritative counters;
- `BeginIntermission`, boards, and match reports publish end-of-map results;
- observer/chase code owns client demo recording and spectator presentation.

SLIPGATE may choose commands and consume public events, but it does not bypass
these host outcomes.

Root `sg_*.c` paths are tracked symlink aliases for corresponding
`slipgate/sg_*.c` implementations. Edit the `slipgate/` target, not both names.

## SLIPGATE runtime pipeline

SLIPGATE bots are native fake clients owned by `slipgate/sg_client.c`.
`SG_OwnsBot` is the ownership boundary; stock monster AI and human clients do
not enter the bot controller through a flag or name heuristic.

At map load, `SG_LevelChange` retires the previous level and `SG_LevelSetup`
binds the new map/RUNE/runtime state. Each `SG_RunFrame` then moves information
through these layers:

1. **Perception (`sg_caco.c`)** records earned sight, sound, damage, item, flag,
   and teammate information. Enemy state is an aging belief, not a direct
   entity lookup granted to decision code.
2. **Team assignment (`sg_arach.c`, `sg_strike*.c`)** assigns attack, defend,
   carry, recover, and escort responsibilities from public team/objective
   state.
3. **Goal and cost fields (`sg_fields.c`, `sg_goal.c`, `sg_price.c`)** compose
   route cost from objectives, items, danger, cover, support, and role policy.
4. **Route descent and actuation (`sg_descend.c`, `sg_move.c`)** select a RUNE
   link and emit a `usercmd_t`. Compound actions keep explicit ownership,
   replay, guard, and completion state across frames.
5. **Combat (`sg_combat.c`)** selects weapons, aims, and fires through normal
   host weapon/client paths.
6. **Presentation (`sg_net.c`, `sg_chat.c`, personas and identity code)** makes
   bots ordinary visible clients while keeping identity and communication
   authority explicit.

Host events enter SLIPGATE through named hooks such as `SG_NoteItemTaken`,
`SG_NoteDamage`, `SG_NoteSound`, and `SG_NoteDeath`. A new behavior that needs
event information should extend a deliberate hook instead of polling private
host state from an unrelated controller stage.

## RUNE navigation lifecycle

A `.rune` is an exact-BSP-bound spatial capability artifact. It is not a generic
waypoint file, an objective route, or a script of movement actions.

### Current implementation being replaced

The current `rune_seed_t`/`rune_link_t` representation is a fixed-grid graph.
`sg_rune.c` and `sg_rune_seed_game.c` sample ground and face anchors, prove
action-labelled links, and prune around two objective roots. `Field_Flood`
performs reverse Dijkstra over those links, and `Think_PickLink`/`sg_move.c`
allow the selected link action to own execution.

That implementation remains present only as migration input. Fixed-grid and
face-anchor coverage, objective-driven pruning, objective-gated artifact
validity, action-owned traversal, and production late-path Dijkstra are not
target architecture. Objective reachability is a runtime diagnostic only;
configuration-space structure owns artifact validity.

### Target static model

The exact BSP is immutable ground truth. The RUNE sidecar binds it rather than
copying its bytes. Construction derives these target sections:

1. **Configuration cells.** Convex or adaptively subdivided regions containing
   every player origin at which the standing or crouching hull fits. Cells name
   BSP leaf/area/cluster provenance, contents, bounds, defining half-spaces,
   stance, support, water, hazard, and mover-relative properties.
2. **Geometric portals.** Shared player-hull-valid boundaries between cells.
   Portals describe continuity, directionality, polygonal extent, clearance,
   and contents changes. They do not prescribe a movement command.
3. **Static landmarks.** Flags, item pads, weapons, armor, health, powerups,
   trigger volumes, mechanism entries, defensive locations, and other BSP/entity
   facts localized into cells. Live availability remains runtime state.
4. **Capability regions and kernels.** Directional reachability and time-cost
   laws for ground movement, crouching, ramps, jumps, drops, water, air control,
   hook visibility/pull/release/coast, and other supported physics. A capability
   establishes what is possible; it does not own runtime traversal.
5. **Mechanism discontinuities.** Exact entity topology, activation path,
   controller identity, dwell/travel timing, entry region, and exit region for
   doors, buttons, lifts, trains, pushes, teleporters, and related stateful
   systems.
6. **Weapon response fields.** Visibility/occlusion regions and compact kernels
   for hitscan, rail, spread and cones, straight bolts, rockets and splash,
   grenades and bounce or fuse, BFG, and special laws over the shared cells and
   surfaces. Weapon families do not duplicate the BSP.

The position basis is three-dimensional. Directional speed, support state,
stance, water/air state, and time form the local phase basis used by capability
and field solvers. The wire format stores compact basis and kernel data, not a
dense value for every point, velocity, destination, player, or weapon.

### Target construction boundary

Construction follows one authority order:

```text
complete BSP and entity parse
  -> standing/crouching player-origin configuration space
  -> exact cells and split-carried geometric portals
  -> contents, landmarks, mechanisms, visibility, and occlusion regions
  -> analytic movement and weapon fields over the shared cells
  -> canonical compact indexes
  -> deterministic wire serialization
```

Configuration-space construction expands solid brushes by the player hull, or
equivalently erodes free space. It preserves adjacency while partitioning at
real geometry, mechanism, movement-law, visibility, occlusion, and
weapon-response discontinuities. Exact host box traces, Pmove, rays, and weapon
laws support the two-map development checker, not production acceptance.

Hook construction separates two questions:

- Can the actual muzzle locus see a non-sky hookable surface through the BSP?
- Can the player hull traverse the pull, release, coast, air-control, and
  possible relaunch envelope under the bound map physics?

Map gravity, air acceleration, maximum velocity, frame cadence, contents, and
movement law are identity-bound inputs. Batching may bound working memory but
must resume until fixed-point exhaustion. Overflow fails construction instead
of publishing partial coverage.

### Target runtime boundary

Runtime responsibilities are layered:

```text
strategy plan
  -> current destination
  -> destination-specific cost field over the RUNE
  -> tactical movement/combat choice
  -> exact mechanism or physics controller when needed
  -> ordinary usercmd and host outcome
```

Static destinations may cache fields. Moving, dropped, displaced, or arbitrary
destinations update fields incrementally through a coarse region hierarchy. The
solver accounts for directional/time-weighted capability costs; structural
connectivity comes from cells and portals, not route repair.

Strategy owns typed conditional goal queues, destination commitment, priority,
alternatives, completion, cancellation, and replacement. Tactics may dodge,
fight, seek cover, hook, strafe, wait, or reposition without erasing the plan.
Temporary threat and opportunity terms may deform the local field but cannot
create permanent minima or replace strategic authority implicitly.

Players are never RUNE records. Earned visual, sound, damage, pickup, and team
observations update sparse per-player runtime beliefs over position, velocity,
movement state, confidence, and future time. Combat combines those beliefs with
the RUNE's weapon response fields and bound weapon profiles. An exact trace runs
immediately before firing a shot or hook.

### Wire, publication, and acceptance

The target codec remains versioned, fixed little-endian, allocation-bounded,
checksum-protected, and exactly bound to BSP, entity semantics, physics ABI,
configuration schema, and map physics. It replaces seed/link/action and
objective-route fields with cells, portals, movement fields, weapon-response
regions and kernels, landmarks, and mechanism references.

Loading still decodes into an unpublished candidate. The candidate becomes
visible only after linear identity, format, count, span, reference, order,
finite-value, checksum, and load checks. Same-directory staged writes,
revalidation, sync, and rename preserve atomic publication. Failure does not
replace the current artifact or expose mixed sidecar state.

The runtime loader and the command-line reader call one frozen C wire inspector.
GNU and Make compile that source but do not define separate parser contracts.
Every artifact passes the inspector and a fresh-process bot load. Deep BSP and
host comparison is development-only for one ordinary RUNE and `tomb05`. Flag
reachability is a gameplay query, not artifact validity.
`tools/rune-corpus-maps.txt` remains the 175-map conversion authority;
`tools/topmaps.txt` remains an ordinary schedule.

## Sidecars and analysis data

Binary sidecars carry optional map/RUNE-bound human movement, flag-live,
escape, defense, and danger inputs. Their headers bind them to the exact RUNE
identity and payload checksum. Runtime loaders reject mismatched identity or
malformed payloads rather than adapting them to a different spatial model. The
learning sidecar schema remains subject to migration review. No legacy
seed/link repair sidecar is loaded, generated, staged, or released.

Tracked analysis JSON is not automatically runtime authority. A report used to
accept the final build needs a capture receipt binding its demo/log, source,
module, BSP, RUNE, config, map, participant, and time interval. The ownership
and retention rules are in
[`docs/repository-hygiene.md`](docs/repository-hygiene.md).

## Persistence and presentation

`p_stats.c` owns map/session counters. `ctf_file_io.c` implements per-player
storage, while `ctf_sqlite_core.c`, `ctf_sqlite_player.c`, and
`ctf_sqlite_unidb.c` implement the shared SQLite paths using the vendored
`sqlite3.c` amalgamation. UI/layout code consumes the same authoritative
counters; it does not maintain a competing result model.

Observer recording is map-local. Every native map transition must close the
current recording before `gamemap`; a subsequent map needs a new recording
authority and a separately completed file receipt.

## Build and verification

`GNUmakefile` and `Makefile` are independent supported build and test dialects.
Both aggregate the C host tests and shell integration checks. GitHub Actions
adds GCC and Clang host matrices, Linux module and link checks, Windows x86 and
x64 builds, and warning rejection. The project does not require Python.

Tests have three different scopes and must not be confused:

- pure reducers and host fixtures prove local state laws;
- integration tests prove the real source wiring and executable call chain;
- isolated engine runs prove map/runtime behavior and produce hash-bound
  receipts.

Final acceptance requires the scope named by the completion plan; a narrower
test cannot stand in for a real engine, corpus, fleet, or outcome gate.
