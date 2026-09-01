#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
sources='tests/sg_tactic_policy_test.c slipgate/sg_tactic_policy.c'

cd "$repo_dir"

if rg -n 'tactic_seed|route_cost|sg_field_key|SG_TacticCache|bestlink|link->action' \
	slipgate/sg_tactic_policy.h slipgate/sg_tactic_policy.c; then
	echo 'tactic policy still owns forbidden route state' >&2
	exit 1
fi
if rg -n 'SG_RUNE_COMPACT_FIELD_(DISCONNECTED|LOCAL_DESTINATION|CELL_DESTINATION|MECHANISMS_REQUIRED|BLOCKED_NOW)' \
	slipgate/sg_tactic_contract.h slipgate/sg_tactic_policy.c; then
	echo 'terminal field results leaked into tactic selection' >&2
	exit 1
fi

gcc $strict -I. $sources -lm -o "$tmp_dir/tactic-policy-gcc"
"$tmp_dir/tactic-policy-gcc"

clang $strict -I. $sources -lm -o "$tmp_dir/tactic-policy-clang"
"$tmp_dir/tactic-policy-clang"

clang $strict -fno-omit-frame-pointer -fsanitize=address -I. \
	$sources -lm -o "$tmp_dir/tactic-policy-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	"$tmp_dir/tactic-policy-asan"

clang $strict -fno-omit-frame-pointer -fsanitize=undefined -I. \
	$sources -lm -o "$tmp_dir/tactic-policy-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/tactic-policy-ubsan"

clang $strict -I. -c slipgate/sg_tactic_policy.c \
	-o "$tmp_dir/sg_tactic_policy.o"
if nm -u "$tmp_dir/sg_tactic_policy.o" | \
	rg ' (malloc|calloc|realloc|free)$'; then
	echo 'tactic selection unexpectedly allocates' >&2
	exit 1
fi
