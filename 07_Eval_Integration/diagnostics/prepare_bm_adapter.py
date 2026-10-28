"""Keep the accepted F6A v1 wrapper, replace only its core with actual BM."""
from pathlib import Path
ROOT=Path(__file__).resolve().parent
s=(ROOT/'coupled_page_adapter.hpp').read_text(encoding='utf-8')
s=s.replace('"coupled_page_index.hpp"','"bm_page_index.hpp"').replace('coupled_page::','bm_page::')
s=s.replace('coupled_adapter','bm_adapter').replace('make_coupled_page_index','make_bm_page_index')
s=s.replace('COUPLED_CORE_SHA256','BM_CORE_SHA256').replace('COUPLED_ADAPTER_SHA256','BM_ADAPTER_SHA256')
s=s.replace('F5 coupled page','BM physical page').replace('F5 page core','Original upstream BlockModel page core')
s=s.replace('F5 core bundle','BM core bundle').replace('F5 coupled','BM physical').replace('fixed 15/16 occupancy;',
    'fixed 15/16 occupancy, original BM fit(block_size=1,bytes=7,segments=8,max_error=1), byte-fence correction;')
(ROOT/'bm_page_adapter.hpp').write_text(s,encoding='utf-8',newline='\n')
