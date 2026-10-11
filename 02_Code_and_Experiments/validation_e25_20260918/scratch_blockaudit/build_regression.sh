#!/usr/bin/env bash
# Regression sweep: the four existing gates must still build after the BlockLinearRank
# wiring and the switches.hpp differ-table refactor. Builds only -- no new timing runs.
set -u
cd "$(dirname "$0")/.."
for t in check_g1_contract check_sround check_compact_routing check_hole_reuse bench_lits2x2; do
  echo "--- $t"
  python3 tools_build.py "$t" 2>&1 | tail -2
done
