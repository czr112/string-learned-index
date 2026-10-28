"""Write the clean-rebuild comparison record and point the docs at it."""
import hashlib
import json
import pathlib

ws = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace')
root = ws / '07_Eval_Integration'
out = root / 'results/bm_clean_rebuild_20261019'
out.mkdir(exist_ok=False)
fresh = ws / 'builds/rebuild_20261019/clean'
recorded_round = root / 'results/bm_space_20261019b'
round_identity = json.loads((recorded_round / 'identity.json').read_text(encoding='utf-8'))
capacity = json.loads((recorded_round / 'capacity.json').read_text(encoding='utf-8'))
reference = [x for x in capacity if x['arm'] == 'bm_base' and x['cell'] == 'english_words'
             and x['process'] == 0][0]


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


comparison = {
    'rebuild_dir': str(fresh.relative_to(ws)).replace('\\', '/'),
    'rebuilt_sha256': sha(fresh),
    'recorded_sha256': round_identity['artifacts']['build/clean'],
    'binary_identical': sha(fresh) == round_identity['artifacts']['build/clean'],
    'single_cell': 'bm_base x english_words',
    'fields': {
        'checksum': [reference['checksum'], 507521454403758900],
        'hits': [reference['hits'], 58983],
        'semantic_checks': [reference['semantic_checks'], 262144],
        'self_report_owned': [reference['self_report']['owned'], 1944569],
        'input_heap_bytes': [reference['input_heap_bytes'], 2688276],
    },
    'build_ns': [reference['build_ns'], 10324511],
    'deterministic_fields_match': all(a == b for a, b in [
        (reference['checksum'], 507521454403758900),
        (reference['hits'], 58983),
        (reference['semantic_checks'], 262144),
        (reference['self_report']['owned'], 1944569),
        (reference['input_heap_bytes'], 2688276)]),
    'scope': 'build and one-cell semantics only; the full round was not re-run',
    'recorded_round': str(recorded_round.relative_to(ws)).replace('\\', '/'),
    'recorded_round_identity_sha256': sha(recorded_round / 'identity.json'),
}
(out / 'comparison.json').write_text(json.dumps(comparison, indent=2) + '\n', encoding='utf-8')
assert comparison['binary_identical'] and comparison['deterministic_fields_match']

state = root / 'STRUCTURE_OPTIMIZATION_STATE.md'
text = state.read_text(encoding='utf-8')
anchor = '**当前汇总入口：[BM 主线当前配置、总账与证据索引](RESULTS_BM_FINAL_TABLE_20261019.md)**'
assert anchor in text
text = text.replace(anchor, anchor +
    '\n阶段6第二项（干净重建复现）已完成：[同一账本二进制逐字节相同、单格运行逐字段相同]'
    '(RESULTS_BM_CLEAN_REBUILD_20261019.md)。')
state.write_text(text, encoding='utf-8')

plan = pathlib.Path(str(ws / 'docs/LONG_TERM_BM_PLAN_20261018.md'))
text = plan.read_text(encoding='utf-8')
text = text + ('\n\n阶段6第二项（干净重建复现）已完成：从源树重编该轮账本二进制得到**逐字节相同**的 ELF，'
               '并用它跑 `bm_base`×english_words，checksum/hits/semantic_checks/自计容量逐字段与记录相同'
               '（仅 build_ns 不同）。见[复现结果](../07_Eval_Integration/RESULTS_BM_CLEAN_REBUILD_20261019.md)。'
               '剩余：分块 COW 集成与15%读预算、24格动态矩阵、最终正式配置。')
plan.write_text(text, encoding='utf-8')

readme = pathlib.Path(str(ws / 'README.md'))
lines = readme.read_text(encoding='utf-8').split('\n')
res, done = [], 0
for line in lines:
    if line.startswith('- **当前优先任务**：'):
        res.append('- **阶段6干净重建复现（10月19日）**：[账本二进制逐字节相同](07_Eval_Integration/RESULTS_BM_CLEAN_REBUILD_20261019.md)：重编得到相同 ELF，单格 checksum/命中/自计容量逐字段与记录一致。')
        res.append('- **当前优先任务**：[分块 COW 集成与24格动态矩阵](docs/LONG_TERM_BM_PLAN_20261018.md)。阶段6简表与干净复现已完成；下一步接根机制并重过15%读预算，再扩24格与ART/Context16同能力对照，最后正式配置收口。')
        done += 1
    else:
        res.append(line)
assert done == 1
readme.write_text('\n'.join(res), encoding='utf-8')
print('recorded and docs updated')
