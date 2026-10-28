"""Independent maintenance gate audit; no performance inference."""
import argparse
import hashlib
import json
from pathlib import Path

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'


def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def function(source,signature):
    start=source.index(signature);begin=source.index('{',start);depth=0
    for i in range(begin,len(source)):
        if source[i]=='{':depth+=1
        if source[i]=='}':
            depth-=1
            if not depth:return source[start:i+1]
    raise AssertionError('function not closed')


def main():
    parser=argparse.ArgumentParser();parser.add_argument('out',type=Path);args=parser.parse_args();out=args.out.resolve()
    meta=read(out/'identity.json')
    for path,digest in meta['sources'].items():
        assert sha(WORKSPACE/path)==digest,('source drift',path)
        assert sha(out/'frozen_sources'/path)==digest,('archive drift',path)
    for path,digest in meta['artifacts'].items():assert sha(out/path)==digest
    for item in meta['inputs'].values():assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
    old=(ROOT/'diagnostics/slotted_page_index.hpp').read_text(encoding='utf-8')
    new=(ROOT/'diagnostics/dirty_compact_index.hpp').read_text(encoding='utf-8')
    node='struct Node {std::unique_ptr<Page> core;std::uint64_t version;};'
    assert node in old and node in new and 'using slotted_page::Page;' in new
    retained=['std::optional<std::uint64_t> find(std::string_view key)const',
              'template<class P> static auto route(P& pages,std::string_view key)',
              'bool update(std::string_view key,std::uint64_t value)',
              'std::vector<Row> scan(std::string_view lower,std::size_t limit)const']
    for signature in retained:assert function(old,signature)==function(new,signature),signature
    compact=function(new,'void compact()')
    assert 'while(!dirty_.empty())' in compact and 'pages_.begin()' not in compact
    gates=read(out/'gates.json');assert gates['normal']==gates['sanitized'] and len(gates['normal'])==4
    log=[json.loads(line) for line in (out/'commands.log').read_text(encoding='utf-8').splitlines() if line.startswith('{')]
    assert log==[row for build in gates.values() for rows in build.values() for row in rows]
    total=0;corpus=[]
    for name,rows in gates['normal'].items():
        assert rows[-1]['status']=='passed';total+=rows[-1]['checks']
        if name=='synthetic':
            record=next(r for r in rows if r['phase']=='dirty_locality')
            assert record['clean_compact_targets']==0 and record['value_updates']==1024
            assert record['one_dirty_rebuilt_rows']==2047 and record['failure_scenarios']==5 and record['adjacent_sources']==2
            locality=next(r for r in rows if r['phase']=='locality')
            assert locality['failed_rebuilds']==3 and locality['coalesces']>0 and locality['max_source_pages']<=2
        else:
            record=rows[0];assert record['phase']=='corpus' and record['rows']==meta['inputs'][name]['rows']
            assert record['compact_targets']<=record['dirty_before_compact']
            assert record['max_source_pages']<=2 and record['max_rebuild_rows']<=2049
            corpus.append({'cell':name,'dirty_before_compact':record['dirty_before_compact'],
                           'compact_targets':record['compact_targets'],'rebuilds':record['rebuilds']})
    verification=read(out/'verification.json')
    assert verification['status']=='passed' and total==sum(verification['checks_per_build'].values())
    assert not verification['production_adopted'] and not verification['read_budget_remeasured']
    result={'status':'verified','checks_per_build':total,'builds':2,'gates_per_build':4,
            'unchanged_Node_and_read_update_scan':True,'corpus_compact_work':corpus,
            'verifier_sha256':sha(Path(__file__)),'commands_log_sha256':sha(out/'commands.log')}
    (out/'independent_verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))


if __name__=='__main__':main()
