#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
defines='-DSG_RUNE_COMPACT_MECHANISMS_TESTING -DSG_RUNE_COMPACT_MECHANISMS_BUILD_TESTING -DSG_RUNE_COMPACT_MECHANISMS_ENTITIES_TESTING -DSG_RUNE_COMPACT_MECHANISM_TRANSITIONS_TESTING'
sources='tests/sg_rune_compact_mechanisms_build_test.c slipgate/sg_rune_compact_mechanisms.c slipgate/sg_rune_compact_mechanisms_build.c slipgate/sg_rune_compact_mechanisms_entities.c slipgate/sg_rune_compact_mechanisms_transitions.c slipgate/sg_rune_compact_localize.c slipgate/sg_configuration_lattice.c'
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)

cd "$repo_dir"
for compiler in gcc clang
do
	$compiler $strict $defines $isl_cflags -I. $sources $isl_libs -lm \
		-o "$tmp_dir/build-$compiler"
	"$tmp_dir/build-$compiler"
done

clang $strict $defines $isl_cflags -fno-omit-frame-pointer \
	-fsanitize=address -I. $sources $isl_libs -lm -o "$tmp_dir/build-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$tmp_dir/build-asan"

clang $strict $defines $isl_cflags -fno-omit-frame-pointer \
	-fsanitize=undefined -I. $sources $isl_libs -lm -o "$tmp_dir/build-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$tmp_dir/build-ubsan"

clang --analyze $strict $defines $isl_cflags -I. \
	slipgate/sg_rune_compact_mechanisms_build.c
clang --analyze $strict $defines $isl_cflags -I. \
	tests/sg_rune_compact_mechanisms_build_test.c
