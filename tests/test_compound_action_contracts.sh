#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/lmctf-compound-action.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
compiler=${CC:-cc}
common='-std=c11 -Wall -Wextra -Werror -Wpedantic'

"$compiler" $common -I"$root" \
	"$root/tests/sg_compound_action_gen_test.c" \
	"$root/slipgate/sg_compound_action_gen.c" \
	"$root/slipgate/sg_compound.c" \
	"$root/slipgate/sg_action.c" -lm -o "$build/gen"
"$build/gen"

"$compiler" $common -I"$root" \
	"$root/tests/sg_compound_action_publication_test.c" \
	"$root/slipgate/sg_compound_action_publication.c" \
	"$root/slipgate/sg_replay.c" \
	"$root/slipgate/sg_compound.c" \
	"$root/slipgate/sg_action.c" -lm -o "$build/publication"
"$build/publication"

python3 -B "$root/tests/test_compound_hook_generator_integration.py"
