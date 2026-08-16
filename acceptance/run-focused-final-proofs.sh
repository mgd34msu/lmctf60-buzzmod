#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
scratch=$(mktemp -d "${TMPDIR:-/var/tmp}/accept-drop-focused-tests.XXXXXX")
trap 'rm -rf -- "$scratch"' EXIT HUP INT TERM
include="$scratch/include"
mkdir -p "$include"
cd "$root"
sed -e 's/\$//g' \
    -e 's/WCLOGCOUNT+2/0/g' \
    -e 's/WCREV=7/accept-drop-focused-test/g' \
    -e 's/WCNOW=%Y/2026/g' \
    GitRevisionInfo.tmpl > "$include/GitRevisionInfo.h"

sh acceptance/sg_accept_drop_authority_seam_test.sh slipgate/sg_move.c
for cc in gcc clang
do
	flags="-std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -I$include -I."
	gc="-Wl,--gc-sections -lm"

	"$cc" $flags acceptance/sg_accept_drop_injection_test.c \
	    slipgate/sg_drop_live.c slipgate/sg_replay.c $gc \
	    -o "$scratch/sg_accept_drop_injection_test.$cc"
	"$cc" $flags -DSG_ACCEPT_DROP_LEGACY_A=1 \
	    acceptance/sg_accept_drop_injection_test.c \
	    slipgate/sg_drop_live.c slipgate/sg_replay.c $gc \
	    -o "$scratch/sg_accept_drop_injection_test.a.$cc"
	"$cc" $flags -DSG_ACCEPT_DROP_LEGACY_A=1 \
	    acceptance/sg_accept_drop_summary_test.c slipgate/sg_replay.c $gc \
	    -o "$scratch/sg_accept_drop_summary_test.a.$cc"
	"$cc" $flags -DSG_ACCEPT_DROP_LEGACY_A=0 \
	    acceptance/sg_accept_drop_summary_test.c slipgate/sg_replay.c $gc \
	    -o "$scratch/sg_accept_drop_summary_test.b.$cc"
	"$cc" $flags -DSG_ACCEPT_DROP_LEGACY_A=1 \
	    acceptance/sg_accept_drop_event_schema_test.c \
	    slipgate/sg_drop_live.c slipgate/sg_replay.c $gc \
	    -o "$scratch/sg_accept_drop_event_schema_test.a.$cc"
	"$cc" $flags -DSG_ACCEPT_DROP_LEGACY_A=0 \
	    acceptance/sg_accept_drop_event_schema_test.c \
	    slipgate/sg_drop_live.c slipgate/sg_replay.c $gc \
	    -o "$scratch/sg_accept_drop_event_schema_test.b.$cc"

	"$scratch/sg_accept_drop_injection_test.$cc"
	"$scratch/sg_accept_drop_injection_test.a.$cc"
	"$scratch/sg_accept_drop_summary_test.a.$cc"
	"$scratch/sg_accept_drop_summary_test.b.$cc"
	"$scratch/sg_accept_drop_event_schema_test.a.$cc" \
	    slipgate/sg_accept_drop.c slipgate/sg_descend.c
	"$scratch/sg_accept_drop_event_schema_test.b.$cc" \
	    slipgate/sg_accept_drop.c slipgate/sg_descend.c
done

printf 'focused-final-proofs ok compilers=2 variants=2\n'
