#!/usr/bin/env bash
#
# iterate2.sh <name>
#
# Free-layout wave launcher (owner's format delegation, 2026-08-04:
# "everything is up to you. only constraint is 1) 10 servers always,
# and 2) never stop.") Layout, matchups (xvx or xvy), maps, and match
# time are set per server in the tables below; different servers may
# run different tests in the same wave, each server's test kept clean.
#
# Everything the fixed-format launcher learned the hard way is kept:
# PID-only process discipline, ~7s launch stagger (same-second starts
# duplicate Q2's RNG), flags travel by per-server cfg (argv ceiling),
# maplist_file defused (the deferred-gamemap join bomb), overlap guard.

set -u

Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
GAMEDIR_ROOT="${GAMEDIR_ROOT:-$HOME/Games/Quake2}"
GAME="${GAME:-lmctf-hooktest}"
CFG="${CFG:-rune.cfg}"
PORT_BASE="${PORT_BASE:-28520}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME="$1"
LOG_DIR="$SCRIPT_DIR/iter-$NAME"
mkdir -p "$LOG_DIR"

if pgrep -x q2ded >/dev/null 2>&1; then
    echo "q2ded already running; refusing to overlap fleets" >&2
    exit 3
fi

# ---------------------------------------------------------------- layout
# One column per server. FILL is sv_botfill ("N" or "R:B" asymmetric).
# SECS is the game window; timelimit is set to 0 in the cfg so nothing
# ends a long game early -- the pipe's quit ends the window.
# same-map A/B pairs: s03/s04 (5v5 lmctf22, escape off/on) and s06/s07
# (5v3 lmctf44 carry farms, escape on/off). Escape corpus is strongest on
# lmctf22/lmctf44/mactf06/smap05 -- the test runs where the data lives.
# 298: s08 becomes the defense-package OFF comparator on mactf06 --
# s05's zero-conceded read was confounded by map identity (mactf06
# concedes ~nothing regardless); the pair settles it. 7v7 shelved.
# 301: defense A/B moves to the farms (they concede 1-7/wave -- the
# sensitive instrument; mactf06 concedes nothing to anyone). s08 back
# to 7v7 density coverage.
# v4 canaries: s02 = the PERMANENT no-opposition canary (5v0 smap05 --
# flawless execution on film or the build is broken); s10 = the fixed-
# matchup canary (5v1 smap05, must hold its band).
LABELS=(s01-2v2  s02-5v0  s03-5v5  s04-5v5  s05-5v5   s06-5v3  s07-5v3   s08-7v7  s09-ctrl s10-5v5)
# lane A/B moved to mactf06 (coverage 29/48 -- THE lane map; lmctf22
# reads 12/48, too weak to show the doctrine). lmctf22 keeps coverage
# on s05.
# s06/s07 on lmctf44 for the MEGAWORTH A/B (2 megas, no quad -- the
# clean mega environment; the comm stack is adopted fleet-wide so the
# quad-map duty is done). BSP census is the map-picker's law now.
MAPS=(  lmctf03  smap05   mactf06  mactf06  lmctf22   lmctf44  lmctf44   lmctf09  lmctf01  mactf06)
FILLS=( "2"      "5:0"    "5"      "5"      "5"       "5:3"    "5:3"     "7"      "5"      "5")
SECS=(  600      600      900      900      900       900      900       600      900      900)
# 295 relayout: ONE variable per pair (284-294 stacked escape+movement
# trials on shared servers -- reads were cross-contaminated).
# s03/s04: fandense dose-2 A/B (escape OFF both).
# s06/s07: escape A/B alone (landtick OFF both).
# s05: defpost+defreact together (defense package vs history).
# escape prior NULL on clean pairs (295-299: 16 ON vs 18 OFF). Ledger:
# the contaminated-era 13v7 was noise. The corpus cut survives for
# future re-derivation (windows were hunter-biased at the SEED level?
# open question) but the flag is parked.
ESCAPE=(0        0        0        0        0         0        0         0        0        0)
# duel-roles A/B (285+): s01 ON s02 OFF -- breaks the size==2 dw=1 pin
# (2v2 census: dw stuck 131/138, zero caps in 16 waves)
# duelroles NULL (285-299 pooled: 7 ON vs 10 OFF, zero caps both) --
# the dw pin was not the binding constraint at duel density. Ledger.
# duel-roles ADOPTED wave 332 (aux verdict 7 caps to 0 over three
# rounds -- the old null was the broken-carrier era's artifact)
DUEL=(  1        1        0        0        0         0        0         0        0        0)
# defense dwell A/B (286+): s05 ON vs s03 OFF (both have .dpo data);
# landing-tick A/B (286+): s06 ON vs s07 OFF (farms feel it most --
# read = touch_loss and relaunch rate from the serverrecord demos,
# plus carrier speed).
# defense frontier re-trial (333+): field-mode on s03 vs s04 OFF --
# the old null is void (measured on broken offense). Read: steals
# CONCEDED on the pair (defense quality), caps as cost column.
# defpost PARKED permanently (2x2: posts concede 28-30 vs 18-21 --
# corpus posts pull defenders off the stand and bots punish it)
DEFPOST=(0       0        0        0        0         0        0         0        0        0)
# defreact ADOPTED fleet-wide wave 337 (react-only = fewest conceded)
DEFREACT=(3      3        3        3        3         3        3         3        0        3)
LANDTICK=(0      0        0        0        0         0        0         0        0        0)
# link latch A/B (290+): s04 ON vs s03 OFF (5v5 pair, demos on s03) --
# read = turn1hz/reversals/180s from botkin, plus steals as the cost
# column. 700ms latch ~= the 1Hz surface refresh with margin.
# link latch NULL (290-291): turn grammar unchanged -- the incumbent
# dies at every seed crossing, same within-seed-only trap as sticky.
# 385 trial: the smap05 map-center orbit (wave 383 canary, 11 steals 1 cap)
# is two seeds flapping on a field plateau at full sprint until the 15s
# stuck self-kill -- the chronic ~130 suicides/wave burn, flat since at
# least wave 340. The latch was built for exactly this (wave 289) and has
# been dark since tactics won the WAYPOINT-level verdict; this is the
# LINK-level residue. 600ms = the field's own 1Hz refresh cadence, on the
# A arms of the matched pairs; canaries and clean control untouched.
# LATCH: NULL, retired (pooled 385-390 matched pairs 50 vs 53; wave
# 385's 7v17 was noise). Ledgered with film. The orbit trap's real fix
# is NOBACKTRACK below.
LINKLATCH=(0     0        0        0        0         0        0         0        0        0)

# 392 trial: the do-si-do killer. Prices the immediate-return link for
# 3s after leaving a seed. s06 armed vs s07 control; metric = suicides
# on the pair + the 5v0 canary's failed-carry count.
# ADOPTED wave 396 (pooled 392-395: 29 vs 39 suicides on the matched
# pair, right direction all four waves -- the do-si-do killer works, and
# two set-#3 judges saw the orbit it kills on film). s09 stays clean.
NOBACKTRACK=(60  60       60       60       60        60       60        60       0        60)
# weave A/B (292+): s04 no-weave vs s03 weave -- the demo census ranked
# the metronomic combat weave the #1 visible jank; sg_noweave is the
# existing switch and botkin the honest instrument the old gauges never
# were. Read: turn1hz/reversals/180s + steals as the cost column.
# noweave NULL on the demo instrument (292-293 pooled: turn metrics and
# steals even) -- the 176 null stands, honestly measured this time.
NOWEAVE=(0       0        0        0        0         0        0         0        0        0)
# fan densify A/B (294+): s04 ON vs s03 OFF -- read = wallbumps_per_min
# fandense NULL at both doses (294-295: bumps flat, dose-2 speed cost).
FANDENSE=(0      0        0        0        0         0        0         0        0        0)
# air-gain fix A/B (296+): s04 ON vs s03 OFF -- SG_Strafe derived its
# air angle from wishspeed 300 against an engine that clamps air
# wishspeed to 30 (pmove.c:382). Read: air_gain_med from botkin.
# airgain PARKED (296-297: negative at both doses -- the harvest turns
# velocity off the route; needs view/path co-rotation, ledgered).
AIRGAIN=(0       0        0        0        0         0        0         0        0        0)
# wetwork A/B (300+): s04 ON vs s03 OFF -- swimmers are half-speed rail
# targets and rails pierce water; read = DMG mod-11 kills on waterlevel
# targets + steals. Water maps matter: lmctf22 has the canal.
WETWORK=(1       1        1        1        1         1        1         1        0        0)
# nadelead A/B (309+): s04 ON vs s03 OFF -- cooked grenade re-aims at
# an airborne live enemy's touchdown while cooking (rocket landlead's
# solver, better fuse). Read: NADEPOP proximity + grenade obituaries.
# nadelead ADOPTED wave 335 (6 aux rounds: zero cost, slight lean --
# the owner's zero-cost-volume economy: free attempts at max volume)
NADELEAD=(1      1        1        1        1         1        1         1        0        1)
# courier dose 2 (310+): escorts out-price defenders for resist/regen
# (starvation fix for the adopted handoff: 10,098 candidates, 40 tosses,
# 74% of games form no pairing at all). Read is within-arm: RTCAND
# distance distribution + RUNETOSS conversions, fleet history as base.
RUNEDOSE=(0      0        2        2        2         2        2         0        0        0)
# pure pursuit A/B (311+): value = arc distance. s03/s04/s06 at 300,
# s07 + s09 control. Read: turn1hz_med (predict 92->55-65), reversals
# (50->38-42%), wallbumps as the corner-cut guard-rail, steals/caps.
# 313: s04 becomes the honest OFF comparator (311 ran the 5v5 pair
# ON/ON by mistake -- no contrast); pursuitz 18 lifts the stair veto.
# pursuit PARKED (311+313: chord active 92% of ticks, body grammar
# unchanged 83.5v85.3 -- command-level fix real, body noise lives in
# the fan/combat/hook layers; falsifiable prediction failed honestly).
PURSUIT=(0       0        0        0        0         0        0         0        0        0)
PURSUITZ=(8      8        8        8        8         8        8         8        8        8)
# approach cover A/B (314+): s03 ON vs s04 OFF, s06 ON vs s07 OFF --
# attackers price steps visible to fresh sightings near the target
# stand. Read: steals (should HOLD or rise -- covered approach means
# arriving alive), carrier survival post-grab, early-kill share.
# appcover ADOPTED fleet-wide wave 333 (healthy-era reval 28v18 caps)
APPCOVER=(200    200      200      200      200       200      200       200      0        200)
# exit-escort A/B (319+): interpose dose 2 -- escorts occupy the exit
# seed ahead of the carrier on its homeward field instead of the
# unreachable midpoint. s03/s06 ON (dose 2), s04/s07 stock interpose.
# formation NULL (326-329: 15v14, flat) -- and the pair was doubly
# loaded with appcover. Parked; midpoint stays. s03/s04 and s06/s07
# are now clean APPCOVER revalidation pairs (ON=200 vs OFF).
INTERDOSE=(0     0        0        0        0         0        0         0        0        0)
# owner's blend (321+): sg_smooth value = slew deg/s. s04 ON 240 vs
# s03 OFF (5v5 pair, demos both) -- read: turn1hz/reversals/steals.
# blend PARKED (321-322: ON worse both waves -- slew lag fights the fan)
# gentle-blend residue test (337+): 500 deg/s on s03 vs s04 OFF,
# demos both -- only adopts if post-tactics jitter still shows
SMOOTHDOSE=(0    0        0        0        0         0        0         0        0        0)
# rail-lane A/B (345+): s03 ON vs s04 OFF -- second defender holds the
# computed sightline post. Read: steals CONCEDED per arm + the per-map
# lane-coverage print (low coverage = geometry has no lane; gate later).
RAILLANE=(1      1        1        1        1         1        1         1        0        1)
# THE RIBBON (351+): the calibrated blind judge's first verdict, 8/8 --
# "rope vs brush": every bot traversal lands on the same polyline.
# Per-leg persistent lateral offset (value = max units). A/B s03 ON
# vs s04 OFF; the verdict is the NEXT FILM, not a number.
# ribbon NULL, both versions and doses (351-357 pooled 32.8v34.1u):
# aim offsets are re-centered by the steering stack. Ledger. The band
# is ROUTE diversity -- see ROUTEJITTER.
# ribbon RESURRECTED (376+): its "null" was measured on the morgue'd
# corridor-std metric -- but the v1 film showed railroad lanes, i.e.
# the mechanism DOES move the body. The fair judges' last tell is the
# needle-over-empty-base, exactly what a drifting per-traversal offset
# fills. A/B s03 ON (48u drift) vs s04 control; verdict on parity film
# needle shape, not on any single number.
# ADOPTED wave 380 (pooled 377/378/379, three independent blind readers):
# control arm carried the needle-over-empty-base corridors every wave
# (3-4/2/2 vs 2/1/1) and every extreme lane concentration (43%, 25%).
# s04 + s09 stay 0 as the standing controls, same pattern as ROUTEJITTER.
# s04 equalized to fleet values 2026-08-07: its ribbon/jitter-0 control
# state silently confounded every film-pair A/B (the dead breather flag,
# the shelf trial's arms). s09 is the ONLY control from here on.
RIBBON=(48       48       48       48       48        48       48        48       0        48)
# route jitter (359+): per-bot-per-life deterministic pricing tilt
# (value = max percent). Near-ties split the population across roads;
# a life rides one opinion of the map. A/B s03 ON vs s04 OFF; read =
# corridor band width on film (human bar 69u).
# jitter ADOPTED at dose 8 (pooled verdict: 2.41 bits vs control 1.99
# vs dose-20 2.21; pooled humans 2.62). s04 stays the clean control.
ROUTEJITTER=(8   8        8        8        8         8        8         8        0        8)

# EXIT-LANE ASYMMETRY (379 trial, ruled 2026-08-05): a human tends to
# leave with the flag by a different road than the one ridden in -- but
# not always. Coin per carry at dose%; armed carries price the inbound
# links 1.5x. s06 armed vs s07 control (the 5v3 pair: carry-rich).
# ADOPTED (2026-08-06, 30-wave pool: comm 36% vs ctrl 34% conversion,
# cap-positive, every subsystem verified live). The communication
# economy runs fleet-wide except the clean control; the freed pair
# carries MEGAWORTH next (stage-2 volume program lever 2).
COMMSTACK=(1     1        1        1        1         1        1         1        0        1)

# megaworth PARKED (11-wave verdict: 17% vs 24% conversion -- the
# detour costs more than the overheal buys at these gates). Honest
# negative to the ledger with its film; retriable with a tighter
# detour budget (4000ms was too generous) or attack-role-only gating.
MEGAWORTH=(0     0        0        0        0         0        0         0        0        0)

# stage-2 lever 3: attack-objective commitment. sg_atkobj is percent
# (100 = shipped). 125 on s06 vs 100 control: do attackers who price
# the flag 25% harder steal at human volume without dying broke?
# ADOPTED at 125 (22 A/B waves across doses: pooled +7% steals, caps
# dead even; 150 converged null -- the mild dose is the real one).
# Free volume, right film direction; the 2.2x gap needs a different
# class of lever (steal-genesis study queued).
ATKOBJ=(125    125      125      125      125       125      125       125      100      125)

# wswitch A/B takes the pair: the fight-sheet's switch-diagonal scalar
# is the primary instrument (humans commit to a gun; bots alternate).
# STRUCK 2026-08-07 (40 waves each arm, fightsheet pooled): wswitch=1
# moved switch_diagonal_mass 0.696 -> 0.676, AWAY from the human anchor
# (0.897 on lmctf44; commitment is the tell and HIGH is human), caps 40
# -> 35. The named rung-3 gap is commitment (bot 0.68 vs human 0.90) --
# the successor feature must SUPPRESS mid-fight switches, not add them.
WSWITCH=(0     0        0        0        0         0        0         0        0        0)

# WEAPON COMMITMENT: ADOPTED fleet-wide 2026-08-07 (12 waves/arm,
# lmctf44): switch_diagonal 0.828 armed vs 0.691 control vs 0.897 human
# -- two-thirds of the strongest rung-3 tell closed, zero overlap
# between arms -- and caps 25 vs 13 (committed guns convert; less
# mid-fight spectator time). s09-ctrl stays 0. Residual 0.83 -> 0.90
# gap stays on the rung-3 list.
# RUNG-3 SET #1 (2026-08-07): FAILED 18/18. Tell #1 on every bot sheet,
# every judge: blaster-dominated matches with machine accuracy -- the
# adopted commitment keeps the SPAWN gun. Mode 2 (s06) refuses to
# commit to the blaster; s07 stays mode 1 as control. Behind it, ranked:
# metronomic cadence bins (rail 1.7s razor spike), pegged fight ranges,
# empty arsenal rows, straight-in approach rose.
WCOMMIT=(1     1        1        1        1         2        1         1        0        1)

# RUNG-2 SET #1 (2026-08-07, judges 11/18): the named tell is OFF-GRAPH
# FRACTION -- humans grapple through open air 3-18% of samples, bots pin
# near zero because the flood prices every rope +1000ms of ritual. The
# dose trial: s03 at 400 vs s04 control at 1000. Bars: off-graph mass
# into the human band on the next route sheets; caps must not fall; the
# 5v0 canary must stay flawless (a rope-happy wreck there kills it).
# TAP VARIANCE (sg_tapvar, rung-3 ranked tell #2: razor cadence bins).
# Skill-scaled per-shot re-aim beat on slow weapons. Armed s03 vs s04.
# Bars: intershot_cv toward the human 0.58 anchor from 0.23; rangesep
# and caps hold. Runs CONCURRENTLY with s06/s07 mode-2 -- independent
# pairs, independent variables, one variable per pair.
TAPVAR=(0     0        1        0        0         0        0         0        0        0)

# NULL at 400 AND 100 (6 waves each, off-graph 0.026-0.027 all arms,
# dead flat): the flood layer is exonerated -- a 90% rope-price cut
# changes nothing observable. Mediator probes (rope rides at 1000 vs
# 100, sg_debug) locate the binding gate; the next change goes where
# they point. Pair returned to steady state.
ROPECOST=(1000  1000     1000     1000     1000      1000     1000      1000     1000     1000)

# PARKED (owner's standing law, re-affirmed 2026-08-05): nothing trades
# caps for cosmetics. Dose 70 cost 31% relative conversion over 22
# pooled waves; no judge ever named the tell it was built for. Off,
# ledgered with its film, retriable only with proof it is free.
EXITASYM=(0      0        0        0        0         0        0         0        0        0)

# 388 trial: the movement-texture judge (4/4 blind) named bang-bang
# throttle the biggest remaining tell. sg_breather = mean seconds of safe
# travel between sub-max windows; s03 armed vs s04 control on the
# mactf06 film pair.
# dose-response (390+): first blind read at 8 was marginal-correct --
# rest structure appeared (a held 25s stop, doubled stopped-time) but
# aggregate texture tied. 4 doubles the cadence; same 0.35 throttle.
# equalized 4/4 on the film pair (the 84-wave pool read a conversion
# cost -- Rule 21 flag on the ledger, dedicated dose ladder owed) so the
# pair is single-variable for SHELFCOST.
# ADOPTED fleet-wide 2026-08-07 (dose ladder 0/4/8, 19 waves/arm,
# mactf06): conversion 0.046 / 0.114 / 0.116 -- the clean s03/s10 pair
# (identical ribbon/jitter) shows the pause near-tripling steal->cap
# conversion; 4 and 8 indistinguishable, smaller dose taken. The old
# 84-wave "breather costs conversion" flag died with the discovery that
# its control arm also ran ribbon/jitter 0 -- three variables, not one.
# s09-ctrl stays 0 as always.
BREATHER=(4      4        4        4        4         4        4         4        0        4)

# THE SHELF TRIAL (steal-genesis study): sg_shelfcost 1 on s03 vs s04
# control, mactf06 -- the map with the measured 1275-cost shelf. Bars
# pre-registered: below-terminal share 0.36 -> <0.10; steals/game up
# toward the 8.5 arithmetic ceiling; caps must not fall.
# STRUCK 2026-08-07 (waves 489-496 census): entries into the pit
# collapsed 89->23 in probes, but steals fell 5.0->4.5/game and close
# approaches -19% -- the doomed low road was cheap TEMPO, and rerouting
# it cost more than the zero-yield room wasted. Rule 21. Code stays,
# cvar stays dark.
SHELFCOST=(0     0        0        0        0         0        0         0        0        0)
# owner's satisficing (321+): sg_tactics on the farm pair s07 ON vs
# s06 OFF (crossed with interdose so each pair carries one variable).
# Waypoint commitment = "is this route still good enough" at 10s holds.
# tactics ADOPTED wave 327 (4-wave verdict 323-326: caps 9v3 on more
# steals, led every wave). The owner's satisficing architecture -- the
# largest doctrine effect ever measured here. s09 control stays clean.
TACTICS=(1       1        1        1        1         1        1         1        0        1)

# ------------------------------------------------------- doctrine flags
# Adopted stack fleet-wide except s09 (the clean-control server: every
# sg_ flag 0 -- the honest drift baseline the fixed format never had).
# Per-server experiments get their own rows; keep each server's test
# clean rather than each wave's.
ADOPT_ON=( 1 1 1 1 1 1 1 1 0 0 )     # strictgrab/press/interpose/scoop/preturn/flycook/runetoss/soundfire/landlead
COVER=(  800 800 800 800 800 800 800 800 0 0 )

# SESSION SHAPE (judge set #3): human pub film has late joins, a leaver,
# a refill; wave film had sixteen bots at t=0 and wall-to-wall presence,
# and a judge sorted the corpus on exactly that. The film pair now fills
# like a pub: 2v2 at the whistle, a body every ~70s to full, one leaves
# at 7:30 and the seat refills a minute later. SAME schedule both arms --
# the ramp is scenery, not a variable.
STAGGER=(0       0        1        1        0         0        0         0        0        1)

for i in 0 1 2 3 4 5 6 7 8 9; do
    (
        (
            sleep 20
            echo "serverrecord wave$NAME-${LABELS[$i]}"
            if [ "${STAGGER[$i]}" = "1" ]; then
                sleep 65;  echo "set sv_botfill \"3:3\""
                sleep 70;  echo "set sv_botfill \"4:4\""
                sleep 75;  echo "set sv_botfill \"5:5\""
                sleep 240; echo "set sv_botfill \"5:4\""
                sleep 65;  echo "set sv_botfill \"5:5\""
                sleep $(( ${SECS[$i]} - 515 ))
            else
                sleep "${SECS[$i]}"
            fi
            echo "quit"
        ) | (
            WCFG="waveflags-s$(( i + 1 )).cfg"
            A=${ADOPT_ON[$i]}
            {
                echo "exec $CFG"
                echo "set maplist_file nomaplist.txt"
                echo "set timelimit 0"
                if [ "${STAGGER[$i]}" = "1" ]; then
                    echo "set sv_botfill \"2:2\""
                else
                    echo "set sv_botfill \"${FILLS[$i]}\""
                fi
                echo "set sg_strictgrab $A"
                echo "set sg_press $A"
                if [ "${INTERDOSE[$i]}" != "0" ]; then
                    echo "set sg_interpose ${INTERDOSE[$i]}"
                else
                    echo "set sg_interpose $A"
                fi
                echo "set sg_scoop $A"
                echo "set sg_preturn $A"
                echo "set sg_flycook $A"
                if [ "${RUNEDOSE[$i]}" != "0" ]; then
                    echo "set sg_runetoss ${RUNEDOSE[$i]}"
                else
                    echo "set sg_runetoss $A"
                fi
                echo "set sg_soundfire $A"
                echo "set sg_landlead $A"
                echo "set sg_carrycover ${COVER[$i]}"
                echo "set sg_escapeprior ${ESCAPE[$i]}"
                echo "set sg_duelroles ${DUEL[$i]}"
                echo "set sg_defpost ${DEFPOST[$i]}"
                echo "set sg_defreact ${DEFREACT[$i]}"
                echo "set sg_landtick ${LANDTICK[$i]}"
                echo "set sg_linklatch ${LINKLATCH[$i]}"
                echo "set sg_nobacktrack ${NOBACKTRACK[$i]}"
                if [ "${COMMSTACK[$i]}" = "1" ]; then
                    echo "set sg_itemcomm 1"
                    echo "set sg_radio 1"
                    echo "set sg_itemlead 1"
                fi
                echo "set sg_megaworth ${MEGAWORTH[$i]}"
                echo "set sg_atkobj ${ATKOBJ[$i]}"
                echo "set sg_wswitch ${WSWITCH[$i]}"
                echo "set sg_wcommit ${WCOMMIT[$i]}"
                echo "set sg_ropecost ${ROPECOST[$i]}"
                echo "set sg_tapvar ${TAPVAR[$i]}"
                echo "set sg_shelfcost ${SHELFCOST[$i]}"
                echo "set sg_noweave ${NOWEAVE[$i]}"
                echo "set sg_fandense ${FANDENSE[$i]}"
                echo "set sg_airgain ${AIRGAIN[$i]}"
                echo "set sg_wetwork ${WETWORK[$i]}"
                echo "set sg_nadelead ${NADELEAD[$i]}"
                echo "set sg_pursuit ${PURSUIT[$i]}"
                echo "set sg_pursuitz ${PURSUITZ[$i]}"
                echo "set sg_smooth ${SMOOTHDOSE[$i]}"
                echo "set sg_tactics ${TACTICS[$i]}"
                echo "set sg_raillane ${RAILLANE[$i]}"
                echo "set sg_ribbon ${RIBBON[$i]}"
                echo "set sg_routejitter ${ROUTEJITTER[$i]}"
                echo "set sg_exitasym ${EXITASYM[$i]}"
                echo "set sg_breather ${BREATHER[$i]}"
                if [ "$i" = "7" ]; then
                    echo "set sg_crowdhold 1"
                fi
                echo "set sg_approachcover ${APPCOVER[$i]}"
            } > "$GAMEDIR_ROOT/$GAME/$WCFG"
            cd "$GAMEDIR_ROOT" && stdbuf -oL -eL \
                timeout $(( 8 + 20 + ${SECS[$i]} + 8 )) \
                "$Q2DED" +set game "$GAME" +set dedicated 1 \
                +set port $(( PORT_BASE + i )) +set net_port $(( PORT_BASE + i )) +set maxclients 16 \
                +exec "$WCFG" +map "${MAPS[$i]}"
        ) > "$LOG_DIR/${LABELS[$i]}-${MAPS[$i]}.log" 2>&1
    ) &
    sleep 7
done
wait

echo "=== WAVE $NAME (free layout) ==="
for i in 0 1 2 3 4 5 6 7 8 9; do
    echo "---- ${LABELS[$i]} ${MAPS[$i]} fill=${FILLS[$i]} ${SECS[$i]}s ----"
    "$SCRIPT_DIR/gamestat.sh" "$LOG_DIR/${LABELS[$i]}-${MAPS[$i]}.log"
done
