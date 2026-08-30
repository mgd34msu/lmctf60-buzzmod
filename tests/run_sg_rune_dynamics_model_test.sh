#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion'
strict="$strict -Wsign-conversion -Wshadow -Wstrict-prototypes"
strict="$strict -Wmissing-prototypes -Wcast-align"
sources='tests/sg_rune_dynamics_model_test.c'
sources="$sources slipgate/sg_rune_dynamics_model.c"
sources="$sources slipgate/sg_rune_dynamics_geometry.c"
sources="$sources slipgate/sg_rune_field_contract.c slipgate/sg_destination.c"
sources="$sources slipgate/sg_rune_model.c"
sources="$sources slipgate/sg_field_attractor.c slipgate/sg_field_service.c"

cd "$repo_dir"
python3 -B tests/test_sg_rune_dynamics_rank_reference.py
for cc in gcc clang
do
	$cc $strict -DSG_FIELD_SERVICE_TESTING -I. $sources -lm \
		-o "$tmp_dir/rune-dynamics-$cc"
	"$tmp_dir/rune-dynamics-$cc"
done

for long_double_bits in 64 80
do
	gcc $strict -DSG_FIELD_SERVICE_TESTING -mlong-double-$long_double_bits \
		-I. $sources -lm \
		-o "$tmp_dir/rune-dynamics-ld$long_double_bits"
	"$tmp_dir/rune-dynamics-ld$long_double_bits"
done

clang $strict -DSG_FIELD_SERVICE_TESTING -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. \
	$sources -lm -o "$tmp_dir/rune-dynamics-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/rune-dynamics-sanitize"

clang $strict --analyze -Xanalyzer -analyzer-output=text -I. \
	slipgate/sg_rune_dynamics_model.c slipgate/sg_rune_dynamics_geometry.c \
	slipgate/sg_rune_field_contract.c slipgate/sg_destination.c
