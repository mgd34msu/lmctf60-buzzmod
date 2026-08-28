#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wcast-align'
sources='tests/sg_strategy_test.c slipgate/sg_strategy.c'

cd "$repo_dir"

gcc $strict -I. $sources -o "$tmp_dir/strategy-gcc"
"$tmp_dir/strategy-gcc"

clang $strict -I. $sources -o "$tmp_dir/strategy-clang"
"$tmp_dir/strategy-clang"

clang $strict -I. -c slipgate/sg_strategy.c -o "$tmp_dir/strategy-reducer.o"
if nm -u "$tmp_dir/strategy-reducer.o" | \
	grep -Eq '[[:space:]](malloc|calloc|realloc|free)$'; then
	echo "sg_strategy_test: reducer references dynamic allocation" >&2
	exit 1
fi

scan-build --status-bugs -o "$tmp_dir/scan-build" \
	clang $strict -I. $sources -o "$tmp_dir/strategy-static"

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -o "$tmp_dir/strategy-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/strategy-sanitize"
