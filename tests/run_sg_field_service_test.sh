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
sources="$sources slipgate/sg_rune_model.c slipgate/sg_field_attractor.c"
sources="$sources slipgate/sg_field_service.c"

cd "$repo_dir"

# Public and general internal callers cannot see or construct the opaque
# adoption source. The owner-only header also rejects accidental inclusion.
for cc in gcc clang
do
	if printf '%s\n' '#include "slipgate/sg_field_service_owner_private.h"' |
		$cc $strict -I. -x c -c -o "$tmp_dir/forged-$cc.o" - 2>/dev/null
	then
		echo "field service owner-private boundary accepted an untrusted include" >&2
		exit 1
	fi
	if printf '%s\n' \
		'#include "slipgate/sg_rune_dynamics_model_internal.h"' \
		'void forged(void) { SG_FieldModelSourceAdoptOwnerPrivate(0, 0, 0, 0); }' |
		$cc $strict -I. -x c -c -o "$tmp_dir/forged-call-$cc.o" - \
			2>/dev/null
	then
		echo "general internal caller reached owner-private adoption" >&2
		exit 1
	fi
	$cc $strict -DSG_FIELD_SERVICE_TESTING -I. $sources -lm \
		-o "$tmp_dir/field-service-$cc"
	"$tmp_dir/field-service-$cc"
done

for long_double_bits in 64 80
do
	gcc $strict -DSG_FIELD_SERVICE_TESTING \
		-mlong-double-$long_double_bits -I. $sources -lm \
		-o "$tmp_dir/field-service-ld$long_double_bits"
	"$tmp_dir/field-service-ld$long_double_bits"
done

# Force binary32 stores and standard excess-precision rules. This catches a
# solver whose intervals depend on the host's wider evaluation registers.
gcc $strict -DSG_FIELD_SERVICE_TESTING -fexcess-precision=standard \
	-ffloat-store -I. $sources -lm -o "$tmp_dir/field-service-binary32"
"$tmp_dir/field-service-binary32"

clang $strict -DSG_FIELD_SERVICE_TESTING -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. $sources -lm \
	-o "$tmp_dir/field-service-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/field-service-sanitize"

clang $strict --analyze -Xanalyzer -analyzer-output=text -I. \
	slipgate/sg_field_service.c
