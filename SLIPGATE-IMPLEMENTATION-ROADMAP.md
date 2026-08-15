# SLIPGATE Implementation Roadmap

Status: active

Working branch: `slipgate`

Integration target: `main` by fast-forward after the complete acceptance gate

Git identity: `Mike Davis <mgd34msu@gmail.com>`

Last updated: 2026-08-14

This is the working source of truth for correcting and extending SLIPGATE. It
combines both approved scopes:

1. Correct and improve the architecture, primitives, contracts, lifecycle,
   validation, and engineering process inherited from Claude.
2. Expand movement coverage and then improve human-like play on top of a
   mechanically trustworthy foundation.

The order is deliberate. Tactical variation must never be allowed to hide or
compensate for an invalid movement graph.

## Restart checkpoint — 2026-08-14 10:55 CDT

### Project-level status

**The project is not complete and is not ready to merge to `main`.** The
mechanical v3 foundation is complete through S3b1 offline replay integration
and dense-action generation-proof admission. The remaining project is live
integration of that replay core, compound movement implementation, final v3
corpus repair/acceptance, and the later human-like tactical program.

Current phase summary:

1. **Foundation and fail-closed v3 mechanics: complete and pushed.** This
   includes the v2 baseline, host/build foundations, action contract, engine
   identity bridge, explicit v3 wire codecs, transactional writer, strict
   loader/runtime, authenticated sidecars, leased danger persistence, and the
   isolated DROP/SWIM/HOOK replay reducers.
2. **Replay integration and v3 acceptance: active, partially complete.** The
   offline generator/oracle proof paths now use the shared reducers without a
   behavior change, and the durable 181-map v3 control corpus is accepted.
   Dense loader publication now admits only exact-artifact `RL_PROVEN` links;
   replay-all was rejected as unbounded. Live-controller adapters remain.
3. **Compound movement: not started.** `DOOR_DROP`, `DOOR_SWIM`, `DOOR_HOOK`,
   restricted RIDE, compound leases/controllers, and enabling actions 9–11
   remain future work.
4. **Human-like tactical improvement: not started.** This begins only after
   mechanical correctness and full v3 acceptance.

### Completed and fully proven

- All implementation slices through commit
  `a72c834` are committed and pushed to
  `origin/slipgate` using the repository Git identity.
- The final v2 baseline is 181/181 terminal: 135 PASS and 46 classified FAIL.
- The v3 action/identity/wire/generator/loader/runtime/sidecar foundation is
  implemented, independently reviewed, and fail-closed.
- S3a provides deterministic pure replay reducers for ordinary DROP, SWIM,
  and HOOK with strict compiler, analyzer, sanitizer, and focused-test proof.
- S3b1 routes offline DROP, SWIM, and HOOK proofs through those reducers while
  preserving command bytes, host-call cadence, proof results, and final
  phantom state. Independent refutation and representative-map differentials
  passed.
- Dense DROP, HOOK, and SWIM publication is fail-closed on `RL_PROVEN` under
  the exact payload, map/entity/physics identity, proof law, and action
  contract. The loader does not synchronously replay millions of ordinary
  links; sparse `RL_DOOR` and future compound transactions retain loader
  replay.
- The restart-safe v3 control corpus completed 181/181 with 135 PASS,
  36 classified map failures, 7 bounded timeouts, 3 invalid assets, and zero
  terminal infrastructure failures.

### Completed core but not fully polished or final-accepted

- The host seam works but still has dynamic `va_list` and const-cast cleanup.
- The deterministic host harness is not yet the complete scripted Quake host
  and full frame-capture/replay system.
- Action metadata is centralized, but execution remains split among historical
  per-action switches rather than one common dispatcher.
- Live SWIM is migrated to the reducer at `73a2049`; ordinary live HOOK is
  migrated and privately accepted at
  `/var/tmp/lmctf6-hook-live-accept.czK6jN/PRIVATE_LIVE_ACCEPTANCE_FINAL_MANIFEST.txt`
  (private bot run plus source-free attach/pull/release probes). This is not
  181-map acceptance, and compound `DOOR_HOOK` remains pending; DROP's live
  adapter also remains to be migrated. The separately accepted DROP candidate
  predates this overlapping HOOK migration and must be recomposed and rerun.
- DROP controller revision 2 freezes the wet/dry contact policy and canonical
  double-yaw byte. Its live-adapter implementation and later 181-map
  acceptance remain pending.
- There is no single unified local pre-push command or final integration gate.
- Generator seed append order, exposure sampling, and tombstone placement can
  vary with generation timing even when the normalized executable graph is
  identical. This pre-existing determinism defect is under audit and raw rune
  byte identity must not be claimed for affected maps.

### Accepted reboot recovery

The post-reboot corpus recovery is preserved under:

`tools/runs-archive/runtime-v3-ae82238-control-01/`

Frozen and verified:

- Exact source archive for `ae82238`, SHA-256
  `cafd8b8cb484b090443a4d2128653cfcf15ea06b3fb27111ba6b6161f98d1827`.
- Exact rebuilt module, SHA-256
  `bea6e097551094040cf647db1853d547520fe3f302b0bfc465ce03f603c7a7c0`;
  `ldd -r` passed.
- Patched engine, SHA-256
  `d106334aacaa77abcc9174728517235e678f097b0707c9ff7042c0b2b3866590`.
- Runtime-v3 linter, SHA-256
  `3795d9d177c75d9af07495bd41f703e4c0ccc258ffb4deff2948d3c6689d62c6`.
- Controller, SHA-256
  `7477f78a4235255f927b8009b501d1bfe384c986ee999aba95f2d23ce5c38295`.
- Controller self-test, SHA-256
  `19e0227f97555d14448eea8ea0a4a8f3734e8bc5e3440d8b80cb2b4a3c5d35c8`.
- Input manifest, SHA-256
  `79f6864e01fc2e653465fd19fc28362025018cc1c29c632c186f7d0a8e088d31`.
- Asset preflight, SHA-256
  `2ff60267cef1a9f7a5ca3da7a1fd0616e7fa5c8b611b3d69a88962a1751c3d98`.
- All 399 required assets, exact 181-map inventory, and the three expected bad
  assets (`lmctf02`, `lmctf05`, and `xmap22`).
- Reserved recovery port range `61200–61380`, deliberately disjoint from the
  unrelated q2ded fleet on `28520–28529`.

Accepted controller result:

- Run root: `corpus/control-02`
- Frozen fingerprint:
  `a50bcfda29404e611dc906bf537b4adff6df61cf6fd0c6df961a7d1258120757`
- Terminal results: 181/181
- PASS: 135
- MAP_FAIL: 36
- TIMEOUT: 7
- ASSET_FAIL: 3
- INFRA: 0
- Summary SHA-256:
  `e31bc027262b6c993fbd9946a8907c457351d50faf5bdb25197a325792eb83c4`

The only retry was `smap43`: attempt 1 ended before the command and was kept as
nonterminal `HARNESS:PRECOMMAND_EXIT_0`; attempt 2 passed. All 181 final
results, summary rows, and heartbeat counts agree under the frozen fingerprint.
All referenced rune, log, and lint hashes verify; 179 spawned-child ownership
records are exited; the controller exited 0; and ports `61200–61380` are free.

The pre-reboot d36 corpus reached 179/181 only in `/tmp` and was destroyed by
the reboot. Its observations may guide diagnosis but are not accepted evidence.

### Exact next order

1. Resolve the newly exposed generator ordering/exposure reproducibility issue
   or prove that it does not block the next differential gate.
2. Complete the in-implementation live DROP revision-2 adapter under frozen
   action contract `5c64bc3b`, then regenerate v3 artifacts and run the
   181-map acceptance gate; runtime acceptance is not claimed before that.
3. Keep the ordinary live-HOOK migration frozen, then begin the first compound
   PREOPEN/RIDE vertical slice; `DOOR_HOOK` and the full 181-map gate remain
   pending.

### Protected/unrelated state

- Do not signal or inspect-by-name-and-kill any pre-existing q2ded process.
- Preserve and exclude from commits: `.goodvibes/**`, `NOT-YET-DONE.md`,
  `SLIPGATE-RECOVERY-PLAN.md`, `aux-2-launch.log`, `docs-layout-isa.md`,
  `tests/__pycache__/**`, `sg_replay_test.gnu`, and `sg_replay_test.make`.
- This roadmap edit is the only new tracked workspace mutation made for this
  restart checkpoint.

## Success criteria

- [ ] Every supported movement link has one versioned, replayable contract.
- [ ] Generator, serializer, loader, runtime, linter, and viewer agree on that
      contract.
- [ ] The loader rejects stale, malformed, incompatible, or disconnected runes
      before bots use them.
- [x] All 181 installed maps have a terminal, classified **v2 baseline** result.
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
- [x] Add an explicit complete-table host install seam and a process-isolated,
      test-build-only reset seam with zero-live-allocation preconditions.
- [ ] Replace fixed 1024-byte double-formatting print wrappers with a safe
      `va_list`/dynamic formatting contract.
- [ ] Audit const-casting shims and document or eliminate each mutability escape.
- [x] Test distinct game- and level-lifetime allocators so cross-arena frees are
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
- [x] Support distinct allocation arenas and leak/cross-free detection in the
      first executable host-boundary harness.

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
- [x] Generate malformed rune headers, records, controls, and topology and
      require deterministic codec/loader rejection. Authenticated sidecar
      corruption remains part of the B4 sidecar slice.
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
- [x] Give every action and wire failure a stable reason code suitable for logs and corpus
      clustering.
- [x] Generate or mechanically verify C loader, Python linter, viewer, and graph
      interpretations from one schema.
- [ ] Remove duplicated switch logic only after each migrated action passes
      replay and corpus equivalence.

Done condition: an action's generator, disk record, loader, runtime, and tools
cannot evolve independently.

## Workstream E: RUNE v3

The exact wire, registry, ownership, migration, fixture, and slice contract is
maintained in [`docs/rune-v3-contract.md`](docs/rune-v3-contract.md). That
document is normative for implementation; this roadmap tracks its progress.

### Header and compatibility

- [x] Version the new format explicitly; old loaders reject v3 and v3 loaders
      reject or deliberately regenerate incomplete v2 assets.
- [x] Bind a rune to map/BSP/entity identity rather than map name alone.
- [x] Store the actual proof law: gravity, air acceleration, maximum velocity,
      funky-gravity mode, cadence, and other action-relevant constants.
- [x] Generate under a map's actual supported physics and require runtime
      equality instead of globally hardcoding gravity 800.
- [x] Use explicit little-endian field encoding or retain a compile-time
      compatibility guard with a documented migration path.

Generator/writer status:

- [x] Convert the pruned native graph to explicit v3 records without changing
      seed or link order and reject every action without a complete supported
      writer contract.
- [x] Capture the authoritative BSP/entity identity and the map's active,
      supported proof law before generation, use the captured gravity in every
      nominal oracle placement, and revalidate both immediately before commit.
- [x] Preflight the complete graph before opening a file, stream deterministic
      header/seed/link fragments, verify the second-pass payload CRC, flush and
      sync an exclusive same-directory temporary, and atomically rename only
      after all fallible validation succeeds.
- [x] Cut the C loader and live runtime over to v3 identity/physics equality.
      Runtime rejects v2 with an actionable regeneration diagnostic, validates
      one immutable v3 snapshot before publication, and holds bots across
      identity/physics drift until exact restoration. Deployment now requires
      runtime-v3 lint and installs through a same-directory durable rename.

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
- [ ] Commit canonical generated products with their source and freshness
      tests, but never mix rebuild detritus, GoodVibes state, or unrelated user
      files into implementation commits.
- [ ] Commit only a coherent, proven vertical slice.
- [ ] Push `slipgate` promptly after each accepted slice.
- [ ] Fast-forward `main` only after the complete acceptance gate.
- [ ] Return to `slipgate` for subsequent development.

## Immediate execution order

1. [x] Finish and freeze the currently running 181-map baseline.
2. [x] Review, commit, and push the current proven checkpoint without unrelated
       dirty files.
3. [x] Fix and test the host allocation/installation contract.
4. [x] Add the first mock-host and deterministic primitive tests.
5. [x] Establish the canonical action interface and stable reason codes.
6. [x] Implement the RUNE v3 header/schema and strict compatibility rejection.
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
- 2026-08-13: Commit `e0366a2` fixed the native build dependency race in
  `GNUmakefile` and `Makefile`: atomic revision/dependency generation, complete
  and identical
  91-object manifests, failure propagation, and no-op rebuild stability passed
  fresh `-j64`/`-j96` stress runs. Independent refutation found and drove fixes
  for stale header-include discovery and compiler-dependent cleanup; the final
  diff passed re-review with no remaining medium/high finding.
- 2026-08-13: Commit `055e974` froze the reviewed RUNE v3/action-contract
  architecture in `docs/rune-v3-contract.md`: explicit 128/16/44-byte
  little-endian records,
  append-only compound action IDs, generated action metadata, atomic mover
  ownership, strict identity/physics binding, migration rules, replay fixtures,
  and E0/S1-S7 done conditions.
- 2026-08-13: Host installation and isolated testing completed: the 34-slot
  inventory is structurally checked, incomplete/replacement tables fail without
  mutation, reset is absent from production, and the deterministic harness
  proves install/reset/reinstall, dynamic `gi` lookup, distinct game/level
  arenas, cross/double/unknown free detection, and bulk teardown. GNU and
  explicit makefiles use distinct test objects/binaries with transitive
  depfiles; clean parallel production+test builds and independent refutation
  passed.
- 2026-08-13: The S1a canonical RUNE action-registry slice completed in this
  commit: append-only action IDs 0-11, provenance IDs 0-4, mode-specific
  compound anchors, stable rejection reasons, wire/proof constants, and
  policy-only effective-suffix metadata now generate byte-exact C and Python
  products from strict JSON. The semantic contract is pinned at CRC32
  `769a7b8e` / SHA-256 `0790272c...`; generator freshness, 13 mutation/parity
  tests, Python syntax/tab checks, a C11 compile probe, and independent
  refutation passed. Compound actions remain deliberately runtime-disabled
  until their complete v3 generator/loader/controller slices land.
- 2026-08-13: Engine prerequisite E0 completed in the separate Yamagi engine
  repository as commit `25a7d3f`, pushed to
  `mgd34msu/yquake2:rune-host-identity`: the server now publishes protected
  authoritative BSP-checksum and coordinated physics-epoch strings before
  `SpawnEntities`, and the console can no longer strip `CVAR_NOSET` through
  flagged `set` syntax. Focused mutation, sanitizer, warnings-as-errors build,
  and live map-switch tests passed. The LMCTF consumer remains intentionally
  fail-closed work for the v3 I/O slice; engine and game DLL must deploy
  together.
- 2026-08-13: The S1b C registry adapter is ready in this commit: generated
  action/provenance enums replace the duplicate manual definitions, while
  known identity, runtime support, and wire validity remain three independent
  gates. V1 is frozen at actions 0-7, v2 at 0-8, and v3 at 0-11; compounds are
  still runtime-disabled. A standalone exhaustive descriptor test, both native
  test targets, full module build, `ldd -r`, sanitizer runs, and independent
  review gate the slice. No movement execution dispatch changed.
- 2026-08-13: Legacy Python readers now consume the generated action metadata
  without widening their wire contracts: v1 remains actions 0-7, v2 remains
  actions 0-8, provenance remains 0-3, and all three readers reject v3 until
  the explicit 128/16/44-byte decoder lands. Focused boundary fixtures pass in
  every reader, all 135 hash-matched passing baseline runes were replayed
  successfully, and representative output stayed byte-for-byte unchanged.
- 2026-08-13: Behavior-neutral C consumers now use the canonical registry for
  command ownership, full-commit localization suppression, field-cost bias,
  and tactical hook policy. Direct runtime traits never follow an effective
  suffix, so dormant compound actions remain non-executable; suffix inheritance
  is confined to pricing and route policy. Exhaustive legacy equivalence,
  sanitizer, both-makefile test/build, linked-module, and symbol-resolution
  checks passed without changing controller dispatch.
- 2026-08-13: A separate generated `RLW_*` wire-diagnostic namespace now keeps
  C and Python header/I/O/CRC/identity/graph errors aligned without changing
  the action-contract digest or making diagnostics part of executable action
  semantics. IDs 0-26 are pinned append-only; generated-product parity,
  message-outside-digest, mutation, Python, and strict C compile tests pass.
- 2026-08-14: The game-side authoritative level-identity boundary is complete.
  A staged authority captures the protected engine BSP checksum, exact
  post-override entity-text CRC, canonical map name, and coordinated physics
  epoch, then publishes only after a successful spawn. Save restore, map
  transition, shutdown, malformed or spoofable host values, and an unpatched
  engine all fail closed. CRC streaming, mock-host mutation tests, sanitizer
  and dual-build gates, patched-engine two-map replay, old-engine compatibility,
  and hash-bound campaign/AB engine snapshots passed independently.
- 2026-08-14: The first explicit RUNE v3 codec is complete in Python. It
  encodes and decodes fixed little-endian 128-byte headers, 16-byte seeds, and
  44-byte links; validates header and payload CRCs, action-contract identity,
  physics law, optional exact active identity, record laws, and graph ownership;
  and keeps wire-known disabled actions distinct from runtime authorization. A
  checked-in gravity-650 golden file round-trips exactly, every one-bit mutation
  rejects, 40,000 CRC-repaired adversarial mutations produced no parser escape,
  and all 37 contract/legacy/v3 tests plus independent refutation passed.
- 2026-08-14: Post-commit identity refutation closed three deployment and
  lifecycle gaps immediately: attested engine snapshots retain the exact
  `q2ded` process name required by fleet/deploy guards, save restore now ends
  with an explicit unavailable-identity record, and `.ent` override paths use
  one bounded formatter for both reads and writes. Durable process-name and
  maximum-path boundary tests run under both Makefiles.
- 2026-08-14: The explicit RUNE v3 codec/tool slice is complete in this commit.
  Allocation-free C and strict Python codecs agree byte-for-byte on the
  128/16/44 little-endian format, CRCs, fixed bounds, identity, record laws,
  and graph ownership; maximum-size, single-bit, CRC-repaired, sanitizer, and
  cross-language differential tests pass. The three primary Python readers
  now inspect v3 through one atomic file snapshot while preserving 135/135 v2
  baseline results byte-for-byte. Runtime-v3 lint re-applies every supported
  legacy controller law and rejects registered-but-disabled actions. The
  generator and C loader remain deliberately on v2 until the next transactional
  writer/loader slices; compound controllers remain disabled.
- 2026-08-14: The transactional v3 generator/writer slice is complete locally.
  Generation now captures authoritative level identity and supported active
  physics, scopes every nominal oracle placement to that exact gravity, adapts
  the final graph to explicit 128/16/44-byte records, and installs through a
  preflighted two-pass CRC stream plus exclusive temporary, file sync, final
  authority recheck, and atomic rename. Both production builds, all integrated
  host tests, strict/sanitizer failure matrices, contract/Python checks, and an
  independent refutation pass are green. Exact patched-engine smokes produced a
  clean gravity-650 `lmctf42` v3 rune (285 seeds, 7,619 links) and a clean
  gravity-800 `lmctf03` v3 rune (980 seeds, 10,231 links); actual gravity-650
  `lmctf07` completed proof before exposing its separate objective-core failure.
  An unpatched engine refused twice without altering the existing destination
  or leaving a temporary file. The C loader/runtime and sidecars remain the next
  deliberately blocked v3 cutover.
- 2026-08-14: The strict B3 v3 loader/runtime cutover is complete in this
  commit. Header-first bounded inspection, exact immutable-snapshot CRC and
  identity validation, literal action/controller checks, live declared-door
  replay, outbound ownership, two-objective reachability, and post-field
  authority revalidation all precede the sole graph publication point. Runtime
  rejects v2 and corrupt or mismatched v3 files with stable symbolic
  diagnostics, accepts a corrected rune on retry, and holds existing bots plus
  blocks new setup during identity/physics drift. Both native build systems,
  all integrated C tests, strict compiler/link checks, 44 Python tests, live
  gravity-800 and gravity-650 bot traversal, drift/resume, and sidecar no-I/O
  sentinels passed. Persisted graph sidecars remain deliberately disabled until
  the authenticated explicit-little-endian B4 slice.
- 2026-08-14: Commit `e14213a` added the authenticated RUNE v3 sidecar wire and
  Python pipeline. HMN, HML, HME, DPO, and DNG use one explicit little-endian
  48-byte header bound to the exact rune payload, action contract, and rune
  header CRC. The bakers consume decoded v3 records, reject stale state before
  atomic replacement, and keep tombstone/non-owner cells neutral. Shared golden
  vectors, corruption/drift tests, and independent refutation passed.
- 2026-08-14: Commit `d1e38b7` loaded authenticated HMN/HML/HME/DPO candidates
  transactionally. Each sidecar is independently optional; malformed or stale
  data is ignored once with a stable diagnostic, while valid candidates remain
  private until fields and a fresh level-authority check succeed. Loader,
  field-candidate, dual-build, sanitizer, private-runtime missing/stale-sidecar,
  and live-bot gates passed.
- 2026-08-14: Commit `d36ccaa` completed authenticated DNG3 persistence.
  Danger learning is active-play-only, persistence is opt-in and default-off,
  one exact selected server holds a whole-level advisory lease, and every
  checkpoint revalidates the immutable leased directory, installed rune,
  authority, revision, and policy immediately before atomic replacement.
  Default-off, malformed-file, intermission-freeze, competing-server,
  installed-rune-drift, strict/sanitizer, dual-build, and independent review
  gates passed; the commit is pushed to `origin/slipgate`.
- 2026-08-14: S3-S6 compound work entered implementation. Read-only architecture
  froze the dependency order: pure pose/command/status reducers first; oracle,
  generator, loader-publication, and runtime adapters second; then a dormant
  full-tail native carrier; finally PREOPEN/RIDE plus one compound support-bit
  flip at a time. The 44-byte record is accepted for PREOPEN and only for a
  uniqueness-proved restricted RIDE; ambiguous trigger, support, contact, or
  carried-TOP state must reject rather than overload the format. A fresh
  181-map runtime-v3 control corpus is running from exact commit `d36ccaa` in
  parallel so subsequent behavior-neutral claims have a format-correct baseline.
- 2026-08-14: S3a froze a host-free shared replay law for DROP, SWIM, and HOOK.
  The reducer owns literal 25 ms commands, 100 ms production boundaries,
  action phases, terminal decisions, timing witnesses, and fall-damage checks;
  adapters continue to own Pmove/ClientThink, collision and trigger queries,
  hook entities, and outer compound dispatch. The harness covers boundary and
  failure timelines under strict GCC/Clang and both sanitizer stacks. The
  two historical DROP disagreements are represented as explicit adapter
  observations: proof-versus-runtime handling of a dry shelf on a wet-destination
  route, and grounded-only versus depth-two-water nonterminal landing. S3b must
  resolve both with differential/corpus evidence rather than silently choosing
  either historical behavior. S3a also selects the generator/proof's exact
  double-promoted DROP yaw quantization as the v3 canonical command byte; the
  current live controller's one-short float-rounding difference is an explicit
  S3b migration, not a behavior-neutral refactor claim.
- 2026-08-14: The reboot recovery completed under frozen fingerprint
  `a50bcfda...`: the durable runtime-v3 control corpus reached 181/181 terminal
  with 135 PASS, 36 map failures, 7 bounded timeouts, 3 invalid assets, and no
  terminal infrastructure failure. All result/log/lint/rune hashes and summary
  counts agree; the sole precommand-exit retry passed on attempt 2; every owned
  child exited and the private port range was released.
- 2026-08-14: Commit `a72c834` completed and pushed S3b1. Offline DROP, SWIM,
  and HOOK proofs now drive the shared replay reducers with exact legacy
  command, host-call, proof, and final-state parity. `lmctf42` and `lmctf08`
  regenerated byte-for-byte; all timing variants of `lmctf10` have an identical
  normalized 1,338-seed/50,994-link execution graph. Its seed order,
  `area_hint`, and tombstone differences arise before the migrated proof seams
  and are tracked as a separate pre-existing generator determinism issue.
- 2026-08-14: Dense-action loader admission was frozen without a redundant RPF
  sidecar. DROP, HOOK, and SWIM require `RL_PROVEN` under the exact v3 payload,
  world/physics identity, proof law, and action contract. The 135 accepted
  runes encode 141,561,036 25 ms traversal quanta (per-map median 919,488;
  linearly interpolated p90 1,891,050; worst 3,494,440). Wet-source HOOK aim
  frames raise faithful replay to 141,803,912 actual 25 ms Pmove commands
  corpus-wide and 3,509,492 for worst-case `xmap08`; including zero-millisecond
  categorization calls raises total Pmove invocations to 142,504,565 and
  3,562,067 respectively. Replay-all is therefore forbidden. Sparse
  declared-door and future compound replay remains synchronous. CRC and
  identity provide integrity/world binding, not signer authentication. The
  subsequent DROP controller-revision-2 timing contract advances the semantic
  contract to `5c64bc3b` and intentionally stales prior v3 runes and indexed
  sidecars.
- 2026-08-14: S3b4 freezes the DROP revision-2 contract before live migration:
  pre-airborne supported handoff continues; post-airborne terminal status wins;
  one dry grounded recovery is allowed; later ground contact or water depth two
  or more rejects; and the command stores the canonical double-promoted yaw
  byte. The generated proof law pins 2500 ms approach, 2000 ms post-walkoff
  travel, and a 4500 ms total bound. Ordinary v3 DROP cost is a positive 100 ms
  frame multiple below that bound; writer, wire codec, loader, Python codec,
  and runtime-v3 lint reject any other value without changing v2. The focused
  contract/golden acceptance proves stale-artifact rejection and cross-language
  encoding only. A new 181-map result remains a later live-adapter and
  corpus-regeneration gate.
