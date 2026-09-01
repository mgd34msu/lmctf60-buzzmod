#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 ASSET_DIR AUTHORITATIVE_IDENTITY_TSV" >&2
	exit 2
fi

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM
strict='-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_configuration_manifest_test.c slipgate/sg_configuration_lattice.c slipgate/sg_configuration_space.c slipgate/sg_configuration_audit.c slipgate/sg_host_collision.c slipgate/sg_bsp_world.c slipgate/sg_rune_model.c slipgate/sg_rune_compact_spatial_index.c'
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)

cd "$repo_dir"
gcc $strict $isl_cflags -I. $sources -lm $isl_libs -o "$tmp_dir/config-manifest"
"$tmp_dir/config-manifest" tools/rune-corpus-maps.txt "$1" "$2"
