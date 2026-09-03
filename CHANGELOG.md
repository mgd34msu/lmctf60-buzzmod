# Changelog

## v1.0.0 -- 2026-09-02

The first release with the SLIPGATE bots.  Everything below is new code.

Routes
- The map carved into a cell complex with the player's hull; crossings
  proved by the engine's own physics (walks, ramps, jumps, drops, swims,
  lifts, doors, rope rides); cost fields from the destination.
- Static brush models carved as walls; lava and slime as hazard cells.
- The carve leaves pieces a brush does not touch uncut, pairs faces on one
  wall plane across hull expansions, and ignores sides that graze a piece
  by under a unit (lmctf09: 77k cells to 24k, the red base reachable).
- Locate bounds a cell's inside test by its box.
- Flags that drop from their spawn point resolve to the flag's floor;
  destinations with no stand nearby take the nearest floor cell.
- Runes build in the background inside the shipped module on first load;
  "sv rune" on demand; a rune remembers the bites it was built with and
  rebuilds when the map's bites file has grown.

Rope
- Rides let go once the pull carries the body, with a clean live arc, on
  the floor outright, and trust the record where the live trace cannot fly.
- Candidates are the farthest bites in view, none inside the pull's slow
  band; 48 candidates and 16 records per cell; a momentum credit for fast
  forward releases; bites need only be level with the eye.
- The players' bites from demos (maps/<map>.bites) join the bite table in
  their own candidate pool; human bites recorded during play grow the file.
- Fired up to sixty degrees off the heading, not re-fired within 0.8 s, a
  hop when the rope bites on the floor, and running under the bolt stops at
  the floor's edge.
- A fall into harm fires up to three rescue ropes; a hang over harm holds
  on while the drop is unsafe and ends after nine seconds.

Movement
- Jumps pressed on the frame that reaches the portal, from a stand when a
  ledge stops the body, judged in the flat; a launch is present only with a
  velocity.
- Walks aim at the nearest point of their portal, not its foot.
- Arrival dead zone with easing; idle footwork around a post or a stand.
- Fight footwork: strafe across the enemy's line, reversals every 0.3 to
  0.9 s from the players' distributions, a hop on the reversal, floor
  checked as far as the hold and the hop carry.
- A body still for three seconds dislodges itself: back off, rope, reroute.
- A crossing the body is stuck on is avoided.

Team
- Team goals: take theirs together, bring it home, recover ours (the
  defenders hunt), hold and retake, turtle when more than two captures
  ahead (one runner, everyone else defends, no escorts).
- A role whose destination stays unreachable falls back on the goal; the
  attackers group up before the flag room; defenders support an engaged
  teammate in the base; posts, patrols for armor and ammo, powerup callouts.
- Team captures tallied at the capture for the turtle rule and the capture
  limit.

Items
- Ammo is worth taking only for weapons in hand and only while short; a held
  weapon is worth its ammo; one tech at a time; only quad, invulnerability
  and the techs are powerups.

Server
- capturelimit cvar; hit and kill sounds for every event with the private
  copy on the attacker's own entity; player sounds resolved on the server
  from the skin (no entity-0 warnings); the scoreboard layout budget lowered
  so a held scoreboard no longer overflows the client; a BSP reader that
  tolerates a text lump past the end of the file (lmctf02, lmctf05).

Tools
- fieldcheck (n, stats, d with sources), bsppoint (the start-solid brush),
  cellsdump, dm2trace (every player's track and every cable's ends),
  demobites and logbites, livestart with rotation, limits and durable logs.

Measured (five-minute games, nine or ten bots): lmctf09 32 ropes per
bot-minute, release 785 in the air 96%, no lava deaths; smap26 with the
players' bites 32 per bot-minute, 15 steals and 3 captures; bctf01 lava
deaths from 87 to 4.  Zest, the target: 30 ropes a minute, release 482,
9 steals and 4 captures per ten minutes.
