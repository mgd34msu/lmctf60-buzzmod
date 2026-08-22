# LMCTF BuzzMod project completion plan

This is the canonical execution plan. It records what is done, what remains,
and what counts as completion. Historical plans and stale checklists do not
override it.

## Fixed scope

- Generate a new RUNE for every map in `tools/rune-corpus-maps.txt`. The corpus
  contains 181 maps. `lmctf02` and `lmctf02c` count separately.
- Generate all 181 RUNEs from one frozen source, module, configuration, engine,
  reader set, linter, semantic checker set, and BSP set.
- Treat every map list as an ordinary server or harness input. `topmaps.txt`
  and its 20 entries have no special completion or release status.
- Judge bot quality from behavior during play. Scores and wins provide context,
  but they are not acceptance criteria.
- Run player-visible behavior through the real game and engine. Focused tests
  and source inspection do not replace live execution.
- Defer downloadable RUNE or PAK distribution. It is outside this completion
  run.

## Completed work

### RUNE and runtime base

- [x] One current RUNE wire and runtime contract.
- [x] Strict GNU C, independent Make C, and Python readers.
- [x] Root-aware lint and authenticated identity checks.
- [x] Transactional RUNE/SNAG generation, validation, cold-load verification,
      and installation.
- [x] Required SNAG data fails closed when missing or invalid.
- [x] Real two-process RUNE/SNAG workflow through q2ded.

### `lmctf58`

- [x] Map-specific route repair.
- [x] All ten declared-door controller identities retained.
- [x] All six cellar identities have valid plans.
- [x] Representative cellar routes reach both flag roots.
- [x] Objective-core remains authoritative. Completion does not retain
      unreachable tombstones or disable pruning.
- [x] RUNE and zero-repair SNAG accepted, installed, and cold-loaded.
- [x] Both C readers, Python decoding, lint, semantic checks, strict compilers,
      sanitizers, and live server execution passed.

`lmctf58` is complete. Its RUNE will be regenerated with the other 180 maps
after the final source freeze.

### Traversal and support

- [x] D_SWIM generation, publication, ownership, execution, recovery, and live
      proof.
- [x] Authenticated rocket-jump generation, projectile identity, execution,
      recovery, and live proof.
- [x] D_DROP generation, publication, live reproof, target fanout, recovery,
      release, and live proof.
- [x] Harnesses fail on empty matches and analyzer failures.
- [x] Serverrecord handles entity disappearance without inventing frozen
      player positions.
- [x] Role, movement, combat, objective, hook, and coordination reporting has
      executable coverage.
- [ ] D_HOOK final integration and real-engine proof.

## Active work: D_HOOK

### Implemented

- The production call chain, coordinator, publication and oracle path, game
  adapter, engine receipts, lifecycle orphaning, and generator discovery are
  implemented.
- D_DROP and D_HOOK guards are action-specific.
- The runtime consumes every fourth 25 ms D_HOOK boundary inside `Think_Emit`,
  so no command crosses the entity-attachment pass.
- Retained recovery approves the replacement command in the same slot. The
  regression suite covers the first recovery step and premature release.
- COMPLETE and SAFE_STOPPED precede requested release.
- Selection, Begin, and irreversible fire each check offhand readiness.
- Immediate-trace bolts publish exact identity before synchronous touch and do
  not leave a stale pointer after abort.
- Ordinary hooks bypass the compound adapter.
- Disconnect cancels delayed uses, orphans the action while identities remain
  valid, removes the physical hook, evicts the bolt, unlinks the entity, and
  then completes disconnect.
- The authenticated `sv sg compoundhook <link>` seam stages only an exact
  published link and records approach, activation, LINKED, ATTACHED, PULL,
  release or recovery, terminal outcome, and ownership retirement.
- The corrected D_HOOK source checkpoint is frozen at `23ae46d`. Its focused
  and full host suites passed under GNUmakefile and Makefile, both production
  modules linked cleanly, and the tracked test worktrees were clean.

### Integration status

- [x] Rebase the complete D_HOOK stack onto the current `slipgate` source
      without dropping D_SWIM, D_DROP, or rocket-jump support.
- [x] Unify the generated action contract for actions 7, 9, 10, and 11.
- [x] Pass contract, action, codec, and source-size checks after the rebase.
- [x] Synchronize local and remote `slipgate` and `main` without rewriting
      history. Both branches track this plan and compare as zero ahead and zero
      behind at the last completed sync.
- [ ] Finish the integrated GNUmakefile and Makefile host and module gates.
- [ ] Commit the verified integration to `slipgate`, merge it to `main`, push
      both branches, and confirm zero ahead and zero behind.

### Live candidate evidence

Candidate tests use the corrected committed module. A valid zero-witness pair
proves the pipeline for that map but does not complete D_HOOK.

| Map | Strict mechanisms | Hook proof result | Outcome |
| --- | ---: | --- | --- |
| `lmctf53` | 0 | none | valid pair, no witness |
| `bmap6` | 0 admitted | none | 143 water seeds; preopen resolver rejected its movers |
| `lmctf02c` | 8 doors, 0 reachable water seeds | impossible | rejected before generation |
| `lmctf08` | 2 doors | no water seed in discovery envelope | rejected before generation |
| `lmctf32` | 4 | no incoming water seed in discovery envelope | valid pair, no witness |
| `xmap28` | 0 | none | rejected by live resolver |
| `smap46` | 0 | none | rejected by live resolver |
| `xmap08` | 0 | none | valid pair, no witness |
| `xmap05` | 10 | 425 approaches rejected before hook proof | valid pair, no witness |
| `lmctf11` | 4 | 48 trials, no proof or publication | valid pair, no witness |
| `smap10` | 10 | 538 approaches rejected before hook proof | valid pair, no witness |
| `xmap06` | 10 | 12 trials, 3 published links | live attempts reached activation, then died in the same under-map water hazard before bolt linkage |

The exact `xmap06` witness pair is retained because it identifies the three
published links used in the live failure analysis:

- RUNE SHA-256: `16e754c2899f6e0aa2072bb26740832a91206428b4acb53c249fb90b91537e26`
- SNAG SHA-256: `3ef88df4334477ed4605d48fe7f3d19151c6b65a07880c9af4963199722cbe3b`

The `xmap06` runs also exposed and verified the old 100 ms touch-frame defect.
The corrected runtime uses `touch_frame_end_ms + suffix_start_ms`. All three
links now reach authenticated Begin, touch, and activation. Their shared map
hazard makes them negative live evidence, not a completed lifecycle.

### D_HOOK work left

1. Finish the rebased integration gates and merge the verified source through
   `slipgate` to `main`.
2. Preflight more water and door candidates through the live strict resolver.
3. Generate the first pair that publishes an admitted D_HOOK link without the
   `xmap06` terminal hazard.
4. Independently accept, cold-load, and install that exact RUNE/SNAG pair.
5. Record one real lifecycle through approach, activation, hook fire, exact
   bolt link, attach, pull, release or bounded recovery, settlement, ownership
   retirement, and ordinary route continuation.

D_HOOK is complete only after step 5 passes in the real engine. A focused test,
clean module link, or generated link alone is not enough.

## Final source freeze

After D_HOOK completes:

1. Integrate every accepted feature into `slipgate`, then merge the proven tree
   to `main`.
2. Remove temporary probes, diagnostics, stale artifacts, and obsolete dormant
   assertions. Keep acceptance evidence needed for review.
3. Regenerate all generated contracts from the final source.
4. Run the complete GNUmakefile and Makefile host suites under GCC and Clang.
5. Run strict warnings, ASan, UBSan, dependency checks, exported `GetGameAPI`,
   module loading, `ldd -r`, diff checks, and deslop.
6. Build both production module aliases and prove their intended identity.
7. Freeze the source commit, final module hashes, configuration, engine,
   readers, linter, semantic checks, BSP set, and 181-map manifest.

Any source or generated-contract change after this freeze invalidates the
module freeze and every RUNE generated from it.

## Generate and validate all 181 RUNEs

1. Create a read-only generation snapshot from the frozen final candidate.
2. Include the exact engine, module aliases, production physics and config,
   181 BSPs, map manifest, readers, linter, and semantic checks.
3. Use durable isolated output roots, disjoint worker ports, bounded parallel
   workers, and per-map timeouts.
4. Require every map to produce:
   - a new RUNE;
   - the matching SNAG declaration, including authenticated zero repairs;
   - two valid objective roots;
   - a clean generator shutdown;
   - acceptance by both C readers and the Python reader;
   - root-aware lint acceptance;
   - every applicable map-specific semantic check;
   - a fresh-process cold load with an admitted bot.
5. Require exactly 181 PASS results. A generation failure, timeout, lint
   failure, reader disagreement, semantic failure, or cold-load failure blocks
   completion.
6. Repair failures at the source, data, or tool boundary that owns them.
7. If frozen source changes, rebuild and regenerate every affected artifact so
   the accepted corpus has one final identity.
8. Freeze the final 181-artifact manifest and its evidence hashes.

## Real-match validation

Run ordinary matches with the final module and RUNEs. The supplied map list is
only the schedule.

Retain evidence for:

- continuous movement and route progress;
- objective pursuit and flag interactions;
- appropriate door, swim, drop, hook, rocket-jump, lift, fall, water, and
  teleport traversal;
- combat, weapon use, aiming behavior, splash safety, and earned perception;
- carrier, escort, recover, intercept, and defender behavior;
- item pursuit and commitment retirement;
- bounded recovery from failed traversal and geometry;
- one valid terminal lifecycle for every physical hook;
- POV recording, playback, and spectator sound attribution;
- clean bot rosters, server operation, and shutdown.

Scores, wins, captures, completion of a named map list, and parser output do not
replace observed behavior. When a match exposes a defect, fix the owning code,
rerun its focused gates, rebuild the candidate, and repeat any invalidated live
evidence.

## Final integration and release

1. Reconcile documentation with the behavior and tools that ship.
2. Confirm the worktree and final commit set contain no accidental runtime
   outputs or unrelated files.
3. Commit and push the final coherent `slipgate` milestones.
4. Require Linux and Windows CI on the exact commit, including both Make
   dialect and compiler matrices.
5. Merge the proven tree to `main` and require the same exact-commit CI.
6. Set the release version, create the tag, and publish the intended assets.
7. Download the release and verify its hashes and version identity.
8. Record the final source, module, 181-artifact corpus, real-match evidence,
   CI, tag, and release identities.

Downloadable RUNE or PAK packaging remains deferred until explicitly resumed.

## Critical path

```text
finish integration and live-prove D_HOOK
  -> freeze the final source and module
  -> generate and validate all 181 RUNEs
  -> run ordinary real matches
  -> repair defects and repeat invalidated evidence
  -> pass CI, update documentation, tag, and publish
```

## Completion checklist

- [x] RUNE/SNAG generation, verification, cold load, and installation.
- [x] `lmctf58` repair and accepted runtime pair.
- [x] D_SWIM.
- [x] Rocket jump.
- [x] D_DROP.
- [ ] D_HOOK integrated and completed in the real engine.
- [ ] Final source and module freeze.
- [ ] Exactly 181 newly generated and fully accepted RUNEs.
- [ ] Real-match behavioral validation with ordinary map-list inputs.
- [ ] Match-exposed defects repaired and revalidated.
- [ ] Final compiler, Make dialect, platform, and repository gates.
- [ ] Documentation, version, branch integration, tag, release, and hash audit.
