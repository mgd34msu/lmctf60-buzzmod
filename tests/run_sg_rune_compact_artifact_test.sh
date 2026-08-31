#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
allocation_test='-DSG_RUNE_COMPACT_ARTIFACT_TEST_WRAP_CALLOC -Wl,--wrap=calloc'
sources='tests/sg_rune_compact_artifact_test.c slipgate/sg_rune_compact_artifact.c slipgate/sg_rune_compact_wire.c slipgate/sg_rune_compact_model.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_static.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict $allocation_test -I. $sources -o "$tmp_dir/rune-compact-artifact-$cc"
	"$tmp_dir/rune-compact-artifact-$cc"
done

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -o "$tmp_dir/rune-compact-artifact-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/rune-compact-artifact-sanitize"
