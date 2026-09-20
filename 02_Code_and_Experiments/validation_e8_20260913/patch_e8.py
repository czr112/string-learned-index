"""E8 one-shot patch: heterogeneous per-group capacity with offset directory.
Run once from the validation_e8_20260913 directory; asserts every anchor."""
import sys

p = 'index.hpp'
s = open(p, encoding='utf-8').read()
n0 = s

def rep(old, new):
    global s
    assert s.count(old) == 1, 'anchor not found: ' + old[:60]
    s = s.replace(old, new)

# --- Region members: per-group slots / block bytes / offsets replace scalars ---
rep("""    Config config_;
    std::size_t slots_=0,start_=0;
    std::string prefix_;""",
"""    Config config_;
    // E8: per-group capacity table. Uniform builds fill every group with the same
    // (slots, block bytes); demand builds give crowded groups more of both. Block
    // address = storage_ + off_[g] (offset directory); its cost is accounted in E8.
    std::vector<std::uint16_t> sg_;    // per-group entry-slot count (<= MAX_SLOTS)
    std::vector<std::uint32_t> bb_;    // per-group block bytes (64-aligned)
    std::vector<std::size_t> off_;     // group base offsets, size groups_+1
    std::string prefix_;""")

# --- helpers ---
rep("""    Header& header(std::uint32_t g){return *reinterpret_cast<Header*>(storage_+std::size_t(g)*config_.block_bytes);}
    const Header& header(std::uint32_t g)const{return *reinterpret_cast<const Header*>(storage_+std::size_t(g)*config_.block_bytes);}
    Entry* entries(std::uint32_t g){return reinterpret_cast<Entry*>(storage_+std::size_t(g)*config_.block_bytes+sizeof(Header));}
    const Entry* entries(std::uint32_t g)const{return reinterpret_cast<const Entry*>(storage_+std::size_t(g)*config_.block_bytes+sizeof(Header));}
    Overflow* overflow(std::uint32_t g)const{return reinterpret_cast<Overflow*>(static_cast<std::uintptr_t>(header(g).overflow_pointer));}
    std::string_view suffix(std::uint32_t g,const Entry&e)const {
        assert(e.offset>=start_ && std::uint64_t(e.offset)+e.length<=config_.block_bytes);
        return {reinterpret_cast<const char*>(storage_+std::size_t(g)*config_.block_bytes+e.offset),e.length};
    }""",
"""    std::size_t slots_of(std::uint32_t g)const{return sg_[g];}
    std::size_t start_of(std::uint32_t g)const{return sizeof(Header)+std::size_t(sg_[g])*sizeof(Entry);}
    std::byte* base_of(std::uint32_t g){return storage_+off_[g];}
    const std::byte* base_of(std::uint32_t g)const{return storage_+off_[g];}
    Header& header(std::uint32_t g){return *reinterpret_cast<Header*>(base_of(g));}
    const Header& header(std::uint32_t g)const{return *reinterpret_cast<const Header*>(base_of(g));}
    Entry* entries(std::uint32_t g){return reinterpret_cast<Entry*>(base_of(g)+sizeof(Header));}
    const Entry* entries(std::uint32_t g)const{return reinterpret_cast<const Entry*>(base_of(g)+sizeof(Header));}
    Overflow* overflow(std::uint32_t g)const{return reinterpret_cast<Overflow*>(static_cast<std::uintptr_t>(header(g).overflow_pointer));}
    std::string_view suffix(std::uint32_t g,const Entry&e)const {
        assert(e.offset>=start_of(g) && std::uint64_t(e.offset)+e.length<=bb_[g]);
        return {reinterpret_cast<const char*>(base_of(g)+e.offset),e.length};
    }""")

# --- put_primary per-group limits ---
rep("""    bool put_primary(std::uint32_t g,std::string_view s,std::uint64_t value,std::uint8_t tag){
        auto &h=header(g);
        if(h.count>=slots_ || s.size()>h.bottom-start_)return false;""",
"""    bool put_primary(std::uint32_t g,std::string_view s,std::uint64_t value,std::uint8_t tag){
        auto &h=header(g);
        if(h.count>=slots_of(g) || s.size()>h.bottom-start_of(g))return false;""")

# --- ctor: split into delegating + caps ctor ---
rep("""    Region(const std::vector<Pair>&sorted,Config cfg):config_(cfg){
        slots_=cfg.slots;start_=sizeof(Header)+std::size_t(cfg.slots)*sizeof(Entry);
        if(cfg.slots==0||cfg.slots>MAX_SLOTS || cfg.block_bytes<=start_ || cfg.block_bytes>65535 || cfg.block_bytes%64 || cfg.load<=0 || cfg.load>=1)throw std::invalid_argument("layout config");
        if(!sorted.empty()){""",
"""    Region(const std::vector<Pair>&sorted,Config cfg):Region(sorted,cfg,{},{}){}
    Region(const std::vector<Pair>&sorted,Config cfg,std::vector<std::uint16_t> sg_in,std::vector<std::uint32_t> bb_in):config_(cfg){
        if(cfg.block_bytes>65535 || cfg.block_bytes%64 || cfg.load<=0 || cfg.load>=1)throw std::invalid_argument("layout config");
        if(!sorted.empty()){""")

# --- ctor body: groups estimate without start_, capacity table, offset directory ---
rep("""        // All suffix bytes count in this average-capacity estimate, including long ones.
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
        }""",
"""        // All suffix bytes count in this average-capacity estimate, including long ones.
        const double count_groups=sorted.size()/(double(cfg.slots)*cfg.load);
        const double byte_groups=suffix_bytes/((cfg.block_bytes-sizeof(Header)-std::size_t(cfg.slots)*sizeof(Entry))*cfg.load);
        const auto estimate=static_cast<std::uint64_t>(std::max(count_groups,byte_groups))+1;
        if(estimate>std::numeric_limits<std::uint32_t>::max())throw std::length_error("too many groups");
        groups_=cfg.fixed_groups?cfg.fixed_groups:static_cast<std::uint32_t>(std::max<std::uint64_t>(1,estimate));
        // E8: per-group capacity table (uniform default or explicit demand table)
        if(sg_in.empty()){sg_in.assign(groups_,static_cast<std::uint16_t>(cfg.slots));bb_in.assign(groups_,cfg.block_bytes);}
        if(sg_in.size()!=groups_||bb_in.size()!=groups_)throw std::invalid_argument("capacity table size");
        sg_=std::move(sg_in);bb_=std::move(bb_in);
        off_.assign(std::size_t(groups_)+1,0);
        std::size_t total=0;
        for(std::uint32_t g=0;g<groups_;++g){
            const auto so=sizeof(Header)+std::size_t(sg_[g])*sizeof(Entry);
            if(sg_[g]==0||sg_[g]>MAX_SLOTS || bb_[g]%64 || bb_[g]<=so || bb_[g]>65535)throw std::invalid_argument("layout config");
            off_[g+1]=off_[g]+bb_[g];total=off_[g+1];
        }
        storage_=static_cast<std::byte*>(::operator new(total,std::align_val_t{64}));
        for(std::uint32_t g=0;g<groups_;++g){
            new(base_of(g))Header{};
            header(g).bottom=static_cast<std::uint16_t>(bb_[g]);
            for(std::size_t j=0;j<sg_[g];++j)new(entries(g)+j)Entry{};
        }""")

# --- accessors ---
rep("""    std::size_t slots()const{return slots_;}
    std::size_t start_offset()const{return start_;}""",
"""    std::size_t slots()const{return sg_.empty()?0:sg_[0];}
    std::size_t block_bytes_total()const{std::size_t t=0;for(auto b:bb_)t+=b;return t;}""")

# --- memory(): sum of per-group blocks ---
rep("""    std::size_t memory()const{
        std::size_t n=sizeof(*this)+model_.allocated_bytes()+prefix_.capacity()+1+std::size_t(groups_)*config_.block_bytes+overflow_owners_.capacity()*sizeof(void*);""",
"""    std::size_t memory()const{
        std::size_t blocks=0;for(auto b:bb_)blocks+=b;
        std::size_t n=sizeof(*this)+model_.allocated_bytes()+prefix_.capacity()+1+blocks+overflow_owners_.capacity()*sizeof(void*);""")

# --- group_stats: per-group capacities ---
rep("""            s.free_entries=slots_-h.count;s.free_bytes=h.bottom-start_;
            s.garbage_bytes=config_.block_bytes-h.bottom-s.primary_bytes;""",
"""            s.free_entries=slots_of(g)-h.count;s.free_bytes=h.bottom-start_of(g);
            s.garbage_bytes=bb_[g]-h.bottom-s.primary_bytes;""")
rep("""                if(e.suffix.size()>config_.block_bytes-start_)++s.oversized;""",
"""                if(e.suffix.size()>bb_[g]-start_of(g))++s.oversized;""")

# --- find_impl: base_of + per-group slot clamp ---
rep("""        const auto g=pred.group;
        const std::byte* address=storage_+std::size_t(g)*config_.block_bytes;""",
"""        const auto g=pred.group;
        const std::byte* address=base_of(g);""")
rep("""                for(std::size_t c=0;c*16<slots_;++c){
                    const std::size_t lo=c*16,top=std::min<std::size_t>(std::min(slots_,lo+16),h.count);""",
"""                for(std::size_t c=0;c*16<slots_of(g);++c){
                    const std::size_t lo=c*16,top=std::min<std::size_t>(std::min(slots_of(g),lo+16),h.count);""")

# --- insert: compact decision per-group ---
rep("""            if(header(g).count<slots_ && s.size()<=config_.block_bytes-start_)compact(g);""",
"""            if(header(g).count<slots_of(g) && s.size()<=bb_[g]-start_of(g))compact(g);""")

# --- Index: rebuild_with_caps (non-split) for E8 demand rebuilds ---
rep("""    void rebuild(std::size_t i,std::vector<Pair> data){""",
"""    // E8: replace a region with an explicit per-group capacity table (no split;
    // caller guarantees data fits the region size). Falls back nothing: caps only
    // change capacity, never semantics.
    void rebuild_with_caps(std::size_t i,std::vector<Pair> data,
                           std::vector<std::uint16_t> sg,std::vector<std::uint32_t> bb){
        nodes_[i].region=std::make_unique<Region>(data,config_,std::move(sg),std::move(bb));
        ++rebuilds;++structure_version_;
    }
    void rebuild(std::size_t i,std::vector<Pair> data){""")

open(p, 'w', encoding='utf-8').write(s)
print('patched OK, delta bytes:', len(s) - len(n0))
