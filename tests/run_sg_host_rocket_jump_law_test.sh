#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
sources='tests/sg_host_rocket_jump_law_test.c'

cd "$repo_dir"

for cc in gcc clang
do
	$cc $strict -I. $sources -lm -o "$tmp_dir/rocket-jump-law-$cc"
	"$tmp_dir/rocket-jump-law-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=undefined -I. $sources -lm \
	-o "$tmp_dir/rocket-jump-law-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$tmp_dir/rocket-jump-law-ubsan"
