# SLIPGATE C style guide

Every rule below was earned by a real failure in this codebase's
development — none is imported taste. The failures are described in
plain terms so the rules stand on their own; the project's internal
development record (LEDGER.md, git history) holds the full forensics
for anyone who wants them.

## 1. Files are modules

One topic per translation unit. `static` file-scope symbols are the
module's private state — that is C's encapsulation mechanism, use it.
The header is the interface and says one thing: if a header
accumulates externs from unrelated subsystems, it is a junk drawer and
gets split. A file's honest size is whatever its one topic needs — a
4,000-line chat module around one emit pipeline is correct; a
10,000-line file mixing navigation, combat, roles, and telemetry is
not, and this tree once had exactly that.

## 2. Functions are stages

A function does one nameable job. Soft budget: ~150 lines. Over 300
requires a banner justifying why the logic is irreducible (the combat
aim model qualifies: one solve, twenty interlocking locals). Over 700
is forbidden outright — this tree's think function once reached 6,800
lines, and its size hid three shipped bugs from every disciplined
reading of it.

## 3. Pipelines pass a context

When stages share frame state, they share ONE context struct built at
the pipeline's top (`sg_think_t`), not twenty-parameter signatures and
not file-scope mutable globals. A new stage costs a field, not a dozen
signature edits.

## 4. A value lives in one place

Cvar names and defaults exist only in the registry X-macro
(sg_cvars.h). Two call sites restating a default is a fork waiting to
ship — before the registry existed, this tree restated defaults at
over two hundred sites and correctness was luck. Tuning constants are
named defines with units in the comment; fitted values say what
measurement fitted them.

## 5. The second copy is the last

The moment a pattern appears twice, it becomes a helper with a name
(SG_EnemyFlag, SG_CanSee, SG_DistXY). This applies to analysis tooling
with equal force: two scripts copy-pasting one detector is how two
instruments drift apart while reporting the same number's name.

## 6. Debug gates wrap output, never state

`if (debug)` may print. It may never compute, assign, or advance a
clock. A role-tracking field in this tree once had its only write
inside a debug gate — with diagnostics off, every consumer of that
field silently read stale data in production for weeks. If a debug
block needs a value, the value is computed outside the gate.

## 7. No hardcoded environment

Paths derive from the gamedir cvar; ports, directories, and map lists
come from configuration. A persistence path in this tree once
hardcoded a specific server's directory name and worked only by
coincidence until read closely.

## 8. Comments carry constraints and self-contained evidence

A banner states what the code must honor and the measured finding that
proved it — in language a stranger inheriting this code can use
without our project records. "Trials determined that hot-room grabs
got the carrier killed within seconds nearly every time" carries
everything; a citation into our internal test numbering carries
nothing to anyone but us. Project-internal coordinates stay in
LEDGER.md and git history, which exist to hold them. Source line
references to THIS tree remain welcome — they travel with the code.
What comments never do: narrate the next line, address a reviewer, or
require an archaeology session to decode.

## 9. Restructuring and behavior never share a commit

Moves are verbatim — a moved body that cannot see its old scope makes
the compiler the verifier. Behavior changes are their own commits with
their own acceptance bars, and deploy alone: one variable at a time is
a code rule, not just a testing rule.

## 10. The gates are not optional

Every commit: full rebuild, RAW output read — a grep-filtered warning
count once declared a broken build clean because the failure text
didn't contain the word "error". Zero warnings on every compiler in
CI, each CI job's conclusion verified individually, never through an
aggregate exit code (the aggregate has lied). The strictest compiler
stays load-bearing: MSVC's uninitialized-variable analysis caught an
infinite loop that gcc shipped quietly — the one that once hung every
test server simultaneously.

## 11. Naming

Module prefix on internal families (Think_, Cbt_, Chat_, Lead_,
Danger_); SG_ prefix on cross-module surface. A name says what the
thing decides, not how ("Think_ApproachBand", not "Think_Helper2").
Loop counters are unique within their function — a careless rename
collision between nested scopes is what produced the server hang in
rule 10.

## 12. The client never sees development-speak

Every string a player or admin can encounter — broadcasts, command
output, menu text, chat lines, cvar names, release notes — speaks the
game's language, never the workshop's. Development vocabulary lives
only in code comments (within rule 8's limits), the LEDGER, and behind
the debug cvar (default off), which is the one sanctioned diagnostic
channel. Development tools may live in the repository; they are never
part of what the client needs to see.

## 13. Working beats beautiful; the ledger arbitrates

When a cleanup risks behavior during live evaluation, the cleanup
waits for a safe window. When beauty and verified behavior conflict,
behavior wins and the ugliness gets a LEDGER entry so it is a debt,
not a secret.
