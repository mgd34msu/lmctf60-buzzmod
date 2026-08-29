#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wcast-align'
sources='tests/sg_strategy_caller_test.c slipgate/sg_strategy_caller.c slipgate/sg_strategy_runtime_bridge.c slipgate/sg_strategy.c'

cd "$repo_dir"

gcc $strict -I. $sources -o "$tmp_dir/strategy-caller-gcc"
"$tmp_dir/strategy-caller-gcc"

clang $strict -I. $sources -o "$tmp_dir/strategy-caller-clang"
"$tmp_dir/strategy-caller-clang"

scan-build --status-bugs -o "$tmp_dir/scan-build" \
	clang $strict -I. $sources -o "$tmp_dir/strategy-caller-static"

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -o "$tmp_dir/strategy-caller-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/strategy-caller-sanitize"
