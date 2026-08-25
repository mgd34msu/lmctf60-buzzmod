# LMCTF BuzzMod project completion plan

This is the current execution plan. Git history and immutable run records hold
superseded experiments and detailed evidence.

## Completion definition

The project is complete when one unchanged source commit satisfies all of the
following conditions:

1. Exact CI is green on synchronized `slipgate` and `main` branches.
2. One immutable snapshot binds the final source, tools, runtime, 175 canonical
   BSPs, and 156 existing RUNE candidates.
3. All 175 maps have accepted RUNEs. Existing candidates are validated before
   generation and passing bytes remain unchanged. A rejected candidate may be
   replaced once from the frozen build. The 19 missing RUNEs are generated.
4. Every accepted RUNE passes both C readers, the Python reader, lint,
   applicable semantic checks, matching SNAG validation, and a fresh-process
   cold load.
5. Every map is route-complete or satisfies the route-only release contract.
6. The authenticated production bundle is installed and verified.
7. Ordinary matches prove the required bot behavior on the installed bytes.
8. The release tag points to the unchanged frozen commit, and downloaded public
   assets pass version and integrity checks.

## Current status

| Area | Status |
|---|---|
| Gameplay and bots | Implemented: combat, roles, objectives, D_SWIM, rocket jump, D_DROP, D_HOOK, local fallback, and human trace capture. |
| Dijkstra fallback | Production-wired after ordinary and BSP-aware closure. It is not a tested-but-uncalled path. |
| Botless RUNE load | Fixed. A real `lmctf01` probe loaded the existing RUNE and SNAG, built fields, and emitted route, objective-root, and ready receipts. |
| `smap32` | Duplicate teleporter fanout is preserved while executable closure selects ordinal zero. A fresh run from zero adopted RUNEs passed generation, both C readers, Python, lint, semantic checks, staged SNAG validation, and a distinct-process cold load. |
| Corpus inputs | 175 canonical maps: 156 preserved adoption candidates and 19 missing artifacts (`smap32` plus the manifest entries from `xmap13` through `xmap30`, using canonical `xmap14a`). No final validation run has started. |
| Controller | Adoption, one-shot replacement, crash recovery, provenance, and unbounded review are implemented and independently reviewed. The focused controller, finalizer, and integration suites pass. Production leaves the generation timeout unset; an explicit safety override is fingerprinted only when deliberately supplied. |
| Timing bounds | Startup/identity and cold-load readiness use generous authenticated bounds. Owned-child teardown is bounded. These bounds do not limit generation or review work. |
| Branches and CI | The full GNU/Make and GCC/Clang host matrix is green locally. The first exact CI run exposed one Windows-only `stat` type warning; its narrow platform fix is under validation. A fresh exact CI run, merge to `main`, synchronized refs, and exact CI on both refs remain. |
| Freeze and corpus | No accepted freeze or final 175-map corpus exists. Freeze creation must wait for the current controller wave and CI. |
| Bundle, matches, release | Final-corpus authority is bound through bundle installation and both fleet paths, with exact-byte loading for every executable Python input. Focused tests pass. Production installation and matches have not started. Fake-engine tests are tooling proof, not match evidence. `v1.0.0` is not tagged or published. |

## Fixed authority and policy

### Corpus authority

- `tools/rune-corpus-maps.txt` is the sole authority for the 175 maps.
- The durable asset set has 180 BSPs. The manifest excludes unsuffixed bases
  `lmctf05`, `smap31`, `xmap07`, `xmap11`, and `xmap14` because their suffixed
  variants are canonical. In each case, the unsuffixed base is out of scope and
  every listed variant remains in scope.
- Build the frozen BSP set by iterating the manifest. Do not copy all 180 BSPs.
- `tools/topmaps.txt` is an ordinary schedule list. It has no special
  completion or release authority.

### Configuration roles

- Snapshot `tools/rune.cfg` as the generator configuration. Validation and
  match overlays cannot replace it.
- `tools/route-only-match.cfg` is only for ordinary route-only matches. The
  production bundle includes the empty `tools/route-only-maplist.txt` so LMCTF
  cannot fall back to an ambient map list.
- Preserve legacy LMCTF gameplay behavior. Change it only when requested bot or
  telemetry work requires it.

### Existing and missing RUNEs

- The snapshot contains 156 adoption candidates, not 156 accepted artifacts.
- Validate each candidate from its frozen bytes. Do not ask the server to
  regenerate it first.
- Preserve every passing candidate byte-for-byte.
- Only a conclusive, authenticated artifact rejection authorizes replacement.
  Infrastructure, lifecycle, observation, or host failures retry adoption.
- Publishing the immutable `generated_replacement` intent consumes that map's
  only replacement attempt for the freeze. It is never repeated in the same
  freeze, even if later infrastructure fails.
- Missing maps use normal generation attempts and may resume in the same run
  root after non-accepted attempts.
- Production leaves the generation timeout unset, so generation and review have
  no elapsed-time deadline. An optional safety override is allowed only when
  deliberately supplied for a controlled run and is included in the fingerprint.
  The external acceptor `--contracts` metadata probe has a justified 300-second
  bound to contain a hung metadata handshake. It does not bound generation or
  review.
  Time bounds otherwise apply only to authenticated external startup/readiness
  or owned-child teardown, with a documented safety reason and ample margin.

### Distribution scope

- Publish the supported Linux and Windows modules, `VERSION`, `SHA256SUMS`, and
  the tracked `assets/lmctf6-buzzmod.pak`.
- The public PAK contains static scoreboard art and sounds, not generated
  navigation data.
- Defer separate downloads of generated RUNEs. The generated corpus and
  production server bundle remain authenticated release evidence and install
  inputs unless the release scope explicitly adds them as public downloads.

## RUNE acceptance rules

Every accepted artifact must:

- bind the frozen module, BSP, configuration, runtime, and tool identities;
- contain exactly two authenticated objective roots resolving to flag stands;
- contain no unproved traversal link;
- produce agreement between the two C readers and the Python reader;
- pass lint and each applicable semantic checker;
- load in a new q2ded process with its exact-bound SNAG, without fallback or
  mixed sidecar state; and
- stop generation and cold-load processes cleanly.

### Complete

A `complete` wire contract requires every live seed to reach both objective
roots. The controller classifies an accepted artifact with this contract as
`PASS`.

### Route-only release contract

`local_only` is the RUNE wire contract. `ROUTE_ONLY` is the controller result
classification for an accepted artifact carrying that contract. Such an
artifact is releasable only when:

- every live seed reaches at least one objective root and seeds reaching
  neither remain tombstoned;
- every live seed has a finite local-objective fallback field;
- combat, reachable attack, defense, escort, seedless recovery, item behavior,
  and mechanism behavior remain active in each proved component;
- exactly two authenticated root markers remain and both resolve to spawned
  flag stands;
- ordinary closure, BSP comparison/repair, and the configured production
  Dijkstra late-path search have completed;
- every added RUN, JUMP, DROP, SWIM, or direct HOOK edge was exactly proved;
- the only missing behavior is an inter-flag or carried-flag return path; and
- an ordinary match proves useful play for both teams.

The validator rejects arbitrary terminal sinks as objective substitutes.

A complete graph mislabeled `local_only`, an unknown route contract, or a link
added only to change classification fails acceptance.

### Human learning

- Human traces nominate movement. They never directly authorize topology.
- Capture and importer tests must retain the exact client and frame-range
  evidence needed to replay a nomination.
- The server records active human movement and can isolate a client and frame
  range. The in-engine oracle must replay and prove a nominated transition.
- Post-match learning is sufficient for the initial release; live mutation is
  optional.
- A learned replacement must pass the complete validator and the normal reader,
  lint, semantic, exact-bound SNAG, and cold-load chain. It must cold-load the
  staged bundle before acceptance.
- Install new sidecars before the RUNE commit point. Publication must never
  publish mixed graph and sidecar state; partial installation fails closed.

## Implemented systems and mandatory gates

The source candidate includes the current RUNE format, transactional RUNE and
SNAG handling, independent readers, root-aware fail-closed publication,
mechanism handling, human capture and replay, local fallback, private Python
runtime closure, authenticated server-bundle installation and rollback, and
the persistent ten-lane match fleet.

Focused targets include the controller, bundle, fleet, route-only match, and
plan tests under both build files. The full GNU and Make host gates remain
mandatory; focused tests never replace them.

## Route-only candidate inventory

These maps are candidates, not a fixed release class. They were classified
earlier in development, and the final count may be lower because later
generation repairs now run on every map.

| Map | Development-era unresolved reason |
|---|---|
| `lmctf01` | Exact replays rejected remaining partition cuts; two human runs are imported. |
| `lmctf06` | The timed BFG shortcut works, but ranked reverse-hook repairs did not close the objective cut. |
| `lmctf12` | BSP contact repair added real SWIM links but did not close the route; two human routes are imported. |
| `lmctf15` | The timed vault works, while exact movement left the flags in separate components. |
| `lmctf19` | Each flag was in a one-way DROP basin and exact egress replay rejected. |
| `lmctf25` | The train works, but exact movement found no valid boarding link into the missing route. |
| `tomb05` | Human play proves the gravity-100 exterior hook route; the oracle had not converted it into a proved edge. |
| `xmap13` | PUSH and door-egress replays remained pinned before a stable outgoing seed. |
| `xmap18` | Rocket-jump and teleporter witnesses reached neutral regions without joining the objectives. |
| `xmap26` | Sparse-water generation was corrected, but the objective cut had no proved bridge. |

For each remaining candidate, the final report must identify the missing path.
Apply the complete-route contract before applying the route-only fallback. A
later replay-proved complete-route update can replace the fallback, but that
optional learning does not block the initial release.

## Current pre-freeze work

The controller/finalizer hardening, exact final-corpus authority binding, fresh
`smap32` staged proof, and full four-way host matrix are complete. A narrow
Windows warning fix and fresh exact CI remain before the freeze. The immutable
freeze and corpus run have not started.

## Execution plan

### 1. Land the final source candidate

1. Complete controller/finalizer recovery, adoption, one-shot replacement,
   provenance, heartbeat, and no-review-deadline tests.
2. Retain the completed staged `smap32` SNAG and cold-load proof as the fresh
   pre-freeze runtime check.
3. Review comments and source size, remove accidental outputs, and update exact
   budgets only for reviewed final counts.
4. Run warning-clean builds, focused suites, and full GNU and Make host gates.
5. Commit and push the coherent wave on `slipgate`; require exact-commit CI.
6. Merge to `main`, move `slipgate` to the merge commit, push both refs, and
   require exact green CI on both synchronized refs.

### 2. Create the immutable freeze

1. Build warning-clean GNU and Make modules from the synchronized commit and
   require the production module aliases to be byte-identical.
2. Build and verify the private Python runtime.
3. Snapshot all frozen source/tool/runtime inputs and the 175 manifest-selected
   BSPs.
4. Add the exact 156 preserved RUNE candidates as adoption inputs.
5. Make the snapshot immutable. Do not commit source or documentation changes
   through the release tag.

### 3. Generate and accept all 175 RUNEs

1. Start with an empty controller run root.
2. Validate all 156 candidates. Preserve passes; retry adoption infrastructure
   failures; create one final-build replacement only after authenticated
   artifact rejection.
3. Generate the 19 missing maps: `smap32` plus the manifest entries from
   `xmap13` through `xmap30`, using canonical `xmap14a` rather than `xmap14`.
4. Run with `jobs > 1`. While both queues are nonempty, overlap adoption
   validation with missing-artifact generation. Leave the production generation
   timeout unset. Fingerprint any deliberate safety override.
5. Apply the complete-route contract first. Require every terminal result to be
   `PASS` or approved `ROUTE_ONLY`, then recheck every reader, semantic, SNAG,
   and cold-load receipt.
6. Run `finalize` to bind all 175 accepted results and their ordered provenance
   histories into one immutable content-addressed corpus.
7. Run `verify-final` before bundle assembly.

### 4. Install and test the production bundle

1. Assemble the authenticated bundle, then verify its archive and release
   manifest.
2. Install one content-addressed generation and verify every installed role and
   both module aliases.
3. Prove failure recovery and rollback.
4. Cold-load the installed maps before match evidence begins.

### 5. Collect real-match evidence

- Run the final route-only remainder, which may contain zero to ten maps.
- Run the persistent ten-process fleet over the scheduled 20-map rotations.
- Retain evidence for earned perception; movement; objectives; mechanisms;
  combat; roles; item pursuit and commitment retirement; recovery; one terminal
  lifecycle per hook use; exact rosters; recordings; spectator sound
  attribution; and clean shutdown.

Judge behavior from play. Scores, wins, captures, schedule completion, and
parser output do not replace observed behavior. If a match exposes a source
defect, abandon the freeze, fix the source, rerun CI, and repeat every
invalidated downstream gate.

### 6. Tag and verify the release

1. Tag the unchanged frozen commit as `v1.0.0`.
2. Publish the supported modules and static public assets.
3. Download the release into a clean directory and verify `VERSION`,
   `SHA256SUMS`, and every payload.
4. Independently verify the installed production bundle and record the final
   source, module, corpus, match, CI, tag, and release identities.
5. Make any evidence-only plan update after the release. Do not rebuild the
   release or retag the frozen commit.

## Invalidation rules

- A source, tool, module, configuration, engine, Python runtime, reader,
  linter, semantic checker, or BSP change invalidates the snapshot and all
  downstream evidence.
- A RUNE change invalidates its SNAG, cold-load, bundle, and match evidence.
- A bundle change invalidates installed-bundle and match evidence.
- Fake-engine tests never replace real-match evidence.
- Development artifacts and prior freezes cannot authorize the final corpus.

## Completion checklist

- [x] Gameplay, traversal, learning, RUNE, SNAG, reader, bundle, and fleet
  systems implemented.
- [x] Dijkstra late-path selection wired into production generation.
- [x] Pre-freeze controller/finalizer and `smap32` proofs complete.
- [ ] Full host gates and exact CI green on synchronized `slipgate` and `main`.
- [ ] Immutable final snapshot contains 175 BSPs and 156 adoption candidates.
- [ ] All 156 candidates validated; passes preserved and authorized failures
  replaced at most once.
- [ ] All 19 missing RUNEs generated and accepted.
- [ ] All 175 artifacts finalized and independently verified.
- [ ] Production bundle installed and rollback verified.
- [ ] Route-only and persistent-fleet real-match evidence accepted.
- [ ] Any match-exposed defect repaired and invalidated evidence repeated.
- [ ] Unchanged frozen commit tagged and public release verified.

## Maintenance rule

Keep this file current and compact. Update status, gates, and the checklist.
Do not add command transcripts, temporary paths, unnecessary hashes, rejected
hypotheses, or superseded narratives.
