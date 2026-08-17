# SLIPGATE implementation roadmap

This is the authoritative project plan. Historical plans, recovery notes, and
release records do not override it.

## Current work

### 1. Finish the RUNE cutover

Status: **in progress**.

There is one implementation and one artifact contract, called **RUNE**. It has
no compatibility readers, historical codecs, format-selection flags, or
internal version discriminator.

Required completion:

- delete every obsolete reader, writer, codec, contract, fixture, test,
  acceptance harness, CLI mode, build target, and installed artifact;
- remove contrast-only RUNE qualifiers from code and tools;
- keep the fixed serialized header geometry, but encode the former
  discriminator slots as required-zero reserved bytes;
- remove those discriminator fields from semantic C and Python objects;
- regenerate contract identities, sidecars, fixtures, and artifacts;
- prove the same current contract in C, Python, both Make dialects, Clang, and
  the Visual Studio project; and
- retain no obsolete implementation copy in the working tree. Git history is
  the archive.

### 2. Complete the map corpus

Status: **pending after the RUNE cutover**.

- Adapt the corpus controller to the current generator, linter, loader, and
  runtime interface.
- Generate every supported map reproducibly from frozen source and module
  bytes.
- Treat generation, binding, publication, lint, cold-load, and runtime failures
  as defects to fix, not exceptions to waive.
- Independently verify counts, identities, sidecars, mechanism plans, and the
  final corpus summary.

## Unfinished player-visible features

### 3. Native POV recording

- Add `povlock` for a true in-eyes observer view of a named bot, defaulting to
  the current top scorer when no name is supplied.
- Add one headless recording client per wave.
- Produce a normal client demo that plays without conversion.
- Start and stop the POV recording with the wave lifecycle while preserving the
  existing analysis capture.

### 4. Rope discipline

- Stop low-value hook throws.
- Require sufficient expected ride value before firing.
- Reduce pointless fire/release cadence while preserving worthwhile long rides.
- Close the hook aim-wedge path by retiring the failed link and failure streak
  when aiming cannot converge.

### 5. Human-speed movement

- Remove the facing-alignment behavior that holds bots near 300 units/second.
- Exercise and tune the existing air-strafe and air-gain mechanics.
- Validate movement against human trajectories, not only aggregate film
  statistics.

### 6. More visibly active defense

- Add observable post movement without increasing captures conceded.
- Measure defensive effectiveness and direct spectator-visible activity
  together.

### 7. Feed snag coordinates into routing

- Consume stall-census clusters in generator or map-repair inputs.
- Add route costs, graph repairs, or explicit per-map fixes as appropriate.
- Regenerate and prove affected maps.

### 8. Item commitment

- Trace the quad walk-by to either worth calculation or approach/pickup control.
- Make a committed approach complete the pickup unless a stronger valid reason
  causes an explicit retirement.

### 9. Complete hook diagnostics

- Pair every hook-fire event with exactly one terminal event.
- Account for release, impact, timeout, death, disconnect, and map transition.

### 10. Correct spectator sound attribution

- Trace the spectator-client misclassification that attributes player voice
  sounds to the world entity.
- Preserve correct server emission ownership and fix the observer-side result.

## Deferred expansion

These follow the player-visible defects unless a prerequisite forces an earlier
slice:

- pluggable objective logic and operation without CTF;
- hook support outside the offhand controller;
- real per-persona skill differences; and
- broader measured learning, cost, and weight tuning.

## Release and delivery

### 11. Fleet cutover

- Install the proven module, RUNE artifacts, and sidecars atomically.
- Verify the observation fleet on the replacement bytes.
- Remove obsolete installed artifacts only after every consumer has moved.

### 12. Documentation and verification

- Reconcile `SLIPGATE.md`, `ARCHITECTURE.md`, `LEDGER.md`, `TOOLING.md`,
  `RELEASES.md`, and `tools/README.md` with verified behavior.
- Retract broad parity claims that direct observation disproved.
- Run full GNU, Make, Clang, Visual Studio, host, integration, corpus, runtime,
  and dependency/loader gates.
- Run source-language, generated-output, and repository-residue scans.

### 13. Cleanup and merge

- Remove generated binaries, objects, dependency files, bytecode, expired
  staging directories, and superseded evidence using reviewed manifests.
- Preserve live fleet inputs and the promoted final evidence set until cutover
  is complete.
- Commit proven slices, push, reconcile with `main`, rerun the replacement
  gates, and merge.

## Acceptance rule

A source file, passing focused test, or successful load is not completion by
itself. Each item completes only after its real consumer path and relevant live
behavior are proven on one frozen set of bytes.
