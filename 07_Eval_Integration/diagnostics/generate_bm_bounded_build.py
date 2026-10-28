"""Generate a constructor-only variant, preserving the baseline files and every other Index method."""
from pathlib import Path
import hashlib

DIAG=Path(__file__).resolve().parent
BASE_SHA='c3e54dc2638e0ba1f5f71a205b1f29dab950d57dc4bec2974ab88f70da87f082'

def index_rendered():
    s=(DIAG/'bm_page_index.hpp').read_text(encoding='utf-8')
    assert hashlib.sha256((DIAG/'bm_page_index.hpp').read_bytes()).hexdigest()==BASE_SHA
    start=s.index('    explicit Index(const std::vector<Row>& rows={}')
    end=s.index('    Index(const Index&)=delete;',start)
    original=s[start:end]
    new=original.replace('    explicit Index(const std::vector<Row>& rows={},',
        '    Index():Index(std::vector<Row>{}){}\n    template<class InputRow>\n    explicit Index(const std::vector<InputRow>& rows,')
    old='            std::vector<Row> part(rows.begin()+begin,rows.begin()+end);'
    assert new.count(old)==1
    new=new.replace(old,'            std::vector<Row> part;part.reserve(end-begin);\n'
        '            for(std::size_t i=begin;i<end;++i)part.push_back({rows[i].key,rows[i].value});')
    s=s[:start]+new+s[end:]
    s=s.replace('namespace bm_page {','namespace bm_bounded_page {\n'
        'using bm_page::Row;using bm_page::Page;using bm_page::compare;using bm_page::string_heap;\n'
        'using bm_page::FlatRoot;using bm_page::Binding;')
    # The old Index is included too so every comparison can instantiate both structures.
    s=s.replace('#include "bm_page_root.hpp"','#include "bm_page_index.hpp"')
    return s.replace('} // namespace bm_page','} // namespace bm_bounded_page')

def adapter_rendered():
    s=(DIAG/'bm_space_adapter.hpp').read_text(encoding='utf-8')
    s=s.replace('#include "bm_page_index.hpp"','#include "bm_bounded_index.hpp"')
    s=s.replace('namespace bm_space {','namespace bm_bounded_space {').replace('bm_page::Index','bm_bounded_page::Index')
    start=s.index('        std::vector<static_physical::Row> physical;')
    end=s.index('\n    }',start)
    s=s[:start]+'        index_=std::make_unique<bm_bounded_page::Index>(rows);'+s[end:]
    s=s.replace('BM-page-base','BM-page-bounded-build').replace('bm_space_adapter.hpp','bm_bounded_adapter.hpp')
    s=s.replace('bulk_load copies the complete contract-row vector before per-page construction.',
        'bulk_load converts only the current page, at most 2048 rows; no full input copy.')
    s=s.replace('// Measurement bridge only: preserve the historical PageArm bulk-load copy and find_base path.',
        '// Measurement bridge: bounded bulk conversion; identical Page, root and find_base.')
    return s

LAYOUT=r'''
static int layout_gate(const char* rp,const char* qp) {
    auto rs=static_blocks::buffered(rp),qs=static_blocks::buffered(qp);
    const auto rows=uni::read_rows(rs);auto queries=uni::read_queries(qs);
    std::vector<bm_page::Row> copied;copied.reserve(rows.size());
    for(const auto& r:rows)copied.push_back({r.key,r.value});
    bm_page::Index baseline(copied);bm_bounded_page::Index bounded(rows);
    std::uint64_t layout_checks=0;
    auto eq=[&](bool b){require(b,"bounded constructor layout drift");++layout_checks;};
    eq(sizeof(baseline)==sizeof(bounded));eq(baseline.size()==bounded.size());
    eq(baseline.page_count()==bounded.page_count());eq(baseline.memory_bytes()==bounded.memory_bytes());
    eq(baseline.root_memory_bytes()==bounded.root_memory_bytes());
    eq(baseline.bm_segment_count()==bounded.bm_segment_count());
    eq(baseline.bm_certified_pages()==bounded.bm_certified_pages());
    const auto a=baseline.page_states();const auto b=bounded.page_states();eq(a.size()==b.size());
    for(std::size_t i=0;i<a.size();++i)eq(a[i].lower==b[i].lower&&a[i].rows==b[i].rows&&a[i].version==b[i].version);
    eq(baseline.fence_keys()==bounded.fence_keys());
    const auto ca=baseline.verify(),cb=bounded.verify();eq(ca==cb);layout_checks+=ca+cb;
    std::map<std::string,std::uint64_t> oracle;for(const auto& r:rows)oracle.emplace(r.key,r.value);
    for(const auto& r:rows)eq(baseline.find_base(r.key)==bounded.find_base(r.key));
    for(std::size_t i=0;i<queries.size();++i) {
        auto& q=queries[i];if(i%10==9){q.key+="~f2/miss";while(oracle.count(q.key))q.key+="~";}
        eq(baseline.find_base(q.key)==bounded.find_base(q.key));
    }
    std::cout<<"{\"layout_checks\":"<<layout_checks<<",\"rows\":"<<rows.size()
        <<",\"queries\":"<<queries.size()<<",\"pages\":"<<baseline.page_count()
        <<",\"logical_bytes\":"<<baseline.memory_bytes()<<",\"same_fences\":true,\"same_models\":true}\n";
    return 0;
}
int main(int argc,char** argv) {
    try {if(argc==4&&std::string(argv[1])=="--layout")return layout_gate(argv[2],argv[3]);
        return bounded_ledger_main(argc,argv);
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
'''

def ledger_rendered():
    s=(DIAG/'ledger_bm_space.cpp').read_text(encoding='utf-8')
    s='#include "bm_bounded_adapter.hpp"\n'+s
    s=s.replace('int main(int argc,char** argv)','int bounded_ledger_main(int argc,char** argv)')
    # A renamed main loses C++'s implicit successful return. Keep failure handling unchanged.
    s=s.replace('        std::cout<<out.str();','        std::cout<<out.str();return 0;')
    s=s.replace('if(arm=="bm_base")space_selected=bm_space::make_base;',
        'if(arm=="bm_bounded")space_selected=bm_bounded_space::make_base;\n'
        '        else if(arm=="bm_base")space_selected=bm_space::make_base;')
    s=s.replace('if(arm=="bm_base")require(', 'if(arm=="bm_base"||arm=="bm_bounded")require(')
    return s+LAYOUT

if __name__=='__main__':
    for name,render in [('bm_bounded_index.hpp',index_rendered),('bm_bounded_adapter.hpp',adapter_rendered),('ledger_bm_bounded.cpp',ledger_rendered)]:
        (DIAG/name).write_text(render(),encoding='utf-8',newline='\n')
