"""Validate record completeness and summarize already completed transaction runs."""
from pathlib import Path
from collections import Counter
import json
import statistics
import hashlib
import struct
import argparse

ROOT=Path(__file__).resolve().parents[1]
BUILDS=ROOT/'builds'

def load(name):
    data=json.loads((BUILDS/name/'report.json').read_text())
    if data['status']!='complete':raise ValueError(f'incomplete run: {name}')
    return data

def performance_integrity(name,arms,cells):
    directory=BUILDS/name
    for cell in cells:
        checksums=set()
        for process in range(16):
            rows=[json.loads(x) for x in (directory/f'{cell}_p{process:02d}.jsonl').read_text().splitlines()]
            mixed=[x for x in rows if x['channel']=='mixed'];terminal=[x for x in rows if x['channel']=='terminal']
            if len(mixed)!=arms*8 or len(terminal)!=arms:raise ValueError('incomplete timing records')
            if len({(r['arm'],r['repeat']) for r in mixed})!=arms*8:raise ValueError('duplicate mixed records')
            if len({r['arm'] for r in terminal})!=arms:raise ValueError('duplicate terminal records')
            if any(r['process']!=process for r in rows):raise ValueError('wrong process identity')
            checksums.update(r['checksum'] for r in mixed)
        if len(checksums)!=1:raise ValueError('cross-process checksum mismatch')

def query_integrity(name,cells):
    for cell in cells:
        for process in range(16):
            block=json.loads((BUILDS/name/f'{cell}_p{process:02d}.json').read_text())
            rows=block['measurements']
            if len(rows)!=8 or len({(r['state'],r['repeat']) for r in rows})!=8:raise ValueError('query block incomplete')
            if any(r['group']!='mixed90' for r in rows):raise ValueError('unexpected timed group')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=BUILDS/'bm_tx_summary_20261030.json')
    args=parser.parse_args()
    acceptance=load('bm_tx_final_sanitizer_20261030')
    mixed=load('bm_tx_performance_20261030')
    q0=load('bm_tx_query_bm0_20261030');q1=load('bm_tx_query_bm1_20261030')
    point=load('bm_tx_point_20261030')
    clean=load('bm_tx_clean_acceptance_20261030')
    reads=load('bm_tx_read_allocations_v2_20261030')
    cells=sorted(acceptance['cells'])
    if len(cells)!=32 or set(cells)!=set(mixed['cells']) or set(cells)!=set(q0['results']) or set(cells)!=set(q1['results']):
        raise ValueError('unexpected cell coverage')
    if set(cells)!=set(clean['cells']) or clean['regression']!=acceptance['regression']:
        raise ValueError('clean-source acceptance changed')
    for build,records in reads['builds'].items():
        if set(records)!=set(cells) or any(v['allocations'] or v['mismatches'] or v['live_after_destroy'] for v in records.values()):
            raise ValueError(f'failed read allocation gate: {build}')
    performance_integrity('bm_tx_performance_20261030',4,cells)
    performance_integrity('bm_tx_point_20261030',5,point['cells'])
    query_integrity('bm_tx_query_bm0_20261030',cells);query_integrity('bm_tx_query_bm1_20261030',cells)
    historical=json.loads((ROOT/'07_Eval_Integration/results/bm_query_recheck_20261024/cases.json').read_text())
    for name in ('bm_tx_query_bm0_20261030','bm_tx_query_bm1_20261030'):
        actual=json.loads((BUILDS/name/'cases.json').read_text())
        for cell in cells:
            for key in ('query_sha256','final_sha256'):
                if actual[cell][key]!=historical[cell][key]:raise ValueError('historical probes/state changed')
    point_inputs={}
    for directory in ('bm_audited_v3_20261025','bm_audited_100k_v3_20261025'):
        manifest=json.loads((ROOT/'07_Eval_Integration/results'/directory/'manifest.json').read_text())
        for cell,meta in manifest.items():
            for key,hash_key in [('rows_path','rows_sha256'),('operations_path','operations_sha256')]:
                if hashlib.sha256((ROOT/meta[key]).read_bytes()).hexdigest()!=meta[hash_key]:raise ValueError('input drift after run')
            if cell in point['cells']:
                data=(ROOT/meta['operations_path']).read_bytes();position=16;filtered=[]
                while position<len(data):
                    begin=position;n=struct.unpack_from('<Q',data,position+17)[0];position+=25+n
                    if data[begin]!=5:filtered.append(data[begin:position])
                payload=data[:8]+struct.pack('<Q',len(filtered))+b''.join(filtered)
                point_inputs[cell]={'point_operations':len(filtered),'point_stream_sha256':hashlib.sha256(payload).hexdigest(),
                                    'original_operations_sha256':meta['operations_sha256']}
    def query_primary(report,cell):return report['results'][cell]['mixed90']['contrasts']['candidate_vs_bm_dynamic']
    comparisons={};per_cell={}
    for cell in cells:
        m=mixed['cells'][cell];a=acceptance['cells'][cell]
        values={'write_bm1':m['mixed']['bm1']['write'],'pure_bm1':m['mixed']['bm1']['pure'],
                'mixed_find_bm1':m['mixed']['bm1']['find'],'exact_query_bm0':query_primary(q0,cell),
                'exact_query_bm1':query_primary(q1,cell),'pure_ctx16':m['mixed']['ctx16']['pure'],
                'new_terminal_find_bm1':m['terminal']['bm1']['find'],
                'write_bm0':m['mixed']['bm0']['write'],'pure_bm0':m['mixed']['bm0']['pure']}
        per_cell[cell]={key:{k:value[k] for k in ('ratio','ci95','verdict')} for key,value in values.items()}
        per_cell[cell].update(terminal_ratio=a['terminal_ratio'],maintenance_peak_ratio=a['maintenance_peak_ratio'],
                             jointly_passed=all(values[k]['verdict']=='pass' for k in
                                 ('write_bm1','pure_bm1','mixed_find_bm1','exact_query_bm0',
                                  'exact_query_bm1','new_terminal_find_bm1')))
        for name,value in values.items():comparisons.setdefault(name,[]).append(value)
    summary={}
    for name,values in comparisons.items():
        summary[name]={'verdicts':dict(Counter(v['verdict'] for v in values)),
                       'ratio_range':[min(v['ratio'] for v in values),max(v['ratio'] for v in values)],
                       'median_ratio':statistics.median(v['ratio'] for v in values)}
    result={'status':'complete','record_integrity':'passed','comparison_intervals':'pointwise, not simultaneous',
            'cells':per_cell,'summary':summary,
            'semantic_checks':sum(v['differential'][0]['checks'] for v in acceptance['cells'].values()),
            'allocation_faults_final_sanitizer':sum(v['injections'] for rows in acceptance['fault'].values() for v in rows),
            'clean_source_revision':'10e0494',
            'clean_source_semantic_checks':sum(v['differential'][0]['checks'] for v in clean['cells'].values()),
            'read_allocations':{k:{'cells':len(v),'queries':sum(x['queries'] for x in v.values()),
                                   'allocations':sum(x['allocations'] for x in v.values())} for k,v in reads['builds'].items()},
            'terminal_space_range':[min(v['terminal_ratio'] for v in acceptance['cells'].values()),max(v['terminal_ratio'] for v in acceptance['cells'].values())],
            'maintenance_peak_range':[min(v['maintenance_peak_ratio'] for v in acceptance['cells'].values()),max(v['maintenance_peak_ratio'] for v in acceptance['cells'].values())],
            'joint_pass_cells':[c for c,v in per_cell.items() if v['jointly_passed']],
            'historical_query_identity':'32/32 query and final-state SHA matched for both direct controls',
            'point_inputs':point_inputs,'point':point['cells']}
    output=args.output
    if output.exists():raise ValueError('refusing to overwrite summary')
    output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('cells','point')},indent=2))

if __name__=='__main__':main()
