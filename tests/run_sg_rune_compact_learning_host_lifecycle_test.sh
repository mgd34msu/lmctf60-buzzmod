#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

sources='tests/sg_rune_compact_learning_consumer_test.c tests/sg_rune_compact_learning_consumer_fixture.c p_hud.c g_spawn.c slipgate/sg_arach.c slipgate/sg_tactic_controller.c slipgate/sg_tactic_runtime.c slipgate/sg_strategy_runtime_bridge.c slipgate/sg_strategy_caller.c slipgate/sg_rune_compact_learning_game.c slipgate/sg_rune_compact_learning_consumer.c slipgate/sg_rune_compact_learning.c slipgate/sg_rune_compact_production.c slipgate/sg_rune_source_authority.c slipgate/sg_crc32.c slipgate/sg_rune_compact_localize.c slipgate/sg_rune_compact_model.c slipgate/sg_rune_compact_source_surface_catalog.c slipgate/sg_rune_compact_weapon_catalog.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_static.c slipgate/sg_rune_model.c'
flags='-std=c11 -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections -DSG_RUNE_COMPACT_LEARNING_HOST_LIFECYCLE_TEST -DSG_RUNE_COMPACT_LEARNING_HOST_RUNTIME_TEST -I. -Islipgate'

cd "$repo_dir"

for cc in gcc clang
do
	$cc $flags $sources -Wl,--gc-sections -lm \
		-o "$tmp_dir/rune-compact-learning-host-$cc"
	"$tmp_dir/rune-compact-learning-host-$cc"
done

echo "compact RUNE learning host lifecycle checks passed"
