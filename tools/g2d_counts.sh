#!/bin/bash
# Re-apply the fixed wiring, add a counts dumper, and print the four numbers around one split.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
git -C "$W" checkout 8a94f76 -- "$D/bm_n1_index.hpp" "$D/bm_split_index.hpp"
python3 - <<'PY'
import pathlib
d = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics')
for name in ('bm_n1_index.hpp','bm_split_index.hpp'):
    p = d/name
    s = p.read_text(encoding='utf-8')
    if 'std::string counts()const' not in s:
        anchor='    const Maintenance& maintenance()const{return maintenance_;}'
        add = ('    std::string counts()const{\n'
               '        std::size_t live=0;for(const auto& p:pages_)live+=p.second.core->size();\n'
               '        return "size="+std::to_string(size_)+" live="+std::to_string(live)+\n'
               '               " resident="+std::to_string(resident_)+\n'
               '               " memory="+std::to_string(memory_bytes())+\n'
               '               " pages="+std::to_string(pages_.size())+\n'
               '               " bindings="+std::to_string(root_->bindings().size());\n'
               '    }\n')
        assert anchor in s, name
        s = s.replace(anchor, anchor + '\n' + add, 1)
        p.write_text(s, encoding='utf-8')
        print(f'{name}: counts() added')
PY
cat > "$D/dbg_bm_counts.cpp" <<'CPP'
#include "bm_n1_index.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
int main(int argc,char** argv){
    if(argc!=2)return 1;
    std::ifstream in(argv[1],std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
    std::size_t pos=16;std::vector<static_physical::Row> rows;
    for(std::uint64_t i=0;i<count;++i){
        std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
        std::string key=bytes.substr(pos,n);pos+=n;
        std::uint64_t v=0;std::memcpy(&v,bytes.data()+pos,8);pos+=8;
        rows.push_back({key,v});
    }
    bm_n1_page::Index index(rows);
    std::cout<<"after build: "<<index.counts()<<"\n";
    auto before=index.maintenance().rebuilds;
    int splits=0;
    for(std::size_t i=0;i<rows.size()&&splits<1;++i){
        std::string key=rows[i].key+"~dbg";
        index.insert(key,1);
        if(index.maintenance().rebuilds>before){++splits;before=index.maintenance().rebuilds;
            std::cout<<"after split "<<i<<": "<<index.counts()<<"\n";}
    }
    return 0;
}
CPP
F="$W/builds/g2d_20261029"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/dbg_bm_counts.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/dbg" 2>"$F/b.err" || { echo BUILD FAILED; tail -12 "$F/b.err"; exit 1; }
"$F/dbg" "$R/results/bm_real_20261018/interchange/english_words_rows.bin"
