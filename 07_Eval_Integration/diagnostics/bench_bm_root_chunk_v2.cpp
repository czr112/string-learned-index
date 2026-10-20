// Stage-5 root entry: whole-root copy versus chunked copy-on-write publication.
// Builds the real binding list from a BM page index, then measures what a publication must copy
// in each scheme, checks that chunked routing picks the same page for every key and query, and
// times both routing paths. The production index is not modified.
#include "bm_page_index.hpp"
#include "static_input.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace uni=string_index_bench::unified;
using Clock=std::chrono::steady_clock;

namespace {
constexpr std::size_t CHUNK=16;
struct ChunkedRoot {
    std::vector<std::vector<std::string>> chunks;   // owned lower keys
    std::vector<std::string> first;                 // first key of each chunk
    std::size_t prefix=0;
    explicit ChunkedRoot(const std::vector<std::string>& lowers) {
        for(std::size_t i=0;i<lowers.size();i+=CHUNK) {
            std::vector<std::string> chunk(lowers.begin()+i,lowers.begin()+std::min(i+CHUNK,lowers.size()));
            first.push_back(chunk.front());
            chunks.push_back(std::move(chunk));
        }
        if(first.size()>1)prefix=static_physical::common_prefix(first[1],first.back());
    }
    std::size_t locate(std::string_view key)const {
        // Locate the chunk by its first keys, then inside the chunk.
        std::size_t lo=0,hi=first.size();
        while(lo<hi) {
            const auto mid=lo+(hi-lo)/2;
            if(static_physical::compare(first[mid],key)<=0)lo=mid+1;else hi=mid;
        }
        const auto chunk=lo?lo-1:0;
        const auto& keys=chunks[chunk];
        std::size_t l=0,h=keys.size();
        while(l<h) {
            const auto mid=l+(h-l)/2;
            if(static_physical::compare(keys[mid],key)<=0)l=mid+1;else h=mid;
        }
        const auto inside=l?l-1:0;
        return chunk*CHUNK+inside;
    }
    // One publication: copy the affected chunk plus the chunk index, nothing else.
    std::size_t publication_entries()const {
        std::size_t n=first.size();                 // chunk first keys
        for(const auto& chunk:chunks)n+=chunk.size()==0?0:0; // unchanged chunks are shared
        return n+CHUNK;
    }
    std::size_t publication_key_bytes(std::size_t chunk)const {
        std::size_t n=0;for(const auto& key:first)n+=key.size();
        for(const auto& key:chunks[chunk])n+=key.size();
        return n;
    }
};
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows.bin queries.bin process_offset");
        const int offset=std::stoi(argv[3]);if(offset<0||offset>4)throw std::invalid_argument("offset");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty workload");
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        for(const auto& r:rows)physical.push_back({r.key,r.value});
        bm_page::Index index(physical);
        std::vector<std::string> lowers;
        for(const auto& state:index.page_states())lowers.push_back(state.lower);
        if(lowers.size()<2)throw std::invalid_argument("too few pages");
        const auto pages=lowers.size();
        const auto chunks=(pages+CHUNK-1)/CHUNK;
        ChunkedRoot chunked(lowers);
        const auto flat=index.root_identity_for_testing();
        if(!flat)throw std::logic_error("root identity missing");

        // Routing equivalence on every page boundary, every row key and every query.
        std::uint64_t checked=0,mismatches=0;
        auto expect=[&](std::string_view key) {
            const auto flat_page=std::size_t(flat->locate(key));
            const auto chunk_page=chunked.locate(key);
            if(flat_page!=chunk_page)++mismatches;
            ++checked;
        };
        for(const auto& lower:lowers)expect(lower);
        for(const auto& row:rows)expect(row.key);
        for(const auto& q:queries)expect(q.key);
        if(mismatches)throw std::logic_error("chunked routing disagrees with the flat root");

        // Publication work: whole-root copy versus chunked copy-on-write.
        std::uint64_t flat_entries=0,flat_key_bytes=0;
        {const auto& bindings=flat->bindings();flat_entries=bindings.size();
         for(const auto& binding:bindings)flat_key_bytes+=binding.lower.size();}
        const auto chunk_entries=chunked.publication_entries();
        const auto chunk_key_bytes=chunked.publication_key_bytes(0);

        // Routing cost: flat digest search versus chunked two-level search.
        std::vector<std::string> keys;keys.reserve(queries.size());
        for(const auto& q:queries)keys.push_back(q.key);
        std::mt19937_64 rng(20261019);
        std::shuffle(keys.begin(),keys.end(),rng);
        auto pass=[&](bool use_chunked) {
            std::uint64_t acc=0;
            for(const auto& key:keys)
                acc=acc*131+(use_chunked?chunked.locate(key):std::size_t(flat->locate(key)));
            return acc;
        };
        const char* names[]={"flat","chunked","flat_AA","chunked_AA"};
        constexpr int williams[]={0,1,3,2};
        pass(false);pass(true);
        std::cout<<"{\"phase\":\"gate\",\"pages\":"<<pages<<",\"chunks\":"<<chunks
            <<",\"chunk_size\":"<<CHUNK<<",\"checked\":"<<checked<<",\"mismatches\":"<<mismatches
            <<",\"publication\":{\"flat_entries\":"<<flat_entries<<",\"flat_key_bytes\":"<<flat_key_bytes
            <<",\"chunked_entries\":"<<chunk_entries<<",\"chunked_key_bytes\":"<<chunk_key_bytes
            <<",\"entry_reduction\":"<<(flat_entries?1.0-double(chunk_entries)/flat_entries:0.0)
            <<",\"byte_reduction\":"<<(flat_key_bytes?1.0-double(chunk_key_bytes)/flat_key_bytes:0.0)<<"}"
            <<"}\n";
        std::cout.flush();
        for(std::size_t repeat=0;repeat<6;++repeat) {
            std::uint64_t checksums[4]={};
            for(int position=0;position<4;++position) {
                const auto arm=(williams[position]+int(repeat)+offset)%4;
                const auto start=Clock::now();
                const auto checksum=pass(arm==1||arm==3);
                const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
                checksums[arm]=checksum;
                std::cout<<"{\"phase\":\"pass\",\"repeat\":"<<repeat<<",\"position\":"<<position
                    <<",\"arm\":\""<<names[arm]<<"\",\"operations\":"<<keys.size()
                    <<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum<<"}\n";
            }
            for(int arm=1;arm<4;++arm)if(checksums[arm]!=checksums[0])throw std::logic_error("arm checksum differs");
        }
        return mismatches?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
