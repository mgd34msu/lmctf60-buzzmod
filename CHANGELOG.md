# Changelog

## v1.0.0, 2026-09-02

The first release with the SLIPGATE bots. All of the bot code is new.

### Routes

The module carves each map into cells with the player's hull and proves every
crossing with the game's physics: walks, ramps, jumps, drops, swims, lifts,
doors and rope rides. Cost fields run from the destination. Static brush
models count as walls; lava and slime are hazard cells. The carve leaves
untouched pieces uncut, pairs faces on one wall plane across hull expansions
and ignores sides that graze a piece by under a unit. On lmctf09 that took
the cell count from 77,000 to 24,000 and made the red base reachable. Locate
now bounds a cell's inside test by its box. A flag that drops from its spawn
point resolves to the floor it lands on. Routes build in the background inside
the shipped module on a map's first load; sv rune builds on demand; a map
whose bites file has grown gets rebuilt.

### Rope

A ride lets go once the pull carries the body and the live arc is clean, or
at once on the floor. Candidates are the farthest bites in view outside the
pull's slow band, 48 candidates and 16 records per cell, with a credit for
fast forward releases. The players' bites from demos join the table in their
own candidate pool, and bites recorded during play grow the file. A rope
fires up to sixty degrees off the heading, never within 0.8 seconds of the
last, gets a hop when it bites on the floor, and the run under the bolt stops
at the floor's edge. A fall into lava fires up to three rescue ropes; a hang
over lava holds on while the drop is unsafe and ends after nine seconds.

### Movement

Jumps press on the frame that reaches the portal, or from a stand against a
ledge, judged in the flat. Walks aim at the nearest point of their portal.
Arrival has a dead zone with easing, and a waiting bot keeps moving. In a
fight the body strafes across the enemy's line and reverses every 0.3 to 0.9
seconds with a hop, checking the floor as far as the move carries. A body
still for three seconds backs off, ropes to a nearby bite, then reroutes. A
crossing the body cannot make is avoided.

### Team

Team goals: take theirs together, bring it home, recover ours, hold and
retake, turtle at more than two captures ahead. A dead role falls back on the
goal. Attackers group before the flag room. Defenders back a fighting
teammate, patrol for armor and ammo, and call out powerups. Team captures are
tallied at the capture, for the turtle rule and the capture limit.

### Items

Ammo is worth taking only for a weapon in hand and only while short. A held
weapon is worth its ammo. One tech at a time. Only the quad, invulnerability
and the techs count as powerups.

### Server

New capturelimit cvar. Hit and kill sounds for every event, with the private
copy on the attacker's own entity. Player sounds resolved on the server from
the skin, which ends the entity-0 warnings on clients. The scoreboard layout
budget is 1000 bytes, so a held scoreboard no longer overflows the client.
The BSP reader accepts a text lump that runs past the end of the file
(lmctf02, lmctf05).

### Tools

fieldcheck, bsppoint, cellsdump, dm2trace, demobites, logbites, and
livestart with map rotation, limits and durable logs.

### Numbers

Five-minute games with nine or ten bots. lmctf09: 32 ropes per bot-minute,
release at 785, 96% in the air, no lava deaths. smap26 with the players'
bites: 32 ropes per bot-minute, 15 steals, 3 captures. bctf01: lava deaths
down from 87 to 4. Zest, the target, fires 30 ropes a minute, releases at
482, and makes 9 steals and 4 captures per ten minutes.
