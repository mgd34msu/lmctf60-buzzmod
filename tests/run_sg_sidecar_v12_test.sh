#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. tests/sg_sidecar_wire_test.c \
		slipgate/sg_sidecar_wire.c slipgate/sg_crc32.c \
		-o "$tmp_dir/sidecar-wire-$cc"
	"$tmp_dir/sidecar-wire-$cc"
	$cc $strict -I. tests/sg_sidecar_loader_test.c \
		slipgate/sg_sidecar_loader.c slipgate/sg_sidecar_wire.c \
		slipgate/sg_crc32.c -o "$tmp_dir/sidecar-loader-$cc"
	"$tmp_dir/sidecar-loader-$cc"
	$cc $strict -I. tests/sg_sidecar_store_test.c \
		slipgate/sg_sidecar_store.c slipgate/sg_sidecar_wire.c \
		slipgate/sg_crc32.c -o "$tmp_dir/sidecar-store-$cc"
	"$tmp_dir/sidecar-store-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	tests/sg_sidecar_loader_test.c slipgate/sg_sidecar_loader.c \
	slipgate/sg_sidecar_wire.c slipgate/sg_crc32.c \
	-o "$tmp_dir/sidecar-loader-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/sidecar-loader-sanitize"

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	tests/sg_sidecar_store_test.c slipgate/sg_sidecar_store.c \
	slipgate/sg_sidecar_wire.c slipgate/sg_crc32.c \
	-o "$tmp_dir/sidecar-store-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/sidecar-store-sanitize"
