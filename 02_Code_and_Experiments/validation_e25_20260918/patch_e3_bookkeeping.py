import io, json, sys

new_hash = 'f573480d5ab602cb29de68ffc8e1ff480f4ad9b89c4a5049cea0bf69d0687f0a'
old_hash = 'a5f042bb849f430ecbd571153e909fc4265db49b4abfbb0da47b849b19aea892'
for f in ('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/adapters/our_index/benchmark-adapter.json',
          '/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/adapters/our_index_lits/benchmark-adapter.json'):
    s = io.open(f, encoding='utf-8').read()
    n = s.count(old_hash)
    if n != 1:
        sys.stderr.write('hash count=%d in %s\n' % (n, f)); sys.exit(2)
    s = s.replace(old_hash, new_hash)
    i = s.find('"index_hpp_note":')
    j = s.find('\n', i)
    note = ('"index_hpp_note": "updated 2026-10-10 (E3 write-path entry round): index.hpp 5,832 lines, '
            'DIAGNOSTIC ONLY -- the E3 write-path clocks live behind E30_WRITE and the C-2 trackers '
            'behind E25_DIAG. Identity trail: 5423 -> 5593 (routing) -> 5629 (low-activity switch) '
            '-> 5673 (low-activity counters) -> 5733 (C-2 trackers) -> 5832 (E3 write-path clocks). '
            'Evidence: 02_Code_and_Experiments/validation_e25_20260918/RESULTS_E3_ENTRY.md",')
    s = s[:i] + note + s[j:]
    json.loads(s)
    io.open(f, 'w', encoding='utf-8', newline='').write(s)
    print('UPDATED', f)

# ---- state document ----
p = '/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/STRUCTURE_OPTIMIZATION_STATE.md'
s = io.open(p, encoding='utf-8').read()
old = u"""**下一步：E3（写路径分配）**——只有剩余成本证据指向分配时才进入，所以先做它的入口（分配次数与字节的归属），
不先写原型。不进入完整四方复测，不扩建协议。
低活跃余下的问题是候选选择（把空块变成一等候选），那是另一个候选，本轮不做。"""
new = u"""**E3（分配、复制与临时工作区）的入口也已跑完并按其原判据关闭**（2026-10-10）：
写路径上没有一样东西叫"分配是大头"——短命临时对象约每次插入一个、容器扩容 3–91 次/千次操作，
两者在任何一格里都不是首位；**字节质量是"搬移"**：溢出容器每次替换移动 68–124 条记录（≈3.3–6.0 KB/op），
而真实写入只有 ~80 B/op（**40–77 倍**）。时间侧的第一位随负载而变：
替换负载是"区域内检索（26–49%）＋放置（40–50%）＋区域 erase（60–81%）"，纯插入负载是"区域分裂重建（44–66%）"。
见 `../02_Code_and_Experiments/validation_e25_20260918/RESULTS_E3_ENTRY.md`。
**因此没有新候选**：证据指向的两处（溢出记录搬移 → `overflow_stable_store`／O1；插入路径的检索与放置）
**都已经在册**，不需要再开一个。

**下一步的选择权在用户**：本轮已把计划书里排队的两个入口（低活跃回收、分支 C-2）与 E3 全部收口，
`OPTIMIZATION_PLAN_V2.md` §6 的候选行至此要么已关闭、要么已在册（O1 稳定存储、`reclaim_widest`、紧凑导航）。
若要继续，建议的顺序是：把已在册的候选做成**组合轮**（同轮验收表含 `query_ms` 与 `scan`，见 A3 约束），
或转读路径的下一项；不进入完整四方复测，不扩建协议。"""
for old_s, new_s in ((old, new),):
    n = s.count(old_s)
    if n != 1:
        sys.stderr.write('state doc mismatch %d\n' % n); sys.exit(3)
    s = s.replace(old_s, new_s)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('UPDATED state doc')