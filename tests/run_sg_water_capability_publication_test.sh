#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-DDEDICATED_ONLY -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion'
strict="$strict -Wsign-conversion -Wshadow -Wstrict-prototypes"
strict="$strict -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align"
strict="$strict -DSG_HOST_LAW_TESTING"
strict="$strict -DSG_WATER_CAPABILITY_PUBLICATION_TESTING"
strict="$strict -ffunction-sections -fdata-sections"
sources='tests/sg_water_capability_publication_test.c
tests/sg_water_capability_fixture.c
slipgate/sg_water_capability_publication.c
slipgate/sg_water_capability.c
slipgate/sg_configuration_lattice.c
slipgate/sg_phase_catalog.c
slipgate/sg_phase_catalog_audit.c
slipgate/sg_phase_catalog_publication.c
slipgate/sg_authority_entropy.c
slipgate/sg_host_law_publication.c
slipgate/sg_host_hook_law.c
slipgate/sg_host_mechanism_law.c
slipgate/sg_host_pmove.c
slipgate/sg_host_collision.c
slipgate/sg_bsp_world.c
slipgate/sg_rune_model.c'
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)
wraps='-Wl,--wrap=SG_HostLawConstructionCurrent'
wraps="$wraps -Wl,--wrap=SG_HostLawConstructionRead"
wraps="$wraps -Wl,--wrap=SG_HostLawConstructionClassifyPose"
wraps="$wraps -Wl,--wrap=SG_HostLawConstructionTransition"
wraps="$wraps -Wl,--wrap=SG_HostLawConstructionPmove"
wraps="$wraps -Wl,--wrap=SG_HostLawConstructionCompletenessProve"
wraps="$wraps -Wl,--wrap=SG_HostLawConstructionConfigurationAudit"
wraps="$wraps -Wl,--wrap=SG_HostLawConstructionSemanticsAudit"

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict $isl_cflags -I. $sources $wraps -Wl,--gc-sections -lm $isl_libs \
		-o "$tmp_dir/water-capability-publication-$cc"
	"$tmp_dir/water-capability-publication-$cc"
done

clang $strict $isl_cflags -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources -lm $isl_libs \
	$wraps -Wl,--gc-sections -o "$tmp_dir/water-capability-publication-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/water-capability-publication-sanitize"

for source in slipgate/sg_water_capability_publication.c \
	tests/sg_water_capability_publication_test.c \
	tests/sg_water_capability_fixture.c
do
	clang --analyze $strict $isl_cflags -I. "$source" \
		-o "$tmp_dir/$(basename "$source").plist"
done
