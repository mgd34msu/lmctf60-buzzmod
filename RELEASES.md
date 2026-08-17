# LMCTF BuzzMod — Releases

Buzzmod uses [Semantic Versioning](https://semver.org/) beginning with `v0.5.0`.
Milestone releases increment the `0.x` series while the remaining SLIPGATE and
behavioral work is completed. `v1.0.0` is reserved for the fully completed
project.

The source version lives in `BuzzmodVersion.h`. A release tag must be exactly
`v<version>`; CI rejects a mismatched tag, builds all three libraries, and
publishes the release with its version, checksums, and runtime assets. The old
`release-1` through `release-6` tags remain the historical pre-SemVer releases.

## Release 6 — August 2026

Release 6 completes the next ordinary-action migration in SLIPGATE: live HOOK
execution now uses the same deterministic replay law that proves HOOK routes
offline. The adapter owns the full attach, pull, settle, and release lifecycle;
checks the final command immediately before each `ClientThink`; and fails closed
when the bolt identity, source witness, attachment, liquid state, or command
differential no longer matches the proved link.

Late physical attachment retains the established tolerance without advancing the
replay clock: the bot emits four independently checked fixed-view commands and
acknowledges attachment on the first real attached frame. Lost or replaced
bolts keep the legacy 15-second shelf priority, while liquid failures retain
their 30-second shelf. Reset and same-link reuse cannot consume a stale command
approval.

The migration passed strict GCC and Clang tests, ASan/UBSan, both build-system
dialects, full shared-object builds, Linux and Windows warning-clean CI, and a
private live server run. That run directly observed the reducer's attach, pull,
and release entry points while two bots used generated HOOK routes, with no
HOOK replay failure.

### Known limits

This release covers ordinary `RL_HOOK`, not compound `RL_DOOR_HOOK` or the
other PREOPEN/RIDE compound actions. The focused DROP acceptance harness also
predates this overlapping HOOK integration and is being recomposed and rerun;
the full 181-map runtime acceptance gate remains pending. Those limits are
recorded explicitly rather than implied complete by this release.

## Release 5 — August 2026

Release 5 is a visibility-and-contract release. For players and admins it
adds richer stats reporting and steadier in-game boards. Under the hood it
replaces implicit RUNE assumptions with an explicit fail-closed
contract and starts moving live movement execution onto the same deterministic
replay law used to prove routes offline.

### What ships

Installation is unchanged from Release 4:

| File | |
|-|-|
| `gamex86_64.so` | Linux 64-bit game module |
| `gamex86_64.dll` | Windows 64-bit game module |
| `gamex86.dll` | Windows 32-bit game module |
| `lmctf6-buzzmod.pak` | scoreboard art and sound assets |

There is still no separate bot library or prebuilt navigation pack. Server-side
RUNE data is generated locally and reused on later loads.

### New for players and admins

Shared-database stats gain four new reports: `activity` (busiest players over
the last 7 days), `momentum` (recent capture-rate movers), `card` (one player's
career card), and `vs` (head-to-head results only from games both players
actually played). Every match now also ends with an automatic console summary:
final score, top capper, top defender, top killer, and accuracy leader.

The live boards are also better behaved. Instead of repainting continuously,
they now update on change and coalesce to at most once per second while busy,
with captures pushed immediately. When a full layout will not fit the wire
budget, boards now step down cleanly through condensed and minimal variants
instead of truncating awkwardly.

### RUNE and deterministic movement

SLIPGATE's movement graph now has an explicit RUNE contract shared across the
game code and Python tools: canonical action IDs, generated wire contracts,
little-endian codecs, authoritative map/entity/physics identity binding,
transactional writers, strict loaders, and authenticated sidecars.

That contract is intentionally strict. Dense DROP, SWIM, and HOOK links are
accepted only when they carry proved provenance under the exact payload, world
identity, proof law, and action contract. Mismatched or stale artifacts fail
closed instead of being silently tolerated.

This release also introduces a deterministic shared replay core for RUNE
actions. Offline DROP, SWIM, and HOOK proofs now run through that common law,
live SWIM is routed through it, and live DROP now uses the same controller
on the same shared contract. Rotating topology proofs were hardened too: routes
around rotating geometry are no longer tied to whatever phase happened to be
sampled during proof.

### Known limits

This is not the end of the SLIPGATE movement program yet. Live HOOK has not been
migrated to the shared replay law, and compound `DOOR_DROP`, `DOOR_SWIM`, and
`DOOR_HOOK` actions are still future work. The project also keeps an honest
181-map control corpus with explicit PASS, map failure, timeout, and
invalid-asset outcomes; it is evidence for the RUNE rollout, not a claim that
every map/action combination is already finished. Incompatible artifacts are
invalidated when the action contract changes; strict rejection is the policy.

## Release 4 — August 2026

The bots are new, top to bottom. Release 3's preview bots (the external
botlib) are gone; this release ships **SLIPGATE**, a from-scratch bot
platform built directly into the game modules and developed against a
single standard: film of bot play is judged blind against recorded human
games, and a change ships only if fresh judges cannot tell the two apart.
On the movement and fighting tests they cannot — the bots were called
"human" more often than the actual humans were on the fights set. The
full design and evidence record is in `SLIPGATE.md` and `LEDGER.md`.

### What ships

| File | |
|-|-|
| `gamex86_64.so` | Linux 64-bit game module |
| `gamex86_64.dll` | Windows 64-bit game module |
| `gamex86.dll` | Windows 32-bit game module |
| `lmctf6-buzzmod.pak` | scoreboard art and the hit sound |

That is the whole download. There is no bot library, no character files,
and no 290 MB navigation pack — the bots live inside the game module and
build their own navigation data.

### Running the bots

Once per map, on the server, generate the map's movement graph:

    sv rune

The file is written under the mod directory and reused on every later
load. Then manage the roster:

    sv sg add            // add a bot on the smaller team
    sv sg add red        // or force a team
    sv sg list
    sv sg remove <name>
    sv sg kick worst

Bots join as ordinary clients: they appear in the scoreboard, talk in
team chat (item callouts, quad timing, banter mined from real games),
and flow into the stats database like any player when `sg_sessiondb` is
on. All tuning cvars ship at the values that passed blind judgment; you
do not need to set anything.

### Known limits

Team-level reads (escort spacing, defender symmetry) and raw capture
volume are still distinguishable from human play under instrumented
comparison and are the focus of the next release. Nothing here affects
play against them on a pub — those tells took purpose-built measurement
to find.

## Release 3 — July 2026

Adds bot support. Bots are a **preview** in this release: they load, join teams
and fight, but their navigation is not finished — see "Known limits" below
before you put them on a public server.

### What ships

| File | |
|-|-|
| `gamex86_64.so` | Linux 64-bit game module |
| `gamex86_64.dll` | Windows 64-bit game module |
| `gamex86.dll` | Windows 32-bit game module |
| `botlib.so` | Linux bot AI library |
| `botlib-x64.dll` / `botlib-x86.dll` | Windows bot AI library |
| `lmctf6-buzzmod.pak` | scoreboard art and the hit sound |
| `lmctf-buzzmod-botfiles.tar.gz` | bot roster and character, chat and item configs |

Navigation data is a separate download — one `.aas` file per map, 180 maps,
about 290 MB. It is not built here because that needs the retail map paks.

### Installing the bots

Put `botlib.so` (or the `.dll`, renamed to `botlib.dll`) next to the Quake II
executable, unpack the bot files into your mod directory, and drop the `.aas`
files into `<moddir>/maps/`. Then:

    set minimumplayers 10     // fill the game to ten players with bots
    set bot_skill 4           // 1 to 5
    set botctfteam 0          // 0 auto, 1 red, 2 blue
    set bot_stats 0           // 1 to include bots in the stats database

Referees get a **Manage Bots** page in the referee menu: add by name, add
random, remove, remove all, and live controls for team, skill, fill-to, chat
and stat tracking. `sv bot menu` opens it from the console.

Bots use LMCTF's offhand grapple — they hook while holding a weapon, the same
as a player, rather than switching to it. `bot_grapple 0` turns that off.

Bot statistics are **off** by default. A server filling out a game usually does
not want bots in the leaderboards; `bot_stats 1` includes them.

### Known limits

Bots spawn, are balanced across red and blue, pick goals, fight and chat. What
they do not yet do is route: the pathfinding call fails and they fall back to
wandering, so they will not reliably run flags. The navigation data is not the
cause — it compiles clean and the bots' own positions resolve inside it. Set
`bot_developer 1` to see which fallback is being taken.

Treat bots as something to experiment with this release, not as a substitute
for players.

### Also fixed

- `SkinRandom` divided by zero when a team had no skins listed, taking the
  server down as soon as anyone connected.
- `Cmd_Team_f` wrote through a pointer to a string literal — reachable by
  typing `team` with no argument while on neither team.
- `visible()` did not exist in a CTF build, though `g_combat.c`, `g_turret.c`
  and `p_trail.c` all still called it.

---

## Release 1 — July 2026

First BuzzMod release. Based on LMCTF TE 6.0.

### Install both files

This release ships **four** files. The library for your platform, and a pak:

| File | |
|-|-|
| `gamex86_64.so` | Linux 64-bit |
| `gamex86_64.dll` | Windows 64-bit |
| `gamex86.dll` | Windows 32-bit |
| `lmctf6-buzzmod.pak` | **required, all platforms** |

Both go in your `lmctf` game directory. The pak carries the artwork behind the
statboard, team statboard and railboard, and the flag-carrier hit sound. No
stock LMCTF pak contains them — verified against every pak and loose file in
`baseq2`, `lmctf` and `ctf`, 10,718 assets, none of the seven present. Without
it those three boards draw their text on an empty background and the hit sound
is silent.

### Statistics now persist

Player statistics are written to a SQLite database and reloaded when the player returns.
Previously every number reset at the map change and nothing was ever written to disk.

Turn it on with `ctf_statsdb 2` (shared database) or `ctf_statsdb 1` (a file per player).
It defaults to `0`, off, so an existing server behaves exactly as before until you enable
it. The database creates itself on first start.

Recorded per player: frags, times fragged, suicides, captures, flag pickups, flag returns,
flag-carrier kills, offense kills, defense kills, assists, best kill streak, killing
sprees, best capture streak, sweeps, and time played.

### New statistics

- **Sweeps** — awarded to everyone on the winning team when the other side never captured
  your flag once. A tie is not a sweep.
- **Capture streaks** — consecutive captures with no enemy capture in between. When either
  team scores, the other team's runs end.
- **Kill streaks and sprees** — your best run without dying, and how often you reached five
  frags in a row.
- **Offense kills** — killing a defender inside the enemy base, the mirror of the
  defend-the-base award that was already there.
- **Fragged** — killed by another player. Deliberately separate from LMCTF's existing
  deaths counter, which has always meant deaths you brought on yourself.

### New commands

Players get `cmd lifetime [name]` for stored totals and `cmd rank [column] [n]` for the
leaderboard. `cmd stats` still shows the current level only.

Admins get `sv statsdb` with `status`, `flush`, `top`, `player`, `export`, `backup`,
`rename`, `prune` and `reset`. `sv statsdb help` lists them. See the README for detail.

`rename` deserves a mention: players are identified by name, so changing your handle used
to mean starting from zero with the old record orphaned. `rename` either relabels the
record or merges the two.

### Crash fixes

**`cmd stats` crashed the server, every time it was run.** The report was assembled with
`strcat` into a 512-byte buffer while emitting roughly 800 bytes of text plus thirty
numeric fields — a stack overrun on every invocation, not an edge case. Worst case now
measured at 1513 bytes against a 2048-byte buffer.

**`cmd stats <name>` crashed after a player left.** The player search checked nothing about
the client slot and handed the result straight to code that dereferenced it. A departing
player keeps their name but not their stats record.

**`ClientBegin` dereferenced the stats record without checking it**, and it is legitimately
null after a map change and after a savegame load.

**Loading a savegame left the entire stats list dangling.** `ReadGame` frees the memory
pool the list lives in without resetting the list head.

**The flag-carrier hit sound reached the engine with a null attacker** when a carrier took
world damage — the sound call sat outside its own guard.

**The railboard dereferenced a stats record with no null check**, reachable for any client
that had not finished connecting.

### Other fixes

- The homing rune's line-of-sight filters never worked. Both tests were written as bare
  function names rather than calls, so they were always false and the rune tracked targets
  through walls and behind itself.
- Hits were attributed to whichever weapon the attacker happened to be holding when the
  damage landed, so a rocket still in flight while its owner switched weapons counted
  against the wrong gun. Attribution now comes from the means of death. The same code read
  `pers.weapon->classname` with no null check, which the rest of the codebase tests for.
- Damage dealt and damage received were commented out, so both counters were dead.
- `Pickup_Rune` left its stat index at 0 on an unrecognised rune type, and 0 is the ping
  counter, so a bad rune quietly corrupted the player's ping figures.
- Only the railgun counted shots and hits, and rail shots were being written to the generic
  `shots` column — so "accuracy" was railgun accuracy under the wrong name. All weapons are
  now instrumented separately from the rail figures.
- The statboard showed different columns for the two teams under the same headings — red
  listed frags and flag-carrier kills, blue listed score and captures.
- A rejoining player's score was restored and then immediately erased, because it was
  written before the call that clears it.
- Statistics were lost at every map change. They were folded into lifetime totals only when
  a player disconnected, so someone who played five maps and then left kept only the last
  map's numbers.
- A team kill during a railgun match credited the killer with a death they never took.
- Seventeen places printed a `long` with `%i`. `Com_sprintf` had no format attribute, so
  the compiler could not check any of its call sites; it does now, and CI fails on
  warnings.
- `va()` was an unbounded `vsprintf` into a fixed 1024-byte buffer. It is bounded now.

### Enabled but off by default

`ctf_switch_penalty` restores the team-switch penalty that had been commented out in
upstream. Set it to `1` and switching to the team that is both larger and winning clears
your score; joining the smaller, losing side is always free. Defaults to `0`, the current
behaviour.

### Notes for server admins

- The database is created automatically, and repairs its own schema if a table goes
  missing. Databases from an earlier BuzzMod release are migrated in place — columns are
  added as needed and existing rows are left alone.
- `sv statsdb backup <file>` is safe to run while players are connected.
- `sv statsdb reset confirm` and `sv statsdb prune <days> confirm` are permanent. They are
  reachable over rcon, so treat rcon access accordingly.
- Statistics only accumulate while a match can score. During countdown and after a match
  ends, nothing is recorded — the same rule the existing scoring already followed.
