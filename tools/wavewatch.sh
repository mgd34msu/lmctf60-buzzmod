#!/usr/bin/env bash
#
# wavewatch.sh -- relaunch the waveloop if the fleet is dead.
#
# Born after two host crashes on 2026-08-07 each cost the fleet 30-60
# dark minutes. Runs from a systemd --user timer every 5 minutes: if no
# q2ded AND no waveloop.sh process exist, compute the next wave number
# from waveloop.log and relaunch detached. The spin guard inside
# waveloop.sh still owns failure handling; this only answers "is anyone
# even trying".
set -u
cd "$(dirname "$0")"
pgrep -x q2ded >/dev/null && exit 0
pgrep -f "waveloop.sh" | grep -v $$ >/dev/null && exit 0
[ -f waveloop-stop ] && exit 0
LAST=$(grep -oE "wave [0-9]+" waveloop.log | tail -1 | cut -d' ' -f2)
NEXT=$(( ${LAST:-1} + 1 ))
echo "$(date +%H:%M:%S) wavewatch: fleet dead, relaunching at wave $NEXT" >> waveloop.log
setsid nohup ./waveloop.sh "$NEXT" >/dev/null 2>&1 < /dev/null &
