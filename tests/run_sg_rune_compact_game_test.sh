#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
sources='tests/sg_rune_compact_game_test.c slipgate/sg_rune_compact_game_offline.c'
runtime_forbidden='SG_(Configuration|StaticVisibility|RuneCompact(Builder|Generation|Geometry|ResponsePartition|Mechanisms|StaticMaterializer|MovementFields|WeaponRelations|WeaponField|Composer))|isl_|__gmp'

cd "$repo_dir"
for cc in gcc clang
do
	$cc $strict -I. -Islipgate -c slipgate/sg_rune_compact_game.c \
		-o "$tmp_dir/game-runtime-$cc.o"
	if nm -u "$tmp_dir/game-runtime-$cc.o" | grep -E "$runtime_forbidden"; then
		echo "runtime sv rune bridge reaches offline construction" >&2
		exit 1
	fi
	$cc $strict -I. -Islipgate $sources -o "$tmp_dir/game-$cc"
	"$tmp_dir/game-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined \
	-I. -Islipgate $sources -o "$tmp_dir/game-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/game-sanitize"
