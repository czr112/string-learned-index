import hashlib, os
E = r'D:\StringLearnedIndex_Workspace\07_Eval_Integration'
V = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918'

def sha(p): return hashlib.sha256(open(p,'rb').read()).hexdigest()
idx = sha(os.path.join(V,'index.hpp')); gate = sha(os.path.join(V,'check_block_rank.cpp'))
tools = sha(os.path.join(V,'tools_build.py')); sw = sha(os.path.join(V,'switches.hpp'))
print('index', idx[:16], 'gate', gate[:16], 'tools', tools[:16], 'switches', sw[:16])

R = os.path.join(E, 'RESULTS_BLOCK_RANK_ENTRY_20261011.md')
s = open(R, encoding='utf-8', newline='').read()
def sub(t, a, r, label):
    if t.count(a) != 1: raise SystemExit('ANCHOR FAIL %s (%d)' % (label, t.count(a)))
    return t.replace(a, r)

s = sub(s, '| `validation_e25_20260918/index.hpp` | `46fdf520d82804e595727c267c428c5fbbb9d8c0da02e09ffe4ffc9f413a6957` |',
           '| `validation_e25_20260918/index.hpp` | `%s` |' % idx, 'idx row')
s = sub(s, '| `validation_e25_20260918/check_block_rank.cpp`（新门） | `c4505e385c7659d9d0cb3b6b4956a7dec3b473e7aa7ba43adff54334e7842265` |',
           '| `validation_e25_20260918/check_block_rank.cpp`（新门） | `%s` |' % gate, 'gate row')
s = s.rstrip() + '''

## 8. 并发编辑事故与恢复（记录，不改结论）

本轮与一个并行的独立审查代理同时进行，它在中途**改写了本轮的三个文件**（`index.hpp` 的
`#ifdef E25_DIAG` 拟合计时、`check_block_rank.cpp` 的整体重写、`tools_build.py` 的目标登记），
随后被中断。处置与理由：

- **`index.hpp`（已回退）**：它加了一个 `fit_ns` 计数器与两处 `steady_clock` 取样（`#ifdef E25_DIAG`，
  发货构建编译不到）。该路径**从未被执行过**，因此身份里不留未经验证的代码；已删除，index.hpp 由
  `46fdf520…` 变为 `%s`，四个适配包 manifest 与 `diagnostics/run_owner_lifecycle.py` 同步重钉。
- **`check_block_rank.cpp`（已重建）**：改写版是「按契约独立重实现 + 逐键对照 + 扫描检查 + 旋转负对照」
  的草稿，方向正确但**不完整**——它引用 `Region::diag_group`（树中不存在）与一份尚不存在的契约文档，
  且出现在本轮证据登记之后。已保留为 `scratch_blockaudit/agent_draft_check_block_model_backend.cpp`
  （41,899 B），本轮改用从补丁原文精确重建的门（`%s`）。
- **恢复后逐格重跑**：三语料 `checks = 324 / 315 / 325`，三个 sanitizer 单元 `204 / 199 / 201`，
  `failures = 0`，且 checksum、probes、比值与恢复前**逐位相同**——重建是语义等价的。

这件事本身的教训与项目既有的 S0/G0 记录同类：**并发的写入者必须被点名，否则身份声明会先于事实**；
本轮的最后一次构建记录（`build/check_block_rank.built.json`）与本表哈希一致，可作为绑定证据。
''' % (idx, gate)
s = s.replace('`tools_build.py` | `355d01857b6f…`', '')
open(R, 'w', encoding='utf-8', newline='').write(s)
print('results doc updated', len(s))

S = os.path.join(E, 'STRUCTURE_OPTIMIZATION_STATE.md')
t = open(S, encoding='utf-8', newline='').read()
t = sub(t, '身份已同步：index.hpp `46fdf520…`、', '身份已同步：index.hpp `%s`…、' % idx[:8], 'state idx hash')
t = sub(t, 'model/block_linear_rank.hpp `17d8714d…`、四个适配包 manifest、`diagnostics/run_owner_lifecycle.py`。',
           'model/block_linear_rank.hpp `17d8714d…`、门 `%s`…、四个适配包 manifest、`diagnostics/run_owner_lifecycle.py`。\\n  并发写入事故：一个审查代理中途改写了 index.hpp / check_block_rank.cpp / tools_build.py；前者（`#ifdef E25_DIAG` 拟合计时，从未执行）已回退并使 index.hpp 由 `46fdf520…` 变为 `%s…`，门的草稿保留在 `scratch_blockaudit/agent_draft_check_block_model_backend.cpp`，恢复后全部格子逐位复现（见 RESULTS §8）。' % (gate[:8], idx[:8]), 'state note')
open(S, 'w', encoding='utf-8', newline='').write(t)
print('state doc updated', len(t))
