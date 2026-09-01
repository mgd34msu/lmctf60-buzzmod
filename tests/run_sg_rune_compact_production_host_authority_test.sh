#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_rune_compact_production_test.c slipgate/sg_rune_compact_production.c slipgate/sg_rune_compact_portal_snapshot.c slipgate/sg_rune_compact_weapon_catalog.c slipgate/sg_weapon_effect_profile.c slipgate/sg_rune_model.c slipgate/sg_rune_source_authority.c slipgate/sg_crc32.c'
host_mode='-DSG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict $host_mode -I. -Islipgate $sources -o "$tmp_dir/production-host-$cc"
	"$tmp_dir/production-host-$cc"
done

clang $strict $host_mode -fno-omit-frame-pointer -fsanitize=address,undefined \
	-I. -Islipgate $sources -o "$tmp_dir/production-host-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/production-host-sanitize"
