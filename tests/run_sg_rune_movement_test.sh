#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM
strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
sources='tests/sg_rune_movement_test.c slipgate/sg_rune_movement.c slipgate/sg_rune_analytic.c slipgate/sg_rune_law.c slipgate/sg_rune_crc.c'
cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. $sources -lm -o "$tmp_dir/movement-$cc"
	"$tmp_dir/movement-$cc"
done
clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. $sources -lm -o "$tmp_dir/movement-sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$tmp_dir/movement-sanitized"
