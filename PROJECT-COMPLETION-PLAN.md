# LMCTF BuzzMod project completion plan

This file governs the target system, current-code disposition, development
checks, and release sequence. Git history and retained playthroughs hold
evidence; exclude transcripts and experiment logs.

## Completion definition

The project is complete only when one unchanged local source commit meets
every catalog requirement and check, review-standard step, ordered execution
checkbox, invalidation rule, and completion checkbox below. No later result
waives an earlier gate.

The source, immutable snapshot, 175 fresh RUNEs, authenticated installed bundle,
ordinary-match evidence, and clean-directory release must all derive from that
commit. Local `slipgate` and `main` must point to it, and it must be tagged
locally as `v1.0.0`. Owner direction keeps remote pushes, remote CI, and remote
publication out of scope.

## Current status

| Area | Current state |
|---|---|
| Architectural state | Compact model, analytics, localization, destination fields, wire, readers, exact geometry materialization, artifact publication, learning priors, and the source-bound BSP builder are accepted. Static semantics, movement, weapons, composition, production cutover, and legacy deletion remain. |
| Generation | Stopped: no corpus controller, generator, acceptor, or finalizer runs; the repository has zero `.rune` files and zero obsolete repair sidecars. Retained demos and human-derived analysis are evidence only. |
| Source refs | Local `slipgate` contains the contract freeze and is ahead of local `main` and both remotes. Do not push or start remote CI. Reunify local `main` only after the final source gates pass. |
| Existing strengths | Map/physics identity, host collision and Pmove, mechanisms, entities, weapon laws, human capture, atomic publication, readers, corpus control, installation, and fleet tooling require review. |
| Invalidated work | Objective-core pruning, `complete`/`local_only` wire validity, route-only acceptance, action-labelled route ownership, production Dijkstra repair, fixed-grid/face-anchor coverage, and old RUNE/corpus evidence are superseded. |
| Freeze and release | Not started; no present-source artifact is eligible for the final corpus. |

## Non-negotiable design and operating rules

- The BSP and its entities are ground truth. Generalize construction per BSP;
  no map name, flag pair, recorded route, or corpus exception may create geometry
  or connectivity.
- A single compact BSP-derived field builder emits the destination-independent,
  higher-dimensional RUNE over exact player-origin space. It contains
  connectivity, movement and weapon response laws, static semantics, and
  directional cost. It does not prescribe routes or named movement scripts.
- Any point where the standing or crouching player hull fits must be represented.
  Every legal connection must be represented. Physically disconnected valid
  regions remain represented and disconnected.
- Bound-map and runtime physics are authoritative: gravity, air acceleration,
  maximum velocity, frame cadence, water behavior, and host movement law. No
  generation path may substitute gravity 800.
- BSP cells are the shared spatial substrate for movement and weapons. Partition
  them at player-hull, topology, contents, mechanism, movement-law, visibility,
  occlusion, and weapon-response discontinuities. Do not duplicate the BSP for
  each movement or weapon family.
- Store position as full 3D player-origin space. Store velocity, stance,
  support, water, airborne, hook, mover-relative, and time behavior as compact
  analytic fields and kernels. Do not enumerate every pose, ray, trajectory, or
  phase combination.
- Weapon fields are first-class RUNE content. They cover hitscan, rail, spread,
  cones, bolts, rockets, splash, grenade arc/bounce/fuse, BFG, and special laws
  over shared cells and surfaces.
- Destination gradients are derived at runtime. Players, teammates, enemies,
  beliefs, current ammo and health, tactics, and human traces are not RUNE
  content.
- The generator is correct by construction. Deep BSP and host comparison tests one ordinary RUNE and one hard RUNE only.
- Production generation performs linear identity, format, count, span, reference, order, finite-value, checksum, and successful bot-load checks. Passing finishes the map. No proof catalog, provider, search tree, hours-long verification, or geometry reconstruction is a release dependency.
- Batching may control memory and scheduling but may not end work. Capacity
  overflow fails loudly and may not downgrade completeness. Production
  generation and review have no elapsed-time limit.
- The ordinary human hook remains base-LMCTF behavior. Bot planning, execution,
  validation, and telemetry must not alter human fire, attach, pull, release, refire,
  collision, or fling semantics.
- Preserve human playthroughs as last-resort evidence and learning input, never
  as authority to invent a connection absent from BSP and physics.
- Commit coherent units locally. Do not push or freeze while source, tests,
  documentation, or wire contracts change.
- `tools/rune-corpus-maps.txt` is the sole 175-map authority;
  `tools/topmaps.txt` is only an ordinary map schedule. Of 180 durable BSPs,
  unsuffixed `lmctf05`, `smap31`, `xmap07`, `xmap11`, and `xmap14` are excluded
  because their suffixed variants are canonical.

## Requirements and current-code catalog

Disposition terms are strict:

- **Review/keep:** retain useful candidate behavior only after its contract,
  implementation, callers, failure behavior, and real integration check pass
  under the new model.
- **Reshape:** retain implementation only behind a different data model or
  ownership boundary.
- **Replace:** remove it from production after callers move.
- **Delete:** remove obsolete code, tests, format fields, and documentation.

### BSP, configuration space, and movement

| ID | Required result | Current code and disposition | Required check |
|---|---|---|---|
| BSP-1 | Parse the complete BSP needed for collision and semantics: planes, nodes, leaves, brushes, brush sides, models, surfaces, contents, visibility/areas, entities, and moving submodels. | `sg_rune_seed_game.c` reads selected face/model lumps and uses host traces. **Replace** its partial face-anchor authority with a complete parser; **review/keep** host collision adapters. | Compare parsed structure and contents against the host collision model on real BSPs; malformed and unsupported inputs fail closed. |
| BSP-2 | Construct exact standing and crouching player-origin free space by expanding solids/eroding free space with the actual hull. | Current fixed-grid seeds and ground traces are sparse and floor-centric. **Replace.** Candidate hull constants and canonical Pmove pose checks in `sg_oracle.c` require review. | Boundary samples just inside/outside every cell agree with host box traces and Pmove, including low ceilings, windows, half-walls, ramps, ledges, and void boundaries. |
| BSP-3 | Represent the full three-dimensional configuration space, including supported surfaces, water volumes, airborne volumes, and every height at which the hull fits. | `rune_seed_t` stores a point and flags. **Replace** with cells, portals, and capability metadata. | A BSP-to-RUNE overlay finds no host-valid player volume omitted from the artifact. |
| BSP-4 | Derive adjacency from shared traversable configuration-space boundaries. A visually open gap that cannot fit the player is not connected. | Local pair traces and O(n-squared) seed overlay are incomplete. **Replace.** | Corridor, doorway, crouch, ramp, window, and half-wall fixtures match host reachability in both directions. |
| BSP-5 | Preserve complete topology during construction. Do not use later route repair or a second generator disguised as a completeness checker. | Delete production `SG_BspCompletenessProve` reconstruction and proof-provider ownership after extracting any useful development-only comparison code. | One ordinary and one hard development RUNE catch injected omitted or invented cells and portals. Production generation uses only linear structural and load checks. |
| BSP-6 | Preserve all valid components without flag-based pruning. Objectives are ordinary destinations placed on the field. | Objective-core pruning and flag-root closure in `sg_rune.c` and `sg_rune_hook_frontier.c` are **delete**. | Non-objective rooms, item pockets, disconnected valid regions, and both flag stands survive serialization. |
| BSP-7 | Annotate cells and surfaces with static contents and semantics: water, hazards, sky, hookability, normals, cover/exposure boundaries, landmarks, items, flags, and mechanism identities. | These facts are split across BSP samples, entity scans, fields, combat traces, and mechanism catalogs. **Reshape after review.** | Static annotations agree with BSP/entity authority; live availability and actor state remain runtime-only. |
| MOV-1 | Encode walking, crouching, ramps, steps, jumping, dropping, swimming, water exits, movers, pushes, teleports, doors, buttons, triggers, dwell waits, and hook traversal as capabilities over the same space. | Exact movement/oracle and live-controller code exists. **Reshape after full review**; remove action-link ownership. | Each capability has host-physics fixtures plus representative real-BSP checks; disabling one capability does not erase geometry reachable by another. |
| MOV-2 | Use actual map gravity and movement cvars throughout construction, cost, loading, and runtime. | Identity captures these values, but legacy compatibility and fallback paths remain. **Review/reshape.** | Gravity 100 and 800 maps produce different, correctly bound costs and airborne envelopes; identity mismatch fails load. |
| MOV-3 | Model airborne control, jump-off/coast trajectories, drops into void-accessible space, and relaunch from any legal airborne pose. | Air-hook frontier samples action states and previously stopped at objective closure. **Replace orchestration; review/keep exact physics kernels.** | `tomb05` reaches both flag entries from BSP and physics alone, using gravity 100, without Dijkstra or human input. |
| MOV-4 | Model water as a 3D traversable volume, not a compound floor edge. | Water forests and swim replay exist around graph links. **Reshape after review.** | Submerged vertical, horizontal, entry, exit, breath, current, and dry-to-water transitions agree with host physics. |
| MOV-5 | Separate hook-bolt visibility from player-body motion. A hook may fire from any valid 3D pose to any visible hookable surface, excluding sky; the body follows pull, release, coast, air-control, and collision physics. | `sg_hook_oracle.c`, replay, compound hook, and live hook code provide partial kernels. **Review/reshape.** | Static, airborne, chained, fling, ceiling, wall, underside, low-gravity, occluded, pickup-obstructed, and sky cases match base LMCTF. |
| MOV-6 | Avoid per-point brute-force rays. Partition configuration space and surface visibility at known occluder boundaries; interpolate only inside one unchanged response region. | Canonical airborne state deduplication is narrower than this requirement. **Replace/extend.** | Development samples match exact rays while production time and memory scale with cells and visibility boundaries, not every possible pose. |
| MOV-7 | Movement costs are directional and time-aware: acceleration, velocity, gravity, air control, hook bolt flight, pull, release, coast, swimming, falling, and mover dwell/travel all contribute. | Per-link `cost_ms` is action-specific. **Replace** with capability/cost kernels over cell state. | Analytic costs and exact Pmove timing agree within a declared tolerance on each movement family and both gravity regimes. |
| MOV-8 | Rocket-jump code remains available where supported, but LMCTF RUNE connectivity generation does not search rocket or grenade jumps when the hook already supplies traversal. | Rocket-jump generation/runtime code exists. **Review/reshape** mode admission; no grenade-jump generator is added. | LMCTF generation logs show no rocket/grenade-jump search; non-LMCTF support and existing runtime behavior remain intact. |
| MOV-9 | Construct connectivity and analytic capability fields to a destination-independent fixed point. No objective closure, work budget, exhaustive pose enumeration, or hook-chain work cap may terminate valid construction; physical hook lifecycle caps remain. | Objective closure, bounded frontiers, and exhaustive phase construction are **replace**; **keep only justified physical/protocol caps after review.** | Repeated batching converges to the same compact artifact; randomized batch sizes and crash/resume points do not change content. |

### Static RUNE, fields, strategy, and tactics

| ID | Required result | Current code and disposition | Required check |
|---|---|---|---|
| NAV-1 | Bind the RUNE exactly to BSP content, entity semantics, module/physics ABI, movement cvars, and construction schema. | `rune_identity_t`, codec checks, and authority code are **review/reshape** candidates. | Any relevant byte or physics-law change rejects the artifact; irrelevant filesystem identity differences do not. |
| NAV-2 | Store compact cells and portals plus movement, weapon-response, and cost kernels. Bind the BSP instead of copying it and do not serialize runtime actors. | Seed/link/action wire format is **replace**. The current explicit phase/dynamics shape must be reviewed for accidental state enumeration. | A reader can reconstruct configuration coverage and query movement and weapon fields using only the exact-bound BSP plus RUNE. |
| NAV-3 | Support a destination at any valid point: flags, items, weapons, armor, powerups, carriers, escort/intercept positions, defensive posts, learned points, and arbitrary waypoints. | `sg_fields.c` already has flag, item, carrier, post, lane, and intercept fields. **Reshape after review.** | Every target class maps to valid configuration space and produces a finite field exactly where physically reachable. |
| NAV-4 | Derive destination-specific directional cost gradients at runtime without regenerating the RUNE. | `Field_Flood` is reverse Dijkstra over action links. **Replace solver; review/keep registry, caching, and target mapping.** | Multiple simultaneous destinations produce correct independent fields; changing a goal does not mutate or rebuild the RUNE. |
| NAV-5 | Use a continuous/anisotropic field method appropriate to directional costs. Structural connectedness uses cell flood/union-find/SCC, not path repair. | Production Dijkstra late-path selection is **delete**. A small independent graph reference may remain test-only if useful. | Solver results agree with analytic fixtures and an independent reference on sampled states; no production call reaches late-path repair. |
| NAV-6 | Cache fields for static destinations and update arbitrary, dropped, displaced, or moving destinations incrementally through a coarse region hierarchy. | Static fields and projected carrier fields already exist over seeds. **Reshape after review.** | Moving a target updates only affected regions, preserves exact reachability, and converges to the same field as a clean solve. |
| STR-1 | Represent strategy as a typed queue of goals with prerequisites, alternatives, priorities, completion, cancellation, replacement, and failure policy. | An independently reviewed compiler and transactional reducer now provide the typed queue, dependency, condition, alternative, priority, retry, completion, cancellation, and authority foundation. **Migrate** the scattered role, strike, item, supply, escort, intercept, carry, recovery, and human-order callers. | Examples such as railgun to armor to flag, escort carrier, recover flag, and timed quad execute and replan under interruptions. |
| STR-2 | Preserve long-term destination commitment through tactical interruptions unless strategic authority changes. | The reviewed foundation preserves commitment across authenticated combat, obstruction, hook-opportunity, death, and respawn suspension, and replaces or cancels only through explicit authority. **Migrate production callers and retire superseded commitment state.** | Combat, obstruction, hook opportunity, item loss, death, role change, and human order tests distinguish suspend, resume, replace, and cancel. |
| TAC-1 | Tactics chooses legal movement execution from the live state and local gradient. The movement mechanism that discovered connectivity does not own traversal. | `Think_PickLink` and `sg_move.c` execute `link->action`; **replace this ownership boundary** while reviewing individual live reducers. | The same local transition may be executed by walk, jump, swim, hook, or another legal tactic without changing the RUNE. |
| TAC-2 | Mechanisms remain explicit authenticated discontinuities with topology, controller identity, trigger/button/dwell timing, execution, and recovery. | Mechanism catalog, plans, bindings, timelines, and live transactions are strong **review/reshape** candidates. | Door families, delayed triggers, lifts, trains, rotating movers, buttons, teleports, and failure recovery pass caller audits and real-BSP trials. |
| TAC-3 | Runtime localization maps the live player pose into configuration space and recovers from numerical drift or temporary absence without inventing connectivity. | The authenticated cell/phase locator checks static-world configuration-portal continuity, exact RUNE boundary identity, phase transitions, and numerical-drift recovery without allocating on the live path. Accepted engine Pmove and mover authority, production callers, and deletion of nearest-seed ownership remain open. | Spawn, crouch, water, airborne, mover, teleport, death, and out-of-field recovery are deterministic and bounded. |
| TAC-4 | Threat, cover, weapon opportunity, obstruction, and teammate coordination may deform the local field temporarily but cannot create permanent minima or silently replace the strategic goal. | `sg_price.c`, danger, cover, combat, and route-purity policy contain partial terms. **Reshape after review.** | Tactical terms expire or retire explicitly; after a fight or dodge the bot resumes the committed strategy unless its authority changed. |

### Perception, beliefs, combat, and learning

| ID | Required result | Current code and disposition | Required check |
|---|---|---|---|
| BEL-1 | Keep all players and match state out of the static RUNE. Maintain per-team, per-player runtime probability distributions over position, velocity, movement state, and future time. | `sg_belief_enemy_t` stores one seed and timestamp. **Replace data model; review/keep earned-information boundaries.** | No decision reads hidden enemy state. Beliefs normalize, age, diffuse only through valid movement, and remain isolated by team. |
| BEL-2 | Visual evidence creates a concentrated pose/velocity belief. Lost sight propagates and diffuses it; negative visual evidence removes impossible mass. | Sight and aging hooks exist. **Reshape after review.** | Controlled visibility/occlusion sequences produce expected concentration, propagation, exclusion, and decay. |
| BEL-3 | Sound creates diffuse or multimodal beliefs consistent with event type, direction, attenuation, PHS/occlusion, and known item locations. Damage adds directional evidence. | Sound/PHS/range, item clocks, rail/haste notes, and damage rings exist; heard enemies currently become one randomized seed. **Reshape.** | Sound-only beliefs never become unjustified exact aim; multiple plausible regions and later evidence update correctly. |
| BEL-4 | Teammates use runtime beliefs for coordination and friendly-fire risk. They do not become RUNE content. | Some teammate state is directly available and combat has safety checks. **Review/reshape** by authority and visibility rules. | Coordination, carrier support, and friendly-fire decisions use only permitted current or believed state. |
| BEL-5 | Store beliefs sparsely as weighted cells/particles or mixtures, including position, velocity, acceleration, orientation, movement/weapon state, confidence, and future time. Team communication may reduce uncertainty only through an authenticated runtime observation. | Sparse prediction and authenticated perception adaptation are accepted. **Migrate production consumers.** | Sight, armor pickup, weapon fire, footsteps, hook, door, lift, water, damage, and team reports produce source-appropriate distributions without dense-map cost. |
| COM-1 | Build first-class weapon-response fields over the same BSP-derived cells and visibility/occlusion partitions used by movement. Do not duplicate the BSP per weapon. | Combat currently traces live shots independently, while the static-affordance catalog treats weapon results mainly as external query evidence. **Reshape.** | The RUNE contains compact weapon-family response regions and kernels; an exact trace runs immediately before firing. |
| COM-2 | Provide distinct RUNE response kernels for hitscan, rail penetration, automatic spread, shotgun cones, straight bolts, rockets and splash, grenades and bounce/fuse, hyperblaster, BFG, and special weapons. | Identity-bound immutable profiles cover host weapon families and physical laws, but they are not yet integrated as complete fields over the cell complex. **Reshape.** | Each static field matches host speed, gravity, collision, penetration, spread, splash, bounce, fuse, self/team exposure, cadence, ammo, and switch law. |
| COM-3 | Choose aim and weapon by integrating predicted target probability and future weapon effect, minus self, teammate, ammo, and opportunity costs. | Current combat leads a visible target or a single retained seed. **Replace decision model while preserving validated physical fire code.** | Visible, sound-only, crossing, occluded, clustered, friendly-nearby, and projectile-intercept scenarios choose defensible actions. |
| COM-4 | Preserve family-specific effects: rail penetration and lanes; automatic-weapon exposure/spread; shotgun cone occupancy; bolt arrival; rocket impact-surface and occluded splash choice; grenade arc/bounce/fuse/area denial; BFG behavior. | Several cases exist in `sg_combat.c` and host weapons. **Thoroughly review/reshape.** | Static affordance queries and live shots agree with host behavior for each family, including wall/floor shots that outperform aiming at a belief mean. |
| LRN-1 | Human traces capture lossless command/Pmove and hook lifecycle data without changing player behavior. | Exact source-bound passive capture, hook lifecycle coverage, rotation, recovery, and fail-closed write handling are accepted. Learning consumers remain a separate migration. | Two independent playthroughs can be isolated by client and frame range, replayed in order, and shown not to alter human input or hook behavior. |
| LRN-2 | Human evidence may refine runtime tactic priors, costs, landing preferences, and strategy. It may not create geometry or bypass host collision. Post-match learning is required; live learning is optional after transactional safety is checked. | Compact identity-bound priors are accepted. **Migrate callers; delete edge nomination.** | A learned update is exact-bound, engine-checked, atomic, rollback-safe, and cannot connect physically disconnected cells. |
| VIS-1 | A client visualization of cells, gradients, beliefs, visibility, or weapon affordances is optional demonstration work, never a freeze blocker. | `tools/runeview.py` may provide a starting point. **Defer and review if scheduled.** | When implemented, visualization is read-only and matches the loaded artifact; absence cannot block release. |

### Wire format, validation, tooling, and release

| ID | Required result | Current code and disposition | Required check |
|---|---|---|---|
| ART-1 | Define a versioned little-endian wire format for cells, portals, movement fields, weapon-response regions and kernels, mechanism references, costs, identities, and checksums. | Codec/file/stream/loader/writer code is a **review/reshape** candidate; the seed/link schema is **delete**. | Truncation, overflow, unknown versions, invalid references, CRC drift, and hostile counts fail before publication. |
| ART-2 | Publication remains fail-closed and atomic. A rejected candidate never replaces the published artifact, and mixed RUNE/sidecar state is impossible. | Compact publication is accepted. **Migrate callers; review installer and sidecars.** | Fault injection at every write, sync, rename, validation, and restart point converges to old-complete or new-complete state. |
| ART-3 | Replace objective-centric `complete`/`local_only` wire validity with configuration-space completeness. Objective reachability is a gameplay diagnostic derived from destinations, not artifact truth. | `sg_rune_contract.h`, controller policy, finalizer, validators, and plan tests are **replace**. | A complete disconnected BSP remains faithfully represented; a missing valid cell/portal fails even if both flags are mutually reachable. |
| ART-4 | Maintain independent GNU C, Make C, and Python readers, linear semantic validation, exact-bound sidecars where applicable, and fresh-process cold load. | The readers and loader probe are **review/keep**. Delete the complete-model proof catalog, proof provider, required proof masks, and duplicate expected-model arrays. | Readers agree on malformed and valid artifacts using identity, format, count, span, reference, order, finite-value, checksum, and load rules without importing generation logic. |
| ART-5 | Delete the obsolete seed/link repair sidecar rather than migrating its route-repair format. | Runtime ownership, producers, controller state, release transport, iteration and POV callers, build entries, tests, and documentation are deleted. Standalone authenticated stall evidence remains diagnostic input only. | No legacy repair-sidecar import, caller, build entry, or operational-document reference remains. Any future learned-cost sidecar is a new stable-cell or capability-kernel contract that cannot alter geometry or connectivity. |
| ART-6 | Controller, finalizer, bundle, installer, and fleet tooling remain crash-resumable, content-addressed, and provenance-bound without generation/review deadlines. | These tools are extensive **review/reshape** candidates. | Crash/restart, stale process, partial line, duplicate worker, artifact replacement, and rollback tests pass with the new schema. |
| ART-7 | Filesystem preflight verifies stable file identity portably. Linux `/proc` checks are conditional; ext4, XFS, Btrfs, ZFS, NTFS, and exFAT mounts are not rejected merely for inode semantics. Native Windows and macOS use supported platform checks or skip Linux-only diagnostics without blocking game execution. | Exact content identity now follows retained handles and mapped bytes. POSIX component walks, Windows handle paths, Linux readable mappings, relocation-backed mappings, and file-size-bounded ELF tables fail closed under independently reviewed platform fixtures. | Platform fixtures distinguish required content identity from optional OS-specific process/file diagnostics. |
| ART-8 | Parallel generation uses 12 isolated workers with no shared writable artifact path. Hard regression maps run after the ordinary set. Worker count changes performance, not bytes. | Corpus controller already supports bounded jobs and scheduling. **Review/reshape.** | Runs with 1 test worker and 12 production workers produce byte-identical per-map output; interruption/resume is idempotent. |
| ART-9 | Measure construction wall time, CPU time, peak memory, cell/portal counts, and movement and weapon field counts. | Existing logs expose action-generation progress. **Replace metrics.** | The ordinary and hard development maps have reviewed scaling; `tomb05` is no longer a pathological search and no hidden terminal budget exists. |
| REL-1 | Build the frozen BSP set by iterating the 175-map manifest. Generate every RUNE fresh after the rewrite; do not adopt old artifacts. | Existing snapshot/controller flow can support this. **Review/reshape.** | Snapshot contains exactly 175 authoritative BSPs and zero pre-rewrite RUNEs; provenance proves every accepted artifact came from frozen bytes. |
| REL-2 | Validate and finalize all 175, assemble and install one authenticated bundle, cold-load installed maps, run ordinary match evidence, then tag and verify the local release. | Finalizer, server bundle, fleet, and release tests exist. **Thorough review/reshape.** | Every manifest map has one accepted result and installed identity; match and clean-directory release receipts bind the unchanged commit. |

## Review standard for candidate reusable code

Every **review/keep** or **reshape** item must complete these steps before its
phase closes:

1. State its target contract, authority, inputs, outputs, mutable state, and
   failure behavior.
2. Read the implementation and every production caller. Search code, generated
   tables, and tests for old seed, link, action, objective-root, route-only,
   fixed-gravity, and Dijkstra assumptions.
3. Compare its behavior with the host engine or wire specification. Do not use
   its existing tests as the sole oracle.
4. Remove one-caller compatibility layers and obsolete state; do not adapt the
   new model around them.
5. Add focused contract tests and at least one real integration check.
6. Review the final diff for player-visible LMCTF regressions, hidden limits,
   map-specific branches, shared mutable state, and silent downgrade paths.
7. Record the result in the catalog as kept, reshaped, replaced, or deleted.

## Execution plan

### 1. Freeze the target specification

- [x] Resolve the exact cell/portal/capability types, coordinate quantization,
  identity fields, cost representation, and format limits.
- [x] Define the destination-field API, strategy-plan types, tactical movement
  API, runtime belief state, and weapon profile/effect interfaces.
- [x] Define completeness, deterministic serialization, and error contracts.
- [x] Update `ARCHITECTURE.md` to distinguish current migration state from the
  target and remove the rejected graph model as architectural guidance.
- [x] Make the requirements catalog and plan tests enforce every numbered item.

### 2. Build the BSP configuration-space foundation

- [x] Add the complete BSP reader and canonical static-world model.
- [x] Construct standing/crouching player configuration cells and portals.
- [ ] Replace per-region whole-world brush scans with an exact spatial index over
  all hull-expanded world brushes. Replace the global binned face-candidate pass
  with exact adjacency preserved through each topology split.
- [ ] Represent supported, water, airborne, void-adjacent, and mover-relative
  space. Reconcile completeness directly against BSP/host collision.
- [ ] Partition the shared cell complex at movement, visibility, occlusion, and
  weapon-response discontinuities. Store compact analytic fields instead of an
  exhaustive pose, ray, trajectory, or phase-state enumeration.
- [ ] Add deterministic partitioning, deduplication, crash-resumable batches,
  metrics, and explicit overflow failure.
- [ ] Delete fixed-grid/face-anchor authority and objective pruning after all
  callers migrate.

### 3. Add movement capabilities and time cost

- [ ] Port and review walk, crouch, ramp, jump, drop, swim, air control, hook,
  mover, push, teleport, door, button, trigger, and dwell behavior.
- [x] Pass the non-enumerative hook-visibility feasibility gate.
- [ ] Partition hook and weapon visibility at first-hit and silhouette
  discontinuities, with sparse occlusion regions over the shared cells.
- [ ] Build compact local state fibers over shared cells from BSP, entity,
  mechanism, and bound-physics evidence in the compact field builder.
- [ ] Build separate bolt, body, pull, release, coast, and relaunch physics.
  Keep human hook code isolated.
- [ ] Build directional/time-weighted cost kernels from exact map physics.
- [ ] Build first-class weapon-response regions and kernels for hitscan, rail,
  spread and cone, straight bolts, rockets and splash, grenades and bounce or
  fuse, and BFG or special laws over the shared cell complex.
- [ ] Remove LMCTF rocket/grenade-jump discovery and production Dijkstra repair.
- [ ] Finish compact destination-independent construction without exhaustive
  search.

### 4. Replace runtime navigation ownership

- [ ] Replace seed/link localization with configuration/phase-space localization.
- [ ] Replace `Field_Flood` with the reviewed directional field solver.
- [ ] Add static-field caching, incremental moving-target updates, and a coarse
  region hierarchy without changing final field values.
- [ ] Finish the typed strategy migration. The queue and role, strike, item,
  supply, escort, intercept, carry, recovery, and human-order callers are in
  production. The caller still needs the accepted `FieldService` provider.
- [ ] Replace action-link descent with tactical capability selection over the
  local gradient. Port reviewed live movement and mechanism reducers.
- [ ] Delete superseded route-link commitment state and compatibility APIs.

### 5. Integrate beliefs, weapons, and learning

- [ ] Replace single-seed enemy memory with per-player runtime phase-space
  beliefs fed only by earned sight, sound, damage, item, flag, and teammate data.
- [ ] Add propagation, diffusion, negative evidence, decay, and future-position
  prediction over valid configuration space.
- [x] Build shared static visibility and occlusion queries.
- [x] Build host-parity weapon profiles.
- [ ] Integrate probabilistic weapon effect with exact live pre-fire validation.
- [ ] Bound tactical threat/opportunity deformation so it cannot replace a
  strategy or create a persistent local minimum.
- [ ] Retarget human learning to verified costs/tactic priors and remove graph
  edge nomination. Preserve post-match learning; keep live mutation optional.

### 6. Replace artifacts and audit all retained subsystems

- [ ] Implement the new wire format and migrate loader, writer, stream,
  publication, sidecars, independent readers, lint, and cold load.
- [ ] Replace route-only/objective validity throughout controller, finalizer,
  bundle, match configuration, tests, and documentation.
- [ ] Complete the mandatory reusable-code review for identities, mechanisms,
  oracles, combat, perception hooks, human capture, publication, controller,
  finalizer, bundle, fleet, and platform preflight.
- [ ] Delete old action contracts, late-path production code, dead tests, stale
  comments, obsolete diagnostics, and accidental generated output.
- [ ] Run warning-clean focused and full GNU and Make host gates.

### 7. Test two RUNEs, performance, and determinism

- [ ] Deep-check one ordinary development RUNE for walk, crouch, mechanism,
  water, visibility, and representative weapon fields.
- [ ] Deep-check `tomb05` as the hard development RUNE for gravity 100,
  airborne control, void traversal, and hook movement without Dijkstra or human
  route input. Compare retained play only after generation.
- [ ] Inject omitted and invented cells, portals, mechanism bindings, and weapon
  fields into those two fixtures and confirm the development checker catches
  them. Do not run this reconstruction against the production corpus.
- [ ] Benchmark time and memory. Confirm worker-count and batch-boundary
  determinism. Fix scaling defects before the freeze.
- [ ] Commit the source locally, run both full host gates, fast-forward local
  `main`, and keep `main` and `slipgate` identical. Do not push.

### 8. Freeze and generate all 175 RUNEs

- [ ] Build warning-clean GNU and Make modules and the private Python runtime.
- [ ] Snapshot the final commit, tools, runtimes, configuration, and exactly 175
  manifest-selected BSPs. Confirm no pre-rewrite RUNE is present, then make the
  snapshot immutable.
- [ ] Generate with 12 isolated workers, no generation or review timeout, and
  the hard regression set scheduled after the ordinary maps. Report progress
  when a map finishes or five minutes pass, including construction stage,
  coverage, capability counts, weapon-field counts, time, memory, and load state.
- [ ] Run the fast linear checks, both C readers, Python reader, exact-bound
  sidecar checks, and fresh-process bot load for every map. Do not rebuild map geometry or run per-map proof catalogs.
- [ ] Finalize one immutable content-addressed 175-map corpus after all 175
  artifacts pass the fast checks and load successfully.

### 9. Install, observe, and release

- [ ] Assemble and check the authenticated production bundle; install one
  content-addressed generation and check recovery and rollback.
- [ ] Cold-load installed maps, then run ordinary matches covering navigation,
  mechanisms, hook lifecycle, perception, combat, items, queued strategy,
  commitment retirement, roles, recording, spectator sound, and shutdown.
- [ ] If a match exposes a source defect, abandon the freeze, fix source, repeat
  both host gates, and repeat every invalidated downstream step.
- [ ] Tag the unchanged frozen commit as `v1.0.0`. Build supported Linux and
  Windows modules plus `VERSION`, `SHA256SUMS`, and the tracked static PAK.
- [ ] Copy the release to a clean directory and independently check every
  payload and installed-bundle identity. Do not publish remotely.

## Invalidation rules

- A source, schema, tool, module, configuration, engine, Python runtime, reader,
  linter, semantic validator, BSP, or identity-law change invalidates the
  snapshot and every downstream artifact.
- A RUNE change invalidates its derived fields, sidecars, cold load,
  bundle, and match evidence.
- A bundle change invalidates installation, rollback, cold-load, and match
  evidence.
- A strategy, tactic, belief, combat, or learning change invalidates the
  relevant ordinary-match evidence even if navigation bytes do not change.
- Unit and fake-engine tests never replace real BSP, real engine, or ordinary
  match evidence.

## Completion checklist

- [ ] Every catalog item is implemented and checked or explicitly deleted.
- [ ] All candidate reusable code passed the mandatory review standard.
- [ ] The target architecture and new wire contract are frozen.
- [ ] The ordinary and hard development RUNEs pass their deep BSP/host checks.
- [ ] Runtime fields, strategy, tactics, beliefs, combat, and learning pass.
- [ ] Both local host gates pass on the final source commit.
- [ ] Local `main` and `slipgate` point to the unchanged frozen commit.
- [ ] The immutable snapshot contains exactly 175 BSPs and no old RUNE.
- [ ] All 175 fresh RUNEs are finalized, pass the linear readers and checks, and
  load successfully in a fresh bot process.
- [ ] The production bundle, installation, recovery, and rollback pass.
- [ ] Ordinary match evidence is accepted.
- [ ] The unchanged commit is tagged and the clean local release is verified.

## Maintenance rule

Keep this file current. Change statuses and checkboxes when evidence lands.
Do not add command transcripts, temporary paths, unnecessary hashes, rejected
hypotheses, map-specific implementation exceptions, or superseded narratives.
