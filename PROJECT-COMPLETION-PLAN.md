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
- The durable asset directory contains 180 BSP files. The 175-entry manifest is
  authoritative and excludes the retained unsuffixed bases `lmctf05`,
  `smap31`, `xmap07`, `xmap11`, and `xmap14`. Build the frozen asset set by
  iterating the manifest, not by copying every BSP in the directory.
- Generate all 175 RUNEs from one frozen source, module, configuration, engine,
  reader set, linter, semantic checker set, and BSP set.
- A route-only failure does not block release when the frozen RUNE is
  structurally valid, both objective regions work, and real matches prove that
  bots still fight, attack, and defend normally. The only allowed omission is
  the path between the flags or the carried-flag return path. Do not invent a
  traversal link to make such a map pass. Human play must later identify the
  missing path or paths and drive a replay-proved RUNE update, but that update
  does not block the initial release.
- Route-only is an authenticated RUNE graph contract, not an inferred loader
  exception. A complete RUNE keeps the existing rule that every live seed must
  reach both objectives. A local-only RUNE may retain only seeds that reach at
  least one real objective; seeds that reach neither objective remain
  tombstoned, unknown contracts reject, and a complete graph mislabeled
  local-only rejects. The generator must exhaust the ordinary closure work
  before emitting local-only and must not add a link merely to change the
  classification.
- A local-only RUNE must cold-load with two valid flag roots and a finite
  multi-source local-objective field for every live seed. When an organic or
  late coordinator objective is unreachable, runtime selection must fall back
  to that proved local field rather than entering seedless recovery solely
  because the missing inter-flag edge is absent. Combat and reachable attack,
  defense, escort, recover, and item behavior remain active inside each proved
  component.
- The two local-only objective roots are authenticated in the seed payload.
  Only those marked roots may be terminal live sinks after neutral geometry is
  removed. The codec requires exactly two markers, the world validator requires
  them to resolve to the spawned flag stands, and arbitrary live sinks reject.
- Post-match learning satisfies that update requirement; live RUNE mutation is
  not required. The server-side trace must be able to record every active
  non-bot client's exact movement and isolate any selected client and frame
  window for replay. The capture and importer tests must remain green in the
  final source freeze.
- Human traces remain evidence, not permission to mutate topology directly.
  The corresponding generic oracle must replay the discovered transition, the
  generator must rebuild a new RUNE, and that replacement must pass the
  original complete two-objective validator. Regenerate the exact-bound SNAG
  and any retained learned sidecars for the replacement, cold-load the staged
  bundle, install sidecars before the RUNE commit point, and publish the new
  graph only at the next coherent map setup. A partial or interrupted install
  may fail closed but must never publish mixed graph and sidecar state.
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
- [x] Repair the remaining maps whose objective route core is closed.
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
`tw2ctf3`, `xmap02`, `xmap04`, `xmap25`, and `xmap29` subsequently passed
every acceptance gate. The later `tw2ctf4` shoot-door repair also passed every
gate. The reduced `lmctf30` repair then passed every acceptance gate, leaving
10 maps in the live repair or end-of-plan human-traversal queue.
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
The source-owned repair queue for the former ten-map set is empty. `lmctf06`
and `lmctf15` now have accepted `ROUTE_ONLY` artifacts under the direct
authenticated post-inventory local-union contract.
`lmctf25` is route-only: its validated `local_only` artifact is releasable for
normal attack and defense, with a route-only real match or later human learning
left to supply the missing traversal evidence. The other seven maps from the
former ten-map queue require only final real-match evidence or later
non-blocking human learning.
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
| `lmctf01` | A fresh integrated-source rerun again proves 1,776 seeds/16,816 links, then all prune: 999 red-only, 425 blue-only, 0 shared, and 352 neither. The sixteen closest opposing pairs are 252--260 units apart. Exact replay in both directions proves zero RUN, JUMP, DROP, HOOK, SWIM, or rocket-jump traversals. The only two cross-partition SWIM boundaries run one way from the red-only partition into neutral geometry; their reverse traversals fail and adding either cannot close the graph. Replacing the old boundary-stop command with a generic brush-center inward contact turns 0/165 into 39/165 exact stock `button_touch` proofs with stable authenticated anchors, but the external button-door is not the objective cut. Ordinary door-face staging produces zero stable ground points across 4,224 authenticated open-pose probes. The button-open DROP investigation reaches five stable waterlevel-three states, but all remain in the same objective-side component. Exact continuation replays from those states add no missing objective bit. Across every authenticated target-sweep incident pair, zero stable/outgoing endpoints in different SCCs cross within the existing 768-unit door envelope. All eight lift links also remain inside their existing side or neutral SCCs. The closest symmetric side-to-neutral cuts are 58.293 units across a permanent world divider: point line-of-sight is clear, but a standing player hull immediately hits world geometry, stock movement stops short, and exact replay of RUN, JUMP, DROP, HOOK, rocket jump, and SWIM proves zero crossings in all four directed pairs. The remaining red-to-neutral SWIM edges are one-way and noncausal. Two authoritative human playthroughs are now exact-applied: one of 46 first-run dry nominations survives reproving, none of 19 second-run nominations survive, and the route remains open. The resulting 1,776-seed/14,696-link `local_only` artifact and matching SNAG pass both readers, Python, lint, and live bot cold load. No additional ordinary dry-edge human run is pending. |
| `lmctf06` | VERIFIED ROUTE-ONLY. The authenticated button 447 -> relay 449 -> wall 443 off -> delayed relay 450 -> wall on transaction remains valid, including 200 ms dwell, four-second lease, restoration, and body-clear, but it is a separate BFG shortcut rather than the objective cut. The current graph has 477 red-only, 676 blue-only, 0 shared, and 284 neither seeds; all 32 ranked exact reverse-HOOK repairs reject. Natural 8,192-, 4,096-, and 1,024-bound fair any-region runs each timed out at 1,800 seconds with no artifact and the same normalized signature, so numeric tuning was closed. Production now uses the generic authenticated post-inventory contract: after relay, core, reverse-boundary, SWIM, and current-identity learning inventories are exhausted while closure remains open, it validates and publishes the retained objective union as `local_only`; it preserves all prior proved merges, adds no edge, removes only neutral-only material, and fails closed on validation or publication errors. The accepted immutable run at `/tmp/lmctf06-direct-accept.54dSRR` publishes 1,437 seeds, 12,827 links, 159 mechanism nodes, 19 activation triggers, 44 inventory edges, 52 total mechanism edges, and four plans. Both C readers, Python, graph-contract lint, semantic gates, matching SNAG, and fresh cold load pass. Python trigger counting was corrected to exclude the three `trigger_hurt` stateful hazard targets, matching both C readers at 19 without changing the artifact. The artifact is accepted for initial route-only release; its preserved human replay remains optional later learning evidence rather than initial-release authority. |
| `lmctf07` | PASS after the accepted train/button repair. |
| `lmctf12` | VERIFIED ROUTE-ONLY; BOTH HUMAN ROUTES EXACT-APPLIED. The prior current-contract rerun reproduced 1,972 seeds. Its pre-Dijkstra audit examined 5,029 BSP-backed contacts and 56 initially crossing directions, retained three already-proved directions, rejected five exact dry directions, and exact-proved 48 bidirectional SWIM links. SCC count fell from 26 to 14, while the false rail-window pair 35/591 was never nominated or added. The former fair any-region pass retained eight genuine exact SCC-changing merges before returning `open-budget`; its 1,972-seed/31,086-link `local_only` artifact retained 973 red-reaching, 956 blue-reaching, and 25 shared seeds and passed both C readers, Python, lint, and cold load. Final acceptance must regenerate it under the direct authenticated post-inventory local-union contract, preserving those proved merges without entering the removed late prover. The existing 47,110-step v1 human trace has two complete blue-to-red traversals. The first route's ten missing dry nominations were all rejected by the unchanged oracle; the second emitted none. Both playthroughs remain exhausted, with no additional human run pending for initial route-only release. |
| `lmctf15` | VERIFIED ROUTE-ONLY. The mirrored timed-vault catalog, transaction, binding, and live runtime retain the exact 200 ms exterior hold, one-second activation delay, nine-second usable lease, full two-leaf door/laser/speaker fanout, flag-pickup egress ownership, ten-second restoration, body-clear deferral, and fail-closed identity checks without changing stock human behavior. Real BSP/Pmove audit proves each flag remains in a separate ten-seed RUN-only component: the nearest authenticated water-exit components are about 697 and 701 units away, direct standing-hull traces block immediately, and no already-proven action connects either cut. The failed dry `RL_RUN` suffix experiment was pruned; the independently valid target-facing `RL_SWIM` selector and generic timed-vault controller remain. The immutable publication at `/tmp/lmctf15-route-only.xu08Cq` writes a deterministic 1,953-seed/172-link `local_only` artifact with objective roots 16/89, 123 mechanism nodes, 19 triggers, 98 inventory edges, and zero fabricated plans. Both C readers, Python, graph-contract lint, semantic gates, matching SNAG bootstrap, fresh cold load, and four admitted bots pass. It is releasable for normal attack and defense; the missing flag route remains later human-learning or route-only real-match evidence, not a source blocker. |
| `lmctf19` | Current immutable generation proves 2,153 seeds/34,747 links, then all prune: 631 red-only, 659 blue-only, 0 shared, and 863 neither. The closest opposing seeds are 112 units apart. Automated inventory is exhausted: both flags are symmetric one-way DROP basins, and wider RUN plus reverse JUMP/HOOK/rocket-jump replays prove no egress. A cumulative-fallback rerun now publishes a loadable 2,153-seed/22,599-link `local_only` artifact; both C readers, Python, lint, SNAG, and cold load pass. It awaits an end-of-plan human traversal. |
| `lmctf25` | VERIFIED ROUTE-ONLY. Fresh current source proves 2,122 seeds, then all prune: 547 red-only, 557 blue-only, 0 shared, and 1,018 neither. The exact nearest cut is 834.602 units, including 832 units vertically. Two START_ON trains traverse a sealed fourteen-corner loop and pause for three seconds at the authored upper and lower stations. The authenticated continuous-station transaction, dwell, binding, serialized approach, and passive runtime remain production-linked and focused-green. An exhaustive bounded same-height BSP search from 36,090 ranked dry sources finds no hull-valid route into posed train support, so exact boarding Pmove never becomes eligible and no `RL_TRAIN` controller-11 link is publishable. The unsuccessful search code was pruned. The safe 2,122-seed/13,566-link `local_only` artifact passes both readers, Python, lint, semantic gates, cold load, and admitted bots. It is releasable for normal attack and defense without a fabricated flag route; a route-only real match or later human learning remains pending for traversal evidence. |
| `lmctf27` | PASS and integrated: 620 seeds, 6,751 retained links, 316 authenticated plans, and 490 seeds reachable from both roots; both C readers, Python, lint, SNAG, fresh cold load, runtime-ready, accepted-map identity, GNU, Make/Clang, production linkage, and deslop pass. |
| `lmctf30` | PASS and integrated. The reduced immutable repair starts from 746 seeds and adds exactly four authenticated toggle-carrier `RL_LIFT` links, six exact directed `RL_SWIM` links across the missing SCC chain, and two reverse-boundary `RL_HOOK` links. The objective core closes with 637 retained seeds and 3,380 links. Both native readers, the Python reader, deployment lint, SNAG bootstrap, cold load/runtime-ready, focused controller tests, full GNU and Make/Clang suites, production linkage, source-size, and deslop pass. |
| `lmctf40` | PASS in 67 seconds after the controller accepts non-adjacent deferred-publication diagnostics. |
| `lmctf45` | PASS and integrated: the generic fixed-point inverse repair adds exactly HOOK, rocket-jump, HOOK; 1,071 seeds become reachable from both roots and the 1,369-seed/14,287-link artifact passes all readers, lint, SNAG, cold load, accepted-map identity, GNU, Make/Clang, production linkage, source-size, and deslop. |
| `lmctf58` | PASS after exact scoped door-bound reuse removes redundant loader sweeps; the regenerated artifact is byte-identical to the accepted reference. |
| `tomb05` | VERIFIED ROUTE-ONLY STRUCTURE; HOOK-SPECIFIC HUMAN EVIDENCE UNDER EXACT APPLY. A fresh immutable run publishes a valid 961-seed/4,272-link `local_only` artifact with two objective roots, 173 red-reaching, 174 blue-reaching, and 26 shared seeds. Both C readers, the Python reader, graph-contract lint, and fresh-process cold load with admitted bots pass; an ordinary real match must still prove normal local combat before release. Human traversal proved both flags are reachable in gravity 100 by attaching around the building exterior, accelerating, releasing, re-firing while airborne, and landing. Four bounded traversals are preserved. The first two produce one exact-source nomination apiece, and the unchanged in-engine hook oracle rejects both without adding an edge. The third produces 153 nominations; the oracle rejects all of them without adding an edge. The fourth will be rebound after the in-progress mechanism-contract refreeze. |
| `tw2ctf2` | PASS and integrated: the generic post-prune closure publishes exactly four proved links, retains 1,922 seeds from both roots, and writes a 1,983-seed/33,586-link artifact; both readers, lint, SNAG/semantic checks, cold load, accepted-map identity, GNU, Make/Clang, production linkage, source-size, and deslop pass. |
| `tw2ctf3` | PASS and integrated without teleport changes: 1,815 seeds/29,948 links, roots 224/225, 156 mechanism nodes, 27 triggers, 62 inventory edges, and 12 plans. The generic repair recognizes that a nonempty automatic-door target resolving to zero live targetnames is an exact stock no-op. Both readers, Python, lint, SNAG, fresh cold load, accepted-map identity, GNU, Make/Clang, production linkage, source-size, and deslop pass. |
| `tw2ctf4` | PASS and integrated: the generic authenticated shoot-door pass resolves all six teams, proves nine crossings, and serializes the twelve links needed for objective closure. The source-owned runtime adapter equips, aims, fires, authenticates the door-team transition, and crosses under the same serialized contract. A fresh post-merge immutable run writes 2,066 seeds/31,503 links with 1,990 seeds reachable from both objectives, 246 mechanism nodes, 35 inventory edges, and 14 plans. Both C readers, Python, lint, SNAG, fresh cold load, focused GNU and Make/Clang suites, production linkage, source-size, and deslop pass. |
| `xmap02` | PASS and integrated. The previous graph had 1,462 seeds/21,679 links, with 698 red-only, 682 blue-only, 0 shared, and 82 neither; its closest opposing seeds were 345 units apart. Exact native rocket-jump replay succeeds in both directions across that flat gap, which the ordinary global scheduler excluded because its rise is below 80 units and its horizontal separation exceeds 320. The bounded, map-independent objective-closure transaction selects the nearest opposing partitions, retains only a two-way replay-proved pair that closes both roots, and rolls back every partial attempt. The accepted artifact adds exactly two `RL_ROCKETJUMP` links and retains 1,462 seeds/21,100 links. Generation, both C readers, Python, lint, SNAG, semantic checks, fresh cold load, GNU, Clang, source-size, and deslop pass. A successful-map control remains byte-identical. |
| `xmap04` | PASS and integrated: 1,094 seeds, 13,356 links, 11 lift links, 12 plans, and 974 seeds reachable from both roots; both readers, lint, SNAG, cold load, byte-identical regeneration, GNU, Make/Clang, production linkage, source-size, and deslop pass. |
| `xmap13` | Current immutable generation proves 1,337 seeds/20,970 links, then all prune: 380 red-only, 376 blue-only, 0 shared, and 581 neither. The complete ordered and equivalent PUSH alternative is refuted: eight clean staged landings finish grounded, dry, uncontaminated, and outside every door sweep, but all ten-second accepted-command egress replays remain pinned before the nearest stable outgoing seed. A cumulative-fallback rerun now publishes a loadable 1,337-seed/13,133-link `local_only` artifact; both C readers, Python, lint, SNAG, and cold load pass. No admissible automated controller remains; it is reserved for an end-of-plan human traversal. |
| `xmap18` | Current immutable generation proves 2,417 seeds/28,556 links, then all prune: 223 red-only, 219 blue-only, 0 shared, and 1,975 neither. Exact condensation has 48 SCCs. The objective regions are SCC 11 with 210 seeds and SCC 42 with 205; the main red and blue geometry is stranded in neutral SCCs 44 with 595 seeds and 47 with 584, while center SCC 45 has 198. Replay of every nearest pad-component cut proves only two one-way rocket-jump entries into the dry base pads and one outbound run at a center pad. The connected water states near the elevated return pads remain outside the 128-unit contract; widening that diagnostic envelope to 256 units reaches them, but the unchanged teleporter-swim oracle rejects every 156-unit rise. A generic SCC-crossing native rocket-jump diagnostic proves 193 witnesses, but all belong to only two one-way neutral-pad cuts and every endpoint has objective mask zero. A stronger optimistic refutation publishes the cheapest two witnesses directly as authenticated movement-to-teleport links without yet requiring exact trigger touch; both only feed the neutral center SCC, and generation still fails with the identical objective signature. Thus even an admissible atomic rocket-jump-to-teleport controller cannot close this route. All diagnostics were removed; `xmap18` is reserved for an end-of-plan authoritative human traversal. |
| `xmap25` | PASS after a fail-closed final-topology repair replay-proves exactly one reverse DROP across a one-sided HOOK boundary; all 1,920 objective-reachable seeds are shared, and the accepted `lmctf42` artifact remains byte-identical. |
| `xmap26` | A prior current-source immutable run corrected the stale dense-water result: sparse-water publication yielded 1,174 seeds, 918 exact SWIM links, 11,842 total links, and zero declared lifts; 202 seeds were red-only, 216 blue-only, 0 shared, and 756 neither before all prune. The closest objective gap was a 1,472-unit static-world cut. An exact inverse-boundary experiment found zero repair links. The former cumulative fallback retained 22 exact bridges before returning `open-budget`, then published a 1,174-seed/6,392-link `local_only` artifact with 209 red-reaching and 223 blue-reaching seeds. Both C readers, Python, lint, matching SNAG, and cold load with three bots passed. Final acceptance requires regeneration under the direct authenticated post-inventory local-union contract; candidate-local mechanism rejection remains nonfatal and retained-link remap corruption remains fatal. |
| `xmap29` | PASS with a replay-proved sparse water graph: 6,465 seeds, 5,348 exact SWIM links, and 68,935 total links. |

The end-of-plan human fallback now has an exact generic evidence path. A
disabled-by-default server hook records real non-bot commands at the `gi.Pmove`
boundary with fixed-point before/after state, contacts, map identity, physics,
and module identity. `tools/humantrace.py` validates and segments those records,
rejecting discontinuities caused by pushers, teleports, hooks, or server-side
effects. Normal proof-backed RUNE self-assembly remains the primary path and
must run unchanged through every generic movement and mechanism pass. Only
after that assembly, run a topology-consistency audit: whenever BSP-backed
walkable-space sampling says two local spaces connect but the RUNE places them
in separate regions, exact local Pmove must prove or refute the missing boundary
edge. This is coverage validation, not pathfinding, and should catch open-room
and corridor omissions before fallback routing. If audited assembly is still
exhausted, the authenticated relay, core, reverse-boundary, SWIM, and
current-identity learning inventories run to their deterministic bounds. If
closure remains open after those inventories, the production generator does
not enter the former fair any-region prover. It validates and immediately
publishes the retained objective union as `local_only`: every prior
oracle-proved merge whose endpoint remains routable to either flag is retained,
neutral-only material is removed, and no new edge is added. Validation,
allocation, owner-contract, and publication failures are fatal and never
publish a partial artifact. This direct post-inventory contract is
deterministic and clock-independent; it replaced the 8,192-, 4,096-, and
1,024-rejection numeric experiments after all three timed out on `lmctf06`
without reaching truthful publication. Pathfinding alone never publishes a
link, and proximity to a blocked frontier is not evidence that the route is
missing. Human evidence identifies
the missing generic transition but does not authorize a RUNE link until the
corresponding oracle replays and proves it. Current verified or pending
route-only candidates are `lmctf01`, `lmctf06`, `lmctf12`, `lmctf15`,
`lmctf19`, `lmctf25`, `tomb05`, `xmap13`, `xmap18`, and `xmap26`. The final controller summary is
authoritative. Each route-only map must still prove normal local attack and
defense in a real match. Later exact human follow-up is required only for
transition classes not already exhausted by supplied traces.

1. Finish the source-owned graph repairs above, including every
   failure found by the diagnostic run.
   The topology audit completed on `lmctf12`; the completed-graph fair repair
   retained eight exact merges and reached its explicit proof budget without
   closure. Retain its verified route-only classification and collect a new
   source-bound human run for the exact-proof update path rather than expanding
   automated search.
2. Build and verify one new exact source, module, configuration, engine, reader,
   linter, semantic-checker, and BSP snapshot.
3. Restart the controller from an empty run root with durable isolated output,
   disjoint worker ports, ten bounded workers, and per-map timeouts.
4. Require every map to produce:
   - a new RUNE;
   - the matching SNAG declaration, including authenticated zero repairs;
   - two valid objective roots;
   - an authenticated complete or local-only route contract;
   - a clean generator shutdown;
   - acceptance by both C readers and the Python reader;
   - strict root-aware lint acceptance for complete artifacts, or union-core
     lint acceptance and a distinct route-only classification for local-only
     artifacts;
   - every applicable map-specific semantic check;
   - a fresh-process cold load with an admitted bot.
5. Require all 175 artifacts to be newly generated from the same freeze,
   structurally valid, accepted by every reader, lint-clean, and cold-loadable.
   Every complete artifact must retain the strict two-objective rule. Every
   local-only artifact must retain exactly the proved reverse-objective union,
   expose a finite local fallback for every live seed, and remain visibly
   classified apart from ordinary PASS. Each map must then have either a
   route-complete PASS or documented real-match proof for the route-only
   release rule. Any other generation failure,
   timeout, lint failure, reader disagreement, semantic failure, cold-load
   failure, or gameplay defect blocks completion.
6. Repair any new failure at the source, data, or tool boundary that owns it,
   then refreeze and restart every artifact invalidated by that repair.
7. Freeze the final 175-artifact manifest and its evidence hashes.

Current implementation checkpoint: route-contract encoding, reader agreement,
union pruning, objective-root authentication, local-field construction, runtime
fallback, and distinct `ROUTE_ONLY` controller classification are implemented.
The generic hook-fling contract, replay/oracle layers, and live executor are
implemented. The former completed-graph fair selector remains covered as a
standalone diagnostic, but production no longer invokes it after authenticated
inventories remain open. Cumulative exact bridge retention, fail-closed local
union publication, and direct post-inventory `ROUTE_ONLY` classification are
implemented. The pre-Dijkstra topology audit is
implemented and verified on `lmctf12`: exact water seams reduce its SCC count,
but the ordinary long corridor is not a missing local flood seam. Its prior
exact late bridges remain valid retained input to the current local-union
contract, and human evidence remains optional follow-up rather than publication
authority. The
fresh `lmctf12` route-only artifact passes both C readers, the Python reader,
lint, and cold load with 1,972 seeds and 31,086 links; all eight exact late
bridges survive into the local-only artifact. Source-bound human dry
RUN/waypoint nominations, strict loading,
fresh-coordinate remapping, exact generator reproving, cumulative local-only
updates, and atomic source-locked publication are implemented. Legacy unbound
traces can be recovered only under explicit `posthoc-identity-exact`
provenance when map, BSP, entity, physics, and current `local_only` RUNE
identity all match; recovery remains nomination-only and every accepted edge
still requires exact in-engine proof. `lmctf12`'s 47,110-step legacy trace has
two bounded complete route windows, ample dry evidence, and ten strict legacy
hook groups. A fresh current-contract 1,972-seed/31,086-link `local_only`
artifact now passes generation, both C readers, Python, lint, and a fresh cold
load. Both recorded route windows were recovered and exact-applied against that
source. The first emits ten missing dry nominations; the unchanged in-engine
oracle rejects all ten and preserves the byte-identical source artifact. The
second emits no missing dry or hook nomination. Both playthroughs are exhausted
for initial release. `legacy-combined-build`
now deterministically emits dry and strict legacy-hook nominations in one
source-bound format-2 artifact, so both evidence classes can be exact-applied
in a single update without invalidating the source identity between them. A
fresh `tomb05` local-only artifact passes both C readers, Python, lint, and cold
load. Its human last mile is hook-specific. Four bounded human traversal
windows have been preserved. The first two each produced one exact-source
hook nomination; both were honestly rejected by the unchanged in-engine hook
oracle and added no edge. The third window emits 153 nominations; the exact
oracle rejects all of them and again adds no edge. The fourth remains queued
for the post-contract source refreeze. Passive v2 hook
telemetry, bounded
one- and two-rope nominations, typed format-2 loading, and exact HOOK/CHAIN_HOOK
update owners are implemented. The explicit legacy fallback passes 23 focused
tests and identifies 155 exact pull samples plus 16 triangulated fixed-bite
groups across the supplied tomb05 flag traversals. The source RUNE is fresh and
the exact update path is active; rejected nominations never become graph edges.
A no-bot human trace binds through a transient
authenticated RUNE load/free without publishing SG state or changing stock
hook control flow. Human hook execution is isolated
from SG ownership: an edict must carry `FL_BOT` and match an active SG roster
entry before any SG hook observer runs, and connection retirement is tested to
precede client-slot reuse. A direct audit against pristine pre-SLIPGATE LMCTF
proves that human fire, sustain, pull, release, re-fire, and pickup collision
paths retain stock behavior while compound hook logic remains bot-owned. The
audit found and fixed one copied-path regression: the human `hook_touch`
null-target guard had been omitted. Focused GNU and Make ownership, live, and
compound suites plus both production compile dialects are green. The pair
installer test also proves
that a later complete RUNE and its matching SNAG replace an installed local-only
pair coherently, while retaining the provisional pair in the rollback backup.
Two lmctf01 human captures are preserved with 89,891 and 37,737 exact Pmove
samples. They predate a usable runtime binding, so the first was split into two
bounded replays and both were recovered against a fresh current-module source.
Before exact proof they nominated 46 and 19 missing dry transitions,
respectively. Exact update rejected all 19 second-run dry nominations. That
temporary-bot result was not retained because the bot changed the ordinary
graph. `sv rune update` now borrows a
resident authenticated source RUNE or transiently loads and later frees one,
so exact learning can run against the no-bot baseline without publishing SG
state or changing gameplay. The uncontaminated baseline reproduced 1,776
seeds and 14,695 retained links. Exact no-bot update accepted one of the first
replay's 45 larger-half nominations; the smaller half's alternate waypoint
added nothing, while the accepted source edge survived exact reproving. The
result remains valid `local_only` with 14,696 retained links, 1,001 red-side
and 425 blue-side seeds, and no shared objective component. Both supplied
artifacts pass both C readers, Python, lint, matching zero-repair SNAG, and live
bot cold load. Both supplied playthroughs are therefore exhausted for ordinary
dry-edge repair; their
remaining useful evidence is mechanism and hook timing.
Bounded dwell remains a declared-mechanism
repair, not a generic flood or Dijkstra relaxation. It remains part of the
active source repairs for `lmctf06` and `lmctf15`. The same authenticated dwell
infrastructure remains valid for `lmctf25`, but exhaustive current evidence
does not produce a publishable train route there. A host-free bounded timeline
reducer now models approach, authenticated activation, trigger delay, active
lease or station wait, egress, completion, and fail-closed expiry. Its GNU and
Make tests pass. A generic relay-wall transaction core also passes both build
systems for lmctf06's 200 ms activation dwell, 4 s lease, crossing, egress,
body-clear restoration, expiry, and source drift. The real game bridge now
executes authenticated dwell and durable restoration through actual
`G_UseTargets`/`DelayedUse` edicts. It proves exact wall, lethal-field, and two-
speaker fanout in both directions, survives bot disconnect, defers restoration
one frame at a time while a body overlaps the wall, fails closed on source
generation drift, and leaves an ordinary human positive-delay target entirely
on the stock path. Both build dialects, the real execution harness, focused
ticket/transaction/live tests, a full production link, and `ldd -r` are green.
Typed catalog recognition, exact ordered
wall, damage-field, and speaker fanout authentication, plan materialization,
both readers, and runtime binding now pass their focused GNU, Make, and Python
gates. The mechanism-contract migration also exposed and fixed an incremental
build dependency error: all production consumers under `slipgate/` now rebuild
when the generated wire contract changes, preventing mixed-contract modules.
The lmctf06 generation-order repair now reuses the topology audit's exact final
SCC labels, rejects stale ordered graph identity, and performs only a bounded
SCC rebuild after an admitted relay edge changes the graph. Focused GNU and
Make topology tests pass, both production links are clean, and the real
controller reaches this handoff immediately with 1,437 seeds and 12,965 links.
The first immutable smoke then reported a 159-node, 44-edge mechanism catalog
but `mechanisms=0 pairs=0 proofs=0 added=0`. Bounded live witness diagnostics
found two exact selector mismatches. The forcefield fanout contains two stock
looping speakers with spawnflag 1, and its `func_wall` retains an inert authored
target even though stock `func_wall_use` never dispatches it. Relay-wall plan,
codec, binding, and Python reader tests now cover both shapes without relaxing
generic door audio or callback, incarnation, timing, and ordered-fanout checks.
A fresh immutable smoke discovers the transaction and reports
`mechanisms=1 pairs=32 proofs=32 added=0`. All 32 physical approaches reject.
Exact contact-height ranking places the nearest stable source about 845
horizontal units from the lower button, while the successful human capture
never enters that button's height band. The relay-wall transaction is therefore
a valid but unrelated BFG shortcut and remains correctly rejected as an
objective repair. Replay-to-SCC grounding instead identifies the ordinary
objective chain as five principal components. The prior complete native run
closed that same chain with four exact reverse-HOOK proofs over existing DROP
or HOOK boundaries. The current raw-order reverse scan is now replaced by a
generic SCC-changing boundary selector: one candidate per SCC pair, native
DROP/HOOK boundaries first, then physical distance and component-gain ranking,
at most 32 ranked candidates, one published native proof, and SCC/objective
recomputation before another edge. No replay-derived or map-specific edge is
admitted. Focused pure and generator diagnostics pass, but the first immutable
run stayed CPU-active in the reverse physical prover until its exact
1,800-second timeout. It produced no artifact and therefore reached no reader,
lint, cold-load, or bot gate. Per-candidate/stage progress plus a real physical
proof bound remains required; the unrelated reverse rocket fallback must not
consume the objective-repair budget after HOOK rejection.
The isolated lmctf15 timed-vault reducer now proves the exact 200 ms physical
activation, one-second relay readiness, nine-second safe lease, ten-second
durable restoration, flag pickup without lease loss, egress, and body-clear
completion. Its typed contract layer now authenticates both button relays, both
rotating-door leaves, the exact one- and ten-second delays, and each identical
ordered fanout of eight START_ON lasers plus one looping speaker. Catalog,
materializer, codec, binding, both C build dialects, and Python reader parity
are green. A host-free game adapter also passes strict GCC and Clang tests for
short/restore delayed tickets, human bypass, the full hold/lease/egress
sequence, disconnect-safe restoration, and body-clear discharge. Its real
edict adapter now enforces HOLD before every `ClientThink` substep, retains the
durable restore obligation across disconnect and authorization failures, and
requires both door leaves to be down plus a clear body sweep before discharge.
The live harness now proves immediate EGRESS pass-through, partial-device-
fanout normalization, the stock human `G_UseTargets`/`Think_Delay` bypass, and
the full durable lifecycle under both GNU and Make. Independent refutation
accepted the runtime with no remaining defect. Both production Make dialects
now link the timed-vault runtime, both `ldd -r` checks are clean, and the shared
mechanism execution harness passes under GNU and Make. The first immutable
real-map smoke still wrote zero activation plans and correctly remained
`local_only`. That run exposed two generator-integration defects rather than a
runtime dwell defect: the catalog sealed while all sixteen `target_laser`
entities were still using their one-second stock initialization callback, and
the declared-link generator had no exact admission path for the button's
relay-plus-two-door fanout. The source repair now preserves the final laser
behavior in the sealed catalog without mutating the initializing edicts and
admits only the exact four-target button shape: two relays at one and ten
seconds plus the ordered two-leaf door. Exact discovery authenticates both
identical nine-target relay fanouts and emits the four directed vault
transactions carrying the 200 ms hold, one-second readiness, nine-second
traversal lease, and ten-second restoration. Focused catalog, plan, objective,
transaction, game, and runtime tests pass under GNU and Make; a new immutable
real-map smoke then completed as `ROUTE_ONLY/local_only` with 1,953 seeds, 172
retained links, 123 mechanism nodes, 98 inventory edges, and zero plans. Both
readers, Python, lint, SNAG, cold load, and admitted-bot gates passed, but this
was not a timed-vault success. The run proved 20,126 links before objective
pruning and retained 20 objective-core seeds; both mirrored buttons produced
five exact wait points, but all 200 TOP-pose egress candidates were rejected
before replay. The exact cause was a proof-scope currentness mismatch: the
generator had intentionally posed the authenticated button and both rotating
door leaves at their cataloged TOP endpoints, while full-closure validation
reapplied the sealed BOTTOM-state check to those same three physical movers.
The focused repair now substitutes their independently authenticated TOP poses
only inside that private synchronous scope; both relays and all nine effects
still require ordinary live execution currentness, and missing or drifting
closure members fail closed. Focused GNU and Make regression and timed-vault
suites plus both production links and unresolved-symbol checks pass. A current-
module physical checkpoint then proved that the exact controller identity and
deep-water command parity were also missing: timed-vault bindings had been
treated as non-button doors, and both generation and live execution emitted a
planar command after the body entered the stock water basin. The shared repair
retains physical-button behavior for controller 10 and switches only its exact
nonhazardous depth-two egress to the existing swim feedback command; ordinary
button doors remain dry-only. Focused GNU and Make catalog, plan, binding,
execution, timed-vault, button, and mover suites and both production links are
green. The real checkpoint still ended the door phase with `125 declared door
links, 0 button-door links (64 wait points, 917 approach/2030 egress trials)`.
The bounded trace showed 75-90 accepted swim commands per candidate in clean
stock water. The body rose from z=-359.875 to at least z=-343.875, aligned
within 5-16 units of the candidate x/y, then fell back to z=-569..-581 and
timed out after 5,000 ms while still at waterlevel one or two. No identity,
currentness, contamination, hazard, fall-damage, or command check rejected it.
This proved that direct aim reaches the ledge column but does not land on the
dry supported endpoint. A focused graph-guided repair now builds the existing
reverse air index from proved water-origin `RL_SWIM` links and, only for a
submerged controller-10 transaction, aims each command at the next indexed
water hop before resuming the exact serialized dry destination after surfacing.
The pure graph regression, complete timed-vault stack, ordinary button-game
regression, mover oracle, both production links, and unresolved-symbol checks
pass under GNU and Make. A fresh immutable current-module checkpoint still
reported the same 1,953 seeds, 1,078 exact swim links, and final `125 declared
door links, 0 button-door links (64 wait points, 917 approach/2030 egress
trials)`. Extending only controller 10 to its authored nine-second lease did
not change that result. An isolated bounded run then proved every submerged
candidate could localize into the reverse index, so missing index coverage was
not the failure. Exact terminal traces instead showed repeated nine-second
timeouts at retained water seeds 1672/1698: the phantom stopped about 63 units
from the fixed next hop on the wrong side of its intervening geometry. A
stateful cursor now holds one selected route, advances only through its
authenticated `RL_SWIM` `next[]` chain, and hands back to the exact planar
destination only after reaching waterlevel one or zero. Its focused regression
also exposed and repaired an initial unproved shortcut by requiring the body to
reach the selected link source before consuming that link. Both dialects and
production links are green, but a fresh immutable checkpoint still produced
the identical 1,953-seed, 1,078-swim-link, `125/0`, `917/2030` result. The
next checkpoint required full player-hull clearance from the wait pose to the
selected graph source, yet again produced the identical result, disproving a
hidden-nearest-source hypothesis. Tightening every pre-link hop from the
ordinary 40-unit destination envelope to a four-unit source capture also left
the result unchanged. A bounded trace finally isolated the real terminal
state: all sampled controller-10 candidates ran the full clean nine seconds,
advanced more than 1,000 units through their authenticated chains, and reached
the neighborhood of retained seeds 1274/1449/1642, but `SG_SwimCommand` kept
driving at full 400 input and approximately 150 units/second. The body therefore
overshot each exact hop instead of settling into the position-and-velocity
state required to begin the next proved `RL_SWIM`. A first controller-10-only
distance throttle did not repair that state: its immutable checkpoint again
ended at `125/0`, `917/2030`. A bounded command trace then proved why. At seed
1674's exact hop the command alternated forever between `(distance=3.973,
speed=6.185, forward=50)` and `(distance=4.052, speed=7.500, forward=0)`;
zero input invokes stock underwater downward drift, while the next distance-
only command returns to the first state. The replacement controller computes a
bounded desired velocity from position error and aims bounded thrust along
`desired_velocity-current_velocity`; it emits zero only when both the four-unit
position and four-unit/second velocity limits already hold. The exact Yamagi
`Pmove` regression recreates the grounded underwater map state and now reaches
`distance=3.973, speed=3.760` in one 25 ms step. Generator and runtime pass the
same live velocity into this law. GNU and Make timed-vault, button-game, mover,
and production-link suites plus unresolved-symbol checks pass; ordinary swim
control, dry shoreline handoff, final supported arrival, and the nine-second
lease are unchanged. Its immutable checkpoint nevertheless ended at the same
`125/0`, `917/2030` line after both mirrored buttons completed all 1,400
grounded trials. This proves the exact-hop limit cycle was real but was not the
only downstream egress failure. The bounded next-state trace never reached a
shoreline or final-endpoint handoff: after the full lease it remained in the
first water-hop stage with encoded source seed `-1676`, zero route transitions,
and no exact capture. Its terminal body was `(59,-2621,-635.25)` moving
`(4.375,-2.5,-0.25)` toward `(64,-2624,-635.875)`, about 5.86 units away at
about 5.05 units/second. The next repair must reproduce and converge from that
exact grounded-water state in the real-Pmove regression. That regression
exposed the four-unit/four-unit-per-second capture as an invented constraint,
not part of the serialized replay contract. The quantization-aware controller
now accepts an eight-unit/eight-unit-per-second source envelope, still five
times tighter than ordinary 40-unit swim arrival, and keeps a minimum 16-unit
desired speed outside it so fixed-point Pmove cannot settle one quantum beyond
the boundary. The traced terminal state now captures immediately, submits the
next authenticated `RL_SWIM`, physically advances under exact Yamagi Pmove,
and captures that next water hop after 39 commands. Merely changing the cursor
was not accepted as proof. Both dialects' focused timed-vault, mover, and
button-game stacks pass. The resulting immutable checkpoint
`/tmp/lmctf15-quantized-capture.zyS7KT` again proved 1,953 seeds and 1,078
exact swim links, but ended the door phase at `125 declared door links, 0
button-door links (64 wait points, 917 approach/2030 egress trials)` after both
mirrored buttons accepted five of 1,400 grounded trials. The 8/8 capture and
immediate next-hop progression are therefore real, but still do not complete a
physical timed-vault egress in the real map. The disposable server was stopped
at that decisive line and its exact PID and snapshot path were verified absent;
no final rune was claimed. The remaining repair must isolate the first state
after the now-proved next-hop capture where the nine-second transaction fails,
then reproduce that state in the engine regression before another full
checkpoint. The final combined smoke remains, and lmctf15 is not complete
before those proofs. A corrected one-candidate trace in the actual generation
egress seam then disproved a new fixed point or route cycle. The body entered
the authenticated water route at 1,375 ms with source seed `-1676`, captured
four successive 64-unit `RL_SWIM` hops at 3,250, 4,600, 6,050, and 7,550 ms
through seeds 1674, 1646, 1573, and 1451, and remained on that same monotonic
chain. At the exact 9,000 ms lease boundary it was 7.9 units from the fifth hop
at `(64,-2368,-635.875)`, still in clean waterlevel three, with body
`(61.375,-2375.5,-635.25)` and velocity `(3,10.75,-0.5)`. The failure is now
classified as total lease consumption by per-hop settle time, not unreachable
geometry, stale identity, contamination, a loop, or another fixed point. The
next engine regression must reproduce this complete five-hop sequence and
reduce its physical traversal time enough to leave a proved shoreline/final-
arrival budget inside the immutable nine-second vault lease; extending that
authored lease is not an admissible repair.
The exact five-hop engine RED then showed that the settle tax was self-imposed:
the initial off-seed source localization needs the strict 8/8 capture, but each
subsequent authenticated `RL_SWIM` already owns the ordinary 40-unit replay
arrival contract. With 8/8 limited to initial localization and the monotonic
cursor advancing ordinary proved hops at that existing radius, the captured
five-hop sequence reached dry supported final at 6,800 ms and left 2,200 ms of
the unchanged lease. Both dialects' complete timed-vault/button focused stacks,
forced production builds, and unresolved-symbol checks passed. The immutable
acceptance snapshot `/tmp/lmctf15-hop-accept.IrFPGZ/snapshot` nevertheless
reached the same real-map terminal line: `125 declared door links, 0 button-door
links (64 wait points, 917 approach/2030 egress trials)`. Thus the production
40-unit hop law is proved and the synthetic five-hop endpoint is insufficient
to represent the remainder of lmctf15's selected real chain. No mechanism link
or plan is accepted from this run; the next bounded trace must capture the exact
furthest seed and shoreline/final state under the 40-unit law before changing
the controller again.
That bounded trace found seven monotonic authenticated transitions, not a loop:
after the initial exact source capture at 3,250 ms, it advanced through seeds
1646/1573/1451/1276/1037/738 at 3,575/4,475/5,400/6,325/7,225/8,150 ms.
At lease expiry it remained in clean waterlevel three at
`(60.625,-2219,-635.25)`, moving `(4.75,71.375,-0.75)`, 43.1 units from the
next `(64,-2176,-635.875)` water hop. The first 40-unit repair changed only
cursor advancement: it incorrectly left `exact_capture` true after source
authentication, so every ordinary `RL_SWIM` still paid for the velocity-settle
command controller. The next RED must extend the real-Pmove chain across these
eight water nodes to a dry shoreline, and the repair must return authenticated
chain hops to the ordinary swim command while retaining 8/8 only for the
initial off-seed localization.
The exact eight-water-node plus dry-shore real-`Pmove` regression then failed
under that residual per-hop command mode before the unchanged 9,000 ms lease.
The causal split is now explicit: only `route_seed < -1` initial source
authentication uses the strict 8-unit/8-unit-per-second capture controller;
once the cursor owns an authenticated seed, every `RL_SWIM` hop uses ordinary
`SG_SwimCommand`, the existing 40-unit replay arrival envelope, and monotonic
cursor advancement. The same eight-node regression now reaches dry supported
shore inside the unchanged lease. Complete timed-vault, mover, transaction,
runtime, and ordinary-button suites pass under GNU and Make; forced production
builds in both dialects link with clean `ldd -r`, the tree passes
`git diff --check`, and `sg_rune.c` remains 9,997 lines. The resulting immutable
acceptance snapshot `/tmp/lmctf15-initial-only-40.QEJwuP/snapshot` still ended
the real door phase at `125 declared door links, 0 button-door links (64 wait
points, 917 approach/2030 egress trials)` after 1,953 seeds, 534 water seeds,
and a 123-node/98-edge mechanism catalog. This is a failed acceptance: it wrote
no controller-10 mechanism link or plan. The controller and server process
group were stopped at that decisive line, both exact PIDs were verified absent,
and the private path and port were clear. The corrected synthetic timing law is
therefore proved, but the real generator still rejects a downstream physical
predicate before timed-vault materialization; another full map run is not
justified until a bounded real-seam trace identifies that predicate.
That bounded, `sg_debug`-gated trace is frozen at
`/tmp/lmctf15-egress-reason.MX4NzD/snapshot`; its temporary diagnostics were
removed and the shared oracle recompiled before execution. It recorded exactly
200 controller-10 egress calls: 100 carried/rider trials rejected immediately
for support mismatch, while all 100 static-support trials consumed the full
9,000 ms and timed out. Every static timeout remained uncontaminated in clean
waterlevel three; none failed source localization, command emission, water
safety, falling damage, identity, or cursor monotonicity. The first ended at
seed 1449, body `(-63.125,-1817.375,-635.5)`, velocity
`(-1.25,152.875,-17)`, 676.548 units from target
`(0,-2432,-359.875)`. The closest still ended at seed 1449, body
`(-63.125,-1817.25,-635.625)`, 508.665 units from target
`(0,-2240,-359.875)`. The rider half is invalid but is not the zero-link
blocker because the static half enters and follows the water graph normally.
The remaining mismatch is now localized to target-independent route choice:
`SG_WaterEscapeIndexBuild` roots every dry seed, and
`SG_TimedVaultEgressSourceSelect` chooses only the nearest reachable water
source without considering the requested final dry target. It can therefore
authenticate an escape tree to an arbitrary shoreline, then spend the authored
lease swimming past the actual target before any planar handoff. The diagnostic
process group was stopped at the aggregate line and both exact PIDs, its path,
and port were verified clear. The next RED must show the selector preferring a
nearby wrong-shore tree over a target-facing authenticated tree; any repair
must choose among existing proved links using the final target and then pass a
real-`Pmove` route replay inside the unchanged lease before another map run.
That selector regression failed exactly as predicted: the old nearest-source
law chose the reachable water source whose authenticated chain ended at the
wrong shoreline, even though another reachable source owned an existing chain
ending at the requested side. The engine RED repeated the captured real-map
entry state at `(1,-2619.375,-580)` with velocity
`(10.125,14.625,-585.5)` and selected the same wrong branch. Selection now uses
one deterministic score comprising physical distance to the reachable source,
geometric length of its existing `next[]` `RL_SWIM` chain, and the chain's
authenticated dry endpoint distance to the requested final target. It does not
add an edge, claim endpoint visibility, alter the chain, or relax any source,
tombstone, loop, hull-reachability, or initial 8/8 capture check. The pure
selector test and captured-state real-`Pmove` replay now choose the target-facing
chain and reach its dry shore inside the unchanged 9,000 ms lease. Complete
timed-vault, mover, transaction, runtime, and ordinary-button suites pass under
both GNU and Make. The immutable acceptance run from the combined snapshot
`/tmp/lmctf06-budget1024.1IBiqq/snapshot` failed at the unchanged decisive
line: `125 declared door links, 0 button-door links (64 wait points, 917
approach/2030 egress trials)`. It therefore accepted no timed-vault mechanism
link or plan. The exact controller/server process group was stopped after that
line; PIDs 2760121/2760174, port 64815, and the run path were verified absent
while its evidence remains at `/tmp/lmctf15-target-shore.B4RmWW`.

This immutable failure refutes target-aware ranking among the already-collapsed
`next[]` trees as sufficient. `SG_WaterEscapeIndexBuild` is a reverse BFS rooted
at every dry seed and retains only the first shortest-hop successor from each
water seed to any dry seed. The later selector can rank sources and their one
retained endpoint, but it cannot recover another authenticated outgoing
`RL_SWIM` branch discarded by that global any-shore collapse. Before another
map run, a focused RED must prove that exact case: one reachable source with a
shorter wrong-shore branch and an existing target-facing branch. The repair
must choose a deterministic target-specific path over the full existing
`RL_SWIM` graph, remain acyclic and monotonic per transaction, add no edge, and
pass the captured-state real-`Pmove` replay inside the unchanged 9,000 ms lease.
That full-graph RED failed under the generic any-shore index: it retained the
shorter wrong-shore successor and discarded the longer existing target-facing
branch from the same source. The timed-vault-specific index now runs a
deterministic weighted reverse search over only existing authenticated
`RL_SWIM` links, initializing each dry endpoint with its geometric distance to
the requested final target. It retains the successor chain with minimum graph
cost plus endpoint distance; generic water-index callers are unchanged. The
two-branch unit is green, and the captured real entry state reaches
target-facing dry support under real `Pmove` before 9,000 ms. Both dialects'
focused stacks, forced production builds, unresolved-symbol checks, and diff
check pass; `sg_rune.c` remains 9,997 lines.

The immutable acceptance snapshot
`/tmp/lmctf15-target-graph.HJc9ia/snapshot` nevertheless reached the same
decisive line: `125 declared door links, 0 button-door links (64 wait points,
917 approach/2030 egress trials)` after 1,953 seeds and 534 water seeds. This is
a failed acceptance, not a completed map. The controller/server process group
was stopped at that line; exact PIDs 2769353/2769411, port 64915, and the run
path were verified clear while the log remains under
`/tmp/lmctf15-target-graph.HJc9ia/run`. This refutes both collapsed-source
ranking and full-graph target-specific branch choice as the final materializer
predicate. Another full map run is not justified until one bounded
real-candidate trace reports the first post-egress predicate that rejects a
target-specific dry-supported arrival.
That bounded trace is frozen at
`/tmp/lmctf15-post-arrival.Bb0Hax/snapshot`; its temporary diagnostics were
removed and the normal oracle rebuilt before execution. Of exactly 200
controller-10 trials, 100 rider trials rejected the known support mismatch and
all 100 static-support trials reached dry ground but first rejected the
horizontal final-arrival radius: `xy=100`, with zero precondition, member-scope,
initial-state, command, contamination, water-safety, fall, no-dry, sweep,
height, hazard, trace, other, or success outcomes. The first recorded failure
used route seed 1449; by the 9,000 ms timeout it was re-submerged at
`(-56.125,-1765.375,-644.375)` while the requested target was
`(0,-2432,-359.875)`. The exact diagnostic process group and port 64965 were
verified clear after the summary.

The shoreline-handoff defect is now covered and repaired. The timed-vault-only
target index retains an authenticated dry `RL_RUN` suffix after the selected
`RL_SWIM` branch instead of discarding the graph at `waterlevel < 2` and
commanding a direct cross-water leg. The pure RED showed the old index stopping
at the first shore. The real-`Pmove` RED reproduced the captured dry-endpoint
horizontal failure; GREEN follows the existing dry corridor to the requested
final seed inside the unchanged 9,000 ms lease. Generic water-index callers are
unchanged, no edge or visibility claim was added, and both dialects' complete
timed-vault/button focused stacks, forced production builds, unresolved-symbol
checks, and diff check pass.

The resulting immutable acceptance snapshot
`/tmp/lmctf15-dry-suffix.yswUrZ/snapshot` still reached the same decisive
real-map line after 1,953 seeds and 534 water seeds: `125 declared door links,
0 button-door links (64 wait points, 917 approach/2030 egress trials)`. This is
a failed acceptance, not a completed map. The controller/server process group
was stopped at that line; exact PIDs 2809064/2809219, port 65015, snapshot path,
and process path were verified clear while the preserved log remains under
`/tmp/lmctf15-dry-suffix.yswUrZ/run`. The dry planar suffix is a real controller
repair but is not the remaining real-generator admission failure. No additional
selector or path change is justified without a new bounded predicate trace.

The bounded controller-10 route trace at
`/tmp/lmctf15-route-proof.ZiefJA/snapshot` identified that predicate before
any route execution. All 100 static trials built their target index, but none
of the indexes contained a dry suffix and every source selection failed:
`target-suffix=0/100 reachable-suffix=0/100 source=0/100
selected-suffix=0/0 shoreline=0 final=0 transitions=0 dry-consumed=0`.
Each static replay subsequently rejected `reason=route` at 1,375 ms with
`route_seed=-1`; the 100 rider variants retained the known support-mode
rejection. The exact process group and port 65115 were verified clear, and the
temporary diagnostic was absent from shared source before execution.

A second read-only pre-prune graph audit at
`/tmp/lmctf15-graph-audit.nhhROD/snapshot` proves this is not an omitted action
in the target index. Each mirrored target is a ten-seed RUN-only component
with 86 incoming and 86 outgoing RUN links. The negative side's nearest proved
water-exit component has 57 seeds at exit 854 and is 696.689 units from target
seed 162; the positive side's has 50 seeds at exit 930 and is 700.577 units
from target seed 370. Direct player-hull BSP traces block almost immediately,
no action crosses either pair of components, and an exact-cost search over all
already-proven actions reports no path from any water exit to either target
component. The audit process group and port 65215 were verified clear, and its
temporary source was removed before launch.

The agreed second-to-last-resort region bridge cannot be published safely for
this map.
`SG_RuneLatePathSelect` is a pure, test-only direct seed-pair selector with no
production caller; it neither creates an intermediate waypoint chain nor
publishes one. The production flood-contact reconciliation runs only after
base proving and door restoration, owns generic RUN/JUMP/SWIM/DROP proofs, and
has no timed-vault member scope. Invoking the selector while the vault is open
could prove component-to-component segments, but the current wire/runtime has
no controller-private multi-segment path representation: ordinary RUN links
would incorrectly remain usable while the vault is closed, while one
BUTTON_DOOR record has only its existing control vectors and represents one
transaction. Adding an authenticated timed-vault-owned multi-segment
wire/runtime path would be a new serialization architecture rather than a
repair to a missing BSP edge. That work is not required by the route-only
release contract and is closed for initial publication; the audited gap may
not be published as a direct edge. The unconsumed dry `RL_RUN` suffix extension
and its corpus claims were pruned. The earlier target-facing water search
remains restricted to existing authenticated `RL_SWIM` links, and its generic
initial-capture, monotonic-cursor, lease, and ordinary shoreline-handoff
regressions remain green.

The final immutable direct-local publication at
`/tmp/lmctf15-route-only.xu08Cq` classifies lmctf15 as
`ROUTE_ONLY/local_only` with no failure line. It writes 1,953 seeds, 172 links,
123 mechanism nodes, 19 activation triggers, 98 inventory edges, and zero
activation plans; roots 16 and 89 each retain their authenticated ten-seed
local component with zero shared seeds. GNU and Make C readers and the Python
reader agree on every count and the route contract. Graph-contract lint,
semantic gates, matching SNAG bootstrap, and fresh-process cold load pass; the
cold loader reports a ready local-only RUNE with all fields up and admits four
bots. The generation and cold-load processes exited cleanly with no remaining
path or port match. lmctf15 is therefore the tenth route-only map: ordinary
attack and defense move to the final fleet phase, while a complete flag
traversal may be learned from later human play without blocking release.

The lmctf25 continuous-station transaction, plan, and runtime are source-complete. Its
catalog and plan authenticate the exact synchronized START_ON train team, the
closed fourteen-corner route, opposite stations 28/35, and the only two authored
3000 ms waits. The passive runtime supports both station directions, owns exactly
four bot command substeps, never mutates either stock train, and consumes a
selected controller-11 link fail-closed rather than falling through to generic
train movement. Exact BSP and stock-movement regressions prove the asymmetric
904/956-unit first legs, 23/24-frame schedules, and 84/86-frame half-routes.
Each train retains independent sealed generation, team, route, target,
predecessor, pose, velocity, callback, residual-distance, and `nextthink`
authentication; only the selected physical train drives boarding, its exact
30-frame source/destination dwell, ride, body-clear, and egress. Zero-wait
fabricated stops, recycled edicts, early/late departure, wrong-train boarding,
off-route or equal-length false segments, overspeed, delayed or due-now
callbacks, and identity drift all fail closed. Independent refutation and an
exact BSP/stock audit accepted the final adapter. Focused transaction, game,
catalog, plan, binding, codec, execution, and integration gates pass under GNU
and Make; both production links and `ldd -r` are clean. Contract generation,
the 59-test artifact/reader suite, and the fresh private-Python-runtime tests
are green. The first immutable real smoke produced a valid
2,122-seed/13,566-link `local_only` artifact, but its 238-node/121-edge
mechanism inventory materialized zero plans. The catalog retained both authored
3000 ms station waits. Exact `RL_TRAIN` controller-11 discovery and game
binding are now production-linked; candidate tests pass under GNU and Make,
Python wiring passes, both full builds link, and `ldd -r` is clean. The first
current-module immutable smoke still writes zero plans, proving that the exact
real-entity station candidate fails closed before link append. Bounded live
diagnostics now identify boarding as the first failed predicate: one exact run
reported `station candidates=1 directions=2 sources=2858 board=0/113 carry=0/0
egress=0/0 appended=0`. Representative boarding probes completed all 155
candidate/jump paths for the full 3000 ms with zero command, water, or
contamination failures and zero train-support samples. They reached the first
outside waypoint but never advanced toward the interior. The defect was a
stateless waypoint selector that reverted to the first stage as soon as Pmove
momentum carried the body more than eight units beyond it, compounded by trying
only the source-nearest carriage side. Focused RED/GREEN tests now cover a
stateful, non-reverting path cursor and deterministic bounded routes around all
four expanded carriage sides; BSP/Pmove and exact train support remain the only
admission authority. A follow-up immutable probe rejected the low-floor seed
band as physically more than 200 units below the train top, then exercised six
same-height sources. Each still produced zero support. The cleanest source
completed all 620 routes with no command, contamination, or water failure but
stopped against world geometry before its first carriage-side waypoint; routes
around that obstruction fell to the lower floor and failed closed. The remaining
boarding repair therefore needs a waypoint nominated from the existing proved
dry graph and checked by BSP/Pmove, not another assumed bbox-side path. A fresh
bounded RED/GREEN repair now implements that contract. Generation considers
only existing direct `RL_RUN` links with proved provenance, positive cost,
stable dry endpoints, no mechanism plan, and no stored detour waypoint. It
replays the identical planar controller from the original source to the dry
approach, requires a clean supported arrival, and continues from that exact
Pmove state into the existing BSP-authoritative four-side boarding proof. The
controller-11 link stores the proved dry approach in `anchor` and the distinct
train contact in `mechanism_anchor`. Runtime commands the same approach during
the wait phase, does not command boarding until supported arrival, and then
continues through the existing dwell, ride, and egress transaction. Non-finite,
source-aliased, boarding-aliased, absent, waypoint-bearing, non-proved, wet, or
mutated approach evidence fails closed. Binding snapshots both coordinates;
the native codec and strict Python reader authenticate their distinct roles,
and the reader now validates controller 11's exact two-train, fourteen-corner,
two-3000-ms-station closure instead of rejecting the controller as unsupported.
Focused candidate, candidate-game, plan, transaction, runtime, binding, codec,
catalog, execution, Python artifact, tool-reader, and integration suites pass
under GNU and Make. A strict `-Werror -Wpedantic` oracle compile, both full
production links, and `ldd -r` are clean; `sg_rune.c` remains 9,998 lines. A
fresh immutable diagnostic replayed the graph-guided source repair into the
boarding oracle. It was stopped after 20 complete independent boarding proofs
because every proof exhausted all 620 bounded paths with zero train-support
samples; completed-path counts ranged from 411 to 620 and every result remained
`support=0 ok=0`. This proves the serialized dry source-to-approach phase is
active and replayable, but it does not yet produce a physically supported train
contact. The next repair is therefore confined to the final approach-to-contact
geometry and support transition; repeating the remaining identical attempts
cannot admit a plan. A second bounded RED/GREEN repair removed raw-link-order
starvation: it deduplicates dry approach endpoints, keeps the deterministic
cheapest incoming proved RUN, ranks the unique endpoints by exact distance to
the train's XY support footprint plus feet-to-top error, and caps boarding at
32 endpoints per direction. Both dialects, strict readers, full production
links, and unresolved-symbol checks pass. The fresh immutable ranked checkpoint
was stopped after five complete unique-endpoint proofs and one partial proof.
Every complete proof again exhausted 620 paths with zero support, and the clean
paths converged to the same outside point `(336.125,-144.125,126.375)` rather
than entering the train footprint. Ranking is therefore valid bounded
infrastructure but not the missing physical transition. The next repair must
derive and replay an actual BSP-valid approach-to-XY-overlap transition; it may
not weaken stock collision, Pmove, or the exact support predicate. A bounded
diagnostic tested the remaining planar hypothesis with a 16-unit cardinal
player-hull search out to 640 units. The immutable combined-module run reached
`station candidates=1 directions=2 sources=36090 board=0/0 carry=0/0
egress=0/0 appended=0`: no ranked dry source has a same-height BSP-valid route
into the posed train-support rectangle, so exact boarding Pmove never became
eligible. The search wave was then pruned from production, runtime, build
rules, and tests because it publishes no link for any current corpus map. The
independently exercised controller-11 transaction, dwell, binding, serialized
approach, and passive runtime remain. lmctf25 is now classified under the
agreed route-only release rule: its ordinary RUNE remains releasable for normal
attack and defense without a fabricated flag route, while future human
learning and a route-only real match supply the missing traversal evidence.
lmctf06 and lmctf15 now have accepted immutable `ROUTE_ONLY` artifacts with
both readers, Python, lint, semantic, SNAG, and cold-load evidence. No map in
the former ten-map queue remains a source-owned blocker. The 175-map freeze and
all required route-only real-match evidence remain pending.

Final-tree hygiene now has executable coverage for the passive human hook
trace writer, including no-bot transient RUNE binding and ordered fire,
attach, and release records. The chain-hook frontier's eligible-pair budget
contract is part of both rune-update gates. The production private-Python
runtime builder is documented and its real link-free output is tested with the
corpus controller. These focused targets pass under GNU and Make.

The final combined source passed the complete local pre-integration matrix:
GNUmakefile and Makefile with both GCC and Clang, including `all` and the full
`host-test` suite. All four warning scans were empty, all four modules passed
`ldd -r`, the generated action contract was current, both deslop gates reported
zero findings, and the worktree and cached diff checks were clean. That source
was committed, merged, and pushed with `main` and `slipgate` at the same merge
commit. Exact CI on both refs then exposed eight MSVC warning locations and a
hosted-runner Python-runtime-builder test that assumes `/usr/bin/python3` has
the required extension modules. The warning-only repairs preserve runtime
semantics. CI now provisions a pinned standard CPython 3.14 whose separately
manifested extension layout satisfies the unchanged fail-closed private-runtime
contract. Two independent reviews found no semantic defect, the exact cached
Python artifact built a private runtime under Ubuntu 24.04, and the complete
GNUmakefile/Makefile by GCC/Clang local matrix again passes with zero warnings
and clean `ldd -r`. The synchronized exact-CI rerun passed both Windows builds
and the packaged Linux module, then all four host jobs found one remaining
portable-runtime assumption: the controller required `libzstd` even when the
selected Python build neither links nor loads it. The builder already copies
the actual loader-reported dependency closure, and preflight rejects every
loaded library outside the immutable manifest. A failing portability regression
now covers a valid runtime without optional `libzstd`; removing only that fixed
marker passes the complete 66-test controller/runtime/workflow suite while
retaining the hostile-library rejection gate. The repeated full
GNUmakefile/Makefile by GCC/Clang local matrix is green with zero warnings and
clean `ldd -r`; exact CI must still be repeated for this repair.

The detached-final-commit 175-map freeze remains pending. A readiness audit
also found that both tracked validation configs execute an untracked
`rune.cfg`, while the controller snapshots exactly one generator config. Before
the final snapshot, the approved generator config must be standalone and
snapshotted directly as `generator_config@game/rune.cfg`; no staged config may
depend on another absent config file.

## Real-match validation

Run ordinary matches with the final module and RUNEs. The supplied map list is
only the schedule.

`tools/fleet-runner.py` now owns the authenticated persistent ten-lane cycle,
and its exact companion reducer publishes the 210 frozen residence receipts.
Both build dialects exercise a native fake-engine cycle with one unchanged
engine generation per lane, offsets 0 through 9, native wrap, pinned clients,
complete POV/serverrecord lifecycles, a hash-chained ledger, and independent
stopped-process verification. Production evidence still requires running the
same boundary against the installed final bundle; the fake-engine test is not
match evidence.
`tools/server_bundle.py` now builds and verifies the exact 175-map archive and
release manifest, installs one frozen content-addressed generation, commits one
active-state file, and switches to the retained rollback generation. Focused
GNU and Make tests assemble a complete synthetic 175-map bundle, reject alias,
archive, partial-generation, runtime-copy, and stale-state drift, recover from
an injected pre-commit failure, and prove exact rollback. The fleet runner
binds the active bundle ID into all 210 receipts and requires every runtime
module, config, maplist, BSP, RUNE, and SNAG copy to match its installed role.
The final production bundle still needs assembly from the accepted 175-map
freeze and a real install before matches. Do not substitute development-era
iteration or deployment scripts for these commands.

A stale systemd `wavewatch` instance previously launched development
`waveloop.sh`, deployed an old module to `lmctf-hooktest`, and then failed the
current mechanism preflight. Its intended stop sentinel was created, its owned
process group was terminated, and no process remained. The production runner
now rejects a development-controller environment and never calls or relies on
`wavewatch`, `waveloop`, `iterate2.sh`, or their process discovery.

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
commit and synchronize the locally proven exact-CI repairs
  -> pass exact CI on both synchronized branches
  -> create a new source, module, and input freeze
  -> restart and structurally validate all 175 RUNEs
  -> prove route completion or the route-only real-match rule for each map
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
- [x] Remaining graph blockers repaired and integrated.
- [ ] New final combined source, tool, module, and input freeze.
- [x] Immutable server-bundle assembly, verification, atomic activation,
      installed identity, failure recovery, and rollback tooling.
- [ ] 175 newly generated, structurally accepted RUNEs; each route-complete or
      backed by real-match proof of the route-only release rule.
- [ ] Real-match behavioral validation with ordinary map-list inputs.
- [ ] Match-exposed defects repaired and revalidated.
- [ ] Final compiler, Make dialect, platform, and repository gates.
- [ ] Documentation, version, branch integration, tag, release, and hash audit.
