# LMCTF BuzzMod project completion plan

This plan tracks work still required. Git history and immutable run records hold
superseded experiments and detailed evidence.

## Completion definition

The project is complete when one unchanged source commit satisfies all of these
conditions:

1. Exact CI is green on synchronized `slipgate` and `main` branches.
2. One immutable snapshot binds the final source, tools, runtime, and 175
   canonical BSPs. It contains no pre-rewrite RUNE candidates.
3. Generate and accept all 175 RUNEs from the unchanged frozen build.
4. Every accepted RUNE passes both C readers, the Python reader, lint, applicable
   semantic checks, matching SNAG validation, and a fresh-process cold load.
5. Every map is route-complete or satisfies the route-only release contract.
6. The authenticated production bundle is installed and verified.
7. Ordinary matches prove the required bot behavior on the installed bytes.
8. The release tag points to the unchanged frozen commit. Downloaded public
   assets pass version and integrity checks.

## Current status

| Area | Current state |
|---|---|
| Code wave | The generalized 3D standing/crouched scan, contact-safe seed identity, exhaustive hook frontier, BSP contact repair, door-family exhaustion, overflow propagation, and controller/finalizer repairs are implemented. The full GNU host gate passes. |
| Route search | Dijkstra is production-wired after ordinary and BSP-aware closure. The old call and rejection budgets are gone. Focused tests prove closure or a complete no-progress `open-exhausted` search. Final real-map proof, review, and host gates remain. |
| Movement generation | Rocket-jump runtime support remains. LMCTF generation omits rocket-jump discovery because hook traversal owns it. The repaired searches exhaust finite candidate sets and fail closed on capacity or allocation. |
| Corpus | `tools/rune-corpus-maps.txt` defines 175 canonical maps. The generator rewrite invalidates the old RUNE corpus, so the new freeze adopts none and generates all 175. |
| Controller | Adoption, crash recovery, provenance, one-shot replacement, and unbounded review are implemented. An adopted `local_only` artifact must be regenerated. A `ROUTE_ONLY` result needs a current generated artifact and an authenticated log that proves BSP reconciliation ran before a complete no-progress late-path search. Production generation and review have no timeout. |
| Diagnostic reruns | The final corrected generator produced a route-complete `lmctf12` RUNE. Both C readers, the Python reader, lint, exact SNAG binding, and a fresh-process cold load pass. The full corpus run will repeat it from frozen bytes. |
| Freeze and CI | The `0988f47` freeze and every pre-rewrite RUNE are invalid. The repaired commit needs fresh exact CI on synchronized refs before the immutable snapshot is created. No final freeze or 175-map corpus exists. |
| Bundle, matches, release | Focused bundle and fleet tests pass. Production installation and matches have not started. `v1.0.0` is not tagged or published. |

## Fixed authority and policy

### Corpus and configuration

- `tools/rune-corpus-maps.txt` is the sole authority for the 175 maps.
- The durable asset set has 180 BSPs. The manifest excludes unsuffixed
  `lmctf05`, `smap31`, `xmap07`, `xmap11`, and `xmap14` because suffixed variants
  are canonical. The unsuffixed bases are out of scope; every listed variant
  remains in scope.
- Build the frozen BSP set by iterating the manifest. Do not copy all 180 BSPs.
- `tools/topmaps.txt` is an ordinary schedule list. It has no completion or
  release authority.
- Snapshot `tools/rune.cfg` as the generator configuration. Validation and match
  overlays cannot replace it.
- Use `tools/route-only-match.cfg` only for ordinary route-only matches. The
  production bundle includes the empty `tools/route-only-maplist.txt` so LMCTF
  cannot use an ambient map list.
- Preserve legacy LMCTF gameplay behavior. Change it only for requested bot or
  telemetry work.

### RUNE generation

- The generator rewrite invalidates every old candidate. Do not include old
  RUNEs in the snapshot or adoption queue.
- Generate every manifest map from the unchanged frozen module and tools.
- Failed infrastructure, lifecycle, observation, or host checks retry without
  consuming or replacing an old artifact.
- Generation attempts may resume in the same run root after non-accepted
  attempts.
- Production generation and review have no elapsed-time deadline. A deliberate
  controlled-run safety override must be fingerprinted. The acceptor's
  `--contracts` metadata probe may use a justified 300-second handshake bound,
  but that bound does not apply to generation or review. Other bounds cover only
  authenticated startup/readiness or owned-child teardown.

### Distribution scope

- Publish the supported Linux and Windows modules, `VERSION`, `SHA256SUMS`, and
  the tracked `assets/lmctf6-buzzmod.pak`.
- The public PAK contains static scoreboard art and sounds, not generated
  navigation data.
- Do not publish separate generated-RUNE downloads unless the release scope
  explicitly adds them. The generated corpus and production bundle remain
  authenticated release evidence and install inputs.

## RUNE acceptance rules

Every accepted artifact must:

- bind the frozen module, BSP, configuration, runtime, and tool identities;
- contain exactly two authenticated objective roots that resolve to flag stands;
- contain no unproved traversal link;
- produce agreement between both C readers and the Python reader;
- pass lint and every applicable semantic checker;
- load in a new q2ded process with its exact-bound SNAG, without fallback or
  mixed sidecar state; and
- stop generation and cold-load processes cleanly.

### Complete

A `complete` wire contract requires every live seed to reach both objective
roots. The controller classifies an accepted artifact with this contract as
`PASS`.

### Route-only release contract

`local_only` is the RUNE wire contract. `ROUTE_ONLY` is the controller result
classification for an accepted artifact carrying that contract. Release an
artifact under this classification only when all of these conditions hold:

- Every live seed reaches at least one objective root. Seeds that reach neither
  root remain tombstoned.
- Every live seed has a finite local-objective fallback field.
- Combat, reachable attack, defense, escort, seedless recovery, item behavior,
  and mechanism behavior remain active in each proved component.
- Exactly two authenticated root markers remain, and both resolve to spawned
  flag stands.
- Ordinary closure, BSP comparison and repair, and the configured production
  Dijkstra late-path search complete without a work budget.
- The current frozen build generated the artifact, and its authenticated log
  proves a complete no-progress search ended as `open-exhausted`. An adopted
  `local_only` header is not enough evidence.
- Prove every added RUN, JUMP, DROP, SWIM, or direct HOOK edge exactly.
- The only missing behavior is an inter-flag or carried-flag return path.
- An ordinary match proves useful play for both teams.

The validator rejects arbitrary terminal sinks as objective substitutes. A
complete graph mislabeled `local_only`, an unknown route contract, or a link
added only to change classification fails acceptance.

### Human learning

- Human traces nominate movement. The in-engine oracle must replay and prove
  each nominated transition before topology changes.
- Capture and importer tests retain the client and frame-range evidence needed
  for replay. The server can isolate a client and frame range.
- Post-match learning is sufficient for the initial release. Live mutation is
  optional.
- A learned replacement must pass the complete validator, both readers, lint,
  semantic checks, exact-bound SNAG validation, and cold load. Cold-load the
  staged bundle before acceptance.
- Install new sidecars before the RUNE commit point. Never publish mixed graph
  and sidecar state; partial installation fails closed.

## Implemented systems and mandatory gates

The source candidate includes the RUNE and SNAG formats, transactional handling,
independent readers, root-aware fail-closed publication, mechanism handling,
human capture and replay, local fallback, private Python runtime closure,
authenticated bundle installation and rollback, and the persistent ten-lane
match fleet.

Focused controller, bundle, fleet, route-only match, and plan tests run under
both build files. Full GNU and Make host gates remain mandatory. Focused tests
do not replace them.

## Next critical path

Commit and push the verified generator, synchronize `slipgate` and `main`, and
require exact green CI. Then create an empty-RUNE immutable snapshot and
generate, verify, finalize, bundle, match-test, and publish all 175 maps.

## Execution plan

### 1. Land the final source candidate

1. Finish door-family wait/fan enumeration and member-overflow propagation.
   Keep controller and finalizer recovery, adoption, one-shot replacement,
   provenance, heartbeat, and no-review-deadline tests green.
2. Make BSP contact reconciliation consume every valid generalized contact.
   Run late-path scheduling until closure or a complete no-progress tour proves
   `open-exhausted`. A batching window is not a terminal budget.
3. Audit every production generator search. Each bounded batch must resume from
   a durable cursor until finite exhaustion. Capacity overflow must fail
   generation instead of omitting candidates.
4. Prove corrected BSP and fallback behavior on the affected real maps. Repeat
   `lmctf01`, `lmctf06`, `lmctf12`, and `lmctf15` on final bytes, and repeat the
   staged `smap32` SNAG and cold-load proof.
5. Review comments and source size, remove accidental outputs, and update
   source-size limits only to reviewed final counts.
6. Run warning-clean builds, focused suites, and full GNU and Make host gates.
7. Commit and push the coherent wave on `slipgate`. Require exact-commit CI.
8. Merge to `main`, move `slipgate` to the merge commit, push both refs, and
   require exact green CI on both synchronized refs.

### 2. Create the immutable freeze

1. Build warning-clean GNU and Make modules from the synchronized commit. The
   production module aliases must be byte-identical.
2. Build and verify the private Python runtime.
3. Snapshot the frozen source, tools, runtime, and the 175 manifest-selected
   BSPs.
4. Include no old RUNE candidates. The rewritten generator must produce all 175.
5. Make the snapshot immutable. Do not commit source or documentation changes
   through the release tag.

### 3. Generate and accept all 175 RUNEs

1. Start with an empty controller run root.
2. Generate all 175 manifest maps from frozen bytes. Do not adopt an old RUNE.
3. Run with `jobs > 1`. Leave the production generation timeout unset.
   Fingerprint any deliberate safety override.
5. Apply the complete-route contract first. Require every terminal result to be
   `PASS` or approved `ROUTE_ONLY`. Recheck every reader, semantic, SNAG, and
   cold-load receipt.
6. Run `finalize` to bind all 175 accepted results and their ordered provenance
   histories into one immutable, content-addressed corpus. Run `verify-final`
   before bundle assembly.

### 4. Install and test the production bundle

1. Assemble the authenticated bundle and verify its archive and release
   manifest.
2. Install one content-addressed generation and verify every installed role and
   both module aliases.
3. Prove failure recovery and rollback.
4. Cold-load the installed maps before match evidence begins.

### 5. Collect real-match evidence

Run the final route-only remainder, which may contain zero to ten maps. Run the
persistent ten-process fleet over the scheduled 20-map rotations. Retain
evidence for:

- earned perception and movement;
- objectives, mechanisms, combat, and roles;
- item pursuit, commitment retirement, and recovery;
- one terminal lifecycle per hook use;
- exact rosters, recordings, spectator sound attribution, and clean shutdown.

Judge behavior from play. Scores, wins, captures, schedule completion, and
parser output do not replace observed behavior. If a match exposes a source
defect, abandon the freeze, fix the source, rerun CI, and repeat every
invalidated downstream gate.

### 6. Tag and verify the release

1. Tag the unchanged frozen commit as `v1.0.0`.
2. Publish the supported modules and static public assets.
3. Download the release into a clean directory. Verify `VERSION`,
   `SHA256SUMS`, and every payload.
4. Independently verify the installed production bundle. Record the final
   source, module, corpus, match, CI, tag, and release identities.
5. Make only evidence updates to this plan after release. Do not rebuild the
   release or retag the frozen commit.

## Invalidation rules

- A source, tool, module, configuration, engine, Python runtime, reader, linter,
  semantic checker, or BSP change invalidates the snapshot and all downstream
  evidence.
- A RUNE change invalidates its SNAG, cold load, bundle, and match evidence.
- A bundle change invalidates installed-bundle and match evidence.
- Fake-engine tests never replace real-match evidence.
- Development artifacts and prior freezes cannot authorize the final corpus.

## Completion checklist

- [x] Gameplay, traversal, learning, RUNE, SNAG, reader, bundle, and fleet
  systems are implemented.
- [x] Controller, finalizer, BSP, late-path, hook, reverse, compound-swim,
  relay, and train repairs pass focused tests.
- [x] Dijkstra is production-wired, and focused tests prove closure or a full
  no-progress `open-exhausted` search without a work budget.
- [x] Door-family exhaustion and member-overflow propagation are complete.
- [x] The corrected generator produces and cold-loads a complete `lmctf12` RUNE.
- [ ] Exact CI is green on synchronized `slipgate` and
  `main`.
- [ ] The immutable final snapshot contains 175 BSPs and no old RUNEs.
- [ ] All 175 RUNEs are generated and accepted from the frozen build.
- [ ] All 175 artifacts are finalized and independently verified.
- [ ] The production bundle is installed and rollback is verified.
- [ ] Route-only and persistent-fleet real-match evidence is accepted.
- [ ] Any match-exposed defect is repaired and invalidated evidence is repeated.
- [ ] The unchanged frozen commit is tagged and the public release is verified.

## Maintenance rule

Keep this file current and compact. Update status, gates, and the checklist. Do
not add command transcripts, temporary paths, unnecessary hashes, rejected
hypotheses, or superseded narratives.
