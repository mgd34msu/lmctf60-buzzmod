#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wcast-align'
sources='tests/sg_strategy_caller_test.c slipgate/sg_strategy_caller.c slipgate/sg_strategy_runtime_bridge.c slipgate/sg_strategy.c'
sources="$sources slipgate/sg_authority_entropy.c"
sources="$sources slipgate/sg_rune_compact_field_service.c"
sources="$sources slipgate/sg_rune_compact_field.c slipgate/sg_rune_compact_field_region_hierarchy.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_eval.c"

cd "$repo_dir"

if sed -n '/^static int StrategyPlanRequest/,/^static int StrategyAuthorityEqual/p' \
	slipgate/sg_arach.c | \
	rg -n 'goal_field|route_field|SG_StrikeWeaponTargetField|SG_CollectibleArmorTargetField|SG_DefenseSupplyTargetField|Lead_Field'; then
	echo 'strategy plan construction still depends on a legacy route field' >&2
	exit 1
fi
if sed -n '/^static qboolean StrategyCommitFrame/,/^static void StrategyInterrupt/p' \
	slipgate/sg_arach.c | \
	rg -n 'goal_field|route_field|Think_PickLink|Think_CommitLink|link->action'; then
	echo 'compact strategy commit still crosses into legacy descent' >&2
	exit 1
fi
if rg -n 'SG_SidecarPath|SG_SIDECAR_DANGER|SG_DangerCheckpoint|SG_DangerPersistenceReset|live_seed_marks' \
	slipgate/sg_arach.c g_main.c slipgate/sg_local.h; then
	echo 'live runtime still references a removed legacy sidecar contract' >&2
	exit 1
fi

gcc $strict -I. $sources -lm -o "$tmp_dir/strategy-caller-gcc"
"$tmp_dir/strategy-caller-gcc"
gcc -std=c11 -Wall -Wextra -Werror -I. -Islipgate -c slipgate/sg_arach.c \
	-o "$tmp_dir/sg-arach-gcc.o"

clang $strict -I. $sources -lm -o "$tmp_dir/strategy-caller-clang"
"$tmp_dir/strategy-caller-clang"
clang -std=c11 -Wall -Wextra -Werror -I. -Islipgate -c slipgate/sg_arach.c \
	-o "$tmp_dir/sg-arach-clang.o"

scan-build --status-bugs -o "$tmp_dir/scan-build" \
	clang $strict -I. $sources -lm -o "$tmp_dir/strategy-caller-static"

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/strategy-caller-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/strategy-caller-sanitize"
