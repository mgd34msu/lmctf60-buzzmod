#!/usr/bin/env bash
#
# campaign.sh -- the validation campaign: N maps in parallel, G consecutive
# 5v5 games per map, one aggregate table at the end.
#
# Each game is its own server process with its own log (a fresh process per
# game resets RNG and level state honestly -- consecutive games on one
# running server would share item-timer phase and whatever leaked). Servers
# for different maps run concurrently on disjoint ports; games for the SAME
# map run back to back. Launches are staggered: two servers started in the
# same second seed Q2's RNG identically and play out duplicate matches
# (observed: batch runs 28448/28449 and 28450/28453 pairwise identical).
#
# Process discipline (hard rules, learned the hard way -- see runegen.sh):
# no pgrep -f / pkill -f anywhere; servers are addressed only by the PID
# captured from $! at launch; nothing pattern-matched is ever killed.
#
# Usage:
#   tools/campaign.sh                       # the standard five maps
#   tools/campaign.sh lmctf03 lmctf22      # explicit maps
#   GAMES=3 GAME_SECS=300 tools/campaign.sh # shorter campaign

set -u

Q2DED="${Q2DED:-/tmp/claude-1000/-home-buzzkill-Projects-lmctf60/efcdc762-1fa3-4536-ae59-d172d832eebc/scratchpad/yquake2/release/q2ded}"
GAMEDIR_ROOT="${GAMEDIR_ROOT:-$HOME/Games/Quake2}"
GAME="${GAME:-lmctf-hooktest}"
CFG="${CFG:-rune.cfg}"

GAMES="${GAMES:-5}"             # consecutive games per map
GAME_SECS="${GAME_SECS:-600}"   # playing time per game
BOTS="${BOTS:-10}"              # 5v5
PORT_BASE="${PORT_BASE:-28800}" # server i uses PORT_BASE+i for every game
STAGGER=7                       # seconds between server lane launches

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$SCRIPT_DIR/campaign-$STAMP"
mkdir -p "$LOG_DIR"

MAPS=("$@")
[ ${#MAPS[@]} -eq 0 ] && MAPS=(lmctf01 lmctf03 lmctf09 smap05 mactf06)

# a lane: one map, GAMES consecutive games, serial
run_lane() {
    local map="$1" port="$2" g pid rc log
    for g in $(seq 1 "$GAMES"); do
        log="$LOG_DIR/$map-g$g.log"
        (
            sleep 8
            for i in $(seq 1 "$BOTS"); do echo "sv sg add"; sleep 1; done
            sleep "$GAME_SECS"
            echo "quit"
        ) | (
            cd "$GAMEDIR_ROOT" && stdbuf -oL -eL \
                timeout $(( 8 + BOTS + GAME_SECS + 40 )) \
                "$Q2DED" +set game "$GAME" +set dedicated 1 \
                +set port "$port" +set maxclients 14 \
                +exec "$CFG" +map "$map"
        ) > "$log" 2>&1 &
        pid=$!
        wait "$pid"; rc=$?
        echo "[lane $map] game $g done rc=$rc" >> "$LOG_DIR/lanes.log"
    done
}

i=0
for map in "${MAPS[@]}"; do
    run_lane "$map" $(( PORT_BASE + i )) &
    i=$(( i + 1 ))
    sleep "$STAGGER"
done
wait

# ---------------------------------------------------------------- report
echo ""
echo "=== CAMPAIGN $STAMP: ${#MAPS[@]} maps x $GAMES games x ${GAME_SECS}s 5v5 ==="
printf "%-10s %-4s %7s %9s %6s %8s %6s %6s %6s %7s\n" \
       map game steals captures kills shelves swim lift rj chatln
TS=0; TC=0; TK=0
for map in "${MAPS[@]}"; do
    for g in $(seq 1 "$GAMES"); do
        log="$LOG_DIR/$map-g$g.log"
        [ -f "$log" ] || { echo "$map g$g: MISSING LOG"; continue; }
        S=$(grep -c 'stole the' "$log")
        C=$(grep -c 'captured the' "$log")
        K=$(grep -cE 'was blasted|was railed|ate .* rocket|was blown|almost dodged|was machinegunned|was cut in half|was melted|saw the pretty lights|was popped|drown' "$log")
        SH=$(grep -c 'SHELVE' "$log")
        SW=$(grep -c 'act=4' "$log")
        LF=$(grep -c 'act=5' "$log")
        RJ=$(grep -c 'act=7' "$log")
        # chat lines only: public "Name[SG]: msg" or team "(Name[SG]): msg";
        # telemetry lines all start with an uppercase tag (SG/CMD/HOOK...)
        CH=$(grep -cE '^\(?[A-Z][a-z]+\[SG\]\)?: ' "$log" 2>/dev/null || echo 0)
        TS=$((TS+S)); TC=$((TC+C)); TK=$((TK+K))
        printf "%-10s %-4s %7s %9s %6s %8s %6s %6s %6s %7s\n" \
               "$map" "g$g" "$S" "$C" "$K" "$SH" "$SW" "$LF" "$RJ" "$CH"
    done
done
echo "TOTALS: steals=$TS captures=$TC kills=$TK  (legacy baseline: 5.3 steals, 1.7 caps per match)"
echo "logs: $LOG_DIR"
