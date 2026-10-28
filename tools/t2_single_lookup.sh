#!/bin/bash
# T2: single-lookup insert - skip the duplicate re-check that Page::try_insert performs after the
# caller already looked the key up.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib, re
p = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics/bm_n1_page_core.hpp')
t = p.read_text(encoding='utf-8')
if 'try_insert_known_absent' in t:
    print('already patched'); raise SystemExit(0)
old_head = "    Insert try_insert(std::string_view key,std::uint64_t value) {"
new_head = ("    Insert try_insert(std::string_view key,std::uint64_t value) {\n"
            "        return try_insert_impl(key,value,true);\n"
            "    }\n"
            "    // The caller has already established that the key is absent in this page; skipping\n"
            "    // the second full lookup is the whole point of this entry point.\n"
            "    Insert try_insert_known_absent(std::string_view key,std::uint64_t value) {\n"
            "        return try_insert_impl(key,value,false);\n"
            "    }\n"
            "private:\n"
            "    Insert try_insert_impl(std::string_view key,std::uint64_t value,bool check_duplicate) {")
assert old_head in t, 'try_insert head not found'
t = t.replace(old_head, new_head, 1)
old_dup = "        if(find(key)){++reasons().duplicates;return Insert::duplicate;}"
new_dup = ("        if(check_duplicate&&find(key)){++reasons().duplicates;return Insert::duplicate;}\n"
           "        if(!check_duplicate)++reasons().duplicates_skipped;")
assert old_dup in t, 'duplicate check not found'
t = t.replace(old_dup, new_dup, 1)
t = t.replace("std::uint64_t page_full=0,prefix_change=0,slot_full=0,arena_short=0,duplicates=0,inserted=0,"
              "block_splits=0;",
              "std::uint64_t page_full=0,prefix_change=0,slot_full=0,arena_short=0,duplicates=0,inserted=0,"
              "block_splits=0,duplicates_skipped=0;", 1)
# close the private section before the next public: member
t = t.replace("    std::vector<std::string> fence_keys()const {",
              "public:\n    std::vector<std::string> fence_keys()const {", 1)
p.write_text(t, encoding='utf-8')

for name in ('bm_n1_index.hpp', 'bm_split_index.hpp'):
    q = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics')/name
    s = q.read_text(encoding='utf-8')
    if 'try_insert_known_absent' in s:
        continue
    old = ("            const auto result=it->second.core->try_insert(key,value);")
    new = ("            const auto result=it->second.core->try_insert_known_absent(key,value);")
    assert old in s, f'{name}: call site not found'
    q.write_text(s.replace(old, new), encoding='utf-8')
print('single-lookup insert wired')
PY
F="$W/builds/t2_20261029"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/diag_bm_cost_t1.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/t1" 2>"$F/build.err" || { echo BUILD FAILED; tail -14 "$F/build.err"; exit 1; }
echo "== cost ledger (dup ratio should drop)"
"$F/t1" "$R/results/bm_real_20261018/interchange/english_words_rows.bin" || { echo RUN FAILED; exit 1; }
echo "== semantic gate (three corpora + zero-alloc)"
bash "$W/tools/n3_gate.sh" 2>&1 | tail -2 || { echo GATE FAILED; exit 1; }
