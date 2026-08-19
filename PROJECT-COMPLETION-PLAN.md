# LMCTF BuzzMod project completion plan

This file is the authoritative execution plan for finishing, promoting, and
releasing the entire project. It replaces the historical feature roadmap as an
execution authority. Historical plans, evidence, and release notes remain useful
records, but they cannot narrow this scope.

A checkbox is complete only when its named consumer path passes on one frozen
source tree, module pair, configuration, data bundle, and evidence set. A source
file, focused unit test, successful build, generated artifact, server banner, or
single server/map residence is never sufficient by itself.

## Scope that must not be confused again

- The supported conversion corpus contains **181 maps**. Both the original
  `lmctf02` and the padded `lmctf02c` are distinct required maps. Every one
  of the 181 maps needs a current RUNE artifact.
- The production fleet runs the exact ordered **top 20** map list in
  `tools/topmaps.txt`. It is the fleet's map list, not a conversion boundary
  and not a substitute for converting all 181 maps.
- Production consists of ten persistent q2ded processes. Each server receives
  the same cyclic top-20 list rotated by its server offset: s01 starts at entry
  0, s02 at entry 1, through s10 at entry 9. Quake performs the map changes
  inside the existing process. Processes are not restarted once per map and a
  "wave" is not the map-rotation mechanism.
- `lmctf58` is one difficult release blocker and semantic regression map. It is
  not the project, the corpus, or the definition of done.
- Bot quality remains product work. Movement, combat, team decisions, defense,
  hook use, item pursuit, snag recovery, perception, observer presentation, and
  match outcomes must continue improving and must be proven through their real
  consumers.
- Completion includes source, gameplay data, all 181 RUNE artifacts, the fleet,
  transactional deployment and rollback, repository history, documentation,
  CI, promotion, tag, and published release.

## Definition of done

The project is complete only when all of these statements are true together:

1. The final source builds without warnings on Linux x86_64, Windows x86, and
   Windows x64.
2. Both GNUmakefile and Makefile pass the complete host suite under GCC and
   Clang on the final source tree.
3. Every promised bot behavior has an executable consumer proof or a
   manifest-bound production-runtime proof. Source-text assertions alone do not
   close player-visible behavior.
4. The bot improvement backlog has been measured and dispositioned. Every trial
   is adopted with evidence or struck with retained counter-evidence; nothing is
   silently left as “planned next.”
5. All 181 maps have a newly generated RUNE artifact from the final source and
   module. All 181 pass the GNU C reader, independent Make C reader, Python
   reader, root-aware linter, applicable semantic checks, and fresh-process
   runtime loading. A controller summary containing GEN_FAIL, TIMEOUT, or
   LINT_FAIL is not successful completion.
6. `lmctf58` retains all ten long declared-door controllers, including all six
   mirrored cellar controllers, and representative cellar routes survive
   objective-core pruning and reach both flags.
7. The release manifest binds both module aliases, production configuration,
   the exact top-20 order, all 181 BSP/RUNE pairs, escape priors, and every
   applicable sidecar. Missing applicable data fails closed.
8. Deployment is quiescent and transactional across the complete bundle. Any
   copy, rename, verification, ownership, or lock failure restores the exact
   previous bundle.
9. The continuously running ten-server fleet uses ten cyclic rotations of the
   exact same ordered top-20 list. The initial offsets are 0 through 9, native
   `EndDMLevel`/`ExitLevel`/`gamemap` transitions advance each process to its
   next list entry, and the initial residence plus the next nineteen
   transitions visits all twenty maps exactly once; transition twenty returns
   to the initial map. No launcher-side per-map quit, one-map authority, forced
   `serverstop`, or process-per-map emulation may satisfy this gate.
10. The production fleet keeps nine 5v5 servers and only s08 at 7v7, without
    delayed roster mutation. Every map residence proves the expected map and
    RUNE identity, expected roster, positive movement/combat/objective activity,
    map-local POV close/reopen lifecycle, and one native transition to the next
    expected map while the same q2ded PID remains alive.
11. Production runtime evidence establishes the remaining outcomes: normal POV
    demo playback, movement quality, team and defense outcomes, snag
    consumption, item commitment, paired hook lifecycles, and observer sound
    attribution.
12. The source tree, installed bundle, 181-artifact corpus, fleet evidence,
    version, documentation, branch history, CI jobs, tag, and published assets
    all identify the same release.
13. `slipgate` and `main` contain coherent merge history for every proven
    milestone, exact-SHA CI is green on both branches, the final tag run
    publishes successfully, and the downloaded release verifies against its
    `SHA256SUMS`.

## Current verified baseline

Verified and already pushed:

- RUNE has one current wire/runtime contract; obsolete format selection is gone.
- Cross-platform warning fixes are green on Linux and both Windows builds.
- Release workflow actions use immutable Node 24 revisions.
- `lmctf44` has a regenerated artifact that passed an independent cold load.
- The shallow-water DIRECT door source, reducer, ticket, and grounded wet
  callback law is committed and its focused and host gates are green.
- The tracked corpus authority contains exactly 181 distinct maps, including
  both `lmctf02` and `lmctf02c`. Its controller requires generation, GNU and
  Make C-reader agreement, the Python reader, lint, applicable semantics, and
  a separate bounded fresh-process cold load before one map can report PASS.
- Non-random map-list parsing preserves the configured order and advances and
  wraps sequentially without changing the random-map branch.
- Role-telemetry consumers parse the production `seed/goal/sgoal/spd` schema,
  use the stable destination where required, and fail on zero recognized rows.
- The RUNE Python readers have one strict corpus loader, working authenticated
  expected-identity comparison, and adversarial duplicate/non-finite/oversized
  number regressions.
- All ten required `lmctf58` door-controller identities survive objective-core.
  The accepted fresh artifact passes both C readers, Python decoding, lint, the
  exact semantic checker, sanitizers, and a separate fresh-process cold load.
- The completed `slipgate`/`main` source pair is `6523af5`/`efc30fd`;
  exact-SHA CI runs `32234992472` and `32235013292` passed all eight
  non-publish jobs.

Known incomplete work:

- No final-source 181-map generation has run to 181/181 PASS. The controller is
  ready; the accepted artifacts do not yet exist.
- The current tracked fleet launchers still run finite per-map processes. A
  private persistent ten-process runner is under code review and does not yet
  provide the required full-cycle map/RUNE/roster/activity/POV receipts.
- The transactional fleet/release bundle and exact per-map-residence health
  gate are still being completed and reviewed.
- Bot behavior closure and real consumer proofs remain active work. Passing the
  `lmctf58` semantic checker will not complete them.
- No final 181-artifact corpus, production cutover, final production-runtime acceptance, final
  documentation reconciliation, version tag, or published release exists yet.

## Bot improvement work

Bot improvement is production-controller work. Measurement code, Stage A,
blind cards, and retained reports are acceptance gates for a controller change;
they are not substitutes for one and are not independent project milestones.
Bot work runs in parallel with navigation/corpus and fleet work, and the final
source freeze waits for the adopted controller changes and their executable
proofs.

### Production implementation order

Implement the remaining material deficiencies in this order:

1. **Remove wasted movement.** In `sg_descend.c`,
   `sg_defense_shift.c`, and `sg_move.c`, replace quiet posted-defender
   micro-pacing with deliberate, proved, non-reversing post-band patrol legs at
   walk throttle; contact must cancel patrol authority before movement. Then
   remove the remaining attacker-stand and far-field grind without weakening
   objective touch or proven route safety.
2. **Raise real steal initiation and conversion.** In `sg_goal.c`,
   `sg_descend.c`, `sg_move.c`, and `sg_arach.c`, make attackers produce more
   qualified stand entries and complete more authoritative `ctf_flagtouch`
   pickups. Do not repeat the struck close-approach pacing suppression and do
   not replace physical touch with controller-owned score state.
3. **Make hooks choose useful rides.** The controller already prices expected
   route progress at staging and again at the exact fire boundary, and its
   aim-wedge timeout shelves only the failed graph link while updating its
   failure streak. Exercise those existing production seams and their terminal
   lifecycle rather than inventing another hook policy. Never satisfy the gate
   by disabling hook use.
4. **Preserve combat strength without hidden information.** Current retained
   evidence has bots ahead of humans in hits per shot, about 0.58 versus 0.29;
   that is not a deficiency and must not be tuned downward. In `sg_combat.c`,
   change only a demonstrated weaker combat dimension while retaining that
   accuracy advantage, reaction delay, overshoot/settle, weapon switching,
   splash safety, skill-scaled error, and visible/audible information admission.
5. **Improve coordinated play.** In `sg_arach.c`, `sg_goal.c`, and the focused
   role modules, increase useful attack pressure and preserve the accepted
   carrier, escort, recover, defender, radio, and persona laws. Team assignment
   must remain stable per identity and independent across teams.
6. **Close item, POV, and sound behavior.** Drive item commitment through the
   real `Touch_Item` chain, prove ordinary observer playback and sound
   attribution, and keep each proof bound to the final controller/module.

Every item above requires a production C change, an executable test that calls
the changed policy or live seam, both Make dialects, and then a matched runtime
gate. A measurement-only change cannot complete an item. Implemented controller
slices now include the enabled quiet-defender patrol. Its unhurried dwell starts
only after the selected proved leg reaches its target, so a long walk cannot
consume the hold interval and collapse the circuit into continuous motion.
Other implemented slices include a bounded proved-RUN home-flag approach
preference that breaks near-stand field plateaus without
granting touch authority, immediate strike release when no ready teammate can
reach the synchronization window before its deadline, per-frame release when a
forming teammate falls behind the remaining synchronization clock, and weapon
preparation that yields to enemy-stand pressure inside five seconds or when its
route saves less than one second. The adopted immediate-return preference now
also yields in a one-finite-exit corridor, so it cannot turn a required reverse
edge into a three-second standstill. Outside the final 600 ms objective band,
an attacker whose composed preferences reject every move now takes the best
fully admitted, strictly descending proved RUN instead of pricing itself into a
far-field standstill; mechanisms and ballistic actions are never forced by that
fallback. The strike overlay now preserves one real RECOVER mission whenever
the team's flag is away, including one- and two-member casualty states, rather
than redirecting every survivor to an enemy flag the team cannot score. During
a live carrier standoff it prioritizes RECOVER, then ESCORT, then its bounded
CLEAR task, so casualties cannot spend the only available helper on optional
room-clearing while the carrier is unable to score. The recovery owner is
reassigned on death without disturbing the carrier or surviving escort.
During that same standoff the carrier's strike duty now retains the immutable
home-stand field; only the RECOVER duty follows the dynamic stolen-home-flag
field toward the enemy thief. A replacement recoverer is chosen around a live
incumbent escort when another reachable helper exists, so a casualty does not
unnecessarily reshuffle the carrier screen.
On the pickup frame before the ahead-of-carrier support flood is valid, the
escort falls back to that same home-stand route instead of briefly following
the thief-bound recovery field.
Live human orders are now excluded from autonomous strike membership, so an
ordered ATTACK, ESCORT, RECOVER, or DEFEND route cannot be overwritten by the
coordinator; actual flag possession still admits the carrier because physical
objective state outranks an older standing order.
The timed item-lead controller now aborts an existing pad commitment for every
ESCORT role, including a human-directed cover order with no flag carrier; the
escort cannot abandon its live screen for an early item.
Any live human order now aborts that optional pad commitment, including an
ordered ATTACK that previously passed the role-specific carrier, recovery,
escort, and defender checks. Item timing cannot replace an explicit mission.
The bounded defender weapon sortie follows the same precedence: it cannot arm
under a human order, and an order arriving during its outbound phase turns the
watchman onto the immutable home route instead of continuing to the weapon.
Leaving DEFEND now retires the role-local patrol target, dwell clock, and stand
latch immediately. A later defensive assignment cannot resume a patrol leg
selected under an older role or topology state.
Quiet-defender patrol route and dwell choices now advance a private per-bot
sequence. Other bots' combat, chat, route, and cosmetic randomness cannot
change a defender's admitted circuit or observation time.
Enabled lateral ribbon lanes and drift intervals likewise use a separate
per-bot sequence. A route transition can vary that bot's line without changing
another bot's movement or consuming combat/chat randomness.
Aim tremor, aim-texture wander, projectile lead error, tap variance, and
trigger-window jitter now advance a private per-client combat sequence. Chat,
movement, and another fighter's shot cadence cannot change a bot's next aim or
trigger decision.
Early powerup timing jitter now advances a separate per-bot sequence. Another
bot's route, chat, or fight cannot change when this bot first becomes eligible
to leave an ordinary mission for an admitted powerup errand.
Persona allocation now treats both human and SG names as occupied, selects one
unique authored row when available, and binds that exact row to the displayed
name. A human name collision cannot create two same-named bots or make a bot
play a different persona from the identity shown on the roster.
Concave-pocket retreat direction and duration now advance a private per-bot
sequence. One bot shelving a failed road cannot change another bot's escape,
aim, or movement line through shared RNG state.
Team-belief callout delays now advance a private team/topic sequence. Cosmetic
banter volume cannot change when earned flag or item information is published
to teammates and downstream item timing.
Earned flag-position intelligence now also admits through an older cosmetic
use of the chosen speaker's team-chat budget. The objective line still stamps
that budget after emission, so it suppresses lower-priority chatter that would
otherwise follow it instead of being silently discarded by that chatter.
The struck `sg_exitasym=0` carrier experiment is now inert even at the grab
edge; it no longer consumes a hidden global random draw while remaining
behaviorally disabled.
The optional defensive-rune courier now admits only an ordinary attacker or
organic carrier escort. It cannot redirect a RECOVER/DEFEND/CARRY mission or
override any live human order, and the same law gates both convergence and the
irreversible toss frame.
Defender ranking now counts only live same-team bodies, so a corpse in an early
bot slot cannot reserve the stand-watch quota while surviving teammates abandon
the base; escort admission now uses that identical live rank, so it cannot steal
the actual stand defender or skip a survivor because a corpse advanced the
quota. The ordinary stable slot order resumes when that bot respawns.
The same living-body law now governs rally partners, paired sentry splits,
attacker lane spreading, and teammate splash suppression: a corpse cannot make
an attacker wait, masquerade as a diversion, repel a live route, or suppress a
safe public-sound shot from its stale origin.
RECOVER terminal homing now finishes at the source of its admitted current or
belief field instead of turning back toward the empty home stand. An exact
dropped-flag item can replace that source only inside the same visible,
same-floor, hull-clear touch envelope used for honest physical contact.
Inside the exact enemy-flag touch envelope, the revalidated physical pickup
line now owns heading through contact: the generic obstacle fan cannot probe
past the flag and turn away because of geometry behind it, and route-heading
smoothing cannot preserve a stale bearing for that terminal frame. This does
not widen the touch envelope or bypass the authoritative `ctf_flagtouch` path.
Graph-hook usefulness is now judged against the exact current route field at
both source staging and the irreversible fire boundary. A tactical waypoint or
bounded weapon errand can therefore take a ride that advances that live route,
while a rope that only appears useful against a different strategic field is
retired before firing.
The successful `Touch_Item` callback now closes a matching early-return item
commitment before consulting the item-communication cvar. Disabling team
callouts suppresses belief/speech only; it cannot leave a bot pursuing a pad
whose item was physically taken.
The early-return planner also consults the same inventory/skill/co-op admission
law as `Pickup_Powerup` before committing, while travelling, and before terminal
homing. A bot at the legal quad or invulnerability capacity cannot abandon its
objective to camp an item the authoritative pickup callback would reject.
Every concrete strike duty now retires an optional early-return commitment
before applying its objective route. Coordinator routing and terminal item
homing can no longer coexist while a stale pad lease silently survives beneath
the bot's live BREACH, CLEAR, PRESS, ESCORT, RECOVER, or CARRY mission.
Sound belief now admits only finite, positively attenuated client audio.
Map-wide `ATTN_NONE` CTF, vote, and countdown announcements remain audible but
cannot turn their protocol emitter edict into an enemy position; ordinary
spatial weapon, pain, movement, and rune sounds retain their range and PHS
gates.
Public obituaries now contribute only victim identity and death time. Enemy-base
surge and strike windows use the freshest still-live seed that the observing
team had already earned for that client; an unseen death no longer publishes
the victim's exact server origin.
Combat target selection now gives a still-visible incumbent a 128-unit distance
hysteresis. Two enemies crossing by a few units can no longer alternate target
identity every frame and repeatedly restart reaction/settle, while a materially
closer threat still wins immediately.
Within the same ordinary range, cone, and live-visibility gates, a believed
enemy flag carrier receives a bounded 256-unit target score priority. Bots can
therefore prefer the objective thief through a modest range difference without
ignoring an immediate non-carrier threat or learning any unseen position.
The generic fifteen-second wedge suicide now recognizes a near-goal organic
escort as an intentional carrier-screen station, just like the existing
defender post and exact human cover hold. Escorts outside the bounded goal band
remain subject to normal stuck recovery.
The same valve now resets on a current retained duel or combat ownership from
the preceding frame. A bot holding position to aim and fire is therefore live
combat activity, not a navigation deadlock eligible for forced suicide.
The four- and eight-second route-failure watches use the same ownership law:
combat and intentional defender/escort holds cannot blacklist a sound route
merely because another controller deliberately kept the body stationary.
Strike-roster admission now considers every currently relevant proved field:
an otherwise disconnected bot with the only finite own-flag recovery or carrier
route cannot be excluded merely because its enemy-base field is infinite. If a
stable four-person roster is already full when the flag leaves home, it makes
one bounded replacement only when none of its members has a finite recovery
route; the actual carrier is never displaced. The same bounded rule admits an
otherwise excluded reachable escort when the retained roster cannot reach its
carrier, while protecting both the carrier and the sole recovery route.
The per-carry escort-dose decision now comes from team, carrier, and carry-epoch
identity rather than the process-global RNG. Unrelated combat, route, patrol,
chat, and cosmetic random calls can no longer decide whether a live carrier
receives its assigned screen, and the escort decision cannot perturb those
controllers in return.
Approach synchronization now relocates a waiting attacker only onto its current
seed or across one proved ordinary RUN edge. A nearby seed behind a wall,
mechanism, drop, or hook can no longer take direct movement authority for the
whole rally window; if no proved cover exists, the attacker keeps pressing the
enemy stand instead of freezing in exposed ground.
When a teammate already carries the enemy flag, the one role-selected escort
owns the moving carrier field and every remaining attacker keeps the fixed
enemy-stand field. Attack pressure can no longer collapse into redundant
carrier-following, nor can an unseen carrier make the enemy-flag belief
fallback route the whole attack share back toward its own stand. The same law
now owns strike `PRESS`, `BREACH`, and `CLEAR` routes, so the coordinator cannot
undo that pressure after the role objective selects it.
Organic escort assignment now ranks eligible bodies by the admitted
team-belief carrier route rather than reading the carrier's exact server
origin. When that sighting expires, one escort uses the fixed capture-stand
field as an honest rendezvous instead of disappearing until somebody sees the
carrier again; the incumbent still retains a bounded anti-flap advantage.
Graph-hook staging now treats live source-medium mismatch as a zero-command
replan: dry-versus-water drift, hazardous or air-starved water, and
water-to-water misuse release the commitment and force fresh localization.
Those states can no longer skip the hook controller while generic steering
walks directly toward its unearned landing, and the valid edge is not shelved.
Coordinated PRESS, BREACH, and CLEAR bodies now retain the bounded last-room
enemy-flag approach preference even when their underlying organic role is
RECOVER or ESCORT. The strike overlay can no longer grant the enemy route and
terminal touch controller while accidentally withholding the intervening
proved RUN preference that converts a near-stand plateau into physical entry.
That same live pressure authority now owns the approach-cover and exposed-seed
prices, forward combat pressure, strictly descending RUN fallback, and the
direct-touch anti-wedge recovery. A repurposed pressure body can no longer
inherit the enemy route while an obsolete organic-role check makes it recede,
stand still, or suicide beside the available flag.
The coordinator now publishes every bot's effective enemy-pressure membership
with the immutable pre-serial frame snapshot. Axis spreading therefore sees
live PRESS, BREACH, and CLEAR teammates even when their organic roles are
RECOVER or ESCORT, while the one actual recovery/escort owner remains outside
the pressure set. The ordinary route report uses the same snapshot for its
static goal, so a pressure body's movement is measured against the enemy stand
it is actually pursuing rather than the abandoned home-role destination.
Strike membership remains broad enough to retain the only reachable recovery
or carrier-route owner, but pressure duties are now narrower: BREACH, CLEAR,
and PRESS require a finite enemy-flag field or an already-proved direct touch.
An admitted bot can no longer have its usable organic mission replaced by an
infinite enemy route merely because the four-person coordinator roster is full.
These pure policies are executable; runtime outcome acceptance remains pending
on the persistent fleet.

### Acceptance instrumentation and provenance

The retained repository data is useful baseline material, but it is not all
final-release authority:

- the 66 human/reference JSON datasets are internally parseable, but the main
  human, escape, flag-live, defense, pro, and entity families do not embed the
  current module/BSP/RUNE identity tuple;
- `botledger.csv` contains 10,287 historical bot rows, but it reflects an older
  heterogeneous map/roster fleet and has no tracked analysis consumer;
- several reference families have consumers but no tracked producer, or a
  producer but no durable capture receipt.

Required support work, performed only as needed to accept or reject the
production changes above:

1. Define one canonical capture receipt binding each demo/log/report to source
   commit, module hash, BSP hash, RUNE hash, map, server, bot/client, and time
   interval.
2. Make every final metric consumer reject missing, stale, or conflicting
   identity rather than silently treating historical reference data as current.
3. Retain historical data for comparison, clearly label its authority, and
   generate a final-candidate bot ledger with a tracked analysis consumer.
4. Freeze metric definitions and acceptance bands before tuning against them.
5. Require measurement tools and reports in both host-test aggregates where
   they are part of release acceptance.

### Movement and route choice

Required work:

- validate the enabled quiet-defender patrol against retained movement film;
  its current executable law uses non-reversing proved walking legs, begins
  dwell only at proved arrival, and keeps route/dwell draws private per bot;
- compare bot trajectories with the retained human demo corpus using a frozen
  speed, acceleration, turn, air-gain, and route-shape metric;
- tune or strike the remaining human-speed air-strafe chain work from measured
  results, not a synthetic flat-floor Pmove alone;
- treat the plateau A-to-B-to-A spike as a bounded residual unless a new
  source-grounded mechanism improves it; the stronger immediate-return dose,
  route dither, and hook-pong shelving were struck and must not be repeated.
  Preserve the adopted `sg_nobacktrack 60` baseline rather than incorrectly
  describing it as disabled;
- keep the struck route-dither dose disabled; seed transitions now derive its
  dormant per-visit salt from bot/route state without consuming the global
  behavior RNG, so route length cannot perturb enabled patrol/ribbon choices;
- keep both the cosmetic scoreboard-ping base and its per-frame flicker derived
  from bot instance, slot, and frame identity; neither bot creation nor the
  ten-bot per-frame display path may consume shared gameplay RNG;
- preserve proven pit, water, fall, door, lift, teleport, and grapple safety;
- generate blind movement film and retain the exact source/demo/metric identity.

Acceptance:

- both Make dialects pass movement and navigation tests;
- the human trajectory comparison is inside its frozen acceptance band;
- no route-safety regression appears across the final 181 corpus;
- blind movement evidence is generated from the final module.

### Hook and rope behavior

Required work:

- retain the existing worthwhile-ride checks at both staging and fire;
- reduce low-value fire/release cadence without suppressing useful long rides;
- drive the real production controller through the aim-wedge timeout;
- prove exact failed-link shelving, failure-streak and ban updates, later
  worthwhile-fire admission, and every death/disconnect/map reset;
- retain exactly one terminal event for every hook-fire event.

Acceptance:

- a controller-level executable test reaches the production aim-wedge path;
- production fleet logs contain no unpaired, duplicate, malformed, or global-fatal
  hook lifecycle;
- hook movement and combat activity remain positive rather than being disabled
  to satisfy the diagnostic.

### Combat and perception

Required work:

- preserve the measured hits-per-shot advantage instead of mislabeling it as a
  shortfall or weakening it for human resemblance;
- identify any actually deficient combat outcome before changing combat
  policy; no combat C change is required merely to make this section busy;
- retain reaction delay, overshoot/settle texture, switch discipline, splash
  safety, and skill-scaled error;
- run instrumented trials for rail-rhythm hearing, belief cone and range,
  item-clock knowledge, spawn-beat knowledge, view tilt, and air-strafe chaining;
- adopt or strike each trial with evidence and update the ledger.

Acceptance:

- gunfight metrics stay inside the human comparison band;
- perception tests prove information is earned through visible/audible state;
- final film and persistent-fleet evidence show positive combat without
  forbidden knowledge.

### Team play, defense, and match outcomes

This work is ordered; a blind card on an intermediate build cannot close final
outcomes.

Metric contract:

- freeze steal rate from production `F Pickup` score events reconciled with
  `STATS_OFFENSE_FLAG`, per active team-minute and stratified by map, team,
  roster size, and configuration; demo flag-carry inference is diagnostic;
- freeze approach rate as 384-unit entries per observed stand-minute;
- freeze close-approach conversion separately as qualifying authoritative carry
  starts within 1.5 seconds divided by those approaches, with the same
  map/team/roster/config strata but not the same denominator;
- use production CTF flag-touch/stats/log events as capture authority; treat the
  `outcomecard.py` 280-unit geometric carry-end result as a diagnostic and
  reconcile every disagreement;
- report steal-to-capture conversion separately from raw steal and capture
  rates;
- pair defender displacement, departure frequency, post dwell, and visible
  motion with captures conceded from the same map/config/time receipt;
- require same map, roster, configuration, duration, recording policy, and
  metric version for baseline-versus-candidate comparisons.

Required execution:

1. Fix `gamestat.sh` and `rolestat.py` to consume the current telemetry schema
   containing `sgoal=`, and fail if zero SG rows are recognized.
2. Implement the actual production-controller changes in the implementation
   order above; report-only, fixture-only, and measurement-only changes cannot
   satisfy this milestone.
3. After each coherent controller change is executable and both Make dialects
   pass, run a matched baseline/candidate Stage A and require numeric movement,
   combat, perception, steal, conversion, capture, and defense regression
   thresholds rather than a generic "no regression" statement. Run every
   tuning trial in a disposable isolated game root on disjoint ports; it may
   not write the installed game or disturb the active production fleet.
4. Only after Stage A passes, run the team-decisions blind rung with at least
   300 seconds, exact roster parity, ordinary bot POV parity, authenticated
   stand fixture, sealed captions, fresh judges, and the required set
   composition on every qualifying map/panel.
5. Preserve the executable unique persona/name assignment check and prove from
   final receipts that both teams instantiated stable, non-mirrored rows;
   persona policy may not consume hidden opponent identity or information.
6. Preserve accepted carrier, escort, recover, intercept, radio, movement,
   combat, and public-information behavior under numeric regression gates.
7. After the final source is promoted, run the match-outcomes rung on aligned
   server log, authoritative stats, serverrecord, and ordinary POV artifacts
   sharing one capture receipt.

Acceptance:

- the production controller path changed and the matched trial attributes a
  measured improvement to it;
- final persistent-fleet cards use authoritative, normalized, stratified
  metrics and reconcile heuristic disagreements;
- final fleet receipts prove the actual production rosters instantiated the
  stable, non-mirrored persona assignments required by the executable test;
- defense motion and captures conceded are evaluated from the same receipts;
- qualification rules are met for every claimed map/panel rather than relying
  on the historical lmctf22 escort or mactf06 steal-only examples;
- production results have positive steals, conversions, captures, flag
  interactions, movement, combat, and clean team rosters;
- any trial that misses a frozen band is fixed or explicitly struck with
  retained counter-evidence before release.

### Item commitment

Required work:

- execute a committed item approach through the real `Touch_Item` pickup
  chain;
- prove a successful pickup closes the commitment before the item can be freed
  or retargeted;
- prove every non-pickup outcome explicitly retires or preserves the commitment
  for a valid stronger reason;
- retain item communication and public-information constraints.

Acceptance:

- an executable production-chain test covers success and every terminal
  non-pickup class;
- final persistent-fleet diagnostics contain no unresolved item commitment.

### Snag recovery

Required work:

- bind each retained observation/demo to the exact module, RUNE, BSP, map,
  player, and time window;
- run `stallcensus.py` on authenticated observations;
- deterministically attribute every usable stall to an exact RUNE seed or fail
  closed with retained diagnostics;
- build map-bound `.snag` repairs against the final exact RUNE identities and
  reprove affected runtime paths;
- package every applicable `.snag` file and reject silent missing-input
  fallback.

Acceptance:

- no observed cluster is silently discarded;
- repair count and geometry obey the runtime format bounds;
- affected maps pass structural, semantic, cold-load, and route gates again;
- the final manifest states exactly which maps have repairs and why.

### POV and spectator presentation

Required work:

- record a normal Yamagi `.dm2` through the real observer client;
- play the retained demo back successfully in Yamagi;
- preserve exact recording lifecycle and source identity;
- observe attributed voice/game sound through an actual spectator/PHS client;
- prove the emitting entity, channel, origin, recipient, and absence of the old
  world-entity misclassification.

Acceptance:

- fake or truncated demo fixtures cannot satisfy the production gate;
- recording, close/fsync receipt, playback, and spectator sound all pass on the
  final module;
- the exact top-20 persistent-fleet evidence retains the normal demo and
  analysis capture.

## lmctf58 route closure

The narrow DIRECT approach envelope now admits the four safe shallow-water
CellarDoor and CellarDoor2 pairings without widening dry, AUTO, BUTTON,
hazardous-liquid, or deep-water behavior. Each selected destination is checked
again at insertion, objective-core remains authoritative, all six cellar
identities retain valid plans, and the representative routes reach both flag
roots. The exact ten-controller checker, both independent C readers, Python
reader, lint, strict compiler and sanitizer gates, fresh generation, and a
separate fresh-process cold load are green. This map is closed; it is one member
of the still-pending final 181-map corpus, not a substitute for that corpus.

## Final source and dual build

The final source candidate is created only after the bot improvements and the
`lmctf58` route closure are integrated.

Required execution:

1. Freeze the exact source tree and generated contracts.
2. Build GNU and Make modules and acceptors independently in clean worktrees.
3. Run full host tests with GCC and Clang for both dialects.
4. Run strict warning gates, sanitizers, dependency/loader checks, exported
   `GetGameAPI`, and unresolved-symbol checks.
5. Prove both module aliases are byte-identical for the production candidate.
6. Freeze module, acceptor, configuration, tool, and source manifests.

Any later source change invalidates this candidate and every generated artifact
that depends on it.

## All 181 RUNE artifacts

The conversion authority is `tools/rune-corpus-maps.txt`. It must contain 181
unique safe names and both `lmctf02` and `lmctf02c`.

Required execution:

1. Create a read-only input snapshot from the final candidate, exact engine,
   production physics, the 181 BSPs, generator configuration, readers, linter,
   semantic checkers, and map manifest.
2. Use durable isolated roots outside the installed game and outside ephemeral
   temporary storage.
3. Use the renamed corpus engine and disjoint ports so the production fleet
   cannot mistake generation workers for its own servers.
4. Run the controller with eight workers and a 3600-second generation timeout.
5. For every map, require a newly written artifact, two valid objective roots,
   clean shutdown, GNU C acceptance, independent Make C acceptance, Python
   acceptance, root-aware lint, applicable map-specific semantics, and a fresh
   runtime cold load with one admitted bot.
6. Publish PASS only when all evidence paths and hashes validate.
7. Require exactly 181 PASS results. “Complete” with any failure classification
   is a failed project gate.
8. Freeze the complete corpus manifest, counts, roots, module/config identity,
   runtime logs, and cold-load results.

Failure loop:

- generator or graph failure returns to source/navigation repair;
- semantic or loader failure returns to format/binding/runtime repair;
- timeout returns to bounded performance work or a proved timeout adjustment;
- BSP/RUNE identity mismatch returns to corpus snapshot construction;
- after any source change, rebuild the candidate and regenerate every artifact
  needed for a consistent final corpus.

## Deterministic native map lifecycle

The fleet must use Quake's in-process map lifecycle. The launcher starts each
q2ded process once; `CheckDMRules` selects an end condition,
`BeginIntermission` records the next map, `ExitLevel` queues `gamemap`, and
`SpawnEntities` initializes the next level inside the same process.

Current source behavior:

- `maplist_file` is parsed inline by `InitGame()` in `g_save.c`;
- `MapList_Configure` preserves file order for the non-random branch and sorts
  only for `CTF_RANDOM_MAPS`;
- `MapList_SequentialStartup` keeps configured entry zero resident when it is
  already loaded and sets the next cursor to entry one;
- `MapList_SelectNext` selects the cursor entry and advances/wraps it without
  the obsolete `Maps_Picked`/`Last_Map` skip;
- startup `MAPLISTFORCE` aligns the current map to list entry zero;
- nonzero `timelimit` and `fraglimit` are the tracked native match-ending
  authorities; there is no tracked `capturelimit` consumer;
- `ExitLevel` changes maps and preserves the q2ded process; it does not exit.

Required source and runtime behavior:

1. Preserve the implemented non-`CTF_RANDOM_MAPS` behavior: configured order,
   sequential next-entry selection, and entry-19-to-entry-0 wrap without a new
   mode or cvar.
2. Keep the existing `CTF_RANDOM_MAPS` branch separate and unchanged except for
   tests that prove the ordered-branch correction did not leak into it.
3. Align startup to entry zero of each server's already-rotated list.
4. Use one source-backed native match limit for production. The current target
   is a 15-minute `timelimit`, with `fraglimit 0`; if the production policy is
   changed, the plan and tests change before cutover rather than silently
   changing the launcher.
5. Preserve bot clients, server ownership, and health observation across
   `ExitLevel`/`SpawnEntities`. POV recordings are map-local: require an exact
   close/finalize receipt before `ExitLevel` and a new recording authority after
   the next `SpawnEntities` rather than claiming one recording spans maps.

Executable acceptance:

- a deterministic list fixture remains byte-order exact after parsing;
- twenty map residences (initial entry 0 through entry 19) visit every entry
  exactly once; transition 20 returns to entry 0 for residence 21;
- each rotated server list begins at its required offset and retains the same
  cyclic order;
- random-mode behavior does not leak into deterministic mode;
- the same q2ded PID survives at least one full twenty-map cycle;
- a native transition cannot pass on wrong map, skipped map, duplicate map,
  premature process exit, or launcher-issued `quit`/`serverstop`.

## Production bundle, rollback, and fleet

The installer manages the complete release data while the runtime fleet uses
the exact selected top 20.

Required bundle:

- both identical module aliases;
- required client/runtime `assets/lmctf6-buzzmod.pak`;
- exact production `rune.cfg`, manifest-bound `sv sg` roster policy, and ten
  generated rotated map-list files;
- the authoritative ordered `tools/topmaps.txt` used to derive those files;
- all 181 BSP files and all 181 matching RUNE files;
- escape priors;
- every applicable snag and other runtime sidecar;
- a canonical manifest with source path, destination path, size, hash, format
  identity, and explicit inclusion/exclusion claims.

Required installer and rollback behavior:

- exact regular same-directory validators; no environment override;
- armed stop sentinel and exclusive deploy/launcher lock;
- exact owner, process, port, and quiescence census;
- same-filesystem staging, file and directory fsync, and pre-rename hash checks;
- complete prior-bundle backup and displaced-state provenance;
- all-file promotion while launchers are excluded;
- post-promotion positive and negative claim verification;
- exact full rollback after copy, rename, verification, interruption, or
  operator-requested rollback failure;
- no mixed module pair, partial map corpus, stale sidecar, stale rotated list,
  or orphaned staging path can be accepted.

Required fleet behavior:

- `GAME=lmctf` only for production entry points;
- ten persistent q2ded processes on the ten production ports;
- one coordinated unstaggered fleet start; no launcher-side per-server delay;
- nine 5v5 rosters and only s08 at 7v7, with no delayed mutation;
- ten files containing the same exact twenty maps in cyclic rotations 0..9;
- each server starts at its list entry zero and advances natively in-process;
- every map residence authenticates the source/module, BSP, RUNE, map name,
  physics, roster, positive activity, and map-local recording state;
- every transition authenticates expected old map, expected next map, unchanged
  owned q2ded PID, finalized old-map recording, newly authorized next-map
  recording, no second build identity, and continued bot admission;
- watchdog recovery is process- and release-owner scoped, bounded, and cannot
  resurrect a held or failed release.

Testing:

- corrupt, truncated, aliased, duplicate, extra, symlinked, and mismatched
  module/BSP/RUNE/config/sidecar/map-list inputs;
- lock and process-start races;
- failed second rename and failed final verification;
- interruption after every promotion boundary;
- incomplete and malicious rollback bundles;
- wrong map, skipped transition, repeated transition, wrong roster, zero bot,
  early exit, rejection banner, duplicate identity, missing demo receipt, and
  dirty shutdown;
- direct deterministic map-list and persistent-process integration tests;
- focused code review and failure-injection tests for the transactional
  installer and rollback paths.

## Promotion and production fleet operation

Promotion begins only after bot/source, all-181 corpus, native-map lifecycle,
and fleet/bundle gates are green on one frozen release candidate.

Execution:

1. Arm the stop and watchdog hold without modifying scripts being read by the
   running old fleet.
2. Let the exact owned old waveloop and every child exit naturally.
3. Prove no sanctioned launcher, q2ded process, or production port remains.
4. Back up and transactionally install the complete manifest-bound bundle.
5. Re-read all installed hashes and cold-load every top-20 map from the
   installed bundle.
6. Start ten q2ded processes once, with rotated list offsets zero through nine.
7. Observe the same ten owned PIDs through native map changes. Do not restart
   them to manufacture successful map evidence.
8. Require every server to complete one full twenty-map cycle with the expected
   order, RUNE identity, roster, gameplay activity, per-map recording receipts,
   and no rejection/fatal diagnostic.
9. Aggregate movement, hook, combat, team, defense, item, snag, POV, sound, and
   match-outcome evidence from those promoted bytes.
10. On any deployment, startup, transition, or runtime-health failure, hold the
    fleet, roll back the complete bundle when deployment ownership requires it,
    repair the owning source/data/tool, and repeat every invalidated gate.

## Repository, history, documentation, and release

Documentation and necessity audit:

1. Inventory every tracked path and every relevant untracked path, explicitly
   including authored C/H, Python/shell tools, build/project/workflow files,
   configs, documents, fixtures, datasets, generated artifacts, diagnostics,
   vendored source, the binary PAK, and evidence directories.
2. For every item, record its authoring source or producer, reproducible build
   command, direct C/Python/runtime consumers, build/workflow/launcher/test/docs
   and operator references, and classify it as current authority, executable
   input, test fixture, generated output, historical record, duplicate,
   obsolete, or unexplained.
3. Verify current-authority documents line by line against the implemented
   source, commands, paths, map counts, fleet lifecycle, formats, and release
   behavior. Update them in the same milestone as the behavior they describe.
4. Keep historical records only when they provide useful provenance and label
   them unmistakably as historical. Git history is sufficient for obsolete
   working plans and duplicate snapshots.
5. Remove obsolete or duplicate documents, dead scripts, stale diagnostics,
   unexplained generated outputs, and superseded configs only after proving
   that no direct runtime/tool/data consumer, producer/reproducer path, build,
   workflow, launcher, test, documentation link, or operator command requires
   them.
6. Preserve required raw evidence outside Git until its concise manifest and
   hashes are accepted; then remove repository-local residue from an explicit
   reviewed path inventory.

Known cleanup candidates that require classification:

- the retired implementation-roadmap stub versus this authoritative plan;
- the untracked duplicate `docs-layout-isa.md`;
- untracked `.diag-*.gdb` scripts whose source-line probes are stale;
- generated `*.gnu`/`*.make` test binaries, Python bytecode, POV temporary
  trees, auxiliary launch logs, and rune-log trees;
- development scripts/configs still defaulting to `lmctf-hooktest` versus the
  deliberately production-only entry points;
- reference datasets with no tracked producer or consumer;
- documentation that still describes SLIPGATE as unreleased, names obsolete
  paths, reports 180 maps, describes process-per-map waves, or claims runes are
  only server-generated when the final bundle ships them.

Required repository cleanup:

- keep runtime-session cache entries untracked through a narrow ignore;
- add narrow ignores for known generated binaries, diagnostics, bytecode,
  temporary POV trees, and rune log roots;
- preserve final evidence outside Git and commit only concise manifests;
- stage from an explicit reviewed file list, never `git add -A`;
- reconcile README, SLIPGATE, RELEASES, LEDGER, ARCHITECTURE, TOOLING,
  `tools/README.md`, the RUNE contract, and the retired-roadmap redirect with
  actual final behavior; obsolete migration notes belong in Git history, not
  the final working documentation.
- run link/path/reference checks across all retained Markdown and operator
  commands, and require every named file/target to exist in the final tree;
- rerun the complete necessity inventory after the final runtime acceptance so
  no temporary evidence or superseded tool is accidentally shipped.

Required commit and merge cadence:

1. Each coherent independently reviewed milestone is committed to `slipgate`
   and pushed immediately.
2. The exact pushed `slipgate` SHA must pass version, Linux, Windows x86,
   Windows x64, GNU/Make GCC, and GNU/Make Clang jobs.
3. Every green milestone is merged into `main` with a no-fast-forward merge.
4. The merge tree must exactly equal that `slipgate` milestone.
5. The exact pushed `main` merge SHA must pass the same jobs.
6. Commits are never backdated, rewritten, or withheld to manufacture an
   activity history.

Final release:

1. Before the final source freeze, update the release workflow, publication
   handoff tool, and tests for the complete declared release contract: the three
   platform modules, required PAK, canonical server-bundle archive, release
   manifest, VERSION, and checksums covering every published payload. The
   workflow must no longer claim that accepted RUNEs are regenerated implicitly
   on the server.
2. Set an honest SemVer. Use `1.0.0` only after every gate in this plan is
   complete.
3. Make the final no-fast-forward release merge into `main` and verify its
   tree.
4. Push `main` and require exact-SHA CI.
5. Create and push the matching annotated version tag on the final `main`
   merge.
6. Require the tag run, including Publish release, to succeed.
7. Download every asset named by the release contract, including the canonical
   server bundle and release manifest.
8. Verify `VERSION`, `sha256sum -c SHA256SUMS`, the server-bundle manifest, and
   its equality with the installed and accepted production bundle.
9. Record final source, corpus, bundle, persistent-fleet, CI, tag, and release
   identities in the external acceptance/provenance manifest. The tracked
   ledger records the pre-freeze plan and expected evidence fields, not IDs that
   do not exist until after the tracked tree is frozen.

## Requirement-to-evidence matrix

| Requirement | Authoritative input | Required output | Evidence that closes it |
| --- | --- | --- | --- |
| Bot quality | final source, identity-bound human/bot captures | adopted behavior and frozen metrics | executable consumer tests plus promoted persistent-fleet results inside frozen bands |
| Ordered native map lifecycle | `tools/topmaps.txt`, deterministic map-list mode | ten cyclic rotations, offsets 0..9 | same ten PIDs traverse twenty exact ordered maps and wrap correctly |
| All-map conversion | exact 181-name manifest, 181 BSPs, final generator/module | 181 newly generated RUNEs | 181/181 dual-C/Python/lint/semantic/cold-load PASS rows |
| `lmctf58` | final BSP/RUNE and exact ten-controller contract | all ten live DIRECT controller routes | each identity retained, non-tombstoned, and reverse-reachable from both flag roots |
| Snag repair | authenticated demo receipts and accepted RUNEs | exact applicable `.snag` files | deterministic attribution, format validation, runtime consumption, no silent missing input |
| POV/sound | promoted module, observer client, retained recording | playable normal demo and attributed spectator sound | real Yamagi playback and observer/PHS receipt, not a source-text or fake-file check |
| Bundle/install | final modules/configs/181 BSP-RUNE pairs/sidecars/lists | canonical bundle and rollback receipt | adversarial transaction suite plus installed post-verification and exact rollback |
| Production fleet | installed bundle and ten owned ports/processes | continuous ten-server operation | full twenty-map native cycle per unchanged PID with roster/activity/identity gates |
| Repository/release | final source tree, canonical server bundle, and acceptance manifest | coherent branches, tag, complete declared release assets | exact-SHA CI on slipgate/main/tag and downloaded SHA256SUMS/bundle-manifest audit |

## Dependency graph

```text
P0  authoritative plan + current-branch reconciliation
 |
 +--------------------+----------------------+----------------------+
 |                    |                      |                      |
 v                    v                      v                      v
B0 production bot    N0 navigation/tools    F0 fleet lifecycle     Q0 CI/repo hygiene
controller fixes     181 authority fixes    ordered map lists      warning/test gaps
 |                    |                      bundle/rollback         |
 v                    +----------+-----------+----------+-----------+
B1 executable tests +            |                      |
matched acceptance gates         v                      v
for changed behavior        N1 map regressions     F1 persistent ten-server
 |                                                  launcher + health
 +-------------+-------------+                      |
               |                                    |
               v                                    |
        S0 integrated source candidate <------------+
               |
               v
        S1 both Make dialects x GCC/Clang
        + Windows x86/x64 + strict + sanitizers + full host
               |
               v
        R0 frozen module/readers/generator/config/181 BSP inputs
               |
               v
        R1 generate every one of 181 RUNEs
               |
               v
        R2 181 GNU-reader + Make-reader + Python + lint
        + semantic check + fresh-process cold-load PASS
               |
               +--------------------------+
               |                          |
               v                          v
        D0 final snag/sidecar       F2 final rotated top-20
        attribution bound to       lists + bundle manifest
        exact RUNE identities             |
               |                          |
               +-------------+------------+
                             v
                    BUNDLE0 complete authenticated bundle
                             |
                             v
                    CUT0 quiescent transactional install
                             |
                             v
                    LIVE0 ten persistent q2ded processes
                             |
                             v
                    LIVE1 each PID completes 20 ordered maps
                             |
                             v
                    OUT0 bot/gameplay/POV/outcome acceptance
                             |
                             v
                    REL0 docs + version + provenance + cleanup
                             |
                             v
                    REL1 slipgate CI -> main merge CI
                             |
                             v
                    REL2 tag -> publish -> downloaded-asset audit
```

### Artifact and evidence invalidation graph

```text
C/H/Python/tool/config change
  -> source candidate invalid
  -> module/readers/generator invalid
  -> generated RUNE corpus invalid where semantics can change
  -> RUNE-bound sidecars and bundle manifest invalid
  -> cold-load and production-runtime evidence invalid

BSP change
  -> that map's RUNE + semantic checks + sidecars + bundle + runtime evidence

RUNE change
  -> that map's readers + semantic checks + sidecars + cold load + bundle
     + production-runtime evidence

fleet/config/map-list change
  -> fleet tests + installed manifest + persistent-cycle/runtime evidence
  -> does not invalidate unrelated compiled bot tests or RUNE generation

documentation-only tracked change
  -> new commit/revision identity
  -> docs checks + branch CI + return to final source freeze before generation
```

### Branch and release graph

```text
coherent reviewed milestone
  -> commit on slipgate
  -> push exact SHA
  -> exact slipgate CI green
  -> no-ff merge to main, assert merge tree == slipgate tree
  -> push exact main SHA
  -> exact main CI green

final green main merge
  -> annotated SemVer tag
  -> tag CI + publish green
  -> download the complete declared public asset set
  -> VERSION + SHA256SUMS + server-bundle-manifest verification
```

## Execution strategy

Work proceeds in gated phases. Parallel work is allowed only where the graph
shows independent inputs. A passing child task does not advance a parent gate.

### Phase 0 — make this plan authoritative

1. Finish the complete source/history/private-change read and record the
   source-grounded gap inventory in this file.
2. Reconcile the current plan and 181-controller work onto the latest
   `origin/slipgate` without losing the already-pushed source history.
3. Run plan/controller tests, independently review the diff, commit it, push
   `slipgate`, merge it into `main`, and require exact-SHA CI on both.
4. From then on, update this checklist in the same milestone commit that
   changes its facts; do not maintain a detached or `/tmp` plan.

### Phase 1 — four parallel implementation lanes

1. **Bot-quality lane:** establish final human/bot baselines, then implement
   movement, route selection, hooks, combat/perception, team/defense/outcome,
   item, POV, sound, and snag consumer improvements with executable tests.
2. **Navigation/corpus lane:** repair the authored Python-tool defects, finish
   the 181 controller and dual-reader/semantic/cold-load gates, identify and
   fix the post-approach lmctf58 graph loss, and close all map-specific tests.
3. **Persistent-fleet lane:** implement deterministic ordered map-list source
   behavior, ten rotated lists, persistent launch/ownership, cross-map health,
   complete bundle transaction, and exact rollback.
4. **Integration lane:** review and test each completed lane, run both build
   dialects and cross-platform CI, make coherent commits, push them, and merge
   each green milestone to `main` rather than accumulating invisible private
   work for days. It also owns the documentation/necessity inventory and removes
   proven trash in small reviewable commits. Before Phase 2 it also reconciles
   all tracked documentation, sets the release version, updates the publishing
   workflow/asset contract, and commits every tree-changing cleanup.

Phase 1 finishes only when all four lanes meet at one integrated source tree.

### Phase 2 — freeze and prove the final source candidate

1. Merge all adopted bot, navigation, tooling, map-lifecycle, fleet, CI,
   data-format, documentation, necessity-cleanup, version, and release-workflow
   work into one candidate.
2. Run GNUmakefile and Makefile under GCC and Clang, full host tests, strict
   diagnostics, sanitizers, module link/load/export checks, and Windows x86/x64
   CI.
3. Review the complete diff and repair every confirmed defect.
4. Freeze the exact tracked tree and commit plus module aliases, readers,
   generator, engine, configs, controller, semantic checkers, and 181 BSP
   inputs. No tracked file changes are permitted after this point; any required
   correction returns to Phase 1/2 and invalidates downstream evidence.

### Phase 3 — convert and accept all 181 maps

1. Generate all 181 artifacts with bounded parallel workers and per-map durable
   logs; do not stop at the top 20 or at a controller-summary success flag.
2. For every map run both independent C readers, Python reader, root-aware
   lint, applicable semantic checker, and a fresh-process runtime cold load.
3. Classify each failure by owning source/tool/data layer, repair it, rebuild
   the candidate, and regenerate every invalidated artifact.
4. Repeat until the terminal table is exactly 181 PASS and zero failure,
   timeout, missing, stale, or waived rows.
5. Freeze the accepted 181-map manifest.

### Phase 4 — finish deployable data and the release bundle

1. Attribute retained stall observations to the exact final RUNE identities and
   build all applicable `.snag` files; reject missing provenance or ambiguous
   attribution.
2. Re-run affected readers, semantics, and cold loads.
3. Derive ten rotated top-20 lists from the exact ordered authority.
4. Build the complete manifest-bound bundle and exercise success, interruption,
   corruption, race, partial-promotion, post-verification, and manual rollback
   paths against a disposable install root.
5. Freeze the exact accepted bundle archive and manifest in a durable
   content-addressed staging location outside Git. Hash them before and after
   upload/staging. The later tag/release process consumes these exact bytes; it
   may not rebuild or substitute the bundle.

### Phase 5 — quiescent cutover and persistent fleet acceptance

1. Hold the old launch authority and allow its owned processes to stop.
2. Prove quiescence, transactionally install, post-verify, and cold-load the
   installed top-20 artifacts.
3. Start the ten production q2ded processes once.
4. Observe each PID through twenty ordered map residences plus transition 20
   back to entry zero, while validating every map/RUNE identity, roster,
   activity, map-local recording close/reopen, and cross-map bot lifecycle.
5. Collect and evaluate the final movement, combat, hook, team, defense, item,
   snag, POV, sound, and match-outcome evidence. Repair and repeat any failed
   owning gate; do not waive a failed map or outcome.

### Phase 6 — finish the repository and publish

1. Do not change the frozen tracked tree. Generate the final acceptance and
   provenance manifest outside Git from the frozen commit, corpus, installed
   bundle, persistent-fleet, CI, and runtime evidence.
2. Confirm the already-frozen documentation/version/workflow remain accurate;
   any required edit returns to Phase 1/2 and repeats downstream work.
3. Complete the final `slipgate` exact-SHA CI if it is not already green.
4. Merge that exact frozen tree to `main`, assert identical trees, push, and
   require exact-SHA CI.
5. Tag the final main merge, require publish CI, download the complete declared
   module/PAK asset set, then use the already-frozen publication handoff tool to
   upload the exact content-addressed server bundle, bundle manifest, and
   external acceptance/provenance manifest from Phase 4/5. Regenerate the final
   complete checksum asset only from the downloaded/uploaded release payloads,
   then verify VERSION, all checksums, and equality with the installed accepted
   bundle.

## Failure and revalidation rules

- A failed check returns to the component that owns the failure; downstream work
  continues only where its inputs remain valid.
- A source change invalidates modules and every generated artifact built from
  them.
- A RUNE change invalidates its semantic check, cold load, bundle manifest, and
  any snag file bound to it.
- A fleet-tool change invalidates its focused tests and promotion dry run,
  but not source-level bot tests.
- A config or map-list change invalidates every affected server-cycle proof.
- A module, RUNE, BSP, sidecar, or config change after cutover invalidates the
  whole installed-bundle manifest and the affected persistent-cycle evidence.
- A documentation-only tracked change creates a new revision identity and is
  completed before the final source freeze. Any such change after Phase 2
  returns to the freeze; it is not exempted merely because C source is
  unchanged.
- Expected failures are never counted as PASS, waived, or hidden by continuing
  to downstream work.

## Live completion checklist

- [x] One current RUNE format and runtime contract.
- [x] Cross-platform warning fixes and immutable Node 24 workflow.
- [x] `lmctf44` regenerated and independently cold-loaded.
- [x] Shallow-water DIRECT door law committed and green.
- [x] The source/tooling milestones preceding this status refresh—the 181
      authority/controller, map-list fix, telemetry consumers, strict readers,
      documentation hygiene, and first reviewed repository cleanup—are integrated
      through the completed branch pair
      `6523af5`/`efc30fd`; exact-SHA CI runs `32234992472` and `32235013292`
      are green.
- [x] Tracked corpus list contains exactly 181 unique maps, including both
      `lmctf02` and `lmctf02c`.
- [x] Corpus controller proves generator, dual C readers, Python reader, lint,
      applicable semantics, and fresh cold load for every terminal PASS.
- [x] Production role-telemetry consumers parse `seed/goal/sgoal/spd`, use
      stable goals where required, and fail when no SG rows are recognized.
- [x] Non-random map-list parsing preserves configured order and its executable
      tests cover startup, wrap, singleton, and random-branch isolation.
- [x] `runeio.py --expected-identity REFERENCE_RUNE ARTIFACT` and
      `corpusgraph.py` reader defects repaired and adversarially regression-tested.
- [x] All ten `lmctf58` door controllers are retained, including the four
      lower cellar identities; independent readers, semantic acceptance,
      sanitizers, fresh generation, and fresh cold load are green.
- [x] Runtime `.snag` format 2 binds the exact RUNE SHA and evidence SHA;
      cold-load attestation, explicit 181-map bootstrap construction, strict
      residence analysis, two-source stall attribution, and immutable final
      corpus construction have executable source tests. Final production
      sidecars remain gated on the accepted 181 corpus and persistent cycle.
- [ ] Real native cyclic transitions preserve the same q2ded PID for a complete
      20-map cycle; source-level startup/wrap/random-isolation tests are green.
- [ ] Human/bot measurement data is bound to exact module/BSP/RUNE provenance
      where used; historical unstamped data is not presented as final proof.
- [ ] Bot improvement backlog measured, resolved, and consumer-proven across
      movement, routes, hooks, combat/perception, team/defense/outcomes, items,
      snag, POV, and sound.
- [ ] Final dual GNU/Make candidate frozen.
- [ ] All 181 maps generated with exactly 181 PASS results.
- [ ] All 181 artifacts pass independent readers, lint, semantics, and cold load.
- [ ] Final snag/sidecar set is provenance-bound to the accepted RUNE corpus.
- [ ] Complete bundle installer and rollback pass code review and explicit
      failure-injection tests.
- [ ] Ten rotated exact top-20 lists, persistent launcher, cross-map health, and
      watchdog ownership pass direct source tests and a real full-cycle run.
- [ ] Full bundle promoted while quiescent and post-verified.
- [ ] Ten persistent production q2ded PIDs each complete an ordered 20-map
      native cycle with offsets 0..9 and healthy rosters/activity.
- [ ] Movement, hook, combat, team, defense, item, snag, POV, and sound runtime
      proofs pass on the promoted bytes.
- [ ] Repository cleanup, documentation, and acceptance manifest complete.
- [ ] Every retained document/path/command matches the final implementation;
      every removed item has no remaining consumer or required provenance role.
- [ ] Final `slipgate` and `main` exact-SHA CI green.
- [ ] Final SemVer tag publishes and downloaded assets verify.
