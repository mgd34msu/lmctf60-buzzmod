# SLIPGATE

SLIPGATE is a from-scratch bot platform for LMCTF (Loki's Minions
Capture the Flag, Quake II). It replaces the legacy Q2/Q3-derived bot
code entirely — every line of the navigation, combat, perception, and
team logic is native to this codebase, written clean-room, with the
old botlib removed from the tree and the build.

What makes SLIPGATE different is not a feature list but a standard:
**the bots are judged blind against recorded human games.** Every
change ships only after film of bot play, rendered through the same
instruments as film of real pub play from the 2021–2023 human demo
corpus, goes in front of fresh judges who are not told which is which.
A change that makes the bots win more but look less human is rejected.
A change that survives is kept with its evidence. The full record —
every adoption, every rejected experiment, every measurement — lives
in `LEDGER.md` and the git history.

## How the bots work (short version)

- **Navigation** rides a per-map "rune" graph of proven movement links
  (runs, jumps, drops, swimming, grapple swings, lifts, and teleports),
  flooded into cost fields that price every seed on the map
  against every objective. Movement between commitments descends those
  fields; a feeler fan and safety layers (terminal braking, sink ban,
  edge guards) keep the body honest on the geometry.
- **Combat** models a player, not an aimbot: skill-scaled reaction
  delays, aim error with human overshoot/settle texture, range-band
  weapon doctrine with human switch discipline, fire windows, and
  splash/self-damage safety rules audited against the game source.
- **Perception is earned, not given.** Bots know what they see and
  hear. Item respawn knowledge exists only if a teammate called the
  take in team chat. Rune positions are never clocked. Enemy positions
  are beliefs that age, not entity lookups.
- **Team play**: roles (attack, defend, escort, recover, intercept)
  argued per-think from shared team state, defender posting by
  sightline, coordinated pushes, and a full radio/chat layer — item
  callouts, quad timing economy ("quad 60"/"quad 30", one voice per
  cycle), mega clocks armed only from public information, and social
  chat mined from the human corpus.
- **Presentation**: bots are real clients with synthetic 5–15 ms ping,
  per-bot personas (movement taste, hook enthusiasm, chat voice), and
  they flow into server stats like any player.

## This release

**Judgment status:** bot play is tested blind at five levels — raw
movement, route choice, gunfights, team decisions, and match outcomes.
**Raw movement, gunfights, and team decisions have all passed**: for
each, three independent fresh judges could not beat chance telling bot
film from human film — on the gunfight and team sets, the bots were
called "human" more often than the actual humans were. Route choice is
closed under an accepted, documented difference (bots never fall off
ledges; humans do — we declined to teach them). Match outcomes remain
under active work, tied to raising steal and capture volume to human
levels.

**Adopted and shipping as defaults** (each with trial evidence in the
ledger):

- *Movement*: route jitter and ribbon (per-life route variety and
  lateral lane texture), tactics waypoints, no-backtrack pricing,
  lookahead and pure-pursuit steering, air-strafe control, the
  four-layer pit-safety chain (80% hazard-conversion, zero drownings
  on census).
- *Combat*: weapon commitment (keep the held gun anywhere it is
  legitimate — the single biggest gunfight-test win), aim texture
  (human overshoot/settle, +cadence raggedness), honest switch
  discipline, full weapon doctrine from the WEAPONS.md audit.
- *Carrier*: breather pause beats (tripled steal→cap conversion in
  its dose ladder), human-mined escape priors (first dark feature to
  earn adoption), water routes, cover pricing, carry movement texture.
- *Team*: attack-objective weighting (atkobj 125), escort dose 35 —
  assigned escorts proved to be wasted bodies; freeing two-thirds of
  them to attack raised captures ~60% across three independent
  confirmations.
- *Comms (Rule 19)*: item callouts, radio wavs with human lag, item
  lead returns, quad 60/30 either-or with speaker rotation, mega
  taker-clocks, the honest ear, hit sense, corpus-mined social chat
  with unified human/bot team-chat parsing.
- *Platform*: native sg_net client layer, synthetic near-local ping,
  personas, session stats (`sg_sessiondb`) flowing bot games into the
  same analytics database as human games.

**Instruments shipping in `tools/`** (each Stage-A calibrated with its
validity limits recorded in-file): film.py (blind film sheets),
routesheet.py (routes), fightsheet.py (fights, with honest
hit-attribution), teamsheet.py (team play, coverage-honest escort
scalar), outcomecard.py (match outcomes), plus the corpus manifest,
map fixtures for 18 blind-set-capable maps, the set-composition rule,
and the fleet tooling (server loop, watchdog, atomic deploy) that
ran hundreds of hours of unattended evaluation during development.

## Planned for the next release

The active list, verbatim from the working board:

1. **Rung 4 (team decisions) pass.** The over-escort tell is fully
   decomposed (86–89% mid-field co-travel, five mechanisms tested);
   the mirror-symmetry tell (identical AI visible as identity on both
   teams) gets per-team persona spread. Sets run until judges hit
   chance.
2. **Rung 5 (match outcomes) pass**, gated on the stage-2 volume work
   below by design — its only validated instrument measures exactly
   that gap.
3. **Stage 2: beat the humans' own numbers while still passing.**
   Steal initiation (0.26/min vs human 1.3/min), close-approach
   conversion, and capture volume to human levels.
4. **Route polish**: plateau tie-break (the A→B→A revisit spike),
   transition-determinism eye and the dither retry behind it.
5. **Fights polish**: closing the remaining ~2× hits/shot edge via
   fire-gate loosening (the honest gap after the hit-marker artifact
   was fixed — a quarter the size it first appeared).
6. **Perception features behind their new instruments**, each trialed
   or struck on measurement the way sg_handoff was: rail-rhythm ear,
   belief cone/range, item-clock play, spawn-beat knowledge, view
   tilt, air-strafe chain tuning.

The rule for all of it is unchanged: film → judgment → gap → one
change → film. Nothing ships on taste.
