# N3 尝试 1（失败）：块内再平衡直接崩溃，已回滚并记录

日期：2026-10-21。依据[N1 计数与 N2 登记](RESULTS_BM_N1_COUNTED_20261021.md)。

## 尝试内容

在 `bm_n1_page_core.hpp` 的 `try_insert` 中，把 `block.count==16 → Insert::rebuild` 换成
**原地块分裂**：把 16 个 entry 的后 8 个搬进新块，插入到 `blocks_`/`stable_fences_`/`fences_`/`digests_`
的 b+1 位置，`regions_[0].count++`，再按比较结果把新键插入到左半或右半。arena 字节不移动。

## 结果：**段错误（core dumped）**

第一个增长格（words 95/5）即崩溃，未产生任何计数输出。**改动已 `git checkout` 回滚**，
仓库中的 `bm_n1_page_core.hpp` 仍是"只加计数"的版本（N1 计数结果不受影响）。

## 失败原因（按现有证据推断，未逐项验证）

最可能是**模型与块数的耦合**：`Page` 在构造时用当时的块边界训练 `blocklearn::BlockModel`（`bm_`），
`directory_block()` 依据该模型预测块号，再以 `regions_[0].count` 夹取。插入新块后：

1. `regions_[0].count` 增加，但 `bm_` 仍按旧块数训练；
2. `stable_fences_` 右移一位，模型的训练输入与预测域不再对齐；
3. 预测出的块号可能落在 `blocks_.size()` 之外或指向错误块，导致越界写。

其余需要同时成立的不变量（`digests_` 与块一一对应、`fence_prefix_`、
`verify()` 的槽范围与槽序检查）在本尝试中均未验证。

## 判据结论

按 N2 登记的语义门（四语料全行/全查询/`verify()`/sanitizer/失败注入），本次尝试在**第一个增长格**
就不通过。依计划"两次合理尝试"的规则，还剩**一次**尝试机会。

## 若做尝试 2，必须先解决的前提

1. **模型与块数的解耦**：块分裂后要么重建该页的 `bm_`（代价回到 O(块数) 训练，
   仍远小于 O(行数) 重写），要么让 `directory_block` 在块数变化时退回纯二分查找；
2. **四个并行数组的一致性**：`blocks_`/`stable_fences_`/`fences_`/`digests_` 必须同时右移，
   并在 `verify()` 里增加"数组长度相等 + 每块 fence 与其首 entry 相同"的断言；
3. **先跑 `verify()` 再跑性能**：本轮尝试直接跑了轨迹，崩溃点没有定位到具体断言。

## 边界

- 本轮没有改任何生产文件；`bm_n1_page_core.hpp` 已回滚到计数版本，N1 结果仍然有效。
- 未测性能、未做语料门；失败发生在语义阶段。

---

## 尝试 2（进行中）：段错误已消除，改为 `verify()` 明确失败

同一机制的第二版做了三处修正：

1. `fences_` 是**每个 region 一条**（构造时 `fences_.push_back(rows[first].key)`），不是每块一条；
   `digests_` 也按 `fences_` 生成。第一版对二者做了 per-block 插入，索引越界→段错误。
   第二版**只插 `blocks_` 与 `stable_fences_`**，崩溃消失。
2. 用逐次 `verify()` 的探针定位：插入 64 次均通过（每次约 119,062 项检查、`splits=0`）；
   **第 65 次插入触发第一次块分裂后立即抛 `exception: size/memory`**。
3. 探针 `probe_bm_n3_split.cpp` 保留，可复现该点。

当前状态：机制在**第一次块分裂**处被 `Page` 自身的不变量检查拒绝（`size/memory`），
说明分裂后 `size_`、块总数、`stable_fences_` 长度或 region 计数之间仍有不一致。
这比第一次的段错误前进了一步：现在有确定的失败点与确定的异常信息。

代码已 `git checkout` 回滚，`bm_n1_page_core.hpp` 回到计数版本；探针与本文保留。
下一次尝试应从 `size/memory` 这条断言反查具体是哪个量不匹配，并在 `verify()` 里
分别断言 `blocks_.size()==stable_fences_.size()`、`regions_[0].count==blocks_.size()`、
`sum(block.count)==size_` 三项，逐项定位。
