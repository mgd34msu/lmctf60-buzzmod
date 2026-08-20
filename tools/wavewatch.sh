#!/usr/bin/env bash
# Restart the development wave loop when neither it nor q2ded is running.
set -u
cd "$(dirname "$0")"
pgrep -x q2ded >/dev/null && exit 0
pgrep -f "waveloop.sh" | grep -v $$ >/dev/null && exit 0
[ -f waveloop-stop ] && exit 0
LAST=$(grep -oE "wave [0-9]+" waveloop.log | tail -1 | cut -d' ' -f2)
NEXT=$(( ${LAST:-1} + 1 ))
echo "$(date +%H:%M:%S) wavewatch: fleet dead, relaunching at wave $NEXT" >> waveloop.log
setsid nohup ./waveloop.sh "$NEXT" >/dev/null 2>&1 < /dev/null &
