#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wcast-align'
sources='tests/sg_compact_runtime_level_test.c slipgate/sg_compact_runtime_level.c'

cd "$repo_dir"

gcc $strict -I. $sources -lm -o "$tmp_dir/compact-runtime-level-gcc"
"$tmp_dir/compact-runtime-level-gcc"

clang $strict -I. $sources -lm -o "$tmp_dir/compact-runtime-level-clang"
"$tmp_dir/compact-runtime-level-clang"

scan-build --status-bugs -o "$tmp_dir/scan-build" \
	clang $strict -I. $sources -lm -o "$tmp_dir/compact-runtime-level-static"

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/compact-runtime-level-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/compact-runtime-level-sanitize"
