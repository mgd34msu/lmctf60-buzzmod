#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections'
sources='tests/sg_combat_compact_weapon_field_test.c slipgate/sg_combat.c slipgate/sg_rune_compact_model.c'
entrypoint_sources='tests/sg_combat_compact_entrypoint_test.c q_shared.c slipgate/sg_rune_compact_model.c'

cd "$repo_dir"

for cc in gcc clang; do
	$cc $strict -I. $sources -Wl,--gc-sections -lm \
		-o "$tmp_dir/combat-compact-weapon-field-$cc"
	"$tmp_dir/combat-compact-weapon-field-$cc"
done

for cc in gcc clang; do
	$cc $strict -I. $entrypoint_sources -Wl,--gc-sections -lm \
		-o "$tmp_dir/combat-compact-entrypoint-$cc"
	"$tmp_dir/combat-compact-entrypoint-$cc"
done

for forbidden in \
	'SG_Rune(' 'sg_caco_enemies' 'Rune_NearestSeed' 'seed' 'link' \
	'->seeds' '->first_link' '->next_link' '->links' 'lost_seed' \
	'SG_CombatAlertSelect'; do
	if rg -n -F -- "$forbidden" slipgate/sg_combat.c; then
		echo "forbidden legacy combat symbol: $forbidden" >&2
		exit 1
	fi
done

for forbidden in 'attachment->profile' 'attachment->family' \
	'attachment->kernel'; do
	if rg -n -F -- "$forbidden" slipgate/sg_combat.c; then
		echo "legacy per-kernel combat attachment assumption: $forbidden" >&2
		exit 1
	fi
done

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -Wl,--gc-sections -lm \
	-o "$tmp_dir/combat-compact-weapon-field-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/combat-compact-weapon-field-sanitize"

clang $strict --analyze -Xanalyzer -analyzer-output=text -I. \
	slipgate/sg_combat.c
