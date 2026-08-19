# LMCTF BuzzMod

Quake II CTF mod. Fork of [QwazyWabbit's LMCTF](https://github.com/QwazyWabbitWOS/lmctf60)
with persistent player stats and the native SLIPGATE bot platform.

Tagged releases are the supported install boundary. The development branches
remain active while the full-project completion gates in
[`PROJECT-COMPLETION-PLAN.md`](PROJECT-COMPLETION-PLAN.md) are being closed;
do not treat an arbitrary checkout or locally generated server bundle as a
release.

## Install

From [releases](https://github.com/mgd34msu/lmctf60-buzzmod/releases), download
`lmctf6-buzzmod.pak` plus the game module for the server's platform and place
both in the `lmctf` directory:

| | |
|-|-|
| `gamex86_64.so` | Linux 64-bit |
| `gamex86_64.dll` | Windows 64-bit |
| `gamex86.dll` | Windows 32-bit |
| `lmctf6-buzzmod.pak` | **required** — statboard artwork and the hit sound |

Then turn stats on:

```
set ctf_statsdb 2
```

The database creates itself. `1` gives one file per player instead; `0` is off.

Stats are viewable on the web via [q2lmstats](https://github.com/mgd34msu/q2lmstats).

## SLIPGATE bots

SLIPGATE is the mod's from-scratch bot system. It is built into the game module;
there is no separate bot library. The current source is still closing the
whole-project gates required for `v1.0.0`.

The target is recognizable, competent LMCTF play: grapple-first movement, real
flag-carrier escape runs, escorts that screen the carrier, and defense that
guards sightlines instead of standing on the flag.

The bots navigate on a per-map graph proven by actual physics runs (runs,
jumps, drops, swimming, grapple swings, lifts, teleports, and declared door
traversals), price their decisions on a live
cost surface (items, danger, duel range, cover, teammate support), and share
one team-wide belief of where enemies and flags are. The repository includes
demo-derived movement/combat inputs and tools for controlled comparisons. Those
retained datasets are development inputs, not proof that the current build has
passed its final matched trials; the completion plan names the measurements and
runtime receipts still required.

Current source includes steal → carry → escort → capture play, physics-proved
RUNE navigation, grapple movement, combat and perception controllers, team
roles, chat, and persistent match instrumentation. Completion still requires:

- generate, independently accept, and cold-load RUNE artifacts for all 181
  authoritative maps;
- finish the measured bot outcome and visible-behavior gaps while preserving
  the movement, combat, perception, and team-play non-regression gates;
- run the ten-server production fleet as persistent processes over the exact
  rotated top-20 lists and retain map-local receipts across native transitions;
- freeze, install, verify, and publish one hash-bound source/corpus/bundle
  identity.

See [`SLIPGATE.md`](SLIPGATE.md) for the design and current behavior, and the
completion plan for the exact gates and dependency graph.

### Server commands

SLIPGATE is administered through the game DLL's `sv` command surface:

| Command | Effect |
|-|-|
| `sv rune` | generate and atomically install `<gamedir>/maps/<map>.rune` for the currently loaded map |
| `sv sg add` | add one bot and let the team balancer place it |
| `sv sg add red` / `sv sg add blue` | add one bot to an explicit team |
| `sv sg list` | print the active bot roster |
| `sv sg remove <name-or-slot>` | remove one bot |
| `sv sg remove` | remove every bot |
| `sv sg kick worst` | remove the lowest-ranked active bot |
| `sv sg weights` | print the active weight source and values |
| `sv sg weights reload` | reload weights, then print them |

`sv rune` mutates the active game directory when generation succeeds; it is not
the corpus acceptance command. A production RUNE must also pass both C readers,
the Python reader, lint, applicable semantic checks, and a fresh-process load
under the exact module/BSP/config identity.

### Current development blockers

| Area | Current code state |
|-|-|
| RUNE corpus | The tracked list/controller now covers 181 distinct maps (including both `lmctf02` and `lmctf02c`), requires GNU/Make C-reader agreement with Python and lint, runs applicable semantic checks, and requires a separate bounded cold-load process before PASS. The 181-map generation run has not yet completed. |
| `lmctf58` | The current artifact passes structural readers but is missing live DIRECT plans for four CellarDoor/CellarDoor2 identities. It is not accepted. |
| Fleet | Non-random maplists now preserve file order and advance/wrap in the same `q2ded` process. `iterate2.sh` still launches finite one-map processes with hard-coded roster tables, and `waveloop.sh` recreates them and discovers a repo-root module. Persistent ten-process top-20 operation and explicit bundle install are not implemented. |
| Tool readers | `runeio.py --expected-identity` authenticates a reference RUNE and checks the artifact identity; `corpusgraph.py` has one strict loader with duplicate-key, non-finite-number, and contextual seed-weight validation. |
| Bot outcomes | Telemetry consumers now parse the production `seed/goal/sgoal/spd` schema and fail on zero recognized rows. Exact-build matched trials for steal initiation, conversion, defense, and captures remain open. |
| Release | The workflow builds the Linux/Windows modules and runs both Make-dialect host gates. The complete authenticated server bundle, transactional cutover, persistent-fleet cycle, and final tag are not complete. |

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
