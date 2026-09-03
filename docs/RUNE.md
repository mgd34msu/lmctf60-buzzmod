# RUNE: the bot route data

A RUNE is the file that tells SLIPGATE bots how a map can be moved through.
There is one per map, `maps/<map>.rune`, built by the game module from the
map's BSP and the movement rules of the engine. This document explains what
it contains, how it is built, how the bots use it, and how to look at it.

## Contents

- [One surface, layered fields](#one-surface-layered-fields)
- [The layers](#the-layers)
- [The runtime gradient](#the-runtime-gradient)
- [Cells](#cells)
- [Crossings](#crossings)
- [Rope rides](#rope-rides)
- [Fire relations](#fire-relations)
- [Fields and steps](#fields-and-steps)
- [Building in the game](#building-in-the-game)
- [Hint files](#hint-files)
- [Tools](#tools)
- [Log lines](#log-lines)

## One surface, layered fields

A RUNE is one higher-order surface. Its domain is the configuration space of
the player's body: the cell complex, every region of the map the body's
centre can occupy, in both stances. Over that one domain the file carries
layered volumetric fields, each describing a different aspect of the same
space: what stands under each cell, how the body can leave it, where a rope
can take it, what a weapon can reach from it, what each move costs. Every
layer is an array laid over the same cells, so a question about a place is
answered by reading the same index in each layer.

Nothing in the file is a sampled instance of behaviour. Where behaviour
depends on a continuous quantity, the file stores a function of that
quantity and the runtime evaluates it: a jump's arc is a polynomial in time,
a facet is a plane, the arc's exit through a facet is a root. The same
evaluator serves the builder's proofs and the running game.

The file is exact within a declared quantisation. Positions are stored in
eighths of a unit and planes as 32-bit floats, so a cell means the same
thing to the builder, the loader and the bot.

## The layers

| Layer | Contents | Consumer |
|-------|----------|----------|
| Domain | Cells with bounds, stances and contents; facets with planes and vertices; incidences; portals with clearance and direction; vertices | Locate, every other layer |
| Static semantics | Per cell: supported, hazard, water, sky and void boundaries, mover volumes | Route search, hazard avoidance, rescue |
| Movement capabilities | Per portal or per landing: walk, crouch, ramp, jump, drop, swim, rocket jump, mechanism crossing; each with cost, stances, run-up and launch velocity | The cost fields, the tactic controller |
| Rope | Per floor cell: rides to a bite point with the release distance and the landing cell; hookable surfaces | Ride selection, release, rescue anchors |
| Mechanisms | Lifts, doors, buttons and what crossing waits on what | Crossings that wait, the executor |
| Fire relations | Per pair of cells that see each other: which weapon families work from here on there, and the posts that cover an approach | Weapon choice, defender posts |
| Analytic functions | Polynomials and piecewise functions over named inputs (distance, hook length, time, velocity, direction) | The builder's proofs and the runtime alike |
| Cost | Time-weighted, directional costs on every capability | The fields; nothing is ever pruned for being costly, only priced |
| Identity and law | Checksums of the map and its entities, the movement law the file was built under, the schema | Loading; a mismatch triggers a rebuild |

## The runtime gradient

At runtime a destination becomes a cost field over the complex: for every
state, the cost to go and the departure that achieves it. The field's state
is the pair (cell, stance). Its gradient at a cell is the departure with the
least cost to go, and a bot follows that gradient one crossing at a time.
The field says which crossing to take; the tactic controller decides how and
when to press what. That split is the whole design: strategy in the field,
tactics in the executor.

Velocity is not an axis of the field. A launch is a record that carries the
velocity it was traced with, and the runtime evaluates the live velocity
against the same analytic profile when a body is already airborne. Fields
are built from the destination backward over the crossings and cached per
destination cell.

## Cells

The module builds cells by subtracting the map's brushes from every BSP leaf
with the player's crouch hull. The result is a set of convex regions in
configuration space: a point in a cell is a position the body's centre can
occupy without touching a wall. Walls, ceilings and floors are cut in that
order, so each cell's bottom is a floor exactly where it has one.

Every cell carries:

- Bounds and a list of facets. A facet is one side of the cell. A facet
  shared with a neighbouring cell is a portal; a facet against a wall is
  closed.
- Stances: whether a standing body, a crouched body, or both fit.
- Semantics: `supported` when a floor is within reach of the cell's bottom,
  `hazard` for lava and slime, `water` for swimming, and markers for sky and
  void boundaries and for the volumes moving brushes sweep through.

Brush models that never move, such as `func_wall`, `func_explosive` and
`func_object`, count as walls. Lava and slime volumes become hazard cells
raised by the depth at which the body starts taking damage; nothing routes
into them, and a flight that enters one is recorded as harm.

Two rules keep the cells clean. A brush that lies wholly outside a piece of
a leaf does not cut it, so a floor slab's wall plane never splits the free
space above the slab. A side that grazes a piece by less than a unit leaves
it uncut, so there are no sub-unit slivers. Portals pair facets by the plane
they lie on, whichever hull produced them, so a wall opening carved for the
crouch hull and for the standing hull is one opening.

## Crossings

A crossing is a proven move from one cell into a neighbour through a portal,
or, for a flight, into any cell the flight lands in. The builder proves each
crossing with the same movement laws the engine applies to players: run
speed, jump velocity, gravity, step height, the hull sizes, the frame length.
A crossing that the engine would not allow is not recorded.

| Kind | Proof |
|------|-------|
| Walk | The floor continues across the portal within step height, measured at the portal |
| Crouch | As a walk, with the crouch hull and the crouch speed |
| Ramp | A walk up a slope the engine treats as floor |
| Jump | A run-up to the portal and a launch velocity; the flight is traced and must land on a supported cell under small errors of speed and direction |
| Drop | A step off an edge at run speed and at half run speed; the landing must be a supported cell and not harm |
| Swim | Movement through water cells |
| Lift, door | A declared mechanism the bot can use; the crossing waits for it |
| Rope ride | See below |

Every crossing records its cost in seconds, the stance it needs, and, for
launches, the run-up point and the launch velocity the flight was traced
with. Flights shorter than 0.15 s are discarded; a flight must land robustly,
which means it still lands when its speed is off by ten percent (fifteen for
a rope release) or its line by eight units.

## Rope rides

The grappling hook is the bots' main means of movement, so rides get the
most care.

For every floor cell the builder looks at the bite points in view: the
centres of hookable surfaces, and the anchor points in the map's hint file.
Candidates are the farthest bites in view that sit at least level with the
eye and outside the pull's slow band (the engine pulls at full speed only
while the bite is more than 120 units away). Up to 48 surface candidates and
48 hint candidates are kept per cell.

For each candidate the builder traces the bolt from the eye; it must reach
the bite without hitting anything. It traces the pull along the line from
the eye to the bite; the body must clear the geometry all the way. It then
considers releasing at 25%, 40%, 55%, 75% and 100% of that line, with the
pull velocity the engine would give at that distance, and traces the flight
from there. A release whose flight lands on a supported cell that is not the
cell it started from becomes a ride record. Where the ride would end hanging
at the bite, the hanging drop is checked as well.

The cost of a ride is half the bolt's flight time (the body keeps running
under the bolt) plus the pull time plus the flight time. A ride that
releases at 400 units per second or more of forward speed costs 30% less,
because the body carries that speed into the next crossing. Up to 16 ride
records are kept per cell, one per landing cell.

## Fire relations

For every floor cell, the builder traces from its eye to the floor cells in
the clusters the map says it can see: a clear line to the other eye for rays
(rail, chaingun, shotguns), a clear corridor for a projectile's body
(blaster, rocket, hyperblaster), a rocket at the other cell's feet whose
burst reaches them, and a grenade arc that lands within its burst where no
line exists. The runtime reads the flags for the pair it is in: which weapon
families can work from here on there, where a defender should stand to cover
an approach, and which cells an attacker is exposed from.

## Fields and steps

A destination is turned into a cost field: a shortest-path search from the
destination backward over the crossings, so every cell knows the cost to go
and which crossing to take. Fields are cached per destination cell and
reused while the destination stands still.

A step is what a bot reads from a field: the crossing kind, the target point
(the portal's nearest point for a walk, the run-up for a launch, the bite for
a ride), the release distance for a ride, and whether the next crossing
needs a different stance. A two-step lookahead marks a step that ends at a
launch, so the walk before it eases into the run-up instead of running
through it.

Locating a body means finding the cell that contains its origin. The test
uses the cell's box and its facets, with a small slack, and prefers a
supported cell within a step of its floor over an air cell the body happens
to be inside. A destination with no supported cell nearby takes the nearest
floor cell within 160 units.

## Building in the game

The module builds a RUNE when a map loads without one, when the file's
identity no longer matches the map or the server's movement settings, or
when the map's hint file has grown enough since the file was built (fifty
anchors and ten percent). The build runs on a second thread with copies of
what it needs; the server keeps its frame rate and the bots wait. Players
see a message when the build starts and one when the routes are ready.
Small maps build in seconds; the largest take a few minutes. `sv rune` starts
a build on demand.

The file records how many hint anchors it was built with in
`maps/<map>.rune.bites-count`.

## Hint files

`maps/<map>.bites` is a text file, one anchor per line:

```
fire_x fire_y fire_z bite_x bite_y bite_z
```

`fire` is where the rope was fired from and `bite` where it held. The builder
traces from the fire point toward the bite; the wall it meets within 48 units
of the bite becomes a candidate, with the surface's normal. Lines that do not
meet a wall there are ignored, so a file from an older version of a map does
no harm.

The module adds anchors from human play on the server: every rope that holds
is recorded once, deduplicated on a 16-unit grid, and the file is written
every thirty seconds and at the end of the map. `tools/demobites.py` adds
anchors from demo files and `tools/logbites.py` from server logs written with
`sg_debug 1`.

## Tools

The Linux build produces three inspection tools.

`fieldcheck MAP.rune ...`

| Arguments | Output |
|-----------|--------|
| `x y z x y z` | The route from the first point to the second: the cells, the crossings taken, the cost |
| `x y z c<cell> 0 0` | The route from a point to a cell |
| `d<cell>` | The cell's bounds, semantics, facets with their sources, and every departing crossing |
| `f x y z vx vy vz` | Trace a flight from a point with a velocity and report where it lands |
| `n x y z r` | Every cell whose box comes within r of the point |
| `stats` | Cell counts and shape statistics |

`bsppoint MAP.bsp x y z` prints the leaf and contents at a point and, when
the standing hull at that point starts inside a brush, the brush and its
sides.

`cellsdump.gnu MAP.bsp OUT.rune` builds a RUNE outside the game, reading
`MAP.bites` beside the map if present, and prints build statistics.

`tools/dm2trace.py DEMO.dm2 [--lines | --player NAME | --players]` reads a
demo and prints movement statistics, per-frame lines, or the players in it.

## Log lines

With `sg_debug 1` the server log carries one line per bot decision change
and a heartbeat every five seconds; `sg_debug 2` logs every frame.

| Line | Contents |
|------|----------|
| `SGBOT` | Bot name, role, cell, destination cell, step kind and crossing, position, target, cost to go, stance, speed, hook state, health, the movement command |
| `SGROPE` | Rope events: fired (with the bite), bit, released or missed, held releases with the live flight's outcome, let go with the speed |
| `SGTEAM` | Team goal changes and role reassignments |
| `SGPOST` | Defender posts chosen for a flag |
| `SGITEM` | Item detours |
| `SGHUMAN` | Human players' movement per packet: position, velocity, ground, hook, inputs, view, cell, and the rope's bite while it holds |
| `SGSOUND` | Hit and kill feedback sounds |

Role numbers in `SGBOT` lines: 0 attack, 1 defend, 2 carry, 3 recover,
4 escort, 5 powerup.
