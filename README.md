# LMCTF BuzzMod

A fork of [QwazyWabbit's LMCTF](https://github.com/QwazyWabbitWOS/lmctf60) for Quake II,
adding persistent player statistics backed by SQLite, an expanded scoreboard, and a set of
console commands for administering the stats database.

Everything QwazyWabbit's version does, this does too. What is new is that the numbers
survive the map change, the server restart, and the player reconnecting.

Maintained by Mike Davis (buzzkill).

## Which version is this based on?

Upstream at the fork point declares itself **`LMCTF TE 6.0`** in `g_local.h`.

That number has wandered, so it is worth being precise. The label went `LMCTF 6.0` →
`6.1` → `6.2` → `6.2-raven` → `LMCTF 6` → `LMCTF TE 6.0`, which is where QwazyWabbit
settled it. Some of the work in this fork was originally written in 2020 against the
`6.1`-era tree and has since been merged forward onto current upstream.

So: based on LMCTF TE 6.0, containing work first written against 6.1.

BuzzMod is not versioned in lockstep with upstream. Releases are numbered and dated —
Release 1, Release 2, and so on, with the month and year — and are listed in
[RELEASES.md](RELEASES.md).

## What BuzzMod adds

**Persistent statistics.** A SQLite database records each player's lifetime totals: frags,
deaths, captures, flag returns, flag-carrier kills, offense and defense kills, assists,
best kill streak, killing sprees, best capture streak, sweeps, suicides, and time played.
Loaded when a player joins, folded and written when they leave or the map changes.

**Two storage backends**, selected by the `ctf_statsdb` cvar:

| `ctf_statsdb` | Backend |
|-|-|
| `0` | off (default) — nothing is read or written |
| `1` | one database per player at `<gamedir>/players/<name>.ctf` |
| `2` | one shared `<gamedir>/players.db`, keyed on an internal player id |

Mode `2` is the one to use on a real server: it is the only one that can answer questions
across players, so leaderboards and the admin query commands need it.

**New statistics.** Beyond what LMCTF already tracked:

- *Sweeps* — you were on the winning team and the other side never captured your flag once
- *Capture streaks* — consecutive captures with no enemy capture in between
- *Kill streaks and sprees* — best run without dying, and how often you reached five
- *Suicides*, *offense kills* (killing a defender inside the enemy base), and *fragged*
  (killed by another player, tracked separately from LMCTF's self-inflicted death counter)

**Expanded scoreboard.** The team pickup totals the scoreboard had been computing all
along — quad, power shield, red armor, mega health, and each of the four runes — are now
actually drawn, per team, when the roster leaves room for them.

## Server CVARs

BuzzMod adds these. All of QwazyWabbit's cvars are unchanged and still apply.

|CVAR|Default|Notes|
|-|-|-|
|ctf_statsdb|0|Stats backend. 0=off, 1=per-player files, 2=shared database|
|ctf_switch_penalty|0|1 clears your score if you switch to the team that is both larger and winning. Joining the smaller, losing side is always free|

Inherited from upstream:

|CVAR|Default|Notes|
|-|-|-|
|dmflags|0||
|maxclients|4|Number of allowed players|
|ctfflags|0||
|refset|0||
|logrename|||
|runes|15|The number of runes to spawn|
|skinset|0|?|
|refpassword||Allows player to become referee|
|motd_file|motd.txt||
|server_file|server.cfg||
|maplist_file|maplist.txt||
|skin_file|skins.ini||
|skin_debug|0||
|disabled_weps|0||
|flag_init|0||
|fastswitch|0|0=normal, 1=super fast|
|mod_website|http://lmctf.com|The website to download paks|
|autolock|0|Automatically lock teams when match starts, unlock when ends. Pausing match will unlock, unpausing will re-lock. 0=no, 1=yes|
|countdown_time|15|Seconds to countdown when starting a match|

## Stats database commands

Server console, via `sv statsdb`:

|Command|Notes|
|-|-|
|`status`|Backend, path, row counts, file size|
|`flush`|Write every connected player to the database now|
|`top <column> [n]`|Leaderboard. Default 10, maximum 50|
|`player <name>`|One player's stored record|
|`export <file>`|Tab-separated dump, written into the game directory|
|`backup <file>`|Copy of the live database, safe to run while players are on|
|`rename <old> <new>`|Relabel a record, or fold one into another|
|`prune <days> confirm`|Drop players not seen in that long|
|`reset confirm`|Delete every stored stat|
|`help`|List the above|

`reset` and `prune` refuse to run without the word `confirm` and explain what they would
do first. `flush` works with either backend; the query commands need `ctf_statsdb 2`.

Sortable columns for `top`: `frags`, `fragged`, `shots`, `shots_hit`, `num_sprees`,
`max_streak`, `suicides`, `flag_pickups`, `flag_captures`, `flag_returns`, `flag_kills`,
`offense_kills`, `defense_kills`, `assists`, `max_cap_streak`, `sweeps`, `playtime_total`,
`playingtime`.

### A note on `rename`

Players are identified by name. Someone who changes their handle starts from zero, and
their old record is left orphaned. `rename` is the fix: if the new name is unused the
record is simply relabelled, and if it already exists the two are merged — counters add,
bests take the larger of the two, and the membership date keeps the earlier one.

## Client Commands

BuzzMod adds:

`lifetime [name]` - your persisted totals, or another player's. `stats` only ever shows
the current level

`rank [column] [n]` - the leaderboard, without needing an admin at the console

Inherited from upstream:

`players` - Show the players connected

`stats [name]` - this level's statistics for you or another player

`statsall` - this level's statistics for everyone

`squadboard` - list players by squads, Offense, Defense, Middle

`squad <category>` - Squads by category

`squadstatus <status>` - set your current status (free text format)

`referee <password>` - authenticate as a ref

`ctfhelp` - show help menu

`ctfmenu` - show the main menu

`users` - show who is connected to the server

`ctfkick <id>` - boot someone from the server

`fobserve` - Force observer mode on idle client.

`quadtime` - Change duration of quad time in seconds

`gotomap` - change map to named map. Must be ref, map must be in the maplist

`match <mapname>` - begin a match on the specified map

`team <red|blue>` - join the red or blue team

## Referee Commands

`refmenu` - show the ref menu

`refcommands` - shows all ref commands and usage

`lock` - locks/unlocks the teams  (toggle)

`unlock` - alias for `lock`

`startmatch` - starts the match on the current map

`stopmatch` - stops the match on the current map

`pausematch` - pauses/unpauses the current match

`unpausematch` - alias for `pausematch`

`setpassword <passwd>` - sets the server password. leave arg blank to unset

`togglefastswitch` - turn on/off the fast weapon switching mode

`changemap <mapname>` - change the map but do not start a match

## Installing

Take the library for your platform from the
[releases page](https://github.com/mgd34msu/lmctf60-buzzmod/releases) and drop it into your
`lmctf` game directory alongside the paks:

| Platform | File |
|-|-|
| Linux 64-bit | `gamex86_64.so` |
| Windows 64-bit | `gamex86_64.dll` |
| Windows 32-bit | `gamex86.dll` |

The filenames matter — the engine looks for them by name. Then turn the database on:

```
set ctf_statsdb 2
```

It creates itself on first start. The console reports which backend came up, or says
plainly that stats will not persist if the path is not writable.

## Building

**Linux / macOS**

```
make -f GNUmakefile
```

Produces `gamex86_64-lmctf-<rev>.so`. Rename it to `gamex86_64.so` to install — the
versioned name is for telling builds apart, not for the engine.

**Windows**

Open `gravity.sln` in Visual Studio 2022 and build Release for `x64` or `Win32`. The
pre-build step wants TortoiseGit's `GitWCRev` to produce `GitRevisionInfo.h`; if you do not
have it, `.github/workflows/build.yml` shows how to generate that header from plain `git`.

SQLite is vendored as the amalgamation (`sqlite3.c` / `sqlite3.h`) and builds as part of
the project. There is nothing to install.

Both platforms build with no errors and no warnings, and CI fails the build if a warning
appears.

## Credits

- **Loki's Minions CTF** — the original mod. LM_Hati, LM_Surt, LM_JORM and the rest of the
  LM team, whose statistics code is still the backbone of this one.
- **QwazyWabbit** — the modern port, the 64-bit and C11 cleanup, the VS2022 project and the
  GNUmakefile this fork builds on.
- **Mark Davies** — StdLog / GibStats logging.
- SQLite is in the public domain.
