import hashlib
P = r'D:\StringLearnedIndex_Workspace\07_Eval_Integration\STRUCTURE_OPTIMIZATION_STATE.md'
s = open(P, 'r', encoding='utf-8', newline='').read()

def sub(a, r, label):
    global s
    if s.count(a) != 1:
        raise SystemExit('ANCHOR FAIL %s (%d)' % (label, s.count(a)))
    s = s.replace(a, r)

sub('**下一阶段计划已制定（本次尚未实施）**：',
    '**下一阶段计划（P0–P2 已于同日执行，结论见下方「BlockLinearRank 收口」节）**：',
    'plan status')

anchor = '## 新模型接入对照（2026-10-11，第一轮已完成）：BlockLearn × 我方索引'
new_section = '''## BlockLinearRank 收口（2026-10-11，P0–P2 已执行）：正确但入口回归

结果：[RESULTS_BLOCK_RANK_ENTRY_20261011.md](RESULTS_BLOCK_RANK_ENTRY_20261011.md)；独立审查：
[REVIEW_BLOCK_MODEL_CONTRACT_20261011.md](REVIEW_BLOCK_MODEL_CONTRACT_20261011.md)。

- **做了什么**：新增 `model/block_linear_rank.hpp`（队友 @ 60837a88 的 9-bit 编码 + 分段线性拟合，
  固定 block_size=1 / 7 字节 / 8 段，无参数搜索；只存前缀长度不存视图），在 `index.hpp` 接成第三个
  `ModelBackend`（默认仍是 `Context16`），并修掉 `switches.hpp` 里对三值枚举仍按二值比较的缺陷
  （该缺陷会让「Context16 vs BlockLinearRank」被判为无差异，正是差异表要防的空洞通过）。
- **正确性**：`encode` 12/12 与上游逐位一致；`fit` 分段表逐字段一致（唯一有意差异是 n=1 边界）；
  新门 `check_block_rank` 三语料 131072 全绿（324/315/325 项检查，0 失败），
  三个 sanitizer 单元全绿（204/199/201 项，ASan/UBSan 无诊断）。
- **入口读数**（入口自身的查询模式，两后端逐区域配对）：主块访问中位比值 mixed 2.84、skew 3.79、
  tranco 2.05；变差区域 62/64、64/64、64/64；skew 完整键比较 205,280→438,279。
- **机制**（独立审查的两族反例）：①坐标饱和——区域前缀之后第 8 个字节不进模型，C3 反例
  2000 个不同键塌成 1 组；②分箱不均匀——C6 反例 32/64 组空置、最大组为平均的 8.5 倍。
- **处置**：按计划 §6「映射造成明显集中 → 先报告瓶颈，不启动大矩阵」，**不开 P3 A/B**；后端保持
  默认关闭。身份已同步：index.hpp `46fdf520…`、switches.hpp `f09dc3f6…`、
  model/block_linear_rank.hpp `17d8714d…`、四个适配包 manifest、`diagnostics/run_owner_lifecycle.py`。
- **下一步三选一**（一轮只做一项）：①结构侧**等深阈值目录**（用区域内键的 63-bit 坐标直接构造
  G 个组阈值，按构造既保序又均衡，模型无关，≈2 KB/区域）；②把两族反例交回模型负责人后再接一次；
  ③按计划回到 M4 有序段归并。

'''
sub(anchor, new_section + anchor, 'insert section')

open(P, 'w', encoding='utf-8', newline='').write(s)
print('state doc updated', hashlib.sha256(open(P,'rb').read()).hexdigest())
