#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion
-Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
-Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_bsp_completeness_proof_test.c
slipgate/sg_bsp_completeness_proof.c
slipgate/sg_bsp_completeness_core.c
slipgate/sg_bsp_completeness_region.c
slipgate/sg_bsp_completeness_lattice.c
slipgate/sg_bsp_completeness_coverage.c
slipgate/sg_bsp_completeness_state.c
slipgate/sg_bsp_completeness_portal.c
slipgate/sg_bsp_completeness_portal_index.c
slipgate/sg_configuration_lattice.c slipgate/sg_configuration_space.c
slipgate/sg_host_collision.c slipgate/sg_bsp_world.c
slipgate/sg_rune_model.c'
scaling_sources='tests/sg_bsp_completeness_portal_index_scaling_test.c
slipgate/sg_bsp_completeness_core.c
slipgate/sg_bsp_completeness_lattice.c
slipgate/sg_bsp_completeness_portal.c
slipgate/sg_bsp_completeness_portal_index.c
slipgate/sg_configuration_lattice.c slipgate/sg_host_collision.c
slipgate/sg_bsp_world.c slipgate/sg_rune_model.c'
guard_sources='tests/sg_bsp_completeness_world_guard_test.c
slipgate/sg_bsp_completeness_proof.c
slipgate/sg_bsp_completeness_core.c
slipgate/sg_bsp_completeness_region.c
slipgate/sg_bsp_completeness_lattice.c
slipgate/sg_bsp_completeness_coverage.c
slipgate/sg_bsp_completeness_state.c
slipgate/sg_bsp_completeness_portal.c
slipgate/sg_bsp_completeness_portal_index.c
slipgate/sg_configuration_lattice.c slipgate/sg_host_collision.c
slipgate/sg_bsp_world.c slipgate/sg_rune_model.c'
owned='tests/sg_bsp_completeness_proof_test.c
tests/sg_bsp_completeness_portal_index_scaling_test.c
tests/sg_bsp_completeness_world_guard_test.c
slipgate/sg_bsp_completeness_proof.c
slipgate/sg_bsp_completeness_core.c
slipgate/sg_bsp_completeness_region.c
slipgate/sg_bsp_completeness_lattice.c
slipgate/sg_bsp_completeness_coverage.c
slipgate/sg_bsp_completeness_state.c
slipgate/sg_bsp_completeness_portal.c
slipgate/sg_bsp_completeness_portal_index.c'
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict $isl_cflags -I. $sources -lm $isl_libs \
		-o "$tmp_dir/bsp-completeness-$cc"
	"$tmp_dir/bsp-completeness-$cc"
	$cc $strict $isl_cflags -I. $scaling_sources -lm $isl_libs \
		-o "$tmp_dir/bsp-portal-scaling-$cc"
	"$tmp_dir/bsp-portal-scaling-$cc"
	$cc $strict $isl_cflags -I. $guard_sources -lm $isl_libs \
		-o "$tmp_dir/bsp-world-guard-$cc"
	"$tmp_dir/bsp-world-guard-$cc"
done

clang $strict $isl_cflags -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources -lm $isl_libs \
	-o "$tmp_dir/bsp-completeness-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/bsp-completeness-sanitize"
clang $strict $isl_cflags -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $scaling_sources -lm $isl_libs \
	-o "$tmp_dir/bsp-portal-scaling-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/bsp-portal-scaling-sanitize"
clang $strict $isl_cflags -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $guard_sources -lm $isl_libs \
	-o "$tmp_dir/bsp-world-guard-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/bsp-world-guard-sanitize"

for source in $owned
do
	clang $strict $isl_cflags -I. --analyze "$source" \
		-o "$tmp_dir/$(basename "$source").plist"
done
