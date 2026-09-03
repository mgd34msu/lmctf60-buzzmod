# LMCTF BuzzMod

A Quake II Capture the Flag mod, forked from
[QwazyWabbit's LMCTF](https://github.com/QwazyWabbitWOS/lmctf60), with two
additions: persistent player statistics and SLIPGATE, a native bot system
built into the game module.

Tagged releases are the supported installs. Development branches are not
releases.

## Contents

- [Requirements](#requirements)
- [Installation](#installation)
- [Server configuration](#server-configuration)
- [SLIPGATE bots](#slipgate-bots)
- [Player statistics](#player-statistics)
- [Building from source](#building-from-source)
- [Documentation](#documentation)
- [Credits](#credits)

## Requirements

- A Quake II server or client (Yamagi Quake II is the tested engine).
- The LMCTF map files for the maps you run.
- Linux x86_64, Windows x64 or Windows x86.

## Installation

Download the release package from the
[releases page](https://github.com/mgd34msu/lmctf60-buzzmod/releases) and
unpack it into the `lmctf` directory of the Quake II installation.

| File | Purpose |
|------|---------|
| `gamex86_64.so` | Game module, Linux x86_64 |
| `gamex86_64.dll` | Game module, Windows x64 |
| `gamex86.dll` | Game module, Windows x86 |
| `lmctf6-buzzmod.pak` | Required. Scoreboard artwork and sounds |
| `maps/*.bites` | Route hints per map, used when a map's bot routes are built |
| `server-sample.cfg` | A sample server configuration |
| `SHA256SUMS` | Checksums of the module files |

Clients need `lmctf6-buzzmod.pak` in their `lmctf` directory as well.

## Server configuration

`server-sample.cfg` shows a complete configuration. The variables specific to
this mod:

| Variable | Default | Effect |
|----------|---------|--------|
| `ctf_statsdb` | 0 | 0 off, 1 one file per player, 2 shared database |
| `ctf_switch_penalty` | 0 | 1 clears the score of a player who joins the larger winning team |
| `ctf_hitsound` | 1 | Hit confirmation: 0 off, 1 flag-carrier hits only, 2 all hits |
| `ctf_killsound` | 2 | Frag confirmation: 0 off, 1 flag-carrier frags only, 2 all frags |
| `capturelimit` | 0 | Captures that end the map; 0 for none |
| `timelimit` | 0 | Minutes that end the map; 0 for none |
| `maplist_file` | `maplist.txt` | The map rotation |
| `sv_botfill` | 0 | Fill each team to this many SLIPGATE bots |
| `sg_debug` | 0 | 1 logs bot decisions to the server log; 2 logs every frame |
| `spawn_loadout` | | Starting equipment, see below |

Hit and kill sounds play at the player the event happened to; the attacker
also hears a private copy. Upstream variables such as `dmflags`, `ctfflags`
and `maxclients` are unchanged.

### Starting equipment

Admin-defined starting equipment. One cvar, grammar `thing[:count]`,
space or comma separated:

    set spawn_loadout "rocketlauncher:5 railgun:5 body:100 health:110"

Tokens match any unambiguous fragment of a live item classname, so new
items are addressable the day they exist. `sv listitems` prints every
token. Counts are ADDITIVE under the game's own caps. Semantics: a
weapon always carries its real pickup ammo bundle and `:count` adds
extra; ammo -> amount; armor -> points (that armor's own max applies);
power armor -> the device plus count cells; other items -> charges.
`health` is the one reserved word. above max rots 1/sec down to max,
the megahealth mechanic. Ingame runes are excluded (own lifecycle).

Named builds are plain cvars, `@`-referenced, nestable to depth 4,
composable with extra tokens; none ship by default:

    set loadout_testing "rocketlauncher:5 railgun:5 grenades:5"
    set spawn_loadout "@testing"

Bad tokens and ambiguous fragments warn on the console by name.

## SLIPGATE bots

SLIPGATE bots are part of the game module. They fill teams, use the
grappling hook as their main means of movement, and decide at three levels:
a team goal that changes only when the flags change hands, a strategy per
bot that holds until an event invalidates it, and tactics that change every
frame.

### Behaviour

- Movement: the bots rope on most moves and release at speed in the air,
  bunny hop, strafe-jump, and use jumps, drops, lifts and doors. In a fight
  they strafe across the enemy's line and reverse direction; waiting at a
  post they keep moving.
- Team goals: with both flags home the team attacks together; with its own
  carrier alive it escorts; with its flag taken the defenders join the
  recovery; with a lead of more than two captures it turtles, leaving one
  runner. A bot whose task becomes impossible falls back on the team goal.
- Roles: attack, defend, carry, recover, escort and powerup. Defenders hold
  posts that cover the approaches to the flag, collect armor and ammo near
  it, and support a teammate under attack.
- Combat: line-of-sight perception with memory, weapon choice by range and
  ammo, aim error scaled by skill and persona, callouts for what a bot sees.

### Routes

The module builds each map's bot routes itself. The first time a map loads,
it carves the map into cells, proves every crossing between cells with the
game's movement physics, and writes the result to `maps/<map>.rune`. This runs
on a second thread; the server keeps playing and the bots wait until the
routes are ready. Players see a message when the build starts and when it
finishes. `sv rune` starts a build on demand.

The routes can be improved with demos. `maps/<map>.bites` lists grappling
hook anchor points for a map; the route builder verifies each one against
the map and adds the rides it allows. The release ships hint files for 47
maps. The module also records hook anchors from play on the server, and
rebuilds a map's routes on its next load when the file has grown.
`tools/demobites.py` adds anchors from demo files and `tools/logbites.py`
from server logs.

### Bot commands

| Command | Effect |
|---------|--------|
| `sv sg add` | Add a bot; the team balancer picks its team |
| `sv sg add red`, `sv sg add blue` | Add a bot to a team |
| `sv sg remove <name>` | Remove one bot |
| `sv sg remove` | Remove every bot |
| `sv sg kick worst` | Remove the lowest-ranked bot |
| `sv sg list` | Print the bot roster |
| `sv rune` | Build the current map's routes now |

`SLIPGATE.md` describes the design.

## Player statistics

With `ctf_statsdb` on, the mod keeps per-player records across maps and
restarts: frags, deaths, suicides, captures, flag pickups, returns, carrier
kills, offense and defense kills, assists, kill streaks, killing sprees,
capture streaks, sweeps, shots, hits, damage and time played. A sweep is a
win in which the enemy never captured your flag.

The records are viewable on the web with
[q2lmstats](https://github.com/mgd34msu/q2lmstats).

### Player commands

| Command | Shows |
|---------|-------|
| `stats [name]` | This map |
| `lifetime [name]` | Career totals |
| `rank [stat] [n]` | Leaderboard |
| `statsall` | Everyone, this map |
| `season` | Top 10 of the last 30 days (shared database) |
| `records` | All-time server records (shared database) |
| `activity` | Busiest players of the last 7 days (shared database) |
| `momentum` | Biggest recent movers in capture rate (shared database) |
| `card [name]` | One player's career card (shared database) |
| `vs <name>` | Head-to-head, games both played (shared database) |

A match summary prints at the end of each game: final score, top capper,
top defender, top killer and accuracy leader.

### Administration

`sv statsdb` subcommands:

| Subcommand | Effect |
|------------|--------|
| `status` | Backend, path and row counts |
| `flush` | Write connected players now |
| `top <stat> [n]` | Leaderboard |
| `player <name>` | One player's record |
| `export <file>` | Tab-separated dump |
| `backup <file>` | Safe copy while players are on |
| `rename <old> <new>` | Relabel or merge a record |
| `prune <days> confirm` | Drop players not seen in that long |
| `reset confirm` | Wipe everything |

Players are identified by name, so `rename` carries a record across a name
change. Referee commands are unchanged from upstream: `refmenu`,
`refcommands`, `lock`, `startmatch`, `stopmatch`, `pausematch`,
`setpassword`, `changemap`, `togglefastswitch`.

## Building from source

Linux:

```
make -f GNUmakefile all      # produces gamex86_64-lmctf-<rev>.so
make -f GNUmakefile check    # runs the unit tests
```

Rename the output to `gamex86_64.so` to install it.

Windows: open `gravity.sln` in Visual Studio 2022 and build Release for x64
or Win32. SQLite is vendored; nothing else is required.

The GitHub workflow builds Linux with gcc and clang, Windows x64 and x86,
and publishes a release for every `v*` tag.

## Documentation

| Document | Contents |
|----------|----------|
| `SLIPGATE.md` | The bot system's design |
| `CHANGELOG.md` | Changes by version |
| `ROADMAP.md` | Planned work |
| `docs/RELEASE-v1.0.0.md` | Release notes |
| `docs/RUNE.md` | The route data: cells, crossings, rope rides, fields, building, hint files, tools, log lines |
| `docs/LAYOUT.md` | The scoreboard layout format |

## Credits

- Loki's Minions CTF: LM_Hati, LM_Surt, LM_JORM and the LM team.
- QwazyWabbit, for the modern port this builds on.
- Mark Davies, StdLog logging.
- SQLite, public domain.
