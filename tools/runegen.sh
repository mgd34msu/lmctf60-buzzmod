#!/usr/bin/env bash

# Generate the fixed RUNE corpus with twelve isolated q2ded workers.

set -u

readonly WORKERS=12
readonly CORPUS_SIZE=175
readonly HARD_REGRESSION_MAPS=(
    bmap5 lmctf01 lmctf06 lmctf11 lmctf12 lmctf15 lmctf19 lmctf25
    lmctf27 lmctf45 tomb05 tw2ctf2 tw2ctf4 xmap06 xmap13 xmap26
)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
# Engine-specific startup flags.  Yamagi takes -portable; the q2pro family
# wants +set basedir <root> +set homedir "" so reads and writes stay in the
# game root.  Word-split on purpose.
Q2DED_FLAGS="${Q2DED_FLAGS:--portable}"
GAMEDIR_ROOT="${GAMEDIR_ROOT:-$HOME/Games/Quake2}"
GAME="${GAME:-lmctf-hooktest}"
CFG="${CFG:-rune.cfg}"
MAXCLIENTS="${MAXCLIENTS:-16}"
PORT_START="${PORT_START:-62000}"
STARTUP_SLEEP="${STARTUP_SLEEP:-1}"
RUNE_GENERATOR_MODULE="${RUNE_GENERATOR_MODULE:-}"
RUNE_COMPACT_READER="${RUNE_COMPACT_READER:-$PROJECT_ROOT/runecompactread.gnu}"
MAP_MANIFEST="${RUNE_MAP_MANIFEST:-$SCRIPT_DIR/rune-corpus-maps.txt}"
RUNEGEN_TEST_IO_FAULT="${RUNEGEN_TEST_IO_FAULT:-}"

usage() {
    echo "usage: $0 [map ...]" >&2
    echo "       no maps selects $MAP_MANIFEST and requires $CORPUS_SIZE maps" >&2
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
    exit 0
fi

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

FULL_MODE=0
MAPS=()
if [ "$#" -eq 0 ]; then
    FULL_MODE=1
    if [ ! -f "$MAP_MANIFEST" ] || [ -L "$MAP_MANIFEST" ]; then
        echo "runegen: map manifest is not a regular file: $MAP_MANIFEST" >&2
        exit 1
    fi
    while IFS= read -r map || [ -n "$map" ]; do
        MAPS+=("$map")
    done < "$MAP_MANIFEST"
else
    MAPS=("$@")
fi

declare -A SELECTED=()
for map in "${MAPS[@]}"; do
    if [[ ! "$map" =~ ^[A-Za-z0-9_][A-Za-z0-9_-]{0,62}$ ]]; then
        echo "runegen: unsafe or invalid map name: $map" >&2
        exit 1
    fi
    if [ "${SELECTED[$map]+yes}" = yes ]; then
        echo "runegen: duplicate map name: $map" >&2
        exit 1
    fi
    SELECTED[$map]=1
done
if [ "${#MAPS[@]}" -eq 0 ]; then
    echo "runegen: no maps selected" >&2
    exit 1
fi
if [ "$FULL_MODE" -eq 1 ] && [ "${#MAPS[@]}" -ne "$CORPUS_SIZE" ]; then
    echo "runegen: full corpus manifest must contain exactly $CORPUS_SIZE unique maps" >&2
    exit 1
fi

if ! GAMEDIR_ROOT="$(cd "$GAMEDIR_ROOT" 2>/dev/null && pwd -P)" || \
        [ ! -d "$GAMEDIR_ROOT/$GAME/maps" ]; then
    echo "runegen: game maps directory not found" >&2
    exit 1
fi
LIVE_GAME_DIR="$GAMEDIR_ROOT/$GAME"
LIVE_MAPS="$LIVE_GAME_DIR/maps"
CORPUS_ROOT="${RUNE_CORPUS_ROOT:-$LIVE_GAME_DIR/.runegen-corpus}"
ACCEPTED_DIR="$CORPUS_ROOT/accepted"
GENERATIONS_DIR="$CORPUS_ROOT/generations"
WORKER_DIR="$CORPUS_ROOT/workers"
OWNED_DIR="$CORPUS_ROOT/owned-q2ded"
LOCK_DIR="$CORPUS_ROOT/.run.lock"
LOCK_FILE="$CORPUS_ROOT/.run.lockfile"
MANIFEST_OUT="$CORPUS_ROOT/manifest.tsv"
Q2DED_REAL=""
GENERATOR_REAL=""
RUNE_COMPACT_READER_REAL=""
RUN_ID="$$-$(date +%Y%m%d%H%M%S)"

if ! Q2DED_REAL="$(readlink -f -- "$Q2DED")" || [ ! -x "$Q2DED_REAL" ]; then
    echo "runegen: q2ded binary not found or not executable: $Q2DED" >&2
    exit 1
fi
if [ -z "$RUNE_GENERATOR_MODULE" ] || [ ! -f "$RUNE_GENERATOR_MODULE" ] || \
        [ -L "$RUNE_GENERATOR_MODULE" ] || \
        ! GENERATOR_REAL="$(readlink -f -- "$RUNE_GENERATOR_MODULE")" || \
        [ ! -f "$GENERATOR_REAL" ]; then
    echo "runegen: RUNE_GENERATOR_MODULE must name a frozen regular module" >&2
    exit 1
fi
if ! RUNE_COMPACT_READER_REAL="$(readlink -f -- "$RUNE_COMPACT_READER")" || \
        [ ! -x "$RUNE_COMPACT_READER_REAL" ]; then
    echo "runegen: canonical compact C reader is not executable: $RUNE_COMPACT_READER" >&2
    exit 1
fi

regular_source() {
    [ -f "$1" ] && [ ! -L "$1" ]
}

for source in "$LIVE_GAME_DIR/$CFG" "$LIVE_GAME_DIR/game.so" \
        "$LIVE_GAME_DIR/gamex86_64.so"; do
    if ! regular_source "$source"; then
        echo "runegen: required frozen runtime input is not a regular file: $source" >&2
        exit 1
    fi
done
for map in "${MAPS[@]}"; do
    if ! regular_source "$LIVE_MAPS/$map.bsp"; then
        echo "runegen: map BSP is not a regular file: $LIVE_MAPS/$map.bsp" >&2
        exit 1
    fi
done

for command in flock sha256sum; do
    if ! command -v "$command" > /dev/null 2>&1; then
        echo "runegen: required command is unavailable: $command" >&2
        exit 1
    fi
done

mkdir -p "$ACCEPTED_DIR" "$GENERATIONS_DIR" "$WORKER_DIR" "$OWNED_DIR" || exit 1

exec {LOCK_FD}> "$LOCK_FILE" || {
    echo "runegen: cannot open corpus lock" >&2
    exit 1
}
if ! flock -n "$LOCK_FD"; then
    echo "runegen: another corpus run owns $CORPUS_ROOT" >&2
    exit 1
fi

owned_process_matches() {
    local pid="$1" stage_game="$2" process_args
    [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 1
    kill -0 "$pid" 2>/dev/null || return 1
    process_args="$(ps -p "$pid" -o args= 2>/dev/null || true)"
    [[ "$process_args" == *"$Q2DED_REAL"* ]] && \
        [[ "$process_args" == *"+set game $stage_game"* ]]
}

remove_owned_stage() {
    local stage_dir="$1"
    case "$stage_dir" in
        "$GAMEDIR_ROOT"/runegen-stage-*)
            rm -rf -- "$stage_dir"
            ;;
        *)
            echo "runegen: refusing to remove an unowned stage path: $stage_dir" >&2
            return 1
            ;;
    esac
}

recover_stale_owned_processes() {
    local record pid stage_game stage_dir
    shopt -s nullglob
    for record in "$OWNED_DIR"/*.pid; do
        if ! IFS=$'\t' read -r pid stage_game stage_dir < "$record"; then
            echo "runegen: malformed owned-process record: $record" >&2
            return 1
        fi
        if kill -0 "$pid" 2>/dev/null; then
            if ! owned_process_matches "$pid" "$stage_game"; then
                echo "runegen: owned-process record no longer matches PID $pid; leaving it alone" >&2
                return 1
            fi
            kill -TERM "$pid" 2>/dev/null || true
            sleep 1
            if owned_process_matches "$pid" "$stage_game"; then
                kill -KILL "$pid" 2>/dev/null || true
            fi
        fi
        if kill -0 "$pid" 2>/dev/null; then
            echo "runegen: cannot clear stale owned q2ded PID $pid" >&2
            return 1
        fi
        rm -f -- "$record"
        remove_owned_stage "$stage_dir" || return 1
    done
}

if [ -d "$LOCK_DIR" ]; then
    if [ -r "$LOCK_DIR/pid" ] && read -r lock_pid < "$LOCK_DIR/pid" && \
            kill -0 "$lock_pid" 2>/dev/null; then
        echo "runegen: another corpus run owns $LOCK_DIR" >&2
        exit 1
    fi
    rm -f -- "$LOCK_DIR/pid"
    if ! rmdir -- "$LOCK_DIR" 2>/dev/null; then
        echo "runegen: stale corpus lock has unexpected contents: $LOCK_DIR" >&2
        exit 1
    fi
fi
if ! recover_stale_owned_processes; then
    exit 1
fi
shopt -s nullglob
for orphan in "$ACCEPTED_DIR"/.runegen-*; do
    [ -f "$orphan" ] && [ ! -L "$orphan" ] && rm -f -- "$orphan"
done
for orphan in "$GENERATIONS_DIR"/.pending-*; do
    case "$orphan" in
        "$GENERATIONS_DIR"/.pending-*) rm -rf -- "$orphan" ;;
    esac
done
for orphan in "$CORPUS_ROOT"/.manifest.*; do
    [ -f "$orphan" ] && [ ! -L "$orphan" ] && rm -f -- "$orphan"
done
if ! mkdir "$LOCK_DIR"; then
    echo "runegen: cannot acquire corpus lock" >&2
    exit 1
fi
if ! printf '%s\n' "$$" > "$LOCK_DIR/pid"; then
    rmdir -- "$LOCK_DIR" 2>/dev/null || true
    echo "runegen: cannot record corpus lock owner" >&2
    exit 1
fi

CHILD_PIDS=()
cleanup_parent() {
    local pid record owned_pid owned_game owned_stage
    for pid in "${CHILD_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid" 2>/dev/null || true
        fi
    done
    for record in "$OWNED_DIR"/*.pid; do
        [ -r "$record" ] || continue
        if IFS=$'\t' read -r owned_pid owned_game owned_stage < "$record" && \
                owned_process_matches "$owned_pid" "$owned_game"; then
            kill -TERM "$owned_pid" 2>/dev/null || true
        fi
    done
    rm -f -- "$LOCK_DIR/pid"
    rmdir -- "$LOCK_DIR" 2>/dev/null || true
}
trap cleanup_parent EXIT
trap 'exit 130' HUP INT TERM

is_hard_regression() {
    local map="$1" hard
    for hard in "${HARD_REGRESSION_MAPS[@]}"; do
        [ "$map" = "$hard" ] && return 0
    done
    return 1
}

ORDINARY_MAPS=()
HARD_MAPS=()
for map in "${MAPS[@]}"; do
    if is_hard_regression "$map"; then
        HARD_MAPS+=("$map")
    else
        ORDINARY_MAPS+=("$map")
    fi
done

declare -A PORT_BY_MAP=()
for index in "${!MAPS[@]}"; do
    PORT_BY_MAP["${MAPS[$index]}"]=$(( PORT_START + index ))
done

copy_regular() {
    local source="$1" destination="$2"
    regular_source "$source" || return 1
    cp -p -- "$source" "$destination"
}

install_stage_modules() {
    local stage_dir="$1" role="$2" module
    case "$role" in
        generator) module="$GENERATOR_REAL" ;;
        runtime) module="" ;;
        *) return 1 ;;
    esac
    if [ "$role" = generator ]; then
        copy_regular "$module" "$stage_dir/game.so" && \
            copy_regular "$module" "$stage_dir/gamex86_64.so"
    else
        copy_regular "$LIVE_GAME_DIR/game.so" "$stage_dir/game.so" && \
            copy_regular "$LIVE_GAME_DIR/gamex86_64.so" "$stage_dir/gamex86_64.so"
    fi
}

prepare_stage() {
    local stage_dir="$1" map="$2" role="$3"
    mkdir -p "$stage_dir/maps" "$stage_dir/players" "$stage_dir/demos" \
        "$stage_dir/screenshots" || return 1
    copy_regular "$LIVE_GAME_DIR/$CFG" "$stage_dir/$CFG" || return 1
    copy_regular "$LIVE_MAPS/$map.bsp" "$stage_dir/maps/$map.bsp" || return 1
    install_stage_modules "$stage_dir" "$role"
}

log_has_complete_lines() {
    local log="$1" last_byte
    [ -s "$log" ] || return 1
    last_byte="$(tail -c 1 "$log" | od -An -tu1 | tr -d '[:space:]')"
    [ "$last_byte" = 10 ]
}

compact_failure_line() {
    case "$1" in
        "rune: compact generation failed "*|"rune: rejected "*|\
        "rune: FAILED"|"rune: FAILED "*|"rune: FAILED:"*|\
        "rune: compact generation refused "*|"rune: generation refused "*|\
        "rune: revalidation failed "*|"rune: install failed "*|\
        "rune: cleanup restored pending door scope;"*)
            return 0
            ;;
    esac
    return 1
}

generation_log_accepted() {
    local log="$1" map="$2" stage_game="$3" line prefix suffix publications=0
    log_has_complete_lines "$log" || return 1
    prefix="rune: compact generation published map=$map path=$stage_game/maps/$map.rune bytes="
    while IFS= read -r line; do
        line="${line%$'\r'}"
        if compact_failure_line "$line"; then
            return 1
        fi
        case "$line" in
            "$prefix"*)
                suffix="${line#"$prefix"}"
                if [[ "$suffix" =~ ^[0-9]+\ durable=1$ ]]; then
                    publications=$(( publications + 1 ))
                fi
                ;;
        esac
    done < "$log"
    [ "$publications" -eq 1 ]
}

cold_log_accepted() {
    local log="$1" map="$2" line ready publications=0
    log_has_complete_lines "$log" || return 1
    ready="slipgate: compact rune ready $map"
    while IFS= read -r line; do
        line="${line%$'\r'}"
        if compact_failure_line "$line"; then
            return 1
        fi
        if [ "$line" = "$ready" ] || [[ "$line" == "$ready, "* ]]; then
            publications=$(( publications + 1 ))
        fi
    done < "$log"
    [ "$publications" -eq 1 ]
}

run_engine() {
    local phase="$1" worker="$2" stage_dir="$3" map="$4" port="$5" logfile="$6"
    local stage_game pid status record
    stage_game="${stage_dir##*/}"
    record="$OWNED_DIR/worker-$worker.pid"
    if [ "$phase" = generation ]; then
        {
            sleep "$STARTUP_SLEEP"
            printf '%s\n' maxclients 'sv rune' quit
        } | (
            cd "$GAMEDIR_ROOT" || exit 1
            printf '%s\t%s\t%s\n' "$BASHPID" "$stage_game" "$stage_dir" \
                > "$record" || exit 1
            exec stdbuf -oL -eL "$Q2DED_REAL" $Q2DED_FLAGS +set game "$stage_game" +set dedicated 1 \
                +set maxclients "$MAXCLIENTS" +set port "$port" +set net_port "$port" \
                +exec "$CFG" +map "$map"
        ) > "$logfile" 2>&1 &
    else
        {
            sleep "$STARTUP_SLEEP"
            printf '%s\n' maxclients 'sv sg add red' quit
        } | (
            cd "$GAMEDIR_ROOT" || exit 1
            printf '%s\t%s\t%s\n' "$BASHPID" "$stage_game" "$stage_dir" \
                > "$record" || exit 1
            exec stdbuf -oL -eL "$Q2DED_REAL" $Q2DED_FLAGS +set game "$stage_game" +set dedicated 1 \
                +set maxclients "$MAXCLIENTS" +set port "$port" +set net_port "$port" \
                +exec "$CFG" +map "$map"
        ) > "$logfile" 2>&1 &
    fi
    pid=$!
    wait "$pid"
    status=$?
    rm -f -- "$record"
    return "$status"
}

stage_path_for() {
    local worker="$1" map="$2"
    mktemp -d "$GAMEDIR_ROOT/runegen-stage-$RUN_ID-w$worker-$map-XXXXXX"
}

canonical_reader_accepts() {
    local artifact="$1" logfile="$2"
    [ -f "$artifact" ] && [ ! -L "$artifact" ] && \
        "$RUNE_COMPACT_READER_REAL" "$artifact" > "$logfile" 2>&1
}

link_artifact() {
    local source="$1" destination="$2"
    ln "$source" "$destination"
}

append_output() {
    local fault_point="$1" destination="$2" contents="$3"
    case "$RUNEGEN_TEST_IO_FAULT" in
        "$fault_point.partial")
            printf '%s' "${contents:0:1}" >> "$destination" || return 1
            return 1
            ;;
        "$fault_point.fail")
            return 1
            ;;
    esac
    printf '%s' "$contents" >> "$destination"
}

rename_output() {
    local fault_point="$1" source="$2" destination="$3"
    if [ "$RUNEGEN_TEST_IO_FAULT" = "$fault_point.fail" ]; then
        return 1
    fi
    mv -f -- "$source" "$destination"
}

discard_new_generation() {
    local generation="$1"
    if ! chmod u+w -- "$generation" || ! rm -rf -- "$generation" || \
            [ -e "$generation" ]; then
        echo "runegen: cannot remove unpublished generation: $generation" >&2
        return 1
    fi
}

cold_load_artifact() {
    local worker="$1" map="$2" artifact="$3" worker_out="$4" port="$5"
    local stage_dir cold_log
    stage_dir="$(stage_path_for "$worker" "$map")" || return 1
    cold_log="$worker_out/$map.$RUN_ID.cold.log"
    if ! prepare_stage "$stage_dir" "$map" runtime || \
            ! link_artifact "$artifact" "$stage_dir/maps/$map.rune" || \
            ! run_engine cold "$worker" "$stage_dir" "$map" "$port" "$cold_log" || \
            ! cold_log_accepted "$cold_log" "$map"; then
        remove_owned_stage "$stage_dir"
        return 1
    fi
    remove_owned_stage "$stage_dir"
}

install_live_artifact() {
    local map="$1" artifact="$2" temporary
    if [ -e "$LIVE_MAPS/$map.rune" ] && [ "$artifact" -ef "$LIVE_MAPS/$map.rune" ]; then
        return 0
    fi
    temporary="$(mktemp "$LIVE_MAPS/.runegen-$map.$RUN_ID.XXXXXX")" || return 1
    rm -f -- "$temporary"
    if ! link_artifact "$artifact" "$temporary"; then
        rm -f -- "$temporary"
        echo "runegen: accepted and live paths must share a filesystem" >&2
        return 1
    fi
    mv -f -- "$temporary" "$LIVE_MAPS/$map.rune"
}

publish_artifact() {
    local worker="$1" map="$2" source="$3"
    local temporary target
    target="$ACCEPTED_DIR/$map.rune"
    temporary="$(mktemp "$ACCEPTED_DIR/.runegen-$map.$RUN_ID.w$worker.XXXXXX")" || return 1
    rm -f -- "$temporary"
    if ! link_artifact "$source" "$temporary" || \
            ! mv -f -- "$temporary" "$target" || \
            ! install_live_artifact "$map" "$target"; then
        rm -f -- "$temporary"
        return 1
    fi
}

record_result() {
    local worker="$1" map="$2" status="$3" detail="$4" worker_out="$5" line
    if ! printf -v line '%s\t%s\t%s\n' "$map" "$status" "$detail" || \
            ! append_output result-append "$worker_out/results.tsv" "$line"; then
        return 1
    fi
    printf 'rune: complete map=%s worker=%s result=%s %s\n' \
        "$map" "$worker" "$status" "$detail"
}

resume_if_valid() {
    local worker="$1" map="$2" worker_out="$3" port="$4"
    local artifact="$ACCEPTED_DIR/$map.rune" reader_log
    reader_log="$worker_out/$map.$RUN_ID.resume-reader.log"
    canonical_reader_accepts "$artifact" "$reader_log" && \
        cold_load_artifact "$worker" "$map" "$artifact" "$worker_out" "$port"
}

generate_one() {
    local worker="$1" map="$2" worker_out="$3" port="$4"
    local stage_dir stage_game generation_log reader_log cold_log
    stage_dir="$(stage_path_for "$worker" "$map")" || return 1
    stage_game="${stage_dir##*/}"
    generation_log="$worker_out/$map.$RUN_ID.generation.log"
    reader_log="$worker_out/$map.$RUN_ID.reader.log"
    cold_log="$worker_out/$map.$RUN_ID.cold.log"
    if ! prepare_stage "$stage_dir" "$map" generator || \
            ! run_engine generation "$worker" "$stage_dir" "$map" "$port" "$generation_log" || \
            ! generation_log_accepted "$generation_log" "$map" "$stage_game" || \
            ! canonical_reader_accepts "$stage_dir/maps/$map.rune" "$reader_log" || \
            ! install_stage_modules "$stage_dir" runtime || \
            ! run_engine cold "$worker" "$stage_dir" "$map" "$port" "$cold_log" || \
            ! cold_log_accepted "$cold_log" "$map" || \
            ! publish_artifact "$worker" "$map" "$stage_dir/maps/$map.rune"; then
        remove_owned_stage "$stage_dir"
        return 1
    fi
    remove_owned_stage "$stage_dir"
}

worker_run() {
    local worker="$1" phase="$2"
    shift 2
    local worker_out map index=0 port failed=0
    trap - EXIT HUP INT TERM
    worker_out="$WORKER_DIR/worker-$worker"
    mkdir -p "$worker_out" || return 1
    : > "$worker_out/results.tsv" || return 1
    for map in "$@"; do
        if [ $(( index % WORKERS )) -eq "$worker" ]; then
            port="${PORT_BY_MAP[$map]}"
            if resume_if_valid "$worker" "$map" "$worker_out" "$port"; then
                if install_live_artifact "$map" "$ACCEPTED_DIR/$map.rune"; then
                    if ! record_result "$worker" "$map" resumed \
                            "reader-and-cold-load-clean" "$worker_out"; then
                        failed=1
                    fi
                else
                    if ! record_result "$worker" "$map" failed \
                            "cannot-repair-live-publication" "$worker_out"; then
                        failed=1
                    fi
                    failed=1
                fi
            elif generate_one "$worker" "$map" "$worker_out" "$port"; then
                if ! record_result "$worker" "$map" accepted published "$worker_out"; then
                    failed=1
                fi
            else
                if ! record_result "$worker" "$map" failed \
                        "compact-gate-or-cold-load-rejected" "$worker_out"; then
                    failed=1
                fi
                failed=1
            fi
        fi
        index=$(( index + 1 ))
    done
    return "$failed"
}

run_phase() {
    local phase="$1"
    shift
    local worker pid failed=0
    CHILD_PIDS=()
    for (( worker = 0; worker < WORKERS; worker++ )); do
        worker_run "$worker" "$phase" "$@" &
        CHILD_PIDS+=("$!")
    done
    for pid in "${CHILD_PIDS[@]}"; do
        if ! wait "$pid"; then
            failed=1
        fi
    done
    CHILD_PIDS=()
    return "$failed"
}

generation_matches_inventory() {
    local generation="$1" map digest relative actual count=0
    [ -d "$generation" ] && [ ! -L "$generation" ] &&
        [ -f "$generation/inventory.tsv" ] &&
        [ ! -L "$generation/inventory.tsv" ] || return 1
    while IFS=$'\t' read -r map digest relative; do
        count=$(( count + 1 ))
        [ "${SELECTED[$map]+yes}" = yes ] || return 1
        [ "$relative" = "$map.rune" ] || return 1
        [[ "$digest" =~ ^[0-9a-f]{64}$ ]] || return 1
        [ -f "$generation/$relative" ] &&
            [ ! -L "$generation/$relative" ] || return 1
        actual="$(sha256sum -- "$generation/$relative")" || return 1
        actual="${actual%% *}"
        [ "$actual" = "$digest" ] || return 1
        canonical_reader_accepts "$generation/$relative" \
            "$CORPUS_ROOT/$map.$RUN_ID.reuse-reader.log" || return 1
    done < "$generation/inventory.tsv"
    [ "$count" -eq "$CORPUS_SIZE" ]
}

finalize_full_corpus() {
    local artifact base map temporary pending inventory digest corpus_id
    local final_dir relative manifest_rows inventory_rows published_new=0
    local -A seen=()
    local artifacts=()
    shopt -s nullglob dotglob
    artifacts=("$ACCEPTED_DIR"/*)
    if [ "${#artifacts[@]}" -ne "$CORPUS_SIZE" ]; then
        echo "runegen: cannot finalize: accepted artifact count is not $CORPUS_SIZE" >&2
        return 1
    fi
    for artifact in "${artifacts[@]}"; do
        base="${artifact##*/}"
        map="${base%.rune}"
        if [ "$base" = "$map" ] || [ "${SELECTED[$map]+yes}" != yes ] || \
                [ -L "$artifact" ] || [ ! -f "$artifact" ] || \
                [ "${seen[$map]+yes}" = yes ]; then
            echo "runegen: cannot finalize: unexpected accepted artifact $base" >&2
            return 1
        fi
        seen[$map]=1
    done
    for map in "${MAPS[@]}"; do
        if [ "${seen[$map]+yes}" != yes ]; then
            echo "runegen: cannot finalize: missing accepted artifact for $map" >&2
            return 1
        fi
    done
    pending="$(mktemp -d "$GENERATIONS_DIR/.pending-$RUN_ID.XXXXXX")" || return 1
    inventory="$pending/inventory.tsv"
    for map in "${MAPS[@]}"; do
        artifact="$ACCEPTED_DIR/$map.rune"
        if ! canonical_reader_accepts "$artifact" "$CORPUS_ROOT/$map.$RUN_ID.final-reader.log"; then
            rm -rf -- "$pending"
            echo "runegen: cannot finalize: canonical reader rejected $map" >&2
            return 1
        fi
        digest="$(sha256sum -- "$artifact")" || {
            rm -rf -- "$pending"
            return 1
        }
        digest="${digest%% *}"
        if [[ ! "$digest" =~ ^[0-9a-f]{64}$ ]] ||
                ! link_artifact "$artifact" "$pending/$map.rune"; then
            rm -rf -- "$pending"
            return 1
        fi
        if ! append_output inventory-append "$inventory" \
                "$map"$'\t'"$digest"$'\t'"$map.rune"$'\n'; then
            rm -rf -- "$pending"
            return 1
        fi
    done
    inventory_rows="$(wc -l < "$inventory")" || {
        rm -rf -- "$pending"
        return 1
    }
    if [ "$inventory_rows" -ne "$CORPUS_SIZE" ]; then
        rm -rf -- "$pending"
        return 1
    fi
    corpus_id="$(sha256sum -- "$inventory")" || {
        rm -rf -- "$pending"
        return 1
    }
    corpus_id="${corpus_id%% *}"
    if [[ ! "$corpus_id" =~ ^[0-9a-f]{64}$ ]]; then
        rm -rf -- "$pending"
        return 1
    fi
    final_dir="$GENERATIONS_DIR/$corpus_id"
    chmod a-w -- "$pending"/*.rune "$inventory" || {
        rm -rf -- "$pending"
        return 1
    }
    if [ -e "$final_dir" ]; then
        if [ ! -d "$final_dir" ] || [ -L "$final_dir" ] ||
                ! cmp -s -- "$inventory" "$final_dir/inventory.tsv" ||
                ! generation_matches_inventory "$final_dir"; then
            rm -rf -- "$pending"
            echo "runegen: damaged content-addressed corpus: $corpus_id" >&2
            return 1
        fi
        rm -rf -- "$pending"
    else
        chmod a-w -- "$pending" || {
            rm -rf -- "$pending"
            return 1
        }
        if ! mv -- "$pending" "$final_dir"; then
            chmod u+w -- "$pending" 2>/dev/null || true
            rm -rf -- "$pending"
            return 1
        fi
        published_new=1
    fi
    temporary="$(mktemp "$CORPUS_ROOT/.manifest.$RUN_ID.XXXXXX")" || {
        if [ "$published_new" -eq 1 ]; then
            discard_new_generation "$final_dir" || return 1
        fi
        return 1
    }
    manifest_rows=0
    while IFS=$'\t' read -r map digest relative; do
        if ! append_output manifest-append "$temporary" \
                "$map"$'\t'"$digest"$'\t'"$final_dir/$relative"$'\n'; then
            rm -f -- "$temporary"
            if [ "$published_new" -eq 1 ]; then
                discard_new_generation "$final_dir" || return 1
            fi
            return 1
        fi
        manifest_rows=$(( manifest_rows + 1 ))
    done < "$final_dir/inventory.tsv"
    if [ "$manifest_rows" -ne "$CORPUS_SIZE" ] ||
            ! rename_output manifest-rename "$temporary" "$MANIFEST_OUT"; then
        rm -f -- "$temporary"
        if [ "$published_new" -eq 1 ]; then
            discard_new_generation "$final_dir" || return 1
        fi
        return 1
    fi
    if ! printf 'rune: finalized maps=%s corpus=%s manifest=%s\n' \
            "$CORPUS_SIZE" "$corpus_id" "$MANIFEST_OUT"; then
        return 1
    fi
}

failed=0
if ! run_phase ordinary "${ORDINARY_MAPS[@]}"; then
    failed=1
fi
if ! run_phase hard-regressions "${HARD_MAPS[@]}"; then
    failed=1
fi
if [ "$FULL_MODE" -eq 1 ] && [ "$failed" -eq 0 ]; then
    if ! finalize_full_corpus; then
        failed=1
    fi
fi

if [ "$failed" -ne 0 ]; then
    echo "runegen: one or more maps failed" >&2
    exit 1
fi
exit 0
