#!/usr/bin/env bash
#
# aux2.sh <name> -- auxiliary 4-server side-fleet (owner-ordered,
# 2026-08-04): knocks out the 2v2 duel-roles revisit and the nadelead
# A/B in parallel with the main fleet. Ports 28530-28533, same process
# discipline as iterate2.sh (per-server cfg, defused maplist, stagger,
# PID-safe). Runs alongside the main 10 -- additive, never replacing.
#
#   a1: 2v2 lmctf03 duelroles ON     a2: 2v2 lmctf03 duelroles OFF
#   a3: 5v5 lmctf09 nadelead ON      a4: 5v5 lmctf09 nadelead OFF
#
# 2v2 games run 20 minutes (duel steals are rare; sample needs time).

set -u

Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
GAMEDIR_ROOT="${GAMEDIR_ROOT:-$HOME/Games/Quake2}"
GAME="${GAME:-lmctf-hooktest}"
CFG="${CFG:-rune.cfg}"
PORT_BASE=28530

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME="$1"
LOG_DIR="$SCRIPT_DIR/aux-$NAME"
mkdir -p "$LOG_DIR"

# duel verdict in (7-0, adopted): a1/a2 repurposed to the carrier-cover
# revalidation (last broken-era adoption unchecked). a1 cover 800 ON,
# a2 cover 0 -- the only pair anywhere with cover OFF.
LABELS=(a1-5v5-cover a2-5v5-nocov a3-5v5-nade  a4-5v5-ctrl)
MAPS=(  mactf06      mactf06      lmctf09      lmctf09)
FILLS=( "5"          "5"          "5"          "5")
SECS=(  900          900          900          900)
DUELR=( 0            0            0            0)
NADEL=( 0            0            1            0)
COVERA=(800          0            800          800)

for i in 0 1 2 3; do
    (
        (
            sleep 20
            sleep "${SECS[$i]}"
            echo "quit"
        ) | (
            WCFG="waveflags-a$(( i + 1 )).cfg"
            {
                echo "exec $CFG"
                echo "set maplist_file nomaplist.txt"
                echo "set timelimit 0"
                echo "set sv_botfill \"${FILLS[$i]}\""
                echo "set sg_strictgrab 1"
                echo "set sg_press 1"
                echo "set sg_interpose 1"
                echo "set sg_scoop 1"
                echo "set sg_preturn 1"
                echo "set sg_flycook 1"
                echo "set sg_runetoss 2"
                echo "set sg_soundfire 1"
                echo "set sg_landlead 1"
                echo "set sg_carrycover ${COVERA[$i]}"
                echo "set sg_approachcover 200"
                echo "set sg_wetwork 1"
                echo "set sg_tactics 1"
                echo "set sg_duelroles ${DUELR[$i]}"
                echo "set sg_nadelead ${NADEL[$i]}"
            } > "$GAMEDIR_ROOT/$GAME/$WCFG"
            cd "$GAMEDIR_ROOT" && stdbuf -oL -eL \
                timeout $(( 8 + 20 + ${SECS[$i]} + 8 )) \
                "$Q2DED" +set game "$GAME" +set dedicated 1 \
                +set port $(( PORT_BASE + i )) +set net_port $(( PORT_BASE + i )) +set maxclients 16 \
                +exec "$WCFG" +map "${MAPS[$i]}"
        ) > "$LOG_DIR/${LABELS[$i]}.log" 2>&1
    ) &
    sleep 7
done
wait
echo "=== AUX $NAME done ==="
