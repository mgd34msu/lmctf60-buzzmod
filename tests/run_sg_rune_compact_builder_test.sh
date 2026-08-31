#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_rune_compact_builder_test.c slipgate/sg_rune_compact_builder.c'
isl_cflags=$(pkg-config --cflags isl)

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict $isl_cflags -I. $sources -lm \
		-o "$tmp_dir/rune-compact-builder-$cc"
	"$tmp_dir/rune-compact-builder-$cc"
done

real_sources="$sources slipgate/sg_weapon_effect_profile.c slipgate/sg_rune_model.c slipgate/sg_bsp_world.c slipgate/sg_bsp_entity_semantics.c slipgate/sg_rune_source_authority.c slipgate/sg_crc32.c"
real_flags='-DSG_COMPACT_BUILDER_REAL_WEAPONS -DSG_COMPACT_BUILDER_REAL_BSP -DSG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY -DSG_COMPACT_BUILDER_REAL_ENTITY_SEMANTICS'
for cc in gcc clang
do
	$cc $strict $isl_cflags $real_flags -I. $real_sources -lm \
		-o "$tmp_dir/rune-compact-builder-real-$cc"
	"$tmp_dir/rune-compact-builder-real-$cc"
	$cc $strict $isl_cflags $real_flags -DWEAP_BALANCE_OK -I. \
		$real_sources -lm -o "$tmp_dir/rune-compact-builder-balanced-$cc"
	"$tmp_dir/rune-compact-builder-balanced-$cc"
done

clang $strict $isl_cflags -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources -lm \
	-o "$tmp_dir/rune-compact-builder-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/rune-compact-builder-sanitize"

clang $strict $isl_cflags $real_flags -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $real_sources -lm \
	-o "$tmp_dir/rune-compact-builder-real-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/rune-compact-builder-real-sanitize"
