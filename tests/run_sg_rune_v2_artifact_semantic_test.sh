#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

warnings='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
strict="-DSG_RUNE_V2_SEMANTIC_TESTING $warnings"
sources='tests/sg_rune_v2_artifact_semantic_test.c slipgate/sg_rune_v2_artifact_semantic.c slipgate/sg_rune_v2_codec.c slipgate/sg_rune_model.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $warnings -I. -c slipgate/sg_rune_v2_artifact_semantic.c \
		-o "$tmp_dir/artifact-semantic-production-$cc.o"
	$cc $strict -I. $sources -lm -o "$tmp_dir/artifact-semantic-$cc"
	"$tmp_dir/artifact-semantic-$cc"
done

clang $strict -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources -lm \
	-o "$tmp_dir/artifact-semantic-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/artifact-semantic-sanitize"

clang --analyze $warnings -I. slipgate/sg_rune_v2_artifact_semantic.c \
	-o "$tmp_dir/artifact-semantic-analyzer.plist"
clang --analyze $strict -I. tests/sg_rune_v2_artifact_semantic_test.c \
	-o "$tmp_dir/artifact-semantic-test-analyzer.plist"
