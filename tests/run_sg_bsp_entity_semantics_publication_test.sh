#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_bsp_entity_semantics_publication_test.c
slipgate/sg_bsp_entity_semantics_publication.c
slipgate/sg_bsp_entity_semantics_audit_expected.c
slipgate/sg_bsp_entity_semantics.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. $sources -lm -o "$tmp_dir/entity-publication-$cc"
	"$tmp_dir/entity-publication-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined \
	-I. $sources -lm -o "$tmp_dir/entity-publication-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/entity-publication-sanitize"

for source in $sources
do
	clang --analyze -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes \
		-Wmissing-prototypes -I. "$source" \
		-o "$tmp_dir/$(basename "$source").plist"
done
