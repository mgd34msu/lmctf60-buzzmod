#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
defines='-DSG_RUNE_COMPACT_MECHANISM_TRANSITIONS_TESTING'
sources='tests/sg_rune_compact_mechanisms_transitions_test.c slipgate/sg_rune_compact_mechanisms_transitions.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict $defines -I. $sources -lm -o "$tmp_dir/transitions-$cc"
	"$tmp_dir/transitions-$cc"
done

clang $strict $defines -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources -lm -o "$tmp_dir/transitions-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/transitions-sanitize"
