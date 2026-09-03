# Slipgate era 4 -- initial release notes (2026-09-02)

What ships
- The game module (gamex86_64.so / gamex86_64.dll): the LMCTF game with the
  era-4 bots and the RUNE generator inside it.
- maps/<map>.bites: the players' rope bites per map (47 maps from the
  beatdown demos), read by the generator.  Optional; a map without one gets
  geometry-only rides.

First run on a server
- A map's routes (its RUNE, maps/<map>.rune) are built in the background
  the first time the map loads: "slipgate: building the bots' routes for
  <map> in the background; the bots wait for it", then "the bots' routes for
  <map> are ready".  Seconds for small maps, a few minutes for the largest
  (lmctf29).  Nothing to run by hand.  "sv rune" starts a build on demand.
- While humans play, their rope bites are added to maps/<map>.bites; when
  the file has grown enough, the map's routes are rebuilt on its next load.

Console variables
- sv_botfill N: fill each team to N (rune.cfg sets 5); "sv sg add red|blue",
  "sv sg list" for individual bots.
- capturelimit, timelimit (rune.cfg: 10 and 20), maplist_file for the
  rotation.
- ctf_hitsound, ctf_killsound: 0 off, 1 carrier events only, 2 all (rune.cfg
  sets 2 and 2).  The sound plays at the player it happened to and privately
  to the attacker.
- spawn_loadout / loadout_<name>: the starting equipment (rune.cfg: rocket
  launcher and grenades).
- sg_debug 1: the bots' decisions, the rope log and the human trace in the
  server log; 2 logs every frame.

Tools (optional)
- tools/demobites.py DEMOS... -o maps/ [--players a,b]: bites from demos.
- tools/logbites.py LOG... -o maps/: bites from server logs.
- fieldcheck, bsppoint, cellsdump.gnu: for looking at a map's routes.

Known limits
- The first load of a map runs the build on one core beside the server; the
  server keeps its frames, the bots stand until it lands.
- Team captures for the turtle rule and the capture limit are counted this
  map; the stats store is not used for them.
