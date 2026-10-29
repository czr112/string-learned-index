#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace
python3 - <<'PY'
import pathlib
p = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics/bm_n1_page_core.hpp')
t = p.read_text(encoding='utf-8')
if 'block_for' not in t:
    anchor = '    struct SplitResult {'
    add = ('    std::uint32_t block_for(std::string_view key)const {\n'
           '        if(regions_.empty())throw std::logic_error("empty page");\n'
           '        const auto& r=regions_[0];\n'
           '        return directory_block(r,key.substr(r.prefix.size()),nullptr);\n'
           '    }\n')
    assert anchor in t
    p.write_text(t.replace(anchor, add + anchor, 1), encoding='utf-8')
    print('block_for added')
else:
    print('block_for present')
PY
bash "$W/tools/g2d_counts.sh"
