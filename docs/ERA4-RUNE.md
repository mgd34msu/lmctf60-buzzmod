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
3. **Movement capabilities** (era 4, done). One record per crossing per
   admissible kind: cell, portal, destination cell, kind, source and
   destination stances, profile, launch velocity, seconds. Contact
   crossings (walk, crouch, swim, step up within a jump, step down, rocket
   step) arrive in the portal's other cell. Flights (jump, drop, rocket
   jump off a floor into the air) are traced at generation: the arc under
   the host's law is a polynomial, every facet is a plane, so the exit from
   each cell is a root and the arc is followed exactly cell to cell until it
   meets a floor or water; that landing cell is the record's destination
   and the launch that got there is stored. Three launches per wall opening
   (drop, jump, rocket jump at full run speed toward the opening). No
   per-portal air-control records: in-flight control evaluates the one
   shared air profile with the live velocity through the same tracer
   (`sg_rune_flight`). Profiles are shared by every crossing of a kind under
   this map's gravity; the rocket profile's cost carries the health the
   blast takes.
   bctf01: 98,558 records (39,208 walk, 39,596 crouch, 6,669 jump, 6,275
   drop, 6,810 rocket jump).
4. **Analytic functions** (era 4, done). Constants, affines, polynomials,
   ballistics, piecewise with clauses, over named input dimensions with
   named output meanings. Flat coefficient store. One evaluator.
5. **Hook reach** (era 4, done). `sg_rune_hook`: from every floor cell,
   the hookable world surfaces (centroid, two units off the face) in the
   clusters the map's own visibility says are in view, within rope range
   and above the eye, facing it: each survives an exact bolt trace from
   the eye, then the pull is traced with the crouch hull toward the bite,
   and letting go at half, three quarters and the whole of the clear pull
   is traced as a flight to a landing.  Each distinct landing is one HOOK
   record: fire at this bite from this cell, ride, let go, land there. The
   runtime keeps the hook step while the rope is out and lets go once the
   body is at the landing's height, over it, or about to meet the bite.
   bctf01: 6,575 bites, 165k records from 23.6k floor cells, 5.6 s.
6. **Mechanisms** (era 4, done). `sg_rune_mechanisms`: doors, lifts,
   buttons, teleporters, pushes, trains read from the host's entity
   semantics into records {kind, activation, bmodel, entity, activator,
   rest bounds, travel, speed, wait, gate cells}.  Before movement a lift
   or train rest top is stamped floor; after movement the records add
   their own crossings (ride, teleport, push flight, train legs) and gate
   the crossings into doors.  The runtime binds each record to its live
   edict and reads state from it.  bctf01: 12 pushes, 1 train.
7. **Fire relations** (era 4, done). `sg_rune_fire`: between the floor
   cells with standing room (32 units each way), in the clusters the map
   says may see each other, within 1,536 units: a clear line eye to eye
   (rays), a clear corridor for a projectile body to the other body, a
   rocket at the feet whose burst reaches them (blast), and when no line
   exists a grenade arc at four pitches that lands within its burst (lob).
   Every other floor cell borrows the row of the nearest representative;
   a target resolves the same way.  The runtime scales each weapon
   family's expected damage by the relation for the pair of cells the
   fight is in, and places defenders on the cells whose lines cover the
   most of the flag's approaches (the second where the first leaves gaps),
   facing the approaches they cover.  bctf01: 3,365 representatives, 1.56M
   pairs, 698k records, 7 s, 5.3 MB.
8. **Wire** (era 4, done). `sg_rune_artifact` v3: one image, a header with
   identity and law, seventeen sections of fixed-layout records behind a
   CRC, every reference validated on load with the failing record named,
   arrays borrowed from the image. bctf01: 110 MB, 0.9 s to load. Too big:
   portal and facet provenance and the vertex arrays dominate; stripping
   them and merging cells is the size work, after the first walking bot.

## Runtime (era 4)

- `sg_rune_locate`: a 128-unit grid over the cells; a point resolves to the
  cell whose facet planes contain it, preferring a cell valid for the
  stance and then a supported one (a floor cell's top is an air cell's
  bottom). bctf01: 33x32x9 buckets, 210k entries, built in 60 ms.
- `sg_rune_field`: the router indexes arrivals per cell and the cost of
  every record once per level; a field is one destination's reverse
  shortest-path pass over (cell, stance) states, 4 ms on bctf01 over the
  19,966 floor states; a step is what the body does now from its state:
  arrived, cross this portal this way toward this point, or unreachable.
  Air cells have no outgoing records, so routing never leaves the floor
  except by a traced flight.
- `sg_tactic_controller`: step and body in, command out. Walk, crouch,
  swim, jump (press within the jump's reach), drop, rocket jump (launcher
  in hand, within reach, aim down, fire and jump), hook (fire at the bite
  point, release at the crossing).
- `sg_rune_level` (game): loads `<gamedir>/maps/<map>.rune` at level start,
  refuses it when its identity or law differs from the live host (message
  names what differs and says to run `sv rune`), builds the locator and
  router, keeps eight destination fields alive by use.
- `sg_bot_frame` (game): the driver. Role from carrying and chat orders
  (carry, recover, escort, defend, attack); destination from the flags and
  carriers; locate; the destination's cell; the field's step; a body in
  the air is traced to its landing and steered there; the executor's
  command becomes the usercmd the host's client think consumes; combat
  owns the view unless the step does; hook fire and release go through the
  same entry points a human's commands reach.
- `tools/fieldcheck`: loads an artifact, locates two points, builds the
  field, walks the chain. Red flag to blue flag on bctf01: 130 steps, 118
  walks, 3 jumps, 7 drops, 2 rocket jumps, 26 s of cost.
- `tests/sg_rune_runtime_test`: a synthetic complex (three floor cells, an
  air column, a lower floor) through locate, movement emission with traced
  flights, the flight tracer, the field, and the step chain.

## What the field state does not carry

The field state is (cell, stance). Velocity is not in it; flights are
recorded per launch instead, and a body already airborne is traced live.
Recorded 2026-09-01 for the owner's ruling; the alternative is a field
over (cell, stance, vertical velocity band).

## Deleted 2026-09-01

The era-3 generation pipeline (builder, composer, partitions, static
visibility, materializer, movement fields, mechanism builders, weapon
relations and field, generation, game entry) and the era-3 runtime
(model, wire, artifact loader, production, field service, localization,
belief runtime, strategy caller and bridge, tactic runtime, policy,
execution, learning, sidecars) with their tests. The game module does not
build until the era-4 level owner and bot driver replace the call sites in
`slipgate/sg_arach.c`, `sg_combat.c`, `sg_caco.c`, `sg_fields.c`,
`sg_client.c`, `sg_bot.h`, `p_client.c`, `g_main.c`, `g_save.c`.
`sg_rune_source_authority` stays: it is host binding (entity spawn records
for the host identity), not RUNE content.

Gone from the artifact: the response projection (fragments, halfspaces,
patches, splits, facts, candidate and endpoint groups, occluders, seal),
movement states and fiber function refs, hook targets, angular schedules,
mechanism authorities and their topology, static transition indices.

## Order of the remaining units

1. analytic library and evaluator (done)
2. movement capabilities (done)
3. artifact and publication; in-game generation entry `sv rune` (done;
   bctf01 generates in 19 s in-game and cold-loads)
4. runtime: locator, router, field, step, executor, level owner, driver,
   roster, orders, combat, items, team roles (done); the flood is gone
5. mechanisms (next); hook reach; weapons in the RUNE; callouts; artifact
   size

After step 4 a bot walks, jumps, drops, and swims on a generated map. That
is the first end-to-end run and it happens before hook and weapons.
