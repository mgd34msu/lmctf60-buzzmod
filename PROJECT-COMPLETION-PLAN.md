# LMCTF BuzzMod project completion plan

This is the canonical execution plan. It records what is done, what remains,
and what counts as completion. Historical plans and stale checklists do not
override it.

## Fixed scope

- Generate a new RUNE for every map in `tools/rune-corpus-maps.txt`. The corpus
  contains 175 maps. When a numbered map has one or more suffixed variants, the
  variants are canonical and the unsuffixed base is out of scope. Keep every
  listed variant when a map has more than one, such as `lmctf02a` and
  `lmctf02c`.
- Generate all 175 RUNEs from one frozen source, module, configuration, engine,
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

`lmctf58` is complete. Its RUNE will be regenerated with the other 174 maps
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
- The canonical D_HOOK source is merged into both working branches.

### Integration status

- [x] Rebase the complete D_HOOK stack onto the current `slipgate` source
      without dropping D_SWIM, D_DROP, or rocket-jump support.
- [x] Unify the generated action contract for actions 7, 9, 10, and 11.
- [x] Pass contract, action, codec, and source-size checks after the rebase.
- [x] Pass the full GNU and Make host and module gates, `ldd -r`, and deslop
      with zero findings on the source-identical candidate. Its tracked
      worktree was clean.
- [x] Merge and push the proven fix stack through `slipgate` to `main`. Local
      and remote `slipgate` and `main` compare as zero ahead and zero behind.

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

- [x] The prior frozen source passed exact-source `slipgate` and `main` CI on
      Windows x86 and x64 and on Linux with GNUmakefile and Makefile under GCC
      and Clang. Both CI runs are green and uploaded their intended artifacts.
- [x] The chosen GNU module has SHA-256
      `2d9cf6029586cf07918617ab9d8f459356787dd70cf63a6a153fc6af35ec52d5`.
      The snapshot uses those exact bytes for both `game/game.so` and
      `game/gamex86_64.so`.
- [x] The prior immutable input snapshot contained all 181 then-required BSPs
      and had no writable file, writable directory, or symlink.
- [x] The input-manifest SHA-256 is
      `422144842721e6bda7e1433d0edb0b17b464dc6ac0c074ef49dd292dff58d0a5`.
- [x] All 53 controller tests passed. The jobs=10 dry-run assigned all 181 maps
      and produced fingerprint
      `01b84dfb9203909293af4483d335af12bb67842ada4dc09c57ff77a7af3a2221`.
- [x] The prior freeze reports, manifest, and supporting logs passed their
      integrity checks. Their temporary run directory is no longer present.
- [x] The corpus controller now treats the exact post-write, same-map missing
      SNAG and field-setup sequence as deferred publication. It then stops the
      generator, runs the artifact gates, creates the authenticated zero-repair
      SNAG, and requires a fresh cold-load RUNE-ready result. Missing or invalid
      SNAG data during cold load still fails closed.
- [x] The controller repair is canonical. Its GNU and Make controller suites
      each passed all 53 tests. A frozen-input `lmctf14` run
      completed generation, dual-reader acceptance, SNAG creation, and fresh
      cold load with a terminal PASS.
- [x] Teleporter staging and objective-core diagnostics are canonical.
      `lmctf02a` completed generation, dual-reader acceptance,
      semantic gates, authenticated SNAG creation, and fresh cold load with a
      terminal PASS.
- [x] Declared-door sibling egress replay is canonical. Fresh
      accepted pairs for both `lmctf03` and `mactf01` reached authenticated
      SNAG-ready and RUNE-ready during cold load.
- [x] The separate, fingerprinted cold-load timeout is canonical.
      The GNU and Make controller suites each passed all 55 tests. A sealed
      `lmctf03` artifact passed direct cold load with the 10-second startup
      delay and a 420-second cold-load budget.
- [x] Exact-source CI passed on both `slipgate` and `main`. Both
      runs passed the Windows x86 and x64 jobs, Linux, all four GCC and Clang
      Make-dialect jobs, and the version check.
- [x] The `lmctf04` objective-root source repair and its source-size budget are
      canonical.
      Both flag roots bind, and the mutual core retains 979 seeds and 26,373
      links. The isolated end-to-end run passed both readers, Python, lint,
      authenticated SNAG creation, and fresh cold runtime-ready.
- [x] The full GNU and Make gates passed for the `lmctf04` repair. Exact-commit
      CI then passed on both `slipgate` and `main` across the full Windows,
      Linux, compiler, and Make-dialect matrix.
- [x] Permanent, untriggered `func_wall` geometry is canonical immutable route
      support. A source-identical `smap14` run generated 827 seeds and 31,524
      links, retained one shared objective core, and passed both readers, lint,
      and fresh cold load.

The prior frozen game module and its recorded evidence remain proven. They are no
longer the final combined source and tool freeze. The controller changed after
that freeze, and the active graph repairs will change game source. The old
snapshot, fingerprint, and generated RUNEs cannot authorize the final corpus.

- [ ] Finish and integrate the remaining graph repairs.
- [ ] Pass exact-commit CI for the final combined source and tools on both
      `slipgate` and `main`.
- [ ] Rebuild the final module, create a new immutable 175-map input snapshot,
      and record its exact manifest and controller fingerprint.

## Active work: repair, refreeze, and regenerate all 175 RUNEs

The prior 181-map run is sealed from acceptance and retained only as diagnostic
evidence. It completed with all 181 terminal results:
zero PASS, 31 `GEN_FAIL`, and 150 `TIMEOUT`. No worker remains active. The final
summary, every referenced result identity, each map identity, and each terminal
classification passed the integrity check. No artifact from this run belongs
to the accepted corpus. The project still requires a new freeze and a full
175-map restart from an empty run root.

Most recorded timeouts completed RUNE generation and were waiting for a
same-process RUNE-ready line after the runtime correctly rejected the missing
SNAG declaration. The controller repair removes that misclassification. At
the old freeze, 32 maps wrote no artifact. Twenty-four stopped at a graph
invariant, and eight timed out during generation. One of those 32 was the now
excluded `lmctf05` base map. The corrected corpus contains 31 of those old
no-artifact results. Later isolated evidence already supersedes some of them,
including `lmctf02a` and `lmctf04`.

### Repair tracks

- [x] Complete the `lmctf05b` repair. The canonical source and budget changes
      generalize `START_OPEN` vertical `func_door`
      carrier support through authenticated `RL_LIFT`. The unsuffixed
      `lmctf05` map is not an acceptance target. The repair closes the
      four-lift route core, admits exact ascending and descending carrier
      identities, fixes the former `bad-closure` plan failure, and prevents a
      multi-stage main-trigger phase-order bypass. A new post-repair
      source-identical run passed generation, both C readers, Python, lint,
      semantic gates, SNAG creation, and fresh-process cold load with 1,809
      seeds, 38,921 links, and 6 plans. Its module SHA-256 is
      `59241da9a3bc0a29d280a5ef9825de44f84febea385e94349ed2f53cbfe7ec09`;
      result SHA-256 is
      `bfb49cab7855e1cb1c3e6d397dc5ca852929d8e53e460a98680a17e0b7aff4a1`;
      and RUNE SHA-256 is
      `6c3f31c529f522326cd693318ed7198476744d06f3dddbc1a129b1199870b74b`.
      Exact-source CI then exposed one Windows-only C4701 warning in the carrier
      resolver. The repair initializes that helper
      output before its mutually exclusive capture calls; both focused harnesses
      and both full local builds pass. Replacement CI confirmed
      both Windows builds and the packaged Linux module, then failed all four
      host jobs only because the overflow regression's three Makefile lines had
      not been added to the source-size budget. The canonical source corrects
      both exact budgets, and the deslop audit now passes with zero findings.
      Exact-source CI is fully green on both `slipgate` and `main`, including
      both Windows builds, the packaged Linux module, all
      compiler jobs, both Make dialects, and the version check.
- [ ] Repair the remaining maps whose objective route core is closed.
      `smap39` is accepted in the canonical source.
      The source-identical live run passed generation, both readers, semantic
      gates, and fresh-process cold load with 998 seeds and 20,805 links. Its
      result SHA-256 is
      `67cc06ba71aa8237343e5dc9b18069fa476f1e4d9f48fda2f5b6171e3ccefca7`;
      its RUNE SHA-256 is
      `7cc6406e0f56d1d2e9441848a9826368c85ad0c0cb5594a3eca6b0b42f71eda6`.
- [x] The `smap28` fixed-push repair is accepted. The integrated series
      adds the authenticated `RL_PUSH` contract, stock speed-85 flight proof,
      serialized impulse identity, pointer-free live reducer, game adapter, and
      live debug probe. Independent review found and repaired two handoff
      defects: ordinary RUN could publish a
      nearby source that PUSH rejected, and PUSH trusted cached playerstate
      without proving an at-rest authoritative body on immutable support. The
      repaired exact source passed the full Make suite, focused GNU and
      Make reducers, RUN-handoff and compound-transition regressions, module
      link checking, and the zero-finding deslop audit. Its fresh controller run
      passed generation, both C readers, Python, lint, authenticated SNAG
      creation, and cold load with 974 seeds, 15,244 links, 119 mechanism nodes,
      and 4 PUSH plans. Both flags reach the same 936-seed objective core from
      roots 11 and 12. The exact repaired module SHA-256 is
      `4024f94f558fdcb5630d054aa744a278e6e84d8ab3e064ed9db2a0af5e5bfeb6`;
      result SHA-256 is
      `a811c30dc42d66b8102e52fb0b060f698d0977c7c82896bd3415076f1dcde2a0`;
      and the byte-identical RUNE SHA-256 is
      `c6a5bd441962aedbf6b85ef856185786b4c5f4f79d081496004670cb7923b947`.
      A fresh real-engine probe on that module logged authoritative rest at
      begin, trigger touch, staged flight, and completion after 5,775 ms with
      the bot alive inside the destination envelope. Its captured-session
      SHA-256 is
      `e790bf96f5faeaf01543ab1b77187175b95848e083161f42daa4710bb8a1c402`.
      Published exact-source CI passed Windows x86 and x64 and the
      packaged Linux build, but both Clang host jobs rejected a near-`INT_MAX`
      integer bound that rounded upward when compared with a float. The repair
      keeps that price and bound in `double`. The formerly failing
      focused PUSH build now passes under Clang with both Make dialects, its
      runtime test passes, and the deslop audit remains at zero findings.
      Replacement exact-source CI is fully green on `slipgate`
      and `main`, including both Windows builds, packaged Linux, all GCC and
      Clang host jobs, both Make dialects, and the version check.
- [x] Repair the missing central transition in `lmctf07`. The four exact
      health-1 buttons each authenticate one firing side on the X axis. Y is
      ambiguous and rejected; Z is the train motion axis and excluded. A real
      smoke with separate firing, entry, and open-pose egress proofs published
      four shoot-train links and closed red-to-blue travel, but blue-to-red
      remained open. Pre-ranking alternate exits by Euclidean distance
      regressed to zero links and was refuted. An exhaustive exact scan then
      found no one-segment entry-to-objective exit. The three-leg controller
      serializes a proved crossing checkpoint and passes focused reducer,
      catalog, plan, binding, game, contract, codec, generator, and object
      builds. Real smokes prove 6 or 10 exact crossings per button and as many
      as 728 supported far-side handoffs, but none has a next leg into the
      missing objective component. The controller is proving authenticated
      base-to-center travel in both directions; neither center side can shoot
      the outer button to reverse the same transaction. Each radiator also has
      two low touch buttons and one high touch button. The first low-to-high
      composition was refuted because its entry-by-crossing fan was quadratic
      and had not returned after more than five minutes. It was reverted. The
      replacement reuses the unique proved high crossing, then separately
      proves each low-button approach and high-entry handoff with bounded work.
      Its first real smoke finished normally in 3 minutes 45 seconds, retained
      two touch and four shoot links, but added no extended touch link and left
      the vertical objective gap unchanged. All four low buttons have exact
      approaches, but each low-contact-to-high-entry proof fails before the
      crossing leg. Replaying the four authenticated low-source/high-seed pairs
      through the unchanged rocket-jump oracle also proved none. The last
      rocket-jump discriminator substituted the exact high contact for the seed
      target and also proved none, ruling out target association. The active
      controller now proves a carried train transaction along the exact
      248-unit motion axis: authenticated low touch, boarding at the low pose,
      declared mover travel, and stable high landing. It does not use
      gate-crossing or direct-physics semantics. Its focused reducer, contract,
      codec, reader, and game-integration tests pass, but the first real smoke
      emitted zero ride links and left the objective graph unchanged. Live
      route capture then proved both trains expose the lower endpoint as OPEN
      and travel upward through CLOSING to CLOSED, opposite the earlier text
      ordering assumption. The corrected generator and reducer now derive lower
      and upper poses from the live route and pass both-orientation focused
      tests. The corrected-direction smoke finished normally but published no
      ride link. Both trains had floor-supported button approaches and nearby
      boarding candidates, but none boarded while the old controller required
      simultaneous button contact and train support. The active replacement
      separates authenticated floor-button activation from subsequent boarding,
      then advances the declared mover timing while requiring real train support
      before carry and upper egress. Focused tests pass immediate dispatch,
      exact support, both route orientations, and late-board rejection. Its
      first immutable real smoke still published only the two existing touch
      links and no carried ride link. Temporary stage counters are separating
      physical activation, dispatch dwell, boarding, carry, and egress before
      the next controller change. All 120 eligible calls authenticated dispatch
      and dwell; 76 physically activated the floor button, but none acquired
      exact train support before motion. The owning failure is now narrowed to
      the post-touch support target and controller. A bounded live-AABB
      RUN/JUMP fan proved that handoff and published two carried ride links,
      raising initial reachability to 1,646 red seeds and 1,624 blue seeds. The
      new RIDE records then exposed an exact selector bug: reverse-touch
      endpoint discovery rejected the mover because it counted independent
      RIDE records alongside PREOPEN touch-crossing records, dropping the four
      existing shoot reversals. Filtering that lookup by train mode restores the
      complete 2 touch + 2 ride + 4 shoot graph and retains all 1,889 seeds in
      both directions. Artifact materialization then exposed two independent
      contract-boundary defects. Link identity omitted the activation plan and
      collapsed distinct authenticated shoot buttons; the Python reader also
      lacked the C contract's sealed-stock TRAIN and TRAIN_SHOOT validation.
      Both now enforce exact plan identity, stock callbacks, the two-corner
      train cycle, button shape, and four-edge closure while rejecting an exact
      duplicate or unsealed callback. Both full host suites, 41 Python
      contract/artifact tests, module linkage, lint, and deslop pass. The fresh
      immutable smoke passes generation, both C readers, Python, lint, SNAG, and
      cold load with 3,403 seeds, 69,230 links, 80 mechanism nodes, 56 mechanism
      edges, 14 triggers, and eight plans. The objective core retains 1,889
      seeds in both directions. `lmctf07` is complete and leaves the live repair
      queue.

The corrected old no-artifact queue contained 23 maps after the later accepted
`lmctf02a`, `lmctf04`, `lmctf05b`, `smap14`, `smap39`, `xmap05`, and `xmap12`
runs. `lmctf58`, `lmctf07`, `lmctf27`, `lmctf40`, `lmctf45`, `tw2ctf2`,
`xmap04`, `xmap25`, and `xmap29` subsequently passed every acceptance gate,
leaving 14 maps in the live repair queue.
Exact retests prove that `tomb05` remains a genuine graph failure.
`tomb05` reaches objective-core with no closed route
shared by both flags.
Exact `tomb05` replay proves the new TELEPORT_DROP traversal, but it does not
close the objective core. From the two center dry seeds, the first airborne
mechanism contact is a cataloged vertical `trigger_push`, not either bottom
teleporter pad. The active repair is therefore a separate PUSH_JUMP admission:
prove the center source, authentic trigger contact, serialized launch, stable
upper landing, and full objective closure. A direct center-to-pad
TELEPORT_JUMP was refuted and will not be published. The staged traversal
contract, reducers, ownership, callbacks, and artifact wiring pass the complete
GNU and Make/Clang host suites. Two inherited speed-85 predicates initially
rejected tomb05's valid stock push speeds; replacing them with finite positive
speed validation retained the exact serialized-velocity check. Real smokes now
publish one witness for all four unique PUSH_JUMP component transitions plus
two same-component shortcuts, exhausting the proved PUSH_JUMP graph. The next
leg is TELEPORT_DROP. Wider distance, angular, broad stepped-lip,
contact-neighborhood, trace-plane, and fixed-point bracket experiments were
refuted. The final generic bracket reproduced the saved fixture's exact source,
lip, and heading on real `tomb05`, but the unchanged oracle stayed grounded and
timed out without traversing. The fixture is valid for the isolated contract,
not a real map traversal. That entire contact-edge experiment was removed;
the final component graph proves that rocket jumps provide one-way inbound legs
from the outer basins to each flag SCC. The flag exits are ordinary RUN/HOOK on
one side and RUN/JUMP on the other, but they terminate in dead SCCs. PUSH_JUMP
targets unrelated upper components. Exact replay of 1,303 topology-derived
outbound DROP pairs proved none, so DROP is not the return controller. The
existing HOOK oracle also proved none over the same outbound frontier. Landing
observation then found 12 exact DROP proofs, but their only unique component
transitions were already inside the flag-forward dead-end closures. Adding all
of them changes no objective reachability, so that experiment was removed. The
authenticated mechanism inventory then proved no PUSH or PUSH_JUMP from 15
dead-exit seeds against four nearby push nodes. Direct teleporter approach also
had zero production-envelope candidates, and both bottom teleporters lack a
static-world staging seed. The earlier TELEPORT_DROP scan preceded later HOOK
and rocket-jump links, so it did not cover the final dead-exit frontier. A late
replay found three production-envelope pairs but zero unchanged controller
proofs, ruling that path out too. Exhaustive bounded RUN, JUMP, and HOOK replay
found no admitted ordinary witness. The map uses gravity 100, while ordinary
JUMP discovery still uses fixed normal-gravity bounds. A wider diagnostic found
five exact unchanged JUMP proofs that the old admission excluded, confirming a
general physics-scaling defect. Their same-side links alone do not close the
map. Applying the full physics envelope admitted 613,836 pairs and produced 124
shared seeds, but pruning still found no bidirectional core and the extra proof
work was too broad. That integration was held back. The active diagnostic now
examines the first pruning boundary around those shared seeds. Both nearest
side-only-to-shared reverse DROP pairs have stable dry sources but no
player-hull lip, so neither admits a walkoff or landing and DROP is ruled out at
that exact boundary. A bounded streaming report is identifying only the held
low-gravity JUMP links needed to assemble the shared components before any
production admission is narrowed. That reconstruction found zero out-of-normal
RUN or JUMP edges crossing baseline components inside the apparent 124-seed
shared partition. The partition was an indirect scheduling and redundancy
perturbation, not a missing low-gravity traversal. Broad admission and all
diagnostics have therefore been removed; the repair continues from the
unchanged baseline side-only-to-shared route gap. A four-round bounded
flag-forward JUMP iteration reached fixed point after three rounds: seven exact
proofs produced four same-side component transitions but zero shared objective
seeds. The next diagnostic isolates rocket-jump scheduling by component and
missing-objective bit after only those exact JUMP witnesses. Fair per-component
rocket-jump queues preserve all three known inbound witnesses and prove four
component transitions in 383 calls, restoring 124 initially shared seeds. The
objective prune still removes that entire set because the reverse route remains
open. The four-mask SCC report proves those 124 seeds are returnable to both
roots but reachable from neither. The two exact missing cuts run from each
side's root-cycle component to the opposite side's return component. Existing
RUN, JUMP, DROP, HOOK, and rocket-jump controllers are being replayed over all
production-admitted pairs for only those two component transitions. That exact
replay exhausted both cuts without a proof. Twenty first stable DROP landings
matched no existing canonical seed envelope, so destination reassociation is
also refuted. The next bounded diagnostic derives new canonical ground seeds
only from those proved landings, publishes the exact DROP to each derived seed,
and recomputes ordinary links and objective masks. All 20 landings produced
valid unique canonical seeds; 16 exact DROP links and 112 ordinary links were
proved, but they expanded only same-side components. Authenticated teleporter,
PUSH, and PUSH_JUMP admission from those seeds proved none. HOOK also proved
only one incidental same-side link at its bound. Eight rounds of iterative
rocket-jump replay likewise produced only same-side transitions and never
changed the objective masks after the first round. The existing controller
inventory is exhausted. A generic target-steered low-gravity DROP diagnostic
is now testing the two exact cuts with destination-derived air control and a
runtime-serializable command law. Four gravity-derived steering schedules each
proved zero stable exact arrivals in both directions, so that controller is
also refuted and no wire change was made. A bounded multi-source Pmove search
over a generic command lattice is now looking for the actual physical route
before any further controller is proposed.
The live exact queue is `lmctf01`, `lmctf06`, `lmctf12`, `lmctf15`, `lmctf19`,
`lmctf25`, `lmctf30`, `tomb05`,
`tw2ctf3`, `tw2ctf4`, `xmap02`,
`xmap13`, `xmap18`, and `xmap26`.
All eight incomplete generation cases reproduced terminal 900-second timeouts
in base-link proof before the first progress interval against the exact accepted
module and immutable 175-map snapshot: `lmctf15` with 2,138 seeds,
`lmctf25` with 2,430, `lmctf58` with 2,108, `tw2ctf2` with 1,983, `xmap05`
with 2,206, `xmap12` with 2,558, `xmap26` with 1,575, and `xmap29` with
12,060. All eight share the same normalized timeout signature.
Instrumented samples attribute roughly 80--84% of categorized base-link CPU to
the shared hook prover. The canonical optimization skips only a repeated
world-only Pmove step after the exact phantom and command have reached a clean
byte-identical fixed point; reducer time, observations, hazards, and failure
classification still advance normally. A complete `lmctf42` comparison wrote
the same 285 seeds and 9,899 links with a byte-identical artifact while reducing
base-link CPU from 19,126 ms to 15,796 ms. The seven non-overflow timeout maps
were then measured concurrently against one immutable source and
module. `lmctf15`, `lmctf25`, `lmctf58`, `tw2ctf2`, `xmap05`, `xmap12`, and
`xmap26` all reached the same 900-second base-link timeout before the first
256-seed progress interval, wrote no artifact, and retained the same normalized
signature. The fixed-point optimization is therefore
safe but insufficient for this family. The hook prover now ranks candidates by
connected seed component and checks them with component/source round robin. It
stops after 8,192 calls. It does not cap individual sources or components. The
full GNU and Clang/Make host suites pass, including both long strike tests,
traversal, PUSH, hook, source-size, deslop, and unresolved-symbol gates.
`lmctf42` also passes both C readers, the Python reader, lint, and a fresh cold
load. It has 285 seeds, 4,293 links, 496 hook links, and 4,895 ms of base-link
CPU. Its seeds and links match the isolated scheduler run. All 3,754 ordinary
RUN/JUMP/DROP/TELE links remain record-identical to the prior 9,899-link
exhaustive baseline. Two independent `xmap05` runs passed generation, both C
readers, the Python reader, lint, SNAG, and fresh cold load. Each artifact has
2,206 seeds and 41,251 links, and the two artifacts are byte-identical.
`xmap12` also passed every gate with 2,562 seeds and 60,746 links. Both maps are
no longer in the unresolved queue. `tw2ctf2` cleared base-link generation and
proved 33,868 links, but objective pruning removed the red and blue route sets.
It wrote no artifact and remains in the unresolved queue as a graph failure.
`lmctf15` also cleared generation and proved 36,551 links. Each flag reached
only 10 seeds, the two sets had no shared seed, and pruning removed all 2,138
seeds. It also remains in the queue as a graph failure. The final three smokes,
`lmctf25`, `lmctf58`, and `xmap26`, reached the 30-minute bound. `lmctf25`
finished base links after about 23 minutes, then timed out at
the start of compound swim. `lmctf58` completed objective closure and wrote a
RUNE, but generation never reached runtime-ready acceptance. `xmap26` remained
inside base-link proof for the full bound. None of these artifacts is accepted,
and all three maps remain in the unresolved queue. Further timing and stack
diagnosis must separate the remaining base-link, post-base, and post-write work.
An isolated exact `xmap26` controller run reproduced the base-link timeout with
one worker, so the earlier concurrent batch was not the cause. Temporary
per-source timing then showed that source 0 alone exceeds one CPU minute. It is
a stable dry high seed paired against hundreds of lower seeds, repeatedly
entering the exact drop prover. A behavior-preserving drop-pair optimization is
active; it may reject only pairs that cannot execute the existing serialized
controller and must retain all current proof and runtime gates. Removing one
duplicate final overlap query preserved all measured outcomes and replay steps
while reducing the isolated Clang source-0 cost by 8.2 percent. A short-landing
preflight was refuted because it added rollouts and replay steps for only a
further 7.8 percent reduction, and its source changes were reverted. The active
population-scan theory was also refuted because exact DROP generation bypasses
that discovery path. Exact profiling attributes the remaining cost to native
Pmove and collision. The landed DROP prefix cache preserves all 298 rollout
outcomes and 14,022 reducer steps, reuses 7,654 identical physics steps, and
reduces the measured DROP cost from 10.323 seconds to 4.651 seconds. Cache-on
and cache-off `lmctf42` runs pass every controller gate with identical ordered
seeds, links, decoded records, counters, and final artifact bytes. The bounded
active-source cache uses at most 1.55 MB on the measured xmap26 source. Both
full GNU and Make/Clang host suites pass. Corrected per-source profiling shows
stable sources finish in about 4.5--5 seconds. Source 0 makes 419 prover calls,
while ordinary RUN/JUMP accounts for only 12 calls and 244 steps. DROP remains
dominant. The earlier no-output probe expected a removed marker and ran past
source 0. Exact stage profiling attributes 4.755 of 4.824 rollout seconds to
6,379 native oracle frames. It reused 7,643 cached frames; reducer, destination
contact, and cache work together took only 32 milliseconds. A proposed general
optimization replaced lip-keyed linear prefixes with a bounded per-source trie
of exact command histories to share native movement across lip candidates.
Exact measurement refuted that design: it created essentially the same native
nodes and cache hits as the linear cache, proving no additional cross-lip
sharing. The trie and its probes were reverted. The next measurement found only
44 repeated full native transition inputs among 6,379 remaining calls, ruling
out a general state-transition cache. Low-distortion hot-path profiling
attributes 3.617 of 3.734 Pmove seconds to synthetic door checks; engine traces
and point-contents together take only 45 milliseconds. More than two million
door candidates repeatedly recompute identical bounds. A scoped exact
door-bounds cache validates mover state and exact hull on every hit, with scope
teardown and original-path fallback on mismatch or allocation failure. Focused
GNU/GCC and Make/Clang tests pass. On the same `xmap26` source class, it reduced
CPU from 4.854--5.005 seconds to 120 milliseconds, using 30 entries and about
16.6 KB. Cache-on and cache-off `lmctf42` smokes both passed generation, dual C
readers, the Python reader, lint, SNAG, and cold load with identical ordered
seeds, links, mechanism records, telemetry counters, and artifact bytes. The
full GNU/GCC suite, exact source-size audit, and full Make/Clang suite pass. A
real `xmap26` smoke remains before integration. The first real smoke was
manually interrupted after 4 minutes 54 seconds because its
missing progress line was mistaken for a stall; it produced no result. A
bounded follow-up proves all 1,575 sources finish in about 31 seconds with zero
cache-state mismatches. The apparent source-loop stall was buffered output.
Post-source profiling localized the remaining cost to compound swim proof,
which previously ran for more than 39 seconds without returning. Keeping the
same exact validated door-bounds cache active through that phase finishes it in
about 18 seconds with zero state mismatches. Diagnostics are being removed and
cache-on/cache-off `lmctf42` artifacts and normalized counters are identical.
The focused oracle test and both full host suites pass. A fresh cache scope
around the separate compound-swim generator then reduced that phase from about
555 seconds to 18 seconds with zero state mismatches. Cache-off and cache-on
`lmctf42` artifacts remain ordered and byte-identical, rotating cache hit and
state-drift coverage passes, and both full host suites pass. The verified cache
changes are canonical. The complete `xmap26` rerun now reaches the same graph
failure in about 299 seconds wall with the same 1,575 seeds, 46,087 links,
catalog shape, objective roots, base counters, and prune result. Objective
pruning removes every seed, so no artifact is written and the map remains a
graph-connectivity repair. Its exact pruning cut and authentic nearby
mechanisms are next. The pre-prune graph has 94 components: one 443-seed red
component, one 464-seed blue component, and 92 components reached by neither.
Every boundary link points out from an objective component; none returns or
crosses between objectives. Only the six center-touch platforms and their six
synthetic triggers touch that cut. The repair is tracing the exact platform
staging rejection. Each bottom pose has one rest-plane seed reached only by
SWIM and no outgoing link; each top pose has four dry egress seeds in the
corresponding objective component. RL_LIFT rejects the bottom source because
it is already inside the center trigger and lacks the dry planar approach the
current oracle and runtime require. The active design is the smallest generic
water-approach-to-lift transaction that reuses existing compound entry and
RL_LIFT contracts where they can represent the full activation, carry, and dry
egress proof. Five platforms have one supported water seed already overlapping
the center trigger; the sixth requires a physical six-unit swim approach. The
common gate therefore proves authoritative water movement until matched trigger
contact and platform support coincide, then switches to the existing lift
hold, carry, and egress transaction.
The canonical source also rejects a known water-seed-capacity overflow before
base-link proof. `xmap29` later passed with a replay-proved sparse water graph,
so the obsolete dense-capacity failure is no longer in the repair queue.
Parallel graph-failure triage groups the remaining work into
shared teleporter, platform/lift, door/train/activation, and push-controller
repairs, with targeted train/elevator and advanced-push extensions after those
batches. `xmap13` joins the speed-85 push retest, while `xmap18` joins the
teleporter/shootable-door batch. `xmap25` has since passed its full acceptance
stack. The accepted `smap28` PUSH handoff and authoritative-state repair is the
source baseline for the `xmap13` retest. That exact retest terminated
`GEN_FAIL` after 11 minutes 16 seconds, published 2 fixed PUSH links, wrote no
artifact, and removed all 1,337 seeds during closed-core pruning.
The BSP contains 12 connected overlapping push fields with equivalent stock
impulses plus 4 singleton fields. A terminal discriminator refuted the saved
staged-PUSH repair: all eight multi-stage witnesses become airborne, then cross
a symmetric stock iris activator pair whose four-leaf door teams generation has
already posed open. Reusing the existing declared-activator and canonical-team
validators proves all eight flights clean and uncontaminated through landing.
They then fail destination-seed ownership. A bounded post-landing replay stays
grounded, dry, clean, and outside every door sweep for ten seconds, but world
collision pins the body at first contact despite accepted commands. No stable
seed owns that contact and no existing controller reaches the nearest stable
outgoing seed. `xmap13` therefore joins the end-of-plan human traversal list;
an unproved obstacle-detour controller will not be invented speculatively.

A fresh immutable generation batch now covers the entire historical 23-map
queue with four concurrent workers. It includes already diagnosed maps and the
accepted `lmctf58` so that indirect repairs and stale queue entries are proved
by current artifacts rather than inferred from older results. Maps changed by
an in-flight repair are rerun after that repair passes its full gates and joins
the next frozen source. `tomb05` no longer justifies speculative controller
work: if ordinary generation still cannot discover its physical return route,
an authoritative human traversal will be used to identify the missing generic
transition before any new admission is implemented.

The fresh batch has replaced the old queue labels with these current results:

| Map | Current result |
| --- | --- |
| `lmctf01` | 2,401 seeds all prune; the water-to-lift repair leaves the graph unchanged. |
| `lmctf06` | Fresh current source has 1,437 seeds/12,528 links, all prune. Native reverse replay rejects all 16 red-forward cuts. Four exact button-to-vertical-carrier pairs now enter the mechanism catalog, but publish zero lift links, locating the next failure inside PLATFORM admission/rest/approach rather than target discovery. A narrow entry-profile diagnostic is running. |
| `lmctf07` | PASS after the accepted train/button repair. |
| `lmctf12` | Fresh current source has 1,972 seeds after sparse-water integration: 939 red-only, 921 blue-only, 0 shared, 112 neither; roots 129/130 and zero boundary links. Inventory has six resolved teleporter pairs and three externally targeted quad doors; all six teleporter sources lack static staging. Exact SCC/cut diagnosis is active. |
| `lmctf15` | 36,551 links; both flags remain isolated in separate ten-seed components. |
| `lmctf19` | Automated inventory is exhausted: both flags are symmetric one-way DROP basins, and wider RUN plus reverse JUMP/HOOK/rocket-jump replays prove no egress. It awaits an end-of-plan human traversal. |
| `lmctf25` | The old timeout is gone; 45,903 links complete before all 2,413 seeds prune. |
| `lmctf27` | PASS and integrated: 620 seeds, 6,751 retained links, 316 authenticated plans, and 490 seeds reachable from both roots; both C readers, Python, lint, SNAG, fresh cold load, runtime-ready, accepted-map identity, GNU, Make/Clang, production linkage, and deslop pass. |
| `lmctf30` | 849 seeds all prune; the water-to-lift repair leaves the graph unchanged. |
| `lmctf40` | PASS in 67 seconds after the controller accepts non-adjacent deferred-publication diagnostics. |
| `lmctf45` | PASS and integrated: the generic fixed-point inverse repair adds exactly HOOK, rocket-jump, HOOK; 1,071 seeds become reachable from both roots and the 1,369-seed/14,287-link artifact passes all readers, lint, SNAG, cold load, accepted-map identity, GNU, Make/Clang, production linkage, source-size, and deslop. |
| `lmctf58` | PASS after exact scoped door-bound reuse removes redundant loader sweeps; the regenerated artifact is byte-identical to the accepted reference. |
| `tomb05` | 961 seeds all prune; automatic discovery remains deferred pending a human traversal if needed. |
| `tw2ctf2` | PASS and integrated: the generic post-prune closure publishes exactly four proved links, retains 1,922 seeds from both roots, and writes a 1,983-seed/33,586-link artifact; both readers, lint, SNAG/semantic checks, cold load, accepted-map identity, GNU, Make/Clang, production linkage, source-size, and deslop pass. |
| `tw2ctf3` | Clean-production PASS without teleport changes: 1,815 seeds/29,948 links, roots 224/225, and both readers, Python/lint/semantic checks, SNAG, and fresh cold load green. The sole repair is automatic-door safety when a nonempty mapper target resolves to zero live targetnames. Accepted-map identity and full source gates remain before integration. |
| `tw2ctf4` | 2,128 seeds all prune; no mechanism yet owns the reverse cut. |
| `xmap02` | 1,480 seeds all prune; four teleporter pads lack staging, but causality remains unproved. |
| `xmap04` | PASS and integrated: 1,094 seeds, 13,356 links, 11 lift links, 12 plans, and 974 seeds reachable from both roots; both readers, lint, SNAG, cold load, byte-identical regeneration, GNU, Make/Clang, production linkage, source-size, and deslop pass. |
| `xmap13` | All 1,337 seeds still prune. Clean staged flights land outside every door sweep, but world collision pins the body at an unowned first-ground state and accepted movement cannot reach the nearest stable outgoing seed. Existing automated controllers are exhausted; it awaits end-of-plan human traversal. |
| `xmap18` | All 2,850 seeds prune across a 2,666-unit central cut; teleporter admission was exactly replayed and refuted. |
| `xmap25` | PASS after a fail-closed final-topology repair replay-proves exactly one reverse DROP across a one-sided HOOK boundary; all 1,920 objective-reachable seeds are shared, and the accepted `lmctf42` artifact remains byte-identical. |
| `xmap26` | Six water-to-lift links now publish; a later static-world 1,440-unit cut still prunes all 1,575 seeds. |
| `xmap29` | PASS with a replay-proved sparse water graph: 6,465 seeds, 5,348 exact SWIM links, and 68,935 total links. |

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
5. Require exactly 175 PASS results. A generation failure, timeout, lint
   failure, reader disagreement, semantic failure, or cold-load failure blocks
   completion.
6. Repair any new failure at the source, data, or tool boundary that owns it,
   then refreeze and restart every artifact invalidated by that repair.
7. Freeze the final 175-artifact manifest and its evidence hashes.

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
8. Record the final source, module, 175-artifact corpus, real-match evidence,
   CI, tag, and release identities.

Downloadable RUNE or PAK packaging remains deferred until explicitly resumed.

## Critical path

```text
finish the remaining graph repairs
  -> pass exact CI and create a new source, module, and input freeze
  -> restart and validate all 175 RUNEs
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
- [x] Prior frozen module identity and evidence proven.
- [x] Controller deferred-SNAG phase repair, full tests, live cold-load proof,
      and exact CI on both branches.
- [x] Teleporter and objective-core diagnostics integrated, with `lmctf02a`
      accepted end to end.
- [x] Declared-door replay integrated, with `lmctf03` and `mactf01` cold-ready.
- [x] Separate cold-load timeout integrated, tested, and green in exact CI.
- [x] `lmctf04` objective-root repair integrated and accepted end to end.
- [ ] Remaining graph blockers repaired and integrated.
- [ ] New final combined source, tool, module, and input freeze.
- [ ] Exactly 175 newly generated and fully accepted RUNEs.
- [ ] Real-match behavioral validation with ordinary map-list inputs.
- [ ] Match-exposed defects repaired and revalidated.
- [ ] Final compiler, Make dialect, platform, and repository gates.
- [ ] Documentation, version, branch integration, tag, release, and hash audit.
