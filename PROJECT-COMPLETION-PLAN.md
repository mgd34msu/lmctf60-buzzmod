# LMCTF BuzzMod project completion plan

This file is the current execution plan. It records scope, acceptance rules,
current status, and remaining work. Git history and immutable run manifests
record experiments and superseded evidence.

## Completion definition

The project is complete when one unchanged source commit satisfies every gate:

1. Exact CI passes on synchronized `slipgate` and `main` branches.
2. One immutable input snapshot produces 175 accepted RUNEs.
3. Every RUNE passes both C readers, the Python reader, lint, applicable
   semantic checks, and a fresh-process cold load.
4. Every map is either route-complete or meets the route-only release contract.
5. The authenticated production bundle is installed and verified.
6. Ordinary matches prove the required bot behavior on the installed bytes.
7. The release tag points to the unchanged frozen commit.
8. Downloaded public assets pass version and hash checks.

## Current status

| Area | Status |
|---|---|
| Gameplay and bot source | Complete. D_SWIM, rocket jump, D_DROP, D_HOOK, combat, roles, objectives, local fallback, and human trace capture are implemented. |
| Map source repair | Complete. No remaining map requires another source-owned graph repair for initial release. |
| Corpus classification | 175 maps total. Development evidence identifies 10 route-only candidates, but this is not a fixed final count. Regenerate and test all 175 normally after the final freeze; classify only the maps that still fail complete-route closure as `ROUTE_ONLY`. |
| Branches | `main` remains on the prior proven tree. The pre-freeze candidate is committed and pushed on `slipgate`; exact CI and branch resynchronization remain. |
| Current source wave | The route-only release work and production Dijkstra fallback are implemented, independently reviewed, committed, and pushed. Both full local build/test gates pass. Exact CI remains. |
| Final freeze | Not started. Exact CI and branch resynchronization block it. |
| Final 175-map run | Not started. |
| Production matches | Not started. Fake-engine tests are tooling proof, not match evidence. |
| Release | Final `v1.0.0` is not tagged or published. Historical releases remain. |

## Fixed scope

### Corpus authority

- `tools/rune-corpus-maps.txt` is the only map authority. It contains 175 maps.
- The durable asset directory contains 180 BSPs. The manifest excludes the
  retained unsuffixed bases `lmctf05`, `smap31`, `xmap07`, `xmap11`, and
  `xmap14`.
- If a numbered map has suffixed variants, the variants are canonical and the
  unsuffixed base is out of scope. Keep every listed variant. For example,
  keep both `lmctf02a` and `lmctf02c`.
- Build the frozen BSP set by iterating the manifest. Do not copy all 180 BSPs.
- Generate all 175 RUNEs from one frozen source, module, configuration, engine,
  reader set, linter, semantic checker set, and BSP set.
- Treat every map list as an ordinary schedule input. `tools/topmaps.txt` has
  no special completion or release authority.

### Configuration roles

- Snapshot `tools/rune.cfg` as `generator_config@game/rune.cfg`. It is the
  generator configuration.
- Do not substitute validation overlays for the generator configuration.
- `tools/route-only-match.cfg` is only for ordinary route-only matches.
- Bind the empty `tools/route-only-maplist.txt` into the production bundle.
  The empty file prevents LMCTF from falling back to an ambient `maplist.txt`.

### Distribution scope

- Publish the Linux and Windows modules, `VERSION`, `SHA256SUMS`, and the
  tracked `assets/lmctf6-buzzmod.pak`.
- The public PAK contains static scoreboard art and sounds. It does not contain
  generated navigation data.
- Defer separate downloads of generated RUNEs, the generated corpus, and the
  production server bundle until the user resumes that work.

## RUNE acceptance rules

### Common requirements

Every accepted artifact must satisfy these rules:

- The RUNE and matching SNAG use the frozen module, BSP, configuration, and
  tool identities.
- The RUNE has exactly two authenticated objective roots that resolve to the
  spawned flag stands.
- The generator publishes no unproved traversal link.
- Both C readers and the Python reader agree.
- Lint and every applicable semantic checker pass.
- A new q2ded process loads the RUNE and admits bots without fallback or mixed
  sidecar state.
- Generation and cold-load processes stop cleanly.

### Complete route contract

A `complete` RUNE requires every live seed to reach both objective roots.

### Route-only release contract

A `local_only` RUNE is releasable only when all of these conditions hold:

- Every live seed reaches at least one authenticated objective root.
- Seeds that reach neither objective remain tombstoned.
- Every live seed has a finite local-objective fallback field.
- Runtime selection uses that field instead of seedless recovery when the
  missing inter-flag edge makes a coordinator objective unreachable. Combat,
  reachable attack, defense, escort, recovery, and item behavior stay active
  in each proved component.
- The seed payload contains exactly two authenticated root markers. Only those
  roots may remain terminal live sinks after neutral geometry is removed; the
  world validator resolves both to spawned flag stands and rejects arbitrary
  terminal sinks.
- The generator exhausts ordinary closure work before it emits `local_only`.
- After ordinary and BSP-aware repair, the late selector cyclically considers
  any ordered disconnected SCC pair. It publishes only exact-proved RUN,
  JUMP, DROP, SWIM, or direct HOOK links and rebuilds SCCs after each addition.
  Its 64 windows cover at most 4,096 SCC slots; exhaustion returns
  `OPEN_BUDGET` and continues to human evidence, then local-only.
- The generator does not add a link only to change the classification.
- The only missing behavior is an inter-flag path or a carried-flag return
  path.
- A real match proves normal combat and reachable attack and defense behavior
  for both teams.
- The artifact loads as `ROUTE_ONLY`. A normal `PASS` remains a distinct
  result.

A complete graph mislabeled `local_only` must fail. An unknown route contract
must fail.

### Human learning

- Human traces are evidence. They do not authorize direct topology changes.
- The server must record every active non-bot client's movement and isolate a
  selected client and frame range.
- Capture and importer tests must pass in the final source freeze.
- The importer may nominate a transition. The in-engine oracle must replay and
  prove the transition before a new RUNE includes it.
- Post-match learning is sufficient for initial release. Live RUNE mutation is
  not required.
- A learned replacement must pass the complete two-objective validator and all
  normal readers, lint, semantic checks, and cold-load checks. Regenerate its
  exact-bound SNAG and retained learned sidecars, then cold-load the staged
  bundle.
- Install new sidecars before the RUNE commit point. Publish the new graph only
  at the next coherent map setup. Partial installation must fail closed and
  must never publish mixed graph and sidecar state.

## Implemented systems

The final source candidate includes these systems:

- One current RUNE wire format and runtime contract.
- Transactional RUNE and SNAG generation, validation, cold load, installation,
  and rollback.
- Strict GNU C, independent Make C, and Python readers.
- Root-aware identity checks and fail-closed publication.
- D_SWIM, rocket jump, D_DROP, and D_HOOK generation and runtime behavior.
- Door, button, lift, train, teleporter, water, drop, hook, and objective
  mechanism handling needed by the corpus.
- Human trace capture, hook events, exact replay, and nomination-only learning.
- Local-objective fallback for valid `local_only` graphs.
- A private Python runtime builder with native dependency closure checks.
- An authenticated server-bundle builder, installer, rollback path, and active
  bundle identity.
- A persistent ten-lane fleet runner with stopped-process verification,
  serverrecord and POV evidence, receipts, and a hash-chained ledger.

The stable focused targets are:

```sh
make -f GNUmakefile rune-corpus-controller-test
make -f Makefile rune-corpus-controller-test
make -f GNUmakefile server-bundle-test
make -f Makefile server-bundle-test
make -f GNUmakefile fleet-runner-test
make -f Makefile fleet-runner-test
make -f GNUmakefile route-only-match-test
make -f Makefile route-only-match-test
make -f GNUmakefile project-completion-plan-test
make -f Makefile project-completion-plan-test
```

The full host gates remain mandatory. Focused targets do not replace them.

## Route-only candidate inventory

These ten maps previously had valid local objective regions but no proved
complete flag route. They are candidates, not a fixed release class. Improved
generation may make some or all of them ordinary `PASS` maps under the frozen
build. Regenerate and evaluate every candidate through the complete-route
contract before applying the route-only release contract.

| Development candidate | Current unresolved reason |
|---|---|
| `lmctf01` | Exact replays reject the remaining partition cuts. Two human playthroughs have already been imported. |
| `lmctf06` | The timed BFG shortcut works, but it is not the objective cut. Ranked reverse-hook repairs reject. |
| `lmctf12` | BSP contact repair added genuine SWIM links, but it did not close the route. Two human routes have been imported. |
| `lmctf15` | The timed vault mechanism works. Exact movement still leaves the two flags in separate components. |
| `lmctf19` | Each flag is in a one-way DROP basin. Exact egress replays reject. |
| `lmctf25` | The train mechanism works, but exact movement found no valid boarding link into the missing route. |
| `tomb05` | Human play proves the gravity-100 exterior hook route. The current oracle has not converted that route into a proved graph edge. |
| `xmap13` | Staged PUSH and door-egress replays remain pinned before a stable outgoing seed. |
| `xmap18` | Rocket-jump and teleporter witnesses reach neutral regions but do not connect the objective regions. |
| `xmap26` | Sparse-water generation is corrected, but the remaining objective cut has no proved bridge. |

No additional human play is required before the initial release. For any map
that remains route-only, later human play must identify the missing path and
drive a replay-proved complete-route update. That update does not block the
initial release.

## Current pre-freeze work

The route-only verifier authenticates all 175 final controller results and
launches only the remaining candidate subset in isolated game roots. The
production late selector now runs after ordinary/BSP/swim closure and before
human evidence or local-only publication. It uses exact movement owners,
retains accepted links on open completion, and rolls them back on fatal error.

Both full local build/test gates, source-size checks, deslop, linkage, and the
complete-diff review pass. The pre-freeze wave is committed on `slipgate`.
Land and prove that candidate through stage 1 below before creating the final
snapshot.

## Final execution plan

### 1. Land the final source candidate

1. Review the complete diff and remove accidental outputs.
2. Commit the coherent source wave on `slipgate`.
3. Push `slipgate` and require exact-commit CI.
4. Merge the proven tree into `main` with a no-fast-forward merge.
5. Move `slipgate` to the merge commit so both branches have the same history
   and tree.
6. Push both refs atomically.
7. Require exact-commit CI on both synchronized refs.

### 2. Create the immutable freeze

1. Build warning-clean GNU and Make modules from the final commit.
2. Require the two Linux module aliases to be byte-identical.
3. Build and verify the private Python runtime.
4. Snapshot the 175 manifest-selected BSPs and every generator input.
5. Record the source, module, engine, Python runtime, configuration, reader,
   linter, semantic checker, and BSP identities.
6. Make the snapshot immutable.

Do not commit source or documentation changes from this freeze through the
release tag.

### 3. Generate and accept all 175 RUNEs

1. Start with an empty controller run root.
2. Run bounded parallel workers with isolated ports.
3. Retry failures with the same controller command and run root. Do not use a
   separate resume path.
4. Apply the complete-route contract first. Require each result to be `PASS` or,
   only after complete-route closure fails, approved `ROUTE_ONLY`. A complete
   summary alone is not enough.
5. Recheck every artifact through both C readers, the Python reader, lint,
   semantic checks, matching SNAG validation, and fresh-process cold load.
6. Freeze the accepted corpus manifest and its evidence hashes.

### 4. Install and test the production bundle

1. Assemble the authenticated bundle from the accepted freeze.
2. Verify the archive and release manifest.
3. Install one content-addressed generation.
4. Verify every installed role and both module aliases.
5. Prove failure recovery and rollback.
6. Cold-load the installed maps before match evidence begins.

### 5. Collect real-match evidence

Run both match programs against the installed frozen bytes:

- Run the final route-only remainder in parallel with `route-only-run`. It may
  contain zero to ten of the development candidates.
- Run the ten-process persistent fleet over the scheduled 20-map rotations.

Retain evidence for:

- movement and route progress;
- objective pursuit and flag interactions;
- doors, water, drops, hooks, rocket jumps, lifts, trains, teleporters, and
  other published mechanisms;
- combat, weapon use, aiming, splash safety, and earned perception;
- carriers, escorts, recovery, interception, and defense;
- item pursuit and commitment retirement;
- bounded recovery from failed movement;
- hook fire, sustain, pull, release, re-fire, and one terminal lifecycle for
  every physical hook;
- exact rosters, serverrecord, POV recording, playback, spectator sound
  attribution, and clean shutdown.

Judge behavior from play. Scores, wins, captures, completion of a named map
list, and parser output do not replace observed behavior.

If a match exposes a source defect, abandon the freeze. Fix the owning source,
restart exact CI, create a new freeze, and repeat every invalidated gate.

### 6. Tag and verify the release

1. Tag the unchanged frozen commit as `v1.0.0`.
2. Publish the supported modules and static public assets.
3. Download every public asset into a clean directory.
4. Verify `VERSION` and `SHA256SUMS`.
5. Compare each downloaded payload with its accepted public payload.
6. Verify the installed production bundle independently through its manifest.
7. Record the final source, module, corpus, match, CI, tag, and release
   identities.
8. Add any evidence-only plan update after the release. Do not rebuild the
   release from that later documentation commit.

## Invalidation rules

- A source, tool, module, configuration, engine, Python runtime, reader,
  linter, semantic checker, or BSP change invalidates the freeze and every
  generated artifact that depends on it.
- A RUNE change invalidates its SNAG, cold-load result, bundle identity, and
  match evidence.
- A bundle change invalidates installed-bundle and match evidence.
- Fake-engine tests prove tooling behavior only. They never replace real-match
  evidence.
- Development-era artifacts and prior freezes cannot authorize the final
  corpus.

Earlier runs informed the source repairs. Final acceptance starts from a new
frozen source, module, configuration, engine, tool set, BSP manifest, and empty
controller run root.

## Completion checklist

- [x] Gameplay and traversal source implemented.
- [x] Map source repairs complete for initial release.
- [x] RUNE, SNAG, reader, lint, semantic, cold-load, and rollback tooling
  implemented.
- [x] Authenticated server-bundle and persistent-fleet tooling implemented.
- [ ] Final route-only verifier, Dijkstra fallback, release docs, version, and
  compact-plan wave integrated.
- [ ] Final source commit synchronized and exact CI green on both branches.
- [ ] New immutable source, module, configuration, tool, and 175-map freeze.
- [ ] All 175 new RUNEs accepted as `PASS` or approved `ROUTE_ONLY`.
- [ ] Production bundle installed and verified.
- [ ] Route-only and persistent-fleet real-match evidence accepted.
- [ ] Any match-exposed defects repaired and invalidated evidence repeated.
- [ ] Unchanged frozen commit tagged and public release hashes verified.

## Plan maintenance rule

Keep this file current and compact. Update status, decisions, gates, and the
checklist. Do not append command transcripts, temporary paths, rejected
hypotheses, intermediate hashes, or superseded run narratives. Store those in
Git history or immutable evidence records.
