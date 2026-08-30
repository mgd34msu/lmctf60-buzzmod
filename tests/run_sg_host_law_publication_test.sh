#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion'
strict="$strict -Wsign-conversion -Wshadow -Wstrict-prototypes"
strict="$strict -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align"
strict="$strict -DSG_HOST_LAW_TESTING"
isl_cflags=$(pkg-config --cflags isl)
isl_libs=$(pkg-config --libs isl)

cd "$repo_dir"

for cc in gcc clang
do
	$cc $strict $isl_cflags -I. -c \
		tests/sg_host_law_construction_no_borrow_compile_test.c \
		-o "$tmp_dir/construction-view-$cc.o"
	if $cc $strict $isl_cflags -I. \
		-DSG_HOST_LAW_ATTEMPT_CONSTRUCTION_MUTATION -c \
		tests/sg_host_law_construction_no_borrow_compile_test.c \
		-o "$tmp_dir/construction-mutation-$cc.o" \
		2>"$tmp_dir/construction-mutation-$cc.err"
	then
		echo "construction view unexpectedly exposed mutable collision storage" >&2
		exit 1
	fi
	if ! grep -q 'member.*collision' \
		"$tmp_dir/construction-mutation-$cc.err"
	then
		cat "$tmp_dir/construction-mutation-$cc.err" >&2
		exit 1
	fi
done

compile_host_law()
{
	cc=$1
	suffix=$2
	extra=$3
	objects=''

	for pair in \
		'tests/sg_host_law_publication_test.c test' \
		'slipgate/sg_host_law_publication.c publication' \
		'slipgate/sg_host_law_owner.c owner' \
		'slipgate/sg_host_engine_pmove.c engine-pmove' \
		'slipgate/sg_host_engine_runtime.c engine-runtime' \
		'slipgate/sg_client_ownership.c client-ownership' \
		'slipgate/sg_host_engine_parity.c engine-parity' \
		'slipgate/sg_host_hook_law.c hook-law' \
		'slipgate/sg_host_mechanism_law.c mechanism-law' \
		'slipgate/sg_host_pmove.c host-pmove' \
		'slipgate/sg_host_collision.c collision' \
		'slipgate/sg_bsp_world.c bsp-world' \
		'slipgate/sg_configuration_semantics.c configuration-semantics' \
		'slipgate/sg_configuration_lattice.c configuration-lattice' \
		'slipgate/sg_configuration_space.c configuration-space' \
		'slipgate/sg_configuration_audit.c configuration-audit' \
		'slipgate/sg_bsp_completeness_proof.c completeness-proof' \
		'slipgate/sg_bsp_completeness_core.c completeness-core' \
		'slipgate/sg_bsp_completeness_region.c completeness-region' \
		'slipgate/sg_bsp_completeness_traversal.c completeness-traversal' \
		'slipgate/sg_bsp_completeness_lattice.c completeness-lattice' \
		'slipgate/sg_bsp_completeness_coverage.c completeness-coverage' \
		'slipgate/sg_bsp_completeness_state.c completeness-state' \
		'slipgate/sg_bsp_completeness_portal.c completeness-portal' \
		'slipgate/sg_bsp_completeness_portal_index.c completeness-portal-index' \
		'slipgate/sg_rune_model.c rune-model'; do
		set -- $pair
		$cc $strict $isl_cflags $extra -I. -DDEDICATED_ONLY -c "$1" \
			-o "$tmp_dir/$2-$suffix.o"
		objects="$objects $tmp_dir/$2-$suffix.o"
	done
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wno-strict-prototypes $extra -I. -DDEDICATED_ONLY \
		-c tests/support/yq2_pmove.c -o "$tmp_dir/yq2-pmove-$suffix.o"
	$cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wno-strict-prototypes $extra -I. -c q_shared.c \
		-o "$tmp_dir/q-shared-$suffix.o"
	$cc $extra $objects \
		"$tmp_dir/yq2-pmove-$suffix.o" \
		"$tmp_dir/q-shared-$suffix.o" -lm $isl_libs \
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
