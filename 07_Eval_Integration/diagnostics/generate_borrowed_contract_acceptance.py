"""Keep every bulk-load input alive for the entire shared suite; assertions stay byte-identical."""
from pathlib import Path
import re

HERE=Path(__file__).resolve().parent


def rendered():
    source=(HERE.parent/'adapters/common/contract_acceptance.cpp').read_text(encoding='utf-8')
    helper='''
#include <deque>
// The contract permits borrowing the vector object and its bytes. The original tests sometimes
// passed a temporary rows_of(...), or declared a vector after its index. Retain test inputs past
// all index destructors. This registry is test-owned, never part of an index or timing binary.
static const std::vector<string_index_bench::v1::Row>& hold_bulk_rows(
    std::vector<string_index_bench::v1::Row> rows) {
    static std::deque<std::vector<string_index_bench::v1::Row>> held;
    held.push_back(std::move(rows));return held.back();
}
'''
    marker='namespace sib = string_index_bench::v1;'
    assert source.count(marker)==1
    source=source.replace(marker,helper+'\n'+marker)
    source,count=re.subn(r'->bulk_load\(([^;]*)\);',r'->bulk_load(hold_bulk_rows(\1));',source)
    assert count==18,count
    return source


if __name__=='__main__':
    (HERE/'borrowed_contract_acceptance.cpp').write_text(rendered(),encoding='utf-8',newline='\n')
