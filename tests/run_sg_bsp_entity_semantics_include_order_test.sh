#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
strict=(-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align)

for compiler in gcc clang; do
	"$compiler" "${strict[@]}" -fsyntax-only -I"$root" \
		"$root/tests/sg_bsp_entity_semantics_before_g_local_compile_test.c" \
		"$root/tests/sg_bsp_entity_semantics_after_g_local_compile_test.c"
done
