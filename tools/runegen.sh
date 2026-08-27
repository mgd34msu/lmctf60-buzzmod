#!/usr/bin/env bash

set -u

Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
GAMEDIR_ROOT="${GAMEDIR_ROOT:-$HOME/Games/Quake2}"
GAME="${GAME:-lmctf-hooktest}"
CFG="${CFG:-rune.cfg}"
MAXCLIENTS="${MAXCLIENTS:-16}"
PORT_START="${PORT_START:-28500}"
STARTUP_SLEEP="${STARTUP_SLEEP:-8}"

for number in "$PORT_START" "$STARTUP_SLEEP"; do
    if [[ ! "$number" =~ ^[0-9]+$ ]]; then
        echo "runegen: port and startup delay must be non-negative integers" >&2
        exit 2
    fi
done
if [[ ! "$MAXCLIENTS" =~ ^[1-9][0-9]*$ ]] || [ "$MAXCLIENTS" -gt 256 ]; then
    echo "runegen: MAXCLIENTS must be an integer in [1,256]" >&2
    exit 2
fi
if [[ ! "$GAME" =~ ^[A-Za-z0-9_][A-Za-z0-9_.-]{0,62}$ ]] || \
        [[ ! "$CFG" =~ ^[A-Za-z0-9_][A-Za-z0-9_.-]{0,62}$ ]]; then
    echo "runegen: GAME and CFG must be safe single names" >&2
    exit 2
fi
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="${RUNE_LOG_DIR:-$SCRIPT_DIR/rune-logs}"
RUNE_LINT="$SCRIPT_DIR/runelint.py"
RUNE_IO="$SCRIPT_DIR/runeio.py"
RUNE_PAIR="$SCRIPT_DIR/runegen_pair.py"
RUNE_ACCEPT="${RUNE_ACCEPT:-$PROJECT_ROOT/runeaccept.gnu}"
RUNE_BACKUP_DIR="${RUNE_BACKUP_DIR:-$LOG_DIR/backups}"
LMCTF58_ACCEPT="$SCRIPT_DIR/lmctf58_rune_accept.py"
DRY_RUN=0
MAPS=()

for argument in "$@"; do
    case "$argument" in
        --dry-run)
            DRY_RUN=1
            ;;
        -h|--help)
            echo "usage: $0 [--dry-run] <map1> [map2 ...]"
            exit 0
            ;;
        *)
            MAPS+=("$argument")
            ;;
    esac
done
if [ "${#MAPS[@]}" -eq 0 ]; then
    echo "usage: $0 [--dry-run] <map1> [map2 ...]" >&2
    exit 1
fi

if ! GAMEDIR_ROOT="$(cd "$GAMEDIR_ROOT" 2>/dev/null && pwd -P)" || \
        [ ! -d "$GAMEDIR_ROOT/$GAME/maps" ]; then
    echo "runegen: game maps directory not found" >&2
    exit 1
fi
LIVE_GAME_DIR="$GAMEDIR_ROOT/$GAME"
Q2DED_REAL=""
Q2DED_DIR=""

check_rune_accept_freshness() {
    local query_status
    (
        cd "$PROJECT_ROOT" &&
        env -u MAKEFLAGS -u MFLAGS -u GNUMAKEFLAGS \
            make --no-print-directory -q -o GitRevisionInfo.h -o .depend \
                -f "$RUNE_ACCEPT_BUILD_FILE" "$RUNE_ACCEPT_BUILD_TARGET"
    ) >/dev/null 2>&1
    query_status=$?
    if [ "$query_status" -eq 0 ]; then
        return 0
    fi
    if [ "$query_status" -eq 1 ]; then
        echo "runegen: C artifact acceptor is stale; rebuild target $RUNE_ACCEPT_BUILD_TARGET before generation" >&2
    else
        echo "runegen: C artifact acceptor build-freshness check failed" >&2
    fi
    return 1
}

if [ "$DRY_RUN" -eq 0 ]; then
    for path in "$RUNE_LINT" "$RUNE_IO" "$RUNE_PAIR"; do
        if [ ! -r "$path" ]; then
            echo "runegen: required tool is not readable: $path" >&2
            exit 1
        fi
    done
    if ! Q2DED_REAL="$(readlink -f -- "$Q2DED")" || \
            [ ! -x "$Q2DED_REAL" ] || \
            ! Q2DED_DIR="$(cd "$(dirname "$Q2DED_REAL")" && pwd -P)" || \
            [ "$Q2DED_DIR" = "/" ]; then
        echo "runegen: q2ded binary not found or not executable: $Q2DED" >&2
        exit 1
    fi
    if [ ! -x "$RUNE_ACCEPT" ]; then
        echo "runegen: C artifact acceptor not executable: $RUNE_ACCEPT" >&2
        exit 1
    fi
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
                echo "runegen: custom RUNE_ACCEPT requires explicit build file and target" >&2
                exit 1
                ;;
        esac
    fi
    if ! check_rune_accept_freshness; then
        exit 1
    fi
fi

mkdir -p "$LOG_DIR"
if command -v pgrep >/dev/null 2>&1; then
    existing="$(pgrep -x q2ded 2>/dev/null || true)"
    if [ -n "$existing" ]; then
        echo "runegen: q2ded already running at PID(s) $existing; not touching them" >&2
    fi
fi

RESULT_LINES=()
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
    portable_stage="$Q2DED_DIR/$stage_name"
    case "$portable_stage" in
        "$Q2DED_DIR"/.runegen-stage.*) ;;
        *)
            echo "runegen: refusing to remove unexpected portable path" >&2
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

handle_signal() {
    local signal="$1"
    cleanup_active_run
    trap - "$signal"
    kill -s "$signal" "$$"
}

trap cleanup_active_run EXIT
trap 'handle_signal HUP' HUP
trap 'handle_signal INT' INT
trap 'handle_signal TERM' TERM

prepare_stage() {
    local stage_dir="$1" map="$2" entry base
    mkdir -p "$stage_dir/maps" "$stage_dir/players" "$stage_dir/demos" \
        "$stage_dir/screenshots" || return 1
    for entry in "$LIVE_GAME_DIR"/*; do
        [ -e "$entry" ] || continue
        base="${entry##*/}"
        case "$base" in
            maps|players|demos|screenshots)
                continue
                ;;
            game.so|gamex86_64.so|*.cfg)
                cp -p -- "$entry" "$stage_dir/$base" || return 1
                ;;
            *)
                ln -s -- "$entry" "$stage_dir/$base" || return 1
                ;;
        esac
    done
    for base in game.so gamex86_64.so "$CFG"; do
        if [ ! -f "$stage_dir/$base" ] || [ -L "$stage_dir/$base" ]; then
            echo "runegen: staged input is not a frozen regular file: $base" >&2
            return 1
        fi
    done
    for entry in "$LIVE_GAME_DIR/maps"/*; do
        [ -e "$entry" ] || [ -L "$entry" ] || continue
        base="${entry##*/}"
        case "$base" in
            "$map.rune"|"$map.snag")
                continue
                ;;
        esac
        ln -s -- "$entry" "$stage_dir/maps/$base" || return 1
    done
}

run_engine() {
    local phase="$1" stage_dir="$2" map="$3" port="$4" logfile="$5"
    local stage_game pid status_code
    stage_game="${stage_dir##*/}"
    if [ "$phase" = "generation" ]; then
        (
            sleep "$STARTUP_SLEEP"
            printf '%s\n' "maxclients" "sv rune" "quit"
        ) | (
            cd "$GAMEDIR_ROOT" &&
            exec stdbuf -oL \
                "$Q2DED_REAL" \
                -portable +set game "$stage_game" +set dedicated 1 \
                +set maxclients "$MAXCLIENTS" +set port "$port" \
                +set net_port "$port" +exec "$CFG" \
                +set maxclients "$MAXCLIENTS" +map "$map"
        ) >"$logfile" 2>&1 &
    else
        (
            sleep "$STARTUP_SLEEP"
            printf '%s\n' "maxclients" "sv sg add red" "quit"
        ) | (
            cd "$GAMEDIR_ROOT" &&
            exec stdbuf -oL \
                "$Q2DED_REAL" \
                -portable +set game "$stage_game" +set dedicated 1 \
                +set maxclients "$MAXCLIENTS" +set port "$port" \
                +set net_port "$port" +exec "$CFG" \
                +set maxclients "$MAXCLIENTS" +map "$map"
        ) >"$logfile" 2>&1 &
    fi
    pid=$!
    ACTIVE_SERVER_PID="$pid"
    wait "$pid"
    status_code=$?
    ACTIVE_SERVER_PID=""
    if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid" 2>/dev/null || true
    fi
    return "$status_code"
}

require_maxclients_report() {
    local logfile="$1" query_count expected_count
    query_count="$(grep -cE '^"maxclients" is "[^"]+"$' "$logfile" || true)"
    expected_count="$(grep -cFx "\"maxclients\" is \"$MAXCLIENTS\"" \
        "$logfile" || true)"
    [ "$query_count" -eq 1 ] && [ "$expected_count" -eq 1 ]
}

fail_run() {
    local map="$1" port="$2" elapsed="$3" detail="$4" stage_dir="$5"
    cleanup_stage "$stage_dir"
    echo "rune: FAILED map=$map port=$port (${elapsed}s) -- $detail"
    RESULT_LINES+=("$map|-|-|$elapsed|FAIL|$detail")
}

run_one() {
    local map="$1" port="$2" log_stem="$3"
    local stage_dir stage_game staged_rune generation_log cold_log
    local accept_log inspect_log count_log lint_log semantic_log
    local evidence provenance manifest backup_manifest
    local t0 t1 elapsed status_code detail write_prefix write_record write_line
    local write_count write_payload roots_count roots_record roots_line roots_number
    local write_number red_root blue_root seeds links nodes triggers inventory plans
    local failure_after_write rune_sha snag_sha evidence_sha

    python3 "$RUNE_PAIR" recover --map "$map" \
        --live-maps "$LIVE_GAME_DIR/maps" || {
        detail="cannot recover prior pair transaction"
        echo "rune: FAILED map=$map port=$port -- $detail"
        RESULT_LINES+=("$map|-|-|0|FAIL|$detail")
        return
    }
    stage_dir="$(mktemp -d "$GAMEDIR_ROOT/.runegen-stage.XXXXXX")" || {
        detail="cannot create staging game directory"
        echo "rune: FAILED map=$map port=$port -- $detail"
        RESULT_LINES+=("$map|-|-|0|FAIL|$detail")
        return
    }
    ACTIVE_STAGE_DIR="$stage_dir"
    stage_game="${stage_dir##*/}"
    staged_rune="$stage_dir/maps/$map.rune"
    generation_log="$log_stem-generation.log"
    cold_log="$log_stem-cold.log"
    accept_log="$log_stem.accept.json"
    inspect_log="$log_stem.inspect.json"
    count_log="$log_stem.counts.log"
    lint_log="$log_stem.lint.log"
    semantic_log="$log_stem.semantic.log"
    evidence="$log_stem.snag-bootstrap-evidence.json"
    provenance="$log_stem.provenance.json"
    manifest="$log_stem.pair.json"
    if ! prepare_stage "$stage_dir" "$map"; then
        fail_run "$map" "$port" 0 "cannot prepare staging game directory" "$stage_dir"
        return
    fi
    t0="$(date +%s)"
    run_engine generation "$stage_dir" "$map" "$port" "$generation_log"
    status_code=$?
    t1="$(date +%s)"
    elapsed=$(( t1 - t0 ))
    if [ "$status_code" -ne 0 ]; then
        fail_run "$map" "$port" "$elapsed" \
            "generation engine exited nonzero status=$status_code (see $generation_log)" \
            "$stage_dir"
        return
    fi
    if ! require_maxclients_report "$generation_log"; then
        fail_run "$map" "$port" "$elapsed" \
            "generation engine did not confirm maxclients=$MAXCLIENTS" "$stage_dir"
        return
    fi
    if grep -qE '^slipgate: (snag|rune) ready ' "$generation_log"; then
        fail_run "$map" "$port" "$elapsed" \
            "generation log contains forbidden runtime readiness" "$stage_dir"
        return
    fi
    write_prefix="rune: wrote $stage_game/maps/$map.rune ("
    write_count="$(grep -cF "$write_prefix" "$generation_log" || true)"
    roots_count="$(grep -cE '^rune: objective roots red=[0-9]+ blue=[0-9]+$' \
        "$generation_log" || true)"
    if [ "$write_count" -ne 1 ] || [ "$roots_count" -ne 1 ]; then
        fail_run "$map" "$port" "$elapsed" \
            "generation requires one objective-root line and one write banner" \
            "$stage_dir"
        return
    fi
    write_record="$(grep -nF "$write_prefix" "$generation_log")"
    write_number="${write_record%%:*}"
    write_line="${write_record#*:}"
    write_payload="${write_line#"$write_prefix"}"
    if [ ! -f "$staged_rune" ] || [ -L "$staged_rune" ] || \
            [[ ! "$write_payload" =~ ^([0-9]+)\ seeds,\ ([0-9]+)\ links,\ ([0-9]+)\ mechanism\ nodes,\ ([0-9]+)\ triggers,\ ([0-9]+)\ inventory\ edges,\ ([0-9]+)\ activation\ plans\)$ ]]; then
        fail_run "$map" "$port" "$elapsed" \
            "generation write banner or staged RUNE is malformed" "$stage_dir"
        return
    fi
    seeds="${BASH_REMATCH[1]}"
    links="${BASH_REMATCH[2]}"
    nodes="${BASH_REMATCH[3]}"
    triggers="${BASH_REMATCH[4]}"
    inventory="${BASH_REMATCH[5]}"
    plans="${BASH_REMATCH[6]}"
    roots_record="$(grep -nE '^rune: objective roots red=[0-9]+ blue=[0-9]+$' \
        "$generation_log")"
    roots_number="${roots_record%%:*}"
    roots_line="${roots_record#*:}"
    red_root="$(sed -n 's/.*red=\([0-9]\+\) blue=.*/\1/p' <<<"$roots_line")"
    blue_root="$(sed -n 's/.*blue=\([0-9]\+\).*/\1/p' <<<"$roots_line")"
    if [ "$roots_number" -ge "$write_number" ] || [ "$red_root" = "$blue_root" ] || \
            [ "$red_root" -ge "$seeds" ] || [ "$blue_root" -ge "$seeds" ]; then
        fail_run "$map" "$port" "$elapsed" \
            "objective roots must precede write and name distinct in-range seeds" \
            "$stage_dir"
        return
    fi
    failure_after_write="$(awk -v after="$write_number" '
        NR > after && ($0 ~ /^rune: rejected / ||
            $0 ~ /^rune: FAILED([: ]|$)/ ||
            $0 ~ /^rune: generation refused / ||
            $0 ~ /^rune: revalidation failed / ||
            $0 ~ /^rune: install failed / ||
            $0 ~ /^rune: cleanup restored pending door scope;/) { line = $0 }
        END { print line }
    ' "$generation_log")"
    if [ -n "$failure_after_write" ]; then
        fail_run "$map" "$port" "$elapsed" \
            "generator failure occurred after write: $failure_after_write" "$stage_dir"
        return
    fi
    if ! check_rune_accept_freshness; then
        fail_run "$map" "$port" "$elapsed" \
            "C artifact acceptor became stale during generation" "$stage_dir"
        return
    fi
    if ! "$RUNE_ACCEPT" "$staged_rune" >"$accept_log" 2>&1; then
        fail_run "$map" "$port" "$elapsed" \
            "C artifact acceptance failed (see $accept_log)" "$stage_dir"
        return
    fi
    if ! python3 "$RUNE_IO" "$staged_rune" >"$inspect_log" 2>&1; then
        fail_run "$map" "$port" "$elapsed" \
            "Python artifact inspection failed (see $inspect_log)" "$stage_dir"
        return
    fi
    if ! python3 - "$accept_log" "$inspect_log" "$map" "$seeds" "$links" \
            "$nodes" "$triggers" "$inventory" "$plans" >"$count_log" 2>&1 <<'PY'
import json
import sys

accept_path, inspect_path, map_name, *raw_counts = sys.argv[1:]
names = ("seed_count", "link_count", "node_count", "trigger_count",
         "inventory_edge_count", "plan_count")
expected = dict(zip(names, map(int, raw_counts)))
with open(accept_path, encoding="utf-8") as stream:
    c_report = json.load(stream)
with open(inspect_path, encoding="utf-8") as stream:
    py_report = json.load(stream)
if c_report.get("map_name") != map_name or py_report.get("map_name") != map_name:
    raise SystemExit("artifact report map mismatch")
agreement = ("seed_count", "link_count", "node_count", "trigger_count",
             "inventory_edge_count", "plan_edge_count", "edge_count", "plan_count")
for name in agreement:
    if c_report.get(name) != py_report.get(name):
        raise SystemExit(f"C/Python {name} mismatch")
for name, value in expected.items():
    if py_report.get(name) != value:
        raise SystemExit(f"artifact/write {name} mismatch")
PY
    then
        fail_run "$map" "$port" "$elapsed" \
            "C/Python/write artifact counts disagree (see $count_log)" "$stage_dir"
        return
    fi
    if ! python3 "$RUNE_LINT" --objective-roots "$red_root" "$blue_root" \
            "$staged_rune" >"$lint_log" 2>&1; then
        fail_run "$map" "$port" "$elapsed" \
            "quality gate failed (see $lint_log)" "$stage_dir"
        return
    fi
    if [ "$map" = "lmctf58" ]; then
        semantic_status=0
        python3 "$LMCTF58_ACCEPT" --objective-roots "$red_root" "$blue_root" \
            "$staged_rune" >"$semantic_log" 2>&1 || semantic_status=$?
        if [ "$semantic_status" -gt 1 ]; then
            fail_run "$map" "$port" "$elapsed" \
                "lmctf58 diagnostic infrastructure failure (see $semantic_log)" \
                "$stage_dir"
            return
        fi
    fi
    if ! python3 "$RUNE_PAIR" provenance --map "$map" --rune "$staged_rune" \
            --q2ded "$Q2DED_REAL" --config "$stage_dir/$CFG" \
            --module "$stage_dir/game.so" --module "$stage_dir/gamex86_64.so" \
            --maxclients "$MAXCLIENTS" --count "seeds=$seeds" \
            --count "links=$links" --count "mechanism_nodes=$nodes" \
            --count "plans=$plans" --output "$provenance" || \
            ! python3 "$RUNE_PAIR" stage --map "$map" \
                --stage-maps "$stage_dir/maps" --evidence "$evidence" \
                --provenance "$provenance" --manifest "$manifest"; then
        fail_run "$map" "$port" "$elapsed" \
            "explicit-zero pair staging failed" "$stage_dir"
        return
    fi
    run_engine cold "$stage_dir" "$map" "$port" "$cold_log"
    status_code=$?
    t1="$(date +%s)"
    elapsed=$(( t1 - t0 ))
    if [ "$status_code" -ne 0 ]; then
        fail_run "$map" "$port" "$elapsed" \
            "cold-load engine exited nonzero status=$status_code (see $cold_log)" \
            "$stage_dir"
        return
    fi
    if ! require_maxclients_report "$cold_log" || \
            ! python3 "$RUNE_PAIR" verify-cold-load --manifest "$manifest" \
                --cold-log "$cold_log"; then
        fail_run "$map" "$port" "$elapsed" \
            "cold-load pair attestation failed (see $cold_log)" "$stage_dir"
        return
    fi
    if ! backup_manifest="$(python3 "$RUNE_PAIR" install --manifest "$manifest" \
            --live-maps "$LIVE_GAME_DIR/maps" --backup-dir "$RUNE_BACKUP_DIR")"; then
        fail_run "$map" "$port" "$elapsed" \
            "journaled pair install failed" "$stage_dir"
        return
    fi
    rune_sha="$(sha256sum "$LIVE_GAME_DIR/maps/$map.rune" | awk '{print $1}')"
    snag_sha="$(sha256sum "$LIVE_GAME_DIR/maps/$map.snag" | awk '{print $1}')"
    evidence_sha="$(sha256sum "$evidence" | awk '{print $1}')"
    cleanup_stage "$stage_dir"
    echo "rune: installed pair rune=$LIVE_GAME_DIR/maps/$map.rune sha256=$rune_sha"
    echo "rune: installed pair snag=$LIVE_GAME_DIR/maps/$map.snag sha256=$snag_sha"
    echo "rune: evidence=$evidence sha256=$evidence_sha backup=$backup_manifest"
    RESULT_LINES+=("$map|$seeds|$links|$elapsed|ok|pair gate clean; backup=$backup_manifest")
}

index=0
for map in "${MAPS[@]}"; do
    if [[ ! "$map" =~ ^[A-Za-z0-9_][A-Za-z0-9_-]{0,62}$ ]]; then
        echo "runegen: unsafe or invalid map name: $map" >&2
        RESULT_LINES+=("$map|-|-|-|FAIL|invalid map name")
        index=$(( index + 1 ))
        continue
    fi
    port=$(( PORT_START + index ))
    timestamp="$(date +%Y%m%d-%H%M%S-%N)"
    log_stem="$LOG_DIR/${map}-${timestamp}"
    if [ "$DRY_RUN" -eq 1 ]; then
        echo "[dry-run] map=$map port=$port maxclients=$MAXCLIENTS no elapsed deadline"
        echo "[dry-run] generation launch sends only: maxclients, sv rune, quit"
        echo "[dry-run] omit $map.rune and $map.snag; freeze game.so and gamex86_64.so"
        echo "[dry-run] run C/Python/count/root/lint gates on the fresh RUNE"
        echo "[dry-run] stage canonical NO_ACCEPTED_OBSERVATION evidence and repairs=0 SNAG"
        echo "[dry-run] cold launch sends only: maxclients, sv sg add red, quit"
        echo "[dry-run] require ordered exact SNAG-ready then RUNE-ready digest attestation"
        echo "[dry-run] journal SNAG-first, RUNE-second pair install with byte recovery"
        RESULT_LINES+=("$map|-|-|-|DRY-RUN|two engines -> pair proof -> journaled install")
    else
        echo "=== runegen: $map (port $port, maxclients $MAXCLIENTS) ==="
        run_one "$map" "$port" "$log_stem"
    fi
    index=$(( index + 1 ))
done

echo
printf '%-12s %8s %8s %8s %-8s %s\n' \
    map seeds links seconds status detail
printf '%-12s %8s %8s %8s %-8s %s\n' \
    ------------ -------- -------- -------- -------- ------
for line in "${RESULT_LINES[@]}"; do
    IFS='|' read -r rmap rseeds rlinks rseconds rstatus rdetail <<<"$line"
    printf '%-12s %8s %8s %8s %-8s %s\n' \
        "$rmap" "$rseeds" "$rlinks" "$rseconds" "$rstatus" "$rdetail"
done
for line in "${RESULT_LINES[@]}"; do
    case "$line" in
        *"|FAIL|"*) exit 1 ;;
    esac
done
exit 0
