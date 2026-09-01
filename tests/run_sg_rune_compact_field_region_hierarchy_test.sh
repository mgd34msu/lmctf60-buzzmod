#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_rune_compact_field_region_hierarchy_test.c slipgate/sg_rune_compact_field_region_hierarchy.c'

cd "$repo_dir"

gcc $strict -I. $sources -o "$tmp_dir/field-region-hierarchy-gcc"
"$tmp_dir/field-region-hierarchy-gcc"

clang $strict -I. $sources -o "$tmp_dir/field-region-hierarchy-clang"
"$tmp_dir/field-region-hierarchy-clang"

scan-build --status-bugs -o "$tmp_dir/scan-build" \
	clang $strict -I. $sources -o "$tmp_dir/field-region-hierarchy-static"

clang $strict -fno-omit-frame-pointer -fsanitize=address -I. \
	$sources -o "$tmp_dir/field-region-hierarchy-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	"$tmp_dir/field-region-hierarchy-asan"

clang $strict -fno-omit-frame-pointer -fsanitize=undefined -I. \
	$sources -o "$tmp_dir/field-region-hierarchy-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/field-region-hierarchy-ubsan"
