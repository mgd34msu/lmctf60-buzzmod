# LMCTF BuzzMod completion plan

This document is the current execution plan for completing, promoting, and
releasing the project. It replaces the old feature-oriented implementation
roadmap. A checkbox is evidence-backed only when the named consumer path has
passed on one frozen source, module, configuration, and artifact set.

## Definition of done

The project is complete only when all of the following are true together:

1. The current SLIPGATE implementation builds without warning-gate failures on
   Linux x86_64, Windows x86, and Windows x64.
2. Every behavior already developed for the bots remains covered by its focused
   and host regression gates on the final source tree, and release CI runs those
   gates under GNUmakefile and Makefile with both GCC and Clang.
3. All twenty production maps have current RUNE artifacts that pass both C
   readers, the Python reader and linter, fresh-process runtime loading, and
   map-specific semantic checks.
4. `lmctf58` retains all ten long declared-door controllers, including the six
   mirrored cellar controllers, with real routes from the cellar region to both
   objectives. Objective-core pruning may not be bypassed.
5. The production fleet installs one hash-pinned module/configuration/artifact
   bundle transactionally, can restore the exact previous bundle, and refuses
   to start or deploy across an ownership or quiescence failure.
6. Two consecutive ten-server production waves cover all twenty maps with nine
   5v5 lanes and only s08 at 7v7, a uniform 900-second duration, no staggered
   roster mutation, correct POV capture, exact RUNE identity, and clean lane
   shutdowns.
7. The final source, installed bundle, generated corpus, wave evidence, version,
   documentation, branch history, tag, CI jobs, and published release assets
   all identify the same release.
8. The formerly roadmap-only player-visible claims have consumer evidence: a
   real recorded POV demo plays back, human-speed and defense outcomes are
   measured, snag census data reaches routing, the hook aim-wedge retirement is
   executed through the production controller, item pickup closes through
   `Touch_Item`, hook lifecycles pair, and an observer receives the attributed
   spectator sound.

No single unit test, successful build, generated artifact, server banner, or
wave is sufficient by itself.

## Current execution state

- [x] RUNE is a single current wire/runtime contract; obsolete format selection
  is gone from the source and readers.
- [x] The cross-platform compiler-warning defects known on the previous release
  candidate are fixed on a pushed SLIPGATE commit and its four branch jobs are
  green.
- [x] GitHub Actions dependencies are pinned to immutable Node 24 revisions and
  the replacement branch run is green with zero annotations.
- [x] The previously rejected `lmctf44` artifact was regenerated and passed an
  independent fresh-process runtime load.
- [ ] The symmetric shallow-water declared-door repair is frozen, independently
  reviewed, integrated, and committed.
- [ ] The repaired module regenerates a semantically complete `lmctf58` artifact.
- [ ] The exact final module and twenty-artifact corpus pass the cold-load matrix.
- [ ] The production fleet/bundle transaction and rollback are frozen, reviewed,
  integrated, and committed.
- [ ] Release CI runs the complete behavior/RUNE host suite for both Make
  dialects with GCC and Clang.
- [ ] Real snag data and the remaining player-visible consumer proofs are bound
  to the final corpus and wave evidence.
- [ ] The old hooktest fleet is quiesced and the production bundle is promoted.
- [ ] Two complete production waves and final gameplay/POV diagnostics pass.
- [ ] Documentation, cleanup, branch reconciliation, final CI, tag, and published
  release verification pass.

## Workstreams

### A. Symmetric declared-door traversal

The confirmed remaining gameplay blocker is asymmetric safe shallow-water
handling. Direct-trigger door destinations and egress accept safe water level 1,
but the reverse source, approach reducer, live ticket, and callback path were
dry-only. The resulting one-way cellar region is removed by objective-core
pruning in `lmctf58`.

Required implementation and proof:

- use one shared safe-liquid predicate for water levels 0 and 1 with no lava or
  slime;
- apply it only to `DIRECT_TRIGGER_DOOR` source and approach paths; humans,
  `AUTO_DOOR`, and `BUTTON_DOOR` retain their existing policy;
- seal exact predicted and actual water level and raw water type in every
  command ticket, and poison the persistent reducer on drift or replay;
- authorize a wet trigger callback only for the exact current grounded DIRECT
  ticket, binding, support, guard, frame, substep, and sealed liquid state;
- retain the established air-duration, capsule, fall-boundary, finalization,
  multi-master, death, pause, identity, and replay contracts;
- execute mirrored red/blue real-map wet-source witnesses through first touch;
- pass GNU and Make focused/host builds, strict GCC/Clang checks, sanitizers,
  production module links, and an independent refutation review.

### B. Artifact corpus and runtime acceptance

After A is committed on the final source base:

- build canonical GNU and independent Make modules and acceptors in clean,
  isolated worktrees;
- regenerate `lmctf58` in an isolated `GAME=lmctf` root with maxclients 16 and
  exact production physics;
- require both C readers, Python reader, root-aware linter, and the independent
  ten-controller checker to agree;
- prove representative mirrored cellar seeds remain non-tombstoned and route to
  both flag roots after objective-core pruning;
- rebuild the twenty-map artifact manifest, adapting the cold-matrix runner to
  the new source/module/artifact hashes;
- cold-load every map in a fresh exact process, require one RUNE-ready identity,
  one admitted bot, correct maxclients/physics, no rejection, and clean teardown;
- loop any map-specific failure back through implementation and regeneration.

### C. Production fleet and transactional release bundle

The final fleet implementation must provide:

- `GAME=lmctf` production defaults only in production fleet entry points;
- exact top-twenty rotation with even offsets, nine 5v5 lanes and s08 7v7,
  uniform 900-second runs, and no delayed roster changes;
- an exact one-map authority file per lane so LMCTF cannot rotate a lane to a
  different map before its evidence window completes;
- a lane gate that validates the expected build/map identity, maxclients 16,
  gravity 800, positive RUNE counts, exact roster, expected POV lifecycle, no
  rejection or admission failure, child exit status, and one clean shutdown;
- an exclusive deploy/launcher interlock and an armed stop sentinel throughout
  cutover;
- a release manifest that binds both module aliases, `rune.cfg`, the ordered map
  list, all twenty BSP/RUNE pairs, escape priors, and applicable sidecars;
- same-filesystem staging, fsync, exact hash verification, complete prior-bundle
  backup, all-file promotion while quiescent, negative-claim checks, and exact
  rollback after copy, rename, or post-verification failures;
- no environment-variable override of validators, rollback authority, or lane
  health gates;
- executable adversarial tests for lock races, mixed-module prevention, bad
  manifests, wrong maps/rosters, zero bots, rejection banners, child failures,
  partial promotion, and rollback restoration;
- independent refutation before integration.

### D. Promotion and live proof

Promotion is permitted only after B and C are both green:

1. Arm the stop/watchdog hold without changing the running fleet's scripts.
2. Let the exact owned old waveloop and every child server exit naturally.
3. Prove no sanctioned launcher, server, or production port remains.
4. Back up and transactionally install the final manifest-bound bundle.
5. Re-read every installed hash and run a fresh production cold load.
6. Run two sequential, finite ten-server waves; do not use an infinite loop as
   the acceptance harness.
7. Require all twenty lane records to pass identity, RUNE, roster, duration,
   bot, POV, gameplay-diagnostic, and shutdown gates.
8. Roll back the whole bundle and stop on the first failed promotion or wave
   gate; fix the source or artifact rather than waive the failure.

### E. Player-visible feature closure

Source presence and source-text assertions are not sufficient for the former
roadmap items. On the final module and data set:

- run an actual Yamagi observer recording, retain the normal `.dm2`, and play it
  back successfully rather than accepting a fake four-byte demo fixture;
- execute the production hook controller through an aim-wedge timeout and prove
  exact link shelving, failure-streak/ban updates, and later worthwhile-fire
  admission;
- compare bot movement against the available human trajectory corpus with a
  frozen metric and acceptance band, not only a synthetic flat-floor Pmove;
- pair spectator-visible defender movement with captures-conceded evidence so
  increased activity cannot silently reduce defensive effectiveness;
- run `stallcensus.py` on authenticated observations, generate deterministic
  map-bound `.snag` repair files, include applicable files in the release-bundle
  manifest, and regenerate/reprove every affected RUNE route;
- execute a committed item approach through the real `Touch_Item` chain and
  prove explicit retirement for every non-pickup outcome;
- require every hook-fire record to have exactly one terminal record in the two
  accepted waves, with malformed, incomplete, and global-fatal diagnostics
  failing the wave;
- observe the spectator sound through a real observer/PHS client and retain the
  emitter/channel/origin evidence.

Missing applicable data is a failure: the neutral no-`.snag` loader path is not
completion of snag routing. Each proof needs an executable pass/fail harness or
a manifest-bound production-wave assertion that release CI/final acceptance
actually invokes.

### F. Whole-tree, documentation, and release closure

On the exact promoted source tree:

- rerun both build dialects, all host/focused/integration tests, strict GCC and
  Clang, sanitizers, loader/dependency checks, and repository residue checks;
- re-prove the already-developed POV, hook discipline and diagnostics,
  human-speed movement, defense, item commitment, snag repair, spectator sound,
  objective, combat, and handoff behavior rather than trusting old roadmap
  statuses;
- remove tracked runtime-session cache files and add narrow ignores for known
  generated outputs; stage only a reviewed explicit path list;
- reconcile README, SLIPGATE, RELEASES, LEDGER, ARCHITECTURE, TOOLING, tool
  reference, RUNE contract, and historical migration records with actual final
  behavior;
- commit a concise acceptance manifest containing the final source tree,
  module/configuration/corpus/sidecar hashes, map order, checker results, cold
  matrix, and production wave identities;
- push the final SLIPGATE commit and require the exact SHA's version, Linux,
  Windows x86, and Windows x64 jobs to pass;
- merge SLIPGATE into `main` with a no-fast-forward release merge, assert that
  the merge tree equals SLIPGATE, push, and require the exact main SHA's jobs;
- set an honest SemVer. Use `1.0.0` only after every completion gate above is
  true, then create the matching annotated tag;
- require the tag's five jobs, including Publish, then download the six release
  assets and verify `VERSION` and `SHA256SUMS` independently.

## Dependency graph

```text
cross-platform warning repair (complete)
                    |
                    v
    A: symmetric DIRECT shallow-water repair
                    |
                    v
       independent review + commit + CI
                    |
                    v
      B: lmctf58 regeneration + all20 cold matrix
                    |
                    +-------------------------------+
                                                    |
C: fleet/bundle transaction + review ---------------+
                                                    |
E1: deployable feature data + executable harnesses --+
                    |
                    v
       D + E2: quiescent cutover, two waves,
               and runtime consumer proofs
                    |
                    v
 F: final audits/docs/cleanup/CI/main/tag/release
```

A and C may be developed and reviewed in parallel. Snag-data preparation and
offline feature harnesses in E may also proceed in parallel, but their final
proof consumes the B corpus and D runtime evidence. D cannot begin until B, C,
and the deployable E data are accepted on hash-pinned bytes. F consumes the
exact promoted result and may not substitute older evidence.

## Execution discipline

- Development, generation, and destructive-counterexample testing occur only
  in isolated worktrees or disposable game roots.
- The currently running fleet checkout and installed game remain unchanged
  until the quiescent cutover step.
- Every implementation slice follows write, refute, fix, confirm, freeze.
- A moving diff is never a release candidate. A freeze names every input hash,
  command, result, and known qualification.
- Expected map lifecycle, generated artifacts, and external tool output are
  treated as inputs that must be authenticated, not assumed stable.
- Commits are made at coherent proven boundaries and pushed promptly; commits
  are not backdated or rewritten to manufacture activity.
- A failure loops to the owning predecessor in the dependency graph. It is not
  waived by continuing with downstream deployment or release work.
