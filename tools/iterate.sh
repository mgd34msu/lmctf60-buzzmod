#!/usr/bin/env bash
#
# iterate.sh <name> <duel_map> <five1> <five2> <five3> <five4> <five5> <dens_map> <ctrl_map>
#
# One SLIPGATE wave in the owner's mixed-density format (2026-08-02):
#   servers 1-2 : 2v2 on duel_map (threshold-duel isolation, within-map replication)
#   servers 3-7 : 5v5 on five different maps (baseline lineage)
#   servers 8-9 : 7v7 on dens_map (room-density stress, within-map replication)
#   server  10  : 5v1 on ctrl_map (conversion-mechanics control)
#
# Ten-minute games, staggered launches (same-second starts duplicate Q2's
# RNG), sv_botfill owns every roster ("R B" form carries the asymmetric
# fill). One gamestat block per server at the end.
#
# Process discipline as everywhere in this tree: PID-only, no pattern
# kills, fresh server per game.

set -u

Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
GAMEDIR_ROOT="${GAMEDIR_ROOT:-$HOME/Games/Quake2}"
GAME="${GAME:-lmctf-hooktest}"
CFG="${CFG:-rune.cfg}"
SECS="${SECS:-600}"  # ten-minute games, owner-ordered wave format
PORT_BASE="${PORT_BASE:-28520}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME="$1"; shift
[ $# -eq 8 ] || { echo "need 8 maps: duel five1..five5 dens ctrl" >&2; exit 2; }
DUEL="$1"; F1="$2"; F2="$3"; F3="$4"; F4="$5"; F5="$6"; DENS="$7"; CTRL="$8"
LOG_DIR="$SCRIPT_DIR/iter-$NAME"
mkdir -p "$LOG_DIR"

# server layout: label, map, botfill spec (single value or "R B")
LABELS=(s01-2v2 s02-2v2 s03-5v5 s04-5v5 s05-5v5 s06-5v5 s07-5v5 s08-7v7 s09-7v7 s10-5v1)
MAPS=("$DUEL" "$DUEL" "$F1" "$F2" "$F3" "$F4" "$F5" "$DENS" "$DENS" "$CTRL")
FILLS=("2" "2" "5" "5" "5" "5" "5" "7" "7" "5:1")
# strict-grab A/B (wave 151+): three 5v5 servers strict, two current
GRABS=("0" "0" "1" "1" "1" "0" "0" "0" "0" "0")

for i in 0 1 2 3 4 5 6 7 8 9; do
    (
        (
            sleep 8
            sleep 45   # botfill assembles the roster: 3s hysteresis per seat, 14 seats worst case
            sleep "$SECS"
            echo "quit"
        ) | (
            cd "$GAMEDIR_ROOT" && stdbuf -oL -eL \
                timeout $(( 8 + 45 + SECS + 40 )) \
                "$Q2DED" +set game "$GAME" +set dedicated 1 \
                +set port $(( PORT_BASE + i )) +set net_port $(( PORT_BASE + i )) +set maxclients 16 \
                +set sv_botfill "${FILLS[$i]}" +set sg_strictgrab "${GRABS[$i]}" \
                +exec "$CFG" +map "${MAPS[$i]}"
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
