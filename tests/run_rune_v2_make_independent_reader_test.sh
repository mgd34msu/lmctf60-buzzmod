#!/bin/sh
set -eu

cc=${1:?compiler is required}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

$cc $strict -I. tools/runev2makecheck.c \
	slipgate/sg_rune_v2_exact_snapshot.c slipgate/sg_rune_v2_content_identity.c \
	-o "$tmp/make-reader"
$cc $strict -DRV2MAKE_TEST_SKIP_HEADER_RESERVED -I. tools/runev2makecheck.c \
	slipgate/sg_rune_v2_exact_snapshot.c slipgate/sg_rune_v2_content_identity.c \
	-o "$tmp/make-reader-corrupt"
$cc $strict -Wcast-align -I. tests/sg_rune_v2_codec_probe.c \
	slipgate/sg_rune_v2_artifact_loader.c slipgate/sg_rune_v2_codec.c \
	slipgate/sg_rune_model.c -lm -o "$tmp/probe"
$cc $strict -Wcast-align -I. tests/sg_rune_v2_fixture_writer.c \
	slipgate/sg_rune_v2_codec.c slipgate/sg_rune_model.c -lm -o "$tmp/writer"
RUNE_V2_MAKE_C_READER="$tmp/make-reader" \
RUNE_V2_MAKE_C_READER_CORRUPT="$tmp/make-reader-corrupt" \
RUNE_V2_CODEC_PROBE="$tmp/probe" RUNE_V2_FIXTURE_WRITER="$tmp/writer" \
	python3 -B tests/test_rune_v2_make_independent_reader.py

clang $strict -I. tools/runev2makecheck.c \
	slipgate/sg_rune_v2_exact_snapshot.c slipgate/sg_rune_v2_content_identity.c \
	-o "$tmp/make-reader-clang"
clang $strict -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	tools/runev2makecheck.c slipgate/sg_rune_v2_exact_snapshot.c \
	slipgate/sg_rune_v2_content_identity.c -o "$tmp/make-reader-sanitized"
clang $strict -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	-DRV2MAKE_TEST_SKIP_HEADER_RESERVED tools/runev2makecheck.c \
	slipgate/sg_rune_v2_exact_snapshot.c slipgate/sg_rune_v2_content_identity.c \
	-o "$tmp/make-reader-corrupt-sanitized"
clang --analyze $strict -I. tools/runev2makecheck.c \
	-o "$tmp/make-reader-analyzer.plist"
ASAN_OPTIONS='detect_leaks=1:halt_on_error=1' \
UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' \
RUNE_V2_MAKE_C_READER="$tmp/make-reader-sanitized" \
RUNE_V2_MAKE_C_READER_CORRUPT="$tmp/make-reader-corrupt-sanitized" \
RUNE_V2_CODEC_PROBE="$tmp/probe" RUNE_V2_FIXTURE_WRITER="$tmp/writer" \
	python3 -B tests/test_rune_v2_make_independent_reader.py
