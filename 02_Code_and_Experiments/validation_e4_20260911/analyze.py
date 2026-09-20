"""Audit complete E4 records, resolve shared policy aliases, summarize batches."""
from pathlib import Path
import argparse,collections,csv,hashlib,json,statistics
root=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--input',type=Path,default=root/'results/main');a=p.parse_args()
summary={}
for d in sorted(a.input.iterdir()):
    if not d.is_dir():continue
    f=json.loads((d/'frozen.json').read_text());done=json.loads((d/'complete.json').read_text());run=json.loads((d/'run.json').read_text())
    for name,want in run['source_sha256'].items():
        if hashlib.sha256((root.parent/name).read_bytes()).hexdigest()!=want:raise RuntimeError('source changed after run: '+name)
    rows=list(csv.DictReader((d/'timings.csv').open()));groups=collections.defaultdict(list)
    if len(rows)!=done['rows']:raise RuntimeError('row count mismatch')
    for r in rows:groups[r['method']].append(r)
    methods={}
    for name,rr in groups.items():
        if len(rr)!=15 or len({(r['query_seed'],r['repeat']) for r in rr})!=15:raise RuntimeError('missing/repeated timing')
        byseed=collections.defaultdict(list)
        for r in rr:byseed[r['query_seed']].append(float(r['ns_per_op']))
        times=[float(r['ns_per_op']) for r in rr]
        methods[name]={'median_ns':statistics.median(times),'min_batch_ns':min(times),'max_batch_ns':max(times),'seed_median_ns':{s:statistics.median(v) for s,v in byseed.items()},'bytes_estimate':int(rr[0]['bytes_estimate']) if rr[0]['bytes_known']=='1' else None,'build_ms':float(rr[0]['build_ms'])}
    aliases={name:methods[target] for name,target in f['aliases'].items() if target in methods}
    chosen=f['aliases']['validated_policy'];uniform=f['aliases']['validated_uniform'];chosen_ns=methods[chosen]['median_ns']
    selected={x['name']:x for x in f['policies']}
    cand=list(csv.DictReader((d/'candidates.csv').open()));matrix={(int(r['region']),int(r['candidate'])):r for r in cand};nr=(f['n']+2047)//2048
    weights=[int(matrix[(i,0)]['routed_queries']) for i in range(nr)];total=sum(weights)
    policy_detail={}
    for name in ['default','warm_uniform','cross_uniform','warm_joint','cross_joint','warm_hybrid','cross_hybrid','validated_uniform','validated_policy','sorted_leaves']:
        s=selected[f['aliases'][name]];ids=s['choices']
        if name!='sorted_leaves' and s['leaf_bytes']>f['budget_leaf_bytes']:raise RuntimeError('budget violation')
        policy_detail[name]={'canonical':s['name'],'validation_ns':s['validation_ns'],'bytes':s['bytes'],'choices_count':dict(collections.Counter(ids)),'warm_proxy_ns':sum(weights[i]*float(matrix[(i,c)]['warm_ns']) for i,c in enumerate(ids))/total,'cross_proxy_ns':sum(weights[i]*float(matrix[(i,c)]['cross_ns']) for i,c in enumerate(ids))/total,'test_median_ns':aliases[name]['median_ns']}
    # Test-best is diagnostic only; it must not feed the frozen selection.
    eligible_test=[s['name'] for s in f['policies'] if s['leaf_bytes']<=f['budget_leaf_bytes'] and s['name'] in methods]
    oracle=min(eligible_test,key=lambda name:methods[name]['median_ns'])
    summary[f['dataset']]={'n':f['n'],'timing_rows':len(rows),'test_checks':len(methods)*done['queries']*done['seeds'],'winner':chosen,'uniform_winner':uniform,'methods':methods,'policies':policy_detail,'budget_leaf_bytes':f['budget_leaf_bytes'],'proxy_setup_ms':f['proxy_setup_ms'],'validation_ms':f['validation_ms'],'all_candidate_build_ms':sum(s['build_ms'] for s in f['policies']),'clock_ns':f['clock_ns'],'diagnostic_test_best':oracle,'winner_regret_pct':100*(chosen_ns/methods[oracle]['median_ns']-1),'gain_vs_default_pct':100*(1-chosen_ns/aliases['default']['median_ns']),'gain_vs_uniform_pct':100*(1-chosen_ns/methods[uniform]['median_ns']),'ratio_to_art':chosen_ns/methods['ART_libart']['median_ns']}
output=root/'summary.json';output.write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
labels=['default','warm_joint','cross_joint','warm_hybrid','cross_hybrid','validated_uniform','validated_policy','sorted_leaves']
lines=['| 方法 | skew | mixed | Tranco |','|---|---:|---:|---:|']
for name in labels:lines.append('| '+name+' | '+' | '.join(f"{summary[k]['policies'][name]['test_median_ns']:.2f}" for k in ['skew','mixed','tranco'])+' |')
for name in ['sorted_array','LITS_pmss_patch','ART_libart']:
    if all(name in summary[k]['methods'] for k in summary):lines.append('| '+name+' | '+' | '.join(f"{summary[k]['methods'][name]['median_ns']:.2f}" for k in ['skew','mixed','tranco'])+' |')
(root/'RESULTS_TABLE.md').write_text('单位：ns/op，三个查询种子各五轮的批次均值之中位数。相同配置的别名复用同一组计时。\n\n'+'\n'.join(lines)+'\n')
print('\n'.join(lines));print('rows',sum(s['timing_rows'] for s in summary.values()),'exact_test_checks',sum(s['test_checks'] for s in summary.values()))
