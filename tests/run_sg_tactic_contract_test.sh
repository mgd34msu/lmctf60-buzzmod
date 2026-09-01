#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'

cd "$repo_dir"

if rg -n 'SG_RUNE_COMPACT_FIELD_(DISCONNECTED|LOCAL_DESTINATION|CELL_DESTINATION|MECHANISMS_REQUIRED|BLOCKED_NOW)' \
	slipgate/sg_tactic_contract.h; then
	echo 'terminal field results leaked into tactic contract' >&2
	exit 1
fi

gcc $strict -I. tests/sg_tactic_contract_test.c -lm \
	-o "$tmp_dir/tactic-contract-gcc"
"$tmp_dir/tactic-contract-gcc"

clang $strict -I. tests/sg_tactic_contract_test.c -lm \
	-o "$tmp_dir/tactic-contract-clang"
"$tmp_dir/tactic-contract-clang"

clang $strict -fno-omit-frame-pointer -fsanitize=address -I. \
	tests/sg_tactic_contract_test.c -lm -o "$tmp_dir/tactic-contract-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$tmp_dir/tactic-contract-asan"

clang $strict -fno-omit-frame-pointer -fsanitize=undefined -I. \
	tests/sg_tactic_contract_test.c -lm -o "$tmp_dir/tactic-contract-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$tmp_dir/tactic-contract-ubsan"
