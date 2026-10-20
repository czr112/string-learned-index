// R3 sizing: how much of page construction is the temporary row copying?
// For each growth trace, count the rebuilds the real index performs, then time only the
// temporary row materialisation (join + part) on the same keys. No index change.
#include "bm_bounded_index.hpp"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using Clock=std::chrono::steady_clock;
namespace {
struct Operation {std::uint8_t kind=0;std::uint64_t value=0,limit=0;std::string key;};
struct Row {std::string key;std::uint64_t value;};

std::vector<std::pair<std::string,std::uint64_t>> read_rows(const char* path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("cannot open rows");
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(bytes.size()<16||bytes.compare(0,8,"SIBROW1\0",8)!=0)throw std::runtime_error("bad rows magic");
    std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
    std::size_t pos=16;std::vector<std::pair<std::string,std::uint64_t>> rows;rows.reserve(count);
    for(std::uint64_t i=0;i<count;++i) {
        std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
        std::string key=bytes.substr(pos,n);pos+=n;
        std::uint64_t value=0;std::memcpy(&value,bytes.data()+pos,8);pos+=8;
        rows.emplace_back(std::move(key),value);
    }
    return rows;
}
std::vector<Operation> read_operations(const char* path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("cannot open operations");
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(bytes.size()<16||bytes.compare(0,8,"SIBOPS1\0",8)!=0)throw std::runtime_error("bad operations magic");
    std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
    std::size_t pos=16;std::vector<Operation> operations;operations.reserve(count);
    for(std::uint64_t i=0;i<count;++i) {
        Operation op;op.kind=std::uint8_t(bytes[pos]);++pos;
        std::memcpy(&op.value,bytes.data()+pos,8);pos+=8;
        std::memcpy(&op.limit,bytes.data()+pos,8);pos+=8;
        std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
        op.key=bytes.substr(pos,n);pos+=n;operations.push_back(std::move(op));
    }
    return operations;
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows.bin operations.bin label");
        const std::string label=argv[3];
        const auto initial=read_rows(argv[1]);
        const auto operations=read_operations(argv[2]);
        std::vector<static_physical::Row> physical;physical.reserve(initial.size());
        std::map<std::string,std::uint64_t> oracle;
        for(const auto& [key,value]:initial){physical.push_back({key,value});oracle.emplace(key,value);}
        bm_bounded_page::Index index(physical);
        const auto before=index.maintenance();
        const auto batch_start=Clock::now();
        for(const auto& op:operations) {
            if(op.kind==1) {
                const auto want=oracle.find(op.key);
                const auto got=index.find_base(op.key);
                if((want==oracle.end())!=!got||(got&&want!=oracle.end()&&*got!=want->second))
                    throw std::logic_error("answer drift");
            } else if(op.kind==2) {
                const bool present=oracle.find(op.key)!=oracle.end();
                const bool ok=index.insert(op.key,op.value);
                if(ok==present)throw std::logic_error("insert drift");
                if(ok)oracle.emplace(op.key,op.value);
            } else throw std::invalid_argument("growth only");
        }
        const auto batch_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now()-batch_start).count());
        const auto after=index.maintenance();
        const auto rebuilds=after.rebuilds-before.rebuilds;
        const auto rebuilt_rows=after.rows_rebuilt-before.rows_rebuilt;
        const auto rows_per=rebuilds?double(rebuilt_rows)/rebuilds:0.0;

        // Time only the temporary row materialisation the rebuild performs per page:
        // join (copy) then part (copy) over `rows_per` keys taken from the corpus.
        const auto per_page_rows=static_cast<std::size_t>(rows_per)+1;
        std::vector<Row> source;source.reserve(per_page_rows);
        for(std::size_t i=0;i<per_page_rows;++i) {
            const auto& entry=initial[(i*initial.size())/per_page_rows];
            source.push_back({entry.first,entry.second});
        }
        std::vector<Row> joined,part;
        std::uint64_t copy_ns=0;
        for(std::uint64_t r=0;r<rebuilds;++r) {
            const auto start=Clock::now();
            joined.assign(source.begin(),source.end());
            part.assign(joined.begin(),joined.end());
            copy_ns+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
        }
        std::cout<<"{\"label\":\""<<label<<"\",\"rebuilds\":"<<rebuilds
            <<",\"rows_per_page\":"<<per_page_rows<<",\"copy_ns\":"<<copy_ns
            <<",\"copy_ns_per_rebuild\":"<<(rebuilds?double(copy_ns)/rebuilds:0.0)
            <<",\"batch_ns\":"<<batch_ns
            <<",\"copy_share_of_batch\":"<<(batch_ns?double(copy_ns)/batch_ns:0.0)
            <<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
