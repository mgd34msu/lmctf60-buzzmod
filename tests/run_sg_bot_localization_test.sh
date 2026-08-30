#!/bin/sh
set -eu

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror'
sources='tests/sg_bot_localization_test.c slipgate/sg_bot_localization.c'
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

for cc in gcc clang; do
	$cc $strict -I. $sources -lm -o "$tmp_dir/bot-localization-$cc"
	"$tmp_dir/bot-localization-$cc"
done

sanitize='-fsanitize=address,undefined -fno-omit-frame-pointer'
clang $strict $sanitize -I. $sources -lm -o "$tmp_dir/bot-localization-sanitize"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
	"$tmp_dir/bot-localization-sanitize"

clang --analyze $strict -I. slipgate/sg_bot_localization.c \
	-o "$tmp_dir/analyzer.plist"
python3 -B tests/test_bot_localization_integration.py
