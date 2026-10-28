"""Native planner ablation: one independent calibration, semantic gates, then paired full find."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import statistics as st
import subprocess
from generate_borrowed_contract_acceptance import rendered as acceptance_rendered

ROOT = Path(__file__).resolve().parents[1]
WS = ROOT.parent
DIAG = ROOT / 'diagnostics'
MODEL = ROOT / 'upstream_snapshots/model_e8e05411'
BENCH = ROOT / 'upstream_snapshots/benchmark_003a55bf'
NATIVE = WS / 'builds/native'
ART = ROOT / 'results/e2_generalization_20261018/build/art/libart_adapter.a'
CELLS = ['english_words', 'memetracker_urls', 'memetracker_phrases', 'tranco64_norepl']
SLOTS = ['legacy', 'cost', 'adaptive', 'ART', 'F5']
ARMS = [a for slot in SLOTS for a in (slot, slot+'_AA')]
PAIRS = [('cost', 'legacy'), ('adaptive', 'cost'), ('adaptive', 'legacy'),
         ('legacy', 'ART'), ('cost', 'ART'), ('adaptive', 'ART'), ('F5', 'ART')] + [(s+'_AA', s) for s in SLOTS]


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text(encoding='utf-8'))
def save(p, x): p.write_text(json.dumps(x, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def summarize(raw, builds):
    result = []
    for cell in CELLS:
        rows = [r for r in raw if r['cell'] == cell]
        ratios = {}
        for x, y in PAIRS:
            values = []
            for process in range(5):
                times = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in rows if r['process'] == process}
                values.append(st.median(times[rep, x]/times[rep, y] for rep in range(10)))
            ratios[x+'/'+y] = {'process_medians': values, 'median': st.median(values),
                               'faster': sum(v < 1 for v in values), 'slower': sum(v > 1 for v in values)}
        band = max(max(v, 1/v) for slot in SLOTS for v in ratios[slot+'_AA/'+slot]['process_medians'])
        for r in ratios.values():
            r['clear_gain'] = r['median'] <= .95 and r['faster'] >= 4 and 1/r['median'] > band
            r['clear_regression'] = r['median'] > 1.05 and r['slower'] >= 4 and r['median'] > band
            r['ten_percent_gain'] = r['median'] <= .9 and r['faster'] >= 4 and 1/r['median'] > band
        bs = [r for r in builds if r['cell'] == cell]
        bt = {(r['process'], r['arm']): r['elapsed_ns'] for r in bs}
        result.append({'cell': cell, 'aa_band': band, 'ratios': ratios,
                       'ns_per_query': {a: st.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm'] == a) for a in ARMS},
                       'build_over_legacy': {s: st.median(bt[p, s]/bt[p, 'legacy'] for p in range(5)) for s in ('cost','adaptive')},
                       'owned_bytes': {s: next(r['owned_bytes'] for r in bs if r['arm'] == s) for s in SLOTS},
                       'retained_input_bytes': {s: next(r['retained_input_bytes'] for r in bs if r['arm'] == s) for s in SLOTS}})
    return result


def decide(summary):
    result = {'production_adopted': False, 'default_changed': False,
              'metadata_5_percent_gate': 'unknown: native interface does not isolate model metadata',
              'status': 'complete native planner ablation, not BM-page adoption'}
    for name, comparator in [('cost', 'legacy'), ('adaptive', 'cost')]:
        real = summary[:3]
        gains = [r['cell'] for r in real if r['ratios'][name+'/'+comparator]['ten_percent_gain']]
        regressions = [r['cell'] for r in real if r['ratios'][name+'/'+comparator]['clear_regression']]
        result[name] = {'over_ART': {r['cell']: r['ratios'][name+'/ART']['median'] for r in summary},
                        'over_comparator': {r['cell']: r['ratios'][name+'/'+comparator]['median'] for r in summary},
                        'ten_percent_gain_real_cells': gains, 'clear_regression_real_cells': regressions,
                        'three_real_cells_within_1_10_ART': all(r['ratios'][name+'/ART']['median'] <= 1.1 for r in real),
                        'cross_real_gain_without_regression': len(gains) >= 2 and not regressions,
                        'build_within_1_20_legacy': all(r['build_over_legacy'][name] <= 1.2 for r in real),
                        'total_capacity_within_1_10_legacy': all(
                            (r['owned_bytes'][name]+r['retained_input_bytes'][name]) <=
                            1.1*(r['owned_bytes']['legacy']+r['retained_input_bytes']['legacy']) for r in real)}
    return result


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--output', default='bm_cost_native_20261019')
    parser.add_argument('--reuse-preflight-build', type=Path)
    args = parser.parse_args(); assert Path(args.output).name == args.output
    assert (DIAG/'borrowed_contract_acceptance.cpp').read_text(encoding='utf-8')==acceptance_rendered()
    lock = read(ROOT / 'results/upstream_update_audit_20261018/identity.json')
    native = read(ROOT / 'results/bm_native_reference_20261018/identity.json')
    for repo, path in [('model', MODEL), ('benchmark', BENCH)]:
        assert subprocess.check_output(['git','rev-parse','HEAD'], cwd=path, text=True).strip() == lock['repositories'][repo]['commit']
    for lib, digest in native['native_build']['libraries'].items(): assert sha(NATIVE / lib) == digest
    for path, digest in lock['repositories']['model']['tracked_files_sha256'].items():
        if path.startswith('src/') and (MODEL / path).is_file(): assert sha(MODEL / path) == digest, path
    real = read(ROOT / 'results/bm_real_20261018/identity.json')
    e2 = read(ROOT / 'results/e2_generalization_20261018/identity.json')
    inputs = {}
    for cell in CELLS:
        x = real['inputs'][cell] if cell != CELLS[-1] else e2['inputs'][cell]
        inputs[cell] = {'rows_path': x['rows_path'], 'rows_sha256': x['rows_sha256'], 'rows': x['rows'],
                        'queries_path': x['queries_path'], 'queries_sha256': x['queries']['sha256'], 'queries': x['queries']['count']}
    for x in inputs.values():
        assert sha(WS / x['rows_path']) == x['rows_sha256']
        assert sha(WS / x['queries_path']) == x['queries_sha256']
    out = ROOT / 'results' / args.output; out.mkdir(exist_ok=False)
    build = out / 'build'; build.mkdir()
    meta = {'base_commit': subprocess.check_output(['git','rev-parse','HEAD'], cwd=ROOT, text=True).strip(),
            'cpu': min(os.sched_getaffinity(0)), 'inputs': inputs, 'sources': {}, 'artifacts': {}, 'commands': [],
            'libraries': {str(p.relative_to(WS)): sha(p) for p in [NATIVE/'libblocklearn_adapter.a',NATIVE/'libblocklearn_core.a',ART]},
            'compiler': subprocess.check_output(['g++','--version'], text=True).splitlines()[0],
            'upstream_commit': lock['repositories']['model']['commit'], 'benchmark_commit': lock['repositories']['benchmark']['commit'],
            'protocol': '5 processes x 10 Williams rounds x 10 arms; complete native find; 90/10 synthetic workload on real keys',
            'limits': ['64k only', 'capacity estimates are not complete heap', 'no dynamic throughput',
                       'sanitizer covers native core and gate driver, not ART/F5 timing controls',
                       'native record costs are not transferred to BM-page fence models', 'no adoption']}
    log = (out / 'commands.log').open('x', encoding='utf-8')

    def run(command, env=None):
        command = list(map(str, command)); meta['commands'].append(command)
        log.write('$ '+' '.join(command)+'\n'); log.flush()
        p = subprocess.run(command, cwd=ROOT, capture_output=True, env=env, timeout=7200)
        stdout=p.stdout.decode('utf-8',errors='backslashreplace')
        stderr=p.stderr.decode('utf-8',errors='backslashreplace')
        log.write(stdout); log.write(stderr); log.flush()
        if p.returncode:
            (out/'failure.stdout.bin').write_bytes(p.stdout);(out/'failure.stderr.bin').write_bytes(p.stderr)
            save(out/'failure.json', {'command': command, 'rc': p.returncode, 'stderr': stderr[-12000:],
                                    'stdout_sha256':hashlib.sha256(p.stdout).hexdigest(),'stderr_sha256':hashlib.sha256(p.stderr).hexdigest()})
            save(out/'identity.json', meta); raise RuntimeError('command failed; failure retained')
        return stdout

    flags = ['-pthread','-std=c++17','-march=x86-64-v3','-DNDEBUG',
             '-I'+str(WS/'02_Code_and_Experiments/validation_e25_20260918'),
             '-I'+str(WS/'02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src'),
             '-I'+str(BENCH/'src'),'-I'+str(MODEL/'src'),'-I'+str(DIAG),
             '-DCOUPLED_CORE_SHA256="f5-core-bundle"','-DCOUPLED_ADAPTER_SHA256="f5-adapter"']
    core = [MODEL/'src'/p for p in ['block_model.cpp','residual_tags.cpp','query_cost.cpp','region_planner.cpp','learned_index.cpp','benchmark/model_factories.cpp']]
    gates = [DIAG/'check_bm_cost.cpp',BENCH/'src/unified/runner.cpp']
    timed = [DIAG/'bench_bm_cost.cpp',ROOT/'adapters/art/art_adapter.cpp',BENCH/'src/unified/runner.cpp']
    calibrator = MODEL/'src/benchmark/calibrate_planner.cpp'
    deps = run(['g++']+flags+['-MM']+core+gates+timed+[calibrator])
    sources = {Path(__file__), DIAG/'verify_bm_cost.py', DIAG/'BM_COST_SPEC_20261019.md',
               DIAG/'generate_borrowed_contract_acceptance.py',ROOT/'adapters/common/contract_acceptance.cpp'}
    for line in deps.replace('\\\n',' ').splitlines():
        if ':' in line:
            sources.update(Path(s).resolve() for s in line.split(':',1)[1].split())
    for p in sorted(sources):
        rel = str(p.relative_to(WS)); meta['sources'][rel] = sha(p)
        dest = out/'frozen_sources'/rel; dest.parent.mkdir(parents=True, exist_ok=True); dest.write_bytes(p.read_bytes())
    save(out/'identity.json',meta)
    profile = out/'planner-costs.txt'
    if args.reuse_preflight_build:
        previous=args.reuse_preflight_build.resolve();old=read(previous/'identity.json')
        assert not (previous/'raw.json').exists(),'cannot resume a performance round'
        assert old['libraries']==meta['libraries'] and old['inputs']==inputs
        for path,digest in old['sources'].items():
            if Path(path).suffix in ('.cpp','.h','.hpp') and not path.endswith('check_bm_cost.cpp'):
                assert sha(WS/path)==digest,('reused binary source drift',path)
        for name in ('calibrate','bench'):
            p=previous/'build'/name;assert sha(p)==old['artifacts']['build/'+name]
            (build/name).write_bytes(p.read_bytes());(build/name).chmod(0o755)
        assert sha(previous/'planner-costs.txt')==old['profile_sha256']
        profile.write_bytes((previous/'planner-costs.txt').read_bytes())
        meta['reused_preflight']={'path':str(previous.relative_to(WS)),
            'identity_sha256':sha(previous/'identity.json'),'profile_sha256':sha(profile),
            'artifacts':{name:sha(build/name) for name in ('calibrate','bench')},
            'reason':'shared-suite temporary input lifetime and byte-output transport fixed before timing'}
        print('reusing original calibration and unchanged benchmark binary',flush=True)
    else:
        print('building independent calibrator',flush=True)
        run(['g++']+flags+['-O3',calibrator,NATIVE/'libblocklearn_core.a','-o',build/'calibrate'])
        run(['taskset','-c',meta['cpu'],build/'calibrate','--output',profile,'--iterations','32768'])
    run([build/'calibrate','--validate',profile])
    meta['profile_sha256'] = sha(profile)
    env = dict(os.environ, BLOCKLEARN_COST_PROFILE=str(profile))
    print('profile frozen',meta['profile_sha256'],flush=True)
    for name,opts,sources_,libs in [
        ('gate',['-O3'],gates,[NATIVE/'libblocklearn_adapter.a',NATIVE/'libblocklearn_core.a']),
        ('gate_san',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'],gates+core,[]),
        ('bench',['-O3'],timed,[NATIVE/'libblocklearn_adapter.a',NATIVE/'libblocklearn_core.a',ART])]:
        if name=='bench' and args.reuse_preflight_build:continue
        print('building',name,flush=True)
        run(['g++']+flags+opts+sources_+libs+['-o',build/name])
    for p in build.iterdir(): meta['artifacts'][str(p.relative_to(out))] = sha(p)
    save(out/'identity.json',meta)
    normal,sanitized=[],[]
    for flavor,records in [('gate',normal),('gate_san',sanitized)]:
        for cell in CELLS:
            x=inputs[cell]
            active_env=dict(env,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            raw=run(['taskset','-c',meta['cpu'],build/flavor,WS/x['rows_path'],WS/x['queries_path']],env=active_env)
            record=json.loads(raw.splitlines()[-1]);record['cell']=cell
            assert [a['arm'] for a in record['arms']]==['legacy','cost','adaptive']
            assert all(a['acceptance_checks']>0 and a['queries_counted']==x['queries'] for a in record['arms'])
            if flavor.endswith('_san'): assert record==normal[CELLS.index(cell)], ('sanitized drift',cell)
            records.append(record);save(out/('gates_sanitized.json' if flavor.endswith('_san') else 'gates.json'),records)
            print(flavor,cell,'passed',flush=True)
    raw,builds,process_gates=[],[],[]
    for cell in CELLS:
        x=inputs[cell]
        for process in range(5):
            lines=run(['taskset','-c',meta['cpu'],build/'bench',WS/x['rows_path'],WS/x['queries_path'],process],env=env)
            records=[dict(json.loads(line),cell=cell,process=process) for line in lines.splitlines()]
            timing=[r for r in records if r['phase']=='pass']; bg=[r for r in records if r['phase']=='build']; pg=[r for r in records if r['phase']=='gate']
            assert len(timing)==100 and len(bg)==5 and len(pg)==10
            for rep in range(10):
                group=[r for r in timing if r['repeat']==rep]
                assert {r['arm'] for r in group}==set(ARMS) and len({r['checksum'] for r in group})==1
                assert all(r['elapsed_ns']>0 and r['operations']==x['queries'] for r in group)
            assert all(g['verified']==x['rows']+x['queries'] for g in pg)
            raw+=timing;builds+=bg;process_gates+=pg
            save(out/'raw.json',raw);save(out/'builds.json',builds);save(out/'process_gates.json',process_gates)
            print('timing',cell,process+1,'/5',flush=True)
    for path,digest in meta['sources'].items(): assert sha(WS/path)==digest,('source drift',path)
    for path,digest in meta['libraries'].items(): assert sha(WS/path)==digest
    assert sha(profile)==meta['profile_sha256']
    for x in inputs.values():
        assert sha(WS/x['rows_path'])==x['rows_sha256'] and sha(WS/x['queries_path'])==x['queries_sha256']
    summary=summarize(raw,builds);save(out/'summary.json',summary);save(out/'decision.json',decide(summary));save(out/'identity.json',meta)
    log.close();data=(out/'commands.log').read_bytes()
    with (out/'commands.log.gz').open('xb') as f:
        with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as z:z.write(data)
    save(out/'log_archive.json',{'raw_sha256':hashlib.sha256(data).hexdigest(),'gzip_sha256':sha(out/'commands.log.gz')})
    (out/'commands.log').unlink()
    print('completed',len(raw),'timing rows',flush=True)


if __name__=='__main__':main()
