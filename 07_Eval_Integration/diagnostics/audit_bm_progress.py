"""Read-only audit of separately recorded BM bracket/short-directory results."""
import hashlib
import json
from pathlib import Path

root=Path(__file__).resolve().parents[1];workspace=root.parent
def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
result={'scope':'existing results audit only; no benchmark rerun or original record mutation','experiments':{}}
for name in ('bm_bracket_entry_20261018','bm_shortdir_20261018'):
    out=root/'results'/name;meta=read(out/'identity.json');drift=[]
    for path,digest in meta['sources'].items():
        if sha(workspace/path)!=digest:drift.append(path)
    artifacts={path:sha(out/path)==digest for path,digest in meta['artifacts'].items()}
    assert all(artifacts.values())
    for item in meta['inputs'].values():
        assert sha(workspace/item['rows_path'])==item['rows_sha256']
        assert sha(workspace/item['queries_path'])==item['queries_sha256']
    result['experiments'][name]={'source_differences':drift,'artifacts_match':True,'inputs_match':True,
                                 'recorded_verification':read(out/'verification.json'),
                                 'decision':read(out/'decision.json')}
short=root/'results/bm_shortdir_20261018'
comparisons=[]
for row in read(short/'counts.json'):
    full_current=row['current']['fence_compares']+row['current']['directory_steps']
    full_short=row['short']['fence_compares']+row['short']['fallback_steps']
    comparisons.append({'cell':row['cell'],'full_fence_current':full_current,'full_fence_short':full_short,
                        'full_fence_ratio':full_short/full_current,
                        'published_boundary_field_ratio':row['short']['fence_compares']/row['current']['fence_compares']})
result['short_directory_audit']={
    'full_fence_counts':comparisons,
    'memory_missing_from_reported_code_bytes':['fence_views_ vector capacity','short_codes_ counted-replica vector capacity','short_prefix_ heap capacity if used'],
    'memory_included_object_delta':'sizeof(Page) delta already appears in memory.copy; it does not cover those dynamic arrays',
    'tranco64_implementation_lower_bound':{'pages':32,'reported_code_array_bytes':35072,
        'fence_views_minimum_bytes':32*137*16,'counted_replica_codes_minimum_bytes':32*137*8,
        'object_delta_bytes':4352,'extra_at_least_bytes':32*137*16+2*35072+4352,
        'notes':'minimum uses capacity >= 137 elements per full page, without assuming a vector growth factor; excludes any heap prefix and allocator metadata; not a complete heap measurement'},
    'scope':'timing includes page.find_model/find_short full-key verification, with root route precomputed; not complete index find',
    'max_error_behavior':'block_model.cpp uses max_error only for the certified flag; lowering it does not tighten fitted radii'}
target=root/'results/bm_progress_audit_20261018';target.mkdir(exist_ok=True)
result['audit_script_sha256']=sha(Path(__file__))
result['revision_note']='v1 preserved; v2 uses a guaranteed minimum capacity instead of a compiler-specific growth estimate'
(target/'audit.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'artifacts_and_inputs_match':True,'source_differences':{n:v['source_differences'] for n,v in result['experiments'].items()},
                  'full_fence_ratios':{r['cell']:r['full_fence_ratio'] for r in comparisons}}))
