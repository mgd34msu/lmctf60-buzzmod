#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion
-Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
-Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_rune_v2_content_identity_test.c
slipgate/sg_rune_v2_content_identity.c
slipgate/sg_rune_v2_exact_snapshot.c'

cd "$repo_dir"

for cc in gcc clang
do
	$cc $strict -I. $sources -o "$tmp_dir/exact-snapshot-$cc"
	"$tmp_dir/exact-snapshot-$cc"
	$cc $strict -I. tests/sg_rune_v2_content_identity_probe.c \
		slipgate/sg_rune_v2_content_identity.c \
		-o "$tmp_dir/content-identity-probe-$cc"
	python3 -B tests/test_sg_rune_v2_content_identity.py \
		"$tmp_dir/content-identity-probe-$cc"
done

clang $strict -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources \
	-o "$tmp_dir/exact-snapshot-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/exact-snapshot-sanitize"

clang --analyze $strict -I. slipgate/sg_rune_v2_content_identity.c \
	-o "$tmp_dir/content-identity-analyzer.plist"
clang --analyze $strict -I. slipgate/sg_rune_v2_exact_snapshot.c \
	-o "$tmp_dir/exact-snapshot-analyzer.plist"
clang --analyze $strict -I. tests/sg_rune_v2_content_identity_test.c \
	-o "$tmp_dir/exact-snapshot-test-analyzer.plist"
clang --analyze $strict -I. tests/sg_rune_v2_content_identity_probe.c \
	-o "$tmp_dir/content-identity-probe-analyzer.plist"

if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1
then
	x86_64-w64-mingw32-gcc $strict -I. -c \
		slipgate/sg_rune_v2_content_identity.c \
		-o "$tmp_dir/content-identity-mingw.o"
	x86_64-w64-mingw32-gcc $strict -I. -c \
		slipgate/sg_rune_v2_exact_snapshot.c \
		-o "$tmp_dir/exact-snapshot-mingw.o"
fi
