# 四步计划 第 1 步（进行中）：有界候选的 `resident_` 缺陷已修复并验证

日期：2026-10-24。承接[第二轮更正](CORRECTION_BM_TOTAL_20261024.md) 的第 1 项。

## 复核指出的问题（静态阅读）

性能测试跑的是**有界** `bm_split_page::Index`，但：

- 非有界 `bm_n1_page::Index` 在块分裂后**更新 `resident_`**；
- 有界版插入路径**没有对应更新**，而它的 `verify()` 仍要求 `resident_ == memory_bytes()`。

## 核对结果：确认缺陷存在

逐行对比两条插入路径，有界版确实缺失：

```cpp
// 非有界版（bm_n1_index.hpp）
const auto before_bytes=it->second.core->memory().total();
const auto result=it->second.core->try_insert(key,value);
if(result==Page::Insert::inserted){
    ++maintenance_.inplace_inserts;
    const auto after_bytes=it->second.core->memory().total();
    if(after_bytes>before_bytes)resident_+=after_bytes-before_bytes;
    ...
```

有界版（`bm_split_index.hpp`）缺少 `before_bytes`/`after_bytes` 与 `resident_` 增量。
这会让有界版在**发生块分裂后** `resident_` 落后于真实结构，`verify()` 随即失败。

## 修复与验证

把同样的增量逻辑补进有界版（分裂会增大页目录，必须同步账本）。随后用
`gate_bm_split_verify.cpp` 直接驱动**有界** `bm_split_page::Index`：构造 → 验证 →
同块密集插入（强制分裂）→ 再次验证 → 全键读回 → 全量 scan。

| 密集插入 | 验证项（两次合计） | 失配 | 重建 | 页内 | 终态行数 |
|---:|---:|---:|---:|---:|---:|
| 100 | 297,513 | **0** | 5 | 95 | 65,636 |
| 1,000 | 299,565 | **0** | 30 | 970 | 66,536 |
| 3,000 | 304,099 | **0** | 83 | 2,917 | 68,536 |

三档全部通过：**有界版在块分裂后 `verify()` 仍然成立**，答案与终态 scan 一致。

## 第 1 步剩余

| 项 | 状态 |
|---|---|
| 有界版 `resident_` 一致 | ✅ 已修并验证 |
| 有界版上的**异常注入** | ⬜ 未做（此前跑的是非有界版） |
| 有界版上的**真实空间账本** | ⬜ 未做（迭代器/P3 用的是非有界版） |
| 有界版上的**密集分裂差分**（对照基线逐次） | ⬜ 未做 |
| 完整源码身份（core/adapter 真实哈希） | ✅ 已在 `st_p1.sh` 绑定 |

## 结论

复核发现的是一个**真实缺陷**，不是文档问题：交付候选（有界版）此前在块分裂后自计常驻会滞后。
现在已修复，并有 297k–304k 项验证记录支撑。

第 1 步其余三项（异常注入、空间账本、密集差分）必须**改到有界版**重跑，之后才谈正式性能验收。

## 证据

- 修复：[bm_split_index.hpp](diagnostics/bm_split_index.hpp)（插入路径 `resident_` 增量）
- 验证程序：[gate_bm_split_verify.cpp](diagnostics/gate_bm_split_verify.cpp)
- 运行：[tools/s1_verify.sh](../tools/s1_verify.sh)
