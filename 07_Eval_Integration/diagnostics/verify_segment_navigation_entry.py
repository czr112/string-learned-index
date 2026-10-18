"""Independently reconstruct C1 entrance decisions from the recorded ledger/log."""
import gzip
import hashlib
import json
from pathlib import Path
import struct
import sys

WORKSPACE=Path(__file__).resolve().parents[2]


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    out=Path(sys.argv[1]).resolve();meta=json.loads((out/'identity.json').read_text())
    for path,h in meta['sources'].items():assert sha(WORKSPACE/path)==h,('source identity',path)
    for name,h in meta['binaries'].items():assert sha(out/'build'/name)==h,('binary identity',name)
    for item in meta['inputs'].values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries_sha256']
    domain=json.loads((out/'domain.json').read_text())
    assert domain['normal']==domain['sanitized'] and domain['rc']==domain['stderr_bytes']==0
    assert domain['normal']['domain_checks']==924214
    archive=json.loads((out/'log_archive.json').read_text());assert sha(out/'commands.log.gz')==archive['gzip_sha256']
    log=gzip.decompress((out/'commands.log.gz').read_bytes());assert hashlib.sha256(log).hexdigest()==archive['raw_sha256']
    emitted=[json.loads(line) for line in log.decode().splitlines() if line.startswith('{')]
    rows=json.loads((out/'results.json').read_text());assert len(rows)==8 and len(emitted)==10
    assert emitted[0]==domain['normal'] and emitted[1]==domain['sanitized']
    assert emitted[2:]==[r['data'] for r in rows]
    expected_main={'tranco_64k_10000','tranco_64k_100000','mixed_full_10000'}
    for row in rows:
        d=row['data'];n=d['queries'];old=d['old'];flat=d['flat']
        source=WORKSPACE/meta['inputs'][row['cell']]['rows_path']
        with source.open('rb') as f:header=f.read(16)
        assert header[:8]==b'SIBROW1\0' and struct.unpack_from('<Q',header,8)[0]==d['rows']
        assert n==int(row['cell'].rsplit('_',1)[1]) and row['main']==(row['cell'] in expected_main)
        gates={'model_search_coverage':d['model_search_queries']/n>=.8,
            'navigation_comparisons':flat['steps']<=1.1*(old['route_steps']+old['model_steps']),
            'full_compare_growth':flat['full_compares']-old['route_full_compares']<=.5*n,
            'space_budget':d['navigation_bytes']<=.05*d['core_bytes']}
        assert gates==row['entry_tests'] and all(gates.values())==row['passed']
    decision=json.loads((out/'decision.json').read_text());failed=[r['cell'] for r in rows if r['main'] and not r['passed']]
    assert sorted(expected_main)==decision['main_cells'] and failed==decision['failed_main_cells']
    assert decision['entry_passed']==(not failed) and not decision['timing_performed'] and not decision['production_changed']
    assert decision['original_S3_failure_preserved'] and not (out/'raw.json').exists()
    result={'verified':True,'cells':len(rows),'domain_checks_per_build':924214,
        'frozen_queries':sum(r['data']['queries'] for r in rows),'sources':len(meta['sources']),
        'entry_passed':decision['entry_passed'],'verification_source_sha256':sha(Path(__file__))}
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
