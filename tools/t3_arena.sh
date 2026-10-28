#!/bin/bash
# T3 attempt: reuse freed arena slots on insert (free list), then prove it with a churn test.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
p = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics/bm_n1_page_core.hpp')
t = p.read_text(encoding='utf-8')
if 'free_slots_' not in t:
    t = t.replace("    std::vector<std::uint64_t> digests_;",
                  "    std::vector<std::uint64_t> digests_;\n"
                  "    // Freed suffix slots, reused by later inserts so the arena stops growing under churn.\n"
                  "    std::vector<std::pair<std::uint32_t,std::uint32_t>> free_slots_;", 1)
    # capture the freed slot in erase
    old_erase = "        --block.count;--size_;return true;"
    new_erase = ("        free_slots_.emplace_back(block.entries[block.count].offset,\n"
                 "                                 block.entries[block.count].length);\n"
                 "        --block.count;--size_;return true;")
    assert old_erase in t, 'erase tail not found'
    t = t.replace(old_erase, new_erase, 1)
    # reuse a freed slot when it fits
    old_ins = ("        const auto tag=fingerprint(s);const auto offset=std::uint32_t(arena_.size());\n"
               "        arena_.insert(arena_.end(),s.begin(),s.end()); // reserved, char construction cannot throw")
    new_ins = ("        const auto tag=fingerprint(s);\n"
               "        std::uint32_t offset=0;\n"
               "        {\n"
               "            std::size_t pick=free_slots_.size();\n"
               "            for(std::size_t i=0;i<free_slots_.size();++i)\n"
               "                if(free_slots_[i].second>=s.size()){pick=i;break;}\n"
               "            if(pick<free_slots_.size()) {\n"
               "                offset=free_slots_[pick].first;\n"
               "                free_slots_.erase(free_slots_.begin()+std::ptrdiff_t(pick));\n"
               "                std::memcpy(arena_.data()+offset,s.data(),s.size());\n"
               "            } else {\n"
               "                offset=std::uint32_t(arena_.size());\n"
               "                arena_.insert(arena_.end(),s.begin(),s.end()); // reserved, no-throw\n"
               "            }\n"
               "        }")
    assert old_ins in t, 'insert arena append not found'
    t = t.replace(old_ins, new_ins, 1)
    if '#include <cstring>' not in t:
        t = t.replace('#include <cstdint>', '#include <cstdint>\n#include <cstring>', 1)
    p.write_text(t, encoding='utf-8')
    print('free list applied')
else:
    print('free list already present')
PY
F="$W/builds/t3_20261028"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/diag_bm_cost_t1.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/t1" 2>"$F/build.err" || { echo BUILD FAILED; tail -12 "$F/build.err"; git -C "$W" checkout -- "$D/bm_n1_page_core.hpp"; exit 1; }
echo "== cost ledger with the free list"
"$F/t1" "$R/results/bm_real_20261018/interchange/english_words_rows.bin" || { echo RUN FAILED; git -C "$W" checkout -- "$D/bm_n1_page_core.hpp"; exit 1; }
echo "== semantic gate"
bash "$W/tools/n3_gate.sh" 2>&1 | tail -3 || { echo GATE FAILED; git -C "$W" checkout -- "$D/bm_n1_page_core.hpp"; exit 1; }
