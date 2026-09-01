#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
allocation_test='-DSG_RUNE_COMPACT_FIELD_TEST_WRAP_CALLOC -Wl,--wrap=calloc'
sources='tests/sg_rune_compact_field_test.c slipgate/sg_rune_compact_field.c slipgate/sg_rune_compact_field_region_hierarchy.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_eval.c'

cd "$repo_dir"

if rg -n 'source_rank|target_rank|uint32_t movement_field|movement_fields\[|movement_field_attachment|pmove_domain|hook_lifecycle|transition_attachment|action_link|Dijkstra|route.only' \
		slipgate/sg_rune_compact_field.c slipgate/sg_rune_compact_field.h; then
	echo 'compact field result still exposes rank or movement provenance' >&2
	exit 1
fi

gcc $strict $allocation_test -I. $sources -lm -o "$tmp_dir/rune-compact-field-gcc"
"$tmp_dir/rune-compact-field-gcc"

clang $strict $allocation_test -I. $sources -lm -o "$tmp_dir/rune-compact-field-clang"
"$tmp_dir/rune-compact-field-clang"

scan-build --status-bugs -o "$tmp_dir/scan-build" \
	clang $strict $allocation_test -I. $sources -lm \
	-o "$tmp_dir/rune-compact-field-static"

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=address -I. \
	$sources -lm -o "$tmp_dir/rune-compact-field-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	"$tmp_dir/rune-compact-field-asan"

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=undefined -I. \
	$sources -lm -o "$tmp_dir/rune-compact-field-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/rune-compact-field-ubsan"
