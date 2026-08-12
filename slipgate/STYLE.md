# SLIPGATE C style guide

Written 2026-08-12, at the close of the standards pass, by the author of
most of the sins it corrects. Every rule below traces to a specific
failure in this tree's history; none is imported taste. The test for
adding a rule: name the commit or wave where its absence cost something.

## 1. Files are modules

One topic per translation unit. `static` file-scope symbols are the
module's private state — that is C's encapsulation mechanism, use it.
The header is the interface and says one thing: if a header accumulates
externs from unrelated subsystems, it is a junk drawer and gets split
(sg_bot.h earned this rule). A file's honest size is whatever its one
topic needs — sg_chat.c at 4,300 lines of one emit pipeline is correct;
sg_arach.c at 10,805 lines of nine topics was not.

## 2. Functions are stages

A function does one nameable job. Soft budget: ~150 lines. Over 300
requires a banner justifying why the logic is irreducible (the combat
aim model qualifies: one solve, twenty interlocking locals). Over 700
is forbidden outright — SG_BotThink reached 6,800 and its size hid
three shipped bugs from every disciplined reading.

## 3. Pipelines pass a context

When stages share frame state, they share ONE context struct built at
the pipeline's top (`sg_think_t`), not twenty-parameter signatures and
not file-scope mutable globals. A new stage costs a field, not eleven
signature edits.

## 4. A value lives in one place

Cvar names and defaults exist only in the registry X-macro
(sg_cvars.h). Two call sites restating a default is a fork waiting to
ship — 222 sites restated defaults before the registry and survival was
luck. Tuning constants are named defines with units in the comment;
fitted values say what fitted them ("waves 63-67").

## 5. The second copy is the last

The moment a pattern appears twice, it becomes a helper with a name
(SG_EnemyFlag, SG_CanSee, SG_DistXY). This applies to the tools with
equal force: two analysis scripts copy-pasting one detector is how two
instruments drift apart while reporting the same name.

## 6. Debug gates wrap output, never state

`if (debug)` may print. It may never compute, assign, or advance a
clock. bot->last_role's only write sat inside a debug gate and the
rally silently read stale roles on every production wave for weeks.
If a debug block needs a value, the value is computed outside the gate.

## 7. No hardcoded environment

Paths derive from the gamedir cvar; ports, directories, and map lists
come from configuration. Danger persistence hardcoded "lmctf-hooktest"
and worked by coincidence until read closely.

## 8. Comments carry constraints and evidence

A banner states what the code must honor and cites the film that
proved it (wave numbers, trial verdicts, source line references).
That evidence trail is this codebase's institutional memory — keep it.
What comments never do: narrate the next line, or address a reviewer.

## 9. Restructuring and behavior never share a commit

Moves are verbatim — a moved body that cannot see its old scope makes
the compiler the verifier. Behavior changes are their own commits with
their own bars, and deploy alone: one variable per trial is a code
rule, not just a film rule.

## 10. The gates are not optional

Every commit: full rebuild, RAW output read (a grep-filtered count
declared a broken build clean once — the GitRevisionInfo race exits
without the word "error"), zero warnings, per-job CI conclusions
(`gh run view --json`, never aggregate exit codes — the aggregate lied
twice). MSVC /W4 stays load-bearing: C4701 caught the infinite loop
gcc shipped quietly, the one that wedged all ten servers.

## 11. Naming

Module prefix on internal families (Think_, Cbt_, Chat_, Lead_,
Danger_); SG_ prefix on cross-module surface. A name says what the
thing decides, not how ("Think_ApproachBand", not "Think_Helper2").
Loop counters are unique within their function — a rename collision
between nested scopes produced the wave-900 wedge.

## 12. Working beats beautiful; the ledger arbitrates

When a cleanup risks behavior mid-trial, the cleanup waits for the
window. When beauty and verified behavior conflict, behavior wins and
the ugliness gets a LEDGER entry so it is a debt, not a secret.
