# LMCTF BuzzMod

Quake II CTF mod. Fork of [QwazyWabbit's LMCTF](https://github.com/QwazyWabbitWOS/lmctf60)
with persistent player stats and the native SLIPGATE bot platform.

Tagged releases are the supported install boundary. Development branches are
not releases. Do not deploy an arbitrary checkout or a locally generated
server bundle.

## Install

Download the release package from
[releases](https://github.com/mgd34msu/lmctf60-buzzmod/releases) and unpack it
into the `lmctf` directory. It holds:

| File | What it is |
|-|-|
| `gamex86_64.so` | the Linux 64-bit game module |
| `gamex86_64.dll` | the Windows 64-bit game module |
| `gamex86.dll` | the Windows 32-bit game module |
| `lmctf6-buzzmod.pak` | required. Scoreboard artwork and the hit sound |
| `maps/*.bites` | the players' rope bites per map, read when a map's routes are built |
| `server-sample.cfg` | a sample server config for bots, limits, sounds and loadout |

Then turn stats on:

```
set ctf_statsdb 2
```

The database creates itself. `1` keeps one file per player instead; `0` is off.
Stats are viewable on the web with [q2lmstats](https://github.com/mgd34msu/q2lmstats).

## SLIPGATE bots

SLIPGATE is the mod's own bot system, built into the game module. The bots
play the way the best LMCTF players play. They rope on nearly every move and
let go at speed, in the air. They strafe and reverse in fights. Each team
follows a goal, and a bot whose own job dies falls back on it.

They are measured against the top players' demos. `docs/PLAYERS-STANDARD.txt`
holds the numbers from Zest, Lequin, seed, Em and sinsemilla, and
`docs/RUNE.md` records what the bots reach after each change. In five-minute
games on smap26 the bots fire 32 ropes per bot-minute and release at 785
units per second; Zest fires 30 a minute.

There is nothing to run by hand. The first time a map loads, the module builds
the map's routes (`maps/<map>.rune`) on a second thread. The server keeps
playing, the bots stand until the routes land, and everyone sees two messages:
"slipgate: building the bots' routes for <map> in the background; the bots
wait for it", then "the bots' routes for <map> are ready". Small maps take
seconds, lmctf29 takes about three minutes. While humans play, their rope
bites go into `maps/<map>.bites`, and a map whose file has grown enough gets
its routes rebuilt on its next load.

The routes come from the map itself. The module carves each map into cells
with the player's own hull, so a cell is a place the body can be. It proves
every crossing between cells with the game's movement physics: walks, jumps,
drops, swims, rope rides, lifts, doors. A bot follows a cost field over those
crossings toward its goal. `SLIPGATE.md` has the design.

### Server commands

| Command | Effect |
|-|-|
| `sv sg add` | add a bot; the balancer picks its team |
| `sv sg add red`, `sv sg add blue` | add a bot to that team |
| `sv sg remove <name>` | remove one bot |
| `sv sg remove` | remove every bot |
| `sv sg kick worst` | remove the lowest-ranked bot |
| `sv sg list` | print the bot roster |
| `sv rune` | build this map's routes now, in the background |

### Server variables

| Variable | Effect |
|-|-|
| `sv_botfill N` | fill each team to N bots. The sample config sets 5 |
| `capturelimit`, `timelimit` | end the map and move to the next in `maplist.txt`. The sample sets 10 captures and 20 minutes |
| `ctf_hitsound`, `ctf_killsound` | 0 off, 1 flag-carrier events only, 2 every event. The sound plays where it happened and privately to the attacker. The sample sets 2 and 2 |
| `spawn_loadout "@name"` with `loadout_name "..."` | starting equipment. The sample gives a rocket launcher and grenades |
| `sg_debug` | 1 logs every bot decision, every rope and the human trace to the server log. 2 logs every frame |

### Tools

These are optional. `tools/demobites.py DEMOS... -o maps/` adds rope bites
from demo files to the maps' bite files, and `tools/logbites.py LOGS... -o maps/`
does the same from server logs. `fieldcheck`, `bsppoint` and `cellsdump.gnu`
inspect a map's routes; `docs/RUNE.md` explains them.

### Documents

`docs/RELEASE-v1.0.0.md` is the release note. `SLIPGATE.md` is the design.
`docs/RUNE.md` describes the routes in detail. `docs/PLAYERS-STANDARD.txt`
lists the players the bots are measured against. `docs/README.md` says which
documents are current and which are development records.

## What's tracked

Frags, deaths, suicides, captures, flag pickups, returns, carrier kills, offense
and defense kills, assists, kill streaks, killing sprees, capture streaks,
sweeps, shots, hits, damage, and time played, kept per player across maps and
restarts.

**Sweeps** are new: you were on the winning team and the enemy never captured
your flag once.

## Commands

Players:

| | |
|-|-|
| `stats [name]` | this level |
| `lifetime [name]` | career totals |
| `rank [stat] [n]` | leaderboard |
| `statsall` | everyone, this level |
| `season` | top 10 of the last 30 days (shared database only) |
| `records` | all-time server records (shared database only) |
| `activity` | busiest players of the last 7 days (shared database only) |
| `momentum` | biggest recent movers in capture rate (shared database only) |
| `card [name]` | one player's career card (shared database only) |
| `vs <name>` | you against them, only counting games you both played (shared database only) |

A match summary prints to everyone at the end of each game: final score, top
capper, top defender, top killer, accuracy leader.

Admin, `sv statsdb`:

| | |
|-|-|
| `status` | backend, path, row counts |
| `flush` | write connected players now |
| `top <stat> [n]` | leaderboard |
| `player <name>` | one player's record |
| `export <file>` | tab-separated dump |
| `backup <file>` | safe copy while players are on |
| `rename <old> <new>` | relabel or merge a record |
| `prune <days> confirm` | drop players not seen in that long |
| `reset confirm` | wipe everything |

`rename` matters because players are identified by name. Change your handle and
you'd otherwise start from zero.

Referee commands are unchanged from upstream: `refmenu`, `refcommands`, `lock`,
`startmatch`, `stopmatch`, `pausematch`, `setpassword`, `changemap`,
`togglefastswitch`.

## CVARs

New:

| | | |
|-|-|-|
| `ctf_statsdb` | 0 | 0 off, 1 per-player files, 2 shared database |
| `ctf_switch_penalty` | 0 | 1 clears your score for joining the bigger winning team |
| `ctf_hitsound` | 1 | hit confirmation: 0 off, 1 flag-carrier hits only, 2 all hits |
| `ctf_killsound` | 2 | frag bell: 0 off, 1 flag-carrier frags only, 2 all frags |

Both sounds ring from the player the event happened to, and the attacker
always hears a private confirmation copy no matter the distance.

Upstream cvars are unchanged: `dmflags` `maxclients` `ctfflags` `refset`

### spawn_loadout (BuzzMod)

Admin-defined starting equipment. One cvar, grammar `thing[:count]`,
space or comma separated:

    set spawn_loadout "rocketlauncher:5 railgun:5 body:100 health:110"

Tokens match any unambiguous fragment of a live item classname, so new
items are addressable the day they exist -- `sv listitems` prints every
token. Counts are ADDITIVE under the game's own caps. Semantics: a
weapon always carries its real pickup ammo bundle and `:count` adds
extra; ammo -> amount; armor -> points (that armor's own max applies);
power armor -> the device plus count cells; other items -> charges.
`health` is the one reserved word -- above max rots 1/sec down to max,
the megahealth mechanic. Ingame runes are excluded (own lifecycle).

Named builds are plain cvars, `@`-referenced, nestable to depth 4,
composable with extra tokens; none ship by default:

    set loadout_testing "rocketlauncher:5 railgun:5 grenades:5"
    set spawn_loadout "@testing"

Bad tokens and ambiguous fragments warn on the console by name.
`logrename` `runes` `skinset` `refpassword` `motd_file` `server_file`
`maplist_file` `skin_file` `skin_debug` `disabled_weps` `flag_init` `fastswitch`
`mod_website` `autolock` `countdown_time`.

## Build

```
make -f GNUmakefile          # Linux, produces gamex86_64-lmctf-<rev>.so
```

Rename the output to `gamex86_64.so` to install it.

Windows: open `gravity.sln` in Visual Studio 2022, build Release for x64 or
Win32. SQLite is vendored, nothing to install.

## Credits

Loki's Minions CTF: LM_Hati, LM_Surt, LM_JORM and the LM team.
QwazyWabbit, the modern port this builds on.
Mark Davies, StdLog logging. SQLite is public domain.
