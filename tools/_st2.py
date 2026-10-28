"""Stage 2: make the block split strong-guarantee (allocate first, mutate after) and add a
fault-injection gate that throws on a chosen allocation."""
import pathlib

diag = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics')
core = diag / 'bm_n1_page_core.hpp'
text = core.read_text(encoding='utf-8')

old = """        if(block.count==16) {
            Block tail;tail.count=8;
            for(std::uint32_t i=8;i<16;++i){tail.entries[i-8]=block.entries[i];tail.tags[i-8]=block.tags[i];}
            block.count=8;
            const auto first=tail.entries[0];
            const auto at_fence=std::size_t(r.first)+std::size_t(b)+1;
            stable_fences_.insert(stable_fences_.begin()+std::ptrdiff_t(at_fence),Fence{first.offset,first.length});
            blocks_.insert(blocks_.begin()+std::ptrdiff_t(std::size_t(r.first)+b)+1,tail);
            ++regions_[0].count;
            ++reasons().block_splits;
            target=compare(s,suffix(blocks_[b+1].entries[0]))<0?std::size_t(b):std::size_t(b)+1;
        }"""
new = """        if(block.count==16) {
            // Strong guarantee: build the tail and reserve every container BEFORE any mutation,
            // so a failed allocation leaves the old block and the old directory untouched.
            Block tail;tail.count=8;
            for(std::uint32_t i=8;i<16;++i){tail.entries[i-8]=block.entries[i];tail.tags[i-8]=block.tags[i];}
            const Fence split_fence{tail.entries[0].offset,tail.entries[0].length};
            const auto at_fence=std::size_t(r.first)+std::size_t(b)+1;
            blocks_.reserve(blocks_.size()+1);
            stable_fences_.reserve(stable_fences_.size()+1);
            // From here on every operation is a no-throw move of trivially copyable elements.
            stable_fences_.insert(stable_fences_.begin()+std::ptrdiff_t(at_fence),split_fence);
            blocks_.insert(blocks_.begin()+std::ptrdiff_t(std::size_t(r.first)+b)+1,tail);
            block.count=8;
            ++regions_[0].count;
            ++reasons().block_splits;
            target=compare(s,suffix(blocks_[b+1].entries[0]))<0?std::size_t(b):std::size_t(b)+1;
        }"""
assert old in text, 'split block not found'
text = text.replace(old, new)
core.write_text(text, encoding='utf-8')

gate = r'''// Stage 2 gate: throw on a chosen allocation during a split and prove nothing is lost.
#include "bm_n1_index.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::uint64_t g_calls=0,g_throw_at=0;
bool g_armed=false;
}
void* operator new(std::size_t n) {
    if(g_armed&&++g_calls==g_throw_at)throw std::bad_alloc();
    if(!g_armed)++g_calls;
    void* p=std::malloc(n?n:1);
    if(!p)throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin throw_at");
        const std::uint64_t throw_at=std::stoull(argv[2]);
        std::ifstream in(argv[1],std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        if(bytes.size()<16||bytes.compare(0,8,"SIBROW1\0",8)!=0)throw std::runtime_error("bad rows");
        std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
        std::size_t pos=16;std::vector<static_physical::Row> rows;
        std::map<std::string,std::uint64_t> reference;
        for(std::uint64_t i=0;i<count;++i) {
            std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
            std::string key=bytes.substr(pos,n);pos+=n;
            std::uint64_t value=0;std::memcpy(&value,bytes.data()+pos,8);pos+=8;
            rows.push_back({key,value});reference.emplace(key,value);
        }
        bm_n1_page::Index index(rows);
        std::uint64_t injected=0,recovered=0,losses=0,checks=0;
        for(int i=0;i<400;++i) {
            std::string key=rows[(std::size_t(i)*rows.size())/400].key+"~fault/"+std::to_string(i);
            g_calls=0;g_throw_at=throw_at;g_armed=true;
            bool threw=false;
            try {
                const bool ok=index.insert(key,9'000+i);
                g_armed=false;
                if(ok)reference.emplace(key,9'000+i);
            } catch(const std::bad_alloc&) {
                threw=true;g_armed=false;++injected;
                // After a failed split every key that was there before must still be readable.
                for(const auto& [k,v]:reference)
                    if(index.find_base(k)!=std::optional<std::uint64_t>(v)){++losses;break;}
                ++checks;
                const auto verify_checks=index.verify();
                if(verify_checks==0)++losses;
                const bool again=index.insert(key,9'000+i);
                if(again)reference.emplace(key,9'000+i);
                if(index.find_base(key)==std::optional<std::uint64_t>(9'000+i))++recovered;
            }
            (void)threw;
        }
        std::uint64_t mismatches=0;
        for(const auto& [k,v]:reference)
            if(index.find_base(k)!=std::optional<std::uint64_t>(v))++mismatches;
        std::cout<<"{\"throw_at\":"<<throw_at<<",\"injected\":"<<injected
            <<",\"recovered\":"<<recovered<<",\"losses\":"<<losses
            <<",\"verify_calls\":"<<checks<<",\"final_rows\":"<<reference.size()
            <<",\"mismatches\":"<<mismatches<<"}\n";
        return (losses==0&&mismatches==0)?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
'''
(diag / 'gate_bm_n3_fault.cpp').write_text(gate, encoding='utf-8')

script = '''#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
F="$W/builds/n3_fault_20261023"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \\
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \\
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \\
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \\
  "$D/gate_bm_n3_fault.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \\
  -o "$F/fault" 2>"$F/build.err" || { echo BUILD FAILED; tail -15 "$F/build.err"; exit 1; }
for at in 1 2 3 5 8; do
  "$F/fault" "$R/results/bm_real_20261018/interchange/english_words_rows.bin" "$at" || { echo "FAULT GATE FAILED at=$at"; exit 1; }
done
'''
pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/tools/n3_fault.sh').write_text(script, encoding='utf-8')
print('strong guarantee applied and fault gate written')
