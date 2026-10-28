"""Independent F4 2x2 audit, including exact normalization back to frozen F3."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'
BASE=['tree_wrapped','tree_single','flat_wrapped','flat_single']
ARMS=[x for a in BASE for x in (a,a+'_AA')]
CONTRASTS=['flat_wrapped/tree_wrapped','tree_single/tree_wrapped','flat_single/flat_wrapped','flat_single/tree_wrapped']


def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def log_records(path):return [json.loads(s) for s in path.read_text(encoding='utf-8').splitlines() if s.startswith('{')]


def source_proof():
    here=ROOT/'diagnostics'
    old_page=(here/'slotted_page_core.hpp').read_text(encoding='utf-8')
    page=(here/'route_attribution_page.hpp').read_text(encoding='utf-8')
    start=page.index('\npublic:\n    template<Lookup Policy> Location locate_blocks_single(')
    end=page.index('\n};\n} // namespace route_audit_leaf',start)
    normalized=page[:start]+page[end:]
    normalized=normalized.replace('namespace route_audit_leaf {','namespace slotted_page {').replace('} // namespace route_audit_leaf','} // namespace slotted_page')
    assert normalized==old_page,'Page changed beyond added methods'
    begin=old_page.index('    template<Lookup Policy> Location locate_blocks(')
    finish=old_page.index('    // Independent byte-fence reference',begin)
    expected=old_page[begin:finish].replace('Location locate_blocks(', 'Location locate_blocks_single(').replace('const auto ri=locate(key);','constexpr std::size_t ri=0;')
    added=page[start+len('\npublic:\n'):end]
    assert added==expected+'''    std::optional<std::uint64_t> find_single(std::string_view key)const {
        return verify_range(key,locate_blocks_single<Lookup::learned>(key),nullptr);
    }
'''
    index=(here/'route_attribution_index.hpp').read_text(encoding='utf-8')
    start=index.index('\npublic:\n    struct Binding ');end=index.index('\n};\n} // namespace route_audit',start)
    normalized=index[:start]+index[end:]
    alias='''namespace route_audit {
using route_audit_leaf::Page;
using route_audit_leaf::Row;
using route_audit_leaf::compare;
using route_audit_leaf::string_heap;
'''
    normalized=normalized.replace(alias,'namespace slotted_page {').replace('} // namespace route_audit','} // namespace slotted_page')
    normalized=normalized.replace('#include "route_attribution_page.hpp"','#include "slotted_page_core.hpp"')
    assert normalized==(here/'slotted_page_index.hpp').read_text(encoding='utf-8'),'Index changed beyond diagnostic access'


def main():
    parser=argparse.ArgumentParser();parser.add_argument('out',type=Path);args=parser.parse_args();out=args.out.resolve()
    meta=read(out/'identity.json')
    for path,digest in meta['sources'].items():
        assert sha(WORKSPACE/path)==digest,('source',path)
        assert sha(out/'frozen_sources'/path)==digest,('archived source',path)
    for path,digest in meta['artifacts'].items():assert sha(out/path)==digest
    for item in meta['inputs'].values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
    assert meta['f3_identity_sha256']==sha(ROOT/'results/f3_slotted_page_read_20261018/identity.json')
    source_proof()
    domain=read(out/'domain.json');assert domain['normal']==domain['sanitized'] and domain['normal'][0]['status']=='passed'
    gates=read(out/'gates.json');raw=read(out/'raw.json');summary=read(out/'summary.json');logged=log_records(out/'commands.log')
    assert len(gates)==len(summary)==4 and len(raw)==1280
    assert [r for r in logged if r['phase']=='domain']==domain['normal']+domain['sanitized']
    assert [r for r in logged if r['phase']=='pass']==[{k:v for k,v in r.items() if k not in ('cell','process')} for r in raw]
    expected=[{k:v for k,v in g.items() if k!='cell'} for g in gates]
    assert [r for r in logged if r['phase']=='gate']==[g for g in expected for _ in range(2)]+[g for g in expected for _ in range(5)]
    for gate in gates:
        assert gate['rebuilds']==0
        for stat in gate['counter'].values():
            assert abs(stat['mean']-stat['total']/gate['queries'])<=1e-5*max(1,abs(stat['mean']))
            assert stat['median']<=stat['p95']
    for recorded in summary:
        cell=recorded['cell'];rows=[r for r in raw if r['cell']==cell];assert len(rows)==320
        gate=next(g for g in gates if g['cell']==cell)
        assert all(r['operations']==gate['queries'] and r['elapsed_ns']>0 for r in rows)
        pairs={name:[] for name in CONTRASTS+[a+'_AA/'+a for a in BASE]}
        for process in range(5):
            block=[r for r in rows if r['process']==process];assert len(block)==64
            t={(r['repeat'],r['arm']):r['elapsed_ns'] for r in block};assert len(t)==64
            for repeat in range(8):
                seq=sorted((r for r in block if r['repeat']==repeat),key=lambda r:r['position'])
                assert [r['position'] for r in seq]==list(range(8))
                assert [r['arm'] for r in seq]==[ARMS[(p+repeat+process)%8] for p in [0,1,7,2,6,3,5,4]]
                assert len({r['checksum'] for r in seq})==1
            for arm in ARMS:assert sorted(r['position'] for r in block if r['arm']==arm)==list(range(8))
            for name in pairs:
                a,b=name.split('/');pairs[name].append(statistics.median(t[r,a]/t[r,b] for r in range(8)))
        bands={a:max(max(x,1/x) for x in pairs[a+'_AA/'+a]) for a in BASE}
        assert bands==recorded['aa_bands']
        for name,points in pairs.items():
            a,b=name.split('/');band=max(bands[a.replace('_AA','')],bands[b]);r=recorded['ratios'][name]
            ratio=statistics.median(points);faster=sum(x<1 for x in points);slower=sum(x>1 for x in points)
            assert r['median']==ratio and r['process_medians']==points and r['band']==band and r['faster']==faster and r['slower']==slower
            assert r['clear_gain_10pct']==(ratio<=.90 and faster>=4 and 1/ratio>band)
            assert r['clear_regression_5pct']==(ratio>1.05 and slower>=4 and ratio>band)
        for arm,ns in recorded['ns_per_op'].items():assert ns==statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==arm)
    decision=read(out/'decision.json');mechanisms={}
    for mechanism,contrast in [('root',CONTRASTS[0]),('single_page',CONTRASTS[1]),('combined',CONTRASTS[3])]:
        main=[r for r in summary if r['cell'].startswith('tranco64')];risks=[r for r in summary if not r['cell'].startswith('tranco64')]
        both=all(r['ratios'][contrast]['clear_gain_10pct'] for r in main)
        regressions=[r['cell'] for r in risks if r['ratios'][contrast]['clear_regression_5pct']]
        mechanisms[mechanism]={'both_main_gains':both,'risk_regressions':regressions,'entry_passed':both and not regressions}
    assert mechanisms==decision['mechanisms']
    selected='root' if mechanisms['root']['entry_passed'] else 'single_page' if mechanisms['single_page']['entry_passed'] else None
    assert selected==decision['selected_single_mechanism'] and not decision['production_adopted'] and not decision['dynamic_topology_validated']
    result={'status':'verified','domain_checks_per_build':domain['normal'][0]['checks'],
            'corpus_checks_per_build':sum(g['checks'] for g in gates),'timing_rows':len(raw),'cells':len(gates),
            'source_field_identity_proved':True,'selected_single_mechanism':selected,
            'verifier_sha256':sha(Path(__file__)),'commands_log_sha256':sha(out/'commands.log')}
    (out/'independent_verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))


if __name__=='__main__':main()
