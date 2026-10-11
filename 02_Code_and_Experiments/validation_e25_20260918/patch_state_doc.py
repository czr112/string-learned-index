import io, sys

p = '/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/STRUCTURE_OPTIMIZATION_STATE.md'
s = io.open(p, encoding='utf-8').read()

old_head = u'更新：2026-10-09。用户明确主任务为索引结构优化。'
new_head = u'更新：2026-10-10。用户明确主任务为索引结构优化。'

old_sec = u'## 当前决策（集成完成后）'
new_sec = u'## 当前决策（低活跃回收入口轮收口之后）'

old_body = u"""紧凑导航已集成到 Index，`compact_region_routing` 默认 false。
源码身份为 `3f662343e4e57a92e758385d3c95638f01e71737dc01ff1d42c4a11e4823b749`，两个适配包 manifest 已同步。
集成结果见 `../02_Code_and_Experiments/validation_e25_20260918/RESULTS_COMPACT_ROUTING.md`。
集成 A/B 的 tranco 比值为 0.784/0.737；mixed/skew 四格未分辨。

**证据范围需收窄**：集成基准先从初始键生成查询，再完成 FIFO 替换至 200% 后查询。
初始键已全部删除；240 条原始记录的 checksum 均等于两百万次未命中的累加值。
上述查询收益目前属于更新后的查空负载，不能直接当作命中查询或静态构建收益。
正确性门的命中验收与性能基准的命中率是两回事。

**下一步：沿用现有入口补静态与更新后存活键查询，然后转 `n<2` 回收。**
不进入完整四方复测，不扩建协议。低活跃回收实验中两臂都关闭紧凑导航，保持单一变量。
具体对照、顺序和停止条件见 `../docs/NEXT_ACTION_PLAN_20261009.md`。"""

new_body = u"""低活跃子块回收（`reclaim_low_activity`）的**入口诊断轮已收口：负结果，不进入计时轮**。
结果见 `../02_Code_and_Experiments/validation_e25_20260918/RESULTS_LOW_ACTIVITY_ENTRY.md`。
一句话：分支够得着的机会只把溢出插入改写了 0.03%–0.84%（最好的格子是 mixed/E31 的 −346 次，占该段 131,072 次插入的 0.26%），
而**真正大的那批拒绝——85% 的字节拒绝落在空块上——根本不在这个入口的候选范围里**：`first_candidate()` 不选空块。
消掉那批拒绝的是 `reclaim_widest`（G3/A1 的 widening，已独立测过，仍是最终组合的候选）。

**源码身份（本轮之后）**：`index.hpp` 5,673 行 / `ec65540c0489bf9b798d3a3fb61fce054a8fc0723fbcee72c6c6889146e9ba8c`，
`switches.hpp` 165 行 / `e75c7fad79a1944af4b1ecc48761b8d8180568aa473d7f0a0f0872ef16d0880f`；两个适配包 manifest 已同步。
本轮对 `index.hpp` 的改动**只有诊断计数器与它们的 `#ifdef E25_DIAG` 包裹**，发布路径行为不变；
另外修掉了诊断程序自身的两处审计缺陷（详见结果文档第 6 节）。

**一处必须记账的身份漂移**：本文件与两份 manifest 之前写的是 `index.hpp` 5,593 行 / `3f662343…`，
但本轮开工时磁盘上已经是 5,629 行 / `44702000…`——低活跃开关与它的四张开关表在那之前就落盘了，文档没跟上。
紧凑导航本身的证据（`compact_region_routing` 默认 false）不受影响，见 `RESULTS_COMPACT_ROUTING.md`：
tranco 五个格子方向 5/5、比值 0.771–0.826，mixed/skew 未分辨。

**下一步：C-2（缩到历史峰值）的入口——先测维护期新峰值事件率**，不能由快照余量推算；
E3（写路径分配）只有剩余成本证据指向分配时才进入。
不进入完整四方复测，不扩建协议。低活跃余下的问题是候选选择（把空块当候选），那是另一个候选，本轮不做。"""

for old, new in ((old_head, new_head), (old_sec, new_sec), (old_body, new_body)):
    n = s.count(old)
    if n != 1:
        sys.stderr.write('MISMATCH count=%d for %r\n' % (n, old[:30]))
        sys.exit(2)
    s = s.replace(old, new)

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('UPDATED state doc')
