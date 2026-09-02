#!/usr/bin/env bash
# livestart.sh [map] -- restart the live server (port 27910) on a map with the
# deployed module and its rune, archive the previous log with a timestamp, and
# relaunch the owner's client (port 27911) fullscreen on the desktop.
set -u
map="${1:-bctf01}"
Q2=$HOME/Games/Quake2
mkdir -p /tmp/sg_scratch/live
for p in $(pgrep -x q2ded); do
  if tr '\0' ' ' < /proc/$p/cmdline | grep -q "rune.cfg" && ! tr '\0' ' ' < /proc/$p/cmdline | grep -q "6210"; then kill $p; fi
done
if [ "${CLIENT:-0}" = "1" ]; then pkill -x quake2; fi
sleep 1
if [ -s /tmp/sg_scratch/server-live.log ]; then
  mv /tmp/sg_scratch/server-live.log "/tmp/sg_scratch/live/server-$(date +%H%M%S)-$(grep -m1 'rune ready' /tmp/sg_scratch/server-live.log 2>/dev/null | sed 's/.*rune ready \([a-z0-9]*\).*/\1/').log"
fi
cd "$Q2" && ( nohup stdbuf -oL -eL ./engines/yquake2/release/q2ded -portable +set game lmctf-hooktest +set dedicated 1 +set maxclients 16 +set maplist_file nomaplist.txt +exec rune.cfg +map "$map" > /tmp/sg_scratch/server-live.log 2>&1 & )
sleep 8
if [ "${CLIENT:-0}" = "1" ]; then
  ( DISPLAY=:0 WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/run/user/1000 nohup stdbuf -oL -eL ./engines/yquake2/release/quake2 -portable +set game lmctf-hooktest +set port 27911 +set net_port 27911 +set vid_fullscreen 1 +connect 127.0.0.1:27910 > /tmp/sg_scratch/client-live.log 2>&1 & )
fi
sleep 10
echo "map $map bots $(grep -c 'entered the game' /tmp/sg_scratch/server-live.log) you $(grep -c 'Buzzkill entered' /tmp/sg_scratch/server-live.log) rune $(grep -c 'rune ready' /tmp/sg_scratch/server-live.log)"
