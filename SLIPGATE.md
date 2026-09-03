# SLIPGATE

SLIPGATE is LMCTF's own bot system. It lives in the game module. It was
written from the game's physics and from measured play, with nothing kept
from earlier bot systems.

## The map

docs/RUNE.md describes the route data in depth; this section is the summary.

The module carves each map into cells. It subtracts the map's brushes from
every BSP leaf with the player's crouch hull, so a cell is a region the
body's centre can occupy. Floors are cut last, so a floor cell sits on its
floor. Lava and slime become hazard cells. Brush models that never move,
such as func_wall, count as walls. A brush that touches none of a piece
leaves it uncut, and faces on the same wall plane pair as portals whichever
hull expanded them.

Every crossing between two cells is proved with the engine's movement laws.
Walks and crouches, ramps, jumps with a run-up and a launch, drops that still
land under small errors, swims, lifts, doors, and rope rides all get a
record. A ride record needs a bolt that reaches the wall, a pull that clears,
and a release point whose flight lands on a floor. Ride candidates come from
the surfaces' centres and from the anchor hints in maps/<map>.bites, keeping
the farthest in view.

A destination becomes a cost field over the crossings. A bot reads its next
step from its own cell, with a two-step lookahead that slows it into a
launch.

The module builds a missing or stale set of routes on its own thread when
the map loads. The bots stand until it lands and the server keeps its frame
rate. Hook anchors recorded during play grow the map's hint file, and enough
growth rebuilds the routes for the next load.

## The body

The tactic controller turns a step into one frame of input. A walk eases to
its point. A jump lines up on the record's run-up and presses on the frame
that reaches the portal, or from a stand when a ledge has stopped the body.
A rope fires on the move up to sixty degrees off the heading, gets a hop
when it bites with the body on the floor, and lets go once the pull carries
the body and the live arc lands.

The driver adds the footwork around the route. In a fight the body
strafes across the enemy's line, reverses every 0.3 to 0.9 seconds and hops
on the reversal. Waiting at a post or a flag stand it keeps moving in a
small circle. A walk aims at the nearest point of its portal. A fall into
lava fires a rescue rope. A body still for three seconds backs off with a
hop, then ropes to a nearby bite, then reroutes.

## The team

Each team has a goal. Both flags home means take theirs together. Our
carrier alive means bring it home and escort the carrier. Their carrier
alive means recover ours; the defenders join the hunt. Both flags out means
hold ours alive and get theirs back. More than two captures ahead means
turtle: everyone defends, one bot keeps running for their flag, nobody
escorts.

The goal hands out roles when something changes: a flag moves, a carrier
changes, the roster changes. Roles are attack, defend, carry, recover,
escort and powerup. A bot whose destination stays unreachable takes the role
the goal would give it. Attackers wait for each other before the flag room.
Defenders hold posts that cover the approaches, pick up armor and ammo near
the flag, and go to a teammate who is fighting in the base. A powerup seen
standing gets a callout and a runner.

## The fight

A bot sees what its eyes see and remembers where each enemy was last.
Weapon choice reads range, ammo and splash safety. Aim leads projectiles,
with an error scaled by the skill setting and the bot's persona. Every shot
is one trace. Callouts say what the bot sees and does.

## How it is measured

The bots' movement is measured against recorded play: speed, air time, rope
rate and release speed, turn rate, and objective rates.
tools/dm2trace.py reads demo files for the same measures.
With sg_debug 1 the server log carries the same data for a live game: SGBOT
lines for decisions, SGROPE for the rope, SGTEAM for the team, SGHUMAN for
the human players.

## Source layout

Everything is under slipgate/. The sg_rune_ files read the BSP, trace,
carve, build the cell complex and the movement and rope records, write and
load the artifact, locate a body and run the fields. The sg_bot_ files drive
the bots: frame, roster, combat, items, callouts, host bridge, personas,
cvars. sg_tactic_controller.c is the body and sg_bites.c collects hook anchors.
