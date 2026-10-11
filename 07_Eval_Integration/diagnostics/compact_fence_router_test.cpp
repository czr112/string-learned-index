#include "compact_fence_router.h"
#include <iostream>
#include <random>
#include <string>

static bool less_bytes(std::string_view a,std::string_view b) {
    return std::lexicographical_compare(a.begin(),a.end(),b.begin(),b.end(),
        [](char x,char y){return static_cast<unsigned char>(x)<static_cast<unsigned char>(y);});
}

int main() {
    std::mt19937_64 random(20261009);
    std::uint64_t checked=0;
    for(int trial=0;trial<250;++trial) {
        std::vector<std::string> keys;
        const std::string common=trial%3==0?std::string(20,'x'):"";
        const auto make_key=[&] {
            std::string key=common;
            const auto length=random()%41;
            for(std::size_t i=0;i<length;++i) key.push_back(static_cast<char>(random()%256));
            return key;
        };
        for(std::size_t i=0, n=1+random()%128;i<n;++i) keys.push_back(make_key());
        std::sort(keys.begin(),keys.end(),[](const auto& a,const auto& b){return less_bytes(a,b);});
        keys.erase(std::unique(keys.begin(),keys.end()),keys.end());
        std::vector<std::string_view> views;for(const auto& key:keys) views.push_back(key);
        CompactFenceRouter router(views);
        const auto check=[&](std::string_view key) {
            const auto it=std::upper_bound(views.begin(),views.end(),key,
                [](auto q,auto fence){return less_bytes(q,fence);});
            const auto expected=it==views.begin()?0:std::size_t(it-views.begin()-1);
            if(router.locate(key)!=expected) throw std::runtime_error("unsigned-byte routing mismatch");
            ++checked;
        };
        check("");check(std::string(80,char(255)));
        for(const auto& key:keys) {
            check(key);
            for(std::size_t length=0;length<key.size();++length) check(std::string_view(key).substr(0,length));
            check(key+std::string(8,'\0'));check(key+std::string(8,char(255)));
        }
        for(int i=0;i<300;++i) check(make_key());
    }
    bool rejected=false;
    try{CompactFenceRouter router({"z","a"});}catch(const std::invalid_argument&){rejected=true;}
    if(!rejected) throw std::runtime_error("unordered fences accepted");
    std::cout << "route_checks=" << checked << ", passed\n";
}
