#!/bin/bash
# P5: rebuild every dependency in a fresh directory and re-run the formal v1 acceptance.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace
R="$W/07_Eval_Integration"
FRESH="$W/builds/p5_20261020"
rm -rf "$FRESH"; mkdir -p "$FRESH/lib" "$FRESH/art" "$FRESH/acc"
echo "== upstream BlockLearn libraries (fresh CMake build)"
cmake -S "$R/upstream_snapshots/model_3894c767/src" -B "$FRESH/lib" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS='-O3 -march=x86-64-v3' \
  -DSTRING_INDEX_CONTRACT_INCLUDE_DIR="$R/upstream_snapshots/benchmark_003a55bf/src" >"$FRESH/cmake.log" 2>&1
cmake --build "$FRESH/lib" --target blocklearn_adapter --parallel 4 >>"$FRESH/cmake.log" 2>&1
echo "== libart (fresh object)"
gcc -std=c11 -DNDEBUG -march=x86-64-v3 -O3 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src" \
  -c "$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src/art.c" \
  -o "$FRESH/art/art.o"
echo "== formal v1 acceptance for the BM page adapter"
g++ -pthread -std=c++17 -DNDEBUG -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" \
  -I"$R/learned-index/src" -I"$R/diagnostics" \
  -DACCEPTANCE_FACTORY_HEADER='"bm_page_adapter.hpp"' \
  -DACCEPTANCE_FACTORY_SYMBOL=make_bm_page_index \
  -DACCEPTANCE_SAMPLE_PARAMETER=lookup \
  -DBM_CORE_SHA256='"p5-fresh-core"' -DBM_ADAPTER_SHA256='"p5-fresh-adapter"' \
  "$R/adapters/common/contract_acceptance.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/residual_tags.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/query_cost.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/region_planner.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/learned_index.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/benchmark/model_factories.cpp" \
  -o "$FRESH/acc/bm_page_acceptance"
"$FRESH/acc/bm_page_acceptance" >"$FRESH/acc/acceptance.txt" 2>"$FRESH/acc/acceptance.err" || true
echo "== hashes"
sha256sum "$FRESH"/lib/libblocklearn_*.a "$FRESH/art/art.o" "$FRESH/acc/bm_page_acceptance"
echo "== acceptance tail"
tail -3 "$FRESH/acc/acceptance.txt" || true
cat "$FRESH/acc/acceptance.err" || true
