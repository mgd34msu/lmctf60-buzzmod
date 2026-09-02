#!/usr/bin/env bash
# botsmoke.sh <map> <seconds> <log>: a dedicated server on the live test game
# directory with two bots a side, sg_debug on, for a while; then the roster
# and a quit.  The log carries one SGBOT line per bot every five seconds:
# role, cell, step, position, target.
set -u
map="$1"; seconds="$2"; log="$3"
Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
cd "$HOME/Games/Quake2" || exit 1
{
    sleep 4
    printf '%s\n' 'set sg_debug 1' 'sv sg add red' 'sv sg add red' 'sv sg add blue' 'sv sg add blue'
    sleep "$seconds"
    printf '%s\n' 'sv sg list' quit
} | "$Q2DED" -portable +set game lmctf-hooktest +set dedicated 1 +set maxclients 16 \
    +set port 62100 +set net_port 62100 +exec rune.cfg +map "$map" > "$log" 2>&1
echo "botsmoke exit=$?"
