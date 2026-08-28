# Positive-volume configuration topology

## Geometric contract

A configuration cell is a three-dimensional convex region with positive
volume. The constructor derives cells from BSP planes and from solid brushes
expanded by the frozen standing or crouching hull. A configuration portal is a
positive-area shared boundary between two cells of the same stance.

Binary32 collision arithmetic can classify a q8 point differently from the
corresponding exact-real plane inequality. Such a point can localize to an
incident positive-volume cell. A host-only isolated point or zero-volume set
does not create a cell or a portal because it has no navigable neighborhood.

Each retained cell must contain a signed-q8 point with positive maximin
clearance from every face. `SG_HostCollisionClassifyPose` must also accept that
point. Each retained portal must have a q8 point on each incident side with
positive clearance from every nonshared face and polygon edge. The projected
crossing must lie in the shared positive-area polygon, and
`SG_HostCollisionTransition` must accept the crossing. A feasible point with
zero maximin clearance is localization evidence only. Host checks can reject
geometric candidates. They cannot create topology that the positive-volume
decomposition does not contain.

## Signed-q8 feasibility

Player origins are triples in `[-32768, 32767]^3`, interpreted as world
coordinates divided by eight. The lattice solver reads the exact IEEE-754
binary32 bits of each normal and distance. A closed face accepts
`n.q/8 <= d`. An expanded-brush outside face accepts `n.q/8 < d`.

The solver multiplies each constraint by its least binary exponent. The result
is an exact GMP integer inequality. An open inequality subtracts one from the
integer right-hand side, so feasibility does not use an epsilon.

Cell selection adds an integer clearance dimension. Each selected halfspace
multiplies that dimension by the largest absolute normal component. This is an
exact L-infinity clearance and stays unchanged if equivalent plane records use
different scales. ISL maximizes clearance before it applies the q8 coordinate
tie-break.

Portal selection maximizes clearance from the nonshared cell faces and the
polygon edges first. It then maximizes the exact boundary-normal objective, so
the point approaches the shared face without choosing a polygon edge or corner
when a deeper crossing exists. Polygon edge halfspaces form an orthogonal prism
through the shared polygon. The constructor and auditor build their constraints
and clearance masks separately.

The solver has no radius, depth, point-count, timeout, or work cap. Counters
record solve calls, emitted integer constraints, and the largest binary
coefficient shift. ISL supplies the integer-set feasibility and optimization
algorithms. This module does not claim a stronger formal complexity bound for
those algorithms.

## Build boundary and distribution

`sg_configuration_lattice.c` belongs only to the offline constructor and
auditor. The runtime model, the frozen-model loader, and the game DLL must not
link it. The current test runners link ISL and GMP through `pkg-config`.

ISL uses the MIT license. GMP uses the LGPLv3 or GPLv2 license. Linux systems
that build frozen configuration output need both development packages. The
repository does not yet package these libraries for a Windows offline
generator. Windows game builds do not need them because runtime code reads the
frozen output.
