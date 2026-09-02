#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
sources='tests/sg_tactic_execution_test.c slipgate/sg_tactic_execution.c'

cd "$repo_dir"

if rg -n 'goal_field|route_field|Think_PickLink|Think_CommitLink|bestlink|link->action' \
	slipgate/sg_tactic_execution.h slipgate/sg_tactic_execution.c; then
	echo 'compact tactic execution still owns legacy route state' >&2
	exit 1
fi
if sed -n '/^static void CompactTacticEmit/,/^void SG_BotThink/p' \
	slipgate/sg_arach.c | \
	rg -n 'goal_field|route_field|Think_TacticalRoute|Think_PickLink|Think_CommitLink|Think_Move|Think_Emit|commit_link|bestlink|link->action'; then
	echo 'live compact command path still reaches legacy descent state' >&2
	exit 1
fi
if sed -n '/^static qboolean CompactTacticTarget/,/^void SG_BotThink/p' \
	slipgate/sg_arach.c | \
	rg -n 'SG_TACTIC_EXECUTION_(PORTAL|DIRECT|STANCE)_STEP|target_stance'; then
	echo 'unsealed compact command path infers body input from a STEP' >&2
	exit 1
fi
if ! rg -U -q 'if \(compact_current\)\n\t\{\n\t\tCompactBotThink\(bot, e\);\n\t\treturn;\n\t\}\n\tif \(!rune_compatible\)' \
	slipgate/sg_arach.c; then
	echo 'live compact branch no longer returns before the legacy rune gate' >&2
	exit 1
fi
if ! sed -n '/^static qboolean StrategyCommitFrame/,/^static void StrategyInterrupt/p' \
	slipgate/sg_arach.c | rg -q 'SG_TacticRuntimePrepareStep' || \
   ! sed -n '/^static qboolean StrategyCommitFrame/,/^static void StrategyInterrupt/p' \
	slipgate/sg_arach.c | rg -q 'SG_TacticExecutionDispatchSelected' || \
   ! rg -q 'SG_TacticControl\(' slipgate/sg_arach.c; then
	echo 'live compact STEP no longer reaches the selector and the executor' >&2
	exit 1
fi
if sed -n '/^static qboolean StrategyCommitFrame/,/^static void StrategyInterrupt/p' \
	slipgate/sg_arach.c | \
	rg -n 'sg_tactic_request_t|sg_tactic_authority_t|sg_tactic_capability_descriptor_t'; then
	echo 'sg_arach constructs tactic authority instead of using the owner' >&2
	exit 1
fi

gcc $strict -I. $sources -lm -o "$tmp_dir/tactic-execution-gcc"
"$tmp_dir/tactic-execution-gcc"

clang $strict -I. $sources -lm -o "$tmp_dir/tactic-execution-clang"
"$tmp_dir/tactic-execution-clang"

clang $strict -fno-omit-frame-pointer -fsanitize=address -I. \
	$sources -lm -o "$tmp_dir/tactic-execution-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	"$tmp_dir/tactic-execution-asan"

clang $strict -fno-omit-frame-pointer -fsanitize=undefined -I. \
	$sources -lm -o "$tmp_dir/tactic-execution-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/tactic-execution-ubsan"

clang $strict -I. -c slipgate/sg_tactic_execution.c \
	-o "$tmp_dir/sg_tactic_execution.o"
if nm -u "$tmp_dir/sg_tactic_execution.o" | \
	rg ' (malloc|calloc|realloc|free)$'; then
	echo 'compact tactic dispatch unexpectedly allocates' >&2
	exit 1
fi
