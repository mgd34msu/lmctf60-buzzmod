# SLIPGATE LEDGER

The page the goal demands: ladder, trials, morgue, polish state — two
minutes, cold. Updated at every verdict and every arm. Times are local;
waves are the fleet clock (~16 min each, 10 servers, never stops).

*Last updated: 2026-08-09 ~03:45, wave ~670. **Rungs 1 and 3 PASSED**;
rung 2 residual-capped by owner ruling; rung 4 has its first verdict and
its named tells. Fleet steady on the adopted stack, no armed variables.*

## The ladder

| Rung | State | Evidence |
|---|---|---|
| 1. Raw movement | **PASSED** | Set #5: bot sheets passed 7/9, judges 3/18 overall |
| 2. Routes | residual-capped (owner ruling); baseline 18/18 | off-graph = accepted residual (9 mechanisms + forensics + measured fall cost). Second tell open: interval-2 revisit spike 0.28 vs human 0.20 — nobacktrack dose struck; it is plateau oscillation between legs, needs tie-break diagnosis |
| 3. Fights | **PASSED** — set #3: judges 5/18 | two of three judges fully inverted; bots read human 6/9 vs humans 2/9; same standard as rung-1's 3/18 pass. Residual: a real ~2x hits/shot edge (0.58 vs 0.29) — measured after the marker artifact was fixed, so the pass was earned against inflated evidence, not because of it |
| 4. Team decisions | set #1 FAILED 16/18; first fix null | tell #1 (unanimous): bots OVER-escort — 0.33–0.75 vs human 0.02–0.32 lone-wolf runs; tell #2: team-mirror symmetry. escortdose null (proximity ≠ role); next cut must move PROXIMITY (teammate routing away from the carrier lane) |
| 5. Match outcomes | protocol written; judging deferred by design | its only eye (steals_total, 0.964) measures the steal-volume gap stage 2 exists to close — so rung-5's blocker and stage-2's first number are THE SAME WORK. Judging waits; the work does not |
| 6. Live vs owner | **STRUCK** | Rule 22: the recorded corpus is the complete bar |

## In trial now

| Trial | Arms | Armed | Bars (pre-registered) | Verdict due |
|---|---|---|---|---|
| (none — all pairs steady on the adopted stack) | | | | |

## Adopted (film + data on record, most recent first)

- **escortdose 35** (2026-08-09): assigned escorts were wasted bodies — 42 vs 22 caps and 92 vs 55 steals (8 waves/arm) confirming the 86-vs-54 side effect on its own pre-registered bar. Freeing two thirds of escorts to attack nearly doubles caps. Neutral on the rung-4 film tell.
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
