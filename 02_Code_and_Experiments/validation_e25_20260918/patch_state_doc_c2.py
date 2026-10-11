import io, sys

p = '/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/STRUCTURE_OPTIMIZATION_STATE.md'
s = io.open(p, encoding='utf-8').read()

old = u"""**源码身份（本轮之后）**：`index.hpp` 5,673 行 / `ec65540c0489bf9b798d3a3fb61fce054a8fc0723fbcee72c6c6889146e9ba8c`，
`switches.hpp` 165 行 / `e75c7fad79a1944af4b1ecc48761b8d8180568aa473d7f0a0f0872ef16d0880f`；两个适配包 manifest 已同步。
本轮对 `index.hpp` 的改动**只有诊断计数器与它们的 `#ifdef E25_DIAG` 包裹**，发布路径行为不变；
另外修掉了诊断程序自身的两处审计缺陷（详见结果文档第 6 节）。"""

new = u"""**源码身份（本轮之后）**：`index.hpp` 5,733 行 / `a5f042bb849f430ecbd571153e909fc4265db49b4abfbb0da47b849b19aea892`，
`switches.hpp` 165 行 / `e75c7fad79a1944af4b1ecc48761b8d8180568aa473d7f0a0f0872ef16d0880f`；两个适配包 manifest 已同步。
本轮对 `index.hpp` 的改动**只有诊断计数器与它们的 `#ifdef E25_DIAG` 包裹**，发布路径行为不变；
另外修掉了诊断程序自身的两处审计缺陷（详见结果文档第 6 节）。
低活跃开关与 C-2 的计数器都只活在 `-DE25_DIAG` 构建里；正确性门与 sanitizer 门在本轮身份下复跑通过。"""

old2 = u"""**下一步：C-2（缩到历史峰值）的入口——先测维护期新峰值事件率**，不能由快照余量推算；
E3（写路径分配）只有剩余成本证据指向分配时才进入。
不进入完整四方复测，不扩建协议。低活跃余下的问题是候选选择（把空块当候选），那是另一个候选，本轮不做。"""

new2 = u"""**C-2「缩到历史峰值」的入口也已跑完并关闭**（2026-10-10）：Term A 确实可回收（期末 0.37–5.26 MB），
但维持收缩的代价是它自身 **27–276 倍**的额外复制（mixed 替换臂：829.9 MB 额外搬移 vs 4.80 MB 可回收；
六格 27×–276×）。结果是**结构性**的：两个负载的溢出存量都在长，容器的峰值持续被刷新，
而「容量 == 峰值」意味着每次刷新都要分配＋整段复制，倍增原本把这些摊还掉了。
见 `../02_Code_and_Experiments/validation_e25_20260918/RESULTS_C2_PEAK_ENTRY.md`。
**分支 C 至此两条都关闭**（缩到活跃数：不耐久；缩到峰值：不值），`RESULTS_BRANCHC_ENTRY.md` 第 8 节已同步登记。

**下一步：E3（写路径分配）**——只有剩余成本证据指向分配时才进入，所以先做它的入口（分配次数与字节的归属），
不先写原型。不进入完整四方复测，不扩建协议。
低活跃余下的问题是候选选择（把空块变成一等候选），那是另一个候选，本轮不做。"""

for old_s, new_s in ((old, new), (old2, new2)):
    n = s.count(old_s)
    if n != 1:
        sys.stderr.write('MISMATCH count=%d for %r\n' % (n, old_s[:40]))
        sys.exit(2)
    s = s.replace(old_s, new_s)

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('UPDATED state doc')
