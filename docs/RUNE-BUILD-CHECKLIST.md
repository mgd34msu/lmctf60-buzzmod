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

16 kinds defined. Producer coverage is uneven — this is the main gap.

| Capability | Producer state | Verdict |
|---|---|---|
| `WALK` `CROUCH` `RAMP` `JUMP` `DROP` `AIR_CONTROL` | **classification predicate only** (one grouped test at `sg_rune_compact_movement_fields.c:5226`) | BUILD |
| `SWIM` | classification only (`:5233`) | BUILD |
| `HOOK_BOLT` | substantial (7 sites) | KEEP |
| `HOOK_RELEASE` | substantial (8 sites) | KEEP |
| `HOOK_COAST` | substantial (4 sites) | KEEP |
| `HOOK_RELAUNCH` | substantial (6 sites) | KEEP |
| `HOOK_BODY` | **never produced**; consumed by `sg_rune_compact_field.c:701` and `sg_tactic_runtime.c:301` | BUILD |
| `HOOK_PULL` | **never produced**; consumed at `:703` / `:302` | BUILD |
| `MOVER` | substantial (5 sites) | KEEP |
| `EXTERNAL_FORCE` | substantial (4 sites) | KEEP |
| `CONTROLLER_ACTION` | substantial (5 sites) | KEEP |

Supporting structure, all built and worth keeping: `movement_states`
(stance/support/water/`hook_phase`/flags/`mover_mechanism`), `movement_fibers`
(source state → destination state carrying analytic function refs),
`movement_angular_schedules`, and the four fiber kinds (PMOVE, HOOK,
MECHANISM_TRANSITION, ANGULAR_MOVER).

**This is the headline finding.** The hook is the most complete part of the
generator and the ordinary ground movement is the least. Two of the six hook
phases the plan names are consumed by the runtime but never emitted.

## A3. Hook layer

| Item | State | Verdict |
|---|---|---|
| `movement_hook_targets` — kind, provenance, response ref, visibility class, source/target stances | built | KEEP |
| `hook_phase` as a movement state field | built | KEEP |
| Six-phase decomposition in the contract | built | KEEP |
| Bolt/release/coast/relaunch producers | built | KEEP |
| Body/pull producers | missing | BUILD |
| Hook target visibility fed by occlusion | **not wired** — hook units never call `SG_StaticVisibility` | BUILD |

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

| Consumer | State | Verdict |
|---|---|---|
| `SG_CompactRuntimeLevelInstall` — install a generated model at level start | wired via `sg_rune_compact_production.c:310` | KEEP |
| Cell/phase localization from live pose | built, not the live path | BUILD |
| `SG_FieldService` — runtime destination gradients, caching, region hierarchy, incremental moving targets | **appears only in tests** | BUILD |
| `Field_Flood` reverse Dijkstra over action links | live in `sg_fields.c` (×9) and `sg_goal.c` | CUT (after field service lands) |
| Tactic execution / dispatch | partly wired (`sg_arach.c:2508–2579`) | KEEP |
| `Think_PickLink` action-link descent | live (`sg_arach.c:3821`), legacy | CUT (after tactic selection lands) |
| Typed strategy queue | foundation complete; waiting on live FieldService registration | BUILD |
| Belief runtime, sparse hypotheses, life identity | built | KEEP |
| Perception adapters → beliefs | caller migration open | BUILD |
| Probabilistic aim + exact pre-fire trace | plan marks complete | KEEP |
| Human hook boundary untouched by bot code | design rule | KEEP |
| Learning: costs/priors only, never geometry | boundary rule | KEEP |

---

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

`sg_field_service.c` — 3,172 lines — is compiled into the shipped game module
and has **zero production callers**. The replacement solver ships in the binary
today and nothing invokes it, while `Field_Flood` does the work. This is not
unwired code sitting harmlessly outside a module; it is in the artifact you
load. Wiring it is `destination_field_caller_migration`.

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
