#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -DSG_BELIEF_TESTING -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wcast-align'
sources='tests/sg_belief_runtime_test.c slipgate/sg_belief_runtime.c slipgate/sg_perception_evidence.c slipgate/sg_belief.c slipgate/sg_rune_v2_content_identity.c'

cd "$repo_dir"

gcc $strict -I. $sources -lm -o "$tmp_dir/belief-runtime-gcc"
"$tmp_dir/belief-runtime-gcc"

clang $strict -I. $sources -lm -o "$tmp_dir/belief-runtime-clang"
"$tmp_dir/belief-runtime-clang"

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/belief-runtime-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/belief-runtime-sanitize"

clang $strict --analyze -Xanalyzer -analyzer-output=text -I. $sources
