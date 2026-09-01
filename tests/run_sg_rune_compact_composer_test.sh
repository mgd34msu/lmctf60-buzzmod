#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_rune_compact_composer_test.c slipgate/sg_rune_compact_composer.c slipgate/sg_rune_compact_model.c slipgate/sg_rune_compact_wire.c slipgate/sg_rune_compact_source_surface_catalog.c slipgate/sg_rune_compact_weapon_catalog.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_static.c slipgate/sg_rune_model.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. -Islipgate $sources -Wl,--wrap=calloc -lm \
		-o "$tmp_dir/composer-$cc"
	"$tmp_dir/composer-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=address -I. -Islipgate \
	$sources -Wl,--wrap=calloc -lm -o "$tmp_dir/composer-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$tmp_dir/composer-asan"

clang $strict -fno-omit-frame-pointer -fsanitize=undefined -I. -Islipgate \
	$sources -Wl,--wrap=calloc -lm -o "$tmp_dir/composer-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$tmp_dir/composer-ubsan"
