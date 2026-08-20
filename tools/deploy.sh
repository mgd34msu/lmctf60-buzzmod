#!/usr/bin/env bash
# Install a development module only while q2ded is stopped.
# Usage: deploy.sh [module.so]

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

for data in escape-priors.json slipgate-weights.cfg; do
    if [ -f "/home/buzzkill/Projects/lmctf6-stats/tools/$data" ]; then
        cp "/home/buzzkill/Projects/lmctf6-stats/tools/$data" "$DEST_DIR/.deploy-tmp.$data"
        mv -f "$DEST_DIR/.deploy-tmp.$data" "$DEST_DIR/$data"
    fi
done
echo "deployed $(basename "$SRC") -> $DEST_DIR/{game,gamex86_64}.so"
