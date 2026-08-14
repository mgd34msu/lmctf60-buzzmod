# SLIPGATE Implementation Roadmap

Status: active

Working branch: `slipgate`

Integration target: `main` by fast-forward after the complete acceptance gate

Git identity: `Mike Davis <mgd34msu@gmail.com>`

Last updated: 2026-08-13

This is the working source of truth for correcting and extending SLIPGATE. It
combines both approved scopes:

1. Correct and improve the architecture, primitives, contracts, lifecycle,
   validation, and engineering process inherited from Claude.
2. Expand movement coverage and then improve human-like play on top of a
   mechanically trustworthy foundation.

The order is deliberate. Tactical variation must never be allowed to hide or
compensate for an invalid movement graph.

## Success criteria

- [ ] Every supported movement link has one versioned, replayable contract.
- [ ] Generator, serializer, loader, runtime, linter, and viewer agree on that
      contract.
- [ ] The loader rejects stale, malformed, incompatible, or disconnected runes
      before bots use them.
- [x] All 181 installed maps have a terminal, classified corpus result.
- [ ] Every applicable map generates, lints, loads, and admits a live bot.
- [ ] Maps using different physics are either proved under their actual law or
      rejected with an explicit, actionable reason.
- [ ] Complex mover interactions are represented atomically rather than as
      ordinary links that happen to cross a moving brush.
- [ ] Refactors can be checked with deterministic frame/action replay rather
      than compilation alone.
- [ ] Human-like tactical behavior is measured separately from mechanical
      correctness.
- [ ] Each proven vertical slice is committed and pushed promptly using the
      repository's existing Git identity.
- [ ] The fully accepted `slipgate` branch is fast-forwarded into `main`.

## Non-negotiable design rules

1. **Proved mechanics below, flexible tactics above.** Tactics may select only
   actions whose mechanical contracts are valid.
2. **One source of action truth.** Offline proof and live execution use the same
   commands, cadence, collision rules, and terminal predicate.
3. **Atomic temporal ownership.** A mover lease and its dependent DROP, SWIM,
   or HOOK suffix may not be separated by localization, field calculation,
   SCC pruning, or replanning.
4. **No map-name patches.** Diagnose one representative map, then fix the
   underlying mover/action/physics class.
5. **Fail closed.** Unknown versions, incomplete witnesses, incompatible
   physics, transiently invalid state, and malformed graphs do not acquire
   runtime permission.
6. **Evidence before claims.** A clean build is necessary but not sufficient.
7. **Preserve user work.** Existing dirty/untracked files are not overwritten
   or folded into implementation commits without explicit scope.

## Baseline snapshots

### Initial rolling snapshot

Snapshot taken 2026-08-13 while the corpus was still running:

- Branch: `slipgate`
- Ahead of `main`: 111 commits
- Ahead of `origin/slipgate`: 1 local checkpoint commit
- Corpus inventory: 181 maps
- Complete: 150
- Pass: 117
- Fail: 33
- Remaining: 31
- Failure clusters at this snapshot:
  - 27 objective-core failures
  - 3 timeouts
  - 2 flag-objective binding failures
  - 1 v2 physics-contract refusal
- Corpus runtime: `/tmp/lmctf-corpus-runtime.ZUBcoc`
- Ledger: `/tmp/lmctf-corpus-runtime.ZUBcoc/summary.tsv`

This is a moving baseline. Final counts and hashes must be recorded when the
181-map pass terminates.

### Final v2 baseline

The current-snapshot corpus terminated and was reconciled on 2026-08-13:

- Complete: 181/181
- Pass: 135
- Fail: 46
- Failure clusters:
  - 33 objective-core failures
  - 6 truthful 900-second timeouts
  - 2 flag-objective binding failures
  - 2 physics-contract refusals
  - 3 invalid map assets
- Module SHA-256:
  `1029840dd583df7aca085196386a0b8cb89af512789d1cac61b09ffd71e5a6b3`
- Durable manifest:
  `tools/baselines/2026-08-13-rune-v2-corpus.json`
- Original reconciled manifest SHA-256:
  `1d96eb88cf6551de8df0c75e2d528987959d6cc1f8ebf906fe0ccb0a7dee24ea`
- Summary SHA-256:
  `603ca63b4a3fa1ffddf073b7212c34623ed33f1ced062e0743206c8f097e0361`

All 135 passing artifacts were re-linted clean. The three invalid assets are
`lmctf02`, `lmctf05`, and `xmap22`; they are not bot-code failures. The baseline
manifest records the one terminal-classification repair and the exact harness
and source hashes.

## Completed or proven in the current local checkpoint

These are implemented locally but still belong to the final push/merge gate.

- [x] Split the original 10,805-line `sg_arach.c` monolith into named modules.
- [x] Centralize timer, team, flag, distance, visibility, and host operations.
- [x] Remove shared per-frame pricing globals in favor of `sg_think_t`.
- [x] Initialize the complete `sg_think_t`; the former command-only memset left
      pointer fields as stack garbage.
- [x] Populate support/intercept and pricing fields before their consumers.
- [x] Confine direct engine imports to the host boundary and intentional network
      interposition.
- [x] Harden fake-client connection, team, spectator, death, disconnect, and
      map-change lifecycle paths.
- [x] Add strict rune record, provenance, marker, control, and endpoint checks.
- [x] Add loader-side duplicate, self-link, ownership, tombstone, and two-root
      reverse-component validation.
- [x] Align declared-door generation, loader replay, and live validation.
- [x] Add RL_DOOR self-link and duplicate suppression.
- [x] Correct long trigger-wait admission; exact `lmctf58` now generates 756
      RL_DOOR links, lints with zero flaws, loads, and executes a live door link.
- [x] Close `lmctf03` objective topology with exact declared-door links; strict
      generation, runtime-v2 lint, hardened loading, and live traversal pass.
- [x] Preserve exact `lmctf09` lateral HOOK witnesses through strict generation,
      lint, loader, and bot join.
- [x] Verify `lmctf44` sound-only triggers remain distinct from door controls.
- [x] Launch a resumable 12-lane, 181-map isolated corpus run.
- [x] Repair corpus result classification so terminal generator failures outrank
      clean process exit and missing linter imports cannot masquerade as flaws.

## Workstream A: Finish and freeze the corpus baseline

- [x] Let all 181 current-snapshot runs reach terminal state.
- [x] Reconcile stale/fast-exit result records from the final logs.
- [x] Save a stable summary with module hash, source hashes, map counts, action
      counts, failure signature, log path, and rune hash.
- [x] Cluster failures by mechanism and contract, not merely by map name.
- [x] Select initial representatives: `lmctf01` for compound/core, `lmctf04`
      for flag binding, `lmctf07` for alternate physics, `xmap26` for timeout,
      and `xmap22` for asset rejection. Adversarial partners are selected when
      each cluster enters implementation.
- [x] Preserve the baseline before changing the rune format.
- [x] Commit and push the currently proven checkpoint independently of later v3
      work once the checkpoint gate is recorded.

Done condition: all 181 maps have a truthful PASS/FAIL record and every failure
belongs to a named, reproducible class.

## Workstream B: Harden the existing primitives and boundaries

### Host boundary

- [x] Add `game_free` paired with `game_alloc`; stop freeing game-lifetime
      allocations through `level_free`.
- [x] Validate the complete required host table instead of using `dprint` as the
      sole initialization sentinel.
- [ ] Add an explicit host install/reset seam for isolated tests.
- [ ] Replace fixed 1024-byte double-formatting print wrappers with a safe
      `va_list`/dynamic formatting contract.
- [ ] Audit const-casting shims and document or eliminate each mutability escape.
- [ ] Test distinct game- and level-lifetime allocators so cross-arena frees are
      detectable.

### Team and clock primitives

- [ ] Add checked team/index conversion and a common `SG_ValidTeam` predicate.
- [ ] Make invalid/spectator inputs fail closed instead of silently becoming a
      negative index or RED.
- [ ] Distinguish deadline stamps from historical/since stamps with types or
      validated wrappers.
- [ ] Add unit coverage for strict versus inclusive timer edges and map-time
      reset behavior.

### Think context and bot state

- [ ] Split immutable frame inputs from stage-owned outputs, or add explicit
      validity bits/assertions for every chronological field group.
- [ ] Give the context one constructor that establishes all defaults.
- [ ] Finish converting remaining edge stages consistently where doing so
      reduces, rather than hides, dependencies.
- [ ] Partition `sg_bot_t` into action, combat, tactical, perception, lifecycle,
      and persistent-memory substructures.
- [ ] State which module owns each substructure and which transitions may reset
      it.
- [ ] Extract action controllers from the oversized movement module only after
      the action contract and replay tests exist.

Done condition: invalid primitive use is caught at its boundary, host lifetime
semantics are paired, and stage-order mistakes cannot silently read plausible
zero or stale values.

## Workstream C: Add deterministic engineering infrastructure

### Mock host

- [ ] Run SLIPGATE logic without a live Quake server using an injected host.
- [ ] Provide scripted trace, contents, BoxEdicts, Pmove, entity-link, clock,
      cvar, allocation, and output behavior.
- [ ] Support distinct allocation arenas and leak/cross-free detection.

### Frame and action replay

- [ ] Capture the complete input required to reproduce one bot frame.
- [ ] Replay it headlessly and compare the emitted command and state transition.
- [ ] Store regression fixtures for every confirmed controller/lifecycle defect.
- [ ] Add before/after differential replay for changes advertised as
      behavior-neutral.
- [ ] Add exact action-trajectory replay at 25 ms substeps and 100 ms production
      boundaries.

### Negative and property testing

- [x] Make both native build descriptions race-free from an artifact-free
      checkout: atomically generate the revision header and dependencies,
      require them before every object, propagate generator/compiler failures,
      and keep the 91-object manifests identical.
- [ ] Generate malformed rune headers, records, controls, topology, and sidecars
      and require deterministic loader rejection.
- [ ] Exercise action boundaries: exact timer edge, trigger cooldown, mover TOP
      window, collision contamination, death, disconnect, map change, and
      external impulse.
- [ ] Add reproducibility checks for generator output and final graph hashes.
- [ ] Make build, syntax, lint, replay, and representative-map checks one local
      pre-push command.

Done condition: a refactor can be falsified without starting a full match, and
every previously confirmed defect has a durable fixture.

## Workstream D: Canonical action contracts

Every action must provide the equivalent of:

```text
prove
encode
validate_record
begin
emit_frame
arrived
recover_or_abort
```

- [ ] Define a common action descriptor/dispatch interface.
- [ ] Move RUN, JUMP, DROP, HOOK, SWIM, LIFT, TELEPORT, and DOOR onto that
      interface incrementally.
- [ ] Use one command generator and one terminal predicate per action across
      oracle and runtime.
- [ ] Give every failure a stable reason code suitable for logs and corpus
      clustering.
- [ ] Generate or mechanically verify C loader, Python linter, viewer, and graph
      interpretations from one schema.
- [ ] Remove duplicated switch logic only after each migrated action passes
      replay and corpus equivalence.

Done condition: an action's generator, disk record, loader, runtime, and tools
cannot evolve independently.

## Workstream E: RUNE v3

### Header and compatibility

- [ ] Version the new format explicitly; old loaders reject v3 and v3 loaders
      reject or deliberately regenerate incomplete v2 assets.
- [ ] Bind a rune to map/BSP/entity identity rather than map name alone.
- [ ] Store the actual proof law: gravity, air acceleration, maximum velocity,
      funky-gravity mode, cadence, and other action-relevant constants.
- [ ] Generate under a map's actual supported physics and require runtime
      equality instead of globally hardcoding gravity 800.
- [ ] Use explicit little-endian field encoding or retain a compile-time
      compatibility guard with a documented migration path.

### Atomic compound mover actions

Implement one honest compound record rather than a localizable pseudo-seed pair.
The currently reviewed design uses a 44-byte edge containing:

- Exact mechanism/contact witness.
- Exact suffix-control anchor.
- `PREOPEN` or `RIDE` mode.
- Suffix action: DROP, SWIM, or HOOK.
- `sweep_clear_ms`, distinct from final suffix arrival.
- Complete timing/window and mover-set identity required for replay.

Required capabilities:

- [ ] `DOOR_DROP`: activate or ride a valid hatch, retain the mover lease until
      the hull clears its complete sweep, then finish the proved DROP.
- [ ] `DOOR_SWIM`: activate a submerged door and execute the proved wet suffix.
- [ ] `DOOR_HOOK`: activate/open from below, prove bolt launch/flight clearance,
      retain the lease until hull and bolt are sweep-clear, then finish HOOK.
- [ ] Preserve ordinary dry `RL_DOOR` for its simpler valid class.
- [ ] Restrict ride mode to validated repeatable, noncrusher, nontoggle,
      nonscripted mover teams with deterministic motion and safe occupancy.
- [ ] Re-prove authoritative live suffixes and define recovery ownership for
      perturbation rather than releasing an unsafe body to generic navigation.

Primary evidence class: `lmctf01` requires compound interactions with `*23/*24`
and `*25/*26`; ordinary DROP/HOOK through their closed sweeps is correctly
rejected, and dry `RL_DOOR` cannot express the route.

Done condition: `lmctf01` closes its objective core through replayable compound
records without weakening ordinary collision or mover checks.

## Workstream F: Close systemic corpus failure classes

For each cluster, follow Write -> Review -> Fix -> Confirm:

1. Reproduce a representative failure with exact hashes.
2. Identify the missing or inconsistent contract stage.
3. Implement one systemic fix with one writer.
4. Refutation-review the actual diff independently.
5. Fix confirmed critical/high findings, at most two review cycles.
6. Run focused replay and representative/adversarial maps.
7. Rerun the affected cluster.
8. Commit and push the proven slice.

Known initial clusters:

- [ ] Objective-core closure failures.
- [ ] Flag-objective binding failures.
- [ ] Physics-contract variants.
- [ ] Generator timeouts/performance cliffs.
- [ ] Compound mover transitions.
- [ ] Any later loader, lint, or live-controller failure exposed by v3.

Done condition: every map is either green or has a narrow, explicitly approved
unsupported engine/map feature rather than a generic core failure.

## Workstream G: Improve tactical and human-like behavior

This begins only after the mechanical layer and corpus gate are trustworthy.

### Measurement

- [ ] Define separate metrics for reachability, objective competence, combat
      effectiveness, and human resemblance.
- [ ] Build human-demo distributions for route choice, hook use, reaction time,
      aim correction, item detours, flag entry/escape, and movement tempo.
- [ ] Preserve earned-information rules in every comparison.

### Behavior

- [ ] Add route diversity among proven near-optimal alternatives.
- [ ] Add contextual rather than global variation in reaction time and aim.
- [ ] Model correction and commitment instead of injecting arbitrary noise.
- [ ] Improve item timing, detours, formation spacing, and escape selection from
      measured evidence.
- [ ] Keep stochastic decisions reproducibly seedable for tests.
- [ ] Ensure tactical mistakes never bypass action validity.

### Evaluation

- [ ] Compare bots and humans on withheld maps/demos.
- [ ] Run blinded gameplay review separately from correctness acceptance.
- [ ] Reject changes that improve resemblance while degrading objective or
      mechanical gates beyond an approved bound.

Done condition: behavior improvements have measured effects and remain strictly
above the proved movement layer.

## Workstream H: Delivery and integration

Each vertical slice must pass, as applicable:

- [ ] Focused syntax/type checks.
- [ ] Clean full build with no swallowed exit status.
- [ ] `git diff --check`.
- [ ] `ldd -r` with no unresolved symbols.
- [ ] Unit/property/replay fixtures.
- [ ] Runtime-v2/v3 lint.
- [ ] Graph invariants and objective reverse reachability.
- [ ] Hardened C loader.
- [ ] Explicit bot join and representative live action.
- [ ] Affected map cluster.
- [ ] Full 181-map final corpus.

Commit policy:

- [x] Preserve `Mike Davis <mgd34msu@gmail.com>` as author and committer.
- [ ] Do not mix generated artifacts, GoodVibes state, or unrelated user files
      into implementation commits.
- [ ] Commit only a coherent, proven vertical slice.
- [ ] Push `slipgate` promptly after each accepted slice.
- [ ] Fast-forward `main` only after the complete acceptance gate.
- [ ] Return to `slipgate` for subsequent development.

## Immediate execution order

1. [x] Finish and freeze the currently running 181-map baseline.
2. [x] Review, commit, and push the current proven checkpoint without unrelated
       dirty files.
3. [ ] Fix and test the host allocation/installation contract.
4. [ ] Add the first mock-host and deterministic primitive tests.
5. [ ] Establish the canonical action interface and stable reason codes.
6. [ ] Implement the RUNE v3 header/schema and strict compatibility rejection.
7. [ ] Implement one complete compound vertical slice (`DOOR_DROP`) with
       generator, loader, runtime, linter, replay, and `lmctf01` verification.
8. [ ] Add `DOOR_SWIM` and `DOOR_HOOK` through the same contract.
9. [ ] Close remaining failure clusters in parallel where files do not overlap.
10. [ ] Run the complete acceptance matrix and integrate into `main`.
11. [ ] Begin the measured tactical/human-behavior program.

## Progress log

Append concise entries here when a slice changes state. Every entry should name
the commit, verification, corpus scope, and remaining blocker.

- 2026-08-13: Recovery checkpoint `3b2ac2d` pushed to `origin/slipgate` using
  `Mike Davis <mgd34msu@gmail.com>`; 181-map baseline remains active in 12
  isolated lanes.
- 2026-08-13: Roadmap created and adopted as the implementation source of truth.
- 2026-08-13: Host allocator slice completed: paired `game_alloc`/`game_free`,
  migrated all generator cleanup paths, and added complete 34-slot host-table
  validation. Clean rebuild, `ldd -r`, syntax checks, diff check, runtime smoke,
  and independent refutation passed.
- 2026-08-13: Initial 181-map v2 baseline completed: 135 PASS / 46 FAIL. All
  passing runes re-linted clean; failures were reconciled into 33 core, 6
  timeout, 2 flag-bind, 2 physics-refusal, and 3 invalid-asset results. Durable
  manifest added under `tools/baselines/`.
- 2026-08-13: Native build dependency race fixed in both `GNUmakefile` and
  `Makefile`: atomic revision/dependency generation, complete and identical
  91-object manifests, failure propagation, and no-op rebuild stability passed
  fresh `-j64`/`-j96` stress runs. Independent refutation found and drove fixes
  for stale header-include discovery and compiler-dependent cleanup; the final
  diff passed re-review with no remaining medium/high finding.
