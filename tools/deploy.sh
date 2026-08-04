#!/usr/bin/env bash
#
# deploy.sh [<path-to-so>] -- put the newest build into the live game dir.
#
# THE ONLY SANCTIONED DEPLOY PATH (written after waves 309-310, where a
# plain `cp` over the dlopen'd game.so while fleets were live corrupted
# the mapped pages and segfaulted 18 of 20 servers mid-game).
#
# Two protections, both mandatory:
#   1. Refuses to run while q2ded is up, unless FORCE=1 -- and even then
#   2. installs via mv (rename): the directory entry is replaced
#      atomically and any running process keeps its old inode unharmed.

set -eu

SRC="${1:-$(ls -t /home/buzzkill/Projects/lmctf6-stats/*.so | head -1)}"
DEST_DIR="$HOME/Games/Quake2/lmctf-hooktest"

if pgrep -x q2ded >/dev/null 2>&1 && [ "${FORCE:-0}" != "1" ]; then
    echo "deploy.sh: fleet is live; refusing (FORCE=1 to override -- the mv is safe, but the wave will finish on the old build)" >&2
    exit 3
fi

for name in game.so gamex86_64.so; do
    cp "$SRC" "$DEST_DIR/.deploy-tmp.$name"
    mv -f "$DEST_DIR/.deploy-tmp.$name" "$DEST_DIR/$name"
done
echo "deployed $(basename "$SRC") -> $DEST_DIR/{game,gamex86_64}.so"
