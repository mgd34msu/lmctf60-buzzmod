#!/usr/bin/env bash
#
# iterate.sh <name> <map1> <map2> <map3> <map4> <map5>
#
# One SLIPGATE iteration under the owner's protocol: five simultaneous
# servers, five different maps from the demo top-10 pool, 5v5, five
# minutes, staggered launches (same-second starts duplicate Q2's RNG),
# one gamestat block per map at the end. Maps needing a re-test carry
# into the next iteration's pick; other slots rotate.
#
# Process discipline as everywhere in this tree: PID-only, no pattern
# kills, fresh server per game.

set -u

Q2DED="${Q2DED:-/tmp/claude-1000/-home-buzzkill-Projects-lmctf60/efcdc762-1fa3-4536-ae59-d172d832eebc/scratchpad/yquake2/release/q2ded}"
GAMEDIR_ROOT="${GAMEDIR_ROOT:-$HOME/Games/Quake2}"
GAME="${GAME:-lmctf-hooktest}"
CFG="${CFG:-rune.cfg}"
BOTS="${BOTS:-10}"
SECS="${SECS:-300}"
PORT_BASE="${PORT_BASE:-28520}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME="$1"; shift
LOG_DIR="$SCRIPT_DIR/iter-$NAME"
mkdir -p "$LOG_DIR"

i=0
for map in "$@"; do
    (
        (
            sleep 8
            for b in $(seq 1 "$BOTS"); do echo "sv sg add"; sleep 1; done
            sleep "$SECS"
            echo "quit"
        ) | (
            cd "$GAMEDIR_ROOT" && stdbuf -oL -eL \
                timeout $(( 8 + BOTS + SECS + 40 )) \
                "$Q2DED" +set game "$GAME" +set dedicated 1 \
                +set port $(( PORT_BASE + i )) +set maxclients 14 \
                +exec "$CFG" +map "$map"
        ) > "$LOG_DIR/$map.log" 2>&1
    ) &
    i=$(( i + 1 ))
    sleep 7
done
wait

echo "=== ITERATION $NAME: $* ==="
for map in "$@"; do
    echo "---- $map ----"
    "$SCRIPT_DIR/gamestat.sh" "$LOG_DIR/$map.log"
done
echo "logs: $LOG_DIR"
