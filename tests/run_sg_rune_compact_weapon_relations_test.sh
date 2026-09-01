#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
sources='tests/sg_rune_compact_weapon_relations_test.c slipgate/sg_rune_compact_weapon_relations.c slipgate/sg_rune_compact_weapon_field.c slipgate/sg_rune_compact_model.c slipgate/sg_rune_compact_source_surface_catalog.c slipgate/sg_rune_compact_weapon_catalog.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_static.c slipgate/sg_weapon_effect_profile.c slipgate/sg_rune_model.c slipgate/sg_bsp_entity_semantics_audit_expected.c slipgate/sg_bsp_entity_semantics.c slipgate/sg_host_collision.c slipgate/sg_bsp_world.c'

cd "$repo_dir"

gcc $strict -DSG_RUNE_COMPACT_WEAPON_RELATIONS_TESTING -I. -Islipgate \
	$sources -lm -o "$tmp_dir/gcc"
"$tmp_dir/gcc"

clang $strict -DSG_RUNE_COMPACT_WEAPON_RELATIONS_TESTING -I. -Islipgate \
	$sources -lm -o "$tmp_dir/clang"
"$tmp_dir/clang"

clang $strict -DSG_RUNE_COMPACT_WEAPON_RELATIONS_TESTING \
	-fsanitize=address -fno-omit-frame-pointer -I. -Islipgate $sources \
	-lm -o "$tmp_dir/asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$tmp_dir/asan"

clang $strict -DSG_RUNE_COMPACT_WEAPON_RELATIONS_TESTING \
	-fsanitize=undefined -fno-omit-frame-pointer -I. -Islipgate $sources \
	-lm -o "$tmp_dir/ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$tmp_dir/ubsan"
