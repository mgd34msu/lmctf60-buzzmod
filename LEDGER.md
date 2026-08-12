# SLIPGATE LEDGER

The page the goal demands: ladder, trials, morgue, polish state — two
minutes, cold. Updated at every verdict and every arm. Times are local;
waves are the fleet clock (~16 min each, 10 servers, never stops).

*Updated 2026-08-12 ~01:20: **THE DEPLOY PACKAGE GOES OUT** — the
through-the-flag grab fix (9900458 + d54c971 + the wall clamp) and the
last_role debug-gate fix (30cbaa1) ride the standards-pass build to the
fleet at the next wave boundary. **All three trial clocks (hookpong
s03, dither-120 s06, teamskew s05) RESTART at WAVE 924** (deployed
01:19:43, all ten servers healthy) — 16
fresh waves per arm under the new baseline; pre-deploy film is void for
those trials. Pre-registered bars for the package itself: stand-area
grind share collapses, steal conversion 1.5% → toward 3.3%, approach
rate up (the rally actually fires now), grind_spm 10.9 → toward 3.2.
Caps read through a fresh A/A check, since the old s03/s04 arm-bias
correction predates both fixes. CANARY (waves 924-925, 8 demos): the
package functions -- attacker stand-grind share DOWN (0.145 vs 0.16),
conversion UP (2.0% vs 1.5%), and the rally visibly holds (fewer,
better entries). Total grind rose (13.3 vs 10.9) because guard presence
jumped 0.237 -> 0.342: last_role working means defenders actually post,
and posted defenders exercise the known micro-pacing defect. The grind
lever is now sg_patrol, next in the trial queue.*

*Last updated: 2026-08-11. **RELEASE 4 PUBLISHED** — tag `release-4` at
main merge 1b7a5e2, all four CI jobs (Linux x86_64, Windows x86, Windows
x64, Publish release) verified **individually** green; release carries
the three game modules + pak, with the dead botlib bundle dropped from
packaging. The stale `slipgate-1.0` tag (cut on a red commit) was
deleted. Standing lesson: CI is verified per job via
`gh run view --json conclusion,jobs`, never via aggregate `gh run watch`
exit codes — the aggregate lied twice. Development continues on
`slipgate`; three trials armed 17:07 (hookpong s03, dither-120 s06,
teamskew s05) — armed film is waves 895–899 and 901+, floors land
~wave 912 (~22:00). **Wave 900 is VOID**: a locally-built .so carrying
the half-renamed sg_fields tree (the C4701 bug CI caught) auto-deployed
mid-wave and pinned all ten servers in an infinite loop at map load;
SIGTERM-immune, cleared by SIGKILL at 19:01, fixed build r662~ecbeaaf
deployed and verified (10 servers, ~5% CPU, film flowing). Lesson: the
fleet auto-deploys the newest repo-root .so — never leave a local build
lying around between edit and commit unless the tree is known-good.*

## Conduct audit (2026-08-11, owner-ordered: "have you watched them?")

Nobody had. `tools/conduct.py` now measures visible stupidity and
defense regime directly from film; `conduct-baseline.json` holds the
first pooled read (94 human demos / 28 bot demos, observed-time
denominators):

- **Grind (going-nowhere movement): bots 10.9 s/min vs human 3.2 —
  3.5x.** Higher reversal rate inside windows (1.37 vs 0.71/s): the
  signature is oscillation, not pinning. This was invisible to every
  scalar instrument. Localization by map cell running; suspicion is it
  co-locates with the plateau/HOOK revisit spike hookpong is in trial
  against.
- **Defense regime: the steal gap is NOT a strong-defense artifact.**
  Guard presence at stands is equal (0.237 vs 0.247). Bot offense
  generates HALF the approach pressure (4.7 vs 9.1 stand entries/min)
  and converts entries at half the rate (1.5% vs 3.3%). Stage-2 bars
  are hereby re-based: opportunity-conditioned rates (approach_pm,
  steal_conv at measured guard_frac), not raw steals/min across
  regimes.
- Caveat, standing: client-POV human film conditions stand observation
  toward busy moments — cross-population values are ratio evidence,
  same rule as escort_fraction_obs.
- **Localization (same night): the grind is three behaviors.** (1)
  lmctf22: ATTACKER grind at the enemy stand dominates (4293 vs 1076
  s) — bots reach the flag room and circle without grabbing; same
  defect as the 1.5%-vs-3.3% steal conversion, two instruments, one
  behavior. Candidate mechanism `sg_grabcommit`: inside stand radius
  with the flag home, commit to the grab vector. (2) mactf06:
  DEFENDER grind at own post dominates (5315 vs 1169 s) — posting
  satisfies Rule 20's no-idle by micro-pacing, which humans never do
  (humans stand still; camping is low gross speed and does not
  trigger the detector). Candidate mechanism `sg_patrol`: walk-pace
  arcs between 2–3 raillane post seeds instead of oscillation. (3)
  ~2/3 of grind on both maps is far-field navigation oscillation —
  the plateau family; hookpong's verdict rules on it first. Both new
  mechanisms queue for arms as trial pairs free up; grind_spm is
  their pre-registered bar (bot 10.9 → toward human 3.2).

## The apology, on the record (2026-08-12, ordered by the owner)

I trashed this codebase and I apologize for it. The specifics, so the
apology means something: I grew sg_arach.c to 10,805 lines with a
6,800-line function at its heart by appending every mechanism to the
same file for four hundred waves instead of building modules; I
duplicated 222 cvar lookups with their defaults restated at every site;
I shipped an attacker grab aimed at a spawn marker the flag does not
sit on after the owner had already fixed that disease twice on other
touches; I left last_role's only write inside a debug gate, which
silently killed the rally on every production wave; I hardcoded the
fleet's directory name into danger persistence; and I wedged all ten
servers with a poisoned local build my own rename bug produced. The
owner ordered the cleanup, named the standards, and caught the grab
bug from film I never watched. The 2026-08-11/12 standards pass — the
registry, the modules, the decomposition, the three bug fixes — is the
repair, and per-job CI plus full-rebuild gates are the standing
protection against my doing it again.

## Standards-pass findings (2026-08-11, code-quality overhaul)

Three real defects surfaced by the refactor, none visible to any film
instrument:

1. **`last_role` never updated with `sg_debug` off** (fixed 30cbaa1).
   Its only write sat inside the debug gate; the fleet's wave configs
   do not set sg_debug. The rally partner census, escort head-count,
   and wavepush attacker census read stale roles on every production
   wave. **Every rally/wavepush/escort-census verdict measured on
   debug-off film is suspect and gets re-read after this deploys.**
2. **Danger persistence hardcoded `lmctf-hooktest`** (fixed a02f57d) —
   wrong path on any other gamedir; follows the gamedir cvar now.
3. **The attacker grab aimed at the spawn marker, not the flag item**
   (fixed 9900458 + d54c971, the owner's through-the-flag order) —
   see the conduct-audit section above.

The overhaul itself: cvar registry (222 sites → one table), util layer,
six subsystem extractions out of sg_arach.c (roster, clock, danger,
weights, tilt, lead), think-function decomposition in progress. Every
commit zero-warning, every CI job verified individually.

## The ladder

| Rung | State | Evidence |
|---|---|---|
| 1. Raw movement | **PASSED** | Set #5: bot sheets passed 7/9, judges 3/18 overall |
| 2. Routes | residual-capped (owner ruling); baseline 18/18 | off-graph = accepted residual (9 mechanisms + forensics + measured fall cost). Second tell open: interval-2 revisit spike 0.28 vs human 0.20 — nobacktrack dose struck; it is plateau oscillation between legs, needs tie-break diagnosis |
| 3. Fights | **PASSED** — set #3: judges 5/18 | two of three judges fully inverted; bots read human 6/9 vs humans 2/9; same standard as rung-1's 3/18 pass. Residual: a real ~2x hits/shot edge (0.58 vs 0.29) — measured after the marker artifact was fixed, so the pass was earned against inflated evidence, not because of it |
| 4. Team decisions | four mechanisms down; routejitter-25 is cut #5 | escort_fraction_obs passes every radius and leave-one-out (0.918–0.986 both maps) but is MORE parity-sensitive than the old scalar — separation survives the coverage knob rather than being freed from it. **Measure rung-4 fixes as between-arm deltas at fixed parity; never as distance-to-human-band.** Two mechanisms down; breather ablation live |
| 5. Match outcomes | protocol written; judging deferred by design | its only eye (steals_total, 0.964) measures the steal-volume gap stage 2 exists to close — so rung-5's blocker and stage-2's first number are THE SAME WORK. Judging waits; the work does not |
| 6. Live vs owner | **STRUCK** | Rule 22: the recorded corpus is the complete bar |

## In trial now

| Trial | Arms | Armed | Bars (pre-registered) | Verdict due |
|---|---|---|---|---|
| (none — all pairs steady on the adopted stack) | | | | |

## Adopted (film + data on record, most recent first)

- **escortdose 35** (2026-08-09, **CONFIRMED 2026-08-10**): assigned escorts were wasted bodies. Three independent samples — 86v54 (side effect), 42v22 (8-wave trial), **83v52 over 17 waves/arm** (4.88 vs 3.06 caps/game, steals 201 vs 118). Freeing two thirds of escorts to attack raises caps ~60%. Neutral on the rung-4 film tell. The interim fleet-wide peek that read caps DOWN was confounded (mixed maps/formats, pre-adopt window containing the winning arm) — a peek is not a census.
- **aimtexture** (2026-08-08): +0.30° toward the human aim anchor (7.66 vs 7.36, 16 waves, caps flat), cadence raggedness side gain; dose-ladder noted. Second dark-feature adoption.
- **escape priors** (2026-08-07): human-mined carrier escape routes — conversion 0.413 vs 0.355, caps 42 vs 31, steals 102 vs 76 (8 waves/arm, lmctf22). First dark-feature adoption.
- **wcommit** (2026-08-07): keep the held gun anywhere in the band ladder — switch_diagonal 0.691→0.828 (human 0.897), caps 25 vs 13, zero arm overlap, 12 waves/arm.
- **breather 4** (2026-08-07): dose ladder 0/4/8, 19 waves/arm — conversion 0.046/0.114/0.116; smaller equal dose shipped.
- atkobj 125: +7% steals, caps flat; 150 was null.
- Movement stack that passed rung 1: jitter 8, ribbon 48, tactics, nobacktrack 60.
- Pit chain (termbrake, terminal straight-walk, sink ban + widening, air graph): 80% conversion, 0 drownings census.
- Comm stack (Rule 19): itemcomm, radio wavs + human lag, itemlead, quad 60/30 either-or one-voice, mega taker-clock, honest ear, hit sense.
- Corpus chat (134 lines, 5 reaction categories), unified human/bot say_team parsing.
- Native clean-room platform: sg_net client layer, ping 5–15ms, personas, stats flow-through; all legacy bot code removed.

## The morgue (honest nulls and strikes, with their lessons)

- **sg_beliefcone / sg_beliefrange** (2026-08-11): STRUCK before trial — the acquisition gap runs backward (bots open on off-cone targets LESS than humans; the combat firing cone already restricts). Fourth and fifth features retired by measurement.
- **sg_spawnbeat** (2026-08-11): STRUCK before trial — bots already sit on the human post-spawn baseline (postspawn_purpose 0.665 vs 0.674, separability 0.625/0.566, chance-adjacent). Third feature retired by measurement.
- **de-pace** (2026-08-11): struck at the 16-wave floor, double miss — escort WRONG WAY (0.638 vs 0.564), conversion down. Slowing a lingering teammate keeps it in the bubble longer when the carrier approaches from behind. Fourth mechanism dead on the co-travel tell.
- **anti-linger** (2026-08-11): struck — the 7-wave delta was noise; corridors give a spatial surcharge no gradient (every link sits in the carrier's bubble).
- **post-zone hypothesis** (2026-08-11): refuted cleanly — 86–89% of the escort gap is mid-field co-travel, not defenders at the stand; humans are MORE post-zone-dominated than bots. The ~12% defender share is the likely Rule-21 residual.
- **aimtexture dose 2.5** (2026-08-09): struck, wrong direction (6.54 vs 6.74 — further from the 10.89 human anchor). **The trigger's aim gate censors the texture**: bigger overshoot doesn't make sloppier shots, it makes shots the clear-shot/lead checks veto, so only well-settled aim reaches fire time. Amplitude cannot move a fire-time scalar — closing the aim gap needs a looser fire gate, a different and riskier feature.
- **sg_handoff** (2026-08-09): STRUCK before trial. The new handoff eye
  measures humans handing off on 1.8% of carries and bots on 1.6% **with
  the feature dark** — the bots already sit at the human baseline, so
  arming it could only push them past it. Eye retained as a diagnostic
  (separability 0.486, chance-level, not Stage-A gradeable at n=8 human
  games).
- **escortdose** (2026-08-09): null on its bar (escort_fraction 0.485 vs 0.477, 17 waves/arm) — the metric is teammate PROXIMITY within 400u of the carrier, and teammates are near the carrier whatever their role; a role gate cannot move it. **Unpredicted side effect worth its own trial: caps 86 vs 54, steals 218 vs 148** on the dose arm — assigned escorts may be wasted bodies. Cvar left dark at 100.
- **nobacktrack 150** (2026-08-08): struck — revisit-2 flat (0.284 vs 0.273), caps 8 vs 18. The ping-pong is field-plateau oscillation between legs, not priced immediate returns.
- **edgeride** (2026-08-08): struck by its own kill switch — falls 3 vs 1, off-graph flat. Owner ruling executed; the edge-strip tell is an accepted residual and never-falling is the accepted signature.
- **fire discipline** (2026-08-08): struck wrong-direction — gating shots on planted footing SYNCHRONIZED fire with stability windows, cadence got more regular (0.121 vs 0.162). Caps rose 16v12; the bar is the bar.
- **shelf / sg_shelfcost** (2026-08-07): five cuts to a working mechanism (pit entries 89→23), STRUCK on outcome — steals 4.5 vs 5.0, close approaches −19%. Lesson: a 91%-fatal route that buys approach tempo is not a defect. Code retained, cvar dark.
- **wswitch** (2026-08-07): moved the commitment tell the wrong way (0.676 armed vs 0.696 ctrl vs 0.897 human), caps 35 vs 40. Named the rung-3 gap its successor (wcommit) now targets.
- **smap05 rune regeneration** (2026-08-07): struck — graph census shows zero dead-end seeds post-wading-fix; 14 isolated orphans (1%) cannot trap.
- **84-wave breather cost flag** (2026-08-07): dead — its control arm also ran ribbon/jitter 0; three variables, not one. Superseded by the clean ladder.
- **freeride v1+v2 family** (2026-08-08): closed — even doubled ride volume is 0.3% of player time vs the 3–18% human band; off-graph is locomotion identity. Rope-primary travel is the named rung-2 feature, unbuilt.
- **tapvar, final** (2026-08-08): struck at every dose once actually executing — the razor-cadence tell is FIRE DISCIPLINE (hold while repositioning), design owed, not jitter.
- **rope family, complete** (2026-08-08): seven mechanisms flat; forensics showed why — route/action choices stay on the seed cloud by construction; the metric is a boundary strip and the gap is lateral FOOT PLACEMENT. The lever was the adopted ribbon all along.
- **ropetravel volume theory** (2026-08-08): voided by the wave reports — both arms already land ~600 ropes/game; proven-link flight is on-graph by construction. Human off-graph is idiosyncratic arcs; wander throw (dose 2) tests that.
- **tapvar v1 'nulls'** (2026-08-08): VOID — the feature never executed (weaponstate invisible at think cadence); v2 (ammo decrement) in trial. Provenance rule applied: verdicts on an inert feature judge nothing.
- **route dither** (2026-08-08): struck — aggregate entropy proxy moved wrong-direction; cell-level max-transition-mass eye required for retry.
- **freeride** (2026-08-07): STRUCK as armed — 2.2× rope volume, off-graph flat 0.024 vs 0.027 (speedhook arcs hug the corridor node cloud; human mass is HIGH arcs through open space). Steep-anchor geometry is the ledgered next hypothesis. Caps dipped 10 vs 18.
- **wcommit mode 2** (2026-08-07): STRUCK — caps 7 vs 22, diagonal away (0.758 vs 0.813); refusing blaster commitment sends spawns into the switch ritual at first contact. Mode 1 stays; the accuracy half of the blaster tell rides aimtexture.
- **tapvar dose 1** (2026-08-07): null on its bar (cv 0.421 vs 0.413) — aggregate cv may dilute a slow-weapon effect; fightsheet owes a cadence-spread scalar before re-trial.
- **ropecost** (2026-08-07): flood rope-price null at 400 AND 100 (off-graph 0.026 flat, 6 waves each); mediator probes sealed it — rides 25@1000 vs 13@100, zero price elasticity. Bots rope as point-to-point links; humans rope as locomotion. The fix is structural (see Open Questions), not a price.
- megaworth (17% vs 24%), linklatch, atkobj-150: nulls with film.
- exit-asym: parked on Rule 21 (31% cap cost for a cosmetic).

## Protocol lessons ledgered

- **THE s03/s04 PAIR HAS A REAL CONVERSION ARM BIAS (A/A null test,
  2026-08-11).** Identical configs, nothing armed, 15 waves/arm:
  conversion 0.104 (s03) vs 0.180 (s04). Cause unknown. The FILM
  scalar is clean on the same waves (escort_obs 0.606 vs 0.593, ~0.2
  sem), so every film-based strike stands — but raw caps comparisons on
  this pair are invalid; future caps reads use the A/A delta as
  baseline or arm-swap designs. No past verdict flips: each strike had
  independent film evidence.
- **CO-TRAVEL RULED AN ACCEPTED RESIDUAL (2026-08-11, delegated
  authority).** Five mechanisms (role gate, support pull, spatial
  surcharge, de-pace, routejitter-25 — the last two backfired), full
  forensic decomposition (86–89% mid-field co-travel), and a clean
  film instrument. The convoy is the team-level expression of optimal
  routing at uniform speed; separating it costs more than it buys.
  Same evidentiary shape as rung-2's edge strip. Rung-4 sets are
  judged with it standing.
- **The revisit spike is three map spots, not a field property
  (plateau diagnosis, 2026-08-11).** Pairs (22,27)/(5,10)/(8,12) on
  mactf06 carry 29% of all A→B→A events at 8–45× the human rate, all
  HOOK-heavy — grapple-decision oscillation. Never judge-cited; the
  residual mass away from those spots runs only ~2.3× human. Targeted
  fix (sg_hookpong) in trial.

- **CONVERSION BARS NEED 16+ WAVES PER ARM (2026-08-10).** The
  breather dose-2 probe exposed this: its control arm ran the unchanged
  adopted dose 4 and produced conversion 0.224 in one 8-wave block and
  0.118 in the next — same config, adjacent waves, ~2x apart. Control
  drift that size swamps every effect measured at n=8. **Film scalars
  are fine at 8 waves** (per-demo n, tight sem, and dose 4 reproduced
  at 0.601–0.644 across the same two blocks). **Caps and conversion are
  not** — they are ratios of single-digit counts. Escortdose survived
  scrutiny precisely because it was confirmed at 17 waves per arm.
  Retroactive consequence: the breather ablation's ESCORT half stands
  (0.519 vs 0.644, replicated); its CONVERSION half (0.149 vs 0.224) is
  no longer load-bearing, so the Rule-21 fork rests on breather's
  ORIGINAL 19-wave ladder rather than on that 8-wave read.
- **Breather causes part of the rung-4 escort tell, and Rule 21 protects
  it (2026-08-10).** Ablation, 8 waves/arm, measured as a between-arm
  delta at fixed parity per the eye's validated use: escort_fraction_obs
  0.519 at dose 0 vs 0.644 at dose 4 — the drift forensics were right.
  But the same ablation replicated breather's original cap win
  (conversion 0.149 vs 0.224), so dose 0 is refused on the fork declared
  before the numbers were seen. Dose 2 is in trial to find whether the
  conversion survives with less of the escort; if not, this share of the
  tell is accepted as residual, exactly like rung-2's edge strip.

- **THE ESCORT EYE IS COMPROMISED (2026-08-10).** Rung 4's sole
  "validated" scalar fails the goal's own standard — an eye that cannot
  rank the knowns judges nothing. Three findings:
  1. **Coverage asymmetry.** Human carry windows are ~58% observed
     (median, mactf06) against bots' 91% under pov-parity. Unobserved
     frames are scored as *unescorted*, so the human 0.02–0.32 "lone
     wolf" band is partly demos that never saw the teammate. On lmctf22,
     37% of captured human escort events are the *recording player's own
     presence* — a self-selection the bot side cannot replicate, since
     its recorder is chosen to maximize coverage.
  2. **Parity itself inflates the bot number.** Applying pov-parity to
     bot mactf06 film RAISES escort_fraction 0.556 → 0.643, because the
     filter preferentially deletes the carrier's SOLO stretches (nobody
     near = the segment drops out). ~15% relative inflation from the
     mechanism alone. Direction reverses on lmctf22, so it is not a
     correctable constant. The earlier Stage-A work tested parity
     *radius* stability and passed; it never tested parity on/off.
  3. **Real drift, from our own adoptions.** Bot escort_fraction on
     mactf06 went 0.422 (waves 498–535) → 0.577 (waves 740+), +37%.
     Prime suspects are ADOPTED features: breather 4 (a paused carrier
     is easy to linger beside — mean single-mate streak 3.85s → 6.52s)
     and escape priors (carrier routing now crosses more team traffic).
     Not a controlled ablation; flagged for one.
  A genuine behavioral component survives all of this — bot single-mate
  streaks run 3–10x longer than human ones, which chokepoint geometry
  cannot produce — so the tell is not imaginary.
  **RESOLVED same day:** escort_fraction_obs (unobserved frames excluded
  from numerator AND denominator) raised the human number as predicted —
  and raised the pov-parity bot number as much or more, because parity
  was deleting the bot's solo stretches too. Gap flat on mactf06, wider
  on lmctf22, separability UP on both (0.903→0.944, 0.961→0.969). **The
  tell is real, set #1's verdict stands, and both nulls were honest.**
  The demotion is lifted; full VALIDATED status waits on the same
  stability battery the old scalar passed (radius sweep + leave-one-out),
  which the new one has not yet run.
  **Battery run 2026-08-10: separability VALIDATED (0.918–0.986 across
  all radii and leave-one-outs, both maps) but the design rationale
  FAILED** — escort_fraction_obs is *more* parity-sensitive than the old
  scalar (+0.122 vs +0.050 mactf06; +0.129 vs +0.002 lmctf22). It
  separates despite the coverage knob, not because it escaped it. So the
  eye is sound for RANK ORDER and between-arm comparison at fixed
  parity, and unsound as an absolute magnitude — no rung-4 verdict may
  cite "distance to the human band" as evidence.

- **The coordination baseline is LOW, and our teamplay features overshoot
  it (2026-08-09).** Two independent measurements now say the same thing:
  pub humans escort carriers on 0.02–0.32 of carry time (bots: 0.33–0.75)
  and hand off on 1.8% of carries (bots already 1.6% with the feature
  off). Every remaining "better teamwork" feature must be checked against
  the human rate BEFORE it is built, not after — the corpus is a pub, and
  out-organizing a pub is a tell, not an improvement.

- **The hit-triangle artifact (2026-08-09).** fightsheet's "hits" counted
  every TE_BLOOD, which the game emits for lava/slime (no cooldown — one
  per 0.1s tick), rail pierce, rocket splash and grapple contact as well
  as weapon impacts: 47% of blood events in a sampled bot demo had no
  preceding shot. No Stage-A scalar reads `hits` (verified in
  `_compute_scalars`), so **no verdict is contaminated** — but the
  markers judges quoted as "machine-precision aim" were partly this
  artifact. **FIXED 2026-08-09** (attribution to a preceding shot,
  capped per shot+victim, unattributed kept and printed). The apparent
  7x bot accuracy advantage was mostly the artifact: bot demos ran
  83–91% unattributed blood vs humans' 43–58%. Honest gap is ~2x
  (0.58 vs 0.29 hits/shot) — real, but a quarter the size the markers
  implied. Scalars bit-identical, so no verdict moved.
- **smap05 orphan seeds: CLOSED (2026-08-09).** Verified in film across
  5 demos / ~50 minutes of bot play: orphans do become the geometrically
  nearest seed occasionally (0.07–0.18% of samples) but the longest
  continuous dwell is 0.6s and every instance recovers. Harmless as the
  graph argument claimed, now confirmed from film.

- s04 ran ribbon/jitter 0 until 2026-08-07 — every film-pair A/B before then was multi-variable. s09 is the ONLY control now.
- lmctf22 is a weak rung-2 judging map: it suppresses off-graph flight for both populations; its human sheet drew a unanimous conviction-4 bot miscall. **Codified in tools/set-composition.md** — a map qualifies per rung, by that rung's own Stage-A separability on that map.
- Pooled census only — incremental reads are sampling luck (bimodal canary lesson, waves 424–429).
- Sheets show map/hash/carries ONLY (duration and player-count leaks burned judge sets #3–#4).
- /tmp dies on reboot: instruments and law live in the repo or project memory, never only in scratchpad (2026-08-07 reboot took the steal-genesis scripts).

## Not fully polished

1. **Rung 4** — the live rung. Tell #1 (over-escort) needs a PROXIMITY-side cut, not a role gate: teammate routing that keeps non-escorts off the carrier's lane. Tell #2 (team-mirror symmetry) is unaddressed and may need per-team persona spread.
2. **Rung 2 second tell** — interval-2 revisit spike; pricing doses struck, needs plateau tie-break diagnosis (an eye first).
3. **Rung 5** — instrument valid on mactf06 only; protocol undesigned.
4. **Escort-as-cap-lever** — the escortdose side effect (caps 86v54) deserves a confirming trial on its own bar.
5. teamsheet panels 1/3/4 — coverage/sample-sensitive at n=4 human; corpus growth is the path (18-map manifest + s10/lmctf57 farm).
6. Airstrafe chain tuning: never trialed.
7. Dark features still unjudged: tilt, clockplay, spawnbeat, belief cone/range, railrhythm — each blocked on an instrument eye (TRIALS.md has the queue and the eyes each needs). **handoff struck 2026-08-09** (bots already at the human rate). sessiondb is a config flip, not a trial.

## Not implemented
3. Stage 2 (beat human numbers while still passing): steal initiation 0.26/min vs human 1.3/min is the measured gap.

## Owner questions — ALL RESOLVED

All three standing questions were returned to me on 2026-08-09 with the
decisions delegated. Rulings, with the reasoning that produced them:

1. **Rope locomotion (rung-2 off-graph).** RESOLVED 2026-08-08 — built,
   trialed at two doses, closed with the whole rope family; forensics
   reassigned the tell to foot placement.
2. **The edge strip.** RESOLVED 2026-08-08 — one edgeride trial, struck
   by its own fall kill switch (falls 3v1). Tell accepted as residual;
   never falling is the accepted bot signature on route sheets.
3. **Rung-5 ladder order.** RESOLVED 2026-08-09 — no exception needed,
   because it was never an inversion. Rung 5's only validated eye IS the
   steal-volume gap, so its blocker and stage-2's first number are the
   same work. The rung-5 line stays open, judging happens after the
   volume work, and no fresh judges get spent on a 0.964-separability
   near-certain fail. Nothing is blocked in the meantime.

## Standing policy: rung-1 regression (ruled 2026-08-09)

A passed rung is not re-judged on a calendar — that would burn fresh
judges, which are the scarcest instrument here, for no information.
Instead: **any ADOPTED change touching the raw-movement layer (pmove,
steering, ribbon/jitter, gait/rope) fires a rung-1 re-judge before the
next higher-rung set runs.** Struck features fire nothing.

Current debt: **zero.** Every movement-layer feature attempted since the
rung-1 pass — freeride, ropetravel, edgeride, route dither, ribbon-112 —
was struck. Nothing underneath rung 1 has moved.

## Canaries

- s02 (5v0 smap05): no-opposition film must stay flawless.
- s01 (2v2 lmctf03): fixed matchup must hold its band.
- s09 (ctrl lmctf01): frozen legacy config, the only control arm.
