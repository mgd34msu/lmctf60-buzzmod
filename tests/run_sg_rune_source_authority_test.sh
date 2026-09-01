#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

cd "$repo_dir"

strict='-std=c11 -Wall -Wextra -Werror -Wpedantic'
sources='tests/sg_rune_source_authority_test.c slipgate/sg_rune_source_authority.c slipgate/sg_crc32.c'
wrap='-Wl,--wrap=malloc -Wl,--wrap=realloc'

for cc in gcc clang
do
	$cc $strict -I. $sources $wrap -o "$tmp_dir/source-authority-$cc"
	"$tmp_dir/source-authority-$cc"
	$cc $strict -DWEAP_BALANCE_OK -I. $sources $wrap \
		-o "$tmp_dir/source-authority-balanced-$cc"
	"$tmp_dir/source-authority-balanced-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources $wrap -o "$tmp_dir/source-authority-sanitize"
ASAN_OPTIONS=detect_leaks=1 "$tmp_dir/source-authority-sanitize"

clang $strict -DWEAP_BALANCE_OK -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources $wrap \
	-o "$tmp_dir/source-authority-balanced-sanitize"
ASAN_OPTIONS=detect_leaks=1 "$tmp_dir/source-authority-balanced-sanitize"

clang $strict --analyze -I. slipgate/sg_rune_source_authority.c \
	-o "$tmp_dir/source-authority.plist"

echo 'run_sg_rune_source_authority_test: ok'
