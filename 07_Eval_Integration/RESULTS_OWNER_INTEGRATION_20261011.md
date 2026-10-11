# M3 集成轮结果：默认关闭开关进生产源码；OFF 逐位不变、ON 复现原型

日期：2026-10-11。承接 [M3 入口](RESULTS_OWNER_ENTRY_20261011.md)、[M3 原型](RESULTS_OWNER_PROTOTYPE_20261011.md)。
**本轮改动了生产源码**（新身份）：
`index.hpp` = `b20c83cab42787078508d5af6dd8a9e3c512fb0731618b9b0b81dc8fc64056d6`（6,376 行）、
`switches.hpp` = `72856bdc89f994e489c5b41c5e87a21dbafb51873c41f08e2550b71efc3edba7`。
原始记录：`results/owner_integration_20261011/`；运行器 `diagnostics/run_owner_integration.py`
（判据直接 import 原型轮的 `analyse()`，避免出现第二套口径）。

---

## 0. 结论

**集成成立。** 开关 `lits_skip_reference_table` 默认 false 进入生产 `Config`，四处开关表全部登记；
**OFF 路径逐字段不变**（生产头文件入口装置六格记录与入口轮记录逐字段相同，不是「门还绿」）；
**ON 路径复现原型**（节省 = 理论值、Context16 控制臂 0、四阶段 × 六格无「可分辨 >5% 回归」、
ASan/UBSan 六格门全绿）。默认值**未翻转**，采用与有限泛化验收留给路线图 §6。

| 语料 / 后端 | 节省 | 理论值 | 占记账 |
|---|---:|---:|---:|
| tranco / LITS | 2,113,536 B | 2,113,536 B | 6.05% |
| skew / LITS | 1,056,768 B | 1,056,768 B | 2.63% |
| mixed / LITS | 1,007,232 B | 1,007,232 B | 2.74% |
| 三个 Context16 格 | 0 | 0 | 控制臂 |

计时（五进程 × 六重复）：24 项「中位 >1.05 且 ≥4/5 更慢 且超 A/A 底」全部不成立。
目标格 tranco/LITS 构建中位 0.981（4/5 更快，但未超过该格底 1.110——**不记收益**），
点查 0.954（5/5 更快，未超过底 1.132）——与设计一致：集成版**训练照旧**，只释放常驻，
所以构建时间不变（这是与原型 0.896 的差别所在，报告如实区分）。

## 1. 改动内容

- `Config::lits_skip_reference_table`（默认 false）：为真时，LITS 校准成功的区域在映射确定后
  `Model::Table().swap(model_.cumulative)` 释放那张永远不读的参考表。训练仍执行 → **纯空间候选**。
- **显式拒绝而不是静默降级**：该区域上提供 preset 时抛 `std::logic_error`（digest 依赖该表）；
  `group_of()` 在无表区域抛 `std::logic_error`（需要参考答案的调用方必须持有独立对象）；
  新增 `reference_table_resident()` 只读访问器。
- `switches.hpp` 四处登记：`baseline_config`、`pin_frozen_switches`、`switches()`、differ 字段表
  （四处各自唯一命中，运行器逐条断言）。
- 四个适配包同步新身份，且 `our_index_adapter.cpp` 在唯一 Config 站点**断言该开关必须为 off**
  （未来有人改动 `baseline_config()`/`pin_frozen_switches()` 时在这里失败，而不是静默测量另一个索引）。

## 2. 一起事故与修复（必须与上面的结论一起读）

**事故**：在给四个适配包加上述断言时，我用了一个 PowerShell 循环：
它读取 `adapters/our_index/our_index_adapter.cpp`，随后对三个**同名的兄弟包文件**读取失败——
而这三个文件**从来不存在**（四个包通过 `add_subdirectory(../our_index)` 共用这一个源文件）。
PowerShell 在读取失败后沿用了上一次的缓冲，于是把累积文本逐个写出，最后该文件本身也被写成了
**18,118 字节的零**。工作区里没有该文件的任何副本（`rg`/全盘查找/构建树/`__pycache__`/归档全部查过），
**2026-10-09 版原文无法逐字节恢复**。

**修复**：按它必须满足的全部接口**重建**该文件——`our_index_adapter.h`、四个 CMakeLists 的编译定义、
共享的 `adapters/common/contract_acceptance.cpp`、`ADAPTER_NOTES.md`、以及**每份 result.json 里逐字记录的
descriptor** 都完好。重建版通过同一套验收：

| 包 | 验收 | 记录基线 |
|---|---|---|
| our_index（Context16） | **100/100** | 100/100 |
| our_index_lits（LITS） | **101/101** | 101/101 |

descriptor 的 `implementation`、`adaptation_notes`、键域（LITS：`max_bytes=4096`、`allows_nul=false`、
`allows_high_bit=false`；Context16：无上界、允许 NUL/高位）与 support 位与记录逐字一致。
重建版源码哈希 `22719a30d6f65dd44ff253ee7b0df122d1545a0ab75ec61fc84500a445689847`；
manifest 的 `source_identity` 只绑定 `index.hpp`/`switches.hpp`（不绑适配器源码），故四个 manifest 无需再改，
但**本条哈希必须记在此处**，因为此前的四方结果是用已丢失的 2026-10-09 版原文构建的
（那些**二进制与结果文件仍然在册**，行为等价由上述验收背书；若要对适配器源码本身做逐字节追溯，
只能回到 2026-10-11 之后）。

**教训（已写进文件头注释）**：整文件重写的脚本必须 (a) 显式确认目标存在、读失败就中止，
(b) 不要假定「同名的兄弟文件」存在——这个仓里四个包共用一份源文件。

## 3. 逐条对照集成轮判据

| 判据 | 结果 |
|---|---|
| OFF 逐字段不变（入口装置六格 vs 入口轮记录） | ✓ 六格全等（`off_unchanged=true`） |
| ON 复现原型：节省 = 理论值且 ≥90% | ✓ 100%（目标格 2,113,536 B） |
| ON：Context16 控制臂节省 0、答案不变 | ✓ |
| ON：四阶段 × 六格无「可分辨 >5% 回归」 | ✓ 24 项全不成立 |
| 开关四处登记 + `switches()` 含该字段 | ✓ 各唯一命中 |
| sanitizer（ASan/UBSan）六格门 | ✓ 全绿、stderr 空 |
| 适配身份同步 + 检查器通过 | ✓ 四 manifest 指向新哈希 |
| 默认值不翻转 | ✓ `default_flipped=false` |

## 4. 未测与限制

- 与原型一致：R1/preset 与固定地址模式未测（前者在该开关打开时**显式抛错**）；
  参考诊断仍缺独立参考对象（OFF 不受影响；ON 时它们显式抛错）。
- 训练照旧 → **没有构建时间收益**；若要省构建时间（不训练），那是另一个候选，需另行预注册。
- `skew/LITS` 的各阶段中位在散布底之下，属未分辨；不构成回归，也不记收益。
- 四方（ART/HOT）同轮复测未做；采用/默认翻转按路线图 §6 的有限泛化验收另行判定。

## 5. 交付

`RESULTS_OWNER_INTEGRATION_20261011.md`（本文）、`results/owner_integration_20261011/{off,gates,timing,raw,decision,identity}.json`、
`commands.log`、两个二进制；装置 `diagnostics/run_owner_integration.py`；
计划 `docs/NEXT_ACTION_PLAN_20261011_OWNER_INTEGRATION.md`；适配器重建：`adapters/our_index/our_index_adapter.cpp`。