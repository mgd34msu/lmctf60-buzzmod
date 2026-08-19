# LMCTF BuzzMod architecture

This document maps the current source tree and its runtime boundaries. It is a
reference for changing the code, not a roadmap or an experiment log. Remaining
work and execution order live in
[`PROJECT-COMPLETION-PLAN.md`](PROJECT-COMPLETION-PLAN.md).

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

A `.rune` is a map- and build-bound navigation artifact. It is not a generic
waypoint file.

### Contract generation

`slipgate/rune_actions.json` is the authoring source for action identifiers and
their controller/plan requirements. `tools/gen_rune_contracts.py` emits the C
and Python contract tables. Tests require the generated tables to match the
authoring JSON.

### Map generation

`slipgate/sg_rune.c` floods fixed-grid seeds and proves links with the actual
movement/oracle code. It adds ordinary movement, water, grapple, lift,
teleport, declared-door, and button-controlled traversal only after their
specific proof gates pass. The mechanism catalog captures exact entity
topology and materializes plans for actions that need runtime controllers.
Objective-core pruning retains only graph state that is usable with respect to
both flag roots.

`sv rune` generates and atomically installs the current map's artifact in the
active game directory. It is a generator entry point, not corpus acceptance.

### Wire format, loading, and publication

`sg_rune_codec.c` validates the fixed little-endian wire format, section
arithmetic, identities, action legality, graph/mechanism shape, and CRCs.
Artifact loading decodes into an unpublished candidate and publishes only after
the whole candidate and its live mechanism bindings validate. A failed load
does not replace the previously published graph.

`sg_rune_install.c` and the sidecar store use same-directory temporary files,
revalidate authority, rename the accepted file, and clean owned temporary state
on failure.

### Independent acceptance

`tools/rune_corpus_controller.py` is the 181-map conversion controller. A map
may report PASS only when generation succeeds and the frozen GNU C reader, Make
C reader, Python reader, linter, applicable semantic checker, and separate
fresh-process cold load agree on the artifact. `tools/rune-corpus-maps.txt` is
the 181-map conversion authority; `tools/topmaps.txt` is only the ordered
20-map production fleet list.

## Sidecars and analysis data

Binary sidecars carry optional map/RUNE-bound human movement, flag-live,
escape, defense, and danger inputs. Their headers bind them to the exact RUNE
identity and payload checksum. Runtime loaders reject mismatched identity or
malformed payloads rather than adapting them to a different graph. `.snag`
repairs use a separate strict text format and only add bounded field-cost
surcharges; they do not alter RUNE graph records or proofs.

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

`GNUmakefile` and `Makefile` are independent supported build/test dialects.
Both aggregate the C host tests and Python/tool tests. GitHub Actions adds GCC
and Clang host matrices, Linux module/link checks, Windows x86/x64 builds, and
warning rejection.

Tests have three different scopes and must not be confused:

- pure reducers and host fixtures prove local state laws;
- integration tests prove the real source wiring and executable call chain;
- isolated engine runs prove map/runtime behavior and produce hash-bound
  receipts.

Final acceptance requires the scope named by the completion plan; a narrower
test cannot stand in for a real engine, corpus, fleet, or outcome gate.
