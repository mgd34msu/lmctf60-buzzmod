#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 WATER_MAP.bsp" >&2
	exit 2
fi

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM
strict='-DDEDICATED_ONLY -std=c11 -Wall -Wextra -Wpedantic -Werror'
strict="$strict -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes"
strict="$strict -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align"

cd "$repo_dir"
cc $strict -I. -c tests/sg_water_real_bsp_test.c -o "$tmp_dir/test.o"
cc $strict -I. -c slipgate/sg_host_pmove.c -o "$tmp_dir/pmove.o"
cc $strict -I. -c slipgate/sg_host_collision.c -o "$tmp_dir/collision.o"
cc $strict -I. -c slipgate/sg_bsp_world.c -o "$tmp_dir/bsp.o"
cc $strict -I. -c slipgate/sg_rune_model.c -o "$tmp_dir/model.o"
cc -DDEDICATED_ONLY -std=c11 -Wall -Wextra -Wpedantic -Werror \
	-Wno-strict-prototypes -I. -c tests/support/yq2_pmove.c \
	-o "$tmp_dir/yq2-pmove.o"
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Wno-strict-prototypes \
	-I. -c q_shared.c -o "$tmp_dir/q-shared.o"
cc "$tmp_dir/test.o" "$tmp_dir/pmove.o" "$tmp_dir/collision.o" \
	"$tmp_dir/bsp.o" "$tmp_dir/model.o" "$tmp_dir/yq2-pmove.o" \
	"$tmp_dir/q-shared.o" -lm -o "$tmp_dir/water-real-bsp"
"$tmp_dir/water-real-bsp" "$1"
