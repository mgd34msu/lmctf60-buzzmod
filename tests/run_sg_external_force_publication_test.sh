#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion
-Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
-Wformat=2 -Wcast-qual -Wcast-align -DSG_PHASE_CATALOG_TESTING'
sources='tests/sg_external_force_publication_test.c
slipgate/sg_external_force_builder.c
slipgate/sg_external_force_publication.c
slipgate/sg_bsp_entity_semantics_publication.c
slipgate/sg_bsp_entity_semantics_audit_expected.c
slipgate/sg_bsp_entity_semantics.c
slipgate/sg_authority_entropy.c
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
slipgate/sg_rune_compact_spatial_index.c
slipgate/sg_configuration_audit.c
slipgate/sg_host_collision.c
slipgate/sg_host_pmove.c
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
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)

cd "$repo_dir"
for cc in gcc clang
do
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wno-strict-prototypes -DDEDICATED_ONLY -I. \
		-c tests/support/yq2_pmove.c -o "$tmp_dir/yq2-$cc.o"
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror -I. \
		-c q_shared.c -o "$tmp_dir/shared-$cc.o"
	$cc $strict $isl_cflags -DDEDICATED_ONLY -I. $sources \
		"$tmp_dir/yq2-$cc.o" "$tmp_dir/shared-$cc.o" -lm $isl_libs \
		-o "$tmp_dir/external-force-$cc"
	"$tmp_dir/external-force-$cc"
done

sanitize='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined'
clang -std=c11 -Wall -Wextra -Wpedantic -Werror -Wno-strict-prototypes \
	$sanitize -DDEDICATED_ONLY -I. -c tests/support/yq2_pmove.c \
	-o "$tmp_dir/yq2-sanitize.o"
clang -std=c11 -Wall -Wextra -Wpedantic -Werror $sanitize -I. \
	-c q_shared.c -o "$tmp_dir/shared-sanitize.o"
clang $strict $sanitize $isl_cflags -DDEDICATED_ONLY -I. $sources \
	"$tmp_dir/yq2-sanitize.o" "$tmp_dir/shared-sanitize.o" -lm $isl_libs \
	-o "$tmp_dir/external-force-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/external-force-sanitize"

for source in slipgate/sg_external_force_builder.c \
	slipgate/sg_external_force_publication.c \
	tests/sg_external_force_publication_test.c
do
	clang --analyze $strict $isl_cflags -I. "$source" \
		-o "$tmp_dir/$(basename "$source").plist"
done

gcc $strict $isl_cflags -I. -fanalyzer \
	-c slipgate/sg_external_force_publication.c \
	-o "$tmp_dir/external-force-analyzer.o"
