# LMCTF BuzzMod project completion plan

This is the maintained local execution plan. It records actual completed work
and the work that remains. Historical plans and stale checklists do not override
this file.

## Fixed scope

- The conversion corpus is all 181 maps in `tools/rune-corpus-maps.txt`.
  `lmctf02` and `lmctf02c` are separate required maps.
- Every one of the 181 maps must receive a RUNE generated from the same frozen
  final source, module, configuration, engine, and BSP set.
- A map list is only an input telling a server or harness which maps to run.
  `topmaps.txt` and its 20 entries have no special completion, promotion,
  sampling, or release meaning. Bots do not have to play every entry to close
  the project.
- Bot quality is judged by behavior during play: movement, activities,
  objectives, traversal, combat decisions, team behavior, and recovery. The
  final score is context, not the acceptance metric.
- Downloadable RUNE or PAK distribution is deferred. It is not part of this
  completion run.
- Focused tests and source inspection are supporting evidence. Player-visible
  behavior also requires execution through the real game and engine.

## Completed work

### RUNE and runtime foundations

- [x] One current RUNE wire and runtime contract.
- [x] Strict GNU C, independent Make C, and Python readers.
- [x] Root-aware lint and authenticated identity checks.
- [x] Transactional RUNE/SNAG generation, validation, cold-load verification,
      and installation.
- [x] Missing or invalid required SNAG data fails closed.
- [x] Real two-process RUNE/SNAG workflow proved with q2ded.

### `lmctf58`

- [x] Map-specific route repair completed.
- [x] All ten required declared-door controller identities retained.
- [x] All six cellar identities retain valid plans.
- [x] Representative cellar routes reach both flag roots.
- [x] Objective-core remains authoritative; completion does not preserve
      unreachable tombstones or disable pruning.
- [x] RUNE generated and accepted.
- [x] Zero-repair SNAG generated and installed.
- [x] Both C readers, Python decoding, lint, semantic checks, strict compilers,
      sanitizers, and a separate cold load passed.
- [x] Exact server loaded the pair, published full fields, admitted bots, and
      produced live commands and movement.

`lmctf58` is complete. A later corpus-wide regeneration from the final frozen
action contract is part of the 181-map corpus run, not unfinished map repair.

### Traversal features

- [x] D_SWIM production generation, publication, ownership, execution,
      recovery, and real-game proof.
- [x] Authenticated rocket-jump generation, projectile identity, execution,
      recovery, and real-game proof.
- [x] D_DROP production generation, publication, continuous live reproof,
      target fanout, recovery, release, and recorded real-game proof.
- [ ] D_HOOK production integration and final real-game proof.

### Supporting behavior and harness work

- [x] RUNE/SNAG harness failures can no longer silently pass empty matches.
- [x] Demo and behavior analyzers return failure on parser, scalar, render,
      skip, or leak-audit failure.
- [x] Serverrecord entity disappearance is handled instead of inventing frozen
      player positions.
- [x] Role, movement, combat, objective, hook, and coordination reporting has
      executable coverage.
- [x] Both Make dialects and deslop gates were green at completed feature
      checkpoints.

## Active work: finish D_HOOK

The current implementation has the pure coordinator, publication/oracle proof,
game adapter, engine-event receipts, lifecycle orphaning, generator discovery,
and production call-chain integration.

Current D_HOOK checkpoint on 2026-08-21:

- [x] Production call-chain integration is implemented in the active isolated
      writer at `/tmp/lmctf6-door-hook-final.uiYb2a`.
- [x] D_DROP and D_HOOK guard dominance is action-specific.
- [x] Every fourth 25 ms D_HOOK boundary is consumed inside `Think_Emit`, so no
      pending command crosses the entity attachment pass.
- [x] Retained recovery preserves and approves the replacement command in the
      same slot; an executable unsafe-clear regression proves the first 25 ms
      recovery step without premature release.
- [x] COMPLETE and SAFE_STOPPED are handled before requested-release dispatch.
- [x] Offhand readiness is checked at selection, Begin, and irreversible fire.
- [x] Immediate-trace bolt lifetime is safe: the exact bolt is published before
      synchronous touch and no stale pointer survives an abort.
- [x] Ordinary hooks bypass the compound adapter unchanged.
- [x] Disconnect ordering is statically proved: cancel delayed uses, action
      orphan while identities are live, physical hook removal, bolt eviction,
      entity unlink, then final disconnect.
- [x] Guard lifecycle tests now link the real production D_HOOK orphan wrapper
      instead of reimplementing it in the test fixture.
- [x] GNU and Make focused live/game/guard suites pass.
- [x] Strict GCC and Clang ASan+UBSan focused suites pass without suppression.
- [x] A clean production module build includes the lifecycle unit and `ldd -r`
      is clean.
- [x] Comment and independent semantic reviews have no accepted open finding in
      the integrated source slice.
- [x] Complete the GNU and Make full host gates at frozen commit `ad520fe` in
      separate detached `/tmp` worktrees. Both complete `host-test` commands
      exited zero on 2026-08-21, including the engine snapshot and deslop gates.
      The POV fixture root fix remains committed as `5a0aff1`; its complete
      17-test suite passes in the full gates.
- [x] Complete the default GNU and Make production module builds at that exact
      frozen commit. Both builds and `ldd -r` checks exited zero, and both
      detached worktrees had no tracked diff. The GNU module SHA-256 is
      `7888f3cc685643a0ede1c834cd2984a533c009d285259d0e5bf28fa75c5e0708`.
      The Make module SHA-256 is
      `0ac558e23483cf92586ab2e04688286e7071afdf4b68347fe6821114d2cc0e38`.
- [x] Commit the coherent D_HOOK integration. The integration is frozen at
      `ad520fe` on top of the POV fixture and Windows source-graph fixes.
- [x] Enable D_HOOK production generation and runtime admission as an admitted,
      runtime-supported, planless action. The canonical generated contracts,
      reviewed digests, codec fixtures, loader gates, and dedicated validated
      D_HOOK publication path pass under both GNU and Make at `4323b3e`.
- [x] Add a bounded authenticated `sv sg compoundhook <link>` observation seam
      that records the exact
      approach, activation, LINKED, ATTACHED, PULL, release or recovery,
      terminal outcome, and ownership-retirement lifecycle required by the
      real-engine acceptance gate. It stages only an exact published link,
      uses production Begin and real engine events, and passed its focused GNU
      and Make tests at `4323b3e`.
- [x] Repair the full-gate guard-game fixture linkage for the new diagnostic
      callback. The corrected integrated enablement commit is `30a2dc6`; the
      exact guard-game target and deslop gate pass under both GNU and Make.
- [x] Replace the stale full-suite dormancy assertion with the admitted runtime
      contract: D_SWIM remains disabled and D_HOOK is runtime-ready. The exact
      compound target and deslop gate pass under both GNU and Make at `58321cc`.
- [x] Pass the complete GNU and Make host suites, clean production builds,
      `ldd -r`, and tracked-tree checks at corrected integrated enablement
      commit `58321cc`. Both complete `host-test` commands exited zero and both
      detached worktrees remained tracked-clean. The GNU module SHA-256 is
      `92c96863b8911e4771471b7bb21e3491ce9fa5247d92f890a4b94762431ae4e4`
      (11,532,120 bytes); the Make module SHA-256 is
      `12771757cb28382ac869056b3633f1d25a11d9a28480880aa3f40ba18eef8ec6`
      (11,839,488 bytes).
- [x] Generate, independently accept, cold-load, and install an exact-module
      `lmctf53` pair in the isolated `lmctf-dhook-live-58321cc` game tree. The
      RUNE SHA-256 is
      `57815de366b5567f78de0bcf60259a64ce634477cb37cb4a7a7202a02e474c32`;
      the generation correctly reported zero authenticated mechanisms and
      therefore zero D_HOOK publications. This is a valid pair gate, not the
      required action witness.
- [x] Generate, independently accept, cold-load, and install an exact-module
      `bmap6` pair. Its RUNE SHA-256 is
      `56262a4be06e99607c2d55506a933636a7dbe6cc465339ad37dc9b35f99dce89`.
      The pair contains 143 water seeds and a 68-node/89-edge catalog, but its
      auto-door/platform records do not satisfy the strict preopen world
      resolver, so it correctly published zero D_HOOK links.
- [x] Reject `lmctf02c` as the action witness from live preflight before its
      expensive link proof: it has eight automatic doors but no generator-
      reachable water seeds, so D_HOOK is impossible there. No pair was
      installed and the isolated server was terminated.
- [x] Reject `lmctf08` as the action witness from static/live preflight before
      its expensive link proof: it has 492 reachable water seeds, but every
      water leaf lies outside D_HOOK's discovery envelope around both doors.
      No pair was installed and the isolated server was terminated.
- [x] Generate, independently accept, cold-load, and install an exact-module
      `lmctf32` pair. Its RUNE SHA-256 is
      `1dc963734bfa5cf6b56457ef67160fafe3ebf219471fb6a48e7ef81b1894c437`.
      All four strict preopen mechanisms resolve, but no incoming water seed
      lies inside their live discovery envelopes, so oracle trials and D_HOOK
      publications are correctly zero.
- [x] Generate, independently accept, cold-load, and install the `xmap06`
      pair. Generation proved 12 D_HOOK trials across ten strict mechanisms
      and published links 89630-89632. The accepted RUNE SHA-256 is
      `16e754c2899f6e0aa2072bb26740832a91206428b4acb53c249fb90b91537e26`;
      its matching SNAG SHA-256 is
      `3ef88df4334477ed4605d48fe7f3d19151c6b65a07880c9af4963199722cbe3b`.
- [x] Diagnose and repair the live consumer's hidden 100 ms touch-frame
      assumption. The published `xmap06` timing shape is valid, but D_HOOK
      interpreted raw mover-relative TOP time as transaction-relative TOP
      time. Commit `23ae46d` now uses `touch_frame_end_ms + suffix_start_ms`,
      matching D_SWIM, D_DROP, and the published timing contract. Focused and
      complete host suites pass under both GNUmakefile and Makefile at this
      commit. Both production modules pass `ldd -r`: the GNU module SHA-256 is
      `e382446d9e84a9682fc5e704291c56202fdbc27693a4912bb5b33d6bc97ed65c`
      (11,531,800 bytes), and the Make module SHA-256 is
      `880f0ca877316be78b43c694dfb40d9815c33cbf215520544448821f9d901278`.
- [x] Exercise all three admitted `xmap06` links in the real engine. Each now
      reaches authenticated Begin, touch, and activation, then dies in the
      same under-map water hazard at TOP before bolt linkage. Ownership is
      orphaned and cleared correctly. These witnesses are retained negative
      live evidence, not a completed D_HOOK lifecycle.
- [x] Generate, independently accept, cold-load, and install a fresh `xmap08`
      pair from exact module
      `e382446d9e84a9682fc5e704291c56202fdbc27693a4912bb5b33d6bc97ed65c`.
      Its RUNE SHA-256 is
      `fae573db47df2e448bf2d5f6a3dfc0310119948f504c560c5c81d8330b34d85a`;
      its matching SNAG SHA-256 is
      `b857de6e3327edaafb55ab8c5d47732a3f94f541a9e31f979e1e086979142646`.
      The pair contains 2,558 seeds, 99,787 links, 71 nodes, and 220 plans,
      but the live strict preopen resolver admits zero mechanisms, so it
      correctly publishes zero D_HOOK links and is not the required witness.
- [ ] Preflight the remaining water/door candidates through the live strict
      resolver, then generate the first candidate with an admitted mechanism
      inside the D_HOOK discovery envelope.
      The validated GDB probe reproduces `xmap06`'s ten mechanisms after live
      map initialization. It rejected `xmap28` and `smap46` at zero, while
      `xmap05` and `smap10` each expose ten. `lmctf11` exposes four.
- [x] Generate, independently accept, cold-load, and install the fresh
      `xmap05` pair. Its RUNE SHA-256 is
      `82f6fe1d0046a3567dab32e0d27de4d1ef0c13a77360943244e7ebc05b99613f`;
      its matching SNAG SHA-256 is
      `a3249243343f1613fd9b2dba523e997c49beb4d5f00994d6764dd5e73d7b68ba`.
      Its ten mechanisms produced 425 contact-discovery attempts, but every
      exact approach replay rejected before a hook trial, so it correctly
      published zero D_HOOK links and is not the required witness.
- [x] Generate, independently accept, cold-load, and install the fresh
      `lmctf11` pair. Its RUNE SHA-256 is
      `19befe3575e23aa2b355adfcb26a7bc0f6c13cb6e23d133c9c6649afae364e4a`;
      its matching SNAG SHA-256 is
      `dc0bc6d45df81fc7368c1e4892738422531cf5edb91b10a5612d0ca5e6ca370f`.
      The pair contains 1,525 seeds and 38,523 retained links. Its four strict
      mechanisms produced 48 hook proof trials, but no proof or publication:
      contact discovery rejected 238 approaches with reason 67 and all 48
      proof trials rejected with reason 69. It is a valid zero-witness pair.
- [x] Generate, independently accept, cold-load, and install the fresh
      `smap10` pair. Its RUNE SHA-256 is
      `2895a12bf15c2f9f17ea52bee1892549661e55585d3e943f189c3536d032fee1`;
      its matching SNAG SHA-256 is
      `4cb5de75c418185f8a0e88c5b471588ed86209ff00e225ba220184344e780621`.
      The pair contains 2,842 seeds and 90,144 retained links. Its ten strict
      mechanisms produced 538 contact-discovery attempts, all rejected with
      reason 67 before a hook proof trial, so it is a valid zero-witness pair.
- [ ] Generate a real admitted D_HOOK link with the corrected committed module.
- [ ] Independently accept and cold-load that exact RUNE/SNAG pair.
- [ ] Record real execution through approach, activation, hook fire, exact bolt
      link, attach, pull, release or bounded recovery, settlement, ownership
      retirement, and ordinary route continuation.

D_HOOK is complete only after the recorded real-engine lifecycle passes. A
focused reducer test, module link, or generated witness alone is insufficient.

## Final source freeze

After D_HOOK is complete:

1. Integrate every accepted feature commit into the active `slipgate` source.
2. Remove temporary probes, diagnostics, stale artifacts, and obsolete dormant
   assertions without deleting retained acceptance evidence.
3. Regenerate all generated contracts from the final source.
4. Run complete GNUmakefile and Makefile host suites under GCC and Clang.
5. Run strict warnings, ASan, UBSan, dependency checks, exported `GetGameAPI`,
   module loading, `ldd -r`, diff checks, and deslop.
6. Build both production module aliases and prove their intended identity.
7. Freeze the exact source commit, module hashes, configuration, engine,
   readers, linter, semantic checks, and 181-map manifest.

Any source or generated-contract change after this point invalidates the module
freeze and every RUNE produced from it.

## Generate and validate all 181 RUNEs

1. Create a read-only generation snapshot from the frozen final candidate.
2. Include the exact engine, module aliases, production physics/configuration,
   181 BSPs, map manifest, readers, linter, and semantic checks.
3. Use durable isolated output roots and disjoint worker ports.
4. Run the 181-map controller with bounded parallel workers and per-map
   timeouts.
5. For every map require:
   - a newly generated RUNE;
   - the required matching SNAG declaration, including authenticated zero
     repairs where applicable;
   - two valid objective roots;
   - clean generator shutdown;
   - GNU C reader acceptance;
   - independent Make C reader acceptance;
   - Python reader acceptance;
   - root-aware lint acceptance;
   - every applicable map-specific semantic check;
   - a fresh-process cold load with an admitted bot.
6. Require exactly 181 PASS results. GEN_FAIL, TIMEOUT, LINT_FAIL, reader
   disagreement, semantic failure, or cold-load failure is not completion.
7. Repair failures at their owning source, data, or tool boundary.
8. If the frozen source changes, rebuild and regenerate the affected corpus so
   every accepted artifact shares one final identity.
9. Freeze the final 181-artifact manifest and all evidence hashes.

## Real-match behavioral validation

After the final module and applicable RUNEs exist, run ordinary real matches.
The supplied map list is merely the schedule for that run.

Observe and retain evidence for:

- continuous useful movement and route progress;
- objective pursuit and actual flag interactions;
- appropriate door, swim, drop, hook, rocket-jump, lift, fall, water, and
  teleport traversal;
- combat activity, weapon use, aiming texture, splash safety, and earned
  perception;
- carrier, escort, recover, intercept, and defender behavior;
- item pursuit and commitment retirement;
- bounded recovery from failed traversal and geometry;
- one valid terminal lifecycle for every physical hook;
- POV recording/playback and spectator sound attribution;
- clean bot rosters, server operation, and shutdown.

Do not substitute scores, wins, captures, a full pass over a named map list, or
parser output for observed behavior. When a match exposes a defect, repair the
owning implementation, rerun its focused gates, rebuild the candidate, and
repeat the invalidated live evidence.

## Final integration and release

1. Reconcile documentation with the behavior and tools that actually ship.
2. Ensure the worktree and explicit commit set contain no accidental runtime
   outputs or unrelated files.
3. Commit and push the final coherent `slipgate` milestones.
4. Require exact-SHA Linux and Windows CI and both Make dialect/compiler
   matrices.
5. Merge the proven tree coherently into `main` and require the same exact-SHA
   CI there.
6. Set an honest version.
7. Create the final tag and publish the intended release assets.
8. Download the release and verify its hashes and version identity.
9. Record the final source, module, 181-artifact corpus, real-match evidence,
   CI, tag, and release identities.

Downloadable RUNE/PAK packaging remains deferred until explicitly resumed.

## Current critical path

```text
finish and live-prove D_HOOK
  -> integrate and freeze the final source/module
  -> generate and validate all 181 RUNEs
  -> run ordinary real matches and judge behavior during play
  -> repair any exposed defects and repeat invalidated evidence
  -> final builds, CI, documentation, tag, publication, and hash audit
```

## Current completion checklist

- [x] RUNE/SNAG producer, verifier, cold-load, and install transaction.
- [x] `lmctf58` map repair and accepted runtime pair.
- [x] D_SWIM.
- [x] Rocket jump.
- [x] D_DROP.
- [ ] D_HOOK production integration and recorded real-engine completion.
- [ ] Final source and module freeze.
- [ ] Exactly 181 newly generated and fully accepted RUNEs.
- [ ] Final real-match behavioral validation using ordinary map-list inputs.
- [ ] Defects exposed by final matches resolved and revalidated.
- [ ] Final dual-dialect/compiler/platform gates and clean repository state.
- [ ] Documentation, version, branch integration, tag, release, and hash audit.
