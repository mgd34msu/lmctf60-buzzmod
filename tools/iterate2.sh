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
LABELS=(s01-2v2  s02-2v2  s03-5v5  s04-5v5  s05-5v5   s06-5v3  s07-5v3   s08-7v7  s09-ctrl s10-5v1)
MAPS=(  lmctf03  lmctf03  lmctf22  lmctf22  mactf06   lmctf44  lmctf44   lmctf09  lmctf01  smap05)
FILLS=( "2"      "2"      "5"      "5"      "5"       "5:3"    "5:3"     "7"      "5"      "5:1")
SECS=(  600      600      900      900      900       900      900       600      900      600)
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
DUEL=(  0        0        0        0        0         0        0         0        0        0)
# defense dwell A/B (286+): s05 ON vs s03 OFF (both have .dpo data);
# landing-tick A/B (286+): s06 ON vs s07 OFF (farms feel it most --
# read = touch_loss and relaunch rate from the serverrecord demos,
# plus carrier speed).
DEFPOST=(0       0        0        0        3         3        0         0        0        0)
DEFREACT=(0      0        0        0        3         3        0         0        0        0)
LANDTICK=(0      0        0        0        0         0        0         0        0        0)
# link latch A/B (290+): s04 ON vs s03 OFF (5v5 pair, demos on s03) --
# read = turn1hz/reversals/180s from botkin, plus steals as the cost
# column. 700ms latch ~= the 1Hz surface refresh with margin.
# link latch NULL (290-291): turn grammar unchanged -- the incumbent
# dies at every seed crossing, same within-seed-only trap as sticky.
LINKLATCH=(0     0        0        0        0         0        0         0        0        0)
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
NADELEAD=(0      0        0        1        0         0        0         0        0        0)
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
APPCOVER=(0      0        400      0        0         400      0         0        0        0)

# ------------------------------------------------------- doctrine flags
# Adopted stack fleet-wide except s09 (the clean-control server: every
# sg_ flag 0 -- the honest drift baseline the fixed format never had).
# Per-server experiments get their own rows; keep each server's test
# clean rather than each wave's.
ADOPT_ON=( 1 1 1 1 1 1 1 1 0 0 )     # strictgrab/press/interpose/scoop/preturn/flycook/runetoss/soundfire/landlead
COVER=(  800 800 800 800 800 800 800 800 0 0 )

for i in 0 1 2 3 4 5 6 7 8 9; do
    (
        (
            sleep 20
            if [ "$i" = "2" ] || [ "$i" = "3" ] || [ "$i" = "5" ] || [ "$i" = "6" ]; then
                echo "serverrecord wave$NAME-${LABELS[$i]}"
            fi
            sleep "${SECS[$i]}"
            echo "quit"
        ) | (
            WCFG="waveflags-s$(( i + 1 )).cfg"
            A=${ADOPT_ON[$i]}
            {
                echo "exec $CFG"
                echo "set maplist_file nomaplist.txt"
                echo "set timelimit 0"
                echo "set sv_botfill \"${FILLS[$i]}\""
                echo "set sg_strictgrab $A"
                echo "set sg_press $A"
                echo "set sg_interpose $A"
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
                echo "set sg_noweave ${NOWEAVE[$i]}"
                echo "set sg_fandense ${FANDENSE[$i]}"
                echo "set sg_airgain ${AIRGAIN[$i]}"
                echo "set sg_wetwork ${WETWORK[$i]}"
                echo "set sg_nadelead ${NADELEAD[$i]}"
                echo "set sg_pursuit ${PURSUIT[$i]}"
                echo "set sg_pursuitz ${PURSUITZ[$i]}"
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
