#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wcast-align'
sources='tests/sg_belief_test.c slipgate/sg_belief.c'

cd "$repo_dir"

python3 -B tests/test_belief_life_identity_contract.py

gcc $strict -I. $sources -lm -o "$tmp_dir/belief-gcc"
"$tmp_dir/belief-gcc"

clang $strict -I. $sources -lm -o "$tmp_dir/belief-clang"
"$tmp_dir/belief-clang"

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/belief-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/belief-sanitize"

clang $strict --analyze -Xanalyzer -analyzer-output=text -I. \
	tests/sg_belief_test.c slipgate/sg_belief.c
