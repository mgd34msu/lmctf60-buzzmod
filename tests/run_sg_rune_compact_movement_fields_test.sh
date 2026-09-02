#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
allocation_test='-DSG_RUNE_COMPACT_MOVEMENT_FIELDS_TESTING -DSG_RUNE_COMPACT_MOVEMENT_FIELDS_TEST_WRAP_CALLOC -Wl,--wrap=calloc'
sources='tests/sg_rune_compact_movement_fields_test.c slipgate/sg_rune_compact_movement_fields.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_eval.c slipgate/sg_host_hook_law.c slipgate/sg_host_mechanism_law.c'

cd "$repo_dir"


gcc $strict $allocation_test -I. $sources -lm \
	-o "$tmp_dir/rune-compact-movement-fields-gcc"
"$tmp_dir/rune-compact-movement-fields-gcc"

clang $strict $allocation_test -I. $sources -lm \
	-o "$tmp_dir/rune-compact-movement-fields-clang"
"$tmp_dir/rune-compact-movement-fields-clang"

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=address \
	-I. $sources -lm -o "$tmp_dir/rune-compact-movement-fields-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	"$tmp_dir/rune-compact-movement-fields-asan"

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=undefined \
	-I. $sources -lm -o "$tmp_dir/rune-compact-movement-fields-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/rune-compact-movement-fields-ubsan"

