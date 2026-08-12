# The cathedral, drawn properly

Written 2026-08-12, after the standards pass and the three-territory
primitive audit (slipgate layer, buzzmod layer, cross-layer seams).
This is how buzzmod + SLIPGATE would be built knowing what we know —
and it is a COMPASS for converging the existing tree, not a demolition
order. The one asset no rewrite can buy back is hundreds of hours of
film-verified behavior; every move below is film-gated like every
feature ever was. Companion documents: `slipgate/STYLE.md` (the line
law), `TOOLING.md` (the development environment), `LEDGER.md` (the
internal development record, where project coordinates live).

## 1. The layering as it should be

```
engine (yquake2)             id's masonry -- untouched
└─ stock game DLL (q2/lmctf) the church -- renovated only at our seams
   └─ PRIMITIVES              what everything above shares
   │    field algebra         Field_Alloc/Flood, Surface_At + components
   │    belief tables         aging sightings, never entity lookups
   │    frame context         sg_think_t, built once per frame
   │    registry              sg_cvars X-macro: a value lives once
   │    util                  SG_DistXY/CanSee/flag resolvers, timers,
   │                          team-index, RNG helpers
   │    db core               one SQLite primitive set for both backends
   └─ PERCEPTION              caco: what a bot has EARNED knowing
   └─ DECISION                the stage pipeline: goal → price → descend
   └─ ACTUATION               move/emit: policy becomes a usercmd
   └─ PRESENTATION            net client shim, personas, chat voice
   └─ RECORD                  stats DB, stdlog -- one write path
```

**Scope note (2026-08-12, owner's boundary):** this document describes
RELEASE ASSETS only -- the code that ships as the three game modules
plus the pak, nothing else. The development environment (the fleet,
the film instruments, the judging harness, the corpus and fixtures) is
deliberately absent from the layering above: it never ships, and
treating it as platform structure muddies both. Its own architecture
and law live in `TOOLING.md`. The one place the two worlds touch is
the release job's Assemble step, which packages exactly four files --
that list is the boundary, and the release-4 near-miss (a dead
dev-file bundle in the packaging) is why it stays explicit.

Two seams get formal surfaces:
- **`sg_hooks.h`** — every game-DLL-facing SLIPGATE entry point, one
  line each, so `grep SG_ sg_hooks.h` answers "what can legacy call."
  Today 8 of 20 hooks are hand-declared in callers and one
  (SG_CombatHit) has no header prototype anywhere.
- **Event hooks vs polling, decided per datum**: events for what a poll
  cannot reconstruct (damage direction, item-taken timing, rail
  rhythm); polling for what entity state always carries (flag status).
  The tree already follows this instinctively; it becomes doctrine.

## 2. What hindsight validates — keep, unchanged

- **Cost-field algebra as the decision language.** Every behavior that
  passed judgment is expressed as field composition. This is the
  platform's load-bearing invention.
- **Earned perception.** Belief tables with ages, Rule 19 comms as the
  only item intelligence. No judged rung required weakening it.
- **The film gate.** Instruments + blind judges + pre-registered bars.
  It caught what code review never would have. (The gate is
  development process, not release structure -- see TOOLING.md.)
- **Evidence-carrying banners.** Measured findings in comments are
  this codebase's institutional memory — self-contained per the
  refined rule 8, with project coordinates confined to LEDGER.md.
- **The print shim.** All 164 bprintf/cprintf sites already funnel
  through one choke point in sg_net.c without either layer knowing —
  the seam pattern working as intended.

## 3. What we'd do differently from day one → the convergence list

Ranked by (payoff × safety), each item its own film-gated commit.
Counts are from the audits, not estimates.

**P1 — primitives with existing proven patterns (do first)**
1. Timer/cooldown primitives — 171 comparison sites across 59 fields
   in three identical hand-rolled shapes. `SG_TimerReady/Arm/Expired`,
   `SG_Stopwatch`.
2. Team-index primitives — 104 unmarked `team - 1` sites plus a second
   spelling (`team - CTF_TEAM_RED`, 7) and opponent-index (3).
   `SG_TEAM_IDX`/`SG_OPP_IDX`.
3. Adopt `SG_FlagStand` at the 7 sites still hand-rolling the G_Find
   it was built to replace (think-path re-scans for immobile entities).
4. Cache the mega entity — two sibling functions each run O(num_edicts)
   scans per bot-second for a spawn point that never moves; the fix
   pattern (Combat_CacheItems) already exists in the same file.
5. `sg_hooks.h` — the legacy seam, one header.

**P2 — the buzzmod record layer (one write path)**
6. `ctf_sqlite_core.{c,h}` — the two backends carry duplicate copies of
   error/exec/transaction/migration/schema primitives and have ALREADY
   drifted (WAL tuning exists in one, not the other). One core, both
   call it.
7. Prepared-statement reuse — 38 prepare sites re-parse SQL per call;
   DB_SessionRecord already demonstrates the reset/clear-bindings
   idiom. Generalize it.
8. DB_NewID's missing rollback on failed COMMIT; its three va()-built
   INSERTs become bound parameters.
9. stdlog joins the gamedir path convention (the one Rule 7 violation
   in buzzmod); failed log open stops being server-fatal.

**P3 — wiring and flow (in progress / opportunistic)**
10. `sg_think_t` through the eleven stages (defined; wiring underway).
11. Split the sg_bot.h junk drawer into sg_corpus.h / sg_frame.h.
12. Player-lookup-by-name-fragment helper (two copies today);
    spawn_loadout resolution cached across respawns (three itemlist
    scans per token per spawn today); ctf_BSafePrint stops discarding
    its priority argument into an unconditional dprintf.
13. Angle-normalize and RNG-range helpers (7 and 18 sites); the
    `safe_append` idiom named when its second copy appears.
14. Control-flow simplification, one flow at a time, canary-gated —
    the relocated-not-reduced complexity from the standards pass.
15. Banner-evidence translation — existing comments citing
    project-internal coordinates (wave numbers, rung labels) are
    rewritten as self-contained findings when their file is next
    touched (STYLE.md rule 8 as refined 2026-08-12).

**Fixed already during the audit** (2026-08-12): the three stale
ERR_FATAL gi.error sites (NULL-format on the fatal path) and the two
FL_BOT-vs-SG_OwnsBot debug gates.

## 4. What the audits cleared

Worth recording what needs NO work, so effort doesn't drift there:
string/buffer safety in both added layers (zero raw sprintf/strcpy in
slipgate; buzzmod's are all bounds-guarded — the historic crash class
is dead), ring buffers (two, both correct, deliberately simple),
reimplemented data structures (none), time representations (four, no
cross-representation comparison exists), per-frame scans introduced by
buzzmod (none), and g_ctffunc's zero SG_ hooks (deliberate: flag state
is pollable).

## 5. The coming split (owner's direction, 2026-08-12)

SLIPGATE is likely to become its own repository — the bot platform as
a standalone project, with LMCTF as its first host game. That reorders
nothing below but re-weights it: every seam item is now also a
split-readiness item.

- `sg_hooks.h` (P1 item 5) becomes the platform's public API in
  waiting — after the split it is the surface a host game implements
  against.
- The coupling inventory that matters: slipgate/ currently includes
  the host's `g_local.h` and `g_ctffunc.h` directly. Pre-split, new
  code avoids deepening that reach; the split itself will interpose a
  host-adapter header where those includes are today.
- The instruments divide naturally: the demo-protocol layer
  (dm2speed/demoents/demokin) is game-agnostic and travels with
  SLIPGATE; the CTF-specific instruments (stands, carry windows, flag
  logic) stay host-side or become the host's instrument pack.
- Nothing splits until the film gate can run on both sides of the
  boundary — the split is itself a convergence step, proven like any
  other.

## 6. The rule for all of it

The fleet never stops, trials never share a window with refactors, and
a convergence step that cannot prove behavior identity (build gates +
canary wave) does not land. The cathedral goes up around the services,
never instead of them.
