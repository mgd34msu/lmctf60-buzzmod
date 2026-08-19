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
- **Team play**: roles (attack, defend, carry, recover, escort)
  argued per-think from shared team state, defender posting by
  sightline, coordinated pushes, and a full radio/chat layer — item
  callouts, quad timing economy ("quad 60"/"quad 30", one voice per
  cycle), mega clocks armed only from public information, and social
  chat mined from the human corpus.
- **Presentation**: bots are real clients with synthetic 5–15 ms ping,
  per-bot personas (movement taste, hook enthusiasm, chat voice), and
  they flow into server stats like any player.

## Current source and evidence status

The instrumentation measures raw movement, route choice, gunfights, team
decisions, and match outcomes. No retained result is final-candidate evidence
until it is rebound or rerun against the exact source/module/BSP/RUNE/config
identity. The current retained data identifies steal initiation, steal-to-cap
conversion, defender movement, and captures conceded as the outcome gaps that
the matched final-build trials must close.

**Implemented source behavior:**

- *Movement*: route jitter and ribbon (per-life route variety and
  lateral lane texture), tactics waypoints, no-backtrack pricing,
  lookahead and pure-pursuit steering, air-strafe control, the
  four-layer pit-safety chain, water traversal, and live mover handoff.
- *Combat*: weapon commitment (keep the held gun anywhere it is
  legitimate), aim texture (overshoot/settle and cadence variation), switch
  discipline, full weapon doctrine from the WEAPONS.md audit.
- *Carrier*: pause beats, human-mined escape priors, water routes, cover
  pricing, and carry movement texture.
- *Team*: attack, defend, carry, recover, and escort assignments;
  defender posts; coordinated strike state; and shared public objective state.
- *Comms (Rule 19)*: item callouts, radio wavs with human lag, item
  lead returns, quad 60/30 either-or with speaker rotation, mega
  taker-clocks, the honest ear, hit sense, corpus-mined social chat
  with unified human/bot team-chat parsing.
- *Platform*: native sg_net client layer, synthetic near-local ping,
  personas, session stats (`sg_sessiondb`) flowing bot games into the
  same analytics database as human games.

**Development instruments in `tools/`** (not runtime release assets): film.py
(blind film sheets),
routesheet.py (routes), fightsheet.py (fights, with honest
hit-attribution), teamsheet.py (team play, coverage-honest escort
scalar), outcomecard.py (match outcomes), plus the corpus manifest,
map fixtures for the currently supported blind sets, the set-composition rule,
and fleet tooling for unattended evaluation. Instrument validity and retained
datasets must be rebound to exact source/module/BSP/RUNE receipts before final
use.

## Remaining completion work

[`PROJECT-COMPLETION-PLAN.md`](PROJECT-COMPLETION-PLAN.md) is the sole current
dependency graph and execution authority. In SLIPGATE terms, the remaining work
is to close all-map RUNE/runtime coverage, repair the telemetry and evidence
provenance needed for honest comparison, improve the measured steal/capture and
defense outcomes, verify stable non-mirrored personas in production rosters,
and rerun qualified matched trials on the exact final build. The rule remains:
source-grounded mechanism → executable gate → matched film/outcomes → retained
receipt. Nothing ships on taste or on an old unbound result.
