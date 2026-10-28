#include "allocation_trace.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>

extern "C" int allocation_shared_fixture();
namespace at=allocation_trace;
static unsigned checks=0;
static void require(bool b){++checks;if(!b)throw std::runtime_error("allocation fixture mismatch");}
static at::Totals now(){return at::snapshot().domains[at::test];}
static void clean(){at::end();const auto t=now();require(t.requested==0&&t.usable==0&&t.objects==0);require(t.total_requested==t.total_freed);at::reset_test();}
int main() {
    try {
        at::begin(at::test);
        auto a=static_cast<unsigned char*>(std::malloc(37));auto b=std::calloc(4,13);
        require(a&&b);require(now().requested==89&&now().objects==2);
        for(unsigned i=0;i<52;++i)require(static_cast<unsigned char*>(b)[i]==0);
        std::memset(a,0x5a,37);a=static_cast<unsigned char*>(std::realloc(a,89));
        require(a&&now().requested==141);require(now().kinds[at::c_realloc]==1);
        for(unsigned i=0;i<37;++i)require(a[i]==0x5a);
        volatile std::size_t huge=SIZE_MAX-4095;errno=0;auto failed=std::realloc(a,huge);
        require(!failed&&errno==ENOMEM&&now().requested==141&&now().objects==2);
        require(a[0]==0x5a);std::free(a);std::free(b);clean();

        at::begin(at::test);auto z=std::malloc(0);require(z&&now().objects==1&&now().requested==0);
        std::free(z);auto p=std::malloc(43);require(p);auto rz=std::realloc(p,0);
        require(!rz&&now().objects==0);clean();

        at::begin(at::test);
        auto n=::operator new(73);auto na=::operator new[](91);
        auto nt=::operator new(101,std::nothrow);auto nat=::operator new[](113,std::nothrow);
        require(now().requested==378);require(now().kinds[at::cpp_new]==2&&now().kinds[at::cpp_array]==2);
        ::operator delete(n,std::size_t(73));::operator delete[](na,std::size_t(91));
        ::operator delete(nt,std::nothrow);::operator delete[](nat,std::nothrow);clean();

        at::begin(at::test);
        auto x=::operator new(81,std::align_val_t(64));auto y=::operator new[](97,std::align_val_t(128));
        auto xn=::operator new(117,std::align_val_t(64),std::nothrow);
        auto yn=::operator new[](131,std::align_val_t(128),std::nothrow);
        require(reinterpret_cast<std::uintptr_t>(x)%64==0&&reinterpret_cast<std::uintptr_t>(y)%128==0);
        require(now().requested==426);require(now().kinds[at::cpp_aligned]==2&&now().kinds[at::cpp_aligned_array]==2);
        ::operator delete(x,std::size_t(81),std::align_val_t(64));::operator delete[](y,std::size_t(97),std::align_val_t(128));
        ::operator delete(xn,std::align_val_t(64),std::nothrow);::operator delete[](yn,std::align_val_t(128),std::nothrow);clean();

        at::begin(at::test);require(allocation_shared_fixture()==1);
        require(now().total_requested==257&&now().allocations==3&&now().peak_requested==196);
        require(now().kinds[at::c_malloc]==1&&now().kinds[at::c_calloc]==1&&now().kinds[at::c_realloc]==1);clean();

        at::begin(at::test);
        {std::ostringstream stream;stream<<std::string(4096,'a');require(stream.str().size()==4096);require(now().allocations>=2);}
        clean();
        at::begin(at::test);auto old=std::malloc(71);at::end();std::free(old);require(now().objects==0);clean();
        const auto state=at::snapshot();require(state.registry_live==0);
        std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"registry_bytes\":"<<state.registry_bytes
            <<",\"shared_c_library\":true,\"libstdcxx\":true,\"realloc_failure_preserved\":true,\"released_all\":true}\n";
    }catch(const std::exception& e){at::end();std::cerr<<e.what()<<'\n';return 1;}
}
