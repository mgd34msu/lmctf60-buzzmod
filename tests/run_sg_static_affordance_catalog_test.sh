#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion
-Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
-Wformat=2 -Wcast-qual -Wcast-align -Wno-unused-function'
# The shared weapon fixture is header-only and this focused test intentionally
# exercises only its construction path; the suppression applies to its unused
# fixture helpers, not to the catalog implementation.
modules='slipgate/sg_static_affordance_catalog.c
slipgate/sg_weapon_static_affordance.c
slipgate/sg_static_visibility_publication.c
slipgate/sg_weapon_effect_profile.c
slipgate/sg_static_visibility.c
slipgate/sg_configuration_semantics.c
slipgate/sg_configuration_lattice.c
slipgate/sg_configuration_space.c
slipgate/sg_rune_compact_spatial_index.c
slipgate/sg_configuration_audit.c
slipgate/sg_host_collision.c
slipgate/sg_bsp_world.c
slipgate/sg_rune_v2_artifact_loader.c
slipgate/sg_rune_v2_codec.c
slipgate/sg_rune_model.c
slipgate/sg_hook_visibility_catalog.c
slipgate/sg_hook_visibility_feasibility.c
slipgate/sg_hook_visibility_feasibility_family.c
slipgate/sg_hook_visibility_feasibility_events.c
slipgate/sg_hook_visibility_feasibility_partition.c
slipgate/sg_hook_visibility_feasibility_construct.c
slipgate/sg_hook_visibility_feasibility_verifier_digest.c
slipgate/sg_hook_visibility_feasibility_audit.c
slipgate/sg_hook_visibility_feasibility_audit_family.c
slipgate/sg_hook_visibility_feasibility_audit_events.c
slipgate/sg_hook_visibility_feasibility_audit_tiling.c'
support='tests/sg_hook_visibility_feasibility_fixture.c
tests/sg_hook_visibility_host_angle_reference.c'
test_main='tests/sg_static_affordance_catalog_test.c'
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)

cd "$repo_dir"
for cc in gcc clang
do
	$cc -std=gnu11 -I. -c q_shared.c -o "$tmp_dir/q-shared-$cc.o"
	$cc $strict $isl_cflags -I. "$test_main" $support $modules \
		"$tmp_dir/q-shared-$cc.o" -lm $isl_libs \
		-o "$tmp_dir/static-affordance-catalog-$cc"
	"$tmp_dir/static-affordance-catalog-$cc"
done

sanitize='-fno-omit-frame-pointer -fsanitize=address,undefined'
clang -std=gnu11 $sanitize -I. -c q_shared.c \
	-o "$tmp_dir/q-shared-sanitize.o"
clang $strict $sanitize $isl_cflags -I. "$test_main" $support $modules \
	"$tmp_dir/q-shared-sanitize.o" -lm $isl_libs \
	-o "$tmp_dir/static-affordance-catalog-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/static-affordance-catalog-sanitize"

for source in slipgate/sg_static_affordance_catalog.c \
	slipgate/sg_weapon_static_affordance.c \
	tests/sg_static_affordance_catalog_test.c
do
	clang --analyze $strict $isl_cflags -I. "$source" \
		-o "$tmp_dir/$(basename "$source").plist"
done
