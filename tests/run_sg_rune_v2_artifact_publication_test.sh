#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion
-Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wcast-align'
sources='tests/sg_rune_v2_artifact_publication_test.c
slipgate/sg_rune_v2_artifact_publication.c
slipgate/sg_rune_v2_artifact_publication_manifest.c
slipgate/sg_rune_v2_artifact_publication_io.c'

cd "$repo_dir"

gcc $strict -I. $sources -o "$tmp_dir/publication-gcc"
"$tmp_dir/publication-gcc"

clang $strict -I. $sources -o "$tmp_dir/publication-clang"
"$tmp_dir/publication-clang"

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -o "$tmp_dir/publication-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/publication-sanitize"
