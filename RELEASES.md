# LMCTF BuzzMod — Releases

Releases are numbered and dated. There is no version number tracking upstream; go by the
release number.

Tag a release as `release-1`, `release-2` and so on. Pushing that tag builds all three
libraries and opens a draft release with them attached.

---

## Release 1 — July 2026

First BuzzMod release. Based on LMCTF TE 6.0.

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
