#!/bin/sh
set -eu

if [ "$#" -ne 1 ] || [ ! -r "$1" ]; then
	echo "usage: $0 GAME-MODULE" >&2
	exit 2
fi

module=$1
runtime_forbidden='SG_(ConfigurationAudit|ConfigurationSemanticsAudit|BspCompletenessProve|HostLawConstruction(ConfigurationAudit|SemanticsAudit|CompletenessProve))|isl_|__gmp'

ldd_output=$(ldd -r "$module" 2>&1) || {
	printf '%s\n' "$ldd_output" >&2
	exit 1
}
printf '%s\n' "$ldd_output"
if printf '%s\n' "$ldd_output" | grep -q 'undefined symbol:'; then
	echo "runtime module has unresolved symbols" >&2
	exit 1
fi
if readelf -d "$module" | grep -E 'NEEDED.*(libisl|libgmp)' >&2; then
	echo "runtime module links an offline solver library" >&2
	exit 1
fi
if nm "$module" | grep -E "$runtime_forbidden" >&2; then
	echo "runtime module contains an offline construction symbol" >&2
	exit 1
fi
if nm -D "$module" | grep -q 'SG_HostLawConstructionOwnerCopyBsp'; then
	echo "runtime module exports its private offline copy-out bridge" >&2
	exit 1
fi
