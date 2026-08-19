#!/usr/bin/env bash
# The fleet lifecycle tools key process ownership on the exact q2ded name.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require_assignment() {
    local script="$1" assignment="$2"

    if ! grep -Fqx "$assignment" "$ROOT/$script"; then
        echo "engine snapshot test: $script does not preserve q2ded basename" >&2
        exit 1
    fi
}

require_assignment tools/campaign.sh 'Q2DED_SNAPSHOT="$LOG_DIR/q2ded"'

for command in chmod cp grep mktemp pgrep rmdir sleep unlink; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "engine snapshot test: missing required command: $command" >&2
        exit 1
    fi
done
if [ ! -r /proc/self/comm ]; then
    echo "engine snapshot test: /proc process names are unavailable" >&2
    exit 1
fi

probe_dir="$(mktemp -d /tmp/lmctf-q2ded-name.XXXXXX)"
probe_bin="$probe_dir/q2ded"
probe_pid=""
cleanup() {
    if [ -n "$probe_pid" ] && kill -0 "$probe_pid" 2>/dev/null; then
        kill "$probe_pid" 2>/dev/null || true
        wait "$probe_pid" 2>/dev/null || true
    fi
    if [ -e "$probe_bin" ]; then
        unlink "$probe_bin"
    fi
    rmdir -- "$probe_dir" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

sleep_bin="$(type -P sleep)"
cp -- "$sleep_bin" "$probe_bin"
chmod 0500 -- "$probe_bin"
"$probe_bin" 30 &
probe_pid=$!

comm=""
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if [ -r "/proc/$probe_pid/comm" ]; then
        IFS= read -r comm < "/proc/$probe_pid/comm" || true
        [ "$comm" = q2ded ] && break
    fi
    sleep 0.02
done
if [ "$comm" != q2ded ]; then
    echo "engine snapshot test: /proc name is '$comm', expected q2ded" >&2
    exit 1
fi
if ! pgrep -x q2ded | grep -Fxq "$probe_pid"; then
    echo "engine snapshot test: pgrep -x q2ded missed PID $probe_pid" >&2
    exit 1
fi

echo "engine_snapshot_name_test: ok"
