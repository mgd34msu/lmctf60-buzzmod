#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wcast-align'
sources='tests/sg_destination_field_cache_test.c slipgate/sg_destination_field_cache.c slipgate/sg_destination_field.c slipgate/sg_rune_model.c'

cd "$repo_dir"

gcc $strict -I. $sources -lm -o "$tmp_dir/cache-gcc"
"$tmp_dir/cache-gcc"

clang $strict -I. $sources -lm -o "$tmp_dir/cache-clang"
"$tmp_dir/cache-clang"

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/cache-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/cache-sanitize"
