#!/usr/bin/env bash
# The standing observer server -- port 28530, never part of trials.
# Always up, always full (instant botfill), full adopted stack, long
# games on the owner's favorite ground. Restarts itself if it dies.
# This loop makes no decisions; it keeps a door open.
Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
cd "$HOME/Games/Quake2"
while :; do
    [ -f /home/buzzkill/Projects/lmctf6-stats/tools/observer-stop ] && exit 0
    "$Q2DED" +set game lmctf-hooktest +set dedicated 1 \
        +set port 28530 +set net_port 28530 +set maxclients 16 \
        +set hostname "SLIPGATE observer -- always on" \
        +exec waveflags-s4.cfg +set timelimit 20 +map lmctf22 \
        >> /home/buzzkill/Projects/lmctf6-stats/tools/observer.log 2>&1
    sleep 3
done
