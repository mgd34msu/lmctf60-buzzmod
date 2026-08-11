# TRIALS

Pre-registered trial designs for every dark (built-but-untrialed) SLIPGATE
feature, plus escape-priors (already in trial, documented for completeness).
Protocol source: LEDGER.md (bars/verdict format, one-variable-per-pair rule,
Stage-A instrument calibration) and tools/iterate2.sh (per-server cvar arrays
— each feature gets one array, one column per server, `s09` stays the clean
control with every `sg_` flag at 0). Format: terse, factual, no prose padding.

Cvar-existence check (task requirement): all ten cvars named in the brief
exist in the tree under the exact names given. No name mismatches. One
location note: `sg_sessiondb` lives in `ctf_sqlite_unidb.c`/`.h` at the repo
root, not under `slipgate/` — it is a stats-recording flag, not a bot
doctrine flag, so it was never going to be in the bot tree.

---

## 1. sg_aimtexture

**What it does / models.** `slipgate/sg_combat.c:456-470` (block header) and
`:568-580` (state fields). Off, the shipped aim model gets error SIZE right
and SHAPE wrong: a decaying-cone tremor that resamples a fresh random
direction every 0.25-0.5s. On, it adds three shape terms on top of the same
size budget: (1) overshoot on acquisition with one-or-two damped corrections,
(2) a settle window that grows with swing size (Fitts's law), (3) slow
continuous tracking wander instead of the stamped tremor. `SG_TEX_FITTS_REF`
is fitted so the settle window is unchanged (1.0x) at a 30-degree flick — the
match average is deliberately preserved; only the *distribution* changes.

**Instrument.** No current instrument sees this. `fightsheet.py`'s
`mean_aim_offset_deg` (`_compute_scalars`, fightsheet.py:1264) is the only
aim-precision scalar on record, and it is a MEAN of single-frame bearing
deviations captured at the instant each shot fires (`attribute_shots`,
fightsheet.py:816-865) — one sample per shot, not a trajectory. It cannot
distinguish a decaying-tremor cone from an overshoot-and-correct swing
because aimtexture is fitted to leave the mean unchanged by design. The rung-3
set #1 blind judges already flagged "machine-grade aim" qualitatively on
film (LEDGER line 258) — that is the only existing evidence, and it is
non-scalar. **Finding: no scalar instrument can validate aimtexture as
built.** What's needed: a per-acquisition yaw-error TIME SERIES from
first-sighting to first-shot (or trigger-window open), reduced to an
overshoot-count / settle-duration-vs-swing-size scalar — i.e., extend
`attribute_shots` to sample every frame of an engagement's opening acquisition
window, not just the frame a shot leaves. Until that exists, the trial can
only be judged qualitatively (a repeat blind film read), not by Stage-A
scalar.

**Pre-registered bars.** Qualitative only, given the instrument gap: the
next blind film set should no longer name "machine-grade aim" / "the
crosshair snaps and holds" as a tell (it was set #1's #1 ranked tell behind
spawn-blaster commitment). No numeric anchor exists in the Stage-A records
for this — that absence is itself part of the finding above.

**Trial slot.** Queued behind route dither (next free pair) and behind
whichever of the two live trials (tapvar s03/s04, wcommit-mode2 s06/s07)
resolves first, since this needs a fightsheet-judged pair (mactf06 or
lmctf44) and a fresh blind film set. First in the dark-feature queue per the
ordering directive — rung-3 already named its tell.

**Risks / interactions.** Shares state-table real estate and per-shot timing
with `sg_tapvar` (both add per-shot fields in `sg_combat_state_t`) — the two
must not be armed together on one pair, or a cadence delta on the next film
set cannot be attributed to either. Also touches the same engagement window
`sg_wcommit` already changed (weapon commitment keeps the fight on one gun
longer, which lengthens exactly the acquisition windows aimtexture would
reshape) — trial after wcommit's effect is stable, not concurrently with a
new wcommit dose.

---

## 2. sg_railrhythm

**What it does / models.** Two mechanisms behind one cvar. (a) Perception:
`sg_caco.c:1783` `SG_RailRhythm()` gates a haste-rail timing table
(`sg_caco_railshot`, `sg_caco.c:1747-1830`) — when each team last heard a
client fire WITH HASTE (the rune's `player/lava1.wav`, a real server-side
sound), used to infer when a rail cycle is "cold" (loaded again),
`SG_RailCold` at `sg_caco.c:1876`. (b) Two doctrine effects gated on that
belief: RAIL COVER pricing (`sg_arach.c:4579-4601`, `:5164-5200`) — a route
candidate visible to a believed railer's post costs 1.5x for a carrier, 1x
otherwise; and THE RAILHOLD (`sg_arach.c:6221-6314`) — a bot whose next step
crosses a believed rail sightline, standing in cover right now, with the gun
not yet cold, physically STOPS and waits (0.8s at skill 0 to 1.5s at skill
4, capped, non-renewable) until the rail fires again (window opens) or
patience runs out, then crosses. Models a human timing a doorway crossing
against a known camper's reload, not just avoiding the doorway outright.

**Instrument.** No current instrument sees either half. The RAILHOLD is a
stop-and-wait in open corridor immediately before a sightline crossing — a
movement-onset pattern, not an engagement (it fires before any fight is
detected), so `fightsheet.py`'s `classify_disengage`/`approach_angle`
(fightsheet.py:972-1050, engagement-scoped) do not reach it. `routesheet.py`
has no dwell-before-crossing scalar; `revisit_spike2_mass` and
`occupancy_kl_bits` are the nearest proxies and neither isolates a
sightline-timed pause from ordinary route noise. **Finding: build a new
scalar** — dwell duration (near-zero net displacement) in the 0-2s
immediately before a track crosses a cell known to be visible from a
recent rail-fire location, cross-referenced against `sg_caco_railshot`
equivalents in the recorded demo (rail muzzle events are already parsed by
`parse_delta_entity_fight`/`_full`).

**Pre-registered bars.** Direction only, no Stage-A anchor recorded (the
instrument doesn't exist yet). Expect: pre-crossing dwell events near known
rail sightlines to appear on armed film that do not appear on control film;
carrier deaths-to-rail on lit approaches (already visible via existing
`weapon_class`/hit attribution) should fall; caps must not fall
(`SG_RAIL_HOLD_GAP`/patience caps the worst case at 1.5s once per crossing,
per the code's own "the cap is the point" comment, sg_arach.c:6238-6244).

**Trial slot.** Second in the dark-feature queue (after aimtexture), needs
its own pair — do not share a pair with aimtexture or tapvar (both also
touch rail-adjacent combat timing and would confound the film read). Best
filmed on mactf06 (the lane map, per iterate2.sh comment "the MEGAWORTH
A/B... mactf06 has the lane doctrine" / "lane A/B moved to mactf06 — THE
lane map") once a fightsheet pair frees.

**Risks / interactions.** Shares route-pricing machinery with the ADOPTED
`sg_raillane` (defender sightline post, RAILLANE array in iterate2.sh) —
both price a seed against a rail sightline, one from a defender's fixed
post, one from a believed railer's live position; verify the two pricing
terms are additive and not double-counting the same corridor on a map that
has both a raillane post and an active railhold. Also interacts with
`sg_beliefcone`/`sg_beliefrange` (below): rail threat is read from the same
belief table those cvars would narrow, so trialing them concurrently
confounds which change moved the film.

---

## 3. sg_beliefcone / sg_beliefrange

**What it does / models.** `slipgate/sg_caco.c:135-165` ("BELIEF HAS EYES,
NOT SONAR", census gap 11). `Caco_Visible` currently forms a sighting from
PVS + unobstructed trace alone, which lets a bot "see" (form a belief about)
a target directly behind it at any distance the map allows. `sg_beliefcone`
is a full cone width in degrees (0 = off; combat's own firing cone is 120,
and the comment notes belief should stay WIDER than that); `sg_beliefrange`
is a distance cap in units (0 = off). Both default off. The code's own
comment states the honest finding up front: "they make bots strictly worse
... so they ship dark until the film says the honesty is worth it" — this is
an accuracy-vs-honesty trade admitted at the point of authorship, not a
performance feature.

**Instrument.** No scalar instrument targets belief formation directly (it
is upstream of everything — routing, combat, tilt, railrhythm all consume
the belief table). The closest indirect read is whatever downstream
behavior narrows: fewer/later engagements initiated from behind (would show
in fightsheet's `approach_angle`/engagement onset timing), later reaction to
a flanker (would show in the react-only defense stack's existing metrics,
if run on a map with flank geometry). **Finding: no instrument isolates
belief-cone/range's own effect; only cross-feature secondary symptoms are
visible, and those are also moved by tilt, railrhythm, and every other
belief consumer**, so a clean read requires this be trialed ALONE, nothing
else belief-dependent armed on the same pair.

**Pre-registered bars.** No confident directional bar — the code itself
predicts bots get "strictly worse" (fewer valid sightings). Pre-registered
success criterion is narrow and cost-gated per Rule 21 (caps > cosmetics):
caps must not fall outside noise: if they do, this is a STRUCK candidate on
arrival, not a dose-tuning problem, since the feature trades information for
realism and Rule 21 forbids paying caps for cosmetics. If caps hold, the
honesty gain (narrower FOV, capped range) is free and adoptable at whatever
cone/range values were tested.

**Trial slot.** Third in queue. Must be isolated from railrhythm and tilt
(both consume belief) — needs a pair with no other belief-dependent dark
feature armed. Given the "one variable per pair" rule and current fleet
saturation, this likely waits for BOTH the aimtexture and railrhythm slots
to clear so a genuinely clean pair exists, or takes the slot the moment one
frees rather than sharing.

**Risks / interactions.** Directly upstream of `sg_railrhythm` (rail_client/
rail_seed come from the same `sg_caco_enemies` belief table narrowed by
this), `sg_tilt` (`tilt_killer_seed` is "where the killer was BELIEVED"),
and the existing adopted approach-cover/carrier-cover terms (all read
`seen_time`/sighting freshness from belief). Arming this alongside any of
them makes a film delta unattributable. Recommend: trial belief-cone/range
FIRST among the belief-dependent group, before railrhythm and tilt, so
their own trials run against a settled belief model rather than a moving
one. (This conflicts with the requested ordering, which puts railrhythm
ahead of belief cone — flagging the conflict rather than silently
resequencing; the requested order is followed above, this note names the
dependency risk it creates.)

---

## 4. sg_clockplay

**What it does / models. ** `slipgate/sg_arach.c:332-360` (MATCH CONTEXT
block) and `:1943-1965` (`Clock_DefendShift` application). No bot in the
tree has ever known the score or the clock — minute 19 plays identically to
minute 2. On, two numbers (score margin, clock fraction) read once/second
drive one latched posture per team on a 15-second tick: AHEAD LATE (>=1 cap
up, <25% clock left) adds one defender, carrier cover prices 1.3x ("a lead
is carried home"); BEHIND LATE (>=1 cap down, same window) pulls one body
OFF defense into the enemy base, carrier cover prices 0.8x ("the flag has to
arrive before the horn"); CLOSE LATE (tied, last 10%) pushes an extra
attacker on BOTH sides (overtime scramble). Applied last, as a +/-1 lean on
top of whatever `defenders_wanted` the flag/duel state already computed —
never a formation replacement, never drops the team below one attacker.

**Instrument.** `teamsheet.py`'s `defense_fraction` (panel 3, "defense
posture", teamsheet.py:335-368) is the only existing posture-over-time
scalar, and `mean_simultaneous_attackers` (panel 4, "push synchronization")
is the only push-timing scalar. Both are UNCONDITIONED means/aggregates —
neither is binned by score state or clock fraction, so neither can currently
show a LATE-GAME, SCORE-DEPENDENT shift; a flat average across a whole match
is exactly what would hide clockplay's effect (it only fires in the last
10-25% of the clock, latched, one body). LEDGER already flags panels 1/3/4
as "diagnostic-only" at n=4 human (LEDGER line 16). **Finding: existing
panels cannot see this feature as built** — they'd need to be re-cut by
time-bin conditioned on live score margin (which the demo does carry:
score is server state, not something that needs mining) to show whether
defense_fraction/attacker-count diverge specifically in AHEAD-LATE vs
BEHIND-LATE windows.

**Pre-registered bars.** Directional, no numeric human anchor in Stage-A
records for score-conditioned posture (none has been cut). Expect: in the
final quarter of armed matches, defense_fraction should rise on the leading
team's control windows and fall on the trailing team's, relative to a
first-half baseline from the same match; mean_simultaneous_attackers should
converge toward parity in the CLOSE-LATE state. Caps/conversion must hold —
this is a positioning lean, not a route-cost change.

**Trial slot.** Fourth in queue; needs a teamsheet-judged pair with enough
duration to reach the last-quarter window reliably (900s maps, not the
600s 2v2/5v0 canaries) — the existing defense-trial pairs (history: s03 vs
s05 dwell/field-mode) are the natural candidates once free.

**Risks / interactions.** Directly modifies `defenders_wanted`, the same
quota the ADOPTED `sg_duelroles` and `sg_defreact` already shape
(`sg_arach.c:1930-1941`). The code applies clockplay LAST and clamps to
never remove the last attacker, which is the right order to avoid fighting
duelroles' pin — but a trial pair must have duelroles/defreact at their
adopted fleet values on BOTH arms (as iterate2.sh already does for every
non-control server), so clockplay is the only delta.

---

## 5. sg_spawnbeat

**What it does / models.** `slipgate/sg_arach.c:279-289` (field comments)
and `:4517-4545` (application, "enhancement 7"). The chase-cam tell: a bot
materializes on respawn already moving at full pace down a corridor it
cannot have looked at yet. A human checks a shoulder, then goes. The cvar is
a multiplier on a skill-scaled orientation pause (0.9s at bot_skill 0, 0.4s
at skill 4) applied to the FIRST beat of a life — never on the first spawn
of a level (join windows already stagger; sixteen bots pausing on the
opening whistle would be its own tell, guarded by `beat_ready`).

**Instrument.** No scalar. This is a rung-1 (raw movement) tell, and rung 1
already PASSED on the bot-sheet judges (LEDGER line 13) — meaning the
current blind film read did not flag it, or flagged it below the pass bar.
`film.py`'s `draw_kinematic_strip` (film.py:1651) plots per-track speed with
death-tick marks and could visually show the pause (a near-zero-speed
segment immediately after a death tick), but film.py computes no
`SCALAR_KEYS` at all — it renders qualitative judge sheets, it does not
score anything automatically. **Finding: no automatic scalar exists; a
new one is buildable cheaply** — time from a death tick to the track
reaching half of its pre-death cruising speed, read straight off
`hspeed_series` (film.py:964) and `death_ticks` (film.py:752), needs no new
parsing.

**Pre-registered bars.** No numeric human anchor in the Stage-A records
(rung 1's PASSED verdict was global judge scoring, not a per-tell scalar).
Pre-registered qualitative bar: on the next rung-1 blind set, no judge
should independently name "instant full-pace departure from spawn" as a
miscall reason; if a time-to-cruising-speed scalar is built per the finding
above, expect the post-death distribution to gain a 0.4-0.9s-scaled lower
tail it does not have today. Caps must hold — this delays engagement onset
by under a second per life.

**Trial slot.** Sixth in queue (after clockplay, before handoff per the
requested order places handoff before session-db and tilt/airstrafe last;
spawnbeat sits between clockplay and handoff in the brief's ordering).
Needs a film.py-judged pair; since rung 1 already passed, this can safely
share a pair with a rung-1-adjacent trial as long as it's the only variable
that touches the death-to-first-move window (do not co-arm with tilt, which
also fires at that transition).

**Risks / interactions.** Shares the exact death->respawn transition with
`sg_tilt` (post-death caution) and reads `bot->lives`/`bot->legs`, which
`sg_routejitter` (ADOPTED, per-life pricing dice) also increments off of.
Stacking spawnbeat with a tilt trial on one pair double-loads the same
frame window and makes a film delta unattributable to either. Route-jitter
and nobacktrack are fleet-adopted on both arms already, so they are not a
confound by the "adopted stack runs on both arms" convention — only a
SECOND experimental death/respawn feature is the risk.

---

## 6. sg_handoff

**What it does / models.** `slipgate/sg_arach.c:5980-6006` (block header,
"census gap 10", owner rulings quoted directly) and `:6007-6060`
(application). A carrier below a skill-scaled HP threshold (60 at skill 0,
35 at skill 4 — the LOW-skill bot bails EARLIER, on the theory it is least
likely to finish the run) who is engaged or in a duel gives the flag to the
nearest-to-home teammate within the fixed ~150-unit toss range
(`ctf_TossEnt`'s hardcoded forward*200/z=300 lob — shared code path with
rune tosses and death-drops, not independently extendable without touching
general item-toss code). Models a human passing off a doomed carry instead
of dying with the flag in the open.

**Instrument.** No dedicated instrument, but a concrete gap exists in an
existing one. `film.py`'s `carry_windows` (film.py:700-732) tracks carry
possession PER ENTITY via the `EF_FLAG` bit; `classify_outcome`
(film.py:768-790) labels each window `captured`/`died`/`lost` — and a
handoff would currently be silently absorbed into `lost` (the flag bit
clears on the passer's entity without a death-teleport event, and there is
no check for a NEW carry window opening on a teammate's entity within ~1s
near the passer's last position). **Finding: `carry_windows`/
`classify_outcome` cannot currently distinguish a handoff from a
flag-timeout drop — both read as `lost`.** A cheap fix: stitch consecutive
same-color windows across entities when the gap is <2s and the new
carrier's start position is within toss range (~150u) of the old carrier's
end position; label those `handed off` instead of `lost`, and count them
separately from timeout drops. Until that stitch exists, the only visible
effect is aggregate: `outcomecard.py`'s `conversion` scalar (steals -> caps,
outcomecard.py:236-264), the same scalar `sg_breather`'s adoption was
argued on.

**Pre-registered bars.** Direction: `conversion` should rise (fewer flags
dying in the open near a robbed stand converts more steals to caps) — same
shape of claim as the ADOPTED breather result (0.046 -> 0.114-0.116, LEDGER
line 30), no reason to expect a comparable magnitude without film. Caps
must not fall. If the `carry_windows` stitch is built first, a direct bar:
handed-off share of non-captured carries should be nonzero and concentrated
in the low-HP band the code targets.

**Trial slot.** Fifth-to-seventh in queue (between spawnbeat and
escape-priors per the requested order). Must NOT share a pair with an
active `RUNEDOSE` (courier/rune-toss economy) trial — both features drive
`ctf_TossEnt`, and s03/s04/s06/s07 all currently carry `RUNEDOSE=2`
(iterate2.sh line 155) — so a handoff trial needs a pair off that set, e.g.
s05/s08 once escape-priors resolves, or s01/s02 if the fixed-matchup
canaries can be spared (unlikely; they're both fixed-format canaries, not
available for A/B).

**Risks / interactions.** Same toss mechanic as `sg_runetoss`/courier dose
(RUNEDOSE) — confound noted above. Also reads `goal_field` (home-field
cost) the same way duel-role assignment does; a 2v2 pair (s01/s02, duel
canary) is a bad venue both for the toss confound and because handoff's
"pass to a teammate nearer home" logic needs >=2 live teammates to have any
receiver candidates, which a 2v2 duel thins out structurally.

---

## 7. sg_escapeprior — IN TRIAL

**What it does / models.** `slipgate/sg_arach.c:598-614` ("enhancement 6",
`escapepriors.py`) and `:3482-3545` (application). Mined from 268 human
demos / 1549 usable steals: which of 8 compass buckets a human carrier was
actually in 3 seconds after grabbing a stand's flag, per map and per
stolen-flag color (colors kept separate — pooled entropy is 0.3-0.8 bits
higher, a real signal loss). On grab, one bucket is DRAWN (hashed, not
`random()`, so concurrent carriers draw independently and one carrier's
draw is stable for the whole 3s window) from that map's mined distribution,
and only that bucket's candidate roads get a price discount proportional to
its measured probability — the distribution's shape survives into behavior
instead of collapsing to the argmin exit every carrier used before.

**Instrument.** `outcomecard.py`'s `conversion` (steals -> caps) is the
named judge per LEDGER's own registration (line 79: "carrier conversion up
on outcomecard, caps hold"). Route-diversity scalars
(`routesheet.py`'s `offgraph_fraction`/`mean_route_entropy_bits`) are
secondary reads since escape-priors also diversifies immediate post-grab
routing, though the trial's primary bar is outcome-level.

**Pre-registered bars (already registered, LEDGER line 79).** Carrier
conversion up on outcomecard; caps hold.

**Trial slot (current).** Armed: s05 vs s08 control, both lmctf22 5v5
(iterate2.sh `ESCAPE` array, index 4 = 1). This is the third concurrent
trial pair alongside tapvar (s03/s04) and wcommit-mode2 (s06/s07) — owner
directive 2026-08-07: "everything concurrent that can be." Ribbon (48) and
route-jitter (8) run identically on both s05 and s08 (fleet-adopted
baseline), so escape-priors is the sole experimental delta on this pair.

**Risks / interactions.** Shares route-diversity territory with the
ADOPTED `sg_ribbon`/`sg_routejitter` stack, but since both run identically
on control and armed arms this is controlled, not confounded. Watch for
interaction with a future `sg_handoff` trial (both are late-carry-life
behaviors) — do not co-arm on the same pair once handoff reaches trial.

---

## 8. sg_sessiondb

**What it does / models.** `ctf_sqlite_unidb.c:1481-1514` and
`ctf_sqlite_unidb.h:44-58`. Not a bot-doctrine feature — no human behavior
modeled. It is an attendance recorder: one row per client per match in
`sg_session_events` (who was on the server, which side, bots flagged
`is_bot`), written from the per-client stats counters already maintained at
match end, plus chat-line counts (the one figure with no existing counter,
via `DB_SessionNoteChat`). Off by default; gated on both the cvar and
`ctf_statsdb 2` (the unified backend — a `match_id` only exists there).

**Instrument.** N/A — there is nothing behavioral to judge. This is
infrastructure for future corpus mining, not a trial subject. It directly
serves the gap named in LEDGER's "Not fully polished" #3 (teamsheet.py
validation needs corpus growth — "the 18-map manifest + s10 lmctf57 bot
film is the path to more eyes") by making bot attendance/session data
queryable the same way human sessions are, once turned on.

**Pre-registered bars.** None applicable — no gameplay effect to bar-test.

**Trial slot.** None needed. This does not compete for a film pair or an
A/B arm. Recommend flipping it fleet-wide (all ten servers, or at minimum
the servers already on the unified `ctf_statsdb 2` backend) as a standing
config change rather than a doctrine trial, whenever convenient — it is
additive, off-path for anything the trials above measure.

**Risks / interactions.** None to gameplay. Operational only: one extra
write per client per match end; negligible at current wave cadence.

---

## 9. sg_tilt

**What it does / models.** `slipgate/sg_local.h:365-372` (public summary)
and `sg_arach.c:304-319` (DEATH LANE MEMORY field block) plus the full
design note at `sg_arach.c:1400-1437` ("three things this is NOT: not aim,
not danger, not permanent"). Per-bot, non-shared "grudge": where the last
life ended and who ended it, plus the seeds within two links of that spot —
a lane the NEXT life prices 1.30x for 25 seconds (50s if the same lane took
two deaths inside a minute), a PREFERENCE not a wall (a genuine one-road
corridor still gets walked). A separate, smaller post-respawn-EVERYWHERE
caution also applies for a few seconds regardless of lane: reduced
engagement willingness (`SG_TiltCaution`, read from `sg_combat.c`, routing
side in `sg_arach.c:5352-5361`) and an open-ground cover surcharge
(borrowing the approach-cover dose) for every role, not just attackers on
the last leg. Route and willingness only — the code is explicit that aim,
reaction, and the trigger are never touched.

**Instrument.** No scalar. This is a route-pricing change conditioned on a
recent death location, which `routesheet.py`'s existing scalars
(`offgraph_fraction`, `occupancy_kl_bits`, `revisit_spike2_mass`) could in
principle move, but none is conditioned on "time since this bot's last
death" or "distance from last death seed", so a real effect would be
diluted into whole-match averages the same way clockplay's would be.
**Finding: needs a death-conditioned cut of the existing route scalars**
(bin route entropy / off-graph fraction by seconds-since-last-death, not
match-wide) rather than a wholly new instrument — the raw death-tick data
(`film.py:death_ticks`) and per-track positions already exist.

**Pre-registered bars.** Directional, no Stage-A anchor for a
death-conditioned cut (none has been run). Expect: in the 25-60s window
after a death, the dying bot's own route diversity near the death seed
should visibly narrow (avoid re-entering the two-hop lane) relative to its
own baseline; caps/steals should hold within noise (the code's own claim is
that on a single-road section the tilt loses to the gradient and costs
nothing).

**Trial slot.** Ninth in queue (per requested order, after session-db,
before airstrafe). Needs its own pair, NOT shared with spawnbeat (both fire
at the death/respawn transition) or with railrhythm/beliefcone (all three
touch belief/perception-adjacent pricing).

**Risks / interactions.** Borrows the `sg_approachcover` dose directly for
its post-death cover surcharge (`SG_TILT_COVER`, sg_arach.c:1447) — since
approachcover is ADOPTED fleet-wide (APPCOVER=200), tilt's contribution
rides on top of an already-active term; a trial pair needs approachcover at
its adopted value on both arms (iterate2.sh already does this), so the
delta stays isolated. Also touches `sg_nobacktrack` territory (both price
against a recently-occupied/exited location) — the two mechanisms are
independent (nobacktrack keys off the immediately-prior seed this life,
tilt off the death seed from the PREVIOUS life) but a judge reading film
should be told both are active if nobacktrack (adopted) is present, to
avoid mis-attributing an avoidance pattern.

---

## 10. sg_airstrafe

**What it does / models.** `slipgate/sg_arach.c:2282-2340` (derivation, "THE
VIEW AND THE PATH TURN TOGETHER") and `:8905-8935` (application: "0 off, 1
the lean, 2 the lean and the hops"). A human rotates view and strafe key
together while airborne so the wish direction stays off the velocity vector
for the whole flight (not just the instant the route bends), which chains
into 800-1600 u/s on a pub server versus this fleet's sustained runs never
exceeding ground speed (300). Derived from the ACTUAL engine constants (not
the negative-measuring `PM_AirAccelerate` derivation the module explicitly
rules out — yquake2 runs air movement through `PM_Accelerate` at accel 1 in
this build, confirmed against source line numbers in the comment). Dose 1 =
the lean alone (a sinusoid swing centered on the route heading, biased by
heading error for anti-drift); dose 2 additionally chains jumps
(`sg_arach.c:2696-2706`) by relaxing the hop-gate from "fast enough to be
worth it" (sp>270) to "on the ground at all" once a chain is live, since a
live chain has already paid the friction cost dose 1 exists to avoid paying
again.

**Instrument.** No scalar names it directly. `film.py`'s `hspeed_series`
(speed-over-time per track) would show elevated airborne speed runs on
film, and rung 1 (raw movement, PASSED) is the natural judging rung, but
this is explicitly flagged in LEDGER's "Not fully polished" #4 as "never
trialed" — no film pair has run it at all, so there is no existing read to
cite, positive or negative. **Finding: this needs a fresh rung-1 film pair
before anything else** — dose ladder tuning (chain-length limits) cannot be
set from Stage-A scalars that don't exist yet; it needs the same treatment
route-jitter/ribbon got (blind film read first, then a dose ladder once a
direction is confirmed).

**Pre-registered bars.** No Stage-A anchor (never trialed). Pre-registered
qualitative bar for the FIRST film read: visible airborne speed excursions
above the 300 ground cap on open/hop-heavy legs, without visible route
drift off the intended corridor (the anti-drift bias term is the whole
claim under test) and without visible chain-abuse on rope/combat/terminal-
brake legs (all three are explicitly vetoed in code — the read should
confirm the vetoes hold on film, not just in source). Caps/steals hold.

**Trial slot.** Last in queue per the requested ordering. Needs an open,
hop-friendly map (not a tight-corridor lane map) for a clean read; the
5v0/5v1 canaries (s02/s10-style) are the wrong venue (fixed-format,
reserved for their own canary role) — needs a genuine free A/B pair, which
given current fleet saturation is realistically the LAST dark feature to
get one.

**Risks / interactions.** Adjacent to the PARKED `sg_airgain` null
(296-297: "negative at both doses — the harvest turns velocity off the
route; needs view/path co-rotation") — airstrafe's own module note directly
addresses why airgain failed (it derived from the wrong engine function)
and positions itself as airgain's fix, not a separate feature; a judge or
future agent must not read an airstrafe null (if it happens) as
re-confirming the airgain null, or vice versa — they are different
derivations sharing a topic. Also interacts with `sg_landtick` (both touch
the airborne/grounded jump-timing boundary) and `sg_breather`/`term_brake`
(chain vetoes exist for exactly this — verify on film they actually hold,
not just in source, since dose 2's relaxed ground-gate is new code).

---

## Queue table

| Order | Feature | Cvar | Rung | Instrument today | Trial slot status |
|---|---|---|---|---|---|
| — | route dither | (rung-2 tell #2) | 2 | routesheet transition matrix | **first in line for next free pair** (already queued, LEDGER line 7) |
| 1 | Aim texture | sg_aimtexture | 3 | none (mean_aim_offset_deg blind to shape) | queued behind route dither; needs fightsheet pair |
| 2 | Rail rhythm | sg_railrhythm | 3 | none (needs new pre-crossing dwell scalar) | queued after aimtexture; own pair, mactf06 preferred |
| 3 | Belief cone/range | sg_beliefcone / sg_beliefrange | 3 (cross-cutting) | none direct; secondary symptoms only | queued after railrhythm; must run isolated from all belief consumers |
| 4 | Clockplay | sg_clockplay | 4 | teamsheet defense_fraction / mean_simultaneous_attackers, but UNCONDITIONED on score — needs re-cut | queued after belief cone; needs 900s teamsheet pair |
| 5 | Spawn beat | sg_spawnbeat | 1 | none (film.py has no scalar layer); buildable from hspeed_series + death_ticks | queued after clockplay; film.py pair, keep off tilt's pair |
| 6 | Handoff | sg_handoff | 5 (outcome) | outcomecard conversion (aggregate only); carry_windows/classify_outcome cannot distinguish handoff from timeout drop yet | queued after spawn beat; must avoid RUNEDOSE-active pairs (s03/s04/s06/s07) |
| 7 | Escape priors | sg_escapeprior | 2/5 | outcomecard conversion (named) | **IN TRIAL**: s05 armed vs s08 control, lmctf22 5v5 |
| 8 | Session DB | sg_sessiondb | n/a | n/a (infra, not doctrine) | no slot needed; recommend fleet-wide flip whenever convenient |
| 9 | Tilt | sg_tilt | 2 | none; needs death-conditioned cut of existing route scalars | queued after session-db; own pair, off railrhythm/spawnbeat/beliefcone pairs |
| 10 | Airstrafe (chain-length) | sg_airstrafe | 1 | none; never trialed at all | last in queue; needs an open hop-friendly map, realistically last to get a free pair |

**Cross-cutting finding:** of the ten features audited, only two
(escape-priors, already in trial; and the aggregate outcomecard `conversion`
scalar as a secondary read for handoff) have any existing Stage-A scalar
that would actually move on a trial. The other eight are either invisible
to every current instrument (aimtexture, railrhythm, belief cone/range,
spawnbeat, tilt, airstrafe) or visible only as an unconditioned average that
would dilute a real, narrow-window effect (clockplay). Session-db is not a
behavioral feature and needs no instrument at all. Before burning a scarce
server pair on several of these, building the specific extensions named
above (per-acquisition yaw trace, pre-crossing dwell, score-conditioned
teamsheet bins, death-conditioned route bins, cross-entity carry-window
stitching) is cheaper than a blind film read and would let more than one of
these trials share a single pair's film without arguing about which feature
moved which number.
