#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
allocation_test='-DSG_RUNE_COMPACT_LEARNING_TEST_WRAP_ALLOC -Wl,--wrap=calloc -Wl,--wrap=realloc'
sources='tests/sg_rune_compact_learning_test.c slipgate/sg_rune_compact_learning.c slipgate/sg_rune_compact_model.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_static.c'

cd "$repo_dir"

gcc $strict $allocation_test -I. $sources -lm -o "$tmp_dir/rune-compact-learning-gcc"
"$tmp_dir/rune-compact-learning-gcc"

clang $strict $allocation_test -I. $sources -lm -o "$tmp_dir/rune-compact-learning-clang"
"$tmp_dir/rune-compact-learning-clang"

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=address -I. \
	$sources -lm -o "$tmp_dir/rune-compact-learning-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	"$tmp_dir/rune-compact-learning-asan"

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=undefined -I. \
	$sources -lm -o "$tmp_dir/rune-compact-learning-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/rune-compact-learning-ubsan"

if gcc $strict $allocation_test -mlong-double-64 -fno-omit-frame-pointer \
	-fsanitize=undefined,float-cast-overflow -I. $sources -lm \
	-o "$tmp_dir/rune-compact-learning-long-double-64" 2>/dev/null; then
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$tmp_dir/rune-compact-learning-long-double-64"
fi
