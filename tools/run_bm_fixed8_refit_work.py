"""Fixed8 matched-layout refit counterfactual; no elapsed-time or policy claims."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
from run_bm_tx import Run,ROOT,DIAG,save,sha

PROTOCOL=ROOT/'docs/PROTOCOL_BM_FIXED8_REFIT_DIAGNOSIS_20261030.md'

def generate(run):
    directory=run.output/'refit_sources';directory.mkdir()
    names={'bm_n1_page_core.hpp':'bm_refit_page_core.hpp','bm_n1_page_root.hpp':'bm_refit_page_root.hpp',
           'bm_n1_index.hpp':'bm_refit_index.hpp','bm_tx_index.hpp':'bm_refit_tx_index.hpp'}
    records={}
    page_hook='''    std::size_t refit_model_for_diagnostics() {
        if(regions_.empty())return 0;
        const auto& r=regions_[0];std::vector<std::string_view> train;train.reserve(r.count);
        for(std::uint32_t b=0;b<r.count;++b)train.push_back(boundary(r,b));
        auto fitted=blocklearn::BlockModel::fit(train,1,7,8,1);
        bm_=std::move(fitted);return train.size();
    }
'''
    index_hook='''    std::size_t refit_models_for_diagnostics() {
        std::size_t trained=0;
        for(auto& item:pages_) {
            auto& page=*item.second.core;const auto before=page.memory().total();
            trained+=page.refit_model_for_diagnostics();
            resident_=resident_-before+page.memory().total();peak();
        }
        return trained;
    }
'''
    for original,new in names.items():
        source=DIAG/original
        frozen=subprocess.check_output(['git','-C',str(ROOT),'show','10e0494:'+source.relative_to(ROOT).as_posix()])
        if source.read_bytes()!=frozen:raise ValueError('refit frozen preimage drift')
        text=frozen.decode()
        for before,after in names.items():text=text.replace(before,after)
        for before,after in (('bm_n1_leaf','bm_refit_leaf'),('bm_n1_page','bm_refit_page'),('bm_tx_page','bm_refit_tx')):
            text=text.replace(before,after)
        if original=='bm_n1_page_core.hpp':
            anchor='    Page(const Page&)=delete;Page& operator=(const Page&)=delete;'
            if text.count(anchor)!=1:raise ValueError('refit Page hook preimage')
            text=text.replace(anchor,page_hook+anchor,1)
        if original=='bm_tx_index.hpp':
            anchor='    const FlatRoot* root_identity_for_testing()const{return root_.get();}'
            if text.count(anchor)!=1:raise ValueError('refit Index hook preimage')
            text=text.replace(anchor,index_hook+anchor,1)
        target=directory/new;target.write_bytes(text.encode())
        records[original]={'base':'10e0494','preimage_sha256':hashlib.sha256(frozen).hexdigest(),
                           'generated_sha256':sha(target),'generated':str(target)}
    save(run.output/'generation.json',records);return directory/'bm_refit_tx_index.hpp'

def decide(cells):
    results={};all_guards=True;headroom=0
    for cell,records in cells.items():
        original,before,after=records
        copy=dict(before,arm=original['arm'])
        if copy!=original:raise ValueError('refit namespace copy changed original work/bytes')
        a=next(g for g in original['groups'] if g['group']=='mixed90')
        b=next(g for g in after['groups'] if g['group']=='mixed90')
        terminal=after['terminal']/original['terminal'];peak=after['refit_peak']/original['terminal']
        payload=b['payload_blocks']/a['payload_blocks'];exact=b['exact_compares']/a['exact_compares']
        eligible=a['directory_steps']/a['queries']>=0.25
        halved=eligible and b['directory_steps']<=a['directory_steps']/2
        guard=(terminal<=1.05 and peak<=1.10 and payload<=1.05 and exact<=1.05
               and b['directory_steps']<=a['directory_steps'] and after['find_allocations']==0)
        all_guards=all_guards and guard;headroom+=halved
        results[cell]={'terminal_ratio':terminal,'refit_peak_vs_original_terminal':peak,'payload_ratio':payload,
             'exact_ratio':exact,'before_directory_per_query':a['directory_steps']/a['queries'],
             'after_directory_per_query':b['directory_steps']/b['queries'],
             'before_outside_fraction':a['model_outside']/a['queries'],'after_outside_fraction':b['model_outside']/b['queries'],
             'headroom_cell':halved,'guard_pass':guard}
    return {'cells':results,'all_guards_pass':all_guards,'headroom_cells':headroom,
            'nominate_maintenance_cost_diagnosis':all_guards and headroom>=8}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--completed-qualification',type=Path,required=True)
    args=parser.parse_args()
    q=json.loads((args.completed_qualification/'report.json').read_text())
    if q.get('status')!='complete' or q.get('phase')!='calibrate':raise ValueError('finish timing qualification first')
    run=Run(args.output)
    run.sources.update({str(Path(__file__).resolve()):sha(Path(__file__)),str(PROTOCOL):sha(PROTOCOL)})
    try:
        generated=generate(run)
        evidence=ROOT/'07_Eval_Integration/results/bm_model16_work_20261030'
        for filename in ('cases.json','input_hashes.json'):
            path=evidence/filename
            frozen=subprocess.check_output(['git','-C',str(ROOT),'show','7ca8b7b:'+path.relative_to(ROOT).as_posix()])
            if path.read_bytes()!=frozen:raise ValueError('refit archived case preimage drift')
            run.sources[str(path)]=sha(path)
        cases=json.loads((evidence/'cases.json').read_text());inputs=json.loads((evidence/'input_hashes.json').read_text())
        if len(cases)!=32 or any(sha(p)!=h for p,h in inputs.items()):raise ValueError('refit input identity')
        save(run.output/'cases.json',cases);save(run.output/'input_hashes.json',inputs)
        save(run.output/'registration.json',{'protocol_sha256':sha(PROTOCOL),'timing_claims':False,
             'fixed_budget':8,'minimum_headroom_cells':8,'maximum_payload_exact_ratio':1.05,
             'completed_qualification_report_sha256':sha(args.completed_qualification/'report.json')})
        definitions={'BM_REFIT_INDEX_HEADER':str(generated),'BM_TX_CONTROL_BM1':'1',
                     'BM_QUERY_BM1_ADAPTER_SHA256':sha(DIAG/'bm_split_adapter.hpp')}
        model=ROOT/'07_Eval_Integration/upstream_snapshots/model_3894c767/src/block_model.cpp'
        run.report['builds']={}
        for name,sanitize in (('opt',False),('sanitizer',True)):
            binary=run.build('refit_'+name,[DIAG/'bm_fixed8_refit_work.cpp',model],sanitize,definitions)
            values={};run.report['builds'][name]=values
            for cell,meta in sorted(cases.items()):
                raw=run.execute([binary,'profile',*meta['paths']],name+'_'+cell+'.jsonl',
                                dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1'))
                records=[json.loads(x) for x in raw.splitlines()]
                if len(records)!=3 or [r['arm'] for r in records]!=['original8','copy8_before','copy8_refit']:
                    raise ValueError('refit profile coverage')
                values[cell]=records;save(run.output/'report.json',run.report)
            print(name+': fixed8 refit32 complete',flush=True)
        if run.report['builds']['opt']!=run.report['builds']['sanitizer']:raise ValueError('refit cross-build disagreement')
        run.report['decision']=decide(run.report['builds']['opt'])
        if any(sha(p)!=h for p,h in inputs.items()):raise ValueError('refit completion input drift')
        run.finish();print(json.dumps({'status':'complete','nominate':run.report['decision']['nominate_maintenance_cost_diagnosis']}),flush=True)
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report);raise

if __name__=='__main__':main()
