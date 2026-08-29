#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
owned='tests/sg_host_collision_test.c slipgate/sg_host_collision.c slipgate/sg_host_pmove.c slipgate/sg_host_engine_pmove.c slipgate/sg_bsp_world.c slipgate/sg_rune_model.c'

cd "$repo_dir"

build_real_oracle()
{
	cc=$1
	suffix=$2
	extra=$3

	$cc $strict $extra -I. -DSG_HOST_REAL_PMOVE_TEST -DDEDICATED_ONLY \
		-c tests/sg_host_collision_test.c -o "$tmp_dir/test-$suffix.o"
	$cc $strict $extra -I. -c slipgate/sg_host_collision.c \
		-o "$tmp_dir/collision-$suffix.o"
	$cc $strict $extra -I. -c slipgate/sg_host_pmove.c \
		-o "$tmp_dir/adapter-$suffix.o"
	$cc $strict $extra -I. -c slipgate/sg_host_engine_pmove.c \
		-o "$tmp_dir/engine-pmove-$suffix.o"
	$cc $strict $extra -I. -c slipgate/sg_bsp_world.c \
		-o "$tmp_dir/bsp-$suffix.o"
	$cc $strict $extra -I. -c slipgate/sg_rune_model.c \
		-o "$tmp_dir/model-$suffix.o"
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Wno-strict-prototypes \
		$extra -I. -DDEDICATED_ONLY -c tests/support/yq2_pmove.c \
		-o "$tmp_dir/yq2-$suffix.o"
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Wno-strict-prototypes \
		$extra -I. -c q_shared.c -o "$tmp_dir/shared-$suffix.o"
	$cc $extra "$tmp_dir/test-$suffix.o" "$tmp_dir/collision-$suffix.o" \
		"$tmp_dir/adapter-$suffix.o" "$tmp_dir/engine-pmove-$suffix.o" \
		"$tmp_dir/bsp-$suffix.o" \
		"$tmp_dir/model-$suffix.o" "$tmp_dir/yq2-$suffix.o" \
		"$tmp_dir/shared-$suffix.o" -lm -o "$tmp_dir/real-$suffix"
}

for cc in gcc clang
do
	$cc $strict -I. $owned -lm -o "$tmp_dir/host-$cc"
	"$tmp_dir/host-$cc"
	build_real_oracle "$cc" "$cc" ''
	"$tmp_dir/real-$cc"
done

sanitize='-fno-omit-frame-pointer -fsanitize=address,undefined'
build_real_oracle clang sanitize "$sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/real-sanitize"
