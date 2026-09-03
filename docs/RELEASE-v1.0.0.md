# LMCTF BuzzMod v1.0.0

Released 2026-09-02.

## What ships

The game module for Linux 64-bit (gamex86_64.so), Windows 64-bit
(gamex86_64.dll) and Windows 32-bit (gamex86.dll), with the SLIPGATE bots
and their route builder inside. lmctf6-buzzmod.pak, required for the
scoreboard artwork and the hit sound. maps/*.bites, hook anchor hints for 47
maps. server-sample.cfg. SHA256SUMS.

## First run

Put the files in the lmctf directory and start the server as usual. The
first time a map loads, the module builds its routes on a second thread.
Players see "slipgate: building the bots' routes for <map> in the
background; the bots wait for it" and then "the bots' routes for <map> are
ready". Small maps take seconds; lmctf29 takes about three minutes. sv rune
starts a build on demand. While humans play, their hook anchors are added to
maps/<map>.bites, and a map whose file has grown enough gets its routes
rebuilt on its next load.

## Server variables

sv_botfill N fills each team to N bots. sv sg add red, sv sg add blue and
sv sg list handle single bots. capturelimit and timelimit end a map and
maplist_file names the rotation; the sample config sets 10 captures and 20
minutes. ctf_hitsound and ctf_killsound take 0, 1 for flag-carrier events
only, or 2 for every event; the sound plays where it happened and privately
to the attacker. spawn_loadout and loadout_<name> set the starting
equipment; the sample gives a rocket launcher and grenades. sg_debug 1 logs
the bots' decisions, the rope and the human trace to the server log, and 2
logs every frame.

## Tools

tools/demobites.py adds hook anchors from demo files. tools/logbites.py adds them from server logs. fieldcheck,
bsppoint and cellsdump.gnu inspect a map's routes.

## Limits

The first load of a map runs the build on one core beside the server; the
server keeps its frames and the bots stand until the routes land. Team
captures for the turtle rule and the capture limit are counted for the
current map, not from the stats database.
