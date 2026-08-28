#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_water_capability_test.c tests/sg_water_capability_fixture.c slipgate/sg_water_capability.c slipgate/sg_configuration_lattice.c slipgate/sg_host_pmove.c slipgate/sg_host_collision.c slipgate/sg_bsp_world.c slipgate/sg_rune_model.c'
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict $isl_cflags -I. $sources -lm $isl_libs \
		-o "$tmp_dir/water-capability-$cc"
	"$tmp_dir/water-capability-$cc"
done

clang $strict $isl_cflags -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources -lm $isl_libs \
	-o "$tmp_dir/water-capability-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/water-capability-sanitize"

for source in slipgate/sg_water_capability.c tests/sg_water_capability_fixture.c
do
	clang --analyze $strict $isl_cflags -I. "$source" \
		-o "$tmp_dir/$(basename "$source").plist"
done
