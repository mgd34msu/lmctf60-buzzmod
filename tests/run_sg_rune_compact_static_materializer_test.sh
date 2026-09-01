#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_rune_compact_static_materializer_test.c slipgate/sg_rune_compact_static_materializer.c slipgate/sg_rune_compact_static.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -DSG_RUNE_COMPACT_STATIC_MATERIALIZER_TESTING -I. $sources \
		-lm -o "$tmp_dir/materializer-$cc"
	"$tmp_dir/materializer-$cc"
	$cc $strict -O3 -ffp-contract=fast \
		-DSG_RUNE_COMPACT_STATIC_MATERIALIZER_TESTING -I. $sources \
		-lm -o "$tmp_dir/materializer-$cc-fast"
	"$tmp_dir/materializer-$cc-fast"
done

clang $strict -DSG_RUNE_COMPACT_STATIC_MATERIALIZER_TESTING \
	-fno-omit-frame-pointer -fsanitize=address -I. $sources -lm \
	-o "$tmp_dir/materializer-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$tmp_dir/materializer-asan"

clang $strict -DSG_RUNE_COMPACT_STATIC_MATERIALIZER_TESTING \
	-fno-omit-frame-pointer -fsanitize=undefined -I. $sources -lm \
	-o "$tmp_dir/materializer-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$tmp_dir/materializer-ubsan"
