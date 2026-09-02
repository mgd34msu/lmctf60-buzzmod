# Era-4 RUNE: contents and construction order

Recorded 2026-09-01 after the cell builder, regions, and compact geometry
landed as era-4 units. This is what the artifact holds and the order the
remaining units are written. Every era-3 unit named here is deleted when its
replacement runs on a real map.

## What the RUNE holds

1. **Identity.** BSP hash, byte count, checksum, entity CRC, hull profiles,
   physics law, host law ids. Binds one artifact to one map and one host.
2. **Cell complex** (era 4, done). Cells with stance validity, contents,
   Q8 bounds, leaf/area/cluster; facets with planes and polygons; incidences;
   portals with the shared facet, clearance, and valid stances; source
   surfaces (every solid brush side polygon in every model).
3. **Movement capabilities** (era 4, next). One record per portal crossing
   per admissible kind: cell, portal, kind, source and destination stances,
   profile. Kinds: walk, crouch, ramp, jump, drop, air control, swim, rocket
   jump, hook, mover, external force, controller action. A profile is a
   small shared record of analytic functions: cost and time over the input
   dimensions, position and velocity over time, reachability. Profiles are
   shared by every crossing of a kind under this map's gravity, so the
   movement section is the crossing records plus a dozen profiles.
4. **Analytic functions** (era 4, next). Constants, affines, polynomials,
   ballistics, piecewise with clauses, over named input dimensions with
   named output meanings. Flat coefficient store. One evaluator.
5. **Hook reach** (era 4, after movement). No per-target fibers. The runtime
   asks the engine's PVS (`gi.inPVS`) and traces exactly at tactic time. The
   RUNE stores per cluster the source surfaces in that cluster, so a cell
   (which carries its cluster) can list candidate attach surfaces in
   potentially visible clusters within hook range without a search. bctf01
   has 2,442 clusters and 25,203 surfaces.
6. **Mechanisms** (era 3 now). Doors, lifts, buttons, teleports, pushes as
   mechanism records with controllers and transitions bound to the portals
   they gate. Rewritten after movement.
7. **Weapons** (era 3 now). Profiles from the host weapon law; per-cell
   kernels as analytic functions over the same visibility as the hook.
   Rewritten after mechanisms.
8. **Wire.** One versioned section per array, counts, checksums, canonical
   order, fail-closed loader. Rewritten with exactly the sections above.

Gone from the artifact: the response projection (fragments, halfspaces,
patches, splits, facts, candidate and endpoint groups, occluders, seal),
movement states and fiber function refs, hook targets, angular schedules,
mechanism authorities and their topology, static transition indices.

## Order of the remaining units

1. analytic library and evaluator
2. movement capabilities (the emission rules already written today move in)
3. model, composer, wire, artifact publication
4. runtime: loader, localizer, field solver, tactic runtime consuming the
   era-4 records; the executor stays
5. hook reach; mechanisms; weapons

After step 4 a bot walks, jumps, drops, and swims on a generated map. That
is the first end-to-end run and it happens before hook and weapons.
