#!/bin/bash
# Measure the single-lookup insert on one growth cell (4 blocks) and compare with the recorded
# pre-change paired ratio for the same cell.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
F="$W/builds/t2_20261029"
gcc -std=c11 -DNDEBUG -march=x86-64-v3 -O3 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src" \
  -c "$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src/art.c" -o "$F/art.o"
g++ -pthread -std=c++17 -march=x86-64-v3 -O3 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" \
  -I"$R/learned-index/src" -I"$R/adapters/art" -I"$R/adapters/our_index" \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src" -I"$D" \
  -DBM_MODEL_COMMIT='"3894c767"' -DBM_CORE_BUNDLE_SHA256='"t2"' \
  -DBM_EXPERIMENT_ADAPTER_SHA256='"t2"' -DBM_SPLIT_CORE_SHA256='"t2"' -DBM_SPLIT_ADAPTER_SHA256='"t2"' \
  "$D/bench_bm_pure.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/residual_tags.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/query_cost.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/region_planner.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/learned_index.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/benchmark/model_factories.cpp" \
  "$R/adapters/art/art_adapter.cpp" "$R/adapters/our_index/our_index_adapter.cpp" "$F/art.o" \
  "$R/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp" -o "$F/pure" 2>"$F/b2.err" \
  || { echo BUILD FAILED; tail -10 "$F/b2.err"; exit 1; }
T="$R/results/bm_audited_v3_20261025/traces"
for cell in words_95_5_uniform_growth words_80_20_uniform_growth; do
  : >"$F/${cell}.jsonl"
  for p in 0 1 2 3; do
    taskset -c 0 "$F/pure" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" "$p" >>"$F/${cell}.jsonl" 2>/dev/null
  done
done
python3 - <<'PY'
import json, pathlib, statistics
base = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/builds/t2_20261029')
old = {'words_95_5_uniform_growth': 0.447, 'words_80_20_uniform_growth': 0.432}
for cell, prev in old.items():
    rows = [json.loads(l) for l in (base/f'{cell}.jsonl').read_text().splitlines() if l.strip()]
    by = {}
    for r in rows: by.setdefault(r['arm'], []).append(r)
    bm, sp = by['bm'], by['bm_split']
    bm.sort(key=lambda r: (r['process'], r['position']))
    sp.sort(key=lambda r: (r['process'], r['position']))
    ratios = [s['write_us']/b['write_us'] for b, s in zip(bm, sp)]
    print(f"{cell}: new mean ratio {statistics.fmean(ratios):.3f} (prev {prev:.3f}), "
          f"drop {(1-statistics.fmean(ratios))*100:.1f}% (prev {(1-prev)*100:.1f}%)")
PY
