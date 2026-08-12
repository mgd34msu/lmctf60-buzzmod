# The screen budget: what the protocol actually gives us

Written 2026-08-12 from two ground-truth surveys: the yquake2 layout
interpreter read from engine source (`docs/layout-isa.md`, the full
token table with citations) and a complete inventory of this game
DLL's current usage. Every number below is measured, not remembered.
This document is the foundation for the declarative screen layer
(layout builder + stat-slot registry) on the convergence list.

## The hard numbers

| Resource | Limit | Consequence of exceeding |
|---|---|---|
| One layout update on the wire | **1400 bytes** (MAX_MSGLEN), no fragmentation | multicast: fatal server crash; unicast: the frame silently drops |
| Client layout buffer | 1024 bytes | excess ignored |
| Statusbar program (CS_STATUSBAR) | 1535 bytes (special multi-slot span) | — |
| Player stats | 32 × 16-bit, delta-compressed per frame | 5 engine-reserved for HUD basics + 22 in use = **5 free slots** (27-31) |
| General configstrings | **512 slots, all free**, live-updatable | — |
| Ordinary configstring length | 64 bytes | — |

## How the interpreter wants to be used

A layout string is a PROGRAM re-executed by the client every rendered
frame. `num/hnum/anum` render live stat values; `stat_string`
dereferences a configstring through a stat; `if <stat>` skips a
following block when the stat is zero. Therefore:

- **Structure rides the layout; data rides stats and configstrings.**
  Resend the program only when a screen's shape changes; scores,
  timers, names, and conditional rows flow through the delta-compressed
  stat pipeline (near-free) and configstring updates.
- Layouts re-parse every frame with no client cache, so a configstring
  update is visible immediately with zero layout resend.
- The interpreter fails silent (bad tokens skipped, `if` is
  non-nesting, numeric fields clamp width) — a compiler must validate
  because the client never will.

## What our current code does (the case for the builder)

Ten hand-assembled producers, five different overflow-guard styles,
and until today two producers with none: the menu system (unguarded
strcat into 2000 bytes) and the MOTD screen (unguarded into 5000) —
both now guarded at 1380, but the guard-per-producer pattern is the
disease, not the cure. The boards cap themselves at 1024 (an arbitrary
sub-limit leaving ~376 bytes unused), one board at 1000, comments in
five places warn "it isn't that hard to overflow the 1400 byte
message limit!", and menus silently truncate at a fixed 18 slots
(paginated by hand where anyone noticed). Ping is repainted as literal
layout text on every refresh instead of riding a stat slot. All of it
is the same missing primitive.

## The design the numbers dictate

1. **A stat-slot registry** (X-macro, like the cvar registry): 27-31
   free today; the registry makes every allocation explicit and
   collision-proof, and frees slots by moving layout-text data
   (e.g. ping columns) onto stats where refresh is free.
2. **A layout compiler**: screens declared as tables/rows/cells with
   truncation priorities; the compiler emits tokens, enforces the
   1380-byte wire budget (crash-proof by construction), and assigns
   hot data to stats/configstring indirection. Boards NEVER paginate
   (owner's ruling: page-flipping demands inputs a passive screen
   does not own) -- density is absorbed by a variant ladder (full /
   condensed / minimal formats, breakpoint-selected by roster size,
   fit-verified by measurement, whole-screen downgrade never partial
   truncation), and content too deep for any single screen belongs to
   the console print stream, which composes across frames without
   practical limit. Serving is push-on-change: stats events mark
   boards dirty; a 1 Hz dirty-gated tick rebuilds and pushes to
   current viewers; milestone events (captures, match end) push
   immediately; caches serve all requests instantly.
3. **Configstring-backed dynamic text**: 512 free slots is a huge
   untapped surface — team names, top-scorer lines, rotating info,
   per-client strings via stat_string, all updatable without layout
   resends.

The builder lands as a convergence block after the record layer
(ARCHITECTURE.md P2); the two guards added today are the interim
protection.
