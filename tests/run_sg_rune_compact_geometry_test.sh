#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

sources="$root/tests/sg_rune_compact_geometry_test.c \
$root/slipgate/sg_rune_compact_geometry.c \
$root/slipgate/sg_rune_compact_geometry_partition.c \
$root/slipgate/sg_rune_compact_localize.c"
flags="-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion \
-Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
-Wformat=2 -Wcast-qual -Wcast-align -I$root -I$root/slipgate"

cc $flags $sources -lm -o "$tmp/gcc-test"
"$tmp/gcc-test"

clang $flags $sources -lm -o "$tmp/clang-test"
"$tmp/clang-test"

cc $flags -fsanitize=address -fno-omit-frame-pointer $sources -lm \
	-o "$tmp/asan-test"
ASAN_OPTIONS=detect_leaks=1 "$tmp/asan-test"

cc $flags -fsanitize=undefined -fno-omit-frame-pointer $sources -lm \
	-o "$tmp/ubsan-test"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/ubsan-test"

model_sources="$root/tests/sg_rune_compact_geometry_model_test.c \
$root/slipgate/sg_rune_compact_geometry.c \
$root/slipgate/sg_rune_compact_geometry_partition.c \
$root/slipgate/sg_rune_compact_localize.c \
$root/slipgate/sg_rune_compact_model.c \
$root/slipgate/sg_rune_compact_analytic.c \
$root/slipgate/sg_rune_compact_static.c"
cc $flags $model_sources -lm -o "$tmp/model-test"
"$tmp/model-test"
clang $flags $model_sources -lm -o "$tmp/model-clang-test"
"$tmp/model-clang-test"
cc $flags -fsanitize=address -fno-omit-frame-pointer $model_sources -lm \
	-o "$tmp/model-asan-test"
ASAN_OPTIONS=detect_leaks=1 "$tmp/model-asan-test"
cc $flags -fsanitize=undefined -fno-omit-frame-pointer $model_sources -lm \
	-o "$tmp/model-ubsan-test"
UBSAN_OPTIONS=halt_on_error=1 "$tmp/model-ubsan-test"

scan-build --status-bugs -o "$tmp/scan-build" clang $flags $model_sources \
	-lm -o "$tmp/model-scan-build-test"
