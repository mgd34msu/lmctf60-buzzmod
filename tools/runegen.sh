#!/usr/bin/env bash
#
# runegen.sh -- serial batch RUNE generator for SLIPGATE.
#
# For each map named on the command line: launch a dedicated q2ded server on
# a unique port, let it boot, issue the console command "sv rune" (which
# generates the rune for whatever map is CURRENTLY loaded). The server boots
# in a temporary, portable game-directory mirror, so Rune_Generate writes to
# staging rather than over the deployed graph. runelint then validates the
# exact layout and both flag-objective reverse components. Only
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
MAXCLIENTS="${MAXCLIENTS:-16}"  # mechanism keys depend on reserved client slots;
                                 # this must match the target server fleet

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
if [[ ! "$MAXCLIENTS" =~ ^[1-9][0-9]*$ ]] || [ "$MAXCLIENTS" -gt 256 ]; then
    echo "runegen: MAXCLIENTS must be an integer in [1,256]" >&2
    exit 2
fi
TIMEOUT_SECS=$(( STARTUP_SLEEP + GEN_BUDGET + SHUTDOWN_MARGIN ))

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="${RUNE_LOG_DIR:-$SCRIPT_DIR/rune-logs}"
RUNE_LINT="$SCRIPT_DIR/runelint.py"
RUNE_IO="$SCRIPT_DIR/runeio.py"
RUNE_ACCEPT="${RUNE_ACCEPT:-$PROJECT_ROOT/runeaccept.gnu}"
RUNE_BACKUP_DIR="${RUNE_BACKUP_DIR:-$LOG_DIR/backups}"

DRY_RUN=0

check_rune_accept_freshness() {
    local accept_build_status

    # A caller may itself be a forced Make recipe.  Inherited -B would make a
    # read-only -q freshness query report every target stale regardless of its
    # timestamps, so this nested query must own its Make flags.
    ( cd "$PROJECT_ROOT" && \
	env -u MAKEFLAGS -u MFLAGS -u GNUMAKEFLAGS \
        make --no-print-directory -q -o GitRevisionInfo.h -o .depend \
            -f "$RUNE_ACCEPT_BUILD_FILE" "$RUNE_ACCEPT_BUILD_TARGET" ) \
        >/dev/null 2>&1
    accept_build_status=$?
    if [ "$accept_build_status" -eq 0 ]; then
        return 0
    fi
    if [ "$accept_build_status" -eq 1 ]; then
        echo "runegen: C artifact acceptor is stale; rebuild target $RUNE_ACCEPT_BUILD_TARGET before generation" >&2
    else
        echo "runegen: C artifact acceptor build-freshness check failed" >&2
    fi
    return 1
}

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

if [ "$DRY_RUN" -eq 0 ] && [ ! -r "$RUNE_IO" ]; then
    echo "runegen: rune inspector not readable: $RUNE_IO" >&2
    exit 1
fi

if [ "$DRY_RUN" -eq 0 ] && [ ! -x "$RUNE_ACCEPT" ]; then
    echo "runegen: C artifact acceptor not executable: $RUNE_ACCEPT" >&2
    exit 1
fi

if [ "$DRY_RUN" -eq 0 ]; then
    if [ -z "${RUNE_ACCEPT_BUILD_FILE:-}" ] || \
            [ -z "${RUNE_ACCEPT_BUILD_TARGET:-}" ]; then
        case "$RUNE_ACCEPT" in
            "$PROJECT_ROOT/runeaccept.gnu")
                RUNE_ACCEPT_BUILD_FILE="$PROJECT_ROOT/GNUmakefile"
                RUNE_ACCEPT_BUILD_TARGET="runeaccept.gnu"
                ;;
            "$PROJECT_ROOT/runeaccept.make")
                RUNE_ACCEPT_BUILD_FILE="$PROJECT_ROOT/Makefile"
                RUNE_ACCEPT_BUILD_TARGET="runeaccept.make"
                ;;
            *)
                echo "runegen: custom RUNE_ACCEPT requires explicit RUNE_ACCEPT_BUILD_FILE and RUNE_ACCEPT_BUILD_TARGET" >&2
                exit 1
                ;;
        esac
    fi
    if [ ! -r "$RUNE_ACCEPT_BUILD_FILE" ] || \
            [[ ! "$RUNE_ACCEPT_BUILD_TARGET" =~ ^[^-][A-Za-z0-9_./-]*$ ]]; then
        echo "runegen: invalid C acceptor build check" >&2
        exit 1
    fi
    if ! command -v make >/dev/null 2>&1; then
        echo "runegen: make is required to verify C acceptor freshness" >&2
        exit 1
    fi
    if ! check_rune_accept_freshness; then
        exit 1
    fi
fi

if ! GAMEDIR_ROOT_ABS="$(cd "$GAMEDIR_ROOT" && pwd)"; then
    echo "runegen: cannot resolve game root: $GAMEDIR_ROOT" >&2
    exit 1
fi
GAMEDIR_ROOT="$GAMEDIR_ROOT_ABS"
LIVE_GAME_DIR="$GAMEDIR_ROOT/$GAME"
Q2DED_REAL=""
Q2DED_DIR=""
if [ "$DRY_RUN" -eq 0 ]; then
    if ! Q2DED_REAL="$(readlink -f -- "$Q2DED")" || \
            [ ! -x "$Q2DED_REAL" ] || \
            ! Q2DED_DIR="$(cd "$(dirname "$Q2DED_REAL")" && pwd -P)" || \
            [ -z "$Q2DED_DIR" ] || [ "$Q2DED_DIR" = "/" ]; then
        echo "runegen: cannot resolve a safe q2ded directory: $Q2DED" >&2
        exit 1
    fi
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
ACTIVE_STAGE_DIR=""
ACTIVE_SERVER_PID=""

cleanup_stage() {
    local stage_dir="$1" stage_name portable_stage

    case "$stage_dir" in
        "$GAMEDIR_ROOT"/.runegen-stage.*)
            stage_name="${stage_dir##*/}"
            ;;
        *)
            echo "runegen: refusing to remove unexpected stage path: $stage_dir" >&2
            return 1
            ;;
    esac

    # Yamagi's -portable mode owns a second write directory beside q2ded.
    # The artifact is staged under GAMEDIR_ROOT, while qconsole and the
    # portable save/screenshot trees land here. Derive it only from the exact
    # validated stage basename; never enumerate or pattern-delete other runs.
    portable_stage="$Q2DED_DIR/$stage_name"
    case "$portable_stage" in
        "$Q2DED_DIR"/.runegen-stage.*)
            ;;
        *)
            echo "runegen: refusing to remove unexpected portable stage path: $portable_stage" >&2
            return 1
            ;;
    esac

    rm -rf -- "$stage_dir"
    if [ "$portable_stage" != "$stage_dir" ]; then
        rm -rf -- "$portable_stage"
    fi
    if [ "$ACTIVE_STAGE_DIR" = "$stage_dir" ]; then
        ACTIVE_STAGE_DIR=""
    fi
}

cleanup_active_run() {
    local active_pid="$ACTIVE_SERVER_PID" active_stage="$ACTIVE_STAGE_DIR"

    ACTIVE_SERVER_PID=""
    if [[ "$active_pid" =~ ^[1-9][0-9]*$ ]] && \
            kill -0 "$active_pid" 2>/dev/null; then
        kill -TERM "$active_pid" 2>/dev/null || true
        wait "$active_pid" 2>/dev/null || true
    fi
    if [ -n "$active_stage" ]; then
        cleanup_stage "$active_stage"
    fi
}

handle_runegen_signal() {
    local signal="$1"

    cleanup_active_run
    trap - "$signal"
    kill -s "$signal" "$$"
}

trap cleanup_active_run EXIT
trap 'handle_runegen_signal HUP' HUP
trap 'handle_runegen_signal INT' INT
trap 'handle_runegen_signal TERM' TERM

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
    local mechanism_nodes trigger_count inventory_edges activation_plans
    local stage_dir stage_game staged_rune lintfile acceptfile inspectfile countfile
    local wrote_count wrote_record wrote_line
    local wrote_line_number wrote_payload runtime_banner_prefix
    local runtime_ready_record runtime_ready_line runtime_ready_payload
    local runtime_seeds runtime_links runtime_nodes runtime_plans
    local expected_banner_prefix roots_count roots_record roots_line_number roots_line
    local red_root blue_root backup_file
    local post_write_failure_line

    runefile="$LIVE_GAME_DIR/maps/$map.rune"
    stage_dir="$(mktemp -d "$GAMEDIR_ROOT/.runegen-stage.XXXXXX")" || {
        detail="cannot create staging game directory"
        echo "rune: FAILED map=$map port=$port -- $detail"
        RESULT_LINES+=("$map|-|-|0|FAIL|$detail")
        return
    }
    ACTIVE_STAGE_DIR="$stage_dir"
    stage_game="${stage_dir##*/}"
    staged_rune="$stage_dir/maps/$map.rune"
    lintfile="${logfile%.log}.lint.log"
    acceptfile="${logfile%.log}.accept.json"
    inspectfile="${logfile%.log}.inspect.json"
    countfile="${logfile%.log}.counts.log"

    if ! prepare_stage "$stage_dir" "$map"; then
        detail="cannot prepare staging game directory $stage_dir"
        cleanup_stage "$stage_dir"
        echo "rune: FAILED map=$map port=$port -- $detail"
        RESULT_LINES+=("$map|-|-|0|FAIL|$detail")
        return
    fi
    t0=$(date +%s)

    ( sleep "$STARTUP_SLEEP"; echo "maxclients"; echo "sv rune"; \
        sleep "$GEN_BUDGET"; echo "quit" ) | \
        ( cd "$GAMEDIR_ROOT" && exec stdbuf -oL timeout "$TIMEOUT_SECS" "$Q2DED_REAL" \
              -portable +set game "$stage_game" +set dedicated 1 \
              +set maxclients "$MAXCLIENTS" \
              +set port "$port" +set net_port "$port" \
              +exec "$CFG" +set maxclients "$MAXCLIENTS" \
              +map "$map" ) > "$logfile" 2>&1 &
    pid=$!
    ACTIVE_SERVER_PID="$pid"

    wait "$pid"
    status_code=$?
    ACTIVE_SERVER_PID=""

    # Belt-and-braces: if something is still alive at this exact PID
    # (should not happen -- `timeout` owns the kill), take it down by PID
    # only. No name/pattern search.
    if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid" 2>/dev/null
    fi

    t1=$(date +%s)
    elapsed=$(( t1 - t0 ))

    if [ "$status_code" -ne 0 ]; then
        seeds="-"
        links="-"
        status="FAIL"
        detail="server process exited nonzero status=$status_code (see $logfile)"
        cleanup_stage "$stage_dir"
        echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
        RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
        return
    fi

    # `exec` inserts the staged cfg before the remaining command-line text.
    # A cfg may therefore change this latched server cvar after an earlier
    # command-line assignment.  The second +set above is the authoritative
    # pre-map value; require the running server itself to report it before
    # accepting an artifact whose mechanism keys depend on reserved clients.
    maxclients_query_count="$(grep -cE '^"maxclients" is "[^"]+"$' \
        "$logfile" 2>/dev/null || true)"
    maxclients_expected_count="$(grep -cFx \
        "\"maxclients\" is \"$MAXCLIENTS\"" "$logfile" 2>/dev/null || true)"
    if [ "$maxclients_query_count" -ne 1 ] || \
            [ "$maxclients_expected_count" -ne 1 ]; then
        seeds="-"
        links="-"
        status="FAIL"
        detail="running server did not confirm authoritative maxclients=$MAXCLIENTS (see $logfile)"
        cleanup_stage "$stage_dir"
        echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
        RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
        return
    fi

    expected_banner_prefix="rune: wrote $stage_game/maps/$map.rune ("
    wrote_count="$(grep -cF "$expected_banner_prefix" "$logfile" 2>/dev/null || true)"
    if [ "$wrote_count" -ne 1 ]; then
        seeds="-"
        links="-"
        status="FAIL"
        detail="expected exactly one write banner for requested map; found $wrote_count (see $logfile)"
        cleanup_stage "$stage_dir"
        echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
        RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
        return
    fi
    wrote_record="$(grep -nF "$expected_banner_prefix" "$logfile" 2>/dev/null)"
    wrote_line_number="${wrote_record%%:*}"
    wrote_line="${wrote_record#*:}"
    wrote_payload="${wrote_line#"$expected_banner_prefix"}"
    if [ -f "$staged_rune" ] && [ ! -L "$staged_rune" ] && \
            [ "$wrote_payload" != "$wrote_line" ] && \
            [[ "$wrote_payload" =~ ^([0-9]+)\ seeds,\ ([0-9]+)\ links,\ ([0-9]+)\ mechanism\ nodes,\ ([0-9]+)\ triggers,\ ([0-9]+)\ inventory\ edges,\ ([0-9]+)\ activation\ plans\)$ ]]; then
        seeds="${BASH_REMATCH[1]}"
        links="${BASH_REMATCH[2]}"
        mechanism_nodes="${BASH_REMATCH[3]}"
        trigger_count="${BASH_REMATCH[4]}"
        inventory_edges="${BASH_REMATCH[5]}"
        activation_plans="${BASH_REMATCH[6]}"
        post_write_failure_line="$(awk -v after="$wrote_line_number" '
            NR > after && ($0 ~ /^rune: rejected / || $0 ~ /^rune: FAILED([: ]|$)/ || $0 ~ /^rune: generation refused / || $0 ~ /^rune: revalidation failed / || $0 ~ /^rune: install failed / || $0 ~ /^rune: cleanup restored pending door scope;/) { line = $0 }
            END { print line }
        ' "$logfile" 2>/dev/null)"
        if [ -n "$post_write_failure_line" ]; then
            status="FAIL"
            case "$post_write_failure_line" in
                "rune: rejected "*)
                    detail="runtime rejected freshly written artifact (see $logfile)"
                    ;;
                *)
                    detail="generator/runtime failure occurred after write (see $logfile)"
                    ;;
            esac
            cleanup_stage "$stage_dir"
            echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
            echo "  $post_write_failure_line"
            RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
            return
        fi
        runtime_banner_prefix="slipgate: rune ready $map, "
        runtime_ready_record="$(awk -v after="$wrote_line_number" \
            -v prefix="$runtime_banner_prefix" \
            'NR > after && index($0, prefix) == 1 { line = NR ":" $0 } \
             END { print line }' "$logfile" 2>/dev/null)"
        runtime_ready_line="${runtime_ready_record#*:}"
        runtime_ready_payload="${runtime_ready_line#"$runtime_banner_prefix"}"
        if [ -z "$runtime_ready_record" ] || \
                [ "$runtime_ready_payload" = "$runtime_ready_line" ] || \
                [[ ! "$runtime_ready_payload" =~ ^([0-9]+)\ seeds,\ ([0-9]+)\ links,\ ([0-9]+)\ mechanism\ nodes,\ ([0-9]+)\ plans,\ gravity\ -?[0-9]+,\ all\ fields\ up$ ]]; then
            status="FAIL"
            detail="runtime acceptance banner missing or malformed after write (see $logfile)"
            cleanup_stage "$stage_dir"
            echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
            RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
            return
        fi
        runtime_seeds="${BASH_REMATCH[1]}"
        runtime_links="${BASH_REMATCH[2]}"
        runtime_nodes="${BASH_REMATCH[3]}"
        runtime_plans="${BASH_REMATCH[4]}"
        if [ "$runtime_seeds" != "$seeds" ] || \
                [ "$runtime_links" != "$links" ] || \
                [ "$runtime_nodes" != "$mechanism_nodes" ] || \
                [ "$runtime_plans" != "$activation_plans" ]; then
            status="FAIL"
            detail="runtime acceptance counts disagree with write: wrote=$seeds/$links/$mechanism_nodes/$activation_plans runtime=$runtime_seeds/$runtime_links/$runtime_nodes/$runtime_plans (see $logfile)"
            cleanup_stage "$stage_dir"
            echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
            RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
            return
        fi
        roots_count="$(grep -cE '^rune: objective roots red=[0-9]+ blue=[0-9]+$' \
            "$logfile" 2>/dev/null || true)"
        roots_record="$(grep -nE '^rune: objective roots red=[0-9]+ blue=[0-9]+$' \
            "$logfile" 2>/dev/null || true)"
        roots_line_number="${roots_record%%:*}"
        roots_line="${roots_record#*:}"
        red_root="$(printf '%s\n' "$roots_line" | \
            sed -n 's/.*red=\([0-9]\+\) blue=.*/\1/p')"
        blue_root="$(printf '%s\n' "$roots_line" | \
            sed -n 's/.*blue=\([0-9]\+\).*/\1/p')"
        if [ "$roots_count" -ne 1 ] || [ -z "$red_root" ] || [ -z "$blue_root" ]; then
            status="FAIL"
            detail="expected exactly one authoritative objective-root line; found $roots_count"
            cleanup_stage "$stage_dir"
            echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
            RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
            return
        fi
        if [ "$roots_line_number" -ge "$wrote_line_number" ]; then
            status="FAIL"
            detail="authoritative objective-root line must precede the write banner"
            cleanup_stage "$stage_dir"
            echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
            RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
            return
        fi
        if [ "$red_root" = "$blue_root" ] || \
                [ "$red_root" -ge "$seeds" ] || [ "$blue_root" -ge "$seeds" ]; then
            status="FAIL"
            detail="objective roots must be distinct seed indexes in [0,$seeds): red=$red_root blue=$blue_root"
            cleanup_stage "$stage_dir"
            echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
            RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
            return
        fi
        if ! check_rune_accept_freshness; then
            status="FAIL"
            detail="C artifact acceptor became stale during generation"
            cleanup_stage "$stage_dir"
            echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
            RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
            return
        fi
        if ! "$RUNE_ACCEPT" "$staged_rune" \
                > "$acceptfile" 2>&1; then
            status="FAIL"
            detail="C artifact acceptance failed (see $acceptfile)"
            echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
            sed 's/^/  /' "$acceptfile"
            cleanup_stage "$stage_dir"
            RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
            return
        fi
        if ! python3 "$RUNE_IO" "$staged_rune" \
                > "$inspectfile" 2>&1; then
            status="FAIL"
            detail="Python artifact inspection failed (see $inspectfile)"
            echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
            sed 's/^/  /' "$inspectfile"
            cleanup_stage "$stage_dir"
            RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
            return
        fi
        if ! python3 - "$acceptfile" "$inspectfile" "$map" \
                "$seeds" "$links" "$mechanism_nodes" "$trigger_count" \
                "$inventory_edges" "$activation_plans" \
                > "$countfile" 2>&1 <<'PY'
import json
import sys

accept_path, inspect_path, map_name, *expected_text = sys.argv[1:]
expected_values = tuple(int(value) for value in expected_text)
expected = dict(zip(
    (
        "seed_count", "link_count", "node_count", "trigger_count",
        "inventory_edge_count", "plan_count",
    ),
    expected_values,
))

def read_report(path):
    with open(path, "r", encoding="utf-8") as report_file:
        value = json.load(report_file)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: report is not a JSON object")
    return value

try:
    c_report = read_report(accept_path)
    python_report = read_report(inspect_path)
    if c_report.get("map_name") != map_name:
        raise ValueError(
            f"C report map mismatch: {c_report.get('map_name')!r} != {map_name!r}"
        )
    if python_report.get("map_name") != map_name:
        raise ValueError(
            "Python report map mismatch: "
            f"{python_report.get('map_name')!r} != {map_name!r}"
        )
    agreement_fields = (
        "seed_count", "link_count", "node_count", "trigger_count",
        "inventory_edge_count", "plan_edge_count", "edge_count", "plan_count",
    )
    for report_name, report in (("C", c_report), ("Python", python_report)):
        for field in agreement_fields:
            value = report.get(field)
            if type(value) is not int or value < 0:
                raise ValueError(
                    f"{report_name} report has invalid or missing {field}: {value!r}"
                )
    for field in agreement_fields:
        if c_report.get(field) != python_report.get(field):
            raise ValueError(
                f"C/Python {field} mismatch: "
                f"{c_report.get(field)!r} != {python_report.get(field)!r}"
            )
    for field, value in expected.items():
        if python_report.get(field) != value:
            raise ValueError(
                f"artifact/write {field} mismatch: "
                f"{python_report.get(field)!r} != {value!r}"
            )
except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
    print(f"runegen: artifact count agreement failed: {error}", file=sys.stderr)
    raise SystemExit(1)

print("runegen: C/Python/write artifact counts agree")
PY
        then
            status="FAIL"
            detail="C/Python/write artifact counts disagree (see $countfile)"
            echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
            sed 's/^/  /' "$countfile"
            cleanup_stage "$stage_dir"
            RESULT_LINES+=("$map|$seeds|$links|$elapsed|$status|$detail")
            return
        fi
        if ! python3 "$RUNE_LINT" --objective-roots "$red_root" "$blue_root" \
                "$staged_rune" \
                > "$lintfile" 2>&1; then
            status="FAIL"
            detail="quality gate failed (see $lintfile)"
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
        detail="rune gate clean; nodes=$mechanism_nodes triggers=$trigger_count inventory_edges=$inventory_edges plans=$activation_plans; backup=$backup_file"
        cleanup_stage "$stage_dir"
        echo "rune: installed $runefile ($seeds seeds, $links links, $mechanism_nodes mechanism nodes, $trigger_count triggers, $inventory_edges inventory edges, $activation_plans activation plans) -- ${elapsed}s, map=$map port=$port"
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
        echo "[dry-run] map=$map port=$port maxclients=$MAXCLIENTS timeout=${TIMEOUT_SECS}s log=$logfile"
        echo "[dry-run]   ( sleep $STARTUP_SLEEP; echo \"maxclients\"; echo \"sv rune\"; sleep $GEN_BUDGET; echo \"quit\" ) |" \
             "( cd \"$GAMEDIR_ROOT\" && stdbuf -oL timeout $TIMEOUT_SECS \"$Q2DED\"" \
             "-portable +set game <temporary-stage> +set dedicated 1 +set maxclients $MAXCLIENTS +set port $port +exec $CFG +set maxclients $MAXCLIENTS +map $map ) > \"$logfile\" 2>&1"
        echo "[dry-run]   require the running server to report exact authoritative maxclients=$MAXCLIENTS before generation"
        echo "[dry-run]   parse authoritative red/blue roots from the server log"
        echo "[dry-run]   require write and ready banners: seeds, links, mechanism nodes, triggers, inventory edges, activation plans"
        echo "[dry-run]   require the selected C artifact acceptor's explicit build target to be current before generation and again immediately before artifact decoding"
        echo "[dry-run]   require exactly one write banner, distinct in-range roots, clean server exit, and no later failure"
        echo "[dry-run]   require a later exact 'slipgate: rune ready $map, ...' banner with matching seed/link/node/plan counts"
        echo "[dry-run]   require production C and Python structural acceptance, including exact action/plan binding"
        echo "[dry-run]   compare C/Python artifact reports with all six write-banner counts"
        echo "[dry-run]   python3 \"$RUNE_LINT\" --objective-roots RED BLUE <staged>/$map.rune"
        echo "[dry-run]   preserve old rune under $RUNE_BACKUP_DIR, then atomically install into $LIVE_GAME_DIR/maps/$map.rune"
        RESULT_LINES+=("$map|-|-|-|DRY-RUN|stage -> rune gate -> backup -> atomic install")
    else
        echo "=== runegen: $map (port $port, maxclients $MAXCLIENTS) ==="
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
