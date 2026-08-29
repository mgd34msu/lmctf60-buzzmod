#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion
-Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
-Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_ground_capability_publication_test.c
slipgate/sg_ground_capability_publication.c
slipgate/sg_ground_capability.c
slipgate/sg_host_pmove.c
slipgate/sg_configuration_audit.c
slipgate/sg_configuration_semantics.c
slipgate/sg_bsp_completeness_proof.c
slipgate/sg_bsp_completeness_core.c
slipgate/sg_bsp_completeness_region.c
slipgate/sg_bsp_completeness_traversal.c
slipgate/sg_bsp_completeness_lattice.c
slipgate/sg_bsp_completeness_coverage.c
slipgate/sg_bsp_completeness_state.c
slipgate/sg_bsp_completeness_portal.c
slipgate/sg_bsp_completeness_portal_index.c
slipgate/sg_configuration_lattice.c
slipgate/sg_configuration_space.c
slipgate/sg_host_collision.c
slipgate/sg_bsp_world.c
slipgate/sg_rune_model.c'
owned='tests/sg_ground_capability_publication_test.c
slipgate/sg_ground_capability_publication.c'
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)

cd "$repo_dir"

build_test()
{
	cc=$1
	suffix=$2
	extra=$3
	objects=

	for source in $sources
	do
		object="$tmp_dir/$(basename "$source" .c)-$suffix.o"
		$cc $strict $extra $isl_cflags -I. -c "$source" -o "$object"
		objects="$objects $object"
	done
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wno-strict-prototypes $extra -I. -DDEDICATED_ONLY \
		-c tests/support/yq2_pmove.c -o "$tmp_dir/yq2-$suffix.o"
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror $extra -I. \
		-c q_shared.c -o "$tmp_dir/shared-$suffix.o"
	$cc $extra $objects "$tmp_dir/yq2-$suffix.o" \
		"$tmp_dir/shared-$suffix.o" -lm $isl_libs \
		-o "$tmp_dir/ground-publication-$suffix"
}

for cc in gcc clang
do
	build_test "$cc" "$cc" ''
	"$tmp_dir/ground-publication-$cc"
done

sanitize='-fno-omit-frame-pointer -fsanitize=address,undefined'
build_test clang sanitize "$sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/ground-publication-sanitize"

for source in $owned
do
	clang $strict $isl_cflags -I. --analyze "$source" \
		-o "$tmp_dir/$(basename "$source").plist"
done
