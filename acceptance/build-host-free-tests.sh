#!/bin/sh
# Strict reproducible host-free acceptance matrix.  Every binary is built in
# disposable scratch; the repository remains source-and-fixture only.
set -eu

test_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_rune=${1:-tests/fixtures/lmctf14.rune}
test_output=$(mktemp -d "${TMPDIR:-/var/tmp}/accept-drop-host-tests.XXXXXX")
trap 'rm -rf -- "$test_output"' EXIT HUP INT TERM
test_include="$test_output/include"
mkdir -p "$test_include"
sed -e 's/\$//g' \
    -e 's/WCLOGCOUNT+2/0/g' \
    -e 's/WCREV=7/accept-drop-host-test/g' \
    -e 's/WCNOW=%Y/2026/g' \
    GitRevisionInfo.tmpl > "$test_include/GitRevisionInfo.h"
# Optional compiler/link flags keep the sanitizer matrix on this exact
# declared-stub dependency graph rather than constructing a bare production
# adapter link by hand.
test_extra_flags=${SG_ACCEPT_DROP_TEST_EXTRA_FLAGS:-}

cd "$test_root"
set -- \
    "$test_output/sg_accept_drop_event_schema_test.a.clang" \
    "$test_output/sg_accept_drop_event_schema_test.a.gcc" \
    "$test_output/sg_accept_drop_event_schema_test.b.clang" \
    "$test_output/sg_accept_drop_event_schema_test.b.gcc" \
    "$test_output/sg_accept_drop_finish_test.clang" \
    "$test_output/sg_accept_drop_finish_test.gcc" \
    "$test_output/sg_accept_drop_fixture_test.a.clang" \
    "$test_output/sg_accept_drop_fixture_test.a.gcc" \
    "$test_output/sg_accept_drop_fixture_test.clang" \
    "$test_output/sg_accept_drop_fixture_test.gcc" \
    "$test_output/sg_accept_drop_injection_test.a.clang" \
    "$test_output/sg_accept_drop_injection_test.a.gcc" \
    "$test_output/sg_accept_drop_injection_test.clang" \
    "$test_output/sg_accept_drop_injection_test.gcc" \
    "$test_output/sg_accept_drop_observer_events_test.a.clang" \
    "$test_output/sg_accept_drop_observer_events_test.a.gcc" \
    "$test_output/sg_accept_drop_selector_test.clang" \
    "$test_output/sg_accept_drop_selector_test.gcc" \
    "$test_output/sg_accept_drop_summary_test.a.clang" \
    "$test_output/sg_accept_drop_summary_test.a.gcc" \
    "$test_output/sg_accept_drop_summary_test.b.clang" \
    "$test_output/sg_accept_drop_summary_test.b.gcc"
test "$#" = 22
freshness_marker="$test_output/.started"
touch "$freshness_marker"
for test_binary in "$@"; do test ! -e "$test_binary"; done

printf '%s  %s\n' \
    6f8e93e85f580701cd3e72cb1ae62011c99a9263bef4fbefcf3434271e544fa6 \
    "$test_rune" | sha256sum -c -

# The short-landing checkpoint observes the ordinary navigation handoff. Bind
# its frozen cadence to the declared sg_subframes default instead of borrowing
# the proved DROP four-by-25 ms law.
test "$(sed -n 's/^[[:space:]]*X(subframes, "sg_subframes", "\([^"]*\)").*/\1/p' \
    slipgate/sg_cvars.h)" = "8"
sh acceptance/sg_accept_drop_authority_seam_test.sh slipgate/sg_move.c

for test_cc in gcc clang
do
	test_flags="-std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -I$test_include -I."
	test_gc="-Wl,--gc-sections -lm"

	$test_cc $test_flags $test_extra_flags acceptance/sg_accept_drop_finish_test.c \
	    slipgate/sg_replay.c $test_gc $test_extra_flags \
	    -o "$test_output/sg_accept_drop_finish_test.$test_cc"
	$test_cc $test_flags $test_extra_flags acceptance/sg_accept_drop_injection_test.c \
	    slipgate/sg_drop_live.c slipgate/sg_replay.c $test_gc $test_extra_flags \
	    -o "$test_output/sg_accept_drop_injection_test.$test_cc"
	$test_cc $test_flags $test_extra_flags -DSG_ACCEPT_DROP_LEGACY_A=1 \
	    acceptance/sg_accept_drop_injection_test.c slipgate/sg_drop_live.c \
	    slipgate/sg_replay.c \
	    $test_gc $test_extra_flags -o "$test_output/sg_accept_drop_injection_test.a.$test_cc"
	$test_cc $test_flags $test_extra_flags acceptance/sg_accept_drop_fixture_test.c \
	    slipgate/sg_replay.c $test_gc $test_extra_flags \
	    -o "$test_output/sg_accept_drop_fixture_test.$test_cc"
	$test_cc $test_flags $test_extra_flags -DSG_ACCEPT_DROP_LEGACY_A=1 \
	    acceptance/sg_accept_drop_fixture_test.c slipgate/sg_replay.c $test_gc $test_extra_flags \
	    -o "$test_output/sg_accept_drop_fixture_test.a.$test_cc"
	$test_cc $test_flags $test_extra_flags -DSG_ACCEPT_DROP_LEGACY_A=1 \
	    acceptance/sg_accept_drop_observer_events_test.c \
	    slipgate/sg_drop_live.c slipgate/sg_replay.c $test_gc $test_extra_flags \
	    -o "$test_output/sg_accept_drop_observer_events_test.a.$test_cc"
	$test_cc $test_flags $test_extra_flags acceptance/sg_accept_drop_selector_test.c \
	    slipgate/sg_rune_wire.c slipgate/sg_crc32.c slipgate/sg_action.c \
	    slipgate/sg_replay.c $test_gc $test_extra_flags \
	    -o "$test_output/sg_accept_drop_selector_test.$test_cc"
	$test_cc $test_flags $test_extra_flags -DSG_ACCEPT_DROP_LEGACY_A=1 \
	    acceptance/sg_accept_drop_summary_test.c slipgate/sg_replay.c $test_gc $test_extra_flags \
	    -o "$test_output/sg_accept_drop_summary_test.a.$test_cc"
	$test_cc $test_flags $test_extra_flags -DSG_ACCEPT_DROP_LEGACY_A=0 \
	    acceptance/sg_accept_drop_summary_test.c slipgate/sg_replay.c $test_gc $test_extra_flags \
	    -o "$test_output/sg_accept_drop_summary_test.b.$test_cc"
	$test_cc $test_flags $test_extra_flags -DSG_ACCEPT_DROP_LEGACY_A=1 \
	    acceptance/sg_accept_drop_event_schema_test.c slipgate/sg_drop_live.c \
	    slipgate/sg_replay.c \
	    $test_gc $test_extra_flags -o "$test_output/sg_accept_drop_event_schema_test.a.$test_cc"
	$test_cc $test_flags $test_extra_flags -DSG_ACCEPT_DROP_LEGACY_A=0 \
	    acceptance/sg_accept_drop_event_schema_test.c slipgate/sg_drop_live.c \
	    slipgate/sg_replay.c \
	    $test_gc $test_extra_flags -o "$test_output/sg_accept_drop_event_schema_test.b.$test_cc"

	"$test_output/sg_accept_drop_finish_test.$test_cc"
	"$test_output/sg_accept_drop_injection_test.$test_cc"
	"$test_output/sg_accept_drop_injection_test.a.$test_cc"
	"$test_output/sg_accept_drop_fixture_test.$test_cc"
	"$test_output/sg_accept_drop_fixture_test.a.$test_cc"
	"$test_output/sg_accept_drop_observer_events_test.a.$test_cc"
	"$test_output/sg_accept_drop_selector_test.$test_cc" "$test_rune"
	"$test_output/sg_accept_drop_summary_test.a.$test_cc"
	"$test_output/sg_accept_drop_summary_test.b.$test_cc"
	"$test_output/sg_accept_drop_event_schema_test.a.$test_cc" \
	    slipgate/sg_accept_drop.c slipgate/sg_descend.c
	"$test_output/sg_accept_drop_event_schema_test.b.$test_cc" \
	    slipgate/sg_accept_drop.c slipgate/sg_descend.c
done

for test_binary in "$@"
do
	test -f "$test_binary" && test -x "$test_binary" &&
	    test "$test_binary" -nt "$freshness_marker"
done
printf 'host-binary-freshness ok exact=22 gcc=11 clang=11\n'
