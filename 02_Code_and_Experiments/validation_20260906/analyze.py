from pathlib import Path
import csv,hashlib,json,statistics

ROOT=Path(__file__).resolve().parent
cases=[]
for path in sorted((ROOT/'results').glob('*/*.csv')):
    rows=list(csv.DictReader(path.open()))
    grouped={}
    for row in rows:grouped.setdefault(row['mode'],[]).append(row)
    expected=5 if path.parent.name=='pilot' else 7
    complete=all(len(v)==expected for v in grouped.values())
    if not complete and path.parent.name!='pilot':raise RuntimeError(f'Incomplete repetitions: {path}')
    actual_repetitions=len(next(iter(grouped.values())))
    if any(len(v)!=actual_repetitions for v in grouped.values()):raise RuntimeError(f'Unequal recorded repetitions: {path}')
    if len(rows)!=(8 if rows[0]['scope']=='local' else 9)*actual_repetitions:raise RuntimeError(f'Incomplete variants: {path}')
    first=rows[0]
    case={k:first[k] for k in ['dataset','n','block','region_keys','scope','miss_percent','queries']}
    case.update(suite=path.parent.name,complete=complete,planned_repetitions=expected,excluded_from_conclusions=path.parent.name=='pilot',raw_csv=str(path.relative_to(ROOT)),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),modes={})
    for mode,rr in grouped.items():
        v=[float(r['ns_per_op']) for r in rr]
        case['modes'][mode]={'median_ns':statistics.median(v),'min_ns':min(v),'max_ns':max(v),'repetitions':len(v),'index_mib':int(rr[0]['index_bytes'])/2**20,'overflow_pct':int(rr[0]['overflow_keys'])/int(rr[0]['n'])*100,'model_bytes':float(rr[0]['mean_model_bytes']),'overflow_checks':float(rr[0]['mean_overflow_checks'])}
    med=lambda m:case['modes'][m]['median_ns']
    case['interval_reduction_vs_full_pct']=100*(1-med('interval')/med('full'))
    case['early_pf_reduction_vs_interval_pct']=100*(1-med('interval_early_pf')/med('interval'))
    case['interval_reduction_vs_residual_pct']=100*(1-med('interval')/med('residual'))
    per_rep=lambda m:{int(r['repeat']):float(r['ns_per_op']) for r in grouped[m]}
    a,b=per_rep('interval_early_pf'),per_rep('interval')
    case['early_pf_winning_repeats']=sum(a[i]<b[i] for i in a)
    cases.append(case)

events=list(csv.DictReader((ROOT/'update_events.csv').open()))
updates={}
for op in sorted({r['operation'] for r in events}|{'rebuild_event'}):
    v=sorted(int(r['ns']) for r in events if (int(r['rebuilds'])>0 if op=='rebuild_event' else r['operation']==op))
    updates[op]={'count':len(v),'median_ns':statistics.median(v),'p99_ns':v[int((len(v)-1)*.99)],'p999_ns':v[int((len(v)-1)*.999)],'max_ns':max(v)}
probes=[]
for path in sorted((ROOT/'overflow_probe'/'results').glob('*.csv')):
    rows=list(csv.DictReader(path.open())); grouped={}
    for row in rows:grouped.setdefault(row['mode'],[]).append(row)
    if set(grouped)!={'overflow_linear','overflow_binary32','sorted_array'} or any(len(v)!=7 for v in grouped.values()):raise RuntimeError(f'Incomplete overflow probe: {path}')
    first=rows[0]
    probe={k:first[k] for k in ['dataset','n','block','region_keys','scope','miss_percent','queries']}
    probe.update(raw_csv=str(path.relative_to(ROOT)),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),modes={})
    for mode,rr in grouped.items():
        v=[float(r['ns_per_op']) for r in rr]
        probe['modes'][mode]={'median_ns':statistics.median(v),'min_ns':min(v),'max_ns':max(v),'repetitions':len(v),'index_mib':int(rr[0]['index_bytes'])/2**20,'overflow_pct':int(rr[0]['overflow_keys'])/int(rr[0]['n'])*100}
    a={int(r['repeat']):float(r['ns_per_op']) for r in grouped['overflow_binary32']}
    b={int(r['repeat']):float(r['ns_per_op']) for r in grouped['overflow_linear']}
    probe['binary_winning_repeats']=sum(a[i]<b[i] for i in a)
    probe['binary_reduction_vs_linear_pct']=100*(1-probe['modes']['overflow_binary32']['median_ns']/probe['modes']['overflow_linear']['median_ns'])
    probes.append(probe)
summary={'cases':cases,'overflow_probes':probes,'updates':updates,'correctness':json.loads((ROOT/'correctness.json').read_text()),'overflow_probe_correctness':json.loads((ROOT/'overflow_probe'/'correctness.json').read_text()),'timed_lookup_count_main_and_diagnostic':sum(int(c['queries'])*sum(m['repetitions'] for m in c['modes'].values()) for c in cases if not c['excluded_from_conclusions']),'audit_notes':['Pilot short_32768_b512_end.csv retains 3 complete rounds, while 5 were requested. Its stdout summary does not match the retained CSV. Cause was not established; retain the raw file, exclude all pilot cases from conclusions, and do not invent missing rows. Main and diagnostic files have all 7 requested rounds.']}
query_copy_probes=[]
for path in sorted((ROOT/'query_copy_probe'/'results').glob('*.csv')):
    rows=list(csv.DictReader(path.open())); grouped={}
    for row in rows:grouped.setdefault(row['mode'],[]).append(row)
    if set(grouped)!={'full','interval','interval_early_pf','sorted_array'} or any(len(v)!=7 for v in grouped.values()):raise RuntimeError(f'Incomplete independent-query probe: {path}')
    first=rows[0]
    probe={k:first[k] for k in ['dataset','n','block','region_keys','scope','miss_percent','queries']}
    probe.update(raw_csv=str(path.relative_to(ROOT)),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),modes={})
    for mode,rr in grouped.items():
        v=[float(r['ns_per_op']) for r in rr]
        probe['modes'][mode]={'median_ns':statistics.median(v),'min_ns':min(v),'max_ns':max(v),'repetitions':len(v),'index_mib':int(rr[0]['index_bytes'])/2**20,'overflow_pct':int(rr[0]['overflow_keys'])/int(rr[0]['n'])*100,'model_bytes':float(rr[0]['mean_model_bytes'])}
    a={int(r['repeat']):float(r['ns_per_op']) for r in grouped['interval_early_pf']}
    b={int(r['repeat']):float(r['ns_per_op']) for r in grouped['interval']}
    probe['early_pf_winning_repeats']=sum(a[i]<b[i] for i in a)
    med=lambda mode:probe['modes'][mode]['median_ns']
    probe['interval_reduction_vs_full_pct']=100*(1-med('interval')/med('full'))
    probe['early_pf_reduction_vs_interval_pct']=100*(1-med('interval_early_pf')/med('interval'))
    query_copy_probes.append(probe)
summary['query_copy_probes']=query_copy_probes
summary['audit_notes'].append('In main, diagnostic and overflow experiments, hit-query bytes alias the sorted-array records. The independent-query probe gives each query its own copy; prefer that probe for cross-structure comparisons, and do not splice times from different processes.')
(ROOT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
lines=['本表由原始 CSV 自动生成。单位为各轮批次平均 ns/op 的中位数；pilot 全部排除于结论，local 不含外层定位。命中查询与有序数组存在字节别名，跨结构比较优先参考 QUERY_COPY_TABLE.md。', '', '| 组别 | 数据／规模／块／区域 | 完整计算 ns | 区间停止 ns | 提前预取 ns | 阈值停止 ns | 有序数组 ns | 区间溢出 % |', '|---|---|---:|---:|---:|---:|---:|---:|']
for c in cases:
    m=c['modes'];value=lambda k:f"{m[k]['median_ns']:.1f}" if k in m else '—'
    desc=f"{c['dataset']} / {c['n']} / B={c['block']} / R={c['region_keys']} / {c['scope']}"
    lines.append('| '+' | '.join([c['suite'],desc,value('full'),value('interval'),value('interval_early_pf'),value('residual'),value('sorted_array'),f"{m['interval']['overflow_pct']:.1f}"])+' |')
(ROOT/'RESULTS_TABLE.md').write_text('\n'.join(lines)+'\n')
probe_lines=['| 数据 | 线性溢出 ns | 大溢出二分 ns | 有序数组 ns | 二分较线性下降 % | 二分胜出轮数 / 7 |','|---|---:|---:|---:|---:|---:|']
for c in probes:
    m=c['modes']
    probe_lines.append('| '+ ' | '.join([c['dataset'],f"{m['overflow_linear']['median_ns']:.1f}",f"{m['overflow_binary32']['median_ns']:.1f}",f"{m['sorted_array']['median_ns']:.1f}",f"{c['binary_reduction_vs_linear_pct']:.2f}",str(c['binary_winning_repeats'])])+' |')
(ROOT/'OVERFLOW_PROBE_TABLE.md').write_text('\n'.join(probe_lines)+'\n')
query_lines=['| 数据 | 完整计算 ns | 区间停止 ns | 提前预取 ns | 有序数组 ns | 区间较完整下降 % | 预取胜出轮数 / 7 |','|---|---:|---:|---:|---:|---:|---:|']
for c in query_copy_probes:
    m=c['modes']
    query_lines.append('| '+' | '.join([c['dataset']]+[f"{m[k]['median_ns']:.1f}" for k in ['full','interval','interval_early_pf','sorted_array']]+[f"{c['interval_reduction_vs_full_pct']:.2f}",str(c['early_pf_winning_repeats'])])+' |')
(ROOT/'QUERY_COPY_TABLE.md').write_text('\n'.join(query_lines)+'\n')
print('cases',len(cases),'main_and_diagnostic_timed_lookups',summary['timed_lookup_count_main_and_diagnostic'])
for c in cases:
    if c['suite']=='pilot':continue
    print(c['raw_csv'],'interval_vs_full_pct',round(c['interval_reduction_vs_full_pct'],1),'pf_vs_interval_pct',round(c['early_pf_reduction_vs_interval_pct'],1),'pf_wins',c['early_pf_winning_repeats'])
