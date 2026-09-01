#!/usr/bin/env bash
# Launch the legacy fixed-layout ten-server development wave.
# Usage: iterate.sh <name> <duel-map> <five1> ... <five5> <density-map> <control-map>

set -u

Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
GAMEDIR_ROOT="${GAMEDIR_ROOT:-$HOME/Games/Quake2}"
GAME="${GAME:-lmctf-hooktest}"
CFG="${CFG:-rune.cfg}"
SECS="${SECS:-600}"
PORT_BASE="${PORT_BASE:-28520}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME="$1"; shift
[ $# -eq 8 ] || { echo "need 8 maps: duel five1..five5 dens ctrl" >&2; exit 2; }
DUEL="$1"; F1="$2"; F2="$3"; F3="$4"; F4="$5"; F5="$6"; DENS="$7"; CTRL="$8"
LOG_DIR="$SCRIPT_DIR/iter-$NAME"
mkdir -p "$LOG_DIR"

LABELS=(s01-2v2 s02-2v2 s03-5v5 s04-5v5 s05-5v5 s06-5v5 s07-5v5 s08-7v7 s09-7v7 s10-5v1)
ROT=$(( $(echo "$NAME" | tr -cd '0-9') % 5 ))
FIVES=("$F1" "$F2" "$F3" "$F4" "$F5")
R0=${FIVES[$(( (0 + ROT) % 5 ))]}; R1=${FIVES[$(( (1 + ROT) % 5 ))]}
R2=${FIVES[$(( (2 + ROT) % 5 ))]}; R3=${FIVES[$(( (3 + ROT) % 5 ))]}
R4=${FIVES[$(( (4 + ROT) % 5 ))]}
MAPS=("$DUEL" "$DUEL" "$R0" "$R1" "$R2" "$R3" "$R4" "$DENS" "$DENS" "$CTRL")
FILLS=("2" "2" "5" "5" "5" "5" "5" "7" "7" "5:1")
GRABS=("0" "0" "1" "1" "1" "1" "1" "0" "0" "0")
PRESS=("0" "0" "1" "1" "1" "1" "1" "0" "0" "0")
INTERPOSE=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
SCOOP=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
HUMAN=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
SMOOTH=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
WAVEPUSH=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
FASTCARRY=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
FLAGPRIOR=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
QUICKROPE=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
HOPFIRE=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
PRETURN=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
FLYCOOK=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
RUNETOSS=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
SOUNDFIRE=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
LANDLEAD=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
WATERCARRY=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
CARRYHOP=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
CARRYCOVER=("0" "0" "800" "800" "800" "800" "800" "800" "800" "0")
CARRYPRESS=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
for i in 0 1 2 3 4 5 6 7 8 9; do
    (
        (
            sleep 8
            sleep 20   # instant initial fill: roster lands in seconds
            if [ "$i" = "3" ] || [ "$i" = "9" ]; then
                echo "serverrecord wave$NAME-${LABELS[$i]}"
            fi
            sleep "$SECS"
            echo "quit"
        ) | (
            WCFG="waveflags-s$(( i + 1 )).cfg"
            {
                echo "exec $CFG"
                echo "set maplist_file nomaplist.txt"
                echo "set sv_botfill \"${FILLS[$i]}\""
                echo "set sg_strictgrab ${GRABS[$i]}"
                echo "set sg_press ${PRESS[$i]}"
                echo "set sg_interpose ${INTERPOSE[$i]}"
                echo "set sg_scoop ${SCOOP[$i]}"
                echo "set sg_preturn ${PRETURN[$i]}"
                echo "set sg_flycook ${FLYCOOK[$i]}"
                echo "set sg_runetoss ${RUNETOSS[$i]}"
                echo "set sg_soundfire ${SOUNDFIRE[$i]}"
                echo "set sg_landlead ${LANDLEAD[$i]}"
                echo "set sg_watercarry ${WATERCARRY[$i]}"
                echo "set sg_flagprior ${FLAGPRIOR[$i]}"
                echo "set sg_carryhop ${CARRYHOP[$i]}"
                echo "set sg_carrycover ${CARRYCOVER[$i]}"
                echo "set sg_carrypress ${CARRYPRESS[$i]}"
            } > "$GAMEDIR_ROOT/$GAME/$WCFG"
            cd "$GAMEDIR_ROOT" && stdbuf -oL -eL \
                timeout $(( 8 + 20 + SECS + 8 )) \
                "$Q2DED" +set game "$GAME" +set dedicated 1 \
                +set port $(( PORT_BASE + i )) +set net_port $(( PORT_BASE + i )) +set maxclients 16 \
                +exec "$WCFG" +map "${MAPS[$i]}"
        ) > "$LOG_DIR/${LABELS[$i]}-${MAPS[$i]}.log" 2>&1
    ) &
    sleep 7
done
wait

echo "=== WAVE $NAME: duel=$DUEL fives=$F1,$F2,$F3,$F4,$F5 dens=$DENS ctrl=$CTRL ==="
for i in 0 1 2 3 4 5 6 7 8 9; do
    echo "---- ${LABELS[$i]} ${MAPS[$i]} ----"
    "$SCRIPT_DIR/gamestat.sh" "$LOG_DIR/${LABELS[$i]}-${MAPS[$i]}.log"
done
echo "logs: $LOG_DIR"
