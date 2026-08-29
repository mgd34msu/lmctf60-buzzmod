#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
strict="$strict -DSG_PHASE_CATALOG_TESTING"
sources='tests/sg_phase_catalog_test.c
slipgate/sg_phase_catalog.c
slipgate/sg_phase_catalog_audit.c
slipgate/sg_phase_catalog_publication.c
slipgate/sg_phase_catalog_owner.c
slipgate/sg_phase_mover_support_provider.c
slipgate/sg_mechanism_capability_seal.c
slipgate/sg_configuration_semantics.c
slipgate/sg_configuration_lattice.c
slipgate/sg_configuration_space.c
slipgate/sg_configuration_audit.c
slipgate/sg_host_collision.c
slipgate/sg_bsp_world.c
slipgate/sg_rune_model.c'
mechanism_sources='slipgate/sg_mechanism_capability.c
slipgate/sg_bsp_completeness_proof.c
slipgate/sg_bsp_completeness_core.c
slipgate/sg_bsp_completeness_region.c
slipgate/sg_bsp_completeness_traversal.c
slipgate/sg_bsp_completeness_lattice.c
slipgate/sg_bsp_completeness_coverage.c
slipgate/sg_bsp_completeness_state.c
slipgate/sg_bsp_completeness_portal.c
slipgate/sg_bsp_completeness_portal_index.c'
sources="$sources
$mechanism_sources"
model_sources='tests/sg_phase_catalog_model_integration_test.c
slipgate/sg_phase_catalog.c
slipgate/sg_phase_catalog_audit.c
slipgate/sg_phase_catalog_publication.c
slipgate/sg_phase_catalog_owner.c
slipgate/sg_phase_mover_support_provider.c
slipgate/sg_mechanism_capability_seal.c
slipgate/sg_configuration_semantics.c
slipgate/sg_configuration_lattice.c
slipgate/sg_configuration_space.c
slipgate/sg_configuration_audit.c
slipgate/sg_host_collision.c
slipgate/sg_bsp_world.c
slipgate/sg_rune_model.c'
model_sources="$model_sources
$mechanism_sources"
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)
producer_sources='tests/sg_phase_catalog_mechanism_integration_test.c
slipgate/sg_mechanism_capability.c
slipgate/sg_mechanism_capability_seal.c
slipgate/sg_phase_catalog.c
slipgate/sg_phase_catalog_audit.c
slipgate/sg_phase_catalog_publication.c
slipgate/sg_phase_catalog_owner.c
slipgate/sg_phase_mover_support_provider.c
slipgate/sg_configuration_semantics.c
slipgate/sg_configuration_lattice.c
slipgate/sg_configuration_space.c
slipgate/sg_configuration_audit.c
slipgate/sg_host_collision.c
slipgate/sg_bsp_world.c
slipgate/sg_rune_model.c
slipgate/sg_bsp_completeness_proof.c
slipgate/sg_bsp_completeness_core.c
slipgate/sg_bsp_completeness_region.c
slipgate/sg_bsp_completeness_traversal.c
slipgate/sg_bsp_completeness_lattice.c
slipgate/sg_bsp_completeness_coverage.c
slipgate/sg_bsp_completeness_state.c
slipgate/sg_bsp_completeness_portal.c
slipgate/sg_bsp_completeness_portal_index.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict $isl_cflags -I. $sources -lm $isl_libs \
		-o "$tmp_dir/phase-catalog-$cc"
	"$tmp_dir/phase-catalog-$cc"
done

for cc in gcc clang
do
	$cc $strict -DSG_PHASE_CATALOG_TEST_TRANSITION_LIMIT=4 \
		$isl_cflags -I. $sources -lm $isl_libs \
		-o "$tmp_dir/phase-catalog-hostile-$cc"
	"$tmp_dir/phase-catalog-hostile-$cc"
done

for cc in gcc clang
do
	$cc $strict $isl_cflags -I. $model_sources -lm $isl_libs \
		-o "$tmp_dir/phase-catalog-model-$cc"
	"$tmp_dir/phase-catalog-model-$cc"
done

for cc in gcc clang
do
	$cc $strict $isl_cflags -I. $producer_sources -lm $isl_libs \
		-o "$tmp_dir/phase-catalog-producer-$cc"
	"$tmp_dir/phase-catalog-producer-$cc"
done

clang $strict $isl_cflags -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $producer_sources -lm $isl_libs \
	-o "$tmp_dir/phase-catalog-producer-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/phase-catalog-producer-sanitize"

clang $strict $isl_cflags -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources -lm $isl_libs \
	-o "$tmp_dir/phase-catalog-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/phase-catalog-sanitize"

clang $strict $isl_cflags -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $model_sources -lm $isl_libs \
	-o "$tmp_dir/phase-catalog-model-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/phase-catalog-model-sanitize"

for source in slipgate/sg_phase_catalog.c slipgate/sg_phase_catalog_audit.c \
	slipgate/sg_phase_catalog_publication.c \
	slipgate/sg_phase_catalog_owner.c \
	slipgate/sg_phase_mover_support_provider.c \
	slipgate/sg_mechanism_capability_seal.c tests/sg_phase_catalog_test.c
do
	clang --analyze $strict -I. "$source" \
		-o "$tmp_dir/$(basename "$source").plist"
done

clang --analyze $strict $isl_cflags -I. \
	tests/sg_phase_catalog_mechanism_integration_test.c \
	-o "$tmp_dir/sg_phase_catalog_mechanism_integration_test.c.plist"

clang --analyze $strict $isl_cflags -I. \
	tests/sg_phase_catalog_model_integration_test.c \
	-o "$tmp_dir/sg_phase_catalog_model_integration_test.c.plist"
