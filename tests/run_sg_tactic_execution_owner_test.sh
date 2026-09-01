#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
sources='tests/sg_tactic_execution_owner_test.c slipgate/sg_tactic_execution_owner.c'

cd "$repo_dir"

if rg -n 'usercmd_t|sg_tactic_runtime|sg_rune_compact_field|callback|fiber|target_point' \
	slipgate/sg_tactic_execution_owner.h; then
	echo 'public execution-owner vocabulary leaks private execution data' >&2
	exit 1
fi
if rg -n 'SG_TacticExecutionOwner|sg_tactic_execution_owner_t' \
	slipgate/sg_tactic_execution_owner.h; then
	echo 'public execution-owner vocabulary exposes owner operations or state' >&2
	exit 1
fi
if rg -n 'Think_PickLink|Think_CommitLink|bestlink|link->action|ClientThink|Cmd_Hook_f|ctf_hook_abort|->use|->touch' \
	slipgate/sg_tactic_execution_owner.c; then
	echo 'base execution owner reaches a legacy or unsealed actuator' >&2
	exit 1
fi

gcc $strict -I. $sources -lm -o "$tmp_dir/owner-production-gcc"
"$tmp_dir/owner-production-gcc"
gcc $strict -DSG_TACTIC_EXECUTION_OWNER_TESTING -I. $sources -lm \
	-o "$tmp_dir/owner-test-gcc"
"$tmp_dir/owner-test-gcc"

clang $strict -I. $sources -lm -o "$tmp_dir/owner-production-clang"
"$tmp_dir/owner-production-clang"
clang $strict -DSG_TACTIC_EXECUTION_OWNER_TESTING -I. $sources -lm \
	-o "$tmp_dir/owner-test-clang"
"$tmp_dir/owner-test-clang"

clang $strict -DSG_TACTIC_EXECUTION_OWNER_TESTING -fno-omit-frame-pointer \
	-fsanitize=address -I. $sources -lm -o "$tmp_dir/owner-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$tmp_dir/owner-asan"

clang $strict -DSG_TACTIC_EXECUTION_OWNER_TESTING -fno-omit-frame-pointer \
	-fsanitize=undefined -I. $sources -lm -o "$tmp_dir/owner-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$tmp_dir/owner-ubsan"

scan-build --status-bugs -o "$tmp_dir/scan-build" \
	clang $strict -DSG_TACTIC_EXECUTION_OWNER_TESTING -I. $sources -lm \
	-o "$tmp_dir/owner-static"
scan-build --status-bugs -o "$tmp_dir/scan-build-production" \
	clang $strict -I. $sources -lm -o "$tmp_dir/owner-static-production"

clang $strict -I. -c slipgate/sg_tactic_execution_owner.c \
	-o "$tmp_dir/sg_tactic_execution_owner.o"
if nm "$tmp_dir/sg_tactic_execution_owner.o" | \
	rg 'SG_TacticExecutionOwnerTest'; then
	echo 'production execution owner exports a test seam' >&2
	exit 1
fi
