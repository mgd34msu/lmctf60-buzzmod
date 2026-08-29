#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion
-Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
-Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_movement_hook_air_test.c
slipgate/sg_movement_hook_air.c
slipgate/sg_bsp_completeness_proof.c
slipgate/sg_bsp_completeness_core.c
slipgate/sg_bsp_completeness_region.c
slipgate/sg_bsp_completeness_traversal.c
slipgate/sg_bsp_completeness_lattice.c
slipgate/sg_bsp_completeness_coverage.c
slipgate/sg_bsp_completeness_state.c
slipgate/sg_bsp_completeness_portal.c
slipgate/sg_bsp_completeness_portal_index.c
slipgate/sg_configuration_semantics.c
slipgate/sg_configuration_lattice.c
slipgate/sg_configuration_space.c
slipgate/sg_host_collision.c
slipgate/sg_bsp_world.c
slipgate/sg_rune_model.c'
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)

cd "$repo_dir"

# The consumer must not regress to the synthetic visibility family, raw host
# callbacks, scalar pose sampling, or direct human-hook ownership.
if rg -n 'sg_hook_visibility_(catalog|feasibility)|sg_host_pmove_function_t|sg_movement_hook_air_direction_function_t|sg_movement_hook_air_pull_function_t' \
		slipgate/sg_movement_hook_air.h slipgate/sg_movement_hook_air.c
then
	echo 'forbidden synthetic authority or raw callback in hook-air consumer' >&2
	exit 1
fi
if rg -n 'PhaseVelocityValue|FindRegionWitness|CTF_Hook|LMCTF_HumanHook' \
		slipgate/sg_movement_hook_air.c
then
	echo 'forbidden sampled or human-hook implementation in hook-air consumer' >&2
	exit 1
fi
test -z "$(git diff --name-only fee0b0b -- p_weapon.c g_local.h \
	slipgate/sg_hook_game.c slipgate/sg_hook_live.c)"

for cc in gcc clang
do
	$cc $strict $isl_cflags -I. $sources -lm $isl_libs \
		-o "$tmp_dir/movement-hook-air-$cc"
	"$tmp_dir/movement-hook-air-$cc"
done

sanitize='-fno-omit-frame-pointer -fsanitize=address,undefined'
clang $strict $sanitize $isl_cflags -I. $sources -lm $isl_libs \
	-o "$tmp_dir/movement-hook-air-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/movement-hook-air-sanitize"

for source in slipgate/sg_movement_hook_air.c \
	tests/sg_movement_hook_air_test.c
do
	clang --analyze $strict $isl_cflags -I. "$source" \
		-o "$tmp_dir/$(basename "$source").plist"
done
