"""Audit and summarize complete E3 runs without tuning on evaluation results."""
from pathlib import Path
from collections import Counter,defaultdict
import csv,hashlib,json,statistics

root=Path(__file__).resolve().parent
methods=['fixed_context1024','uniform_tuned','sequential','joint_learned','joint_with_leaf','sorted_leaves','sorted_array']
cases=[]
for directory in sorted((root/'results').glob('*/*')):
    if directory.parent.name=='incomplete':continue
    if not (directory/'timings.csv').exists():continue
    meta=json.loads((directory/'run.json').read_text())
    for name,digest in meta['source_sha256'].items():
        sources=[root/name]
        if name=='bench.cpp':sources.append(root/'archive'/'bench_before_io_checks.cpp')
        if not any(p.exists() and hashlib.sha256(p.read_bytes()).hexdigest()==digest for p in sources):raise RuntimeError(f'Missing source snapshot: {directory}/{name}')
    rows=list(csv.DictReader((directory/'timings.csv').open()))
    grouped=defaultdict(list)
    for row in rows:grouped[row['method']].append(row)
    if set(grouped)!=set(methods):raise RuntimeError(f'Missing methods: {directory}')
    expected_seeds={str(20260920+i) for i in range(meta['query_seeds'])}
    selections=list(csv.DictReader((directory/'selections.csv').open()))
    selected=defaultdict(list)
    for row in selections:selected[row['method']].append(row)
    candidates=list(csv.DictReader((directory/'candidates.csv').open()))
    registry={(r['region'],r['candidate']):r for r in candidates}
    regions={r['region'] for r in candidates}
    if len(registry)!=len(regions)*10:raise RuntimeError('Incomplete candidate matrix')
    first=rows[0]
    case={'dataset':first['dataset'],'n':int(first['n']),'suite':directory.parent.name,'directory':str(directory.relative_to(root)),'setup_ms':float(first['setup_ms']),'budget_mib':int(first['budget_total_bytes'])/2**20,'methods':{},'comparisons':{},'same_selections':[],'sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in directory.iterdir() if p.is_file()}}
    case['candidate_totals']={}
    outer=int(first['budget_total_bytes'])-int(first['budget_leaf_bytes'])
    for c in range(10):
        rr=[r for r in candidates if r['candidate']==str(c)]
        case['candidate_totals'][str(c)]={'model':rr[0]['model'],'block':int(rr[0]['block']),'mib':(sum(int(r['bytes']) for r in rr)+outer)/2**20,'overflow_pct':100*sum(int(r['overflow_keys']) for r in rr)/case['n'],'weighted_tune_ns':sum(int(r['key_count'])*float(r['tune_median_ns']) for r in rr)/case['n']}
    for name in methods:
        rr=grouped[name];by_seed=defaultdict(list)
        for r in rr:by_seed[r['query_seed']].append(r)
        if set(by_seed)!=expected_seeds:raise RuntimeError('Wrong query seeds')
        for seed,part in by_seed.items():
            if {int(r['repeat']) for r in part}!=set(range(meta['rounds_per_seed'])) or len(part)!=meta['rounds_per_seed']:raise RuntimeError('Incomplete rounds')
        if name not in ['sorted_array','sorted_leaves'] and any(r['budget_feasible']!='1' for r in rr):raise RuntimeError('Budget violation')
        vals=[float(r['ns_per_op']) for r in rr]
        m={'median_ns':statistics.median(vals),'min_ns':min(vals),'max_ns':max(vals),'seed_medians':{s:statistics.median(float(r['ns_per_op']) for r in part) for s,part in by_seed.items()},'measurements':len(vals),'mib':int(rr[0]['bytes'])/2**20,'budget_feasible':rr[0]['budget_feasible']=='1','overflow_pct':100*int(rr[0]['overflow_keys'])/case['n'],'sorted_regions':int(rr[0]['sorted_regions']),'tune_objective_ns':float(rr[0]['tune_objective_ns']),'build_ms':float(rr[0]['build_ms']),'signature':rr[0]['selection_signature']}
        if name!='sorted_array':
            ss=selected[name]
            if {r['region'] for r in ss}!=regions or len(ss)!=len(regions):raise RuntimeError('Incomplete selected regions')
            if sum(int(r['bytes']) for r in ss)!=int(rr[0]['leaf_bytes']):raise RuntimeError('Selected memory mismatch')
            for r in ss:
                c=registry[(r['region'],r['candidate'])]
                if int(r['bytes'])!=int(c['bytes']) or int(r['overflow_keys'])!=int(c['overflow_keys']):raise RuntimeError('Candidate construction mismatch')
            m['candidate_regions']=dict(Counter(r['candidate'] for r in ss))
            m['candidate_keys']=dict(Counter())
            for r in ss:m['candidate_keys'][r['candidate']]=m['candidate_keys'].get(r['candidate'],0)+int(r['key_count'])
            reasons={key:0 for key in ['blocked_slots_only','blocked_bytes_only','blocked_both','oversized','fits_now','overflow_suffix_bytes','primary_suffix_bytes']}
            for r in ss:
                c=registry[(r['region'],r['candidate'])]
                for key in reasons:reasons[key]+=int(c[key])
            m['overflow_diagnostics']=reasons
        case['methods'][name]=m
    for a,b in [('joint_learned','fixed_context1024'),('joint_learned','uniform_tuned'),('joint_learned','sequential'),('joint_with_leaf','joint_learned'),('joint_with_leaf','sorted_leaves'),('joint_with_leaf','sorted_array')]:
        aa={(r['query_seed'],r['repeat']):float(r['ns_per_op']) for r in grouped[a]}
        bb={(r['query_seed'],r['repeat']):float(r['ns_per_op']) for r in grouped[b]}
        am,bm=case['methods'][a],case['methods'][b]
        case['comparisons'][a+'_vs_'+b]={'median_reduction_pct':100*(1-am['median_ns']/bm['median_ns']),'paired_wins':sum(aa[k]<bb[k] for k in aa),'paired_count':len(aa),'seed_reductions_pct':{seed:100*(1-am['seed_medians'][seed]/bm['seed_medians'][seed]) for seed in expected_seeds}}
    for i,a in enumerate(methods[:-1]):
        for b in methods[i+1:-1]:
            if case['methods'][a]['signature']==case['methods'][b]['signature']:
                av=sorted((int(r['region']),r['candidate']) for r in selected[a]);bv=sorted((int(r['region']),r['candidate']) for r in selected[b])
                if av!=bv:raise RuntimeError('Selection signature collision')
                case['same_selections'].append([a,b])
    group_rows=list(csv.DictReader((directory/'joint_groups.csv').open()))
    for r in group_rows:
        if sum(int(r[k]) for k in ['blocked_slots_only','blocked_bytes_only','blocked_both','fits_now'])!=int(r['overflow_entries']):raise RuntimeError('Overflow reason partition error')
        if int(r['primary_entries'])+int(r['free_entries'])!=16:raise RuntimeError('Slot conservation error')
    if sum(int(r['primary_entries'])+int(r['overflow_entries']) for r in group_rows)!=case['n']:raise RuntimeError('Group record count error')
    # A rough amortization threshold, not a deployment forecast; setup includes I/O.
    baseline=case['methods']['fixed_context1024']['median_ns']
    for name in ['joint_learned','joint_with_leaf']:
        saving=baseline-case['methods'][name]['median_ns']
        extra_ms=max(0,case['setup_ms']+case['methods'][name]['build_ms']-case['methods']['fixed_context1024']['build_ms'])
        case['methods'][name]['estimated_extra_setup_ms_vs_fixed']=extra_ms
        case['methods'][name]['setup_break_even_queries_vs_fixed']=extra_ms*1e6/saving if saving>0 else None
    cases.append(case)

summary={'correctness':json.loads((root/'correctness.json').read_text()),'cases':cases,'excluded_runs':['results/incomplete/mixed_262144: selections.csv ended mid-row at region 106 of uniform_tuned. Cause not established. Preserve original, exclude entire attempt, enable stream error reporting and file completeness checks, rerun only mixed without changing experimental parameters.'],'notes':['All pilot results excluded from conclusions.','Same choices can have different timing due to host noise, allocation and code layout; not evidence of algorithmic improvement.','Capacity is estimated bytes, not RSS.','Local tuning is warm-cache and excludes outer routing; final evaluation includes it.','Query identities are disjoint between tuning and evaluation; all indexed keys are available for index construction.']}
(root/'summary.json').write_text(json.dumps(summary,indent=2,ensure_ascii=False)+'\n')
labels={'fixed_context1024':'固定配置','uniform_tuned':'统一调参','sequential':'分步选择','joint_learned':'联合选择','joint_with_leaf':'联合＋回退','sorted_leaves':'有序叶','sorted_array':'有序数组'}
lines=['以下仅列正式实验：3 个查询种子，每种子 5 轮，表中为 15 次批次 ns/op 的中位数。越低越好；不是置信区间或单次查询 P50。','', '| 数据／规模 | '+' | '.join(labels[m] for m in methods)+' |','|---|'+'---:|'*len(methods)]
for c in cases:
    if c['suite']!='main':continue
    lines.append('| '+f"{c['dataset']}／{c['n']}"+' | '+' | '.join(f"{c['methods'][m]['median_ns']:.1f}" for m in methods)+' |')
(root/'RESULTS_TABLE.md').write_text('\n'.join(lines)+'\n')
print('AUDIT PASSED cases=',len(cases),'main=',sum(c['suite']=='main' for c in cases))
for c in cases:
    if c['suite']=='main':print(c['dataset'],json.dumps(c['comparisons'],ensure_ascii=False),'same_choices',c['same_selections'])
