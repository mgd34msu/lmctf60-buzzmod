#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off -ffunction-sections -fdata-sections'
allocation_test='-DSG_RUNE_COMPACT_WEAPON_FIELD_TEST_WRAP_CALLOC -Wl,--wrap=calloc -Wl,--gc-sections'
sources='tests/sg_rune_compact_weapon_field_test.c slipgate/sg_rune_compact_weapon_field.c slipgate/sg_rune_compact_model.c slipgate/sg_rune_compact_source_surface_catalog.c slipgate/sg_rune_compact_weapon_catalog.c slipgate/sg_rune_compact_static.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_eval.c slipgate/sg_weapon_effect_profile.c slipgate/sg_bsp_entity_semantics_audit_expected.c'

cd "$repo_dir"

gcc $strict $allocation_test -I. $sources -lm -o "$tmp_dir/rune-compact-weapon-field-gcc"
"$tmp_dir/rune-compact-weapon-field-gcc"

clang $strict $allocation_test -I. $sources -lm -o "$tmp_dir/rune-compact-weapon-field-clang"
"$tmp_dir/rune-compact-weapon-field-clang"

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=address -I. \
	$sources -lm -o "$tmp_dir/rune-compact-weapon-field-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	"$tmp_dir/rune-compact-weapon-field-asan"

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=undefined -I. \
	$sources -lm -o "$tmp_dir/rune-compact-weapon-field-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/rune-compact-weapon-field-ubsan"

# The sealed compact profile records include resolved pellet/lane/direct
# shapes. Exercise the enabled host-balance law as a distinct build, rather
# than treating the default build's profile shapes as universal.
gcc $strict $allocation_test -DWEAP_BALANCE_OK -I. $sources -lm \
	-o "$tmp_dir/rune-compact-weapon-field-balance-gcc"
"$tmp_dir/rune-compact-weapon-field-balance-gcc"

clang $strict $allocation_test -DWEAP_BALANCE_OK -I. $sources -lm \
	-o "$tmp_dir/rune-compact-weapon-field-balance-clang"
"$tmp_dir/rune-compact-weapon-field-balance-clang"

clang $strict $allocation_test -DWEAP_BALANCE_OK -fno-omit-frame-pointer \
	-fsanitize=address -I. $sources -lm \
	-o "$tmp_dir/rune-compact-weapon-field-balance-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	"$tmp_dir/rune-compact-weapon-field-balance-asan"

clang $strict $allocation_test -DWEAP_BALANCE_OK -fno-omit-frame-pointer \
	-fsanitize=undefined -I. $sources -lm \
	-o "$tmp_dir/rune-compact-weapon-field-balance-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/rune-compact-weapon-field-balance-ubsan"
