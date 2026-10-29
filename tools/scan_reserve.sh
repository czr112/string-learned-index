#!/bin/bash
# Cheap, safe win inside the 64-77% block: reserve the scan output so it does not regrow.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
p = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics/bm_n1_page_core.hpp')
t = p.read_text(encoding='utf-8')
if 'out.reserve(size_)' in t:
    print('already reserved'); raise SystemExit(0)
old = """        std::uint32_t b=lo?lo-1:0;
        bool seek=true;"""
new = """        out.reserve(size_<limit?size_:limit);   // one allocation instead of ~log2(n) regrowths
        std::uint32_t b=lo?lo-1:0;
        bool seek=true;"""
assert old in t, 'scan head not found'
p.write_text(t.replace(old, new, 1), encoding='utf-8')
print('scan output reserved')
PY
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/diag_bm_u3_scan.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$W/builds/dev/u3scan2" 2>&1 | head -6
echo "== after reserve"
"$W/builds/dev/u3scan2" "$R/results/bm_real_20261018/interchange/english_words_rows.bin"
"$W/builds/dev/u3scan2" "$R/results/bm_real_20261018/interchange/memetracker_urls_rows.bin"
echo "== semantic gate"
bash "$W/tools/n3_gate.sh" 2>&1 | tail -2
