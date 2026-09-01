#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

cd "$repo_dir"

fail()
{
	printf '%s\n' "build ownership: $*" >&2
	exit 1
}

canonicalize_file()
{
	input_file=$1
	canonical_file="$tmp_dir/canonical"
	sorted_file="$tmp_dir/canonical-sorted"
	duplicates_file="$tmp_dir/canonical-duplicates"
	while IFS= read -r source
	do
		test -f "$source" || fail "manifest source is missing: $source"
		readlink -f -- "$source" || fail "cannot resolve manifest source: $source"
	done < "$input_file" > "$canonical_file"
	sort "$canonical_file" > "$sorted_file"
	uniq -d "$sorted_file" > "$duplicates_file"
	if test -s "$duplicates_file"
	then
		printf '%s\n' 'build ownership: a source is listed more than once:' >&2
		cat "$duplicates_file" >&2
		exit 1
	fi
	cat "$sorted_file"
}

make_sources()
{
	build_file=$1
	variable=$2
	objects_file="$tmp_dir/objects"
	candidates_file="$tmp_dir/candidates"
	make -s -f "$build_file" --no-print-directory -o .depend \
		--eval "print-build-ownership: ; @printf '%s\\n' \"\$($variable)\"" \
		print-build-ownership > "$objects_file"
	tr ' ' '\n' < "$objects_file" | sed -n 's/\.o$/.c/p' > "$candidates_file"
	canonicalize_file "$candidates_file"
}

project_sources()
{
	candidates_file="$tmp_dir/project-candidates"
	sed -n 's/.*<ClCompile Include="\([^"]*\)".*/\1/p' "$1" |
		tr '\\' '/' > "$candidates_file"
	canonicalize_file "$candidates_file"
}

project_headers()
{
	candidates_file="$tmp_dir/project-header-candidates"
	sed -n 's/.*<ClInclude Include="\([^"]*\)".*/\1/p' "$1" |
		tr '\\' '/' > "$candidates_file"
	canonicalize_file "$candidates_file"
}

same_sources()
{
	left=$1
	right=$2
	label=$3
	if ! cmp -s "$left" "$right"
	then
		printf '%s\n' "build ownership: $label differ" >&2
		diff -u "$left" "$right" >&2 || true
		exit 1
	fi
}

make_runtime="$tmp_dir/make-runtime"
gnu_runtime="$tmp_dir/gnu-runtime"
windows_runtime="$tmp_dir/windows-runtime"
windows_filters="$tmp_dir/windows-filters"
windows_headers="$tmp_dir/windows-headers"
windows_filter_headers="$tmp_dir/windows-filter-headers"
make_generator="$tmp_dir/make-generator"
gnu_generator="$tmp_dir/gnu-generator"
all_compact="$tmp_dir/all-compact"
owned_compact="$tmp_dir/owned-compact"

make_sources Makefile OBJS > "$make_runtime"
make_sources GNUmakefile OBJS > "$gnu_runtime"
make_sources Makefile RUNE_COMPACT_GENERATOR_OFFLINE_OBJS > "$make_generator"
make_sources GNUmakefile RUNE_COMPACT_GENERATOR_OFFLINE_OBJS > "$gnu_generator"
project_sources gravity.vcxproj > "$windows_runtime"
project_sources gravity.vcxproj.filters > "$windows_filters"
project_headers gravity.vcxproj > "$windows_headers"
project_headers gravity.vcxproj.filters > "$windows_filter_headers"

same_sources "$make_runtime" "$gnu_runtime" 'Makefile and GNUmakefile runtime lists'
same_sources "$make_generator" "$gnu_generator" 'Makefile and GNUmakefile generator lists'
same_sources "$make_runtime" "$windows_runtime" 'native and Visual Studio runtime lists'
same_sources "$windows_runtime" "$windows_filters" 'Visual Studio project and filters lists'
same_sources "$windows_headers" "$windows_filter_headers" 'Visual Studio header lists'

if comm -12 "$make_runtime" "$make_generator" | grep -q .
then
	fail 'an offline generator source is linked into the runtime module'
fi
if comm -12 "$windows_runtime" "$make_generator" | grep -q .
then
	fail 'an offline generator source is listed in the Windows runtime module'
fi

find slipgate -maxdepth 1 -type f -name 'sg_rune_compact_*.c' -print \
	> "$tmp_dir/compact-candidates"
canonicalize_file "$tmp_dir/compact-candidates" > "$all_compact"
cat "$make_runtime" "$make_generator" | sort -u > "$owned_compact"
comm -23 "$all_compact" "$owned_compact" > "$tmp_dir/unowned-compact"
if test -s "$tmp_dir/unowned-compact"
then
	printf '%s\n' 'build ownership: compact source lacks module ownership:' >&2
	cat "$tmp_dir/unowned-compact" >&2
	exit 1
fi

expected_reader="$tmp_dir/expected-reader"
actual_readers="$tmp_dir/actual-readers"
printf '%s\n' 'tools/runecompactread.c' > "$expected_reader"
find tools -maxdepth 1 -type f -name '*read*.c' -print | sort > "$actual_readers"
same_sources "$expected_reader" "$actual_readers" 'canonical C reader list'

for legacy in \
	tools/runeaccept.c \
	tools/runev2read.c \
	tools/runev2makecheck.c \
	slipgate/sg_rune_v2_artifact_publication.c \
	slipgate/sg_rune_v2_artifact_publication_io.c \
	slipgate/sg_rune_v2_artifact_publication_manifest.c \
	slipgate/sg_rune_v2_artifact_semantic.c
do
	test ! -e "$legacy" || fail "legacy source remains: $legacy"
	if grep -F -q -- "$legacy" Makefile GNUmakefile gravity.vcxproj \
		gravity.vcxproj.filters .github/workflows/build.yml
	then
		fail "legacy source remains in a build manifest: $legacy"
	fi
done

if find . -path './.git' -prune -o -type f \( -name 'sg_rune_learning*.c' \
	-o -name 'sg_rune_learning*.h' \) \
	-print -quit | grep -q .
then
	fail 'legacy graph learning C/header source remains'
fi
if grep -F -q -- 'sg_rune_learning' Makefile GNUmakefile gravity.vcxproj \
	gravity.vcxproj.filters .github/workflows/build.yml
then
	fail 'legacy graph learning remains in a build manifest'
fi

if find . -path './.git' -prune -o -path '*/__pycache__/*' -prune -o \
	-type f -name '*.py' -print -quit | grep -q .
then
	fail 'a Python source remains in the C-only tree'
fi
if grep -E -i -q '(^|[^[:alnum:]_])(python|python3|pytest)([^[:alnum:]_]|$)' \
	Makefile GNUmakefile .github/workflows/build.yml
then
	fail 'a build manifest still invokes Python'
fi

printf '%s\n' 'run_c_build_ownership_test: ok'
