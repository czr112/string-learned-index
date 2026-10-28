"""Derive F3's page core from frozen D1; preserve its model/certificate functions."""
from pathlib import Path

HERE = Path(__file__).resolve().parent
source = (HERE / 'collision_refined_index.hpp').read_text(encoding='utf-8')


def replace(old, new, count=1):
    global source
    assert source.count(old) == count, (old[:100], source.count(old))
    source = source.replace(old, new)


replace('// D1 experimental owning core: one local-prefix refinement level. B/S3 unchanged.',
        '// F3 generated from frozen D1. Model/certificate algorithms are unchanged; see generator.')
replace('namespace collision_refined {', 'namespace slotted_page {')
replace('class Index {', 'class Page {\n    struct Fence {std::uint32_t offset,length;};\n    std::vector<Fence> stable_fences_;')
replace('return suffix(blocks_[r.first+block].entries[0]);',
        'const auto& f=stable_fences_[r.first+block];\n        return std::string_view(arena_.empty()?"":arena_.data()+f.offset,f.length);')
replace('template<class InputRow> explicit Index(const std::vector<InputRow>& rows,std::size_t region_keys=2048) {\n        if(!region_keys||region_keys>65536)throw std::invalid_argument("region size");',
        'template<class InputRow> explicit Page(const std::vector<InputRow>& rows,std::size_t occupancy=15) {\n        const std::size_t region_keys=2048;\n        if(rows.size()>region_keys||(occupancy!=15&&occupancy!=16))throw std::invalid_argument("page size/occupancy");')
replace('(end-first+15)/16', '(end-first+occupancy-1)/occupancy')
replace('arena_.reserve(bytes);blocks_.reserve(nb);',
        'const auto extra=occupancy==15?bytes/32+(bytes%32!=0):0;\n        if(extra>UINT32_MAX-bytes)throw std::length_error("page arena reserve limit");\n        arena_.reserve(bytes+extra);blocks_.reserve(nb);stable_fences_.reserve(nb);')
replace('while(i<end&&b.count<16)', 'while(i<end&&b.count<occupancy)')
replace('arena_.insert(arena_.end(),s.begin(),s.end());++b.count;++i;\n                }\n            }',
        'arena_.insert(arena_.end(),s.begin(),s.end());++b.count;++i;\n                }\n                stable_fences_.push_back({b.entries[0].offset,b.entries[0].length});\n            }')
replace('full_compare(start,blocks_[start.first+mid].entries[0])',
        'compare_boundary(start,mid,lower)')
replace('const auto& start=regions_[ri];', 'const auto& start=regions_[ri];')
replace('Memory m;m.object=sizeof(*this);', 'Memory m;m.object=sizeof(*this);m.routing=stable_fences_.capacity()*sizeof(Fence);')
replace('m.routing=digests_.capacity()*sizeof(std::uint64_t);', 'm.routing+=digests_.capacity()*sizeof(std::uint64_t);')
replace('} // namespace collision_refined', '} // namespace slotted_page')

methods = r'''
    int compare_boundary(const Region& r,std::uint32_t b,std::string_view key)const {
        const auto n=std::min(key.size(),r.prefix.size());
        const int c=n?std::memcmp(r.prefix.data(),key.data(),n):0;
        if(c)return c;
        if(key.size()<r.prefix.size())return 1;
        return compare(boundary(r,b),key.substr(r.prefix.size()));
    }
    struct Slot {std::uint32_t block=0,entry=0;bool found=false;};
    Slot seek(std::string_view key)const {
        const auto location=locate_blocks<Lookup::learned>(key);
        if(!location.prefix_verified)return {};
        const auto s=key.substr(regions_[0].prefix.size());const auto tag=fingerprint(s);
        for(auto b=location.begin;b<location.end;++b) {
            const auto& block=blocks_[b];
            const auto tags=_mm_load_si128(reinterpret_cast<const __m128i*>(block.tags));
            auto mask=std::uint32_t(_mm_movemask_epi8(_mm_cmpeq_epi8(tags,_mm_set1_epi8(static_cast<char>(tag)))));
            mask&=(1u<<block.count)-1;
            while(mask) {
                const auto i=std::uint32_t(__builtin_ctz(mask));mask&=mask-1;
                if(block.entries[i].length==s.size()&&compare(suffix(block.entries[i]),s)==0)return {b,i,true};
            }
        }
        return {};
    }
public:
    enum class Insert {inserted,duplicate,rebuild};
    std::optional<std::uint64_t> find(std::string_view key)const{return find_model(key);}
    bool update(std::string_view key,std::uint64_t value) {
        const auto slot=seek(key);if(!slot.found)return false;
        blocks_[slot.block].entries[slot.entry].value=value;return true;
    }
    bool erase(std::string_view key) {
        const auto slot=seek(key);if(!slot.found)return false;
        auto& block=blocks_[slot.block];
        for(auto i=slot.entry+1;i<block.count;++i){block.entries[i-1]=block.entries[i];block.tags[i-1]=block.tags[i];}
        --block.count;--size_;return true;
    }
    Insert try_insert(std::string_view key,std::uint64_t value) {
        if(key.size()>UINT32_MAX)throw std::length_error("key exceeds page limit");
        if(find(key))return Insert::duplicate;
        if(size_>=2048||regions_.empty())return Insert::rebuild;
        const auto& r=regions_[0];
        if(key.size()<r.prefix.size()||compare(key.substr(0,r.prefix.size()),r.prefix)!=0)return Insert::rebuild;
        const auto s=key.substr(r.prefix.size());
        const auto b=directory_block(r,s,nullptr);auto& block=blocks_[b];
        if(block.count==16||s.size()>arena_.capacity()-arena_.size())return Insert::rebuild;
        std::uint32_t at=0;while(at<block.count&&compare(suffix(block.entries[at]),s)<0)++at;
        const auto tag=fingerprint(s);const auto offset=std::uint32_t(arena_.size());
        arena_.insert(arena_.end(),s.begin(),s.end()); // reserved, char construction cannot throw
        for(auto i=block.count;i>at;--i){block.entries[i]=block.entries[i-1];block.tags[i]=block.tags[i-1];}
        block.entries[at]={offset,std::uint32_t(s.size()),value};block.tags[at]=tag;
        ++block.count;++size_;return Insert::inserted;
    }
    std::vector<std::string> fence_keys()const {
        std::vector<std::string> out;
        if(!regions_.empty())for(std::uint32_t b=0;b<regions_[0].count;++b)out.push_back(regions_[0].prefix+std::string(boundary(regions_[0],b)));
        return out;
    }
    std::size_t verify()const {
        std::size_t live=0,checks=verify_numeric_certificate();
        if(blocks_.size()!=stable_fences_.size())throw std::logic_error("stable fence count");
        for(std::size_t b=0;b<blocks_.size();++b) {
            const auto& block=blocks_[b];const auto& r=regions_[0];
            if(block.count>16)throw std::logic_error("slot count");
            for(std::uint32_t i=0;i<block.count;++i) {
                const auto& e=block.entries[i];
                if(e.offset>arena_.size()||e.length>arena_.size()-e.offset)throw std::logic_error("slot arena");
                const auto s=suffix(e);
                if((b&&compare(s,boundary(r,b))<0)||(b+1<blocks_.size()&&compare(s,boundary(r,b+1))>=0))throw std::logic_error("slot range");
                if(i&&compare(suffix(block.entries[i-1]),s)>=0)throw std::logic_error("slot order");
                if(block.tags[i]!=fingerprint(s))throw std::logic_error("slot fingerprint");
                const auto key=r.prefix+std::string(s);check_window(key);
                if(find(key)!=e.value)throw std::logic_error("slot find");
                ++checks;++live;
            }
        }
        if(live!=size_)throw std::logic_error("slot cardinality");
        return checks;
    }
'''
replace('\n};\n} // namespace slotted_page', '\nprivate:\n' + methods + '\n};\n} // namespace slotted_page')
(HERE / 'slotted_page_core.hpp').write_text(source, encoding='utf-8', newline='\n')
print('generated slotted_page_core.hpp')
