# 验收轮进度：阶段 1 完成、阶段 2 通过、阶段 3 遇阻（2026-10-23）

按[可信验收计划](../docs/LONG_TERM_BM_PLAN_20261023.md)执行。**默认仍 Context16，候选仍在诊断副本。**

## 阶段 1：冻结现状 ✅

| 对象 | 位置 | 状态 |
|---|---|---|
| 原 BM | `bm_page_core.hpp` / `bm_page_root.hpp` / `bm_page_index.hpp` | 生产基线（多轮身份） |
| 再平衡候选 | `bm_n1_page_core.hpp` / `bm_n1_page_root.hpp` / `bm_n1_index.hpp` | 诊断副本，无正式 factory |
| 模型 | `model_3894c767` | 已冻结 |
| 数据 | 65,536 三类真实键、100k 词表、N4 16 格轨迹 | 含 SHA |
| 入口 | `make_bm_experiment_index`（基线） | R0 manifest |

## 阶段 2：异常安全 ✅ 通过

### 修复

原分裂顺序是"先 `block.count=8`、再 `blocks_.insert`"——两者之间抛 `bad_alloc` 会丢后 8 个键。
改为**强保证**：

1. 先构造 `tail` 与 `split_fence`（不抛）；
2. `blocks_.reserve(size+1)`、`stable_fences_.reserve(size+1)`——**可能抛，但此时尚未改动任何结构**；
3. 预留成功后，`insert` 只做平凡类型搬移（不抛），最后才 `block.count=8`。

同时删除了一处误加的 `fences_.push_back`——`fences_` 是**每 region 一条**，不是每块一条，
运行期追加会让它与 `digests_` 的对应关系失去意义。

### 失败注入结果

`gate_bm_n3_fault.cpp`：重载 `operator new`，在第 N 次分配时抛 `bad_alloc`，
600 次插入中持续注入，每次失败后检查"全部既有键仍可读 + `verify()` 通过 + 仍可继续插入"。

| throw_at | 注入次数 | 失败后恢复 | **丢键** | 失配 |
|---:|---:|---:|---:|---:|
| 1 | 96 | 96 | **0** | 0 |
| 2 | 32 | 32 | **0** | 0 |
| 3 | 32 | 32 | **0** | 0 |
| 5 | 32 | 32 | **0** | 0 |
| 8 | 32 | 32 | **0** | 0 |
| 13 | 32 | 32 | **0** | 0 |

六个注入点全部：**零丢键、零失配、每次失败后都能继续插入**，终态 66,136 行一致。

> 说明：这证明**当前实现**在分配失败下保持强保证；旧实现的丢键是代码审查发现的，
> 未保留"旧实现 + 该门"的对照运行。

## 阶段 3：统一入口与空间核验 —— **遇阻**

写好了增量空间账本程序（`gate_bm_n3_ledger.cpp`：同一轨迹分别回放基线与候选，
记录 `memory_bytes` 起止、增量、`accounted_peak_bytes`，判据 ≤1.05×），
但**在第一条轨迹上就报 `sorted unique rows required`**，未产出任何数字。

已排除的方向：轨迹行文件本身是**有序且无重复**的（52,428 行、0 个逆序对、0 个重复键）。
因此问题出在回放路径（构造或某次重建）而非数据。**在定位前不能给出空间结论**。

下一步（按顺序）：
1. 用最小复现定位该异常（先只构造不插入，再逐段加操作）；
2. 确认是否由本次强保证改动引入（对照：临时关闭 `reserve` 分支）；
3. 修复后再跑四格空间账本，按 1.05× 门判定。

## 阶段 4–6：未开始

公平性能（无校验计时 + 四臂 AA + 五进程）、合规负载（2:1:1 / 热点 / scan 16·128）、
泛化收口均未开始。

## 证据

- 失败注入：[gate_bm_n3_fault.cpp](diagnostics/gate_bm_n3_fault.cpp)、[tools/n3_fault.sh](../tools/n3_fault.sh)
- 空间账本（未通过）：[gate_bm_n3_ledger.cpp](diagnostics/gate_bm_n3_ledger.cpp)、[tools/n3_ledger.sh](../tools/n3_ledger.sh)
- 强保证实现：[bm_n1_page_core.hpp](diagnostics/bm_n1_page_core.hpp)
