#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_weapon_effect_profile_test.c slipgate/sg_weapon_effect_profile.c slipgate/sg_rune_model.c'
correction_sources='tests/sg_weapon_effect_profile_correction_test.c slipgate/sg_weapon_effect_profile.c slipgate/sg_rune_model.c'

cd "$repo_dir"

gcc $strict -I. $sources -lm -o "$tmp_dir/weapon-profile-gcc"
"$tmp_dir/weapon-profile-gcc"

clang $strict -I. $sources -lm -o "$tmp_dir/weapon-profile-clang"
"$tmp_dir/weapon-profile-clang"

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/weapon-profile-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/weapon-profile-sanitize"

analysis_index=0
for analysis_source in $sources $correction_sources; do
	analysis_index=$((analysis_index + 1))
	clang $strict --analyze -I. "$analysis_source" \
		-o "$tmp_dir/analysis-$analysis_index.plist"
done
gcc $strict -I. $correction_sources -lm -o "$tmp_dir/correction-gcc"
"$tmp_dir/correction-gcc"
clang $strict -I. $correction_sources -lm -o "$tmp_dir/correction-clang"
"$tmp_dir/correction-clang"
clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$correction_sources -lm -o "$tmp_dir/correction-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/correction-sanitize"

gcc $strict -DWEAP_BALANCE_OK -I. $sources -lm \
	-o "$tmp_dir/weapon-profile-balanced-gcc"
"$tmp_dir/weapon-profile-balanced-gcc"

clang $strict -DWEAP_BALANCE_OK -I. $sources -lm \
	-o "$tmp_dir/weapon-profile-balanced-clang"
"$tmp_dir/weapon-profile-balanced-clang"

clang $strict -DWEAP_BALANCE_OK -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources -lm \
	-o "$tmp_dir/weapon-profile-balanced-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/weapon-profile-balanced-sanitize"

analysis_index=0
for analysis_source in $sources $correction_sources; do
	analysis_index=$((analysis_index + 1))
	clang $strict -DWEAP_BALANCE_OK --analyze -I. "$analysis_source" \
		-o "$tmp_dir/analysis-balanced-$analysis_index.plist"
done
gcc $strict -DWEAP_BALANCE_OK -I. $correction_sources -lm \
	-o "$tmp_dir/correction-balanced-gcc"
"$tmp_dir/correction-balanced-gcc"
clang $strict -DWEAP_BALANCE_OK -I. $correction_sources -lm \
	-o "$tmp_dir/correction-balanced-clang"
"$tmp_dir/correction-balanced-clang"
clang $strict -DWEAP_BALANCE_OK -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $correction_sources -lm \
	-o "$tmp_dir/correction-balanced-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/correction-balanced-sanitize"
