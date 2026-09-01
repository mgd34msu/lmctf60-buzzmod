#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion
-Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
-Wformat=2 -Wcast-qual -Wcast-align'
modules='slipgate/sg_hook_visibility_feasibility.c
slipgate/sg_hook_visibility_feasibility_family.c
slipgate/sg_hook_visibility_feasibility_events.c
slipgate/sg_hook_visibility_feasibility_partition.c
slipgate/sg_hook_visibility_feasibility_construct.c
slipgate/sg_hook_visibility_feasibility_verifier_digest.c
slipgate/sg_hook_visibility_feasibility_audit.c
slipgate/sg_hook_visibility_feasibility_audit_family.c
slipgate/sg_hook_visibility_feasibility_audit_events.c
slipgate/sg_hook_visibility_feasibility_audit_tiling.c
slipgate/sg_host_collision.c'
support='tests/sg_hook_visibility_feasibility_fixture.c
tests/sg_hook_visibility_host_angle_reference.c'
test_mains='tests/sg_hook_visibility_feasibility_test.c
tests/sg_hook_visibility_production_angle_test.c
tests/sg_hook_visibility_clearance_face_test.c
tests/sg_hook_visibility_reverse_span_test.c
tests/sg_hook_visibility_slanted_source_test.c
tests/sg_hook_visibility_scaling_test.c'

cd "$repo_dir"
for cc in gcc clang
do
	$cc -std=gnu11 -I. -c q_shared.c -o "$tmp_dir/q-shared-$cc.o"
	for main in $test_mains
	do
		name=$(basename "$main" .c)
		$cc $strict -I. "$main" $support $modules \
			"$tmp_dir/q-shared-$cc.o" -lm -o "$tmp_dir/$name-$cc"
		"$tmp_dir/$name-$cc"
	done
done

sanitize='-fno-omit-frame-pointer -fsanitize=address,undefined'
clang -std=gnu11 $sanitize -I. -c q_shared.c \
	-o "$tmp_dir/q-shared-sanitize.o"
for main in $test_mains
do
	name=$(basename "$main" .c)
	clang $strict $sanitize -I. "$main" $support $modules \
		"$tmp_dir/q-shared-sanitize.o" -lm -o "$tmp_dir/$name-sanitize"
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$tmp_dir/$name-sanitize"
done

for source in slipgate/sg_hook_visibility_feasibility.c \
	slipgate/sg_hook_visibility_feasibility_family.c \
	slipgate/sg_hook_visibility_feasibility_events.c \
	slipgate/sg_hook_visibility_feasibility_partition.c \
	slipgate/sg_hook_visibility_feasibility_construct.c \
	slipgate/sg_hook_visibility_feasibility_verifier_digest.c \
	slipgate/sg_hook_visibility_feasibility_audit.c \
	slipgate/sg_hook_visibility_feasibility_audit_family.c \
	slipgate/sg_hook_visibility_feasibility_audit_events.c \
	slipgate/sg_hook_visibility_feasibility_audit_tiling.c \
	tests/sg_hook_visibility_feasibility_fixture.c \
	tests/sg_hook_visibility_host_angle_reference.c \
	tests/sg_hook_visibility_feasibility_test.c \
	tests/sg_hook_visibility_production_angle_test.c \
	tests/sg_hook_visibility_clearance_face_test.c \
	tests/sg_hook_visibility_reverse_span_test.c \
	tests/sg_hook_visibility_slanted_source_test.c \
	tests/sg_hook_visibility_scaling_test.c
do
	clang --analyze $strict -I. "$source" \
		-o "$tmp_dir/$(basename "$source").plist"
done
