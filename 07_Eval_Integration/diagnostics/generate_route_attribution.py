"""Copy frozen F3 without changing object fields; add diagnostic access/methods."""
from pathlib import Path

HERE=Path(__file__).resolve().parent
page=(HERE/'slotted_page_core.hpp').read_text(encoding='utf-8')
assert page.count('namespace slotted_page {')==1
page=page.replace('namespace slotted_page {','namespace route_audit_leaf {').replace('} // namespace slotted_page','} // namespace route_audit_leaf')
signature='    template<Lookup Policy> Location locate_blocks('
start=page.index(signature);end=page.index('    // Independent byte-fence reference',start)
method=page[start:end]
assert method.count('const auto ri=locate(key);')==1
single=method.replace('Location locate_blocks(', 'Location locate_blocks_single(').replace('const auto ri=locate(key);','constexpr std::size_t ri=0;')
addition=single+'''    std::optional<std::uint64_t> find_single(std::string_view key)const {
        return verify_range(key,locate_blocks_single<Lookup::learned>(key),nullptr);
    }
'''
marker='\n};\n} // namespace route_audit_leaf'
assert page.count(marker)==1
page=page.replace(marker,'\npublic:\n'+addition+marker)
(HERE/'route_attribution_page.hpp').write_text(page,encoding='utf-8',newline='\n')

index=(HERE/'slotted_page_index.hpp').read_text(encoding='utf-8')
assert index.count('#include "slotted_page_core.hpp"')==1
index=index.replace('#include "slotted_page_core.hpp"','#include "route_attribution_page.hpp"')
index=index.replace('namespace slotted_page {','''namespace route_audit {
using route_audit_leaf::Page;
using route_audit_leaf::Row;
using route_audit_leaf::compare;
using route_audit_leaf::string_heap;
''').replace('} // namespace slotted_page','} // namespace route_audit')
addition='''
public:
    struct Binding {std::string lower;const Page* page;};
    std::vector<Binding> bindings()const {
        std::vector<Binding> result;result.reserve(pages_.size());
        for(const auto& item:pages_)result.push_back({item.first,item.second.core.get()});
        return result;
    }
    const Page* tree_page(std::string_view key)const{return route(pages_,key)->second.core.get();}
'''
marker='\n};\n} // namespace route_audit'
assert index.count(marker)==1
index=index.replace(marker,addition+marker)
(HERE/'route_attribution_index.hpp').write_text(index,encoding='utf-8',newline='\n')
print('generated F4 page and index copies; added methods only, no new object fields')
