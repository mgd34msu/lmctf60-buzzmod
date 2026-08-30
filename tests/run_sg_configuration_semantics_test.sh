#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -DSG_CONFIGURATION_SEMANTICS_TESTING -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_configuration_semantics_test.c slipgate/sg_configuration_semantics.c slipgate/sg_configuration_lattice.c slipgate/sg_configuration_space.c slipgate/sg_configuration_audit.c slipgate/sg_host_collision.c slipgate/sg_bsp_world.c slipgate/sg_rune_model.c'
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict $isl_cflags -I. $sources -lm $isl_libs \
		-o "$tmp_dir/configuration-semantics-$cc"
	"$tmp_dir/configuration-semantics-$cc"
done

clang $strict $isl_cflags -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources -lm $isl_libs \
	-o "$tmp_dir/configuration-semantics-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/configuration-semantics-sanitize"
