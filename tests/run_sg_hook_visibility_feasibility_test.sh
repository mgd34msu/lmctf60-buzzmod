#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion
-Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
-Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_hook_visibility_feasibility_test.c
tests/sg_hook_visibility_feasibility_fixture.c
tests/sg_hook_visibility_host_angle_reference.c
slipgate/sg_hook_visibility_feasibility.c
slipgate/sg_hook_visibility_feasibility_partition.c
slipgate/sg_hook_visibility_feasibility_proof.c
slipgate/sg_hook_visibility_feasibility_audit.c
slipgate/sg_host_collision.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. $sources -lm -o "$tmp_dir/hook-visibility-$cc"
	"$tmp_dir/hook-visibility-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/hook-visibility-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/hook-visibility-sanitize"

for source in slipgate/sg_hook_visibility_feasibility.c \
	slipgate/sg_hook_visibility_feasibility_partition.c \
	slipgate/sg_hook_visibility_feasibility_proof.c \
	slipgate/sg_hook_visibility_feasibility_audit.c \
	tests/sg_hook_visibility_feasibility_fixture.c
do
	clang --analyze $strict -I. "$source" \
		-o "$tmp_dir/$(basename "$source").plist"
done
