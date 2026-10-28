"""Generate F5 runners/drivers from frozen F3/F4M protocols, without editing them."""
from pathlib import Path
ROOT=Path(__file__).resolve().parent
s=(ROOT/'run_dirty_compact.py').read_text(encoding='utf-8')
s=s.replace('F4M','F5').replace('f4m_dirty_compact_20261018','f5_coupled_page_20261018')
s=s.replace("f3=read(ROOT/'results/f3_slotted_page_20261018_attempt1/identity.json')", """f3=read(ROOT/'results/f4m_dirty_compact_20261018b/identity.json')
    f4=read(ROOT/'results/f4_route_attribution_20261018/identity.json')
    for path,digest in f4['sources'].items():assert sha(WORKSPACE/path)==digest,('frozen F4 drift',path)
    f3['sources'].update(f4['sources'])""")
s=s.replace("('generate_dirty_compact.py','dirty_compact_index.hpp','check_dirty_compact.cpp','F5_DIRTY_COMPACT_SPEC_20261018.md')", "('generate_coupled_page.py','generate_coupled_check.py','prepare_coupled_runners.py','coupled_root.hpp','coupled_page_index.hpp','check_coupled_page.cpp','F5_COUPLED_PAGE_SPEC_20261018.md')")
s=s.replace("'dirty_compact_'+kind", "'coupled_page_'+kind").replace("diagnostics/check_dirty_compact.cpp", "diagnostics/check_coupled_page.cpp")
s=s.replace("save('verification.json',", """space=[]
    for name,records in gates['normal'].items():
        if name=='synthetic':continue
        row=records[0];assert row['root_publications']==row['root_prepared']==row['rebuilds']+1
        ratio=row['initial_slotted_bytes']/row['initial_d1_bytes']
        space.append({'cell':name,'live_over_D1':ratio,'space_gate_passed':ratio<=1.10})
    save('entry_decision.json',{'space_cells':space,'timing_allowed':all(r['space_gate_passed'] for r in space)})
    save('verification.json',""")
(ROOT/'run_coupled_page.py').write_text(s,encoding='utf-8',newline='\n')

s=(ROOT/'bench_slotted_page_read.cpp').read_text(encoding='utf-8')
s=s.replace('slotted_page_index.hpp','coupled_page_index.hpp').replace('slotted_page::','coupled_page::')
s=s.replace('        const coupled_page::Index control(rows,16);\n','')
s=s.replace('live.verify()+control.verify()+d1.verify_numeric_certificate()','live.verify()+d1.verify_numeric_certificate()')
s=s.replace('||control.find(q.key)!=q.expected','').replace('control.check_window(q.key);','')
s=s.replace('const auto scanned=live.scan("",SIZE_MAX),controlled=control.scan("",SIZE_MAX);','const auto scanned=live.scan("",SIZE_MAX);')
s=s.replace('||controlled.size()!=rows.size()','').replace('||controlled[i].key!=rows[i].key||controlled[i].value!=rows[i].value','')
s=s.replace('<<",\\\"control_bytes\\\":"<<control.memory_bytes()','')
s=s.replace('        std::mt19937_64 writer', '        const auto* root=live.root_identity_for_testing();\n        std::mt19937_64 writer')
s=s.replace('        rows.clear();for', '        if(root!=live.root_identity_for_testing()||live.maintenance().root_publications!=1||live.dirty_page_count()!=0)throw std::logic_error("updates changed root");\n        rows.clear();for')
s=s.replace('<<",\\\"inplace_updates\\\":"', '<<",\\\"root_publications\\\":"<<live.maintenance().root_publications<<",\\\"root_entries_copied\\\":"<<live.maintenance().root_entries_copied<<",\\\"root_bytes\\\":"<<live.root_memory_bytes()\n            <<",\\\"inplace_updates\\\":"')
s=s.replace('PageRead live_read(live),control_read(control);','PageRead live_read(live);')
s=s.replace('&live_read,&live_read,&control_read,&control_read,&d1_read,&d1_read','&live_read,&live_read,&d1_read,&d1_read')
s=s.replace('"live","live_AA","control","control_AA","D1","D1_AA"','"live","live_AA","D1","D1_AA"')
s=s.replace('{0,1,5,2,4,3}','{0,1,3,2}').replace('repeat<6','repeat<8').replace('position<6','position<4').replace('process)%6','process)%4')
assert 'control' not in s
(ROOT/'bench_coupled_page_read.cpp').write_text(s,encoding='utf-8',newline='\n')

s=(ROOT/'run_slotted_page_read.py').read_text(encoding='utf-8')
s=s.replace('import statistics','import statistics\nimport shutil')
s=s.replace("ARMS=['live','live_AA','control','control_AA','D1','D1_AA']", "ARMS=['live','live_AA','D1','D1_AA']")
start=s.index('def summarize(raw):');end=s.index('\ndef main():',start)
s=s[:start]+'''def summarize(raw):
    result=[]
    for cell in sorted({r['cell'] for r in raw}):
        rows=[r for r in raw if r['cell']==cell];assert len(rows)==160
        values={name:[] for name in ('live/D1','live_AA/live','D1_AA/D1')}
        for process in range(5):
            block=[r for r in rows if r['process']==process];assert len(block)==32
            table={(r['repeat'],r['arm']):r['elapsed_ns'] for r in block};assert len(table)==32
            for repeat in range(8):
                seq=sorted((r for r in block if r['repeat']==repeat),key=lambda r:r['position'])
                assert [r['arm'] for r in seq]==[ARMS[(x+repeat+process)%4] for x in (0,1,3,2)]
                assert [r['position'] for r in seq]==list(range(4)) and len({r['checksum'] for r in seq})==1
            for name in values:
                a,b=name.split('/');values[name].append(statistics.median(table[r,a]/table[r,b] for r in range(8)))
        bands={a:max(max(x,1/x) for x in values[a+'_AA/'+a]) for a in ('live','D1')}
        ratios={}
        for name,points in values.items():
            a,b=name.split('/');band=max(bands[a.replace('_AA','')],bands[b]);median=statistics.median(points)
            ratios[name]={'median':median,'process_medians':points,'slower':sum(x>1 for x in points),'band':band}
        r=ratios['live/D1'];failed=r['median']>1.15 and r['slower']>=4 and r['median']>r['band']
        result.append({'cell':cell,'ratios':ratios,'aa_bands':bands,'read_budget_failed':failed,
                       'read_budget_status':'failed' if failed else 'within budget' if r['median']<=1.15 else 'unresolved',
                       'ns_per_op':{a:statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==a) for a in ARMS}})
    return result

''' +s[end:]
s=s.replace('f3_slotted_page_read_20261018','f5_coupled_page_read_20261018').replace('bench_slotted_page_read.cpp','bench_coupled_page_read.cpp')
s=s.replace("ROOT/'diagnostics/bench_coupled_page_read.cpp')", "ROOT/'diagnostics/bench_coupled_page_read.cpp',ROOT/'diagnostics/prepare_coupled_runners.py')")
s=s.replace("save('identity.json',identity);log=", """for path in sources:
        target=out/'frozen_sources'/path;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(WORKSPACE/path,target)
    save('identity.json',identity);log=""")
s=s.replace("'slotted_read'","'coupled_read'").replace("'slotted_read_san'","'coupled_read_san'")
s=s.replace('F3 read entry failed','F5 read entry failed').replace('len(lines)==37','len(lines)==33').replace('len(raw)==1080','len(raw)==960')
s=s.replace("'status':'close current layout at read-budget gate' if failed else 'not rejected by read screen; full dynamic validation pending'", """'unresolved_cells':[r['cell'] for r in summary if r['read_budget_status']=='unresolved'],
                          'dynamic_gate_allowed':all(r['read_budget_status']=='within budget' for r in summary),
                          'status':'close current layout at read-budget gate' if failed else 'read screen complete; adoption and full dynamic matrix pending'""")
s=s.replace("'live/control',round(r['ratios']['live/control']['median'],3),",'')
(ROOT/'run_coupled_page_read.py').write_text(s,encoding='utf-8',newline='\n')
