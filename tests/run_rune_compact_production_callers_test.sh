#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sh "$repo_dir/tests/run_sg_rune_compact_learning_production_link_test.sh"
