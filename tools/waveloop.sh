#!/usr/bin/env bash
# Repeat development waves until tools/waveloop-stop exists.
# Usage: waveloop.sh <first-wave-number>

set -u
cd "$(dirname "$0")"

WAVE="${1:?usage: waveloop.sh <first-wave-number>}"
STOP="$(pwd)/waveloop-stop"
LAST_SO=""

rm -f "$STOP"

while [ ! -f "$STOP" ]; do
    SO="$(ls -t /home/buzzkill/Projects/lmctf6-stats/*.so 2>/dev/null | head -1)"
    if [ -n "$SO" ] && [ "$SO" != "$LAST_SO" ] && ! pgrep -x q2ded >/dev/null; then
        if ./deploy.sh "$SO" >> waveloop.log 2>&1; then
            LAST_SO="$SO"
            echo "$(date +%H:%M:%S) deployed $(basename "$SO")" >> waveloop.log
        fi
    fi
    echo "$(date +%H:%M:%S) wave $WAVE" >> waveloop.log
    T0=$(date +%s)
    mkdir -p runs-archive
    WAVE_STATUS=0
    ./iterate2.sh "$WAVE" >> "runs-archive/iter-$WAVE-launch.log" 2>&1 || WAVE_STATUS=$?
    T1=$(date +%s)
    if [ "$WAVE_STATUS" -ne 0 ] || [ $(( T1 - T0 )) -lt 120 ]; then
        FAILS=$(( ${FAILS:-0} + 1 ))
        echo "$(date +%H:%M:%S) wave $WAVE FAILED status=$WAVE_STATUS in $(( T1 - T0 ))s (streak $FAILS)" >> waveloop.log
        if [ "$FAILS" -ge 5 ]; then
            echo "$(date +%H:%M:%S) five straight failures -- stopping" >> waveloop.log
            break
        fi
        sleep 300
        continue
    fi
    FAILS=0
    WAVE=$(( WAVE + 1 ))
    sleep 2
done
echo "$(date +%H:%M:%S) stopped by $STOP" >> waveloop.log
