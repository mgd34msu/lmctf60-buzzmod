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
- [x] Standalone `tools/runegen.sh` escalates generator and cold-load timeouts,
      then allows five seconds before forced termination. Its GNU and Make
      tests pass. The 181-map Python controller already used escalation.

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
- [x] D_HOOK source integration and recorded real-engine lifecycle.

## Completed feature: D_HOOK

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
- The canonical D_HOOK source is merged at `d5d4365`.

### Integration status

- [x] Rebase the complete D_HOOK stack onto the current `slipgate` source
      without dropping D_SWIM, D_DROP, or rocket-jump support.
- [x] Unify the generated action contract for actions 7, 9, 10, and 11.
- [x] Pass contract, action, codec, and source-size checks after the rebase.
- [x] Pass the full GNU and Make host and module gates, `ldd -r`, and deslop
      with zero findings on source-identical candidate `bfcabf2`. Its tracked
      worktree was clean.
- [x] Merge and push the proven fix stack through `slipgate` to `main` at
      `d5d4365`. Local and remote `slipgate` and `main` compare as zero ahead
      and zero behind.

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

Live preflight also rejected `as2m1`, `tw2ctf4`, `lmctf23`, `lmctf17`,
`lmctf06`, `lmctf14`, `lmctf16`, `lmctf33`, and `xmap11` as D_HOOK
witnesses.

The ranked search then tested 21 more maps. Nineteen completed with zero
publication. `xmap26` and `xmap29` reached the bounded generation limit, and
neither installed a pair. Results and logs remain under
`/tmp/lmctf6-dhook-witness-search`. This ends blind brute-force selection.

The `xmap06` runs also exposed and verified the old 100 ms touch-frame defect.
The corrected runtime uses `touch_frame_end_ms + suffix_start_ms`. All three
links now reach authenticated Begin, touch, and activation. Their shared map
hazard makes them negative live evidence, not a completed lifecycle.

### Accepted real-engine witness

- [x] `xmap06` link 89637 reached Begin, touch and activation, LINKED at 500 ms,
      ATTACHED at 900 ms, four pull steps, and applied release at 1400 ms.
- [x] The action reached terminal COMPLETE with no failure at 2200 ms, cleared
      ownership, and delivered the bot to a dry destination.
- [x] The accepted witness uses module SHA-256
      `32a344865eb2e99b65acf8ce033eae46c1af715cf4170e2b8df26079c2d43df1`
      and lifecycle-log SHA-256
      `97a142d62784c40d93eb34672691406379bbff65e432d50dd8f4be9903ad2ed8`.

D_HOOK is complete. Its source integration and required real-engine lifecycle
both passed.

## Prior freeze and current invalidation

- [x] Frozen source `72c52db` passed exact-commit `slipgate` and `main` CI on
      Windows x86 and x64 and on Linux with GNUmakefile and Makefile under GCC
      and Clang. Both CI runs are green and uploaded their intended artifacts.
- [x] The chosen GNU module has SHA-256
      `2d9cf6029586cf07918617ab9d8f459356787dd70cf63a6a153fc6af35ec52d5`.
      The snapshot uses those exact bytes for both `game/game.so` and
      `game/gamex86_64.so`.
- [x] The immutable input snapshot is
      `/tmp/lmctf6-final-freeze-72c52db/input-snapshot`. It contains all
      181 required BSPs and has no writable file, writable directory, or
      symlink.
- [x] The input-manifest SHA-256 is
      `422144842721e6bda7e1433d0edb0b17b464dc6ac0c074ef49dd292dff58d0a5`.
- [x] All 53 controller tests passed. The jobs=10 dry-run assigned all 181 maps
      and produced fingerprint
      `01b84dfb9203909293af4483d335af12bb67842ada4dc09c57ff77a7af3a2221`.
- [x] Freeze reports are
      `/tmp/lmctf6-final-freeze-72c52db/FREEZE-EVIDENCE.md` and
      `/tmp/lmctf6-final-freeze-72c52db/FREEZE-MANIFEST.json`; supporting logs
      are under `/tmp/lmctf6-final-freeze-72c52db/logs`.
- [x] The corpus controller now treats the exact post-write, same-map missing
      SNAG and field-setup sequence as deferred publication. It then stops the
      generator, runs the artifact gates, creates the authenticated zero-repair
      SNAG, and requires a fresh cold-load RUNE-ready result. Missing or invalid
      SNAG data during cold load still fails closed.
- [x] The controller repair is canonical at `98775a4`. Its GNU and Make
      controller suites each passed all 53 tests. A frozen-input `lmctf14` run
      completed generation, dual-reader acceptance, SNAG creation, and fresh
      cold load with a terminal PASS.
- [x] Teleporter staging and objective-core diagnostics are canonical at
      `38f6e32`. `lmctf02a` completed generation, dual-reader acceptance,
      semantic gates, authenticated SNAG creation, and fresh cold load with a
      terminal PASS.
- [x] Declared-door sibling egress replay is canonical at `119bc96`. Fresh
      accepted pairs for both `lmctf03` and `mactf01` reached authenticated
      SNAG-ready and RUNE-ready during cold load.
- [x] The separate, fingerprinted cold-load timeout is canonical at `30c8667`.
      The GNU and Make controller suites each passed all 55 tests. A sealed
      `lmctf03` artifact passed direct cold load with the 10-second startup
      delay and a 420-second cold-load budget.
- [x] Exact-commit CI for `30c8667` passed on both `slipgate` and `main`. Both
      runs passed the Windows x86 and x64 jobs, Linux, all four GCC and Clang
      Make-dialect jobs, and the version check.
- [x] The `lmctf04` objective-root source repair is canonical at `3b6b1f5`.
      Both flag roots bind, and the mutual core retains 979 seeds and 26,373
      links. The isolated end-to-end run passed both readers, Python, lint,
      authenticated SNAG creation, and fresh cold runtime-ready.
- [x] The full GNU and Make gates passed for the `lmctf04` repair. Exact-commit
      CI then passed on both `slipgate` and `main` across the full Windows,
      Linux, compiler, and Make-dialect matrix.

The `72c52db` game module and its recorded evidence remain proven. They are no
longer the final combined source and tool freeze. The controller changed after
that freeze, and the active graph repairs will change game source. The old
snapshot, fingerprint, and generated RUNEs cannot authorize the final corpus.

- [ ] Finish and integrate the remaining graph repairs.
- [ ] Pass exact-commit CI for the final combined source and tools on both
      `slipgate` and `main`.
- [ ] Rebuild the final module, create a new immutable 181-map input snapshot,
      and record its exact manifest and controller fingerprint.

## Active work: repair, refreeze, and regenerate all 181 RUNEs

The run at `/tmp/lmctf6-rune181-72c52db` is sealed from acceptance and retained
only as diagnostic evidence. The run is complete with all 181 terminal results:
zero PASS, 31 `GEN_FAIL`, and 150 `TIMEOUT`. No worker remains active. The final
summary, every referenced result hash, each map identity, and each terminal
classification passed the integrity check. No artifact from this run belongs
to the accepted corpus. The project still requires a new freeze and a full
181-map restart from an empty run root.

Most recorded timeouts completed RUNE generation and were waiting for a
same-process RUNE-ready line after the runtime correctly rejected the missing
SNAG declaration. The controller repair removes that misclassification. At
least `lmctf15` and `lmctf25` are genuine no-write generation timeouts and must
remain timeouts unless later source repair lets generation finish.

### Repair tracks

- [ ] Complete the isolated `lmctf05` and `lmctf05b` repair. The candidate
      generalizes `START_OPEN` vertical `func_door` carrier support through
      authenticated `RL_LIFT`, but it is not accepted yet.
- [ ] Repair the remaining maps whose objective route core is closed.
- [ ] Repair the missing central transition in `lmctf07`.

1. Finish the source-owned graph repairs above, including every
   failure found by the diagnostic run.
2. Build and verify one new exact source, module, configuration, engine, reader,
   linter, semantic-checker, and BSP snapshot.
3. Restart the controller from an empty run root with durable isolated output,
   disjoint worker ports, ten bounded workers, and per-map timeouts.
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
6. Repair any new failure at the source, data, or tool boundary that owns it,
   then refreeze and restart every artifact invalidated by that repair.
7. Freeze the final 181-artifact manifest and its evidence hashes.

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
finish the remaining graph repairs
  -> pass exact CI and create a new source, module, and input freeze
  -> restart and validate all 181 RUNEs
  -> run ordinary real matches
  -> repair defects and repeat invalidated evidence
  -> update documentation, tag, and publish
```

## Completion checklist

- [x] RUNE/SNAG generation, verification, cold load, and installation.
- [x] `lmctf58` repair and accepted runtime pair.
- [x] D_SWIM.
- [x] Rocket jump.
- [x] D_DROP.
- [x] D_HOOK integrated and completed in the real engine.
- [x] Prior `72c52db` module identity and evidence proven.
- [x] Controller deferred-SNAG phase repair, full tests, live cold-load proof,
      and exact CI on both branches.
- [x] Teleporter and objective-core diagnostics integrated, with `lmctf02a`
      accepted end to end.
- [x] Declared-door replay integrated, with `lmctf03` and `mactf01` cold-ready.
- [x] Separate cold-load timeout integrated, tested, and green in exact CI.
- [x] `lmctf04` objective-root repair integrated and accepted end to end.
- [ ] Remaining graph blockers repaired and integrated.
- [ ] New final combined source, tool, module, and input freeze.
- [ ] Exactly 181 newly generated and fully accepted RUNEs.
- [ ] Real-match behavioral validation with ordinary map-list inputs.
- [ ] Match-exposed defects repaired and revalidated.
- [ ] Final compiler, Make dialect, platform, and repository gates.
- [ ] Documentation, version, branch integration, tag, release, and hash audit.
