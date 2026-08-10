# Blind-set composition rule

Written 2026-08-09 after two sets were damaged by map choice. A judging
set is an instrument like any other: if its maps cannot express the
behavior under test, the verdict measures the map, not the bots.

## The two incidents this rule exists for

**Rung-2 set #1 (lmctf22 human sheet, unanimous conviction-4 miscall).**
Three judges called a real human game "bot" at high conviction because
lmctf22's flat geometry suppresses off-graph flight *for both
populations*. The discriminator had no room to exist on that map, so the
sheet showed bot-like numbers for a human game. One of six sheets was
noise by construction.

**Outcomecard Stage A (lmctf22 gate FAILS).** The same map fails the
rung-5 instrument's separability gate outright, for the same family of
reasons: too little of the behavior the scalar reads.

## The rule

A map may appear in a blind set for a given rung only if it is
**discriminating for that rung's instrument** — i.e. the instrument's
Stage-A record shows the map's validated scalar separating known-human
from known-bot film at or above the 0.85 gate on that map specifically.
Separability recorded on a *different* map does not transfer.

Practical consequences, per rung, as of this writing:

| Rung | Instrument | Discriminating maps on record | Excluded (with reason) |
|---|---|---|---|
| 2 | routesheet | mactf06 | lmctf22 — suppresses off-graph for both populations |
| 3 | fightsheet | mactf06 | lmctf44 — no usable human fight film (all stubs) |
| 4 | teamsheet | lmctf22 (escort_fraction 0.95, radius-stable) | mactf06 — escort_fraction sub-gate at 0.69 |
| 5 | outcomecard | mactf06 (steals_total 0.964) | lmctf22 — gate fails outright (best 0.828) |

Note that rungs 2 and 4 discriminate on *opposite* maps. That is not a
contradiction: they measure different behaviors, and a map that flattens
route texture can still expose team structure. It does mean a single
"good map" list is wrong; the list is per rung.

## Secondary requirements

1. **Roster matching.** Bot film is structurally 5v5. Human demos with
   materially different rosters (the lmctf22 3v3) are excluded rather
   than balanced, because no matching bot film exists and manufacturing
   it is a bigger intervention than dropping one demo. Where exclusion
   would take the human arm below three demos, the set waits for corpus
   growth instead of running underpowered.
2. **Recording-shape caveats are briefed, not hidden.** Bot film is
   serverrecord and takes pov-parity; human film is client POV and
   cannot. Panels whose Stage-A record shows coverage sensitivity are
   rendered but the judges are told, in the prompt, not to convict on
   them alone (see tools/rung4-protocol.md for the wording that worked).
3. **Sealed captions, always.** Map, 12-char hash, carry count. Nothing
   else — duration and player-count leaks burned judge sets #3 and #4.
4. **Fresh judges every set.** A judge who has seen a previous set knows
   the answer key's shape.

## When no map qualifies

Do not run the set. Either grow the corpus on a map where the behavior
exists (the manifest at tools/corpus-manifest.csv lists 18 blind-set-
capable maps by usable human volume, and the fleet can farm bot film on
any of them), or build the eye that can see the behavior on the maps you
have. A set run on non-discriminating maps produces a number that looks
like a verdict and is not one.
