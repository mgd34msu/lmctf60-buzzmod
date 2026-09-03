# SLIPGATE

SLIPGATE is LMCTF's native bot system, built into the game module.  Every
part of it is written from the game's own physics and the players'
own play, with nothing carried over from the bot systems that came before.

## The map: the RUNE

- **Cells.**  The map's brushes are subtracted from each BSP leaf with the
  player's crouch hull, so a cell is a region of positions the body's centre
  can occupy.  Floors are cut last so a floor cell sits exactly on its
  floor; lava and slime become hazard cells; brush models that stand still
  (func_wall and the like) are carved as walls.  A brush that touches none
  of a piece leaves it uncut, and faces on one wall plane pair as portals
  whatever hull expanded them .
- **Crossings.**  Every way from one cell to the next is proved by the
  engine's movement laws (sg_rune_law.h, sg_engine_facts.h): walks and
  crouches, ramps, jumps with their run-up and launch, drops that land
  robustly under small errors, swims, lifts, doors, and rope rides -- a
  bolt that clears, a pull that clears, a release point whose flight lands
  on a floor.  Rides are built from the surfaces' centres and from the
  players' own bites (maps/<map>.bites), keeping the farthest bites in view.
- **Fields.**  A destination becomes a cost field over the crossings (a
  Dijkstra from the destination); a bot reads the step from its cell, with a
  two-step lookahead that eases it into launches.
- **Built in the game.**  A missing or stale RUNE is built on a thread of
  its own the moment its map loads ; the bots wait
  and the server plays on.  Human bites recorded during play 
  grow the map's bites file, and enough growth rebuilds the RUNE for its
  next load.

## The body

The tactic controller  turns a step into one frame
of input: walks eased to a point, jumps lined up on the record's run-up and
pressed on the frame that reaches the portal (or from a stand when a ledge
stops the body), rope rides fired on the move up to sixty degrees off the
heading, hopped when they bite on the floor, and let go once the pull
carries the body and the live arc lands.  The driver  adds
what the players do around the route: strafing across an enemy's line with
reversals every 0.3 to 0.9 s and a hop on the reversal, idle footwork around
a post or a flag stand, a walk aimed at the nearest point of its portal, a
rescue rope on a fall into harm, and a staged dislodge for a body still for
three seconds (back off, rope to a bite nearby, reroute).

## The team

Each team holds a goal : take their flag together, bring it
home (escort the carrier), recover ours (the defenders hunt), hold ours
alive and get theirs back, or turtle when more than two captures ahead
(everyone defends, one runner, no escorts).  The goal hands out roles --
attack, defend, carry, recover, escort, powerup -- at events (a flag moves,
a carrier changes, the roster changes), and a role whose destination stays
unreachable falls back on the goal's role.  Attackers group up before the
flag room; defenders hold posts that cover the approaches, stock up on what
stands near the flag, and back an engaged teammate.  Powerups seen standing
are called out and fetched.

## The fight

Perception is live sight and memory of where each enemy was last seen;
weapon choice reads range, ammo and splash safety; aim leads projectiles
with an error scaled by skill and persona; fire is one trace.  Callouts
name what the bot sees and does.

## The measure

Every player in 155 tournament demos is measured by track
(tools/dm2trace.py, tools/demobites.py); the standard is in
docs/PLAYERS-STANDARD.txt and the bots' numbers per change in
docs/RUNE.md.  With sg_debug 1 the server log carries every bot's
decision (SGBOT), the rope (SGROPE), the team (SGTEAM), and the human trace
(SGHUMAN) for the same comparison on a live game.

## Source layout

Everything is under `slipgate/`: sg_rune_* (BSP reader, trace, entities,
law, carve, cell complex, movement and rope builders, artifact, locate,
field, level owner, in-game generator), sg_bot_* (frame driver, roster,
combat, items, callout, host bridge, persona, cvars), sg_tactic_controller,
sg_bites.  docs/UNITS.md names each unit and its origin.
