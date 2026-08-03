#!/usr/bin/env bash
#
# waveloop.sh -- the fleet never idles again.
#
# Self-refilling wave runner: reads the next wave number from wavecounter,
# runs one full wave via iterate.sh, increments, repeats. No notification
# chain, no per-wave supervision -- the loop owns the cadence and the
# analyst reads the ledger whenever it likes. Stop with: touch wavestop
#
# Waits politely if servers are already up (a hand-launched chain), and
# survives iterate.sh failures by logging and continuing after a cooldown.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COUNTER="$SCRIPT_DIR/wavecounter"
STOP="$SCRIPT_DIR/wavestop"
MAPS="lmctf03 lmctf22 lmctf02c mactf06 lmctf05 lmctf01 lmctf09 smap05"

while :; do
    [ -f "$STOP" ] && { echo "waveloop: stop file found, exiting"; exit 0; }
    if pgrep -x q2ded >/dev/null 2>&1; then
        sleep 30
        continue
    fi
    W=$(cat "$COUNTER" 2>/dev/null || echo 300)
    echo "waveloop: launching wave $W ($(date '+%H:%M:%S'))"
    if ! "$SCRIPT_DIR/iterate.sh" "$W" $MAPS >> "$SCRIPT_DIR/waveloop.log" 2>&1; then
        echo "waveloop: wave $W FAILED, cooling down 60s"
        sleep 60
    fi
    echo $(( W + 1 )) > "$COUNTER"
done
