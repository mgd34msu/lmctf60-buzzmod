# Rung 5 (MATCH OUTCOMES) blind-judging protocol

Status: pre-registered, not yet run. Dry run complete (four sheets rendered:
two from the sparse/no-capture regime, two from the low-capture regime that
this map actually produces). No judge set has been shown to anyone under
this protocol. **Execution is deliberately deferred — see section 2.**

Sources read before writing this: `LEDGER.md` (ladder standards, the rung-1
and rung-3 pass bars, and the rung-5 line), `tools/set-composition.md` (the
per-rung map-qualification rule), `tools/outcomecard.py` in full including
its trailing MODULE NOTE (the Stage-A record, 2026-08-07), and — for the
judge-prompt and leak-checklist precedent this protocol mirrors —
`tools/rung4-protocol.md` (rung 4 / teamsheet.py), which is this document's
direct structural template.

---

## 1. What the instrument actually supports

`outcomecard.py` renders four panels per demo: (1) score progression —
cumulative captures per team, (2) cap-timing distribution — inter-capture
interval histogram plus first-cap markers, (3) momentum — cap differential
with lead-change markers, (4) pressure balance — cumulative steals per team
overlaid with a running conversion-ratio line. Stage A (module comment,
outcomecard.py:886-1026) ran the gate on both maps this ladder has ever
qualified a corpus on:

| map | scalar | AUC(bot>human) | separability | verdict |
|---|---|---|---|---|
| mactf06 | steals_total | 0.036 | **0.964** | **PASSES the 0.85 gate** |
| mactf06 | conversion | 0.253 | 0.747 | sub-gate |
| mactf06 | total_caps | 0.188 | 0.812 | sub-gate |
| mactf06 | caps_blue | 0.250 | 0.750 | sub-gate |
| mactf06 | caps_red | 0.432 | 0.568 | sub-gate |
| mactf06 | first_cap_t_norm | 0.524 | 0.524 | sub-gate |
| mactf06 | lead_changes | 0.500 | 0.500 | sub-gate |
| lmctf22 | steals_total | 0.172 | 0.828 | best on this map, still sub-gate |
| lmctf22 | (all other scalars) | — | ≤0.818 | sub-gate |

`steals_total` on mactf06 is the **only** cell across either map that
clears 0.85. It lives on **panel 4 (pressure balance)** — specifically the
two solid step lines (cumulative steals per team), not the dashed
conversion-ratio line that shares the same axes (`conversion` is sub-gate,
0.747). No other panel, and no other scalar on panel 4 itself, has
validated status. lmctf22 fails the gate outright (best 0.828,
`steals_total` again, same direction, short of the bar) and per
`set-composition.md`'s rule does not qualify for a rung-5 set at all.

**Consequence for this protocol: the two `steals_total` step lines on panel
4 are the judging centerpiece.** All four panels render on every sheet —
suppressing any of them would itself be a population-conditional
difference the fairness rule treats as a leak-checklist violation — but no
judge briefing or pass-standard interpretation may treat panels 1, 2, 3, or
panel 4's conversion-ratio overlay, as evidence on their own. See section 3.

---

## 2. The central problem: a gate that measures what stage 2 exists to erase

Every other rung's validated scalar (rung 2's off-graph fraction, rung 3's
hit-triangle accuracy, rung 4's escort_fraction) measures a *skill or
behavioral texture* question — can this player fight, does this team route
like a team. `steals_total`'s separability comes from a different kind of
gap: mactf06's four qualifying human demos initiate **~1.3 flag steals per
minute**; the 48-file bot corpus initiates **~0.26/min** (module comment
(c)) — bots simply go for the flag far less often, independent of what
happens after the pickup (`conversion` sits at 0.061 human vs 0.043 bot,
same order of magnitude — not the driver). The module's own closing note
names this exactly: "a lower steal-initiation tempo is a property of THIS
bot AI era's objective-seeking behaviour, and stage 2 of the project's own
goal is to change exactly that behaviour" (outcomecard.py:1001-1008).

That is not a design flaw in the instrument — Stage A did its job and
found the one real, stable signal that exists in the corpus today (stable
across the pov-parity radius sweep, 0.966→0.964→0.956, Delta=-0.010 end to
end; not stable in the cap_radius-check sense, but that check is *blind* to
`steals_total` by construction, since it's a raw carry-window count
computed before `classify_outcome` ever runs — see module note (a)). The
problem is what a rung-5 verdict would *mean*: the one channel a blind
judge could plausibly key on is the exact quantity stage 2's own
in-progress work (steal-initiation frequency) is trying to move. A pass
earned today could be un-earned by stage-2 progress tomorrow — or,
symmetrically, a fail earned today is a near-certainty *because* stage 2
hasn't landed yet, not because the bots read as inauthentic on any axis
rungs 1, 3 or 4 didn't already clear.

### Recommendation: sequence rung 5 AFTER stage 2's volume work, not now

**Do not run the judging set yet.** Reasoning:

1. **A verdict today would be dishonest ladder bookkeeping.** Every prior
   PASS on this ladder certified something durable — raw movement, fight
   skill, team routing — that doesn't have a scheduled reason to change.
   Rung 5 today would certify (or fail to certify) "does this era's AI
   rush the flag as often as humans," a gap the project already knows
   about, already measures precisely (0.26 vs 1.3/min), and is already
   working. Logging that as a ladder verdict conflates a known,
   in-progress engineering gap with an open behavioral question. It is
   the exact failure mode `set-composition.md` names in its own closing
   line about non-discriminating maps: "a number that looks like a
   verdict and is not one" — here the map discriminates fine, but the
   *reason* it discriminates is scheduled to move.

2. **Fresh judges are a consumed, non-renewable resource on this ladder.**
   `set-composition.md`'s own rule 4 ("a judge who has seen a previous set
   knows the answer key's shape") means every judge seated for a rung-5
   set can never be reseated for a rerun of rung 5. Spending that seat on
   a result that Stage A already predicts with 0.964 separability — i.e.
   a near-certain, already-explained fail — is a worse trade than holding
   the seat for the post-stage-2 run, where the outcome is genuinely
   uncertain and therefore actually diagnostic: either the steal-tempo
   fix closes the gap and a real pass becomes possible, or the gap closes
   and judges surface a *different*, previously-invisible tell (the way
   rung 3's judging surfaced the hit-triangle artifact and rung 4's
   surfaced both over-escort *and* team-mirror symmetry, neither of which
   was the scalar being graded). That second kind of finding is only
   available once the loudest, already-known signal stops drowning it
   out.

3. **This matches the project's own standing pattern of parking an
   instrument behind a named blocker rather than running it prematurely**
   — rung 2's second tell is parked behind an undesigned plateau
   tie-break eye; the edgeride question waited for a single, cheap,
   falsifiable trial before a ruling. Rung 5's blocker (stage 2's volume
   work) is not undesigned or hypothetical — it is already ledgered as
   "Not implemented #3: Stage 2 ... steal initiation 0.26/min vs human
   1.3/min is the measured gap" — so this is the same pattern applied to
   a blocker that is already scheduled, not a new one invented for this
   document.

**Consequence of this recommendation, stated plainly:** the ladder's rung-5
line stays "instrument valid on mactf06 only, protocol pre-registered,
execution deferred" for as long as stage 2 takes — an explicit, visible gap
in the ladder rather than a premature verdict. Nothing in this protocol is
wasted by waiting: every other section below (set composition, judge
prompt, pass standard, tell-ranking procedure) is fully specified and
dry-run-verified, ready to execute the moment stage 2's steal-initiation
work lands, whatever its outcome. **When it does land, re-run Stage A
first** (the calibration invocation is `outcomecard.py --calibrate`) to
confirm `steals_total`'s separability actually moved before reusing this
document's judging apparatus unchanged — if stage 2 succeeds, 0.964 should
visibly drop, and if it doesn't, that is itself a stage-2 finding, not a
rung-5 one.

### Is a single-map (mactf06-only) rung-5 set acceptable?

**Yes — and it is not a special weakness of rung 5.** Every rung this
project has judged so far is single-map by construction, per
`set-composition.md`'s own table: rung 2 discriminates only on mactf06
(lmctf22 excluded), rung 3 only on mactf06 (lmctf44 excluded), rung 4 only
on lmctf22 (mactf06 excluded), and now rung 5 only on mactf06 (lmctf22
excluded). No map has yet supported two different rungs' judging sets.

The cost is real and worth stating plainly rather than waved off: a
rung-5 verdict, whenever it runs, certifies match-outcome realism on
**mactf06 only**. It says nothing about whether captures/steals/momentum
shape holds up on lmctf22 (which fails the gate outright at 0.828) or on
any of the other 16 maps in `corpus-manifest.csv`'s blind-set-capable list
that Stage A has not yet calibrated at all. If the human corpus deepens on
another map (lmctf22 past its current n=4 usable demos, or a third map
matures), rung 5 needs a second, independent judging pass there before
"match outcomes read as human" can be claimed generally rather than
map-locally. This is the same "grow the corpus or build the eye" choice
`set-composition.md` closes on — for rung 5 specifically, growing the
human corpus on a second map is the more likely path, since the instrument
itself (event-driven capture/steal counting) needs no redesign to work
elsewhere; it just needs a second map's Stage A numbers to clear the gate.

---

## 3. Judge briefing (mirrors rung-3/fightsheet.py and rung-4/teamsheet.py)

Every sheet carries outcomecard.py's own `NOTES_TEXT` strip verbatim
(identical on every sheet, human or bot). One line from it, quoted per the
same "quote one" discipline rung 4 used:

> "panel 2's histogram and panel 3's lead-change count are drawn/reported
> even when this demo has zero or one capture; an empty histogram or a
> flat zero-lead line means nothing filled it, not that data is missing."

This matters more here than it did on rung 4: mactf06 games in this corpus
mostly have 0-2 total captures (module note (b): human range 0-2 caps
across the four qualifying demos; the dry run's own bot and human samples
below both landed at 0 or 1). Panels 1-3 will read as near-empty on most
sheets of *both* shapes — that is the corpus, not a rendering failure, and
a judge who does not know that convention will read "blank panel" as a
tell when it is symmetric noise.

**Additional spoken/written preamble for this rung specifically:**

> "Panel 4 (pressure balance) is the panel this instrument trusts — and
> only its two solid step lines (cumulative steals per team), not the
> dashed conversion-ratio line sharing the same axes. Panels 1 (score
> progression), 2 (cap timing), 3 (momentum), and the conversion-ratio
> overlay within panel 4, render on every sheet but have not been
> validated as behavioral signal on this map — treat them as supporting
> context, not as a basis for your verdict on their own."

**Per-sheet task, forced choice + conviction** (identical shape to rungs
2-4's judge form):

1. "Is this team bot or human?" (forced choice, no abstain)
2. "Conviction: 1 (guess) - 4 (certain)"
3. Free-text: what on the sheet drove the call (required even on a guess)

---

## 4. Map, corpus and set composition

**Map: mactf06** — the only map that qualifies (section 1); lmctf22 is
excluded per `set-composition.md`'s own table entry for rung 5 (best 0.828,
short of the gate).

**Roster matching: not needed on this map.** Unlike rung 4's lmctf22 3v3
outlier, module note (b) confirms "mactf06's four qualifying human demos
are all n_players=10 (checked directly against F.anonymize); this map's
human arm has no roster-size artifact." Bot fixtures are structurally 5v5
(10 tracked players) throughout. No exclusion is required for this reason.

**Human arm — the 9 mactf06 demos per `tools/corpus-manifest.csv`, filtered
by the 300s `DURATION_MIN_S` floor:**

| file | duration_s | players | usable |
|---|---|---|---|
| lmctf-2022-02-08-mactf06-20.01.dm2 | 652.4 | 10 | **yes** |
| lmctf-2022-02-08-mactf06-20.12.dm2 | 0.9 | 2 | no |
| lmctf-2022-02-08-mactf06-20.13.dm2 | 8.2 | 2 | no |
| lmctf-2022-02-08-mactf06-20.14.dm2 | 118.6 | 10 | no (under floor) |
| lmctf-2022-02-08-mactf06-20.18.dm2 | 11.2 | 3 | no |
| lmctf-2022-02-08-mactf06-20.20.dm2 | 115.3 | 10 | no (under floor) |
| lmctf-2022-02-08-**mactf06-20.37**.dm2 | 383.1 | 10 | usable — **dropped, see below** |
| lmctf-2022-02-15-mactf06-20.42.dm2 | 762.9 | 10 | **yes** |
| lmctf-2022-02-15-mactf06-20.54.dm2 | 1321.5 | 10 | **yes** |

Four demos clear the floor; the set needs three. Selection rule: keep the
chronological first and last (20.01, 20.54 — the widest possible time
spread, and already on different recording dates, 2026-02-08 vs
2026-02-15), then of the two middle demos (20.37, 20.42) keep the one whose
recorded `steals_total` (module comment (b): 20.01=6, 20.37=16, 20.42=14,
20.54=21) adds the most new spread to the set rather than duplicating a
value already covered — 20.42 (14) sits far from both kept endpoints
(6, 21); 20.37 (16) is redundant with 20.42's 14. Drop 20.37. This is a
value-spread rule stated and applied *after* looking at the Stage-A record
that was already published in this repo before this document existed, not
a rule tuned to produce a particular judging outcome — nothing about
captures, momentum shape, or panel appearance entered the decision.

**Final human arm (3 files), all in `~/Games/Quake2/lmctf-hooktest/demos/`:**
`lmctf-2022-02-08-mactf06-20.01.dm2`, `lmctf-2022-02-15-mactf06-20.42.dm2`,
`lmctf-2022-02-15-mactf06-20.54.dm2`.

**Bot arm — the wave6xx-s03/s04 farm, waves 600+ (the servers Stage A's own
wave498-522 s03/s04 mactf06 corpus was drawn from).** Verified live in this
session (`--scalars --stands tools/stands.json --pov-parity` over
`wave6{00..24}-s0{3,4}-5v5.dm2`, 50 files): **all 50 candidates map to
mactf06 with zero skips and zero failures.** Selection rule, identical in
form to rung 4's bot-arm rule: sort the 50 candidates chronologically
(wave, then server), take positions 1, 26 and 50 of 50 — first wave/first
server, middle wave/second server, last wave/second server, so no two
files are adjacent waves and both servers are represented:

**Final bot arm (3 files):** `wave600-s03-5v5.dm2` (steals=3, 0 caps),
`wave612-s04-5v5.dm2` (steals=8, 0 caps in the position-1/26/50 sweep),
`wave624-s04-5v5.dm2` (steals=7, 0 caps).

**Set composition:** 3 bot / 3 human, all mactf06, all bot sheets rendered
with `--pov-parity` (mandatory, L5) and `--stands tools/stands.json`
(mactf06's stand coordinates, `{"red": [200.0, -1698.0, 175.0], "blue":
[-200.0, 1702.0, 175.0]}`, are already in that file — no `StandsMissing`
risk). Captions: `map=mactf06 hash=<12-hex> carries=<N>` — the exact set
outcomecard.py's own fairness rule allows, nothing else. Sheets are handed
to three fresh judges (no judge who has seen this protocol document, any
prior outcomecard.py output, or any earlier rung's judge set on this map).

---

## 5. Pre-registration

**Pass standard.** Same standard as every other rung on this ladder
(LEDGER.md's ladder-standards line): the rung passes when three fresh
judges' pooled forced-choice score sits at or below the rung-1 precedent of
~3/18 — chance or worse (9/18 expected at 50/50 on 3 judges x 6 sheets =
18 total calls), the same bar rung 3 cleared at 5/18. Pre-registered before
any judge sees a sheet; not adjusted after seeing scores. **This standard
applies only once section 2's sequencing recommendation is overridden by
the owner or superseded by stage-2 completion** — it is written now so
nothing blocks execution when that happens.

**Tell-ranking procedure on failure** (pooled score above the bar):
mirrors rung 3's and rung 4's post-mortem method.

1. For every sheet, split judges' free-text "what drove the call" answers
   by verdict (correct bot ID / correct human ID / wrong). A tell is any
   phrase that recurs across ≥2 of the 3 judges on the *same* sheet shape
   (bot or human) and correlates with a correct call.
2. Cross-reference each recurring tell against panel 4's raw steal-count
   step lines first — since `steals_total` is the one validated scalar, a
   tell traceable to "how many total steals by the end" (i.e., final
   height of the step lines, or equivalently the captioned `carries=N`)
   is read as the **expected, already-known** signal, not a new finding —
   log it as confirmation, not discovery, and cross-check it against the
   post-stage-2 steal-rate numbers once available.
3. A tell traceable to panels 1-3, or to panel 4's conversion-ratio
   overlay specifically, is read as an instrument artifact (these are all
   sub-gate per section 1) rather than a behavioral finding, and gets
   logged as design debt against those scalars — most likely candidate
   per the dry run (section 6): the conversion-ratio line's tendency to
   spike toward 1.0 immediately after an early, low-steal-count capture,
   which is an artifact of a rate with a small denominator, not
   population-conditional in principle but correlated with the bot arm's
   already-lower steal volume in practice.
4. Any tell that survives cross-reference against 2 and 3 — i.e., that is
   NOT explainable by final steal count or by a sub-gate panel — is the
   valuable outcome of running this rung at all: a genuinely new,
   previously-uninstrumented signal for match-outcome authenticity, worth
   naming as its own follow-on the way rung 3's hit-triangle artifact and
   rung 4's team-mirror-symmetry tell were.
5. Per rung 3's and rung 4's precedent, check for judge-level inversion (a
   judge scoring *worse* than chance individually) before pooling — worth
   naming even if the pooled score alone would already pass or fail.

---

## 6. Dry run

Environment: `~/.venvs/slipgate-film/bin/python3` (this repo's durable film
venv), `timeout 570` wrapper on every invocation, `--stands
tools/stands.json` (has mactf06's stand coordinates already).

**Bot-arm map/skip verification** (50 candidates,
`wave6{00..24}-s0{3,4}-5v5.dm2`, `--scalars --pov-parity --stands
tools/stands.json`): all 50 rows printed `bot,mactf06,...`, zero SKIP, zero
FAIL. Steals_total across the sample ranged 0-11 (e.g. wave611-s03=0,
wave621-s03=11), consistent with the Stage-A record's bot range.

**Bot sheet #1** (`wave600-s03-5v5.dm2`, one of the three files selected
for the real set):

```
timeout 570 ~/.venvs/slipgate-film/bin/python3 tools/outcomecard.py \
  ~/.local/share/YamagiQ2/lmctf-hooktest/demos/wave600-s03-5v5.dm2 \
  --out <dir> --pov-parity --stands tools/stands.json
```
`OK wave600-s03-5v5.dm2 -> 119fe657534e.png  map=mactf06 bot players=10
carries=3 vis=0.293 caps_red=0 caps_blue=0 first_cap=n/a lead_changes=0
steals=3 conversion=0.000`. `pov_parity.applied=True,
sample_keep_fraction=0.353`.

**Human sheet #1** (`lmctf-2022-02-08-mactf06-20.01.dm2`, one of the three
files selected for the real set):

```
timeout 570 ~/.venvs/slipgate-film/bin/python3 tools/outcomecard.py \
  ~/Games/Quake2/lmctf-hooktest/demos/lmctf-2022-02-08-mactf06-20.01.dm2 \
  --out <dir> --stands tools/stands.json
```
`OK lmctf-2022-02-08-mactf06-20.01.dm2 -> 9271d2e8a777.png  map=mactf06
human players=10 carries=6 vis=0.300 caps_red=0 caps_blue=0 first_cap=n/a
lead_changes=0 steals=6 conversion=0.000`. `pov_parity.applied=False` ("not
a serverrecord demo" — confirms the same control-flow note teamsheet.py's
Stage A already found, not a bug).

Both of these landed at zero captures, which is common at this map/corpus
(module note (b)), so two supplementary sheets were also rendered — not
part of the official three-file set, but needed to check panel-2/3/4
rendering under an actual capture event rather than only the empty-panel
case:

**Bot sheet #2 (supplementary, not in the official set)**
`wave621-s03-5v5.dm2`: `OK ... -> 8cb7fa564152.png  map=mactf06 bot
players=10 carries=11 vis=0.330 caps_red=1 caps_blue=0 first_cap=0.087
lead_changes=0 steals=11 conversion=0.091`.

**Human sheet #2** (`lmctf-2022-02-15-mactf06-20.54.dm2`, one of the three
official files, and also the richest available capture data):
`OK ... -> 089f5e768dcf.png  map=mactf06 human players=10 carries=21
vis=0.375 caps_red=0 caps_blue=1 first_cap=0.768 lead_changes=0 steals=21
conversion=0.048`.

**Caption check:** all four PNGs read `map=mactf06   hash=<12-hex>
carries=<N>` and nothing else — no duration, no player count, no filename,
no parity marker, matching the fairness rule exactly. Sidecars (not shown
to judges) correctly record `demo_shape` as `serverrecord(bot)` /
`client(human)` for the corresponding files — the unblinding table works.

**Panel comparability, by eye:**

- All four panels are event-driven step functions or a fixed-bin
  histogram, never a continuous position/coverage trace — structurally
  unlike teamsheet.py's panel 1, which drew continuous spacing lines and
  was where rung 4's "coverage burstiness" leak risk lived. **That specific
  leak class cannot occur here**: there is no panel on this sheet that
  plots per-frame tracking coverage, so a pov-parity-induced visibility
  gap has no direct visual channel to leak through the way it did on
  teamsheet.py.
- Panel 1 (score progression) and panel 3 (momentum) render as flat lines
  at zero on both zero-capture sheets, identical in structure — no visible
  asymmetry, and both match the NOTES_TEXT convention ("flat zero-lead
  line means nothing filled it").
- Panel 2 (cap-timing histogram) is empty on both zero-capture sheets, and
  on the two capture-bearing sheets shows a single full-height dashed
  marker at the (one) first-cap time, identical drawing convention on bot
  and human.
- Panel 4's solid step lines (the validated channel) render identically
  in structure on all four sheets: a staircase rising by 1 at each
  carry-window start, axis ceiling fixed at 30 regardless of demo. The
  *height reached* differs by population (3-11 bot vs 6-21 human in this
  sample) — that is the validated signal itself, working as intended, not
  a leak.

**Instrument-leak candidates found by eye (neither disqualifying on its
own; both logged for the tell-ranking pass per section 5, step 3):**

1. **Panel 4's conversion-ratio line spikes toward 1.0 immediately after
   an early capture with few steals banked.** On bot sheet #2
   (`8cb7fa564152`, cap at normalized t=0.087 with only 1-2 steals so far),
   the dashed conversion line jumps to the top of its axis right at the
   capture and decays steadily as more uncredited steals accumulate — a
   visually dramatic spike. On human sheet #2 (`089f5e768dcf`, cap at
   t=0.768 after 16 prior steals), the same line stays under 0.35
   throughout, a much flatter shape, because the denominator is already
   large by the time the capture lands. This is a mechanical property of
   a ratio with a small early denominator, not population-conditional in
   principle — a human demo with an early, low-volume capture would spike
   the same way — but in *this* corpus it correlates with the bot arm
   because bots bank fewer total steals (the validated signal itself), so
   their captures are proportionally more likely to land early relative
   to a small denominator. Mitigation already in place: the judge briefing
   (section 3) explicitly excludes the conversion-ratio overlay from what
   judges may use as evidence, even though it shares panel 4 with the one
   trusted channel.
2. **No coverage-shape or roster-count leak found**, unlike rung 4 —
   consistent with the structural point above (no continuous-tracking
   panel exists on this sheet) and with module note (b)'s confirmation
   that mactf06's human arm has no roster-size artifact to begin with.
3. **`sample_keep_fraction` (pov-parity) and `visible_fraction` differ
   between bot and human sheets in the sidecar (e.g. 0.353 vs "not
   applicable" on sheet #1) but neither number, nor anything derived
   from per-frame visibility, appears anywhere on the PNG or in the
   caption** — confirmed by inspection of all four rendered sheets. This
   is the same confound module note (a) already prices into `steals_total`
   itself (via the pov-parity radius sweep), not a new, panel-visible leak.

---

## 7. Summary for the record

- Instrument: `tools/outcomecard.py`. Judging centerpiece: `steals_total`
  (panel 4's solid step lines only), the one scalar clearing 0.85 on
  either calibrated map — 0.964 on mactf06, stable across a pov-parity
  radius sweep (0.966→0.964→0.956). lmctf22 fails outright (best 0.828)
  and is excluded per `set-composition.md`. Panels 1-3 and panel 4's
  conversion-ratio overlay render on every sheet but are diagnostic-only
  and must be captioned to judges as such.
- **Central problem and recommendation:** the one validated scalar reads
  as REGIME-DEPENDENT — bots initiate steals far less often than humans
  (~0.26/min vs ~1.3/min) specifically because stage 2 of this project has
  not yet done the volume work to close that gap. Recommendation:
  **defer judging until after stage 2's steal-initiation work lands**,
  because (1) a verdict today would certify a known, in-progress
  engineering gap rather than an open behavioral question, (2) fresh
  judges are non-renewable on this ladder and spending them on a
  near-certain, already-explained fail (0.964 separability) is a worse
  trade than holding them for a genuinely uncertain post-stage-2 result,
  and (3) this matches the project's standing pattern of parking an
  instrument behind a named, already-scheduled blocker. Consequence: the
  ladder's rung-5 line stays open longer, stated plainly rather than
  papered over with a premature number.
- **Single-map cost:** mactf06-only is consistent with every other rung on
  this ladder (none has yet been judged on two maps), but it means any
  rung-5 verdict, whenever run, certifies match-outcome realism on mactf06
  specifically, not generally — a second map's corpus would need to
  separately clear the same 0.85 gate before a broader claim is possible.
- Set (pre-registered, ready to run when stage 2 lands): 3 bot
  (`wave600-s03-5v5.dm2`, `wave612-s04-5v5.dm2`, `wave624-s04-5v5.dm2`, all
  confirmed mactf06, all rendered with mandatory `--pov-parity`) / 3 human
  (`lmctf-2022-02-08-mactf06-20.01.dm2`,
  `lmctf-2022-02-15-mactf06-20.42.dm2`,
  `lmctf-2022-02-15-mactf06-20.54.dm2`), sealed captions, three fresh
  judges, forced choice + 1-4 conviction + required free text.
- Pass standard: pooled score at/below ~3/18 (chance or worse), same bar
  as rung 1 and the bar rung 3 cleared at 5/18. Tell-ranking procedure
  pre-registered above, with an explicit rule for distinguishing "expected
  confirmation of the known steal-count signal" from "genuinely new tell."
- Dry run: both sheet types render correctly, captions clean, panels
  structurally comparable, and the specific leak class rung 4 found
  (coverage-burstiness on a continuous-tracking panel) structurally cannot
  occur here since no panel on this sheet plots per-frame position or
  coverage. One leak-risk candidate logged (panel 4's conversion-ratio
  overlay spiking early on low-steal-count demos) — not disqualifying,
  pre-flagged for the tell-ranking pass, and already excluded from what
  judges may cite as evidence.
