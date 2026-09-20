#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__SSE4_2__)
#include <nmmintrin.h>
#endif

namespace validation {
inline int compare(std::string_view a, std::string_view b) {
    const auto n=std::min(a.size(),b.size());
    const int c=n?std::memcmp(a.data(),b.data(),n):0;
    return c?c:(a.size()<b.size()?-1:a.size()>b.size()?1:0);
}
struct Less {
    bool operator()(std::string_view a,std::string_view b) const {return compare(a,b)<0;}
};
using Pair=std::pair<std::string,std::uint64_t>;
inline bool pair_less(const Pair&a,const Pair&b){return compare(a.first,b.first)<0;}

struct Fingerprint {
    std::uint32_t state=0x9e3779b9U;
    void byte(std::uint8_t x) {
#if defined(__SSE4_2__)
        state=_mm_crc32_u8(state,x);
#else
        state^=x;
        for(int i=0;i<8;++i)state=(state>>1)^(0x82f63b78U & (0U-(state&1U)));
#endif
    }
    void tail(std::string_view x) {
#if defined(__SSE4_2__)
        while(x.size()>=8){std::uint64_t w;std::memcpy(&w,x.data(),8);state=static_cast<std::uint32_t>(_mm_crc32_u64(state,w));x.remove_prefix(8);}
#endif
        for(unsigned char c:x)byte(c);
    }
    std::uint8_t tag()const{return static_cast<std::uint8_t>(state);}
    static std::uint8_t of(std::string_view s){Fingerprint f;f.tail(s);return f.tag();}
};

enum class Route {Full,Interval,Residual};
enum class Prefetch {Off,Early,Late};
// E5-2: mode.simd selects the batched SSE2 tag filter; identical results by construction.
struct Mode {Route route;Prefetch prefetch;bool fingerprint;bool simd=false;};
struct Result {bool found=false;std::uint64_t value=0;std::uint32_t steps=0;std::uint32_t overflow_checks=0;};
// E5-1: slots is an explicit layout parameter (default 16 reproduces the E3 behavior).
enum class ModelKind {Context16,Position16,Unigram};
struct Config {std::uint32_t block_bytes=1024;double load=.75;std::size_t region_keys=2048;Route canonical=Route::Full;std::uint32_t fixed_groups=0;ModelKind model_kind=ModelKind::Context16;std::uint32_t slots=16;};

// A version-local, normalized conditional model. The context is read-prefix-only.
class Model {
public:
    static constexpr std::uint64_t Q=1ULL<<47;
    static constexpr std::uint32_t MASS=1U<<16;
    static constexpr std::size_t ROWS=16;
    using Table=std::vector<std::array<std::uint32_t,258>>;
    Table cumulative;
    ModelKind kind=ModelKind::Context16;
    std::size_t row(std::size_t pos,unsigned previous)const{
        if(kind==ModelKind::Unigram)return 0;
        if(kind==ModelKind::Position16)return std::min<std::size_t>(15,pos);
        return ((pos&3U)<<2U)|(previous&3U);
    }
    std::size_t allocated_bytes()const{return cumulative.capacity()*sizeof(Table::value_type);}
    void train(const std::vector<Pair>& keys,std::size_t prefix,ModelKind choice=ModelKind::Context16) {
        kind=choice;
        const auto rows=kind==ModelKind::Unigram?1:ROWS;
        cumulative.assign(rows,{});
        std::vector<std::array<std::uint64_t,257>> counts(rows);
        for(auto &r:counts)r.fill(1); // finite smoothing, including EOS and byte 0
        for(const auto &kv:keys){
            const auto s=std::string_view(kv.first).substr(prefix);
            unsigned previous=0;
            const auto lim=std::min<std::size_t>(16,s.size());
            for(std::size_t p=0;p<lim;++p){unsigned d=static_cast<unsigned char>(s[p])+1U;++counts[row(p,previous)][d];previous=d;}
            if(s.size()<=16)++counts[row(s.size(),previous)][0];
        }
        for(std::size_t r=0;r<rows;++r){
            std::uint64_t total=0;for(auto x:counts[r])total+=x;
            std::array<std::uint32_t,257> frequency{};
            std::uint32_t sum=0;
            for(std::size_t d=0;d<257;++d){frequency[d]=static_cast<std::uint32_t>(counts[r][d]*MASS/total);sum+=frequency[d];}
            // Deterministic remainder assignment; at most 256 residual units.
            for(std::uint32_t d=0;d<MASS-sum;++d)++frequency[d];
            cumulative[r][0]=0;
            for(std::size_t d=0;d<257;++d)cumulative[r][d+1]=cumulative[r][d]+frequency[d];
            assert(cumulative[r][257]==MASS);
        }
    }
    static void step(const std::array<std::uint32_t,258>&c,unsigned d,std::uint64_t&lo,std::uint64_t&hi){
        const std::uint64_t origin=lo,w=hi-lo;
        lo=origin+((w*c[d])>>16);
        hi=origin+((w*c[d+1])>>16);
    }
    static std::uint32_t bucket(std::uint64_t x,std::uint32_t groups){
        const auto g=static_cast<std::uint64_t>((static_cast<unsigned __int128>(x)*groups)>>47);
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(groups-1,g));
    }
    struct Prediction {std::uint32_t group,consumed;};
    template<bool Hash>
    Prediction predict(std::string_view s,std::uint32_t groups,Route route,Fingerprint &fp)const{
        std::uint64_t lo=0,hi=Q;unsigned previous=0;std::size_t p=0;
        for(;p<s.size();++p){
            const auto ch=static_cast<unsigned char>(s[p]);
            step(cumulative[row(p,previous)],ch+1U,lo,hi);previous=ch+1U;
            if constexpr(Hash)fp.byte(ch);
            const bool stop=(lo==hi) ||
                (route==Route::Interval && bucket(lo,groups)==bucket(hi,groups)) ||
                (route==Route::Residual && static_cast<unsigned __int128>(hi-lo)*groups<Q);
            if(stop){++p;break;}
        }
        // EOS has cumulative 0, so the final coordinate is the current lower bound.
        return {bucket(lo,groups),static_cast<std::uint32_t>(p)};
    }
};

struct Entry {std::uint32_t offset,length;std::uint64_t value;};
// E5-1: tags[48] keeps the header at 64B for every supported slot count (S<=48);
// tags start at offset 16, so every 16-tag chunk is 16-byte aligned for SIMD loads.
struct alignas(64) Header {
    std::uint64_t overflow_pointer;
    std::uint16_t count,bottom,flags,reserved0;
    std::uint8_t tags[48];
};
static_assert(sizeof(Entry)==16 && sizeof(Header)==64);
static_assert(sizeof(std::uintptr_t)==8,"This validation layout assumes 64-bit pointers");

struct GroupStats {
    std::uint32_t group=0;
    std::size_t primary_entries=0,primary_bytes=0,overflow_entries=0,overflow_bytes=0;
    std::size_t free_entries=0,free_bytes=0,garbage_bytes=0;
    std::size_t blocked_slots_only=0,blocked_bytes_only=0,blocked_both=0,oversized=0,fits_now=0;
};

class Region {
    // E5-1: the entry-slot count S and the entries-area end are per-region values now.
    static constexpr std::size_t MAX_SLOTS=48;
    struct OverflowRecord {std::string suffix;std::uint64_t value;std::uint8_t tag;};
    using Overflow=std::vector<OverflowRecord>;
    Config config_;
    std::size_t slots_=0,start_=0;
    std::string prefix_;
    Model model_;
    std::byte *storage_=nullptr;
    std::uint32_t groups_=1;
    std::size_t size_=0,overflow_count_=0;
    std::vector<std::unique_ptr<Overflow>> overflow_owners_;
    Header& header(std::uint32_t g){return *reinterpret_cast<Header*>(storage_+std::size_t(g)*config_.block_bytes);}
    const Header& header(std::uint32_t g)const{return *reinterpret_cast<const Header*>(storage_+std::size_t(g)*config_.block_bytes);}
    Entry* entries(std::uint32_t g){return reinterpret_cast<Entry*>(storage_+std::size_t(g)*config_.block_bytes+sizeof(Header));}
    const Entry* entries(std::uint32_t g)const{return reinterpret_cast<const Entry*>(storage_+std::size_t(g)*config_.block_bytes+sizeof(Header));}
    Overflow* overflow(std::uint32_t g)const{return reinterpret_cast<Overflow*>(static_cast<std::uintptr_t>(header(g).overflow_pointer));}
    std::string_view suffix(std::uint32_t g,const Entry&e)const {
        assert(e.offset>=start_ && std::uint64_t(e.offset)+e.length<=config_.block_bytes);
        return {reinterpret_cast<const char*>(storage_+std::size_t(g)*config_.block_bytes+e.offset),e.length};
    }
    bool prefix_matches(std::string_view s)const{return s.size()>=prefix_.size() && (!prefix_.size()||std::memcmp(s.data(),prefix_.data(),prefix_.size())==0);}
    std::uint32_t canonical_group(std::string_view s)const{
        Fingerprint dummy;return model_.predict<false>(s,groups_,config_.canonical,dummy).group;
    }
    bool put_primary(std::uint32_t g,std::string_view s,std::uint64_t value,std::uint8_t tag){
        auto &h=header(g);
        if(h.count>=slots_ || s.size()>h.bottom-start_)return false;
        auto *es=entries(g);std::size_t p=0;
        while(p<h.count && compare(suffix(g,es[p]),s)<0)++p;
        for(std::size_t i=h.count;i>p;--i){es[i]=es[i-1];h.tags[i]=h.tags[i-1];}
        h.bottom=static_cast<std::uint16_t>(h.bottom-s.size());
        if(!s.empty())std::memcpy(storage_+std::size_t(g)*config_.block_bytes+h.bottom,s.data(),s.size());
        es[p]={h.bottom,static_cast<std::uint32_t>(s.size()),value};h.tags[p]=tag;++h.count;return true;
    }
    void put_overflow(std::uint32_t g,std::string_view s,std::uint64_t value,std::uint8_t tag){
        auto *o=overflow(g);
        if(!o){overflow_owners_.push_back(std::make_unique<Overflow>());o=overflow_owners_.back().get();header(g).overflow_pointer=reinterpret_cast<std::uintptr_t>(o);}
        auto it=std::lower_bound(o->begin(),o->end(),s,[](const OverflowRecord&a,std::string_view b){return compare(a.suffix,b)<0;});
        o->insert(it,{std::string(s),value,tag});++overflow_count_;
    }
    void compact(std::uint32_t g){
        auto &h=header(g);std::vector<Pair> live;live.reserve(h.count);
        for(std::size_t i=0;i<h.count;++i)live.emplace_back(suffix(g,entries(g)[i]),entries(g)[i].value);
        h.count=0;h.bottom=static_cast<std::uint16_t>(config_.block_bytes);
        for(const auto &kv:live){bool ok=put_primary(g,kv.first,kv.second,Fingerprint::of(kv.first));if(!ok)throw std::logic_error("compaction lost capacity");}
    }
public:
    Region(const std::vector<Pair>&sorted,Config cfg):config_(cfg){
        slots_=cfg.slots;start_=sizeof(Header)+std::size_t(cfg.slots)*sizeof(Entry);
        if(cfg.slots==0||cfg.slots>MAX_SLOTS || cfg.block_bytes<=start_ || cfg.block_bytes>65535 || cfg.block_bytes%64 || cfg.load<=0 || cfg.load>=1)throw std::invalid_argument("layout config");
        if(!sorted.empty()){
            const auto&a=sorted.front().first;const auto&b=sorted.back().first;std::size_t p=0;
            while(p<std::min(a.size(),b.size())&&a[p]==b[p])++p;
            prefix_=a.substr(0,p);
        }
        model_.train(sorted,prefix_.size(),config_.model_kind);
        std::uint64_t suffix_bytes=0;for(const auto&kv:sorted)suffix_bytes+=kv.first.size()-prefix_.size();
        // All suffix bytes count in this average-capacity estimate, including long ones.
        const double count_groups=sorted.size()/(double(cfg.slots)*cfg.load);
        const double byte_groups=suffix_bytes/((cfg.block_bytes-start_)*cfg.load);
        const auto estimate=static_cast<std::uint64_t>(std::max(count_groups,byte_groups))+1;
        if(estimate>std::numeric_limits<std::uint32_t>::max())throw std::length_error("too many groups");
        groups_=cfg.fixed_groups?cfg.fixed_groups:static_cast<std::uint32_t>(std::max<std::uint64_t>(1,estimate));
        const auto bytes=std::size_t(groups_)*cfg.block_bytes;
        storage_=static_cast<std::byte*>(::operator new(bytes,std::align_val_t{64}));
        for(std::uint32_t g=0;g<groups_;++g){
            new(storage_+std::size_t(g)*cfg.block_bytes)Header{};
            header(g).bottom=static_cast<std::uint16_t>(cfg.block_bytes);
            for(std::size_t j=0;j<slots_;++j)new(entries(g)+j)Entry{};
        }
        for(const auto&kv:sorted){
            auto s=std::string_view(kv.first).substr(prefix_.size());const auto g=canonical_group(s);const auto tag=Fingerprint::of(s);
            if(!put_primary(g,s,kv.second,static_cast<std::uint8_t>(tag)))put_overflow(g,s,kv.second,static_cast<std::uint8_t>(tag));
            ++size_;
        }
    }
    ~Region(){::operator delete(storage_,std::align_val_t{64});}
    Region(const Region&)=delete;Region&operator=(const Region&)=delete;
    std::size_t size()const{return size_;}
    std::size_t overflow_count()const{return overflow_count_;}
    std::size_t prefix_length()const{return prefix_.size();}
    std::uint32_t groups()const{return groups_;}
    std::size_t slots()const{return slots_;}
    std::size_t start_offset()const{return start_;}
    const Model& model()const{return model_;}
    std::string_view residual(std::string_view key)const{return key.substr(prefix_.size());}
    bool accepts_prefix(std::string_view key)const{return prefix_matches(key);}
    std::size_t memory()const{
        std::size_t n=sizeof(*this)+model_.allocated_bytes()+prefix_.capacity()+1+std::size_t(groups_)*config_.block_bytes+overflow_owners_.capacity()*sizeof(void*);
        for(const auto&o:overflow_owners_){n+=sizeof(Overflow)+o->capacity()*sizeof(OverflowRecord);for(const auto&e:*o)n+=e.suffix.capacity()+1;}
        return n; // conservative capacity accounting; allocator metadata excluded
    }
    std::vector<GroupStats> group_stats()const{
        std::vector<GroupStats> out;out.reserve(groups_);
        for(std::uint32_t g=0;g<groups_;++g){
            const auto& h=header(g);GroupStats s;s.group=g;s.primary_entries=h.count;
            for(std::size_t i=0;i<h.count;++i)s.primary_bytes+=entries(g)[i].length;
            s.free_entries=slots_-h.count;s.free_bytes=h.bottom-start_;
            s.garbage_bytes=config_.block_bytes-h.bottom-s.primary_bytes;
            if(const auto*o=overflow(g))for(const auto&e:*o){
                ++s.overflow_entries;s.overflow_bytes+=e.suffix.size();
                const bool slots=s.free_entries==0,bytes=e.suffix.size()>s.free_bytes;
                if(slots&&bytes)++s.blocked_both;
                else if(slots)++s.blocked_slots_only;
                else if(bytes)++s.blocked_bytes_only;
                else ++s.fits_now;
                if(e.suffix.size()>config_.block_bytes-start_)++s.oversized;
            }
            out.push_back(s);
        }
        return out;
    }
    template<bool Hash>
    Result find_impl(std::string_view key,Mode mode)const{
        if(!size_||!prefix_matches(key))return {};
        if((config_.canonical==Route::Residual)!=(mode.route==Route::Residual))throw std::logic_error("incompatible mapping");
        const auto s=key.substr(prefix_.size());Fingerprint fp;
        const auto pred=model_.predict<Hash>(s,groups_,mode.route,fp);
        const auto g=pred.group;
        const std::byte* address=storage_+std::size_t(g)*config_.block_bytes;
        if(mode.prefetch==Prefetch::Early)__builtin_prefetch(address,0,3);
        std::uint8_t tag=0;
        if constexpr(Hash){fp.tail(s.substr(pred.consumed));tag=fp.tag();}
        if(mode.prefetch==Prefetch::Late)__builtin_prefetch(address,0,3);
        const auto &h=header(g);const auto *es=entries(g);
        Result result;result.steps=pred.consumed;
        auto exact=[&](std::size_t i){
            if(es[i].length==s.size() && compare(suffix(g,es[i]),s)==0){result.found=true;result.value=es[i].value;}};
#if defined(__SSE2__)
        constexpr bool has_simd=true;
#else
        constexpr bool has_simd=false;
#endif
        if constexpr(Hash && has_simd){
            if(mode.simd){
                // E5-2: batched tag filter. Mask bits are clamped to live entries, so
                // stale tag bytes beyond count can never produce a false candidate.
                const __m128i vt=_mm_set1_epi8(static_cast<char>(tag));
                for(std::size_t c=0;c*16<slots_;++c){
                    const std::size_t lo=c*16,top=std::min<std::size_t>(std::min(slots_,lo+16),h.count);
                    if(top<=lo)break;
                    const __m128i tv=_mm_load_si128(reinterpret_cast<const __m128i*>(h.tags+lo));
                    std::uint32_t mask=static_cast<std::uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(tv,vt)));
                    mask&=(1u<<unsigned(top-lo))-1u;
                    while(mask&&!result.found){exact(lo+unsigned(__builtin_ctz(mask)));mask&=mask-1;}
                    if(result.found)break;
                }
            }else{
                for(std::size_t i=0;i<h.count&&!result.found;++i){if(h.tags[i]!=tag)continue;exact(i);}
            }
        }else if constexpr(Hash){
            for(std::size_t i=0;i<h.count&&!result.found;++i){if(h.tags[i]!=tag)continue;exact(i);}
        }else{
            for(std::size_t i=0;i<h.count&&!result.found;++i)exact(i);
        }
        if(result.found)return result;
        if(auto*o=overflow(g))for(const auto&e:*o){
            ++result.overflow_checks;
            if constexpr(Hash){if(e.tag!=tag)continue;}
            if(compare(e.suffix,s)==0){result.found=true;result.value=e.value;break;}
        }
        return result;
    }
    Result find(std::string_view key,Mode mode)const{return mode.fingerprint?find_impl<true>(key,mode):find_impl<false>(key,mode);}
    bool insert(std::string_view key,std::uint64_t value){
        if(!prefix_matches(key))throw std::invalid_argument("prefix requires rebuild");
        Mode m{config_.canonical,Prefetch::Off,true,false};if(find(key,m).found)return false;
        auto s=key.substr(prefix_.size());const auto g=canonical_group(s);auto tag=Fingerprint::of(s);
        if(!put_primary(g,s,value,tag)){
            if(header(g).count<slots_ && s.size()<=config_.block_bytes-start_)compact(g);
            if(!put_primary(g,s,value,tag))put_overflow(g,s,value,tag);
        }
        ++size_;return true;
    }
    bool update(std::string_view key,std::uint64_t value){
        if(!size_||!prefix_matches(key))return false;
        auto s=key.substr(prefix_.size());const auto g=canonical_group(s);auto *es=entries(g);
        for(std::size_t i=0;i<header(g).count;++i)if(compare(suffix(g,es[i]),s)==0){es[i].value=value;return true;}
        if(auto*o=overflow(g))for(auto&e:*o)if(compare(e.suffix,s)==0){e.value=value;return true;}
        return false;
    }
    bool erase(std::string_view key){
        if(!size_||!prefix_matches(key))return false;
        auto s=key.substr(prefix_.size());const auto g=canonical_group(s);auto&h=header(g);auto*es=entries(g);
        for(std::size_t i=0;i<h.count;++i)if(compare(suffix(g,es[i]),s)==0){
            for(std::size_t j=i+1;j<h.count;++j){es[j-1]=es[j];h.tags[j-1]=h.tags[j];}--h.count;--size_;return true;
        }
        if(auto*o=overflow(g))for(auto it=o->begin();it!=o->end();++it)if(compare(it->suffix,s)==0){o->erase(it);--size_;--overflow_count_;return true;}
        return false;
    }
    void scan(std::string_view start,std::size_t limit,std::vector<Pair>&out)const{
        if(!size_||out.size()>=limit)return;
        bool matches=prefix_matches(start);
        if(!matches && compare(start,prefix_)>0)return;
        const auto s=matches?start.substr(prefix_.size()):std::string_view{};
        const auto first=matches?canonical_group(s):0;
        for(std::uint32_t g=first;g<groups_ && out.size()<limit;++g){
            const auto &h=header(g);const auto*es=entries(g);const auto*o=overflow(g);
            std::size_t a=0,b=0;
            if(g==first && matches){
                while(a<h.count && compare(suffix(g,es[a]),s)<0)++a;
                if(o)while(b<o->size() && compare((*o)[b].suffix,s)<0)++b;
            }
            while(out.size()<limit && (a<h.count || (o&&b<o->size()))){
                bool take_a=a<h.count && (!o || b==o->size() || compare(suffix(g,es[a]),(*o)[b].suffix)<0);
                if(take_a){out.emplace_back(prefix_+std::string(suffix(g,es[a])),es[a].value);++a;}
                else {out.emplace_back(prefix_+(*o)[b].suffix,(*o)[b].value);++b;}
            }
        }
    }
    std::vector<Pair> all()const{std::vector<Pair> out;out.reserve(size_);scan({},size_,out);return out;}
};

class Index {
    Config config_;
    struct Node {std::string fence;std::unique_ptr<Region> region;};
    std::vector<Node> nodes_;
    std::size_t size_=0;
    // E6-1: bumped on every fence-changing event so auxiliary routing metadata
    // (built outside the index) can detect staleness and rebuild.
    std::uint64_t structure_version_=0;
public:
    std::size_t rebuilds=0,splits=0;
    explicit Index(Config c):config_(c){}
    const std::string& fence(std::size_t i)const{return nodes_[i].fence;}
    std::uint64_t structure_version()const{return structure_version_;}
    void bulk_load(const std::vector<Pair>&pairs){
        nodes_.clear();size_=pairs.size();
        const auto chunk=config_.region_keys?config_.region_keys:std::max<std::size_t>(1,pairs.size());
        for(std::size_t p=0;p<pairs.size();p+=chunk){
            std::vector<Pair> part(pairs.begin()+p,pairs.begin()+std::min(p+chunk,pairs.size()));
            nodes_.push_back({part.front().first,std::make_unique<Region>(part,config_)});
        }
        if(nodes_.empty())nodes_.push_back({{},std::make_unique<Region>(std::vector<Pair>{},config_)});
        ++structure_version_;
    }
    std::size_t locate(std::string_view key)const{
        if(nodes_.empty())throw std::logic_error("bulk_load must initialize index");
        auto it=std::upper_bound(nodes_.begin(),nodes_.end(),key,[](std::string_view k,const Node&n){return compare(k,n.fence)<0;});
        return it==nodes_.begin()?0:static_cast<std::size_t>(it-nodes_.begin()-1);
    }
    const Region& region(std::size_t i)const{return *nodes_[i].region;}
    Result find(std::string_view key,Mode m)const{return region(locate(key)).find(key,m);}
    Result find_known(std::size_t r,std::string_view key,Mode m)const{return region(r).find(key,m);}
    std::size_t size()const{return size_;}
    std::size_t region_count()const{return nodes_.size();}
    std::size_t memory()const{std::size_t n=sizeof(*this)+nodes_.capacity()*sizeof(Node);for(const auto&x:nodes_)n+=x.fence.capacity()+1+x.region->memory();return n;}
    std::size_t overflow_count()const{std::size_t n=0;for(const auto&x:nodes_)n+=x.region->overflow_count();return n;}
    void rebuild(std::size_t i,std::vector<Pair> data){
        if(config_.region_keys && data.size()>2*config_.region_keys){
            const auto mid=data.size()/2;std::vector<Pair> left(data.begin(),data.begin()+mid),right(data.begin()+mid,data.end());
            auto a=std::make_unique<Region>(left,config_);auto b=std::make_unique<Region>(right,config_);
            const auto fence=right.front().first;
            nodes_[i].region=std::move(a);nodes_.insert(nodes_.begin()+i+1,Node{fence,std::move(b)});++splits;
        }else{auto fresh=std::make_unique<Region>(data,config_);nodes_[i].region=std::move(fresh);}
        ++rebuilds;++structure_version_;
    }
    bool insert(std::string_view key,std::uint64_t value){
        const auto i=locate(key);auto&r=*nodes_[i].region;
        if(!r.accepts_prefix(key)){
            auto data=r.all();auto it=std::lower_bound(data.begin(),data.end(),key,[](const Pair&a,std::string_view b){return compare(a.first,b)<0;});
            if(it!=data.end()&&compare(it->first,key)==0)return false;
            data.insert(it,{std::string(key),value});rebuild(i,std::move(data));++size_;return true;
        }
        if(!r.insert(key,value))return false;
        ++size_;
        if(config_.region_keys && r.size()>2*config_.region_keys)rebuild(i,r.all());
        return true;
    }
    bool update(std::string_view key,std::uint64_t v){return nodes_[locate(key)].region->update(key,v);}
    bool erase(std::string_view key){bool ok=nodes_[locate(key)].region->erase(key);if(ok)--size_;return ok;}
    bool upsert(std::string_view key,std::uint64_t value){if(update(key,value))return false;return insert(key,value);}
    void rebuild_all(){for(std::size_t i=0;i<nodes_.size();++i)rebuild(i,nodes_[i].region->all());}
    std::vector<Pair> scan(std::string_view start,std::size_t count)const{
        std::vector<Pair> out;out.reserve(std::min(count,size_));
        for(std::size_t i=locate(start);i<nodes_.size()&&out.size()<count;++i)nodes_[i].region->scan(i==locate(start)?start:std::string_view{},count,out);
        return out;
    }
};
}
