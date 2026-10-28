// Reuse the frozen native reference's input/token/permutation helpers and F5/ART includes.
// The original main is compiled under an unused name, never executed.
#define main frozen_bm_native_reference_main
#include "bench_bm_native.cpp"
#undef main

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows queries process_offset");
        const int offset=std::stoi(argv[3]);if(offset<0||offset>4)throw std::invalid_argument("offset");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);auto queries=uni::read_queries(qs);
        std::map<std::string,std::uint64_t> oracle;for(const auto& r:rows)oracle.emplace(r.key,r.value);
        for(std::size_t i=0;i<queries.size();++i)if(i%10==9) {
            queries[i].key+="~f2/miss";while(oracle.count(queries[i].key))queries[i].key+="~";
        }
        const sib::Factory factories[]={make_block_residual_delta_shortdir_first,
            make_block_residual_delta_shortdir_first_cost,make_block_adaptive_delta_shortdir_first,
            make_art_index,make_coupled_page_index};
        const char* slots[]={"legacy","cost","adaptive","ART","F5"};
        const char* names[]={"legacy","legacy_AA","cost","cost_AA","adaptive","adaptive_AA","ART","ART_AA","F5","F5_AA"};
        std::unique_ptr<sib::Index> indexes[5];
        for(int pos=0;pos<5;++pos) {
            const auto slot=(pos+offset)%5;const auto start=Clock::now();
            indexes[slot]=factories[slot]({});indexes[slot]->bulk_load(rows);
            const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
            const auto m=indexes[slot]->memory_usage();
            std::cout<<"{\"phase\":\"build\",\"arm\":\""<<slots[slot]<<"\",\"position\":"<<pos<<",\"elapsed_ns\":"<<ns
                <<",\"owned_bytes\":"<<(m.index_owned_bytes?std::to_string(*m.index_owned_bytes):"null")
                <<",\"retained_input_bytes\":"<<(m.retained_input_bytes?std::to_string(*m.retained_input_bytes):"null")<<"}\n";
        }
        for(int arm=0;arm<10;++arm) {
            const auto& idx=indexes[arm/2];
            for(const auto& r:rows)if(idx->find(r.key)!=r.value)throw std::logic_error("timing row oracle");
            for(const auto& q:queries) {
                const auto it=oracle.find(q.key);const auto want=it==oracle.end()?std::optional<std::uint64_t>{}:it->second;
                if(idx->find(q.key)!=want)throw std::logic_error("timing query oracle");
            }
            std::cout<<"{\"phase\":\"gate\",\"arm\":\""<<names[arm]<<"\",\"verified\":"<<rows.size()+queries.size()<<"}\n";
        }
        constexpr int williams[]={0,1,9,2,8,3,7,4,6,5};
        for(int repeat=0;repeat<10;++repeat) {
            const auto order=permutation(queries.size(),repeat);std::uint64_t sums[10]{};
            for(int pos=0;pos<10;++pos) {
                const auto arm=(williams[pos]+repeat+offset)%10;
                const auto start=Clock::now();std::uint64_t checksum=0;
                for(auto i:order)checksum=checksum*131+token(indexes[arm/2]->find(queries[i].key));
                const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
                sink=checksum;sums[arm]=checksum;
                std::cout<<"{\"phase\":\"pass\",\"repeat\":"<<repeat<<",\"position\":"<<pos
                    <<",\"arm\":\""<<names[arm]<<"\",\"elapsed_ns\":"<<ns<<",\"operations\":"<<queries.size()
                    <<",\"checksum\":"<<checksum<<"}\n";
            }
            for(int a=1;a<10;++a)if(sums[a]!=sums[0])throw std::logic_error("timing checksum mismatch");
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
