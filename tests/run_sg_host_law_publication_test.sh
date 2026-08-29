#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion'
strict="$strict -Wsign-conversion -Wshadow -Wstrict-prototypes"
strict="$strict -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align"

cd "$repo_dir"

compile_host_law()
{
	cc=$1
	suffix=$2
	extra=$3

	for pair in \
		'tests/sg_host_law_publication_test.c test' \
		'slipgate/sg_host_law_publication.c publication' \
		'slipgate/sg_host_law_owner.c owner' \
		'slipgate/sg_host_engine_pmove.c engine-pmove' \
		'slipgate/sg_host_engine_runtime.c engine-runtime' \
		'slipgate/sg_host_engine_parity.c engine-parity' \
		'slipgate/sg_host_hook_law.c hook-law' \
		'slipgate/sg_host_mechanism_law.c mechanism-law' \
		'slipgate/sg_host_pmove.c host-pmove' \
		'slipgate/sg_host_collision.c collision' \
		'slipgate/sg_bsp_world.c bsp-world' \
		'slipgate/sg_rune_model.c rune-model'; do
		set -- $pair
		$cc $strict $extra -I. -DDEDICATED_ONLY -c "$1" \
			-o "$tmp_dir/$2-$suffix.o"
	done
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wno-strict-prototypes $extra -I. -DDEDICATED_ONLY \
		-c tests/support/yq2_pmove.c -o "$tmp_dir/yq2-pmove-$suffix.o"
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wno-strict-prototypes $extra -I. -c q_shared.c \
		-o "$tmp_dir/q-shared-$suffix.o"
	$cc $extra \
		"$tmp_dir/test-$suffix.o" \
		"$tmp_dir/publication-$suffix.o" \
		"$tmp_dir/owner-$suffix.o" \
		"$tmp_dir/engine-pmove-$suffix.o" \
		"$tmp_dir/engine-runtime-$suffix.o" \
		"$tmp_dir/engine-parity-$suffix.o" \
		"$tmp_dir/hook-law-$suffix.o" \
		"$tmp_dir/mechanism-law-$suffix.o" \
		"$tmp_dir/host-pmove-$suffix.o" \
		"$tmp_dir/collision-$suffix.o" \
		"$tmp_dir/bsp-world-$suffix.o" \
		"$tmp_dir/rune-model-$suffix.o" \
		"$tmp_dir/yq2-pmove-$suffix.o" \
		"$tmp_dir/q-shared-$suffix.o" -lm \
		-o "$tmp_dir/host-law-$suffix"
}

for cc in gcc clang
do
	compile_host_law "$cc" "$cc" ''
	"$tmp_dir/host-law-$cc"
done

sanitize='-fno-omit-frame-pointer -fsanitize=address,undefined'
compile_host_law clang sanitize "$sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/host-law-sanitize"

for source in \
	slipgate/sg_host_law_publication.c \
	slipgate/sg_host_law_owner.c \
	slipgate/sg_host_engine_pmove.c \
	slipgate/sg_host_engine_runtime.c \
	slipgate/sg_host_engine_parity.c \
	slipgate/sg_host_hook_law.c \
	slipgate/sg_host_mechanism_law.c \
	slipgate/sg_host_pmove.c \
	slipgate/sg_host_collision.c; do
	base=${source##*/}
	base=${base%.c}
	clang --analyze $strict -I. -DDEDICATED_ONLY "$source" \
		-o "$tmp_dir/$base.plist"
done
