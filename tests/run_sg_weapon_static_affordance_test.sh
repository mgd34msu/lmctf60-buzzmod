#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_weapon_static_affordance_test.c slipgate/sg_weapon_static_affordance.c slipgate/sg_static_visibility_publication.c slipgate/sg_weapon_effect_profile.c slipgate/sg_static_visibility.c slipgate/sg_configuration_semantics.c slipgate/sg_configuration_lattice.c slipgate/sg_configuration_space.c slipgate/sg_configuration_audit.c slipgate/sg_host_collision.c slipgate/sg_bsp_world.c slipgate/sg_rune_v2_artifact_loader.c slipgate/sg_rune_v2_codec.c slipgate/sg_rune_model.c'
wrap_alloc='-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc'
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict $isl_cflags -DSG_STATIC_VISIBILITY_WRAP_ALLOC -I. \
		$sources -lm $isl_libs $wrap_alloc \
		-o "$tmp_dir/weapon-static-affordance-$cc"
	"$tmp_dir/weapon-static-affordance-$cc"
done

clang $strict $isl_cflags -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources -lm $isl_libs \
	-o "$tmp_dir/weapon-static-affordance-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/weapon-static-affordance-sanitize"

clang --analyze $strict $isl_cflags -I. \
	slipgate/sg_weapon_static_affordance.c \
	-o "$tmp_dir/weapon-static-affordance.plist"
clang --analyze $strict $isl_cflags -I. \
	slipgate/sg_static_visibility_publication.c \
	-o "$tmp_dir/static-visibility-publication.plist"
clang --analyze $strict $isl_cflags -DSG_STATIC_VISIBILITY_WRAP_ALLOC -I. \
	tests/sg_weapon_static_affordance_test.c \
	-o "$tmp_dir/weapon-static-affordance-test.plist"
