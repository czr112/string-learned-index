#pragma once
#include "physical_locator_index.hpp"

namespace collision_refinement {
using static_physical::compare;
using static_physical::coordinate;
using static_physical::Run;
using static_physical::Segment;
using static_physical::Window;
struct Work:static_physical::Work {
    std::uint64_t family_steps=0,covered=0,local_narrow=0,parent_corrections=0,local_steps=0,prefix_bytes=0;
};
struct Result {bool used=false,correction=false;Window range;};
struct Family {
    std::uint64_t x=0;
    std::uint32_t first=0,last=0;
    std::string prefix;
    std::vector<Segment> segments;
    std::uint32_t floor()const{return first?first-1:0;}
};
class Table {
    std::vector<std::vector<Family>> regions_;
    static Segment certificate(const std::vector<Run>& runs,std::size_t begin,std::size_t end,std::uint32_t floor) {
        Segment s;s.xmin=runs[begin].x;s.xmax=runs[end].x;s.anchor=runs[begin].first;
        s.lower=begin?runs[begin-1].last:floor;s.upper=runs[end].last;
        s.slope=s.xmin==s.xmax?0:double(s.upper-s.anchor)/double(s.xmax-s.xmin);
        s.error_low=INT32_MAX;s.error_high=INT32_MIN;
        const auto record=[&](std::uint64_t x,std::uint32_t lo,std::uint32_t hi) {
            const auto p=static_cast<std::int32_t>(s.predict(x));
            s.error_low=std::min(s.error_low,static_cast<std::int32_t>(lo)-p);
            s.error_high=std::max(s.error_high,static_cast<std::int32_t>(hi)-p);
        };
        for(std::size_t i=begin;i<=end;++i) {
            record(runs[i].x,runs[i].first?runs[i].first-1:0,runs[i].last);
            const auto gap_lo=i?runs[i-1].x+1:0;
            if(gap_lo<runs[i].x) {
                const auto predecessor=i?runs[i-1].last:floor;
                record(gap_lo,predecessor,predecessor);record(runs[i].x-1,predecessor,predecessor);
            }
        }
        return s;
    }
    static Window model_window(const Family& f,std::uint64_t x,Work* work) {
        if(x>f.segments.back().xmax)return {f.last,f.last};
        std::size_t lo=0,hi=f.segments.size();
        while(lo<hi) {
            if(work){++work->model_steps;++work->local_steps;}
            const auto mid=lo+(hi-lo)/2;if(f.segments[mid].xmax<x)lo=mid+1;else hi=mid;
        }
        const auto& s=f.segments[lo];const auto p=static_cast<std::int64_t>(s.predict(x));
        return {static_cast<std::uint32_t>(std::max<std::int64_t>(f.floor(),p+s.error_low)),
            static_cast<std::uint32_t>(std::min<std::int64_t>(f.last,p+s.error_high))};
    }
public:
    Table()=default;
    template<class Count,class Fence> Table(std::size_t nr,Count count,Fence fence):regions_(nr) {
        for(std::size_t r=0;r<nr;++r) {
            std::vector<std::pair<std::uint32_t,std::uint32_t>> spans;
            const auto n=count(r);
            for(std::uint32_t first=0;first<n;) {
                auto last=first;const auto x=coordinate(fence(r,first));
                while(last+1<n&&coordinate(fence(r,last+1))==x)++last;
                if(last-first+1+(first>0)>3)spans.emplace_back(first,last);
                first=last+1;
            }
            auto& families=regions_[r];families.reserve(spans.size());
            for(const auto& span:spans) {
                Family f;f.first=span.first;f.last=span.second;f.x=coordinate(fence(r,f.first));
                const auto first=fence(r,f.first),last=fence(r,f.last);
                f.prefix=std::string(first.substr(0,static_physical::common_prefix(first,last)));
                if(f.prefix.size()<7)throw std::logic_error("wide coordinate family has short prefix");
                std::vector<Run> runs;
                for(auto b=f.first;b<=f.last;++b) {
                    const auto s=fence(r,b);
                    if(s.size()<f.prefix.size()||s.substr(0,f.prefix.size())!=f.prefix)throw std::logic_error("family prefix");
                    const auto x=coordinate(s.substr(f.prefix.size()));
                    if(!runs.empty()&&x<runs.back().x)throw std::logic_error("local coordinate order");
                    if(!runs.empty()&&x==runs.back().x)runs.back().last=b;else runs.push_back({x,b,b});
                }
                for(std::size_t begin=0;begin<runs.size();) {
                    auto end=begin;auto best=certificate(runs,begin,end,f.floor());
                    if(best.error_high-best.error_low+1<=3)while(end+1<runs.size()) {
                        auto next=certificate(runs,begin,end+1,f.floor());
                        if(next.error_high-next.error_low+1>3)break;best=next;++end;
                    }
                    f.segments.push_back(best);begin=end+1;
                }
                families.push_back(std::move(f));
            }
        }
    }
    Result locate(std::size_t ri,std::string_view suffix,std::uint64_t x,Work* work=nullptr)const {
        const auto& families=regions_[ri];
        std::size_t lo=0,hi=families.size();
        while(lo<hi) {
            if(work)++work->family_steps;
            const auto mid=lo+(hi-lo)/2;if(families[mid].x<x)lo=mid+1;else hi=mid;
        }
        if(lo==families.size()||families[lo].x!=x)return {};
        const auto& f=families[lo];if(work)++work->covered;
        const Window parent{f.floor(),f.last};
        bool match=suffix.size()>=f.prefix.size();
        if(match) {
            if(work) {
                std::size_t i=0;while(i<f.prefix.size()&&suffix[i]==f.prefix[i])++i;
                work->prefix_bytes+=i+(i<f.prefix.size());
            }
            match=compare(suffix.substr(0,f.prefix.size()),f.prefix)==0;
        }
        if(!match) {if(work)++work->parent_corrections;return {true,true,parent};}
        const auto w=model_window(f,coordinate(suffix.substr(f.prefix.size())),work);
        if(w.lo>w.hi||w.lo<parent.lo||w.hi>parent.hi)throw std::logic_error("local certificate bounds");
        if(w.hi-w.lo+1>3) {if(work)++work->parent_corrections;return {true,true,parent};}
        if(work)++work->local_narrow;return {true,false,w};
    }
    std::size_t memory()const {
        std::size_t bytes=sizeof(*this)+regions_.capacity()*sizeof(std::vector<Family>);
        for(const auto& fs:regions_) {
            bytes+=fs.capacity()*sizeof(Family);
            for(const auto& f:fs)bytes+=static_physical::string_heap(f.prefix)+f.segments.capacity()*sizeof(Segment);
        }
        return bytes;
    }
    const auto& regions()const{return regions_;}
    template<class Fence> std::size_t verify_numeric(Fence fence)const {
        std::size_t checked=0;
        for(std::size_t ri=0;ri<regions_.size();++ri)for(const auto& f:regions_[ri]) {
            std::vector<std::uint64_t> xs,probes{0,UINT64_C(0x7fffffffffffffff)};
            for(auto b=f.first;b<=f.last;++b)xs.push_back(coordinate(fence(ri,b).substr(f.prefix.size())));
            for(auto x:xs){probes.push_back(x);if(x)probes.push_back(x-1);if(x<UINT64_C(0x7fffffffffffffff))probes.push_back(x+1);}
            for(auto x:probes) {
                const auto first=std::lower_bound(xs.begin(),xs.end(),x)-xs.begin();
                const auto last=std::upper_bound(xs.begin(),xs.end(),x)-xs.begin();
                const auto low=first?f.first+first-1:f.floor(),high=last?f.first+last-1:f.floor();
                const auto w=model_window(f,x,nullptr);
                if(w.lo>low||w.hi<high||w.lo<f.floor()||w.hi>f.last)throw std::logic_error("local numeric coverage");
                ++checked;
            }
        }
        return checked;
    }
};
} // namespace collision_refinement
