#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
sources='tests/sg_tactic_field_selector_test.c slipgate/sg_tactic_runtime.c slipgate/sg_tactic_policy.c slipgate/sg_authority_entropy.c slipgate/sg_strategy_runtime_bridge.c slipgate/sg_strategy_caller.c slipgate/sg_strategy.c slipgate/sg_rune_compact_field_service.c slipgate/sg_rune_compact_field.c slipgate/sg_rune_compact_field_region_hierarchy.c slipgate/sg_compact_localization.c slipgate/sg_rune_compact_spatial_index.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_eval.c'

cd "$repo_dir"

gcc $strict -I. $sources -lm -o "$tmp_dir/tactic-field-selector-gcc"
"$tmp_dir/tactic-field-selector-gcc"

clang $strict -I. $sources -lm -o "$tmp_dir/tactic-field-selector-clang"
"$tmp_dir/tactic-field-selector-clang"

scan-build --status-bugs -o "$tmp_dir/scan-runtime" \
	clang $strict -I. -c slipgate/sg_tactic_runtime.c \
	-o "$tmp_dir/sg_tactic_runtime-static.o"
scan-build --status-bugs -o "$tmp_dir/scan-bridge" \
	clang $strict -I. -c slipgate/sg_strategy_runtime_bridge.c \
	-o "$tmp_dir/sg_strategy_runtime_bridge-static.o"
scan-build --status-bugs -o "$tmp_dir/scan-caller" \
	clang $strict -I. -c slipgate/sg_strategy_caller.c \
	-o "$tmp_dir/sg_strategy_caller-static.o"

clang $strict -fno-omit-frame-pointer -fsanitize=address -I. \
	$sources -lm -o "$tmp_dir/tactic-field-selector-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	"$tmp_dir/tactic-field-selector-asan"

clang $strict -fno-omit-frame-pointer -fsanitize=undefined -I. \
	$sources -lm -o "$tmp_dir/tactic-field-selector-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/tactic-field-selector-ubsan"
