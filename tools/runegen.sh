#!/usr/bin/env bash
#
# runegen.sh -- serial batch RUNE generator for SLIPGATE.
#
# For each map named on the command line: launch a dedicated q2ded server on
# a unique port, let it boot, issue the console command "sv rune" (which
# generates the rune for whatever map is CURRENTLY loaded). The server boots
# in a temporary, portable game-directory mirror, so Rune_Generate writes to
# staging rather than over the deployed graph. runelint's runtime-v3 gate then
# validates the exact format and both flag-objective reverse components. Only
# a clean graph is backed up and atomically renamed into the live maps/ tree.
# Generation, lint, backup, or install failure leaves the old rune untouched.
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

Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
GAMEDIR_ROOT="${GAMEDIR_ROOT:-$HOME/Games/Quake2}"
GAME="${GAME:-lmctf-hooktest}"
CFG="${CFG:-rune.cfg}"

PORT_START="${PORT_START:-28500}"  # unique port per run, PORT_START + map
                                   # index; env-overridable so parallel
                                   # lanes use disjoint ranges
STARTUP_SLEEP="${STARTUP_SLEEP:-8}"  # seconds given to the server to boot before "sv rune"
GEN_BUDGET="${GEN_BUDGET:-900}"  # seconds given to rune generation before "quit"; env-overridable
SHUTDOWN_MARGIN="${SHUTDOWN_MARGIN:-60}"  # extra seconds before the hard `timeout` kill
for number in "$PORT_START" "$STARTUP_SLEEP" "$GEN_BUDGET" "$SHUTDOWN_MARGIN"; do
    if [[ ! "$number" =~ ^[0-9]+$ ]]; then
        echo "runegen: port and timeout settings must be non-negative integers" >&2
        exit 2
    fi
done
TIMEOUT_SECS=$(( STARTUP_SLEEP + GEN_BUDGET + SHUTDOWN_MARGIN ))

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${RUNE_LOG_DIR:-$SCRIPT_DIR/rune-logs}"
RUNE_LINT="$SCRIPT_DIR/runelint.py"
RUNE_BACKUP_DIR="${RUNE_BACKUP_DIR:-$LOG_DIR/backups}"

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

if [[ ! "$GAME" =~ ^[A-Za-z0-9_][A-Za-z0-9_.-]{0,62}$ ]]; then
    echo "runegen: GAME must be one safe game-directory name: $GAME" >&2
    exit 2
fi

if [ ! -d "$GAMEDIR_ROOT/$GAME/maps" ]; then
    echo "runegen: game maps directory not found: $GAMEDIR_ROOT/$GAME/maps" >&2
    exit 1
fi

if [ "$DRY_RUN" -eq 0 ] && ! command -v python3 >/dev/null 2>&1; then
    echo "runegen: python3 is required for the deployment quality gate" >&2
    exit 1
fi

if [ "$DRY_RUN" -eq 0 ] && [ ! -r "$RUNE_LINT" ]; then
    echo "runegen: rune validator not readable: $RUNE_LINT" >&2
    exit 1
fi

if ! GAMEDIR_ROOT_ABS="$(cd "$GAMEDIR_ROOT" && pwd)"; then
    echo "runegen: cannot resolve game root: $GAMEDIR_ROOT" >&2
    exit 1
fi
GAMEDIR_ROOT="$GAMEDIR_ROOT_ABS"
LIVE_GAME_DIR="$GAMEDIR_ROOT/$GAME"

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

cleanup_stage() {
    local stage_dir="$1"
    case "$stage_dir" in
        "$GAMEDIR_ROOT"/.runegen-stage.*)
            rm -rf -- "$stage_dir"
            ;;
        *)
            echo "runegen: refusing to remove unexpected stage path: $stage_dir" >&2
            return 1
            ;;
    esac
}

prepare_stage() {
    local stage_dir="$1" map="$2" entry base

    mkdir -p "$stage_dir/maps" "$stage_dir/players" \
             "$stage_dir/demos" "$stage_dir/screenshots" || return 1

    # Link large/read-only assets. Copy cfg files because the engine may
    # rewrite its config on shutdown; staging must never write through a
    # symlink into the live game directory.
    for entry in "$LIVE_GAME_DIR"/*; do
        [ -e "$entry" ] || continue
        base="${entry##*/}"
        case "$base" in
            maps|players|demos|screenshots)
                continue
                ;;
        esac
        if [ -f "$entry" ] && [[ "$base" == *.cfg ]]; then
            cp -p -- "$entry" "$stage_dir/$base" || return 1
        else
            ln -s -- "$entry" "$stage_dir/$base" || return 1
        fi
    done

    # maps/ must itself be real so Rune_Generate's same-directory temporary
    # and rename remain inside staging. Its inputs can safely be symlinks.
    for entry in "$LIVE_GAME_DIR/maps"/*; do
        [ -e "$entry" ] || [ -L "$entry" ] || continue
        base="${entry##*/}"
        if [ "$base" = "$map.rune" ]; then
            # Generation must create this exact file from absence. Carrying the
            # old target into staging lets a failed or wrong-map `sv rune`
            # masquerade as fresh output merely because a generic write banner
            # appeared elsewhere in the log.
            continue
        fi
        ln -s -- "$entry" "$stage_dir/maps/$base" || return 1
    done
}

install_rune_atomic() {
    local source="$1" destination="$2"

    # The generation directory may be a different filesystem from a symlinked
    # or mounted live maps directory. Copy into an exclusive temporary created
    # beside the destination, sync it, and make rename the sole commit point.
    # A failure before os.replace leaves the deployed file byte-for-byte intact.
    python3 - "$source" "$destination" <<'PY'
import os
import shutil
import stat
import sys
import tempfile

source, destination = sys.argv[1:]
directory = os.path.dirname(destination) or "."
prefix = ".runegen-install."
fd = -1
temporary = None
committed = False
try:
    fd, temporary = tempfile.mkstemp(prefix=prefix, suffix=".tmp", dir=directory)
    source_mode = stat.S_IMODE(os.stat(source, follow_symlinks=False).st_mode)
    with os.fdopen(fd, "wb", closefd=True) as output:
        fd = -1
        with open(source, "rb") as input_file:
            shutil.copyfileobj(input_file, output, length=1024 * 1024)
        output.flush()
        os.fchmod(output.fileno(), source_mode)
        os.fsync(output.fileno())
    os.replace(temporary, destination)
    committed = True
    temporary = None
    try:
        directory_fd = os.open(directory, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    except OSError as error:
        # Rename is already the atomic commit point. A directory-sync failure
        # affects crash durability, not which complete file readers can see.
        print(f"runegen: warning -- directory sync after install failed: {error}",
              file=sys.stderr)
except Exception as error:
    print(f"runegen: atomic install failed: {error}", file=sys.stderr)
    raise SystemExit(1)
finally:
    if fd >= 0:
        os.close(fd)
    if not committed and temporary is not None:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
PY
}

run_one() {
    local map="$1" port="$2" logfile="$3"
    local t0 t1 elapsed pid status detail seeds links runefile status_code
    local stage_dir stage_game staged_rune lintfile wrote_line wrote_payload
    local expected_banner_prefix roots_line red_root blue_root backup_file

    runefile="$LIVE_GAME_DIR/maps/$map.rune"
    stage_dir="$(mktemp -d "$GAMEDIR_ROOT/.runegen-stage.XXXXXX")" || {
        detail="cannot create staging game directory"
        echo "rune: FAILED map=$map port=$port -- $detail"
        RESULT_LINES+=("$map|-|-|0|FAIL|$detail")
        return
    }
    stage_game="${stage_dir##*/}"
    staged_rune="$stage_dir/maps/$map.rune"
    lintfile="${logfile%.log}.lint.log"

    if ! prepare_stage "$stage_dir" "$map"; then
        detail="cannot prepare staging game directory $stage_dir"
        cleanup_stage "$stage_dir"
        echo "rune: FAILED map=$map port=$port -- $detail"
        RESULT_LINES+=("$map|-|-|0|FAIL|$detail")
        return
    fi
    t0=$(date +%s)

    ( sleep "$STARTUP_SLEEP"; echo "sv rune"; sleep "$GEN_BUDGET"; echo "quit" ) | \
        ( cd "$GAMEDIR_ROOT" && exec stdbuf -oL timeout "$TIMEOUT_SECS" "$Q2DED" \
              -portable +set game "$stage_game" +set dedicated 1 \
              +set port "$port" +set net_port "$port" \
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

    expected_banner_prefix="rune: wrote $stage_game/maps/$map.rune ("
    wrote_line="$(grep -F "$expected_banner_prefix" "$logfile" 2>/dev/null | tail -n1)"
    wrote_payload="${wrote_line#"$expected_banner_prefix"}"
    if [ -f "$staged_rune" ] && [ ! -L "$staged_rune" ] && \
            [ "$wrote_payload" != "$wrote_line" ] && \
            [[ "$wrote_payload" =~ ^([0-9]+)\ seeds,\ ([0-9]+)\ links\)$ ]]; then
        seeds="${BASH_REMATCH[1]}"
        links="${BASH_REMATCH[2]}"
        roots_line="$(grep -E 'rune: objective roots red=[0-9]+ blue=[0-9]+' \
            "$logfile" | tail -n1)"
        red_root="$(printf '%s\n' "$roots_line" | \
            sed -n 's/.*red=\([0-9]\+\) blue=.*/\1/p')"
        blue_root="$(printf '%s\n' "$roots_line" | \
            sed -n 's/.*blue=\([0-9]\+\).*/\1/p')"
        if [ -z "$red_root" ] || [ -z "$blue_root" ]; then
            status="FAIL"
            detail="generator omitted authoritative post-spawn objective roots"
            cleanup_stage "$stage_dir"
            echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
            RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
            return
        fi
        if ! python3 "$RUNE_LINT" --runtime-v3 \
                --objective-roots "$red_root" "$blue_root" \
                "$staged_rune" > "$lintfile" 2>&1; then
            status="FAIL"
            detail="runtime-v3 quality gate failed (see $lintfile)"
            echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
            sed 's/^/  /' "$lintfile"
            cleanup_stage "$stage_dir"
            RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
            return
        fi

        backup_file="none (no previous rune)"
        if [ -e "$runefile" ] || [ -L "$runefile" ]; then
            mkdir -p "$RUNE_BACKUP_DIR" || {
                status="FAIL"
                detail="cannot create backup directory $RUNE_BACKUP_DIR"
                cleanup_stage "$stage_dir"
                echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
                RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
                return
            }
            backup_file="$RUNE_BACKUP_DIR/${map}-$(date +%Y%m%d-%H%M%S-%N).rune"
            if ! cp -p -- "$runefile" "$backup_file"; then
                status="FAIL"
                detail="cannot preserve old rune at $backup_file"
                cleanup_stage "$stage_dir"
                echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
                RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
                return
            fi
        fi
        if ! install_rune_atomic "$staged_rune" "$runefile"; then
            status="FAIL"
            detail="atomic install failed; old rune remains (backup=$backup_file)"
            cleanup_stage "$stage_dir"
            echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
            RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
            return
        fi
        status="ok"
        detail="runtime-v3 gate clean; backup=$backup_file"
        cleanup_stage "$stage_dir"
        echo "rune: installed $runefile ($seeds seeds, $links links) -- ${elapsed}s, map=$map port=$port"
    else
        seeds="-"
        links="-"
        status="FAIL"
        if [ ! -s "$logfile" ]; then
            detail="empty log -- server never produced output"
        else
            detail="$(tail -n1 "$logfile")"
        fi
        cleanup_stage "$stage_dir"
        echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
    fi

    RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
}

i=0
for map in "${MAPS[@]}"; do
    if [[ ! "$map" =~ ^[A-Za-z0-9_][A-Za-z0-9_-]{0,62}$ ]]; then
        echo "runegen: unsafe or invalid map name: $map" >&2
        RESULT_LINES+=("$map|-|-|-|FAIL|map name must match [A-Za-z0-9_][A-Za-z0-9_-]{0,62}")
        i=$(( i + 1 ))
        continue
    fi
    port=$(( PORT_START + i ))
    ts="$(date +%Y%m%d-%H%M%S)"
    logfile="$LOG_DIR/${map}-${ts}.log"

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "[dry-run] map=$map port=$port timeout=${TIMEOUT_SECS}s log=$logfile"
        echo "[dry-run]   ( sleep $STARTUP_SLEEP; echo \"sv rune\"; sleep $GEN_BUDGET; echo \"quit\" ) |" \
             "( cd \"$GAMEDIR_ROOT\" && stdbuf -oL timeout $TIMEOUT_SECS \"$Q2DED\"" \
             "-portable +set game <temporary-stage> +set dedicated 1 +set port $port +exec $CFG +map $map ) > \"$logfile\" 2>&1"
        echo "[dry-run]   parse authoritative red/blue roots from the server log"
        echo "[dry-run]   python3 \"$RUNE_LINT\" --runtime-v3 --objective-roots RED BLUE <staged>/$map.rune"
        echo "[dry-run]   preserve old rune under $RUNE_BACKUP_DIR, then atomically install into $LIVE_GAME_DIR/maps/$map.rune"
        RESULT_LINES+=("$map|-|-|-|DRY-RUN|stage -> runtime-v3 gate -> backup -> atomic install")
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
