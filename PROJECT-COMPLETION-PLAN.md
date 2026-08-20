# LMCTF BuzzMod completion plan

This is the current execution plan. Git history records completed experiments
and rejected approaches. This file records only the required end state, current
facts, open work, and the order in which to finish it.

## Required end state

The project is complete when one frozen revision satisfies all of these gates:

1. The game module builds on Linux x86_64, Windows x86, and Windows x64.
2. `GNUmakefile` and `Makefile` pass their host suites with GCC and Clang.
3. Bots play competent LMCTF through the production controller. They move with
   purpose, initiate and finish steals, use useful hook routes, fight without
   hidden information, coordinate roles, protect carriers, and defend lanes.
4. Every player-visible bot change has an executable policy or live-path test.
   Match evidence confirms the result but never substitutes for implementation.
5. The authoritative corpus contains 181 distinct maps, including `lmctf02`
   and `lmctf02c`. Every map has a newly accepted RUNE from the final module.
6. Each RUNE passes both native readers, the Python reader, lint, applicable
   semantic checks, and a separate fresh-process load.
7. `lmctf58` retains all ten required declared-door controllers and routes from
   each cellar side to both flags.
8. One manifest binds the modules, pak, configuration, top-20 map order, all
   181 BSP/RUNE pairs, and every applicable sidecar.
9. Installation is quiescent and transactional. Any failed promotion restores
   the exact previous managed bundle without touching unmanaged files.
10. Ten persistent q2ded processes run the top 20 in native map rotation. s01
    starts at offset 0 through s10 at offset 9. Nine servers are 5v5; s08 is
    7v7. Transition 20 returns each process to its first map without changing
    PID identity.
11. Each map residence binds the process, module, BSP, RUNE, roster, activity,
    server recording, and s03 POV lifecycle.
12. `slipgate`, `main`, CI, the tag, published assets, installed bundle, and
    release checksums all identify the same frozen tree and payload.

## Current facts

The following work is in the current branch:

- one RUNE wire/runtime contract;
- a 181-map corpus authority and controller;
- independent GNU and Make native readers plus the Python reader and linter;
- bounded fresh-process RUNE loading;
- ordered non-random map-list parsing and native sequential wrap;
- all ten required `lmctf58` declared-door controllers;
- strict role-telemetry consumers using `seed`, `goal`, `sgoal`, and `spd`;
- source-bound snag evidence and runtime sidecar checks;
- direct bot improvements for quiet-defense patrols and blocked close-range
  attacker route retention;
- a repository-wide source-comment audit and removal of implementation diaries.

The following work is not complete in this branch:

- bot behavior still wastes movement and produces too few successful steals;
- the reviewed persistent-fleet stack has not been integrated here;
- the release-bundle transaction has not been integrated here;
- no final-source 181-map run has produced 181 accepted artifacts;
- no final bundle has been installed or exercised through a full top-20 cycle;
- final cross-platform CI, tag, publication, and downloaded-asset verification
  have not run.

## Work order

### 1. Improve the bots

This is controller work, not a reporting exercise. Work directly in the live
decision and movement paths, add a focused executable test, and keep changes
that improve actual play.

1. Remove wasted movement.
   - Stop quiet defenders from micro-pacing or reversing patrol legs.
   - Remove attacker orbiting and far-field grind without weakening touch,
     collision, pit, or route safety.
   - Keep contact and objective urgency above movement texture.
2. Increase steals and captures.
   - Produce more purposeful approaches to the enemy stand.
   - Preserve proved route progress when a close direct trace is blocked.
   - Complete physical `ctf_flagtouch`; never synthesize score state.
   - Improve post-pickup escape and carrier support.
3. Use the hook for route progress.
   - Prefer proved rides that beat ground travel.
   - Retire only the failed link after an aim or lifecycle failure.
   - Preserve fire, attach, pull, release, and fallback authority.
4. Improve coordinated play.
   - Keep role assignment stable per bot identity.
   - Send useful pressure instead of isolated attackers.
   - Keep escorts near enough to screen without blocking the carrier.
   - Defenders cover approaches and react to public flag state.
5. Preserve combat quality.
   - Retain reaction delay, skill-scaled error, overshoot and settle, weapon
     commitment, splash safety, and visible or audible target admission.
   - Change only a demonstrated weak combat behavior.
6. Finish item, observer, and sound behavior through the real game paths.

For each change:

- reproduce the bad decision at the narrowest callable seam;
- change production C code;
- test that seam with executable state, not comment matching;
- build both Make dialects;
- keep or strike the change from observed play;
- do not turn measurement tooling into a milestone.

### 2. Integrate the persistent fleet

Bring the reviewed fleet work onto the current branch, then re-review the
combined tree. The integrated runner must:

- derive ten cyclic lists from the exact `tools/topmaps.txt` bytes;
- launch all ten servers behind one barrier with no stagger;
- authenticate lock, stop sentinel, ports, process generations, engine, module,
  configuration, map lists, BSPs, RUNEs, and runner bytes;
- keep engine and POV child images immutable after authentication;
- kill owned children if the manager dies;
- record complete consecutive server demos per residence;
- emit one canonical residence receipt and one append-only evidence ledger;
- verify a stopped evidence set without trusting filenames or caller claims.

Run 21 short fixture residences per lane to prove all 20 entries plus wrap. The
real production acceptance later runs the full 15-minute residences.

### 3. Integrate the release bundle transaction

Rebase the bundle work onto the integrated fleet API. The bundle tool must:

- accept only a clean authenticated Git source root;
- bind the exact module, controller result set, 181 BSP/RUNE pairs, applicable
  sidecars, pak, configuration, rotated lists, and fleet runner;
- create deterministic archive and manifest bytes;
- keep provenance outside the installed runtime payload;
- stage and promote managed paths without changing unmanaged files;
- publish durable intent before mutation;
- recover after every tested crash boundary;
- preserve the prior managed state until final verification;
- support explicit rollback and reject foreign or ambiguous recovery state.

The tag workflow must publish the exact accepted bundle. It may not rebuild or
substitute one after production acceptance.

### 4. Freeze the source candidate

Merge the bot, RUNE, fleet, bundle, documentation, version, and workflow work
into one candidate. Then run:

- both Make dialects with GCC and Clang;
- full host tests;
- strict diagnostics and sanitizers for the focused reducers;
- module link, export, and load checks;
- Windows x86 and x64 CI;
- an independent diff review.

Any source correction returns to this step. Freeze the commit, modules, readers,
generator, engine, controller, semantic checkers, configs, and BSP inputs only
after every gate passes.

### 5. Generate and accept all 181 RUNEs

1. Generate every map with bounded workers and durable per-map logs.
2. Run both native readers, Python decode, lint, applicable semantics, and a
   fresh-process load for every artifact.
3. Repair the owning source, tool, or input for each failure.
4. Rebuild and regenerate every artifact invalidated by a source change.
5. Stop only at exactly 181 PASS with no timeout, waiver, stale row, or missing
   result.

Freeze the accepted corpus manifest after the final successful run.

### 6. Build data and bundle

- Derive each snag sidecar only from authenticated final residence evidence.
- Re-run affected RUNE checks and cold loads.
- Build the deterministic release bundle and verify it independently.
- Exercise install, interruption, corruption, collision, restart recovery, and
  rollback in a disposable game root.
- Store the accepted archive and manifest under content hashes outside Git.

### 7. Cut over and accept production

1. Hold the old launch authority and stop only its owned processes.
2. Prove port and process quiescence.
3. Install and verify the accepted managed bundle transactionally.
4. Cold-load the installed top-20 artifacts.
5. Start the ten production processes once, without launcher-side delays.
6. Observe 20 native transitions and the return to entry zero under unchanged
   PID identity.
7. Validate every residence receipt, recording, roster, activity counter, RUNE,
   POV close and reopen, and evidence-ledger link.
8. Inspect actual bot play. Any failed bot, map, bundle, or fleet condition
   returns to its owning step. Nothing is waived.

### 8. Publish

- Require exact-SHA CI on `slipgate`.
- Merge the identical frozen tree to `main` and require exact-SHA CI there.
- Tag the main merge.
- Publish the previously accepted bundle and declared client assets.
- Download every release asset and verify `VERSION`, sizes, SHA-256 values, and
  equality with the accepted installed bundle.

## Invalidation rules

- A source change invalidates modules and generated RUNEs.
- A RUNE change invalidates its semantics, cold load, snag, bundle entry, and
  residence evidence.
- A fleet change invalidates fleet fixtures and production-cycle evidence.
- A config or top-20 change invalidates affected residence evidence.
- A bundle-tool change invalidates transaction tests and accepted bundle bytes.
- A tracked documentation change must land before source freeze because it
  changes the release revision.
- A failed check is a failure. Expected failures, partial runs, and old evidence
  never count as PASS.

## Completion checklist

- [x] One RUNE contract.
- [x] 181-map authority includes `lmctf02` and `lmctf02c`.
- [x] Dual native readers, Python reader, lint, semantics, and cold-load gates.
- [x] Ordered sequential map-list behavior.
- [x] All ten `lmctf58` declared-door controllers.
- [x] Production telemetry consumers recognize the current schema.
- [x] Initial direct bot movement improvements.
- [x] Source and tool commentary cleanup.
- [ ] Remaining bot movement, steal, hook, coordination, and presentation work.
- [ ] Persistent fleet integrated and independently reviewed.
- [ ] Release bundle transaction integrated and independently reviewed.
- [ ] Final source candidate frozen and cross-platform green.
- [ ] Exactly 181 final RUNEs accepted.
- [ ] Final snag data and deterministic bundle accepted.
- [ ] Transactional production cutover complete.
- [ ] Ten persistent servers complete the top-20 cycle and wrap.
- [ ] Final bot play accepted on the installed build.
- [ ] `slipgate` and `main` exact-SHA CI green.
- [ ] Tag, published assets, and downloaded checksums verified.
