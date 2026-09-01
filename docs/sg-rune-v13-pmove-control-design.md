# V13 PMove control regions

## Decision

V13 adds PMove control regions as first-class canonical RUNE content. The
first slice covers standing, dry, world-supported movement along the positive
X axis of one empty convex corridor toward its one terminal portal.

The persisted model owns five facts:

- a stable compact-artifact and host-law identity;
- a positive intrinsic-volume viability domain in horizontal position and
  velocity on one certified support stratum;
- a fixed-point potential with distance, reversal-velocity, and
  lateral-velocity debt;
- a corridor-law certificate with clearance and conservative four-substep
  descent bounds;
- ordered same-cell and terminal-portal transitions.

The canonical section contains no live subject, tactic, action, input, or
replay data. Its codec is a section codec only. The main compact wire owns
publication and acceptance after the v13 section is registered there.

## Runtime boundary

The RUNE module provides pure validation, potential, gradient, and transition
descent queries. It cannot replay movement and cannot accept caller-supplied
movement state as host authority.

The private engine owner receives a potential gradient and an opaque current
bot-life subject. It mints the same PMove seed as `ClientThink`: current
`ps.pmove`, body origin and velocity overrides, `old_pmove`, gravity, hook
state, and view-angle deltas. It derives a stack-local walking input and runs
the captured host PMove exactly four times at 25 ms. It re-mints the seed after
replay and rejects drift.

The private tactic runtime checks the replay's BSP and PMove identity, timing,
source pose, four supported/dry/standing substeps, collision provenance, and
the unique terminal portal-plane crossing. It then applies the pure RUNE
admission rule with checked integer arithmetic:

```text
source = ceil(Phi(source)) + 1
admit only when next + frame_cost < source
```

`next` excludes the reserve. A same-cell transition evaluates the successor
potential. A portal transition uses the authenticated next-cell tail.
Equality and overflow reject.

## Certificate scope

The certificate does not generalize a sampled replay. It fixes the relevant
YQ2 branch: standing, dry, static planar support, no horizontal obstacle, 100
ms frame, four 25 ms substeps, ground friction retention at most 85/100, 75
units/s acceleration per substep, 300 units/s wish speed, and 2000 units/s
maximum velocity.

For nonnegative longitudinal velocity, a conservative fixed-point recurrence
proves minimum forward displacement. For reversed velocity, a rational
inequality proves reversal debt dominates the maximum full-frame backward
displacement over the entire declared interval. With neutral lateral input,
host friction retains at most 85/100 of lateral speed per substep. The stored
lateral bounds are intersected with `|y-center| + |vy|/4 < half-width`; the
exact four-step friction bound makes this envelope invariant, including the
declared maximum lateral speed at the corridor center. Runtime YQ2 replay
remains authoritative and rejects any live trace that leaves this branch.

## Alternatives

A separate sidecar was rejected because it would restore mixed ownership and
an independent acceptance path. Persisted action bytes were rejected because
they would put runtime policy and ABI-shaped input into the RUNE. Sampled
affine PMove deltas were rejected because friction, acceleration clamping, and
collision are nonlinear outside a single sample.
