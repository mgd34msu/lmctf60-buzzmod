# SLIPGATE

A native movement intelligence for Quake II + LMCTF (BuzzMod). Lives entirely in
the game shared object. No external tools, no Q3 botlib, no bspc.

Three systems:

    SLIPGATE = RUNE + ARACHNOTRON + CACO

## RUNE — what the map affords (per-map artifact, `maps/<name>.rune`)

A proven link graph over the map's phase space, generated once per map by
rolling the REAL physics (Pmove and the LMCTF hook) forward from sampled
states. Nothing in a rune is inferred from geometry heuristics; every link was
demonstrated by simulation before it was written down.

- Seeds: walkable-surface samples (~64u), hook anchor candidates on faces,
  action points (doors, lifts, teleporters, flag stands, item spawns).
- Links: (entry envelope: position, speed band, heading cone) + action
  (run / jump / hook-at-anchor / drop / swim) -> (exit state) + cost in real
  seconds + provenance tag: PROVEN (simulated), OBSERVED (a player did it),
  ADJUSTED (cost corrected by experience). Dense DROP, HOOK, and SWIM links
  are generated and admitted only as PROVEN; observation or cost learning
  cannot mint one of those controllers.
- The rune is the map's memory. Learned routes append; observed costs
  overwrite; deaths accumulate into a persisted danger layer.

The rune discretises the game's phase-space dynamics: entries are conditioned
on speed and heading, not just position, because what a player can do depends
on the state they arrive in.

## ARACHNOTRON — the brain with legs (runtime: value and movement)

Reads the rune, prices the world, moves the body. Three internal stages, one
system:

- Fields: flood-fills over the rune from anything that matters — each flag,
  each base, item classes, the live carrier's projected phase mass. Scalar
  potentials, ~KBs each, milliseconds to build, rebuilt on events.
- Surface: one value function per bot per moment,
      V(x | bot) = sum_i  w_i(role, phase, self, team) * field_i(x)
                 + detour terms: worth(item) / (cost_to + cost_from - direct)
  Roles are weight vectors. Phases are multiplier schedules. Behaviour is
  data, sweepable by the experiment harness, learnable from outcomes.
- Body: executes per pmove step, natively. Steers by descending the surface
  through link envelopes — no waypoint arrival, overshoot just re-reads the
  surface. Movement policy is closed-form, derived from engine source:
    - ground strafe at cos(theta) = (wishspeed - A) / v, A = accel*ft*wishspeed
    - ground accel is 10x air accel; friction caps ground at ~370;
      the air is uncapped but weak — build on the floor, leave to keep
    - jump on the landing pmove step (PM_CheckJump precedes PM_Friction;
      a landing-step jump pays zero friction); jump is a tap, never held
    - hook: rope SETS velocity to a flat 800 (p_weapon.c), no gravity while
      taut, braking ladder under 120 rope; anchors are points on the route,
      chosen by cycle-average economics (flight time is dead time), fired
      only when the projected average beats current speed
- Combat runs concurrently with navigation. There is no state that suspends
  movement.

## CACO — the eye (perception, belief, learning)

Every layer above reads only what CACO grants. No g_edicts omniscience.

- Belief: seen-it (PVS + trace, fresh), heard-it (teammate callout, stale),
  common knowledge (HUD flag status), remembered (survives own death).
- Other agents are tracked as phase mass, not points: an estimate ages by
  advecting along the rune under the agent's presumed policy (a carrier
  descends their route-home field). Interception is our reachable set meeting
  their likely set — ambush where our arrival state beats theirs.
- Learning: completed traversals adjust rune link costs (measurement);
  demonstrated non-dense links may append as OBSERVED (memorisation), never
  creating a DROP, HOOK, or SWIM proof; deaths shade the danger layer
  (statistics); match outcomes nudge the global weight tables
  (the one genuinely fitted component, dozens of named floats, shared across
  all maps).

## Principles (paid for in the prior session, not negotiable)

1. Read the engine, never assume it. Every physics claim carries a source
   line. The prior session's error catalogue — PM_CmdScale that doesn't
   exist, a pitch/3 basis unaccounted for, a flight timeout that made its own
   configuration impossible — all came from plausible assumption.
2. Simulated time must sum to real time. Finer decisions, never a longer
   clock.
3. Facts are measured, preferences are fitted, and nothing else is learned.
   Five of the six models are facts.
4. Every claim A/B-able in the same harness, old bots selectable by cvar.
   Differences under the noise floor (±29 mean, ±1.3 steals at 3 reps) are
   not findings.
5. Speed serves the objective. A bot that crosses beautifully and does not
   capture is worse than a slower one that does — proven twice.

## Build order

1. TRINITY wrapper: SimulateMove(state, cmd, steps) — phantom pmove rollouts,
   zero side effects. (Internal name only; it is Pmove used as an oracle.)
2. RUNE generator + visual dump on lmctf03. Walk it by eye before any bot
   reads it.
3. Fields + debug overlay.
4. One bot descending, basic movement. A/B against the fae42b7 bots.
5. Full movement policy in the Body.
6. CACO belief gating, then phase-mass tracking.
7. Weight sweeps; then learning, costs first, weights last.
