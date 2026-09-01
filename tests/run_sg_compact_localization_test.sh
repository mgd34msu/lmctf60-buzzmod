#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion'
strict="$strict -Wsign-conversion -Wshadow -Wstrict-prototypes"
strict="$strict -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align"
sources='tests/sg_compact_localization_test.c slipgate/sg_compact_localization.c'
sources="$sources slipgate/sg_rune_compact_localize.c"
sources="$sources slipgate/sg_rune_compact_spatial_index.c"

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. $sources -lm -o "$tmp_dir/compact-localization-$cc"
	"$tmp_dir/compact-localization-$cc"
done

clang $strict -fsanitize=address,undefined -fno-omit-frame-pointer -I. \
	$sources -lm -o "$tmp_dir/compact-localization-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/compact-localization-sanitize"

clang --analyze $strict -I. slipgate/sg_compact_localization.c \
	-o "$tmp_dir/compact-localization.plist"
