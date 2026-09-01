#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_rune_compact_portal_snapshot_test.c slipgate/sg_rune_compact_portal_snapshot.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. -Islipgate $sources -o "$tmp_dir/portal-snapshot-$cc"
	"$tmp_dir/portal-snapshot-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined \
	-I. -Islipgate $sources -o "$tmp_dir/portal-snapshot-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/portal-snapshot-sanitize"
