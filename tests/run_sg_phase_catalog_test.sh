#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_phase_catalog_test.c
slipgate/sg_phase_catalog.c
slipgate/sg_phase_catalog_audit.c
slipgate/sg_phase_catalog_publication.c
slipgate/sg_rune_model.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. $sources -lm -o "$tmp_dir/phase-catalog-$cc"
	"$tmp_dir/phase-catalog-$cc"
done

clang $strict -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources -lm \
	-o "$tmp_dir/phase-catalog-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/phase-catalog-sanitize"

for source in slipgate/sg_phase_catalog.c slipgate/sg_phase_catalog_audit.c \
	slipgate/sg_phase_catalog_publication.c tests/sg_phase_catalog_test.c
do
	clang --analyze $strict -I. "$source" \
		-o "$tmp_dir/$(basename "$source").plist"
done
