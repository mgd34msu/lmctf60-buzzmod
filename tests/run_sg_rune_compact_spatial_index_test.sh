#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion'
strict="$strict -Wsign-conversion -Wshadow -Wstrict-prototypes"
strict="$strict -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align"
sources='tests/sg_rune_compact_spatial_index_test.c slipgate/sg_rune_compact_spatial_index.c slipgate/sg_bsp_world.c'
lmctf22=${LMCTF22_BSP:-/home/buzzkill/Projects/qfiles/q2/ctf/maps/lmctf22.bsp}
lmctf58=${LMCTF58_BSP:-/home/buzzkill/Projects/qfiles/q2/lmctf/maps/lmctf58.bsp}
lmctf76=${LMCTF76_BSP:-/home/buzzkill/Projects/qfiles/q2/lmctf/maps/lmctf76.bsp}

run_suite()
{
	spatial_binary=$1
	shift
	set --
	if [ -f "$lmctf22" ]
	then
		set -- "$@" "$lmctf22"
	fi
	if [ -f "$lmctf58" ]
	then
		set -- "$@" "$lmctf58"
	fi
	if [ -f "$lmctf76" ]
	then
		set -- "$@" "$lmctf76"
	fi
	"$spatial_binary" "$@"
}

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. $sources -lm -o "$tmp_dir/compact-spatial-$cc"
	run_suite "$tmp_dir/compact-spatial-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=address -I. $sources -lm \
	-o "$tmp_dir/compact-spatial-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	run_suite "$tmp_dir/compact-spatial-asan"

clang $strict -fno-omit-frame-pointer -fsanitize=undefined -I. $sources -lm \
	-o "$tmp_dir/compact-spatial-ubsan"
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	run_suite "$tmp_dir/compact-spatial-ubsan"
