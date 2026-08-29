#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion'
strict="$strict -Wsign-conversion -Wshadow -Wstrict-prototypes"
strict="$strict -Wmissing-prototypes -Wcast-align"
sources='tests/sg_destination_test.c slipgate/sg_destination.c'
sources="$sources slipgate/sg_rune_model.c"

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. $sources -lm -o "$tmp_dir/destination-$cc"
	"$tmp_dir/destination-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/destination-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/destination-sanitize"

clang $strict --analyze -Xanalyzer -analyzer-output=text -I. \
	slipgate/sg_destination.c slipgate/sg_rune_model.c
