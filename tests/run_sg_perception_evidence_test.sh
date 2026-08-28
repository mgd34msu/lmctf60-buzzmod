#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wcast-align'
sources='tests/sg_perception_evidence_test.c slipgate/sg_perception_evidence.c slipgate/sg_belief.c'

cd "$repo_dir"

gcc $strict -I. $sources -lm -o "$tmp_dir/perception-gcc"
"$tmp_dir/perception-gcc"

clang $strict -I. $sources -lm -o "$tmp_dir/perception-clang"
"$tmp_dir/perception-clang"

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/perception-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/perception-sanitize"

clang $strict --analyze -Xanalyzer -analyzer-output=text -I. $sources
