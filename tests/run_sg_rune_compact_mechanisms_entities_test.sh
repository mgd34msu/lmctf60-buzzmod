#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
defines='-DSG_RUNE_COMPACT_MECHANISMS_ENTITIES_TESTING'
sources='tests/sg_rune_compact_mechanisms_entities_test.c slipgate/sg_rune_compact_mechanisms_entities.c'

cd "$repo_dir"
for compiler in gcc clang
do
	$compiler $strict $defines -I. $sources -o "$tmp_dir/entities-$compiler"
	"$tmp_dir/entities-$compiler"
done

clang $strict $defines -fno-omit-frame-pointer -fsanitize=address -I. \
	$sources -o "$tmp_dir/entities-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$tmp_dir/entities-asan"

clang $strict $defines -fno-omit-frame-pointer -fsanitize=undefined -I. \
	$sources -o "$tmp_dir/entities-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$tmp_dir/entities-ubsan"

scan-build --status-bugs -o "$tmp_dir/scan-build" clang $strict $defines \
	-I. $sources -o "$tmp_dir/entities-scan-build"
