#!/bin/sh
set -eu

# The host harness enters the real BeginIntermission boundary, executes the
# compact production lifecycle, then reloads a different compact identity.
sh tests/run_sg_rune_compact_production_test.sh
sh tests/run_sg_rune_compact_production_host_authority_test.sh
sh tests/run_sg_rune_compact_learning_consumer_test.sh
sh tests/run_sg_rune_compact_learning_host_lifecycle_test.sh

echo "compact RUNE learning production lifecycle checks passed"
