#!/usr/bin/env bash
# Launch the four-server auxiliary development wave.
# Usage: aux2.sh <wave-name>

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

LABELS=(a1-5v5-post  a2-5v5-react a3-5v5-both  a4-5v5-none)
MAPS=(  lmctf22      lmctf22      lmctf22      lmctf22)
FILLS=( "5"          "5"          "5"          "5")
SECS=(  900          900          900          900)
DUELR=( 0            0            0            0)
NADEL=( 1            1            1            1)
COVERA=(800          800          800          800)
DPOSTA=(3            0            3            0)
DREACTA=(0           3            3            0)

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
                echo "set sg_defpost ${DPOSTA[$i]}"
                echo "set sg_defreact ${DREACTA[$i]}"
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
