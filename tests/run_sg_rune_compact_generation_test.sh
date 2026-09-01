#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_rune_compact_generation_test.c slipgate/sg_rune_compact_generation.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. -Islipgate -fsyntax-only \
		slipgate/sg_rune_compact_generation.c
	$cc $strict -I. -Islipgate $sources \
		-o "$tmp_dir/generation-$cc"
	"$tmp_dir/generation-$cc"
done

clang $strict -fno-omit-frame-pointer \
	-fsanitize=address -I. -Islipgate $sources \
	-o "$tmp_dir/generation-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$tmp_dir/generation-asan"

clang $strict -fno-omit-frame-pointer \
	-fsanitize=undefined -I. -Islipgate $sources \
	-o "$tmp_dir/generation-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$tmp_dir/generation-ubsan"
