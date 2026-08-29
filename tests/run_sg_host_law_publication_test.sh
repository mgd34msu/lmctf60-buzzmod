#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion'
strict="$strict -Wsign-conversion -Wshadow -Wstrict-prototypes"
strict="$strict -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align"
sources='tests/sg_host_law_publication_test.c slipgate/sg_host_law_publication.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. -DDEDICATED_ONLY $sources -lm \
		-o "$tmp_dir/host-law-$cc"
	"$tmp_dir/host-law-$cc"
done

sanitize='-fno-omit-frame-pointer -fsanitize=address,undefined'
clang $strict $sanitize -I. -DDEDICATED_ONLY $sources -lm \
	-o "$tmp_dir/host-law-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/host-law-sanitize"

clang --analyze $strict -I. -DDEDICATED_ONLY \
	slipgate/sg_host_law_publication.c -o "$tmp_dir/host-law.plist"
