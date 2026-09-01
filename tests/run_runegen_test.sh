#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNNER="$ROOT/tools/runegen.sh"
TMP="$(mktemp -d /tmp/runegen-shell-test.XXXXXX)"

cleanup() {
    find "$TMP" -type d -exec chmod u+w {} + 2>/dev/null || true
    rm -rf -- "$TMP"
}
trap cleanup EXIT

fail() {
    echo "runegen shell test: $*" >&2
    exit 1
}

ENGINE="$TMP/q2ded"
READER="$TMP/runecompactread"

cat > "$ENGINE" <<'ENGINE'
#!/usr/bin/env bash
set -euo pipefail

commands="$(cat)"
stage=""
map=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        +set)
            if [ "${2:-}" = game ]; then
                stage="${3:-}"
            fi
            shift 3
            ;;
        +map)
            map="${2:-}"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

[ -n "$stage" ] && [ -n "$map" ]
stage_dir="$GAMEDIR_ROOT/$stage"
if printf '%s\n' "$commands" | grep -qx 'sv rune'; then
    [ "$(cat "$stage_dir/game.so")" = generator ]
    printf 'generation\t%s\t%s\n' "$map" "$stage" >> "$ENGINE_TRACE"
    case "$map" in
        reject)
            printf 'bad\n' > "$stage_dir/maps/$map.rune"
            ;;
        *)
            printf 'good\n' > "$stage_dir/maps/$map.rune"
            ;;
    esac
    bytes="$(wc -c < "$stage_dir/maps/$map.rune")"
    if [ "$map" = partial ]; then
        printf 'rune: compact generation published map=%s path=%s/maps/%s.rune bytes=%s durable=1' \
            "$map" "$stage" "$map" "$bytes"
    else
        printf 'rune: compact generation published map=%s path=%s/maps/%s.rune bytes=%s durable=1\n' \
            "$map" "$stage" "$map" "$bytes"
    fi
else
    [ "$(cat "$stage_dir/game.so")" = runtime ]
    [ -f "$stage_dir/maps/$map.rune" ]
    printf 'cold\t%s\t%s\n' "$map" "$stage" >> "$ENGINE_TRACE"
    printf 'slipgate: compact rune ready %s\n' "$map"
fi
ENGINE
chmod +x "$ENGINE"

cat > "$READER" <<'READER'
#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 1 ]
grep -qx good "$1"
READER
chmod +x "$READER"

setup_case() {
    local case_root="$1"
    shift
    mkdir -p "$case_root/quake/live/maps"
    printf 'set maxclients 16\n' > "$case_root/quake/live/rune.cfg"
    printf 'runtime\n' > "$case_root/quake/live/game.so"
    printf 'runtime\n' > "$case_root/quake/live/gamex86_64.so"
    printf 'generator\n' > "$case_root/generator.so"
    for map in "$@"; do
        printf 'bsp\n' > "$case_root/quake/live/maps/$map.bsp"
    done
}

run_runner() {
    local case_root="$1" trace="$2"
    shift 2
    env \
        Q2DED="$ENGINE" \
        GAMEDIR_ROOT="$case_root/quake" \
        GAME=live \
        CFG=rune.cfg \
        MAXCLIENTS=16 \
        PORT_START=58400 \
        STARTUP_SLEEP=0 \
        RUNE_GENERATOR_MODULE="$case_root/generator.so" \
        RUNE_COMPACT_READER="$READER" \
        RUNE_CORPUS_ROOT="$case_root/corpus" \
        RUNEGEN_TEST_IO_FAULT="${RUNEGEN_TEST_IO_FAULT:-}" \
        ENGINE_TRACE="$trace" \
        "$RUNNER" "$@"
}

assert_finalization_fault() {
    local fault="$1" case_root="$2" trace="$3"
    shift 3
    if RUNEGEN_TEST_IO_FAULT="$fault" run_runner "$case_root" "$trace" \
            > "$case_root/out" 2>&1; then
        fail "finalization fault was accepted: $fault"
    fi
    if grep -q '^rune: finalized ' "$case_root/out"; then
        fail "failed finalization reported completion: $fault"
    fi
    [ ! -e "$case_root/corpus/manifest.tsv" ] || \
        fail "failed finalization published a manifest: $fault"
    if find "$case_root/corpus/generations" -mindepth 1 -maxdepth 1 \
            ! -name '.pending-*' -print -quit | grep -q .; then
        fail "failed finalization published a generation: $fault"
    fi
    if find "$case_root/corpus/generations" -mindepth 1 -maxdepth 1 \
            -name '.pending-*' -print -quit | grep -q .; then
        fail "failed finalization retained a pending generation: $fault"
    fi
    if find "$case_root/corpus" -maxdepth 1 -name '.manifest.*' \
            -print -quit | grep -q .; then
        fail "failed finalization retained a temporary manifest: $fault"
    fi
}

ordering_root="$TMP/ordering"
ordering_trace="$ordering_root/trace.tsv"
setup_case "$ordering_root" ordinary bmap5
run_runner "$ordering_root" "$ordering_trace" bmap5 ordinary > "$ordering_root/first.out"

first_generated="$(grep '^generation' "$ordering_trace" | sed -n '1p')"
second_generated="$(grep '^generation' "$ordering_trace" | sed -n '2p')"
[[ "$first_generated" == $'generation\tordinary\t'* ]] || fail "ordinary map did not run first"
[[ "$second_generated" == $'generation\tbmap5\t'* ]] || fail "hard regression ran before ordinary map"
for worker in $(seq 0 11); do
    [ -d "$ordering_root/corpus/workers/worker-$worker" ] || \
        fail "worker $worker did not get an isolated output directory"
done
[ -f "$ordering_root/corpus/accepted/ordinary.rune" ] || fail "ordinary artifact missing"
[ "$(stat -c %i "$ordering_root/corpus/accepted/ordinary.rune")" = \
    "$(stat -c %i "$ordering_root/quake/live/maps/ordinary.rune")" ] || \
    fail "publication did not retain the accepted artifact"

generation_count="$(grep -c '^generation' "$ordering_trace")"
run_runner "$ordering_root" "$ordering_trace" bmap5 ordinary > "$ordering_root/resume.out"
[ "$(grep -c '^generation' "$ordering_trace")" -eq "$generation_count" ] || \
    fail "resume regenerated an accepted artifact"
[ "$(grep -c 'result=resumed' "$ordering_root/resume.out")" -eq 2 ] || \
    fail "resume did not require reader and fresh cold load"

reject_root="$TMP/reject"
reject_trace="$reject_root/trace.tsv"
setup_case "$reject_root" reject
if run_runner "$reject_root" "$reject_trace" reject > "$reject_root/out" 2>&1; then
    fail "canonical reader rejection was accepted"
fi
[ ! -e "$reject_root/corpus/accepted/reject.rune" ] || \
    fail "rejected artifact was published"

partial_root="$TMP/partial"
partial_trace="$partial_root/trace.tsv"
setup_case "$partial_root" partial
if run_runner "$partial_root" "$partial_trace" partial > "$partial_root/out" 2>&1; then
    fail "unterminated publication record was accepted"
fi
[ ! -e "$partial_root/corpus/accepted/partial.rune" ] || \
    fail "partial publication was published"

full_root="$TMP/full"
mapfile -t full_maps < "$ROOT/tools/rune-corpus-maps.txt"
setup_case "$full_root" "${full_maps[@]}"
run_runner "$full_root" "$full_root/trace.tsv" > "$full_root/out"
[ "$(wc -l < "$full_root/corpus/manifest.tsv")" -eq 175 ] || \
    fail "full manifest did not contain exactly 175 accepted artifacts"
[ "$(find "$full_root/corpus/accepted" -maxdepth 1 -type f | wc -l)" -eq 175 ] || \
    fail "accepted directory did not contain exactly 175 artifacts"
grep -q '^rune: finalized maps=175 ' "$full_root/out" || \
    fail "full corpus did not finalize"
corpus_id="$(sed -n 's/.* corpus=\([0-9a-f]\{64\}\) .*/\1/p' "$full_root/out")"
[ -n "$corpus_id" ] || fail "finalization did not publish a corpus identity"
generation="$full_root/corpus/generations/$corpus_id"
[ -d "$generation" ] && [ ! -L "$generation" ] || \
    fail "content-addressed generation directory missing"
[ ! -w "$generation" ] || \
    fail "content-addressed generation directory remained writable"
[ "$(find "$generation" -maxdepth 1 -name '*.rune' -type f | wc -l)" -eq 175 ] || \
    fail "content-addressed generation did not contain 175 artifacts"
first_manifest_artifact="$(awk -F '\t' 'NR == 1 { print $3 }' \
    "$full_root/corpus/manifest.tsv")"
case "$first_manifest_artifact" in
    "$generation"/*.rune) ;;
    *) fail "manifest did not address the immutable generation" ;;
esac
old_inode="$(stat -c %i "$first_manifest_artifact")"
first_map="${full_maps[0]}"
mkdir -p "$full_root/corpus/generations/.pending-orphan"
printf 'orphan\n' > "$full_root/corpus/generations/.pending-orphan/file"
printf 'orphan\n' > "$full_root/corpus/accepted/.runegen-orphan"
run_runner "$full_root" "$full_root/trace.tsv" "$first_map" \
    > "$full_root/after-finalize.out"
[ ! -e "$full_root/corpus/generations/.pending-orphan" ] || \
    fail "pending generation orphan survived recovery"
[ ! -e "$full_root/corpus/accepted/.runegen-orphan" ] || \
    fail "accepted publication orphan survived recovery"
[ "$(stat -c %i "$first_manifest_artifact")" = "$old_inode" ] || \
    fail "later publication mutated the finalized corpus"
chmod u+w "$generation"
rm -f -- "$first_manifest_artifact"
printf 'damaged\n' > "$first_manifest_artifact"
chmod a-w "$first_manifest_artifact" "$generation"
if run_runner "$full_root" "$full_root/trace.tsv" \
        > "$full_root/damaged-reuse.out" 2>&1; then
    fail "damaged content-addressed generation was reused"
fi

for fault in inventory-append.partial inventory-append.fail \
        manifest-append.partial manifest-append.fail manifest-rename.fail; do
    fault_root="$TMP/finalization-${fault//./-}"
    setup_case "$fault_root" "${full_maps[@]}"
    assert_finalization_fault "$fault" "$fault_root" "$fault_root/trace.tsv"
done

lock_root="$TMP/locked"
setup_case "$lock_root" locked
mkdir -p "$lock_root/corpus"
exec 9> "$lock_root/corpus/.run.lockfile"
flock -n 9
if run_runner "$lock_root" "$lock_root/trace.tsv" locked \
        > "$lock_root/out" 2>&1; then
    fail "concurrent corpus owner bypassed the advisory lock"
fi
flock -u 9
exec 9>&-

echo "runegen shell test: ok"
