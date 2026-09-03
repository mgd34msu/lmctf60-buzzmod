# LMCTF BuzzMod

Quake II CTF mod. Fork of [QwazyWabbit's LMCTF](https://github.com/QwazyWabbitWOS/lmctf60)
with persistent player stats and the native SLIPGATE bot platform.

Tagged releases are the supported install boundary. Development branches are
not releases. Do not deploy an arbitrary checkout or a locally generated
server bundle.

## Install

From [releases](https://github.com/mgd34msu/lmctf60-buzzmod/releases), download
the release package and unpack it into the `lmctf` directory:

| | |
|-|-|
| `gamex86_64.so` | Linux 64-bit game module (the bots and the route builder are inside it) |
| `gamex86_64.dll` | Windows 64-bit game module |
| `maps/<map>.bites` | the players' rope bites per map, read when a map's routes are built (optional) |
| `slipgate-server-sample.cfg` | a sample server config: bots, limits, sounds, loadout |
| `lmctf6-buzzmod.pak` | **required** for stats and the hit sound (from an earlier release) |

Then turn stats on:

```
set ctf_statsdb 2
```

The database creates itself. `1` gives one file per player instead; `0` is off.

Stats are viewable on the web via [q2lmstats](https://github.com/mgd34msu/q2lmstats).

## SLIPGATE bots (v1.0.0)

SLIPGATE is the mod's from-scratch bot system, built into the game module.
It plays LMCTF the way the best players do: the rope on nearly every move,
released at speed in the air; strafing and reversing in fights; a team goal
that every bot follows and falls back on.  The bots are measured against the
top players' demos (Zest, Lequin, seed, Em, sinsemilla): the rope rate,
release speed, air time, turn rate and objective rates in
`docs/ERA4-PLAYERS-STANDARD.txt` are the targets, and the numbers the bots
reach are in `docs/ERA4-RUNE.md`.

**Nothing to run by hand.**  The first time a map loads, its routes (the
RUNE, `maps/<map>.rune`) are built in the background while the server plays
on and the bots wait: "slipgate: building the bots' routes for <map> in the
background; the bots wait for it", then "the bots' routes for <map> are
ready".  Seconds for small maps, a few minutes for the largest.  While humans
play, their rope bites are added to `maps/<map>.bites`, and a map whose file
has grown enough is rebuilt on its next load.

How it works, in short: the map is carved into a complex of cells with the
player's hull (every place the body can be), every crossing between cells is
proved by the game's own physics (walks, jumps, drops, swims, rope rides,
lifts, doors), and a bot follows a cost field over those crossings toward its
goal.  The team's goal (take their flag together, bring it home, recover
ours, hold and retake, turtle when well ahead) hands out roles; a role whose
destination dies falls back on the goal.  See `SLIPGATE.md`.

### Server commands and variables

| Command | Effect |
|-|-|
| `sv sg add [red\|blue]` | add a bot (the balancer picks the team without one) |
| `sv sg remove <name>` / `sv sg remove` | remove one bot / every bot |
| `sv sg kick worst` | remove the lowest-ranked bot |
| `sv sg list` | the bot roster |
| `sv rune` | build this map's routes now, in the background |

| Variable | Effect |
|-|-|
| `sv_botfill N` | fill each team to N bots (sample config: 5) |
| `capturelimit`, `timelimit` | end the map (sample: 10 and 20) and rotate through `maplist.txt` |
| `ctf_hitsound`, `ctf_killsound` | 0 off, 1 flag-carrier events only, 2 every event (sample: 2, 2); the sound plays where it happened and privately to the attacker |
| `spawn_loadout "@name"`, `loadout_name "..."` | starting equipment (sample: rocket launcher and grenades) |
| `sg_debug` | 1 logs the bots' decisions, the rope and the human trace to the server log; 2 every frame |

### Tools (optional)

- `tools/demobites.py DEMOS... -o maps/` and `tools/logbites.py LOGS... -o maps/`
  add rope bites from demos or from server logs to the maps' bite files.
- `tools/dm2trace.py`, `fieldcheck`, `bsppoint`, `cellsdump.gnu` for looking at
  demos and at a map's routes (`docs/ERA4-RUNE.md`).

### Documents

- `docs/RELEASE-ERA4.md` -- the release notes.
- `SLIPGATE.md` -- the design.
- `docs/ERA4-RUNE.md` -- the RUNE in detail and the day-by-day record of what
  the play data changed.
- `docs/ERA4-PLAYERS-STANDARD.txt` -- the players the bots are measured against.
- `docs/README.md` -- which documents are current and which are history.

## What's tracked

Frags, deaths, suicides, captures, flag pickups, returns, carrier kills, offense
and defense kills, assists, kill streaks, killing sprees, capture streaks,
sweeps, shots, hits, damage, and time played — kept per player across maps and
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

A match summary — final score, top capper, top defender, top killer, accuracy
leader — prints to everyone at the end of each game.

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

`rename` matters because players are identified by name — change your handle and
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

Loki's Minions CTF — LM_Hati, LM_Surt, LM_JORM and the LM team.
QwazyWabbit — the modern port this builds on.
Mark Davies — StdLog logging. SQLite is public domain.
