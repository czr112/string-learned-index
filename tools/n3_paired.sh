#!/bin/bash
# N3 final gate: four arms (bm / bm_split / art / ctx16) on the four growth traces, 5 runs each.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace
R="$W/07_Eval_Integration"; D="$R/diagnostics"
F="$W/builds/n3_paired_20261022"; rm -rf "$F"; mkdir -p "$F"
gcc -std=c11 -DNDEBUG -march=x86-64-v3 -O3 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src" \
  -c "$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src/art.c" -o "$F/art.o"
g++ -pthread -std=c++17 -march=x86-64-v3 -O3 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" \
  -I"$R/learned-index/src" -I"$R/adapters/art" -I"$R/adapters/our_index" \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src" -I"$D" \
  -DBM_MODEL_COMMIT='"3894c767"' -DBM_CORE_BUNDLE_SHA256='"n3"' -DBM_EXPERIMENT_ADAPTER_SHA256='"n3"' \
  "$D/bench_bm_r1_growth.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/residual_tags.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/query_cost.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/region_planner.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/learned_index.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/benchmark/model_factories.cpp" \
  "$R/adapters/art/art_adapter.cpp" "$R/adapters/our_index/our_index_adapter.cpp" "$F/art.o" \
  "$R/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp" \
  -o "$F/growth" 2>"$F/build.err" || { echo BUILD FAILED; tail -20 "$F/build.err"; exit 1; }
T="$R/results/bm_p3_dynamic_20261020/traces"
for cell in english_words_95_5_growth english_words_80_20_growth \
            memetracker_urls_95_5_growth memetracker_urls_80_20_growth; do
  for run in 0 1 2 3 4; do
    for arm in bm bm_split art ctx16; do
      taskset -c 0 "$F/growth" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" "$arm" \
        >>"$F/${cell}.jsonl" 2>>"$F/${cell}.err" || { echo "FAILED $cell $arm"; tail -3 "$F/${cell}.err"; exit 1; }
    done
  done
  echo "== $cell done"
done
