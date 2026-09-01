#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_rune_compact_model_test.c slipgate/sg_rune_compact_model.c slipgate/sg_rune_compact_source_surface_catalog.c slipgate/sg_rune_compact_weapon_catalog.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_static.c slipgate/sg_rune_model.c'
allocation_test='-DSG_RUNE_COMPACT_MODEL_TEST_WRAP_CALLOC -Wl,--wrap=calloc'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict $allocation_test -I. $sources -lm -o "$tmp_dir/rune-compact-model-$cc"
	"$tmp_dir/rune-compact-model-$cc"
done

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/rune-compact-model-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/rune-compact-model-sanitize"
