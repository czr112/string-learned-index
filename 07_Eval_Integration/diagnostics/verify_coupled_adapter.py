"""Independent F6A bridge identity and common/corpus acceptance audit; no timing."""
import argparse
import hashlib
import json
from pathlib import Path

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))
def main():
    p=argparse.ArgumentParser();p.add_argument('out',type=Path);args=p.parse_args();out=args.out.resolve()
    meta=read(out/'identity.json')
    for path,digest in meta['sources'].items():
        assert sha(WORKSPACE/path)==digest,('source drift',path)
        assert sha(out/'frozen_sources'/path)==digest,('archive drift',path)
    for path,digest in meta['artifacts'].items():assert sha(out/path)==digest
    for item in meta['inputs'].values():assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
    f5=ROOT/'results/f5_coupled_page_read_20261018'
    assert meta['f5_identity_sha256']==sha(f5/'identity.json') and meta['f5_decision_sha256']==sha(f5/'decision.json')
    assert read(f5/'independent_verification.json')['dynamic_gate_allowed']
    assert meta['core_bundle_sha256']==hashlib.sha256(''.join(sha(WORKSPACE/p) for p in meta['core_bundle_paths']).encode()).hexdigest()
    adapter=ROOT/'diagnostics/coupled_page_adapter.hpp'
    assert meta['adapter_sha256']==sha(adapter)
    source=adapter.read_text(encoding='utf-8')
    assert 'if(!parameters.empty())throw' in source and 'return built().find(key);' in source
    assert 'throw sib::UnsupportedOperation("range_sum unsupported")' in source
    assert 'm.retained_input_bytes=0;' in source and 'c.keys.max_bytes=UINT32_MAX;' in source
    accepted=[(out/('acceptance_'+kind+'.txt')).read_text(encoding='utf-8') for kind in ('normal','sanitized')]
    assert accepted[0]==accepted[1] and accepted[0].count('99/99 checks passed')==1 and 'FAIL:' not in accepted[0]
    assert meta['core_bundle_sha256'] in accepted[0] and 'insert=1 update=1 erase=1 scan=1 range_sum=0' in accepted[0]
    gates=read(out/'gates.json');assert gates['normal']==gates['sanitized'] and len(gates['normal'])==3
    log=(out/'commands.log').read_text(encoding='utf-8')
    assert log.count('99/99 checks passed')==2
    logged=[json.loads(line) for line in log.splitlines() if line.startswith('{')]
    assert logged==[row for build in gates.values() for row in build.values()]
    total=0
    for name,row in gates['normal'].items():
        assert row['status']=='passed' and row['rows']==meta['inputs'][name]['rows']
        assert row['initial_updates']==1024 and row['mixed_steps']==600 and row['mutation_calls']==2224
        assert row['root_publications']>1;total+=row['checks']
    verification=read(out/'verification.json')
    assert verification=={'status':'passed','shared_acceptance_per_build':99,'corpus_checks_per_build':total,
                          'production_registered':False,'production_adopted':False,'dynamic_timing_started':False}
    result={'status':'verified','builds':2,'shared_acceptance_per_build':99,'corpus_checks_per_build':total,
            'unchanged_F5_bundle':True,'production_adopted':False,'dynamic_timing_started':False,
            'verifier_sha256':sha(Path(__file__)),'commands_log_sha256':sha(out/'commands.log')}
    (out/'independent_verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))

if __name__=='__main__':main()
