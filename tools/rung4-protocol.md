# Rung 4 (TEAM DECISIONS) blind-judging protocol

Status: pre-registered, not yet run. Dry run complete (one bot sheet, one
human sheet). No judge set has been shown to anyone under this protocol.

Sources read before writing this: `LEDGER.md` (ladder standards and the
rung-4 line), `tools/teamsheet.py` in full including its two Stage-A
MODULE NOTE blocks at the bottom of the file, `tools/corpus-manifest.csv`,
and — for the judge-prompt and leak-checklist precedent this protocol
mirrors — the equivalent sections of `tools/fightsheet.py` (rung 3) and
`tools/routesheet.py` (rung 2).

---

## 1. What the instrument actually supports

`teamsheet.py` renders four panels per demo: (1) teammate spacing,
(2) escort presence over carry windows + a per-team mean bar, (3) defense
posture with steal/capture ticks, (4) push synchronization. Two Stage-A
runs are recorded in the module's own trailing comments (2026-08-07, plus
a same-day addendum closing out the first run's open questions). The
addendum is the authoritative one — it is the only pass that combined a
5-point pov-parity-radius sweep (700/800/900/1000/1100u) with a 4-way
leave-one-out pass over the human corpus, and it produced a verdict table
(teamsheet.py:1357-1370):

| map | scalar | min sep | max sep | verdict |
|---|---|---|---|---|
| lmctf22 | **escort_fraction** | 0.933 | 0.962 | **VALIDATED** |
| lmctf22 | spacing_median | 0.786 | 1.000 | coverage-sensitive |
| lmctf22 | defense_fraction | 0.524 | 0.752 | sub-gate |
| lmctf22 | mean_simultaneous_attackers | 0.505 | 0.771 | sub-gate |
| mactf06 | spacing_median | 0.539 | 1.000 | coverage-sensitive |
| mactf06 | escort_fraction | 0.652 | 0.743 | sub-gate |
| mactf06 | defense_fraction | 0.725 | 0.995 | coverage-sensitive |
| mactf06 | mean_simultaneous_attackers | 0.793 | 1.000 | coverage-sensitive |

`escort_fraction` on lmctf22 is the **only** cell in either map's table
that clears 0.85 at every radius and every leave-one-out exclusion. (The
single-point figure recorded in `LEDGER.md`'s rung-4 line, 0.917, is the
first run's number, before the radius sweep and leave-one-out pass
existed; the addendum's combined range, 0.933-0.962, supersedes it and is
what this protocol relies on. Both numbers describe the same scalar and
point the same direction.) Every other scalar on lmctf22 — spacing_median,
defense_fraction, mean_simultaneous_attackers — is either coverage-
sensitive (moves with the untuned pov-parity radius, teamsheet.py note
(a)) or never gates at all. Panel 3 (defense) is sub-gate outright on
lmctf22 and stays diagnostic-only "everywhere," per the module's own
closing verdict (teamsheet.py:1388-1390).

**Consequence for this protocol: escort_fraction (panel 2) is the judging
centerpiece.** Panels 1, 3 and 4 render on every sheet — pulling them
would itself be a population-conditional difference between a real sheet
set and this dry run, and the fairness rule in teamsheet.py's own header
treats "some demos have a panel and some don't" as exactly the kind of
asymmetry the leak checklist exists to catch — but no judge briefing or
pass-standard interpretation may treat panels 1/3/4 as evidence on their
own. See section 3.

---

## 2. The panel-comparability problem (design constraint 1)

The risk named in the brief: bot sheets get `--pov-parity` (mandatory,
teamsheet.py's own CLI help: "MANDATORY on every serverrecord sheet in a
judge set (L5)"), human sheets never do (pov-parity is a structural no-op
on client demos — `apply_pov_parity` only fires when `d['svrecord']` is
true, confirmed both by the module note (b)/(2) and by this protocol's own
dry run, where the human sidecar reads `pov_parity: {'applied': False}`).
If bot sheets are *also* the only sheets built from a uniform 5v5 roster
(every bot fixture in this corpus is 5v5 by construction) while human
sheets vary in tracked player count, "full 10-slot roster + smooth
panels" is a learnable tell that has nothing to do with team play.

Two independent findings settle how to handle it:

**(a) The panels themselves do not render player count.** Panel 1 draws
one median+IQR band per *team*, not one line per player; panel 2's rows
are a duration-bucketed padded count, not a carry-per-player count; panel
4's y-axis ceiling (`PUSH_YMAX`) is a fixed instrument constant. Confirmed
by eye in the dry run (section 5) — neither sheet exposes a player-count
number or a visibly different line/row density traceable to roster size.
So panel *rendering* is not the leak surface.

**(b) The roster-size confound is real in the scalars themselves, and it
has a name and a number.** teamsheet.py's leave-one-out table
(teamsheet.py:1317-1328) identifies lmctf22's n=6 human demo
(`lmctf-2021-11-14-lmctf22-20.32.dm2`, effectively 3v3) as a genuine
outlier: spacing_median=1488.0u against 605-798u for the other three human
demos and 526-619u for bots across the radius sweep; escort_fraction=0.004
against 0.147-0.237 for the other three. On `mean_simultaneous_attackers`
it is the single biggest leverage point in the whole leave-one-out table
(dropping it moves separability +0.166, teamsheet.py:1330-1336) — its own
mean_simultaneous_attackers (0.634) sits almost on top of the bot arm's
900u mean (0.891), i.e. a 3v3 roster produces a bot-like attacker count
purely from having fewer players on the map, not from being less human.

**Decision: exclude the 3v3 demo. Do not attempt to "match" it (e.g. by
building a 3v3 bot fixture) — this corpus has no 3v3 bot film, and
building one only for a single judging pass is a different, larger project
than this rung needs.** Justification: every bot fixture in the available
farm (see section 4) is 5v5. Excluding the one human demo that is not
roughly 5v5 leaves the remaining three human lmctf22 demos at 10, 11 and 9
tracked players — none identical to the bots' 10, but all in the same
regime, and critically none of the panels expose the exact count (finding
(a) above), so the residual mismatch is a scalar-level statistical
question, not a visible-on-the-sheet one. This also happens to leave
**exactly three** demos for a 3-human-arm set — see section 4.

---

## 3. Judge briefing (mirrors rung-2/routesheet.py and rung-3/fightsheet.py)

Every sheet carries teamsheet.py's own `NOTES_TEXT` strip verbatim
(identical on every sheet, human or bot — required by the same leak
checklist item, L2, that voided part of judge set #3 on rung 3 when a
note's mere *presence* discriminated). One line from it, quoted per the
brief's "quote one":

> "a frame with no sampled player near a threshold reads as 'not present'
> on both demo shapes alike -- a client demo's PVS holes and a
> serverrecord's pov-parity holes are treated identically (L6)."

That sentence is teamsheet.py's version of the mandated judge-preamble
sentence rung 3 introduced (fightsheet.py:1612-1614: "this sheet carries
derived labels, and a judge who does not know that can convict on an
artifact of the attribution heuristic"). Both exist for the same reason:
a judge who does not know a panel's blank spots are a real, symmetric
coverage mechanic — not an absence of behavior — will convict on the
mechanic instead of on team play.

**Additional spoken/written preamble for this rung specifically** (not on
the sheet itself, delivered to judges before they see any sheet, exactly
as the escort/defense-radius caveat in section 1 requires):

> "Panel 2 (escort presence) is the panel this instrument trusts. Panels 1
> (spacing), 3 (defense) and 4 (push synchronization) render on every
> sheet but have not been validated as behavioral signal — treat them as
> supporting context, not as a basis for your verdict on their own."

**Per-sheet task, forced choice + conviction** (same shape as rung-2/3's
judge form):

1. "Is this team bot or human?" (forced choice, no abstain)
2. "Conviction: 1 (guess) - 4 (certain)"
3. Free-text: what on the sheet drove the call (required even on a guess)

---

## 4. Map, corpus and set composition

**Map: lmctf22** — the validated map (section 1); mactf06 is not used
because none of its four scalars reach VALIDATED status in the addendum's
combined table.

**Human arm — the 4 usable lmctf22 demos per `tools/corpus-manifest.csv`,
minus the 3v3 exclusion (section 2):**

| file | duration_s | players | usable | in set? |
|---|---|---|---|---|
| lmctf-2021-10-29-lmctf22-20.30.dm2 | 105.2 | 5 | False | no (unusable) |
| lmctf-2021-11-14-**lmctf22-20.32**.dm2 | 407.9 | 6 | True | **excluded — 3v3 roster outlier** |
| lmctf-2021-11-14-lmctf22-21.01.dm2 | 919.6 | 10 | True | **yes** |
| lmctf-2021-11-20-lmctf22-14.39.dm2 | 114.4 | 1 | False | no (unusable) |
| lmctf-2021-11-22-lmctf22-20.47.dm2 | 3.3 | 0 | False | no (unusable) |
| lmctf-2021-11-22-lmctf22-21.43.dm2 | 118.1 | 10 | False | no (unusable) |
| lmctf-2021-11-22-lmctf22-21.45.dm2 | 1032.8 | 11 | True | **yes** |
| lmctf-2022-01-13-lmctf22-21.38.dm2 | 61.9 | 9 | False | no (unusable) |
| lmctf-2022-03-21-lmctf22-20.32.dm2 | 59.4 | 7 | False | no (unusable) |
| lmctf-2022-04-18-lmctf22-21.00.dm2 | 10.7 | 1 | False | no (unusable) |
| lmctf-2022-06-13-lmctf22-20.49.dm2 | 993.2 | 9 | True | **yes** |

Final human arm (3 files, all in `~/Games/Quake2/lmctf-hooktest/demos/`):
`lmctf-2021-11-14-lmctf22-21.01.dm2`, `lmctf-2021-11-22-lmctf22-21.45.dm2`,
`lmctf-2022-06-13-lmctf22-20.49.dm2`.

**Bot arm — s05/s08 farm, waves 620+, the steady adopted config.** Verified
live in this session (`--scalars` over `wave6[2-4][0-9]-s0[58]-5v5.dm2`,
`--pov-parity --stands tools/stands.json`): all 50 candidate files
(waves 620-644, both servers) map to lmctf22 with no skips or failures.
Three-file selection rule, chosen for spread rather than hand-picking:
sort the 50 candidates chronologically (wave, then server), take positions
1, 26 and 50 of 50 — first wave/first server, middle wave/second server,
last wave/second server, so no two files are adjacent waves and both
servers are represented:

Final bot arm (3 files): `wave620-s05-5v5.dm2`, `wave632-s08-5v5.dm2`,
`wave644-s08-5v5.dm2`.

**Set composition:** 3 bot / 3 human, all lmctf22, all bot sheets rendered
with `--pov-parity` (mandatory, L5) and `--stands tools/stands.json`
(lmctf22 stand coordinates are in that file already — no `StandsMissing`
risk). Captions: `map=lmctf22 hash=<12-hex> carries=<N>` — the exact set
teamsheet.py's own fairness rule allows, nothing else. Sheets are handed
to three fresh judges (no judge who has seen this protocol document or any
prior teamsheet.py output).

---

## 5. Pre-registration

**Pass standard.** Same standard as every other rung on this ladder
(LEDGER.md's ladder-standards line): the rung passes when three fresh
judges' pooled forced-choice score sits at or below the rung-1 precedent
of ~3/18 — i.e. no better than chance (9/18 expected at 50/50 on 3 judges
x 6 sheets = 18 total calls) and, per the standing precedent, meaningfully
*worse* than chance, the same bar rung 3 cleared at 5/18. This is
pre-registered before any judge sees a sheet; it is not adjusted after
seeing scores.

**Tell-ranking procedure on failure** (pooled score above the bar,
disguise not holding): mirror rung 3's post-mortem method.
1. For every sheet, split judges' free-text "what drove the call" answers
   by verdict (correct bot ID / correct human ID / wrong). A tell is any
   phrase that recurs across >=2 of the 3 judges on the *same* sheet
   shape (bot or human) and correlates with a correct call.
2. Cross-reference each recurring tell against panels 1/3/4 first, since
   those are the coverage-sensitive panels flagged in section 1 — a tell
   traceable to spacing_median, defense_fraction or
   mean_simultaneous_attackers is read as an instrument artifact
   (untuned pov-parity radius or the roster residual from section 2), not
   a behavioral finding, and gets logged as a design debt against those
   scalars rather than against the bots' team play.
3. A tell traceable to panel 2 (escort) is read as real signal — it is
   the validated panel — and gets logged as an actionable rung-4 gap, the
   same way rung 2's revisit-2 spike or rung 3's hit-triangle-accuracy
   residual were logged.
4. Per rung 3's precedent ("two of three judges fully inverted" was
   itself the finding, not noise) check for judge-level inversion
   (a judge scoring *worse* than chance individually) before pooling —
   an inverted judge is evidence the bots read as more human than the
   humans on some dimension, and that dimension is worth naming even if
   the pooled score alone would already pass.

**Roster-size confound handling — decided, not deferred:** exclude
`lmctf-2021-11-14-lmctf22-20.32.dm2` (the 3v3 demo) from the human arm.
Justification is in section 2: it is a genuine outlier on two scalars
(spacing_median, escort_fraction) and the single biggest leverage point
in the whole leave-one-out table on a third (mean_simultaneous_attackers),
for a structural reason (fewer players on the map) that has nothing to do
with humanness, and the bot corpus has no matching 3v3 fixture to pair it
against. Excluding it rather than matching it is the cheaper, honest
option: matching would require producing a 3v3 bot film run this rung
does not otherwise need, purely to keep one demo in a judge set whose loss
does not otherwise threaten set size (removing it still leaves exactly
three qualifying human lmctf22 demos, section 4).

---

## 6. Dry run

Environment: `~/.venvs/slipgate-film/bin/python3` (this repo's durable
film venv), `timeout 570` wrapper on every invocation, `--stands
tools/stands.json` (has lmctf22's stand coordinates already).

**Bot sheet** (`wave620-s05-5v5.dm2`, one of the three files selected for
the real set in section 4):

```
timeout 570 ~/.venvs/slipgate-film/bin/python3 tools/teamsheet.py \
  ~/.local/share/YamagiQ2/lmctf-hooktest/demos/wave620-s05-5v5.dm2 \
  --out <dir> --pov-parity --stands tools/stands.json
```
`OK wave620-s05-5v5.dm2 -> 5815c652a90e.png  map=lmctf22 bot players=10
carries=10 vis=0.294 spacing=561.699 escort=0.406 defense=0.187
attackers=1.027`. `pov_parity.applied=True`, `sample_keep_fraction=0.247`.

**Human sheet** (`lmctf-2021-11-14-lmctf22-21.01.dm2`, one of the three
files selected for the real set in section 4):

```
timeout 570 ~/.venvs/slipgate-film/bin/python3 tools/teamsheet.py \
  ~/Games/Quake2/lmctf-hooktest/demos/lmctf-2021-11-14-lmctf22-21.01.dm2 \
  --out <dir> --stands tools/stands.json
```
`OK lmctf-2021-11-14-lmctf22-21.01.dm2 -> c6c7ce170a78.png  map=lmctf22
human players=10 carries=14 vis=0.270 spacing=604.763 escort=0.237
defense=0.197 attackers=0.953`. `pov_parity.applied=False` ("not a
serverrecord demo" — confirms note (2)'s described control flow, this is
not a bug).

**Caption check:** both PNGs read `map=lmctf22   hash=<12-hex>
carries=<N>` and nothing else — no duration, no player count, no
filename, no parity marker, matching the fairness rule exactly.

**Panel comparability, by eye:**
- Panel 2 (escort, the validated panel) renders identically in structure
  on both sheets: padded carry-window rows, colour-scaled by escort
  fraction, companion mean bar below. No visible asymmetry.
- Panel 1 (spacing) is legible on both and neither sheet's line/band count
  betrays player count (finding (a), section 2).
- Panel 4 (push sync) axis ceiling is identical (0-6) on both, as
  expected from a fixed instrument constant.

**Instrument-leak candidates found by eye (neither is disqualifying on
its own; both are logged for the tell-ranking pass if the real set fails
— section 5, step 2):**

1. **Panel 1 coverage burstiness.** The bot sheet shows three distinct
   full-width blank intervals (roughly x=0.12-0.14, 0.60-0.63, 0.83) where
   *neither* team's spacing line is drawn at all. The human sheet has no
   equivalent full blackout anywhere in its range — its line is
   continuous (with one large single-frame spike near x=0.32, ~2750u,
   which is a different, non-structural artifact — see below). The two
   sidecars' aggregate `visible_fraction` are close (0.294 bot vs 0.270
   human), so this is not an overall-coverage gap; it looks like a
   *clustering* difference in when coverage drops to zero, plausibly
   pov-parity's radius filter producing synchronized blackouts across
   both tracked teammates at once on the bot side, something the human
   side's real PVS holes apparently do less often in this pair of demos.
   This is exactly the kind of pattern note (a) in teamsheet.py already
   named as a live risk for panel 1 (coverage-sensitive, not validated),
   and it is visually distinctive enough that a judge could plausibly
   learn "blank stripes = bot" without ever reasoning about team play.
   Mitigation already in place: the judge briefing (section 3) explicitly
   tells judges panel 1 is unvalidated supporting context, not a basis for
   a verdict.
2. **Panel 3 capture-marker asymmetry.** The human sheet shows two
   capture ticks (▼, near x=0.47 and x=0.83); the bot sheet shows none in
   this single-file comparison. Panel 3 is sub-gate on lmctf22 regardless
   (section 1), so this is not read as a validated finding, but a
   presence/absence-of-a-marker cue is a cheap visual tell if it recurs
   across the full 3-vs-3 set — worth a specific check in the tell-ranking
   pass rather than an assumption it is noise from n=1.
3. **The single large spacing spike is not a leak candidate.** The
   ~2750u single-frame spike on the human sheet's panel 1 (~x=0.32) is a
   one-off value, not a recurring pattern, and both demo shapes are
   already known to produce this kind of one-frame excursion (a
   respawn/teleport position jump). Noted so a future reader of this
   dry run does not mistake it for something structural.

**Roster-matching, confirmed workable:** the human file used here (10
tracked players) is one of the three post-exclusion demos from section 4,
deliberately not the 3v3 outlier, precisely to demonstrate the section-2
mitigation in the dry run rather than just assert it on paper.

---

## 7. Summary for the record

- Instrument: `tools/teamsheet.py`. Judging centerpiece: escort_fraction
  (panel 2), VALIDATED on lmctf22 (separability 0.933-0.962 across the
  full pov-parity-radius sweep and leave-one-out pass; LEDGER's 0.917 is
  the same scalar's earlier single-point figure, superseded but
  consistent). Panels 1/3/4 render on every sheet but are diagnostic-only
  by the module's own verdict table and must be captioned to judges as
  such.
- Leak mitigation: roster-matched human demos (exclude the one 3v3
  lmctf22 demo; bots are structurally 5v5 throughout) rather than panel
  suppression, because the panels themselves do not expose player count —
  only the underlying scalars carry the roster confound, and only on
  scalars already flagged non-validated.
- Set: 3 bot (`wave620-s05-5v5.dm2`, `wave632-s08-5v5.dm2`,
  `wave644-s08-5v5.dm2`, all confirmed lmctf22, all rendered with
  mandatory `--pov-parity`) / 3 human (`lmctf-2021-11-14-lmctf22-21.01.dm2`,
  `lmctf-2021-11-22-lmctf22-21.45.dm2`,
  `lmctf-2022-06-13-lmctf22-20.49.dm2`), sealed captions, three fresh
  judges, forced choice + 1-4 conviction + required free text.
- Pass standard: pooled score at/below ~3/18 (chance or worse), same bar
  as rung 1 and the bar rung 3 cleared at 5/18. Pre-registered
  tell-ranking procedure and roster-confound decision both on record
  above, before any judge sees a sheet.
- Dry run: both sheet types render correctly, captions clean, panels
  structurally comparable. Two leak-risk candidates logged (panel 1
  coverage-burstiness pattern, panel 3 capture-marker presence/absence) —
  neither disqualifying, both pre-flagged for the tell-ranking pass so
  they are recognized immediately if they recur in the real set rather
  than rediscovered from scratch.
