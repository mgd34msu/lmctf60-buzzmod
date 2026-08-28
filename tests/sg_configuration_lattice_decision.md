# Host-float witness decision

## Decision

The configuration-space foundation uses Option B. The ISL solver handles
rational halfspaces over signed-q8 origins. It
does not model the binary32 multiply and add operations in
`SG_HostCollisionTrace`. Near a plane, the rational and host predicates can
disagree in either direction. The prototype therefore cannot prove that it
retains every host-valid origin.

Both options keep ISL and GMP in the offline constructor and auditor. Neither
option changes the runtime model, the loader, or the game DLL. ISL uses the MIT
license. GMP uses the LGPLv3 or GPLv2 license. The current test runners use
`pkg-config` on Linux. A Windows offline-generator package is not present. A
Windows game build does not need either library because it reads frozen output.

## Option A: widened ISL sets with host refinement

For each host-sensitive plane, compute a directed binary32 error bound for the
exact operation order in `sg_host_collision.c`. Widen the rational halfspace by
that bound. The widened ISL set contains every q8 origin that the host plane
predicate accepts.

After ISL returns a point, evaluate the point with the host. If one host plane
rejects the point, use the signs of that plane's coefficients to remove the
rejected monotone orthant. IEEE round-to-nearest multiplication and addition
are monotone for finite operands. Every point in that orthant fails the same
host predicate, so the removal cannot delete a host-valid point. Repeat on the
remaining union of ISL sets.

The search terminates because each rejection removes at least the returned q8
point from the finite `65536^3` domain. The remaining-domain cardinality is the
termination measure. If ISL reports an empty set, no host-valid point exists in
the original region. If the host accepts a point, the point is a witness.

This argument proves sound results and finite completion, but it does not prove
the requested polynomial work bound. In three dimensions, rejected candidates
can form a two-dimensional antichain. The refinement count can therefore grow
as `65536^2`. That bound is too large for a per-cell or per-portal operation.
Option A satisfies literal host completeness and isolated rounded-point
behavior, but it weakens the generator scaling requirement.

The refinement loop is not implemented, so there is no honest refinement
measurement yet. The exact-real baseline completed the six current synthetic
builds with 256 constructor solves and 620 audit solves. Those solves emitted
3,909 and 12,306 integer constraints, respectively. The largest binary
coefficient shift was 28. All host checks accepted after duplicate portal
vertices were removed. This result measures ISL overhead only. It does not
measure the hard rounding cases. The 175-map run remains blocked because the
freeze has no authoritative per-map identity snapshot.

## Option B: positive-volume topology with host interior witnesses

Keep the continuous BSP and expanded-brush decomposition as the definition of
a configuration cell. Retain a cell only when it has a representable q8 point
that the host accepts. Retain a portal only when both positive-volume sides
have q8 witnesses and `SG_HostCollisionTransition` accepts their crossing.

This option treats host-only points caused by binary32 rounding at a geometric
boundary as localization behavior. Such an isolated point or zero-volume set
does not create a cell or a portal. The auditor proves the positive-volume
topology and checks every recorded witness against the host.

Option B keeps finite convex cells, deterministic IDs, positive-volume portal
geometry, disconnected components, and practical fixed-dimension rational
lattice work. It does not satisfy a literal requirement to preserve every
host-valid q8 origin. The auditor also cannot report omission of a host-only
zero-volume point because the model excludes that point by definition.

The current prototype implements most of Option B. The synthetic suite passes
under GCC, Clang, ASan, UBSan, and leak detection. It includes the lmctf01-style
sub-q8 sliver, but it does not yet include the two required rational-versus-host
rounding disagreements. The full real-BSP measurement has the same missing
identity prerequisite as Option A.

## Comparison

| Requirement | Option A | Option B |
| --- | --- | --- |
| Every host-valid q8 origin | Satisfied | Weakened at rounding-only boundaries |
| Host-valid portals | Satisfied after refinement and transition checks | Satisfied for positive-volume geometric portals |
| Finite convex cell records | A refined answer is a union, so records need another decomposition step | Satisfied |
| No work cap or timeout | Satisfied | Satisfied |
| Proved practical work bound | Not satisfied | Satisfied for the rational solver, subject to an ISL algorithm audit |
| Runtime portability | Satisfied | Satisfied |
| Independent host audit | Satisfied if the auditor builds its own widened sets and refinements | Satisfied only for the positive-volume contract |

Option A is the only choice that preserves isolated host-only q8 points, but its
known termination bound is not acceptable for the 175-map generator. The
project defines navigable topology by positive-volume geometry, so Option B
does not create cells or portals for rounding-only boundary points. The missing
authoritative identity snapshot leaves the 175-map result as downstream proof,
not a claim of this foundation.
