# 阶段1：上游完整 BlockLearn 参照与本项目同轮对照

日期：2026-10-18；起点`4b2ada2`工作树。依据[最新长期计划](../docs/LONG_TERM_BM_PLAN_20261018.md)阶段1。
**结论：上游完整工厂`block-residual-delta`对ART为1.704/1.372/1.352，三项均清晰更慢；
它与本项目BM页接入相差仅0.987–1.104（未分辨），说明瓶颈不是我们的接入方式，而是共有的预测/验证工作。
上游自带的`-shortdir-first`比`delta`快0.846/0.944/0.900，与页内短目录结论方向一致，但仍慢于ART。**

## 接了什么

| 臂 | 实现 | 来源 |
|---|---|---|
| `native-delta` | `block-residual-delta` | 上游完整工厂`make_block_residual_delta`，CMake目标`blocklearn_adapter` |
| `native-shortdir-first` | `block-residual-delta-shortdir-first` | 同上 |
| `bm-page-window` | 本项目BM页 + 窗口内纠正 | `bm_page::Index::find` |
| `bm-page-base` | 同页 + 安全窗口扫描 | `find_base` |
| `F5` | 自研模型冻结F5 bridge | `coupled_page_adapter.hpp` |
| `ART` | libart适配包 | 本项目适配器 |

九臂（三个AA对）全部用**同一新contract**（benchmark`003a55bf/src/index_contract.h`）、同一行/查询文件、
同一单线程`find`；BM页/F5/ART按该contract重编译，源码未改。上游按快照原样构建，
未改参数（`fit(1,7,8,1)`）或宏；该快照的`related_works`未初始化，因此上游自带ART/HOT基线不可用，
ART由本项目适配包测量，HOT因未解泄漏继续排除。全部臂对**131,072条行+查询**逐一验证通过，
六臂校验和相等；每十条查询替换为已证明缺席的后缀键（约90%命中）。

## 结果（越小越好；比值<1表示前者更快）

| 输入 | native-delta/ART | native-sdf/ART | native-delta/BM页 | sdf/delta | BM页/ART | F5/ART |
|---|---:|---:|---:|---:|---:|---:|
| english_words | **1.704** | **1.492** | 1.038 | **0.846** | **1.625** | 1.145 |
| memetracker_urls | **1.372** | **1.315** | 1.104 | 0.944 | **1.272** | **0.949** |
| memetracker_phrases | **1.352** | **1.194** | 0.987 | **0.900** | **1.314** | **0.938** |

本轮三对AA的最大观测带为1.122/1.179/1.119；加粗表示清晰（≥5%、≥4/5同向、超带）。

每请求耗时（ns，同轮同输入）：

| 输入 | delta | sdf | BM页base | BM页window | F5 | ART |
|---|---:|---:|---:|---:|---:|---:|
| english_words | 315.6 | 268.2 | 263.5 | 304.5 | 206.2 | 179.3 |
| memetracker_urls | 479.4 | 445.7 | 383.6 | 432.1 | 330.1 | 349.0 |
| memetracker_phrases | 442.2 | 396.8 | 377.8 | 442.9 | 299.5 | 330.9 |

## 判读

1. **完整工厂不是答案**：`native-delta`三格对ART清晰更慢（1.35–1.70），
   通用≤1.10目标未过。它相对本项目BM页为0.987–1.104、均未分辨，
   因此“我们接错了、完整工厂会更好”这一假设**未获支持**；两侧是同一套预测+验证工作在付费。
2. **上游自己的两项优化确有效果**：`-shortdir-first`把`delta`降到0.846/0.944/0.900
   （english_words与phrases清晰，URLs在1.179带内未分辨）。这与我们页内短目录
   （fence比较降到0.114、页级0.611）方向一致，说明“用紧凑码替代随机arena比较”是可复用的机制，
   而“预测块优先”本身不足以补上对ART的差距。
3. **自研控制反而更好**：F5在URLs/phrases为0.949/0.938（清晰更快），english_words为1.145。
   这进一步说明差距来自模型/定位组织，而不是语言或评测层。
4. **窗口纠正没有被真实数据支持**：`bm-page-base`在三格都比`bm-page-window`快
   （263.5对304.5、383.6对432.1、377.8对442.9），与BM首轮“纠正整体不采用”一致。

按计划阶段1的决策条款：两侧都慢→优先分析**共有的预测/验证工作**，不把失败归罪原算法，
也不继续堆接入工程。下一入口应是阶段2的“必要查询步骤”：先做短目录的完整读门
（原生`-shortdir-first`已是可用参照），再做预测块payload探测；cost/adaptive按计划另开一组。

## 边界与披露

- 只有命中+10%缺席，无动态操作、无clean/dirty两态、无HOT排名；不能外推混合读写或百万规模。
- 上游自带ART/HOT基线因快照`related_works`未初始化而不可用；ART由本项目适配器测量，
  两者适配层不同，但共用同一contract与同一`find`虚调用。
- `native-*`的`implementation_version`是上游自报的`blocklearn-prototype-v3`，
  不是逐字节源码身份；本节以`upstream_update_audit_20261018/identity.json`记录的逐文件哈希为准。
- 规格在计时前写入（[BM_NATIVE_SPEC](diagnostics/BM_NATIVE_SPEC_20261018.md)），判据未在结果后调整。
- 一次中止：驱动把`bulk_load/insert/scan`的枚举值打成未加引号的JSON，解析失败，未产生有效计时；
  修复后重跑，结果目录为首次提交的`bm_native_reference_20261018`。

## 证据

- 规格：[BM_NATIVE_SPEC_20261018.md](diagnostics/BM_NATIVE_SPEC_20261018.md)
- 原始记录：[results/bm_native_reference_20261018](results/bm_native_reference_20261018/)：
  `raw.json`(810行)、`process_gates.json`(135条门记录)、`summary.json`、`decision.json`、
  `identity.json`（35份源码/输入/库哈希）、`verification.json`、`commands.log.gz`。
- 上游构建：`builds/native/libblocklearn_adapter.a`、`libblocklearn_core.a`（哈希已记录，产物不入库）。
- 复核：`python3 -B 07_Eval_Integration/diagnostics/verify_bm_native.py <结果目录>` → `verified`。
