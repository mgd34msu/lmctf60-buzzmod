#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion'
strict="$strict -Wsign-conversion -Wshadow -Wstrict-prototypes"
strict="$strict -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align"
sources='tests/sg_cell_phase_localization_test.c'
sources="$sources slipgate/sg_cell_phase_localization.c"
sources="$sources slipgate/sg_host_collision.c slipgate/sg_host_pmove.c"
sources="$sources slipgate/sg_rune_model.c"

cd "$repo_dir"

raw_owner_symbols='sg_host_pmove_function_t|SG_HostPmoveReplayFrame'
raw_owner_symbols="$raw_owner_symbols|SG_HostCollision(Trace|PointContents"
raw_owner_symbols="$raw_owner_symbols|ClassifyPose|Transition)"
if grep -Eq "$raw_owner_symbols" \
		slipgate/sg_cell_phase_localization.c \
		slipgate/sg_cell_phase_localization.h; then
	echo 'raw caller-selected physics/collision authority in localization' >&2
	exit 1
fi
if grep -Eq 'sg_phase_catalog|SG_PhaseCatalog' \
		slipgate/sg_cell_phase_localization.c \
		slipgate/sg_cell_phase_localization.h; then
	echo 'offline phase construction leaked into runtime localization' >&2
	exit 1
fi
for cc in gcc clang
do
	$cc $strict -I. $sources -lm -o "$tmp_dir/localization-$cc"
	"$tmp_dir/localization-$cc"
done

build_real_localization()
{
	cc=$1
	suffix=$2
	extra=$3

	$cc $strict $extra -I. -DSG_LOCALIZATION_REAL_PMOVE_TEST -DDEDICATED_ONLY \
		-c tests/sg_cell_phase_localization_test.c -o "$tmp_dir/test-$suffix.o"
	$cc $strict $extra -I. -c slipgate/sg_cell_phase_localization.c \
		-o "$tmp_dir/localization-$suffix.o"
	$cc $strict $extra -I. -c slipgate/sg_host_collision.c \
		-o "$tmp_dir/collision-$suffix.o"
	$cc $strict $extra -I. -c slipgate/sg_host_pmove.c \
		-o "$tmp_dir/pmove-$suffix.o"
	$cc $strict $extra -I. -c slipgate/sg_rune_model.c \
		-o "$tmp_dir/model-$suffix.o"
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Wno-strict-prototypes \
		$extra -I. -DDEDICATED_ONLY -c tests/support/yq2_pmove.c \
		-o "$tmp_dir/yq2-$suffix.o"
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Wno-strict-prototypes \
		$extra -I. -c q_shared.c -o "$tmp_dir/shared-$suffix.o"
	$cc $extra "$tmp_dir/test-$suffix.o" "$tmp_dir/localization-$suffix.o" \
		"$tmp_dir/collision-$suffix.o" "$tmp_dir/pmove-$suffix.o" \
		"$tmp_dir/model-$suffix.o" "$tmp_dir/yq2-$suffix.o" \
		"$tmp_dir/shared-$suffix.o" -lm -o "$tmp_dir/real-$suffix"
}

for cc in gcc clang
do
	build_real_localization "$cc" "$cc" ''
	if nm -u "$tmp_dir/localization-$cc.o" | grep -Eq \
			'SG_PhaseCatalog|SG_HostPmoveReplayFrame|SG_HostCollision'; then
		echo 'forbidden offline or raw-authority symbol in localization object' >&2
		exit 1
	fi
	"$tmp_dir/real-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/localization-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/localization-sanitize"

sanitize='-fno-omit-frame-pointer -fsanitize=address,undefined'
build_real_localization clang sanitize "$sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/real-sanitize"

clang --analyze -std=c11 -Wall -Wextra -Wpedantic -Werror -I. \
	-Xanalyzer -analyzer-output=text \
	slipgate/sg_cell_phase_localization.c
