#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_cell_phase_localization_test.c slipgate/sg_cell_phase_localization.c slipgate/sg_host_collision.c slipgate/sg_rune_model.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. $sources -lm -o "$tmp_dir/localization-$cc"
	"$tmp_dir/localization-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/localization-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/localization-sanitize"

clang --analyze -std=c11 -Wall -Wextra -Wpedantic -Werror -I. \
	-Xanalyzer -analyzer-output=text \
	slipgate/sg_cell_phase_localization.c
