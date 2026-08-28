#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_mechanism_capability_test.c slipgate/sg_mechanism_capability.c slipgate/sg_rune_model.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. $sources -lm -o "$tmp_dir/mechanism-capability-$cc"
	"$tmp_dir/mechanism-capability-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/mechanism-capability-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/mechanism-capability-sanitize"

for source in slipgate/sg_mechanism_capability.c \
	tests/sg_mechanism_capability_test.c
do
	clang --analyze $strict -I. "$source" \
		-o "$tmp_dir/$(basename "$source").plist"
done
