#!/usr/bin/env bash
#
# runegen.sh -- serial batch RUNE generator for SLIPGATE.
#
# For each map named on the command line: launch a dedicated q2ded server on
# a unique port, let it boot, issue the console command "sv rune" (which
# generates the rune for whatever map is CURRENTLY loaded and writes
# <gamedir>/maps/<map>.rune -- see slipgate/sg_rune.c, Rune_Generate), give
# it a generation budget, then quit. Verify the .rune file appeared, pull
# the seed/link counts out of the server's own "rune: wrote ..." log line,
# and print a summary table at the end. A map that fails to load prints an
# error and the server exits fast, so a short run time with no .rune file
# is treated as a failure, not a hang.
#
# Usage:
#   tools/runegen.sh [--dry-run] <map1> [map2 ...]
#   tools/runegen.sh --dry-run $(grep -v '^#' tools/topmaps.txt)
#
# Resilience (read before touching process management in this file):
#   Four earlier runs were killed by a `pgrep -f` / `pkill -f` pattern that
#   matched this script's OWN command line -- e.g. a pattern like "q2ded"
#   matches not only the real q2ded process but also the shell invocation
#   here that embeds the q2ded path as a string argument, so the "cleanup"
#   step killed the wrapper (and everything downstream of it) mid-run.
#   Rules followed in this file:
#     - No `pgrep -f` / `pkill -f` anywhere.
#     - The only process lookup is `pgrep -x q2ded` (exact executable-name
#       match against comm, not the full command line) and it is used
#       read-only, to warn about a pre-existing server, never to kill.
#     - The per-map server is only ever addressed by the PID captured from
#       $! right after backgrounding it. Any kill in this script targets
#       that exact PID, nothing pattern-matched.
#     - This script never kills anything belonging to another run. It is
#       not to be used for the full batch while the main line owns the
#       server (port conflicts) -- --dry-run only until told otherwise.

set -u

# ---------------------------------------------------------------- config

Q2DED="${Q2DED:-$HOME/q2linux/q2reproded}"
GAMEDIR_ROOT="${GAMEDIR_ROOT:-$HOME/Games/Quake2}"
GAME="${GAME:-lmctf-hooktest}"
CFG="${CFG:-rune.cfg}"

PORT_START="${PORT_START:-28500}"  # unique port per run, PORT_START + map
                                   # index; env-overridable so parallel
                                   # lanes use disjoint ranges
STARTUP_SLEEP=8          # seconds given to the server to boot before "sv rune"
GEN_BUDGET="${GEN_BUDGET:-900}"  # seconds given to rune generation before "quit"; env-overridable
SHUTDOWN_MARGIN=60        # extra wall-clock seconds before the hard `timeout` kill
TIMEOUT_SECS=$(( STARTUP_SLEEP + GEN_BUDGET + SHUTDOWN_MARGIN ))

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$SCRIPT_DIR/rune-logs"

DRY_RUN=0

# --------------------------------------------------------------- parsing

MAPS=()
for arg in "$@"; do
    case "$arg" in
        --dry-run)
            DRY_RUN=1
            ;;
        -h|--help)
            echo "usage: $0 [--dry-run] <map1> [map2 ...]"
            exit 0
            ;;
        *)
            MAPS+=("$arg")
            ;;
    esac
done

if [ "${#MAPS[@]}" -eq 0 ]; then
    echo "usage: $0 [--dry-run] <map1> [map2 ...]" >&2
    exit 1
fi

if [ "$DRY_RUN" -eq 0 ] && [ ! -x "$Q2DED" ]; then
    echo "runegen: q2ded binary not found or not executable: $Q2DED" >&2
    exit 1
fi

mkdir -p "$LOG_DIR"

# Read-only heads-up. Exact-binary match only (comm field, not argv) --
# never used to kill, see the resilience note above.
if command -v pgrep >/dev/null 2>&1; then
    existing="$(pgrep -x q2ded 2>/dev/null || true)"
    if [ -n "$existing" ]; then
        echo "runegen: note -- q2ded already running (pid(s): $existing)." \
             "Not touching it. Make sure your port range doesn't collide." >&2
    fi
fi

# ------------------------------------------------------------ per-map run

RESULT_LINES=()   # "map|seeds|links|secs|status|detail"

run_one() {
    local map="$1" port="$2" logfile="$3"
    local t0 t1 elapsed pid status detail seeds links runefile

    runefile="$GAMEDIR_ROOT/$GAME/maps/$map.rune"

    t0=$(date +%s)

    ( sleep "$STARTUP_SLEEP"; echo "sv rune"; sleep "$GEN_BUDGET"; echo "quit" ) | \
        ( cd "$GAMEDIR_ROOT" && exec stdbuf -oL timeout "$TIMEOUT_SECS" "$Q2DED" \
              +set game "$GAME" +set dedicated 1 +set port "$port" +set net_port "$port" \
              +exec "$CFG" +map "$map" ) > "$logfile" 2>&1 &
    pid=$!

    wait "$pid"
    status_code=$?

    # Belt-and-braces: if something is still alive at this exact PID
    # (should not happen -- `timeout` owns the kill), take it down by PID
    # only. No name/pattern search.
    if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid" 2>/dev/null
    fi

    t1=$(date +%s)
    elapsed=$(( t1 - t0 ))

    if [ -f "$runefile" ] && grep -q "rune: wrote" "$logfile" 2>/dev/null; then
        local wrote_line
        wrote_line="$(grep "rune: wrote" "$logfile" | tail -n1)"
        seeds="$(printf '%s\n' "$wrote_line" | sed -n 's/.*(\([0-9]\+\) seeds, \([0-9]\+\) links).*/\1/p')"
        links="$(printf '%s\n' "$wrote_line" | sed -n 's/.*(\([0-9]\+\) seeds, \([0-9]\+\) links).*/\2/p')"
        seeds="${seeds:-?}"
        links="${links:-?}"
        status="ok"
        detail="$wrote_line"
        echo "rune: wrote $runefile ($seeds seeds, $links links) -- ${elapsed}s, map=$map port=$port"
    else
        seeds="-"
        links="-"
        status="FAIL"
        if [ ! -s "$logfile" ]; then
            detail="empty log -- server never produced output"
        else
            detail="$(tail -n1 "$logfile")"
        fi
        echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
    fi

    RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
}

i=0
for map in "${MAPS[@]}"; do
    port=$(( PORT_START + i ))
    ts="$(date +%Y%m%d-%H%M%S)"
    logfile="$LOG_DIR/${map}-${ts}.log"

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "[dry-run] map=$map port=$port timeout=${TIMEOUT_SECS}s log=$logfile"
        echo "[dry-run]   ( sleep $STARTUP_SLEEP; echo \"sv rune\"; sleep $GEN_BUDGET; echo \"quit\" ) |" \
             "( cd \"$GAMEDIR_ROOT\" && stdbuf -oL timeout $TIMEOUT_SECS \"$Q2DED\"" \
             "+set game $GAME +set dedicated 1 +set port $port +exec $CFG +map $map ) > \"$logfile\" 2>&1"
        RESULT_LINES+=("$map|-|-|-|DRY-RUN|would write $GAMEDIR_ROOT/$GAME/maps/$map.rune")
    else
        echo "=== runegen: $map (port $port) ==="
        run_one "$map" "$port" "$logfile"
    fi

    i=$(( i + 1 ))
done

# ------------------------------------------------------------------ table

echo
printf '%-12s %8s %8s %8s %-8s %s\n' "map" "seeds" "links" "seconds" "status" "detail"
printf '%-12s %8s %8s %8s %-8s %s\n' "------------" "--------" "--------" "--------" "--------" "------"
for line in "${RESULT_LINES[@]}"; do
    IFS='|' read -r rmap rseeds rlinks rsecs rstatus rdetail <<< "$line"
    printf '%-12s %8s %8s %8s %-8s %s\n' "$rmap" "$rseeds" "$rlinks" "$rsecs" "$rstatus" "$rdetail"
done

fail_count=0
for line in "${RESULT_LINES[@]}"; do
    case "$line" in
        *"|FAIL|"*) fail_count=$(( fail_count + 1 )) ;;
    esac
done

if [ "$fail_count" -gt 0 ]; then
    exit 1
fi
exit 0
