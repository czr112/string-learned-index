"""Freeze the two requested upstream updates and prepare a separate BM run.

The completed 60837a8 semantic run and failed HOT preflight stay untouched.
No timing from the failed preflight was observed before this protocol revision.
"""
import datetime
import hashlib
import json
from pathlib import Path
import subprocess

WORKSPACE = Path(__file__).resolve().parents[2]
ROOT = WORKSPACE / '07_Eval_Integration'
HERE = Path(__file__).resolve().parent

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def git(path, *args): return subprocess.check_output(['git', *args], cwd=path).decode('utf-8').strip()
def put(name, text):
    target = HERE / name
    if target.exists():
        assert target.read_text(encoding='utf-8') == text, ('existing generated file differs', name)
    else:
        target.write_text(text, encoding='utf-8', newline='\n')
def change(text, old, new):
    assert old in text, old
    return text.replace(old, new)

def main():
    out = ROOT / 'results/upstream_update_audit_20261018'
    out.mkdir(exist_ok=True)
    lock = {'checked_at': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'repositories': {}}
    for name, folder, url, before, after in [
        ('benchmark', 'benchmark_003a55bf', 'https://github.com/zsh20070718/learned-index',
         '403de526a9104147d5d2659b6882c126c1323b5f', '003a55bf674b43350fbb70242070fb74f33a56ce'),
        ('model', 'model_e8e05411', 'https://github.com/Zephyr761/Learned-Index-Model',
         '60837a886b7040828e6f0bb5382d7950d8eed4fa', 'e8e05411135b222edb82e3861799a2ae07ae5470')]:
        path = ROOT / 'upstream_snapshots' / folder
        assert git(path, 'rev-parse', 'HEAD') == after
        assert not git(path, 'status', '--porcelain')
        remote = git(path, 'ls-remote', url, 'HEAD', 'refs/heads/main')
        assert all(row.split()[0] == after for row in remote.splitlines())
        files = git(path, 'ls-files').splitlines()
        source_hashes = {n: sha(path/n) for n in files if (path/n).is_file()}
        gitlinks = {n: git(path, 'rev-parse', 'HEAD:'+n) for n in files if not (path/n).is_file()}
        item = {'url': url, 'previous_commit': before, 'commit': after,
                'snapshot_path': str(path.relative_to(WORKSPACE)).replace('\\', '/'),
                'commit_info': git(path, 'show', '-s', '--format=%H %cI %s', 'HEAD'),
                'remote_heads': remote, 'tracked_files_sha256': source_hashes, 'gitlinks_not_initialized': gitlinks}
        lock['repositories'][name] = item
        for label, args in [('changes', ['diff', '--stat', before, after]),
                            ('commits', ['log', '--format=%H %cI %s', before+'..'+after]),
                            ('diff', ['diff', before, after, '--', 'src', 'docs', 'tests', 'CMakeLists.txt'])]:
            (out/(name+'_'+label+'.txt')).write_text(git(path, *args)+'\n', encoding='utf-8')
    if (out/'identity.json').exists():
        previous = json.loads((out/'identity.json').read_text(encoding='utf-8'))
        assert previous['repositories'] == lock['repositories'], 'upstream audit drift'
    else:
        (out/'identity.json').write_text(json.dumps(lock, indent=2)+'\n', encoding='utf-8')
    lock_path = "ROOT/'results/upstream_update_audit_20261018/identity.json'"
    model_path = "ROOT/'upstream_snapshots/model_e8e05411'"
    benchmark_path = "ROOT/'upstream_snapshots/benchmark_003a55bf'"

    text = (HERE/'run_bm_page.py').read_text(encoding='utf-8')
    text = change(text, "UPSTREAM=WORKSPACE/'02_Code_and_Experiments/Learned-Index-Model'", 'UPSTREAM='+model_path+'\nBENCHMARK='+benchmark_path)
    text = text.replace('bm_page_20261018', 'bm_latest_page_20261018')
    start = text.index("    pin=read(ROOT/'results/blocklearn_identity_20261011.json')")
    end = text.index('    for path in [Path(__file__)', start)
    text = text[:start] + f'''    lock=read({lock_path});pin=lock['repositories']['model']
    assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=UPSTREAM,text=True).strip()==pin['commit']
    upstream_identity=[]
    for name in ('src/block_model.h','src/block_model.cpp','src/key_bytes.h','src/query_cost.h'):
        path=UPSTREAM/name;data=path.read_bytes()
        assert data==subprocess.check_output(['git','show','HEAD:'+name],cwd=UPSTREAM)
        assert sha(path)==pin['tracked_files_sha256'][name]
        upstream_identity.append({{'path':name,'compiled_sha256':sha(path),'matches_fixed_git_blob':True}})
        sources[str(path.relative_to(WORKSPACE))]=sha(path)
    for name in ('src/index_contract.h','src/unified/runner.h','src/unified/runner.cpp'):
        path=BENCHMARK/name;assert sha(path)==lock['repositories']['benchmark']['tracked_files_sha256'][name]
        sources[str(path.relative_to(WORKSPACE))]=sha(path)
    sources[str(({lock_path}).relative_to(WORKSPACE))]=sha({lock_path})
''' + text[end:]
    text = change(text, "'BM_PAGE_SPEC_20261018.md')", "'BM_PAGE_SPEC_20261018.md','BM_LATEST_SPEC_20261018.md','prepare_bm_latest.py')")
    text = change(text, "'fit':{'block_size'", "'benchmark_commit':lock['repositories']['benchmark']['commit'],'model_snapshot':str(UPSTREAM.relative_to(WORKSPACE)),\n          'fit':{'block_size'")
    text = text.replace("ROOT/'learned-index/src'", "BENCHMARK/'src'").replace("ROOT/'learned-index/src/unified/runner.cpp'", "BENCHMARK/'src/unified/runner.cpp'")
    text = change(text, "flags=['-std=c++17'", "flags=['-pthread','-std=c++17'")
    put('run_bm_latest_page.py', text)

    text = (HERE/'bench_bm_page_read.cpp').read_text(encoding='utf-8')
    text = change(text, '#include "../adapters/hot/hot_adapter.h"\n', '')
    text = text.replace('ART/HOT', 'ART')
    text = change(text, 'auto art=make_art_index({}),hot=make_hot_index({});art->bulk_load(loaded);hot->bulk_load(loaded);', 'auto art=make_art_index({});art->bulk_load(loaded);')
    text = change(text, '||hot->update(key,value)!=sib::UpdateResult::updated', '')
    text = change(text, ',hot.get(),hot.get()', '')
    text = change(text, ',"HOT","HOT_AA"', '')
    text = change(text, 'verify_scan(hot->scan("",SIZE_MAX));', '')
    text = change(text, '0,1,11,2,10,3,9,4,8,5,7,6', '0,1,9,2,8,3,7,4,6,5')
    text = text.replace('repeat<12', 'repeat<10').replace('position<12', 'position<10').replace('%12', '%10')
    put('bench_bm_latest_read.cpp', text)

    text = (HERE/'run_bm_page_read.py').read_text(encoding='utf-8')
    text = text.replace('ART/HOT', 'ART').replace('4320', '3000')
    text = change(text, "MODEL=WORKSPACE/'02_Code_and_Experiments/Learned-Index-Model'", 'MODEL='+model_path+'\nBENCHMARK='+benchmark_path)
    text = text.replace("'F5','ART','HOT']", "'F5','ART']").replace(",'BM/HOT'", '')
    text = text.replace('bm_page_20261018', 'bm_latest_page_20261018').replace('bm_page_read_20261018', 'bm_latest_read_20261018')
    text = text.replace('bench_bm_page_read.cpp', 'bench_bm_latest_read.cpp').replace('BM_READ_SPEC_20261018.md', 'BM_LATEST_SPEC_20261018.md')
    text = text.replace('==720', '==500').replace('==144', '==100').replace('range(12)', 'range(10)').replace('%12', '%10')
    text = text.replace('0,1,11,2,10,3,9,4,8,5,7,6', '0,1,9,2,8,3,7,4,6,5')
    text = change(text, "'processes':5,'repeats':12", "'processes':5,'repeats':10,'benchmark_commit':source_meta['benchmark_commit'],\n          'excluded_baseline':{'HOT':'bm_page_read_20261018/failure.json: LSan 96-byte leak before any timing'},")
    # Remove the extra separator introduced above, retaining valid dictionary syntax.
    text = text.replace("before any timing'},,", "before any timing'},")
    text = change(text, "(('art','ART'),('hot','HOT'))", "(('art','ART'),)")
    text = change(text, "'-D'+opt+'_BUILD_ACCEPTANCE=ON'", "'-D'+opt+'_BUILD_ACCEPTANCE=ON','-DSTRING_INDEX_CONTRACT_INCLUDE_DIR='+str(BENCHMARK/'src')")
    text = text.replace("ROOT/'learned-index/src'", "BENCHMARK/'src'").replace("ROOT/'learned-index/src/unified/runner.cpp'", "BENCHMARK/'src/unified/runner.cpp'")
    text = change(text, "flags=['-std=c++17'", "flags=['-pthread','-std=c++17'")
    text = text.replace('==145', '==101')
    put('run_bm_latest_read.py', text)

    text = (HERE/'run_bm_adapter.py').read_text(encoding='utf-8')
    text = change(text, "MODEL=WORKSPACE/'02_Code_and_Experiments/Learned-Index-Model'", 'MODEL='+model_path+'\nBENCHMARK='+benchmark_path)
    text = text.replace('bm_adapter_20261018', 'bm_latest_adapter_20261018').replace('bm_page_20261018', 'bm_latest_page_20261018')
    text = text.replace("ROOT/'learned-index/src/index_contract.h'", "BENCHMARK/'src/index_contract.h'").replace("ROOT/'learned-index/src'", "BENCHMARK/'src'")
    text = change(text, "('block_model.h','block_model.cpp','key_bytes.h')", "('block_model.h','block_model.cpp','key_bytes.h','query_cost.h')")
    text = change(text, "'upstream_commit':base['upstream_commit']", "'upstream_commit':base['upstream_commit'],'benchmark_commit':base['benchmark_commit']")
    put('run_bm_latest_adapter.py', text)

    text = (HERE/'verify_bm_page.py').read_text(encoding='utf-8')
    text = text.replace(",'HOT'", '').replace('60837a886b7040828e6f0bb5382d7950d8eed4fa', 'e8e05411135b222edb82e3861799a2ae07ae5470')
    text = change(text, "WORKSPACE/'02_Code_and_Experiments/Learned-Index-Model'", model_path)
    line = "        assert hashlib.sha256(data.replace(b'\\r\\n',b'\\n').replace(b'\\n',b'\\r\\n')).hexdigest()==item['historical_sha256']\n"
    text = change(text, line, '')
    text = text.replace('4320', '3000').replace('==720', '==500').replace('==144', '==100').replace('range(12)', 'range(10)').replace('%12', '%10')
    text = text.replace('0,1,11,2,10,3,9,4,8,5,7,6', '0,1,9,2,8,3,7,4,6,5')
    text = text.replace("('art','hot')", "('art',)")
    put('verify_bm_latest.py', text)
    print('Frozen latest upstreams; generated separate semantic, read, adapter runners and verifier.')

if __name__ == '__main__': main()
