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
   **The world as it stands (2026-09-02).** The carve subtracts the
   player-solid brushes of model 0 and of every brush model that is solid
   from spawn on: a `func_wall` with no trigger flags or with START_ON, a
   `func_explosive` not waiting for a trigger, a `func_object` with no
   flags (`SG_RuneEntitiesStatics`, declared on the BSP with
   `SG_RuneBspSetStatics`; movers are mechanisms, not statics). Every
   world trace (`SG_RuneTraceBox` on model 0, poses, point contents) sees
   those models too, so hook pulls, fire lines and support tests agree with
   the engine. bctf01 builds its base structures and both lava pits as
   twelve `func_wall` models; before this the builder saw open floor where
   the walls stand and a dry pit floor at z -388 under the lava, which
   made bots walk into walls, re-fire the same hook into a wall, and drop
   into the pits (203 lava deaths in one ten-minute game, 12 of them after
   taking damage).
   **Hazard cells.** Lava and slime brushes, world or static, are not
   carved away: the free pieces are split by each such brush raised by the
   feet's depth below the origin (the point the engine tests for water
   level), and what is inside is a cell flagged `HAZARD`, never supported.
   Portals into a hazard cell make no crossing; a flight that enters one
   ends with outcome `HARM` and makes no record; a body already in one
   keeps its departures so it can climb out. Schema id 0x...0002: every
   earlier artifact is refused and regenerated.
   **Portals across stances (2026-09-02).** The portal pass pairs faces by
   construction key alone; a standing cell and a crouch-only cell that share
   a face get a portal whose stance is crouching, and the witness sweep is
   made with the crouch hull. Before this, cells of different stances never
   paired, so every crouch-only sliver under a ceiling was sealed off and,
   worse, its sealed bottom face counted as a floor.
   **Support (2026-09-02).** A cell is supported when the host probe says so
   or when one of its closed facets faces down out of it *and is an expanded
   brush side*. Split planes and the domain never count. bctf01 lost half its
   "floor" cells to this rule (22,228 cells with hook rides became 11,022):
   they were ceiling slivers and other sealed faces, and hook rides and drops
   had been landing on them.
   **Run-up (2026-09-02, runtime).** A flight record is traced from the
   cell's middle at full run speed toward its portal; a hook ride from the
   cell's middle toward its bite. The step carries that point (`run_up`) and
   the flight's launch velocity, and the tactic controller lines the body up
   there first: behind the portal along the launch line, near the line, its
   own velocity along it, before it runs at the edge or fires the rope. The
   dense log showed bots running +y along a ledge, reaching a drop whose
   launch pointed +x, and leaving the edge with their +y momentum over the
   pit.
   **Floor-exact cells (2026-09-02).** A brush's sides are cut in the order
   walls, ceilings, floors. The piece outside a side is taken the moment
   that side is cut, so cutting the floor side last confines the piece
   above a floor to the floor's own footprint: a supported cell is floored
   everywhere and its edge is the floor's edge. Cut first, that piece
   reached out over whatever lay beside the brush, and bots walked off
   ledges inside one "supported" cell.
   **Rides that end at the bite (2026-09-02).** A hook ride's release arc
   (let go at 50/75/100% of the pull and fly) is the plan; a body that
   keeps riding is held at the bite and drops straight down. The builder
   keeps a bite only when that hanging drop lands on a floor that is not
   lava or slime. At runtime the release is let through only when the exact
   tracer, run from the body's cell with the velocity it has now, lands it
   on such a floor; otherwise the body rides to the bite, hangs, and the
   stall rule lets go there without avoiding the ride. While the bolt is
   still flying a supported body stands still. A body whose live flight
   ends in harm or the void fires its rope at the best bite recorded from
   the floor it left (ahead and above) and hangs; a rescue rope that pulls
   nowhere is let go of after a stall and that bite is not tried again.
   The hanging point the builder checks is a hold's length back along the
   pull line (where the rope stops pulling), not the bite itself: a long
   shallow ride hangs well short of the wall, over whatever is below.
   Landings, flights and release arcs alike, must also survive a body
   length's fraction to either side of the line (8 units).
   **Stance in place (2026-09-02).** The field relaxes a crossing from the
   other stance of its source cell too, at the stance-change cost, when the
   cell has room for it: a crouched body stands up to walk on, a standing
   one ducks to crawl. Before this a body crouched by a ride crawled the
   rest of its route at a third of run speed, because every walk edge
   wanted a standing body and nothing ever said "stand up".
   **Stuck (2026-09-02).** The stuck hop fires only for a plain contact
   crossing: a body easing to a point, lining up a launch, or holding a
   rope is slow on purpose, and a hop there is a hop off the ledge.
   **Launch commit (2026-09-02).** A launch being lined up is held while
   the body is within 48 of its run-up point and still behind the portal
   along the launch line: the cells at a floor's edge are small and a body
   settling on the point drifts across them, where the field would send it
   off again. Past the portal the field decides afresh. The run-up approach
   eases over two body lengths, since a frame at full speed covers one.
   The rope is fired only under a slow walk's speed, and a rope that never
   carried the body avoids its ride for a while. A hang is timed from its
   first frame at the bite, however the body sways there.
   **Posts (2026-09-02).** A defend post is a cell within three seconds of
   the flag that stands on floor, is neither water nor hazard, is a body
   length or more from the flag, and sees the most approach cells by the
   fire relations; it must also be reachable from the flag, which one
   forward field from the flag (`SG_RuneFieldBuildFrom`, over departures)
   answers for every candidate. There are no waypoints anywhere: posts,
   routes and every target are cells of the carved complex.
   **Support probes (2026-09-02).** A cell is floor only when the witness
   column and every corner of its footprint that lies inside the polytope
   stand on floor at the cell's own lowest height there; a wedge over a
   slope that is floored on one side only is air.
   **Sub-frame flights.** A flight over in under 0.15 s is a step onto the
   floor beside, which the contact crossing already makes; it is no record.
   **Strategy holds until an event (2026-09-02).** The team pass reassigns
   roles only when the team's situation changes: where each flag is (home,
   carried, dropped), who carries, which bots are on the team, what a human
   ordered. Between events a bot keeps its role; a bot that dies respawns
   into the same role; the steps under a role change as they like. Before
   this the pass re-scored every frame with a stickiness weight and a bot
   flipped between attack, escort and defend within seconds.
   **Run-up length and the jump press (2026-09-02).** A flight record left
   its portal at full run speed; a body needs a frame's run to be at that
   speed, so the run-up sits at least 48 units behind the portal along the
   launch line, moved back onto whatever floor is there when the cell's
   middle is nearer. Jump is pressed on the frame that would carry the body
   over the portal, and only at speed; pressed at the run-up from a standing
   start it fell short (one such jump over the blue pit drew 91 rescue ropes
   in four minutes). A launch that ends in a rescue is avoided for a while.
   **No ricochet rides (2026-09-02).** The flight tracer counts the walls and
   ceilings a body glances off; a hook release arc must reach its floor with
   none. bctf01 had a ride whose landing on a ledge came from bouncing off a
   45-degree bevel at a ceiling's edge (bite at 502 -820 175, ceiling at z
   192): the model predicted the bounce one way, the body went another, the
   live check refused the release, and three bots hung at that bite in turn.
   A ride that ends hanging at the bite without its drop reaching the
   record's landing is avoided; the avoid list holds 16 for 60 seconds.
   A rescue rope is one per fall and its hang is three seconds at most.
   **Powerup objective (2026-09-02).** The quad and the techs are objectives:
   a powerup a teammate sees standing on floor (in the PVS, a clear line
   from the eye, within 1500) becomes known to the team for 45 seconds and
   is called out; the situation changes, and the nearest free bot that can
   use it (no tech in hand for a tech) takes the powerup role until it is
   gone. **Defender patrol.** A posted defender with nothing in sight goes
   for the best pickup within 640 of the flag for up to eight seconds and
   returns to the post; the role stays defend. **Idle view.** With nothing
   in sight and nowhere to go the view holds; posted defenders face their
   approaches. **Out of the lava.** A body in lava or slime fires its rope
   at the best bite it knows, from the floor it left or from any ride
   recorded within 256.
   **Moving like the owner (2026-09-02).** Measured from four minutes of
   the owner's play (`SGHUMAN` trace): above run speed 69% of the time,
   airborne 53%, strafing 73%, the rope a burst fired mostly from the air
   at ~290, attached ~0.6 s, let go in the air at ~500; air acceleration is
   off on this server, so all speed above 300 is rope momentum kept alive
   in the air. The bots now: fire the rope on the move when their velocity
   points at the bite, keep running at the bite while the bolt flies over
   floor, hop on every safe walk (the exact tracer says the hop lands
   cleanly and the run after it stays on floor), follow the route from the
   landing cell while airborne and fire rides from the air, and fire only
   at a bite the eye can see (a bite behind a wall avoids its ride). After
   the first of these the bots measured mean 254, median 268, top tenth
   360, above 300 for 39% of the time.
   **The players' standard (2026-09-02, from demos).** `tools/dm2trace.py`
   reads a Quake II demo: the recorder's state every frame and every other
   player's position while in view, the rope through the grapple-cable
   effect. The best LMCTF players in the owner's archive (seedless, Em,
   Zest, sinsemilla) move at a mean of 350 to 380, median about 320, top
   tenth 700 to 750, above run speed 61 to 74% of the time, and rope 19 to
   31 times a minute in bursts of about half a second, fired at 370 to 470
   and released at 510 to 570. The rope is their locomotion. The builder
   therefore prices a ride at what it takes a running body: half the bolt's
   flight, the pull, the release arc, and no fixed penalty; before this a
   walk out-priced the rope on most edges and bots roped eight times a
   minute. The archive-wide numbers (155 demos, about thirty hours of
   visible movement) are in `docs/ERA4-PLAYERS-STANDARD.txt`: the four best
   rope 19 to 28 times a minute; the owner 4.6.
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
   is traced as a flight to a landing, with the velocity the host's pull
   gives at that distance from the bite (full speed beyond 120 units, then
   slower by bands, gravity off within fifty).  Each distinct landing is
   one HOOK record: fire at this bite from this cell, ride, let go when the
   eye is the record's distance from the bite, land there. The runtime
   keeps the hook step while the rope is out, lets go at that distance (or
   at the landing's height, or over it), and a rope that pulls the body
   nowhere for a second and a half is let go and that ride avoided for
   thirty seconds.  bctf01: 6,575 bites, 182k records from 23.7k floor
   cells, 4.2 s.
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
7b. **Faces and flights** (era 4, 2026-09-02). The complex keeps one boundary
   facet per configuration face (only where the face keeps area beyond its
   portals) plus one facet per portal; nothing is cut into remainders, so an
   open face that touches thousands of cells costs one facet, not a square
   of them (bmap4: 285k facets, 26 s end to end, where the cut version ran
   for hours on 68M facets). A flight leaving a cell through a plane shared
   by a portal and its face goes through the portal whose cell beyond
   contains the exit point (cells are convex, so that is exact with planes
   alone); otherwise the face is a wall. Flights are lost past 96 crossings
   or 8 seconds; the fire pass traces the nearest 1,500 targets per source
   and lobs at the nearest 256 without a line.
8. **Wire** (era 4, done). `sg_rune_artifact` v4: one image, a header with
   identity and law, seventeen sections of fixed-layout records behind a
   CRC (no facet vertices: each portal carries its foot; bctf01 108 MB), every reference validated on load with the failing record named,
   arrays borrowed from the image. bctf01: 110 MB, 0.9 s to load. Too big:
   portal and facet provenance and the vertex arrays dominate; stripping
   them and merging cells is the size work, after the first walking bot.

## Runtime (era 4)

- `sg_rune_locate`: a 128-unit grid over the cells; a point resolves to the
  cell whose facet planes contain it, preferring a cell valid for the
  stance, then a floor cell the body is within two units over (a body at
  rest sits a unit or two over the carve's floor level), then the nearest.
  bctf01: 33x32x9 buckets, 210k entries, built in 60 ms.
- `sg_rune_field`: the router indexes arrivals and departures per cell and
  the cost of every record once per level; a field is one destination's
  reverse shortest-path pass over (cell, stance) states, 4 ms on bctf01;
  a weighted build adds a per-cell surcharge (exposure). A step is what
  the body does now from its state: arrived, cross this capability toward
  this point (a portal's foot pushed sixteen units into the next cell, a
  hook's bite and landing, a mechanism's landing), or unreachable. A
  selection that avoids listed capabilities takes the cheapest other
  departure by edge cost plus the field beyond it.
- `sg_rune_fire` (runtime half): the relation for a pair of cells, either
  cell resolved to its representative.
- `sg_tactic_controller`: step and body in, command out. Walk, crouch,
  swim, jump (press within the jump's reach), drop, rocket jump (launcher
  in hand, within reach, aim down, fire and jump), hook (fire at the bite,
  push toward the landing, let go at the landing's height, over it, or at
  the bite).
- `sg_rune_level` (game): loads `<gamedir>/maps/<map>.rune` at level start,
  refuses it when its identity or law differs from the live host, builds
  the locator and router, keeps eight fields alive by use (plain and
  exposure variants), binds mechanism records to live edicts, answers fire
  relations, chooses defend posts (the floor cells whose lines cover the
  most of a flag's approaches, reachable from the flag, the second where
  the first leaves gaps), and builds the exposure surcharge for a base
  (every cell entered under its posts' or flag's lines costs a quarter second more, so a 300-unit crossing under fire costs about as much as a three-second detour).
- `sg_bot_frame` (game): the driver. The team pass assigns roles once per
  frame per team (recoverers nearest a taken flag, defenders when there
  are enough of us, an escort for the carrier, the rest attack; a held
  role counts as nearer so roles do not flap; human orders override).
  Destinations: the enemy flag or its carrier, our flag or its carrier,
  the carrier to escort (held a second behind), a defend post. Attackers,
  carriers and escorts route on the exposure field of the enemy base.
  Locate; the field's step, never through a crossing that failed on this
  body lately; a rope that pulls nowhere is let go and its ride avoided;
  a body in the air is traced to its landing and steered there; item
  detours priced against the goal; teammates close ahead are given way;
  the executor's command becomes the usercmd the host's client think
  consumes; combat owns the view unless the step does; a posted defender
  with nothing in sight faces the approaches its post covers.
- `sg_bot_combat` (game): sight and hearing, target choice (our flag's
  carrier first, else nearest, kept two seconds), weapon choice by expected
  damage per second under the aim error, scaled by the fire relation for
  the pair of cells (rays need a line, projectiles a corridor, rockets a
  corridor or a blast at the feet, grenades a corridor or a lob), a switch
  finishes before another is asked for; aim with lead and arc; fire when
  the aimed ray reaches the target or passes within the body plus the
  weapon's spread at that range, never into our own blast.
- `sg_bot_callout` (game): team talk for what a teammate cannot see: the
  enemy carrier and where, incoming at our base, our flag taken, dropped,
  home, got their flag and which way, need cover, role changes; places
  named by route time to each flag; rate-limited per bot and per team.
- `sg_bot_roster`, `sg_bot_orders`, `sg_bot_items`: add, remove, list
  bots and personas; human orders in; item worth by role.
- `tools/fieldcheck`: loads an artifact (section sizes, kind tally),
  locates two points, builds the field, walks the chain; the generation
  script's acceptance gate. `tools/cellsdump`: the whole offline pipeline
  with counts and timings.
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
The host-facing units that survived that pass (BSP reader, collision,
host laws, identity, source authority, hooks bridge, net bridge, weapon
profiles, personas, cvars, helpers) were rewritten or removed on
2026-09-02; see ERA4-REVIEW.md.

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

## Every map (2026-09-02, in-game generation, bounded pipeline)

bctf01 (108 MB, 27.6 s as bmap1, the same geometry) plus:

| map | MB | cells | portals | capabilities | mechanisms | hooks | fires | seconds |
|---|---|---|---|---|---|---|---|---|
| bmap1 | 103 | 113,616 | 679,584 | 348,229 | 13 | 168,027 | 692,699 | 27.6 |
| bmap2 | 45 | 44,942 | 240,078 | 225,687 | 0 | 92,240 | 319,503 | 6.0 |
| bmap3 | 51 | 17,960 | 84,052 | 70,255 | 2 | 38,147 | 79,105 | 4.0 |
| bmap4 | 59 | 62,751 | 354,691 | 254,920 | 6 | 115,704 | 145,831 | 19.3 |
| bmap5 | 42 | 42,345 | 210,342 | 164,174 | 3 | 84,657 | 435,842 | 7.1 |
| bmap6 | 39 | 38,047 | 189,354 | 166,881 | 13 | 73,256 | 550,708 | 5.2 |
| bmap7 | 15 | 12,571 | 69,876 | 66,045 | 2 | 23,943 | 285,373 | 2.2 |
| bmap8 | 41 | 39,267 | 217,729 | 155,375 | 0 | 60,105 | 701,176 | 5.4 |
| lmctf02c | 67 | 70,258 | 423,877 | 228,437 | 12 | 91,762 | 485,801 | 8.3 |
| lmctf09 | 66 | 75,047 | 419,559 | 234,772 | 2 | 110,823 | 234,309 | 15.2 |
| lmctf12 | 72 | 74,706 | 440,683 | 292,129 | 11 | 108,036 | 540,062 | 12.1 |
| lmctf22 | 41 | 46,566 | 254,423 | 172,067 | 0 | 73,834 | 191,705 | 6.3 |
| lmctf26 | 18 | 17,977 | 97,910 | 80,581 | 8 | 38,481 | 102,832 | 2.5 |
| lmctf31 | 34 | 33,628 | 199,036 | 164,634 | 4 | 71,824 | 162,708 | 6.0 |
| lmctf32 | 20 | 18,299 | 103,036 | 78,761 | 4 | 35,034 | 409,501 | 3.8 |
| lmctf35 | 22 | 15,294 | 80,408 | 88,675 | 6 | 36,623 | 850,731 | 4.2 |
| lmctf39 | 20 | 20,167 | 103,400 | 81,765 | 2 | 42,951 | 238,163 | 3.5 |
| lmctf41 | 73 | 73,170 | 413,540 | 407,027 | 14 | 171,508 | 55,036 | 16.9 |
| lmctf44 | 53 | 60,388 | 323,195 | 180,539 | 16 | 77,818 | 147,881 | 7.5 |
| lmctf48 | 41 | 36,040 | 194,960 | 210,665 | 8 | 93,775 | 538,694 | 6.6 |
| lmctf50 | 56 | 50,040 | 330,780 | 115,848 | 2 | 27,371 | 1,718,983 | 9.9 |
| lmctf51 | 150 | 144,846 | 996,645 | 711,347 | 0 | 69,186 | 227,581 | 24.4 |
| lmctf52 | 54 | 46,192 | 259,978 | 183,013 | 6 | 71,140 | 1,680,082 | 10.9 |
| lmctf53 | 21 | 18,240 | 102,766 | 107,501 | 4 | 37,294 | 264,832 | 3.2 |
| lmctf54 | 55 | 72,785 | 417,444 | 152,994 | 0 | 52,341 | 76,518 | 11.6 |
| lmctf57 | 26 | 26,429 | 134,854 | 151,089 | 2 | 65,865 | 41,647 | 2.3 |
| lmctf58 | 100 | 108,505 | 663,898 | 397,913 | 41 | 109,225 | 549,806 | 16.6 |
| lmctf63 | 32 | 31,818 | 180,356 | 117,348 | 4 | 47,861 | 461,461 | 5.4 |
| lmctf74 | 31 | 27,676 | 150,187 | 132,642 | 0 | 54,433 | 593,792 | 9.3 |
| lmctf76 | 52 | 54,236 | 266,754 | 279,555 | 0 | 117,809 | 212,754 | 14.6 |
| mactf06 | 38 | 33,644 | 195,494 | 170,772 | 8 | 70,354 | 653,425 | 9.2 |
| smap05 | 31 | 30,041 | 169,771 | 157,245 | 29 | 56,929 | 297,142 | 4.6 |
| smap26 | 47 | 50,589 | 295,789 | 141,643 | 4 | 57,654 | 472,027 | 10.2 |
| smap30 | 22 | 22,432 | 123,460 | 85,065 | 2 | 38,377 | 270,889 | 3.4 |
| smap33 | 64 | 69,918 | 414,855 | 152,017 | 2 | 47,944 | 972,096 | 17.3 |
| tomb05 | 5 | 3,947 | 20,708 | 24,231 | 8 | 7,605 | 83,828 | 0.4 |
| xmap20 | 12 | 12,728 | 66,790 | 52,031 | 2 | 18,730 | 72,782 | 1.7 |

## 2026-09-02 afternoon: what the owner's play and the demos exposed, and the fixes

Measured against the players' standard (docs/ERA4-PLAYERS-STANDARD.txt) the
bots were slow on the rope and stuck in places the owner runs through.  The
causes, each found against the BSP with the tools below:

- Flags that float at their spawn point (lmctf09) drop to the floor in the
  game; the flag's own dropped origin is the stand when it is at home, and
  StandingCellNear drops a trace to the floor when no probe finds a
  supported cell.
- Locate (sg_rune_locate.c) tested a point against a cell's facets only; a
  two-facet sliver is an unbounded slab and swallowed points a hundred units
  away, parking bots with a walk toward a target above them.  The cell's
  box now bounds the test.
- The carve (sg_configuration_cells.c) cut every piece of a leaf by every
  side plane of every brush the leaf held, so a floor slab's wall plane split
  the free space above the slab into a sliver and a cell.  A brush that lies
  wholly outside one side of a piece leaves it uncut (PieceClearOfBrush).
  lmctf09 went from 77k cells (25k thinner than four units) to 24k (3k).
- The portal pass keyed faces by the hull variant that expanded the plane;
  a wall expanded for the crouch hull and for the standing hull is the same
  plane, and the faces on it never met: the red base of lmctf09 was an
  island of 5,600 states.  The key leaves the variant out and SameKey
  accepts equal distances.  The owner's trace, bisected for reachability
  flips from the blue base, went from 37 flips to 16, all airborne.
- Rides let go one frame after the bite (attached 0.1 s, released at running
  speed) because the release rule fired the moment the bite was within the
  record's release distance.  A ride lets go only once the pull carries the
  body (480 along the pull) unless it is already inside the pull's slow band.
- The ride builder kept the nearest bites and none beyond 48 units; the
  players fire far.  It keeps the farthest bites in view and none inside the
  pull's slow band plus 48; rides carry a momentum credit (0.7) when they
  release at 400 forward, and bites need only be 4 above the eye.
- A jump stalled against its ledge (within 40 of the portal, under 60 speed)
  is pressed from a stand.
- A body hanging over harm holds on: it lets go only where the drop is safe
  or where another rescue rope can catch the fall; a fall may fire up to
  three rescue ropes.
- A crossing the body is stuck on for two seconds is avoided, not retried.

Tools: fieldcheck `n x y z r` lists every cell near a point, `stats` reports
cell shapes, the `d` dump names each facet's brush side or BSP plane;
bsppoint names the brush a hull starts inside and prints its sides;
cellsdump.gnu regenerates a map in seconds (lmctf09 6 s, bctf01 26 s);
SG_CFG_WATCH_D=<distance> makes the carve print the portal pass on one
plane; tools/dm2trace.py reads demos.  The bot log line carries the command
(cmd=status(direction)xspeed up=), the rope log names held and taken
releases.

## 2026-09-02 evening: the players as the model

- Every player in the 155 beatdown demos is measured by entity track
  ($CLAUDE_JOB_DIR/tmp/demoplayers.py; docs/ERA4-PLAYERS-STANDARD.txt).  Zest
  is the target (30 ropes a minute, 36 sharp turns a minute, 9 steals and 4
  captures per ten minutes); Lequin ("leq", "lequen") second.  vereke is not
  a model (owner).
- The players' rope bites come out of the demos too (tools/dm2trace.py
  keeps every cable's ends; $CLAUDE_JOB_DIR/tmp/humanbites.py writes
  maps/<map>.bites: fire x y z, bite x y z, one line per bite, deduped on a
  16-unit grid).  The ride builder reads the file beside the BSP
  (SG_RuneHookSetHumanBites, set by cellsdump and the in-game generator),
  traces each bite from its fire point, and adds the wall it meets to the
  bite table with its own candidate pool beside the surfaces' centres.
  smap26: 960 of 2,974 demo bites verified; rides 18k -> 28k records.
- The driver's footwork is timed from the owner's play: strafe reversals
  after 0.5-1.4 s (his median 1.1), a hop on the reversal; idle footwork at
  a post or a stand; a rope that bites on the floor gets a hop so the pull
  is not dragged against friction (the owner releases in the air 92%).
- Team goals (sg_team_goal_t): take theirs together, bring it home,
  recover ours (defenders hunt, no home post), hold and retake, turtle at
  more than two captures ahead (one runner, everyone else defends).  A role
  whose destination stays unreachable falls back on the goal's role;
  attackers group up before the flag room; defenders support an engaged
  teammate in the base.
- Dislodge: still for three seconds -> back off with a hop; five -> rope to
  a bite nearby; eight -> reroute with the crossing avoided.

The players' bites are mod data, not a local file: data/bites/<map>.bites in
the repo (47 maps, 1.6 MB), copied to the game's maps/ by tools/deploy.sh, and
read by the in-game generator on any install.  A release ships them under
maps/ beside the module.
