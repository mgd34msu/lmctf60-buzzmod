# LMCTF BuzzMod

Quake II CTF mod. Fork of [QwazyWabbit's LMCTF](https://github.com/QwazyWabbitWOS/lmctf60)
with persistent player stats.

> **Note:** This mod is in active development and is likely not in working
> condition at any given moment. Please be patient.

## Install

Grab both files from [releases](https://github.com/mgd34msu/lmctf60-buzzmod/releases)
and drop them in your `lmctf` directory:

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

SLIPGATE is a from-scratch bot system built into the mod, aimed at bots that
play LMCTF the way humans play it — grapple-first movement, real flag-carrier
escape runs, escorts that screen the carrier, and defense that guards
sightlines instead of standing on the flag.

The bots navigate on a per-map graph proven by actual physics runs (runs,
jumps, drops, grapple swings, rocket jumps), price their decisions on a live
cost surface (items, danger, duel range, cover, teammate support), and share
one team-wide belief of where enemies and flags are. Movement doctrine and
combat habits are mined from a large corpus of demos from the game's
competitive era and validated in continuous automated experiments — every
behavior ships only after winning a controlled A/B trial.

Working today: full steal → carry → escort → capture play at every team size
from 2v2 to 7v7, cooked-grenade area denial, sound-directed speculative fire,
landing-point rocket leads on airborne targets, covered approach routing,
route commitment ("stay the course until it stops being good enough"), and
computed rail-lane defense posts.

Looking to add, in general terms:

- Smarter high-density play — coordinated breaches when the flag room never
  clears, instead of waiting forever or dying at the pedestal
- Attackers that exploit a railer's rhythm — sprint windows between shots,
  fling arcs that end behind cover
- Smoother travel movement where it still looks bot-like, without giving up
  any speed or outcomes
- True air-strafe acceleration chained through hook flights
- Defense that adapts its posts per map instead of one doctrine everywhere
- Bots modeled on individual players from the demo archive — styles, not
  just averages
- Rune-aware strategy beyond incidental pickups

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
