#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
header="$root/BuzzmodVersion.h"

version=$(sed -n 's/^#define BUZZMOD_VERSION "\([^"]*\)"$/\1/p' "$header")
count=$(printf '%s\n' "$version" | sed '/^$/d' | wc -l | tr -d ' ')

if [ "$count" -ne 1 ]; then
	echo "BuzzmodVersion.h must define BUZZMOD_VERSION exactly once" >&2
	exit 1
fi

if ! printf '%s\n' "$version" | grep -Eq \
	'^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-((0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(\.(0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?(\+([0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*))?$'; then
	echo "BUZZMOD_VERSION is not valid Semantic Versioning: $version" >&2
	exit 1
fi

if [ "$#" -gt 1 ]; then
	echo "usage: $0 [v<version>]" >&2
	exit 1
fi

if [ "$#" -eq 1 ] && [ "$1" != "v$version" ]; then
	echo "release tag $1 does not match Buzzmod v$version" >&2
	exit 1
fi

printf '%s\n' "$version"
