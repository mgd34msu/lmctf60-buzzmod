#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'

cd "$repo_dir"

for cc in gcc clang
do
	$cc $strict -I. tests/sg_rune_compact_pmove_control_test.c \
		slipgate/sg_rune_compact_pmove_control_build.c \
		slipgate/sg_rune_compact_pmove_control.c \
		slipgate/sg_rune_compact_pmove_control_wire.c -lm \
		-o "$tmp_dir/pmove-control-$cc"
	"$tmp_dir/pmove-control-$cc"
done

sanitize='-fno-omit-frame-pointer -fsanitize=address,undefined'
clang $strict $sanitize -I. tests/sg_rune_compact_pmove_control_test.c \
	slipgate/sg_rune_compact_pmove_control_build.c \
	slipgate/sg_rune_compact_pmove_control.c \
	slipgate/sg_rune_compact_pmove_control_wire.c -lm \
	-o "$tmp_dir/pmove-control-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/pmove-control-sanitize"

build_real_owner()
{
	cc=$1
	suffix=$2
	extra=$3
	objects=''

	for pair in \
		'tests/sg_tactic_pmove_control_runtime_test.c test' \
		'slipgate/sg_rune_compact_pmove_control_build.c control-build' \
		'slipgate/sg_rune_compact_pmove_control.c control' \
		'slipgate/sg_tactic_pmove_control_runtime.c runtime' \
		'slipgate/sg_host_engine_runtime.c engine-runtime' \
		'slipgate/sg_host_engine_pmove.c engine-pmove' \
		'slipgate/sg_host_pmove.c host-pmove' \
		'slipgate/sg_host_collision.c collision' \
		'slipgate/sg_bsp_world.c bsp-world' \
		'slipgate/sg_rune_model.c rune-model' \
		'slipgate/sg_client_ownership.c client-ownership'
	do
		set -- $pair
		$cc $strict $extra -I. -DDEDICATED_ONLY -c "$1" \
			-o "$tmp_dir/$2-$suffix.o"
		objects="$objects $tmp_dir/$2-$suffix.o"
	done
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wno-strict-prototypes $extra -I. -DDEDICATED_ONLY \
		-c tests/support/yq2_pmove.c -o "$tmp_dir/yq2-$suffix.o"
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wno-strict-prototypes $extra -I. -c q_shared.c \
		-o "$tmp_dir/shared-$suffix.o"
	$cc $extra $objects "$tmp_dir/yq2-$suffix.o" \
		"$tmp_dir/shared-$suffix.o" -lm -o "$tmp_dir/real-owner-$suffix"
}

for cc in gcc clang
do
	build_real_owner "$cc" "$cc" ''
	"$tmp_dir/real-owner-$cc"
done

build_real_owner clang sanitize "$sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/real-owner-sanitize"

if rg -n 'usercmd_t|sg_rune_pmove_control_command|replay_live_fn' \
	slipgate/sg_rune_compact_pmove_control.h \
	slipgate/sg_rune_compact_pmove_control.c \
	slipgate/sg_rune_compact_pmove_control_wire.h \
	slipgate/sg_rune_compact_pmove_control_wire.c
then
	echo 'public RUNE PMove-control surface contains forbidden input bytes' >&2
	exit 1
fi
