#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

flags="-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion \
-Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
-Wformat=2 -Wcast-qual -Wcast-align -I$root -I$root/slipgate"
sources="$root/tests/sg_rune_compact_response_partition_test.c \
$root/slipgate/sg_rune_compact_response_partition.c \
$root/slipgate/sg_rune_compact_source_surface_catalog.c \
$root/slipgate/sg_rune_compact_geometry.c \
$root/slipgate/sg_rune_compact_geometry_partition.c"

gcc $flags $sources -lm -o "$tmp/response-gcc"
clang $flags $sources -lm -o "$tmp/response-clang"

clang $flags -fsanitize=address -fno-omit-frame-pointer $sources -lm \
	-o "$tmp/response-asan"

clang $flags -fsanitize=undefined -fno-omit-frame-pointer $sources -lm \
	-o "$tmp/response-ubsan"

"$tmp/response-gcc" &
gcc_pid=$!
"$tmp/response-clang" &
clang_pid=$!
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$tmp/response-asan" &
asan_pid=$!
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$tmp/response-ubsan" &
ubsan_pid=$!

scan-build --status-bugs -o "$tmp/scan-build" clang $flags $sources -lm \
	-o "$tmp/response-scan-build"

status=0
wait "$gcc_pid" || status=1
wait "$clang_pid" || status=1
wait "$asan_pid" || status=1
wait "$ubsan_pid" || status=1
test "$status" -eq 0
