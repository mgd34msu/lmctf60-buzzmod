#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
allocation_test='-DSG_RUNE_COMPACT_MOVEMENT_FIELDS_TESTING -DSG_RUNE_COMPACT_MOVEMENT_FIELDS_TEST_WRAP_CALLOC -Wl,--wrap=calloc'
sources='tests/sg_rune_compact_movement_fields_test.c slipgate/sg_rune_compact_movement_fields.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_eval.c slipgate/sg_host_hook_law.c slipgate/sg_host_mechanism_law.c'
owner_defines='-DSG_HOST_LAW_TESTING -DSG_COMPACT_BUILDER_TEST_HOOKS -DSG_COMPACT_BUILDER_REAL_WEAPONS -DSG_COMPACT_BUILDER_REAL_BSP -DSG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY -DSG_COMPACT_BUILDER_REAL_ENTITY_SEMANTICS'
owner_sources='tests/sg_rune_compact_movement_owner_integration_test.c slipgate/sg_rune_compact_builder.c slipgate/sg_rune_compact_model.c slipgate/sg_rune_compact_weapon_catalog.c slipgate/sg_rune_compact_weapon_relations.c slipgate/sg_rune_compact_weapon_field.c slipgate/sg_rune_compact_composer.c slipgate/sg_rune_compact_wire.c slipgate/sg_weapon_effect_profile.c slipgate/sg_rune_model.c slipgate/sg_bsp_world.c slipgate/sg_bsp_entity_semantics.c slipgate/sg_rune_source_authority.c slipgate/sg_crc32.c slipgate/sg_configuration_lattice.c slipgate/sg_rune_compact_geometry.c slipgate/sg_rune_compact_geometry_partition.c slipgate/sg_rune_compact_source_surface_catalog.c slipgate/sg_rune_compact_response_partition.c slipgate/sg_rune_compact_mechanisms.c slipgate/sg_rune_compact_mechanisms_build.c slipgate/sg_rune_compact_mechanisms_transitions.c slipgate/sg_rune_compact_mechanisms_entities.c slipgate/sg_rune_compact_static_materializer.c slipgate/sg_rune_compact_static.c slipgate/sg_rune_compact_movement_fields.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_eval.c slipgate/sg_host_law_publication.c slipgate/sg_host_engine_pmove.c slipgate/sg_host_pmove.c slipgate/sg_host_collision.c slipgate/sg_host_hook_law.c slipgate/sg_host_mechanism_law.c'
owner_sections='-ffunction-sections -fdata-sections'
owner_gc='-Wl,--gc-sections'
visibility_renames='-DSG_StaticVisibilityDefaultLimits=SG_RealStaticVisibilityDefaultLimits -DSG_StaticVisibilityBuild=SG_RealStaticVisibilityBuild -DSG_StaticVisibilityAudit=SG_RealStaticVisibilityAudit -DSG_StaticVisibilityDestroy=SG_RealStaticVisibilityDestroy'
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)

cd "$repo_dir"

# Execute the production owner chain.  The harness supplies only the minimal
# engine/configuration fixture at the outer boundary; every compact owner and
# read/currentness seam from builder through movement is the real module.
for cc in gcc clang
do
	owner_binary="$tmp_dir/rune-compact-movement-owner-integration-$cc"
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wno-strict-prototypes -DDEDICATED_ONLY -I. \
		-c tests/support/yq2_pmove.c -o "$tmp_dir/yq2-$cc.o"
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wno-strict-prototypes -I. -c q_shared.c \
		-o "$tmp_dir/q-shared-$cc.o"
	$cc $strict -Wno-unused-function -I. \
		-c slipgate/sg_rune_compact_localize.c \
		-o "$tmp_dir/localize-$cc.o"
	$cc $strict $visibility_renames -I. \
		-c slipgate/sg_static_visibility.c \
		-o "$tmp_dir/static-visibility-$cc.o"
	$cc $strict $owner_sections $owner_defines -DDEDICATED_ONLY $isl_cflags -I. \
		$owner_sources "$tmp_dir/yq2-$cc.o" "$tmp_dir/q-shared-$cc.o" \
		"$tmp_dir/localize-$cc.o" "$tmp_dir/static-visibility-$cc.o" -lm \
		$isl_libs $owner_gc -o "$owner_binary"
	"$owner_binary"
done

gcc $strict $allocation_test -I. $sources -lm \
	-o "$tmp_dir/rune-compact-movement-fields-gcc"
"$tmp_dir/rune-compact-movement-fields-gcc"

clang $strict $allocation_test -I. $sources -lm \
	-o "$tmp_dir/rune-compact-movement-fields-clang"
"$tmp_dir/rune-compact-movement-fields-clang"

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=address \
	-I. $sources -lm -o "$tmp_dir/rune-compact-movement-fields-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	"$tmp_dir/rune-compact-movement-fields-asan"

clang $strict $allocation_test -fno-omit-frame-pointer -fsanitize=undefined \
	-I. $sources -lm -o "$tmp_dir/rune-compact-movement-fields-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/rune-compact-movement-fields-ubsan"

clang -std=c11 -Wall -Wextra -Wpedantic -Werror -Wno-strict-prototypes \
	-DDEDICATED_ONLY -fno-omit-frame-pointer -fsanitize=address -I. \
	-c tests/support/yq2_pmove.c -o "$tmp_dir/yq2-asan.o"
clang -std=c11 -Wall -Wextra -Wpedantic -Werror -Wno-strict-prototypes \
	-fno-omit-frame-pointer -fsanitize=address -I. -c q_shared.c \
	-o "$tmp_dir/q-shared-asan.o"
clang $strict -Wno-unused-function -fno-omit-frame-pointer \
	-fsanitize=address -I. -c slipgate/sg_rune_compact_localize.c \
	-o "$tmp_dir/localize-asan.o"
clang $strict $visibility_renames -fno-omit-frame-pointer \
	-fsanitize=address -I. -c slipgate/sg_static_visibility.c \
	-o "$tmp_dir/static-visibility-asan.o"
clang $strict $owner_sections $owner_defines -DDEDICATED_ONLY $isl_cflags \
	-fno-omit-frame-pointer -fsanitize=address -I. $owner_sources \
	"$tmp_dir/yq2-asan.o" "$tmp_dir/q-shared-asan.o" \
	"$tmp_dir/localize-asan.o" "$tmp_dir/static-visibility-asan.o" \
	-lm $isl_libs $owner_gc \
	-o "$tmp_dir/rune-compact-movement-owner-integration-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	"$tmp_dir/rune-compact-movement-owner-integration-asan"

clang -std=c11 -Wall -Wextra -Wpedantic -Werror -Wno-strict-prototypes \
	-DDEDICATED_ONLY -fno-omit-frame-pointer -fsanitize=undefined -I. \
	-c tests/support/yq2_pmove.c -o "$tmp_dir/yq2-ubsan.o"
clang -std=c11 -Wall -Wextra -Wpedantic -Werror -Wno-strict-prototypes \
	-fno-omit-frame-pointer -fsanitize=undefined -I. -c q_shared.c \
	-o "$tmp_dir/q-shared-ubsan.o"
clang $strict -Wno-unused-function -fno-omit-frame-pointer \
	-fsanitize=undefined -I. -c slipgate/sg_rune_compact_localize.c \
	-o "$tmp_dir/localize-ubsan.o"
clang $strict $visibility_renames -fno-omit-frame-pointer \
	-fsanitize=undefined -I. -c slipgate/sg_static_visibility.c \
	-o "$tmp_dir/static-visibility-ubsan.o"
clang $strict $owner_sections $owner_defines -DDEDICATED_ONLY $isl_cflags \
	-fno-omit-frame-pointer -fsanitize=undefined -I. $owner_sources \
	"$tmp_dir/yq2-ubsan.o" "$tmp_dir/q-shared-ubsan.o" \
	"$tmp_dir/localize-ubsan.o" "$tmp_dir/static-visibility-ubsan.o" \
	-lm $isl_libs $owner_gc \
	-o "$tmp_dir/rune-compact-movement-owner-integration-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/rune-compact-movement-owner-integration-ubsan"

scan-build --status-bugs -o "$tmp_dir/analyzer" clang $strict \
	$allocation_test -I. $sources -lm \
	-o "$tmp_dir/rune-compact-movement-fields-analyzer"
test -x "$tmp_dir/rune-compact-movement-fields-analyzer"
scan-build --status-bugs -o "$tmp_dir/owner-analyzer" clang $strict \
	$owner_sections $owner_defines -DDEDICATED_ONLY $isl_cflags -I. $owner_sources \
	"$tmp_dir/yq2-clang.o" "$tmp_dir/q-shared-clang.o" \
	"$tmp_dir/localize-clang.o" "$tmp_dir/static-visibility-clang.o" \
	-lm $isl_libs \
	$owner_gc -o "$tmp_dir/rune-compact-movement-owner-integration-analyzer"
test -x "$tmp_dir/rune-compact-movement-owner-integration-analyzer"
