# RUNE and runtime build checklist

A joint decision document. The RUNE is one higher-order surface: a cell complex
carrying layered volumetric fields, each describing a different aspect of the
same space. This lists every layer and every runtime consumer, what exists
today, and a proposed verdict.

Verdicts: **KEEP** (works, leave it) · **BUILD** (contract exists, producer
missing) · **CUT** (old or rejected path) · **RULE** (needs your decision).

Every status line below was measured from the tree, not inferred from the plan.

---

# Era map

Read from `git log --diff-filter=A`. The whole subsystem is about two months
old: 7 files introduced in 2026-07, 210 in 2026-08, 32 in 2026-09.

| Era | Dates | Representative files | Disposition |
|---|---|---|---|
| 1 — seed / link | 07-31 → 08-18 | `sg_rune`, `sg_arach`, `sg_oracle`, `sg_fields`; then `sg_move`, `sg_goal`, `sg_descend`; then `sg_rune_codec` | ~77,800 lines, still shipping, still the working bot |
| 2 — v2 model / phase / capability | 08-28 → 08-29 | `sg_rune_model`, `configuration_space`, `ground`/`water_capability`, `bsp_completeness_proof`, `hook_visibility_feasibility`, `weapon_effect_profile`; `phase_catalog` next day | built in two days, rejected two days later |
| 3 — compact | 08-30 → 09-01 | 50 files, `0bc4bf29` "Add compact RUNE contract and wire model" onward | the target |

The cut line runs *through* era 2, not around it. `sg_configuration_space` and
`sg_weapon_effect_profile` are 08-28 files that survive — the first is linked
into the compact generator, the second ships. That is the graph's "after their
useful host, geometry, weapon-law, publication, and runtime-service pieces
migrate."

Everything created on **2026-09-01** is scaffolding written on the fleet's last
day: `movement_fields`, `weapon_field`, `weapon_relations`, all four
`mechanisms` units, `composer`, `generation`, `game_offline`, `production`, and
the entire tactic layer. Contracts and control flow are in place; the physics
and field content are not. Every gap in Part A traces to that date.

The consequence: completing the system is mostly **new construction against a
good contract**, not migration. The plumbing exists.

# Part A — The artifact

## A0. Domain: the cell complex

The manifold every other layer is a field over.

| Item | State | Verdict |
|---|---|---|
| `cells` — Q8 bounds, incidence span, contents, semantics, `valid_stances` | built | KEEP |
| `facets` — binary32 plane, vertex span, incidence span, portal index, kind | built | KEEP |
| `incidences` — `(cell, facet, cell_ordinal, side, boundary_ownership)` | built | KEEP |
| `portals` — facet + negative/positive incidence, `clearance_q8`, direction, stances | built | KEEP |
| `vertices` — Q8 | built | KEEP |
| Half-open boundary ownership (no double count, no gap) | built | KEEP |
| Both hulls in one complex via `valid_stances` bitmask | built | KEEP |

Q8 positions with binary32 planes is the right exactness posture: exact within a
declared quantization rather than chasing float equality with the host.

**Nothing to do here.** This layer is the strongest part of the system.

## A1. Static semantics

| Item | State | Verdict |
|---|---|---|
| `contents` mask (solid/window/water/lava/slime/clip/sky/currents) | built | KEEP |
| Cell semantics (hazard, sky boundary, void boundary, mover volume) | built | KEEP |
| `source_surfaces` + surface vertices | built | KEEP |
| Surface semantics (hookable, sky, cover, exposure, bounce) | built | KEEP |

## A2. Movement capability layer

17 kinds defined. Measured 2026-09-01 after era-4 commits d9fe1873/6678251b and
the rocket-jump commit that follows 09d853b8.

| Capability | Producer state | Verdict |
|---|---|---|
| `WALK` `CROUCH` | emitted per portal crossing between level supported cells (`EmitBoundaryFields`) | KEEP |
| `DROP` | emitted when the far floor is lower than STEPSIZE or the crossing leaves support through a partition | KEEP |
| `JUMP` | emitted when the far floor is higher within `v²/2g` of the 270 impulse, or up through a floor facet | KEEP |
| `AIR_CONTROL` | emitted for any crossing whose source is unsupported | KEEP |
| `SWIM` | emitted when both sides are water | KEEP |
| `RAMP` | **not emitted**; a sloped floor needs the support plane, which the region lacks. Ramps traverse as WALK. | BUILD |
| `ROCKET_JUMP` | kind added (wire v13). Emitted beside every upward JUMP and alone where the far floor is above the jump rise but within the rocket rise. Launch derived in `sg_host_rocket_jump_law.h` from the host's knockback, splash, and body laws: attack and jump in one command, blast at the floor one frame into the jump, ~47 self-damage, ~218 unit rise at g=800. Tactic gate needs the launcher, a rocket, and health above the post-armor cost. | KEEP |
| `HOOK_BOLT` `HOOK_BODY` `HOOK_PULL` `HOOK_RELEASE` `HOOK_COAST` `HOOK_RELAUNCH` | all six emitted by `EmitHookField`'s range loop (earlier "never produced" claim was a grep artifact) | KEEP |
| `MOVER` `EXTERNAL_FORCE` `CONTROLLER_ACTION` | emitted | KEEP |

Profiles: `PROFILE_GROUND`/`WATER` are contact motion (distance over the
engine speed clamp); `PROFILE_AIR` is exact free flight under map gravity;
`PROFILE_JUMP` is air plus the engine's 270 launch impulse; `PROFILE_ROCKET_JUMP`
is air from the blast height with the summed vertical velocity and the lead
frame added to its clock. The air template carries the half-substep gravity
term, so it is exact at substep boundaries under gravity-before-move Euler.
Engine pmove terms live in `sg_host_engine_pmove.h`; gravity stays a bound
per-map value.

Before d9fe1873, `EmitBoundaryFields` resolved a portal's cells and returned:
no capability crossed any portal, so a bot could hook, ride a lift and be
pushed, and could not walk.

## A3. Hook layer

| Item | State | Verdict |
|---|---|---|
| `movement_hook_targets` — kind, provenance, response ref, visibility class, source/target stances | built | KEEP |
| `hook_phase` as a movement state field | built | KEEP |
| Six-phase decomposition, all six emitted | built | KEEP |
| Fire admissible from any legal state, velocity preserved (no stand-still precondition) | requirement, to verify in emission | RULE |
| Release admissible at every point of the pull, not only at arrival (fling; chain = fire again mid-arc) | requirement, to verify in emission | RULE |
| Hook target visibility fed by occlusion | **not wired** — hook units never call `SG_StaticVisibility` | BUILD |
| Human hook fidelity | audited faithful to LMCTF 6.0 (see below) | KEEP |

## A4. Mechanism layer

| Item | State | Verdict |
|---|---|---|
| `mechanism_authorities`, controllers, topology edges, transitions | built | KEEP |
| Inverse provenance indexes across authority/static projections | built | KEEP |
| Door / lift / train / button / teleport / rotator coverage | RULE | RULE |

## A5. Weapon response layer

| Item | State | Verdict |
|---|---|---|
| `weapon_profiles` — 14 profiles, host parity, ships | built | KEEP |
| 10 families + effect flags | built | KEEP |
| `weapon_response_kernels`, `weapon_attachments` | contract built; producer completeness unmeasured | RULE |
| `weapon_relation_spans` / `refs`, `weapon_function_refs` | built | KEEP |
| Response projection over shared cells | partial | BUILD |
| Per-family kernels (rail lanes, cone occupancy, splash reach, bounce/fuse, BFG) | plan marks "reshape", not complete | BUILD |

## A6. Analytic function library

| Item | State | Verdict |
|---|---|---|
| constants, affines + slopes, polynomials + coefficients | built | KEEP |
| **ballistics** | built | KEEP |
| piecewise + clauses | built | KEEP |
| input dimensions (distance, hook length, time, velocity, world, direction) | built | KEEP |
| half-open interval ownership | built | KEEP |

This is the anti-enumeration machinery and it is genuinely good. Behaviour is
stored as functions over state regions, not sampled instances.

## A7. Cost

| Item | State | Verdict |
|---|---|---|
| Directional, time-weighted cost kernels from real map physics | plan §3 unchecked | BUILD |
| Cost expresses preference so expensive routes are avoided, never deleted | design rule | KEEP |

Cost is the layer that makes long hook shots unattractive without pruning them
out of the artifact. Nothing should ever remove a connection for being costly.

## A8. Identity, law binding, completeness

| Item | State | Verdict |
|---|---|---|
| Per-law fingerprints: physics ABI, collision, pmove, gravity, hook, mechanism | built | KEEP |
| `movement_pmove_abi`, behaviour fingerprint, host level generation | built | KEEP |
| Completeness as the sole validity rule | design rule | KEEP |
| Linear production acceptance (identity/format/count/span/reference/order/finite/checksum/load) | design rule | KEEP |

## A9. Wire and codec

| Item | State | Verdict |
|---|---|---|
| Compact wire encode/decode with round-trip in the pipeline | built | KEEP |
| `tools/runecompactread.c` as the single canonical C inspector | built | KEEP |
| `sg_rune_v2_codec`, `sg_rune_v2_artifact_loader` | orphaned, superseded | CUT |

---

# Part B — Runtime

Re-measured 2026-09-01 by tracing the shipped module from `G_RunFrame`. The
earlier rows here were written before commits 0017ab60/d9fe1873 and were wrong
about what is live.

The legacy rune pointer `sg_rune` is never assigned in the shipped module (its
only writer is behind `SG_STRIKE_TRANSITION_TEST_API`, which no build defines),
so `Field_Flood`, `Think_PickLink`, `Think_Move`, and everything in
`sg_fields.c`/`sg_goal.c`/`sg_descend.c`/`sg_move.c`/`sg_price.c` is compiled
and unreachable. Era 1 was never the running bot; it is demolition.

| Stage | State | Verdict |
|---|---|---|
| Load: `SG_LevelSetup` → compact production load → `SG_CompactRuntimeLevelInstall` (field service, localization, strategy provider, tactic provider) | live (`sg_arach.c:523-606`) | KEEP |
| Localization per frame: `SG_BotLocalizationObservePmove` after the bot's host pmove | live (`p_client.c:3056`) | KEEP |
| Typed strategy queue → `SG_StrategyRuntimePlanResolve` → field target | live (`sg_arach.c:2408-2549`) | KEEP |
| Field service gradient query | live (`sg_strategy_runtime_bridge.c:1425`) | KEEP |
| Tactic selector `SG_TacticSelectCapability` via `SG_TacticExecutionOwnerPrepare` | live every STEP frame (`sg_tactic_runtime.c:977`) | KEEP |
| Selected capability → usercmd: `SG_TacticControl` in `sg_tactic_controller.c`, called from `CompactTacticEmit` | built. Stateless per frame: walk, crouch, drop, air control, swim, mechanism/teleport/push/mover steer at the step's point; jump presses within the jump's reach; rocket jump asks for the launcher, then faces the crossing, aims down, fires and jumps in one command; stance steps duck in place. Hook: the winning probe's hook target resolves to an aim point (a certified fact's witness, or the nearest patch of a candidate group); idle or coasting with an in-flight successor fires from the eye while still moving, riding presses nothing, a coast successor releases, and a coasting body fires again for the chained fling. The sealed owner, its witness stubs, and their tests are deleted. | KEEP |
| Hook, mechanism, teleport, push, mover execution | no consumer of `execution.mechanism_handoff`; hook phase is legacy bot state | BUILD (inside the executor) |
| `sg_descend.c` indexing compact cells into legacy seed arrays | latent index-space confusion, harmless only because the legacy pointer is NULL | CUT |
| v13 pmove-control layer (`sg_rune_compact_pmove_control*`, `sg_tactic_pmove_control_runtime.c`) | no producer, no wire section, no caller; a validator not a controller | CUT |
| Belief runtime, sparse hypotheses, life identity | built | KEEP |
| Perception adapters → beliefs | caller migration open | BUILD |
| Probabilistic aim + exact pre-fire trace | plan marks complete | KEEP |
| Human hook boundary untouched by bot code | design rule; production hook-fire entry hardcodes phase IDLE so it never reports a refire (`sg_host_law_publication.c:2438`) | KEEP, fix the phase |
| Learning: costs/priors only, never geometry | boundary rule | KEEP |

---

# Player hook audit (2026-09-01)

Compared against the archived LMCTF 6.0 source in `~/Projects/qsrc/lmctf60`.
The human grapple path is `Weapon_Hook_Fire` → `LMCTF_HumanHookFire` for
non-bot clients.

| Function | Result |
|---|---|
| `LMCTF_HumanHookFire` | line-for-line the original `Weapon_Hook_Fire`: same pull ladder (>120 → `GRAPPLE_PULL_SPEED`, then ×5/×4/×3/×2/×1 tiers), same `SV_AddGravity` placement, same `velocity`/`oldvelocity` writes |
| `LMCTF_FireHumanHook` | original `fire_hook` plus a CTF-id attribution tag, passive trace capture, and one fix: the original passed `NULL` as the plane on immediate obstruction and `hook_touch` dereferences it on a damageable target |
| `hook_touch` | identical |
| `ctf_hook_abort` | original core preserved; adds a bot-gated compound call and a passive trace reset |
| `Weapon_Hook` | identical plus a passive trace call before abort |
| `Cmd_Hook_f` (offhand bind) | whitespace-only diff |

`SG_HumanTraceHook*` write nothing to player or bolt state. The gravity-free
`CTF_HookPullStep` is bot-only; its comment is correct that the original's
`SV_AddGravity` was overwritten by `ent->velocity = dir` on the next line and
never had an observable effect.

Verdict: the human hook is faithful to LMCTF 6.0. If it feels wrong in play,
the cause is outside these functions.

# Part C — Removal

Measured by module membership crossed with old/new signals.

| Group | Size | Verdict |
|---|---|---|
| **Legacy in the shipped module** — seed/link/action, `Field_Flood`, objective pruning, compound action families, oracle, descend, legacy codec/file/binding/topology/hook frontier/seed game | **54 files, 77,804 lines** | CUT last, capability by capability. This is the only working bot today. |
| **Rejected intermediate** — phase catalog (+owner/publication/mover support), ground/water capability (+publication), external force (+publication), mechanism capability (+seal), hook visibility feasibility + audits, `movement_hook_air`, `host_law_construction_offline` | **21 files, 23,288 lines**, in no module | CUT after mining the pieces the graph names |
| **Orphans** — `bsp_completeness_*` (9 files), hook visibility audit/verifier (5), `sg_rune_v2_codec` + `artifact_loader`, `cell_phase_localization`, `weapon_static_affordance`, `static_affordance_catalog`, `static_visibility_publication`, `sg_hooks_test` | **21 files, 15,742 lines**, in no module | CUT now, lowest risk |

### Destination-era layering

A third category, distinct from old-vs-new. Some code predates the
strategy/tactics split, when "what the bot wants" was expressed by *which field
you flooded* rather than by a typed goal. That conflates two layers which are
now supposed to be separate:

- **Goal selection** — belongs to the typed strategy queue
- **Gradient derivation** — belongs to the field service, and should be
  indifferent to why the destination was chosen

Destination itself is not the obsolete part. `sg_destination.h` declares the
runtime destination classes (flag, item, weapon, armor, powerup, carrier,
escort, …), which is exactly NAV-3 — a destination at any valid point. That
stays.

What needs unwinding is goal policy baked into field code:

| Unit | Size | Note |
|---|---|---|
| `sg_fields.c` | 1,349, shipped | per-class flag/item/carrier/post/lane/intercept fields, flooded by `Field_Flood` |
| `sg_field_projection.h` | shipped | projection tied to link actions |
| `sg_carrier_cover.h` | shipped | goal policy keyed on `RL_RUN` links |
| `sg_defense_facing.h` | shipped | same |
| `sg_intercept_policy.h` | shipped | same |
| `sg_combat_alert_policy.h`, `sg_death_belief.h` | shipped | policy expressed in seed terms |

Each holds real behavioural intent worth preserving. The intent should move to
strategy as typed goals; the flooding should collapse into one destination-
independent gradient query.

### Dead weight already shipping

Corrected 2026-09-01: the compact field service is live (see Part B). The dead
weight is the other way round: the 54 legacy files are compiled into the
shipped module and unreachable.

### The v2 contract problem

`sg_rune_model.h` does double duty. It defines the **v2 artifact records**
(cell, portal, phase, phase transition, surface, affordance, capability kernel,
landmark, mechanism) *and* the **shared primitives** (vec3, bounds, interval,
order key, stable id, identity) that eight files depend on, including
`sg_host_collision.h` and `sg_weapon_effect_profile.h`.

The compact path is independent of it except for one include in
`sg_rune_compact_spatial_index.h`.

Proposal: split the header — keep the primitives, delete the v2 artifact
records. That single move makes the phase catalog and capability publication
uncompilable, which forces the rejected path out rather than leaving two
artifact contracts alive to be confused for one another.

---

# Part D — Rulings needed

1. **Ground movement.** WALK/CROUCH/RAMP/JUMP/DROP/SWIM/AIR_CONTROL exist only
   as a classification predicate. Build them as analytic fibers in the compact
   builder, or is there prior work to mine first?
2. **Hook body and pull.** The runtime consumes them; the generator never emits
   them. Confirm these are producer gaps and not deliberately deferred.
3. **Hook occlusion.** Wire hook target visibility to `SG_StaticVisibility`
   (PVS + occluders, conservative, exact trace at the boundary)? It ships and
   has region-to-surface queries but no hook caller.
4. **v2 header split.** Approve splitting `sg_rune_model.h` as above.
5. **Orphan deletion.** Approve cutting the ~15,700 unreferenced lines now.
6. **Weapon kernel completeness.** I have not measured per-family producer
   coverage. Worth measuring before deciding.
7. **Mechanism family coverage.** Same — contract is built, producer coverage
   unmeasured.
8. **Destination-era policy.** `sg_fields.c` and the policy headers
   (`carrier_cover`, `defense_facing`, `intercept_policy`, `field_projection`,
   `combat_alert_policy`, `death_belief`) express goal choice as field
   selection. Do we lift that intent into typed strategy goals and collapse the
   per-class fields into one gradient query, or is some of it worth keeping in
   place?
9. **Order.** My proposal: orphans → ground movement → hook body/pull → hook
   occlusion → cost kernels → field service wiring → destination-era policy
   lift → legacy retirement.

---

# Generating on this machine (2026-09-01)

The module demands four `CVAR_NOSET` identity cvars from the engine
(`sv_rune_mapchecksum`, `sv_rune_physics_id`, `sv_rune_bsp_sha256`,
`sv_rune_bsp_bytes`). Only the patched Yamagi tree at
`~/Games/Quake2/engines/yquake2` publishes them; its `release/q2ded` lagged the
source by two cvars until rebuilt with `make server`. `~/q2linux/q2reproded`
(q2repro) has none of them, rejects game names containing dots, and does not
know `-portable`; `tools/runegen.sh` now takes `Q2DED_FLAGS` and names stages
with `[A-Za-z0-9_-]` only.

Working sequence: `make -f GNUmakefile` (runtime module, now the default goal),
`make -f GNUmakefile rune-compact-generator runecompactread.gnu`,
`tools/deploy.sh <runtime .so>` into `~/Games/Quake2/lmctf-hooktest`, then
`RUNE_GENERATOR_MODULE=<generator .so> RUNE_COMPACT_READER=./runecompactread.gnu
tools/runegen.sh <map>` with the script defaults. The 175 corpus BSPs are in
`~/.cache/lmctf06-relay-snapshot-v1/assets`; the live maps directory holds 38.

The `rune-compact-test` family is not part of `host-test`; run it separately.
Its static test carried a stale fixture (fixed in d32981b9) and its reader
target lacked two link units (fixed in 54413d98).
