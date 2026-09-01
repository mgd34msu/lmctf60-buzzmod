#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
allocation_test='-DSG_RUNE_COMPACT_LEARNING_CONSUMER_TEST_WRAP_ALLOC -Wl,--wrap=calloc -Wl,--wrap=realloc'
sources='tests/sg_rune_compact_learning_consumer_test.c tests/sg_rune_compact_learning_consumer_fixture.c slipgate/sg_rune_compact_learning_game.c slipgate/sg_rune_compact_learning_consumer.c slipgate/sg_rune_compact_learning.c slipgate/sg_rune_compact_production.c slipgate/sg_rune_compact_model.c slipgate/sg_rune_compact_source_surface_catalog.c slipgate/sg_rune_compact_weapon_catalog.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_static.c slipgate/sg_rune_model.c'

cd "$repo_dir"

gcc $strict $allocation_test -I. $sources -lm -o "$tmp_dir/rune-compact-learning-consumer-gcc"
"$tmp_dir/rune-compact-learning-consumer-gcc"

clang $strict $allocation_test -I. $sources -lm -o "$tmp_dir/rune-compact-learning-consumer-clang"
"$tmp_dir/rune-compact-learning-consumer-clang"

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=address -I. \
	$sources -lm -o "$tmp_dir/rune-compact-learning-consumer-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	"$tmp_dir/rune-compact-learning-consumer-asan"

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=undefined -I. \
	$sources -lm -o "$tmp_dir/rune-compact-learning-consumer-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/rune-compact-learning-consumer-ubsan"

if : | gcc -mlong-double-64 -x c - -fsyntax-only >/dev/null 2>&1; then
	gcc $strict $allocation_test -mlong-double-64 -fno-omit-frame-pointer \
		-fsanitize=undefined,float-cast-overflow -I. $sources -lm \
		-o "$tmp_dir/rune-compact-learning-consumer-long-double-64"
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$tmp_dir/rune-compact-learning-consumer-long-double-64"
else
	echo "gcc does not support -mlong-double-64; long-double check skipped" >&2
fi
