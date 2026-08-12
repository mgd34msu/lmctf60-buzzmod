#!/usr/bin/env bash
#
# waveloop.sh <first-wave-number> -- the fleet's own heartbeat.
#
# Born wave 403, after the owner's never-stop order met its second
# human-shaped failure: a coordinator forgetting to arm the next chain
# left ten servers dark for 25 minutes. The loop removes the coordinator
# from the critical path: run a wave, deploy the newest build if it
# changed, run the next wave, forever. Stop it with tools/waveloop-stop
# (touch the file) or by killing the process group.
#
# Deploys happen BETWEEN waves via deploy.sh's atomic mv, and only when
# the newest .so differs from what was last deployed -- a mid-flux build
# tree never surprises a running fleet.

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
    ./iterate2.sh "$WAVE" >> "runs-archive/iter-$WAVE-launch.log" 2>&1
    T1=$(date +%s)
    # A wave that "finished" in under two minutes did not run -- the
    # overlap guard refused (stale q2ded) or the launch failed. Spinning
    # here once burned wave numbers 438-882 in fifteen minutes. Do not
    # increment on a failed wave; clear stragglers, back off, retry, and
    # give up loudly after five straight failures.
    if [ $(( T1 - T0 )) -lt 120 ]; then
        FAILS=$(( ${FAILS:-0} + 1 ))
        echo "$(date +%H:%M:%S) wave $WAVE FAILED in $(( T1 - T0 ))s (streak $FAILS)" >> waveloop.log
        pkill -x q2ded 2>/dev/null
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
