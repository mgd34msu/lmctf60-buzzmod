# SLIPGATE LEDGER

The page the goal demands: ladder, trials, morgue, polish state — two
minutes, cold. Updated at every verdict and every arm. Times are local;
waves are the fleet clock (~16 min each, 10 servers, never stops).

*Last updated: 2026-08-07 ~15:25, wave ~537. Concurrent pairs: s03/s04 tapvar, s06/s07 mode-2; probes + 2 agents in flight.*

## The ladder

| Rung | State | Evidence |
|---|---|---|
| 1. Raw movement | **PASSED** | Set #5: bot sheets passed 7/9, judges 3/18 overall |
| 2. Routes | **IN TRIAL** | Set #1 failed 11/18; tell = off-graph fraction; sg_ropecost armed |
| 3. Fights | set #1 FAILED 18/18; mode-2 fix armed | tell #1 unanimous: spawn-blaster commitment with machine accuracy; ranked behind: metronomic cadence, pegged ranges, empty arsenal rows |
| 4. Team decisions | instrument part-calibrated | Stage A: escort_fraction validated (0.917, radius-stable); panels 1/3/4 diagnostic-only (pov-parity inflation + n=4 human arm) |
| 5. Match outcomes | instrument part-calibrated | Stage A: steals_total 0.964 on mactf06 (regime-dependent by design); lmctf22 gate FAILS |
| 6. Live vs owner | **STRUCK** | Rule 22: the recorded corpus is the complete bar |

## In trial now

| Trial | Arms | Armed | Bars (pre-registered) | Verdict due |
|---|---|---|---|---|
| wcommit mode 2 (no blaster commitment) | s06 vs s07 mode-1 ctrl | r567, wave ~536 | blaster share collapses; switch_diagonal holds ≥0.83; caps hold | ~8 waves/arm |
| sg_tapvar (slow-weapon re-aim beat) | s03 vs s04 ctrl | r569, wave ~538 | intershot_cv 0.23 → toward human 0.58; rangesep/caps hold | ~8 waves/arm |

## Adopted (film + data on record, most recent first)

- **wcommit** (2026-08-07): keep the held gun anywhere in the band ladder — switch_diagonal 0.691→0.828 (human 0.897), caps 25 vs 13, zero arm overlap, 12 waves/arm.
- **breather 4** (2026-08-07): dose ladder 0/4/8, 19 waves/arm — conversion 0.046/0.114/0.116; smaller equal dose shipped.
- atkobj 125: +7% steals, caps flat; 150 was null.
- Movement stack that passed rung 1: jitter 8, ribbon 48, tactics, nobacktrack 60.
- Pit chain (termbrake, terminal straight-walk, sink ban + widening, air graph): 80% conversion, 0 drownings census.
- Comm stack (Rule 19): itemcomm, radio wavs + human lag, itemlead, quad 60/30 either-or one-voice, mega taker-clock, honest ear, hit sense.
- Corpus chat (134 lines, 5 reaction categories), unified human/bot say_team parsing.
- Native clean-room platform: sg_net client layer, ping 5–15ms, personas, stats flow-through; all legacy bot code removed.

## The morgue (honest nulls and strikes, with their lessons)

- **shelf / sg_shelfcost** (2026-08-07): five cuts to a working mechanism (pit entries 89→23), STRUCK on outcome — steals 4.5 vs 5.0, close approaches −19%. Lesson: a 91%-fatal route that buys approach tempo is not a defect. Code retained, cvar dark.
- **wswitch** (2026-08-07): moved the commitment tell the wrong way (0.676 armed vs 0.696 ctrl vs 0.897 human), caps 35 vs 40. Named the rung-3 gap its successor (wcommit) now targets.
- **smap05 rune regeneration** (2026-08-07): struck — graph census shows zero dead-end seeds post-wading-fix; 14 isolated orphans (1%) cannot trap.
- **84-wave breather cost flag** (2026-08-07): dead — its control arm also ran ribbon/jitter 0; three variables, not one. Superseded by the clean ladder.
- **ropecost** (2026-08-07): flood rope-price null at 400 AND 100 (off-graph 0.026 flat, 6 waves each) — the flood layer is exonerated; the off-graph gap's binding constraint is elsewhere. Mediator probes running.
- megaworth (17% vs 24%), linklatch, atkobj-150: nulls with film.
- exit-asym: parked on Rule 21 (31% cap cost for a cosmetic).

## Protocol lessons ledgered

- s04 ran ribbon/jitter 0 until 2026-08-07 — every film-pair A/B before then was multi-variable. s09 is the ONLY control now.
- lmctf22 is a weak rung-2 judging map: it suppresses off-graph flight for both populations; its human sheet drew a unanimous conviction-4 bot miscall.
- Pooled census only — incremental reads are sampling luck (bimodal canary lesson, waves 424–429).
- Sheets show map/hash/carries ONLY (duration and player-count leaks burned judge sets #3–#4).
- /tmp dies on reboot: instruments and law live in the repo or project memory, never only in scratchpad (2026-08-07 reboot took the steal-genesis scripts).

## Not fully polished

1. Rung 2: ropecost in trial; secondary tell queued (deterministic p=1.0 transition cells).
2. Rung 3: wcommit adopted; residual commitment gap 0.83→0.90; blind set #1 next.
3. teamsheet.py: panels 1/3/4 unvalidated -- parity-radius calibration + bigger human corpus owed (task #7).
4. Airstrafe chain-length tuning: never trialed.
5. Dark features built, never armed: aimtexture, tilt, clockplay, spawnbeat, belief cone/range, handoff, session-db, railrhythm. Enter only when a rung names their gap.

## Not implemented

1. Rung-2 set-composition rule (map discriminator weighting).

## Canaries

- s02 (5v0 smap05): no-opposition film must stay flawless.
- s01 (2v2 lmctf03): fixed matchup must hold its band.
- s09 (ctrl lmctf01): frozen legacy config, the only control arm.
