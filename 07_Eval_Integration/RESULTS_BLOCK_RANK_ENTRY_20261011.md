# BlockLinearRank：模型级接入与入口诊断（P0–P2）

日期：2026-10-11。计划：`docs/NEXT_ACTION_PLAN_20261011_BLOCK_MODEL.md`（§3 P0–P2，§6 停止规则）。
模型来源：队友仓库 `Zephyr761/Learned-Index-Model` @ `60837a88`（其 `src/key_bytes.h` 与
`src/block_model.{h,cpp}`）。本轮**不修改**队友源码，也不为其做参数搜索。

## 0. 判定

**接入成功、机制正确、门全绿；但第一版映射是一次可测量的入口回归，按 §6 停止规则不进入 P3 A/B。**
默认配置不变（`Config::model_backend` 仍是 `Context16`），新后端以默认关闭的候选身份收口。

判定依据：在**入口自己的查询模式**下（`Route::Interval`、fingerprint、SIMD、Hybrid overflow、
`locate=false`，与 `adapters/our_index` 的 `query_mode()` 一致），rank 后端每个查询访问的主块数
在三个语料上普遍升高：按区域中位比值 2.84 / 3.79 / 2.05，方向一致性 62/64、64/64、64/64。
这不是个别离群区域，也不是计时噪声——它是**布局**的差异，任何重复次数都只会把它测得更准，不会把它测没。

## 1. 身份

| 文件 | sha256 |
| --- | --- |
| `validation_e25_20260918/index.hpp` | `f553630d5cc31296a5c654c67127e7123c04056a3c214b2e91200f5ae05fa78e` |
| `validation_e25_20260918/switches.hpp` | `f09dc3f6dbff1031d8aa8667ceb45822b784f419db7de9dbc026a4bdda62ecf1` |
| `validation_e25_20260918/model/block_linear_rank.hpp`（新） | `17d8714d175da10fb349bf1fe8dc00f35510cafaf142ba1d9a595c1e0e2aa5f8` |
| `validation_e25_20260918/check_block_rank.cpp`（新门） | `b5cf16fc77c74348945d7a2ff21ae772f4bed574cfe010a976a3ebf545831fdd` |
| 队友模型（只读） | commit `60837a88` |
| 四个适配包 manifest + `diagnostics/run_owner_lifecycle.py` | 已同步到上表的 index/switches 哈希 |

## 2. 实现了什么（P0/P1）

1. **`model/block_linear_rank.hpp`**：9-bit 保序编码（EOS=0，字节 b→b+1，最多 7 个符号）＋ 分段线性
   拟合，`block_size = 1`（y 轴＝区域内的排名），固定 7 字节 / 8 段，无参数搜索；组映射
   `g = min(G-1, floor(p*G/N))`。与前缀 LENGTH（不是 `string_view`）：不持有任何指向训练键的视图。
2. **`index.hpp` 接入**：`ModelBackend::BlockLinearRank`；`Region::block_rank_` / `use_block_rank_`；
   `BlockRankStats`；`calibrate_block_rank()`（组合拒绝：`ModelKind != Context16`、`canonical != Full`）；
   `canonical_group` / `predict_for` / `demand_profile_impl` 三处分发读同一标志；`mapping_identity()`
   把拟合模型的身份混进 R1/R2 preset 守卫；`memory_breakdown()` 计入段表字节。
3. **`switches.hpp` 缺陷修复（不是新候选，是修复）**：`model_backend` 是**三值**枚举，而
   ①打印把它写成非 `Context16` 即 `LitsHpt`，②`require_only_switches_differ` 用「是否 == LitsHpt」
   表示差异。后者会让「Context16 vs BlockLinearRank」被判为**没有差异**——正是这张表存在的目的
   （防止臂静默变成另一臂的副本）被绕过。现在表结构改为「差异表」并直接比较枚举；打印走
   三值 `backend_name()`。

## 3. 正确性证据（P2）

**交叉验证（与队友实现逐位对照，`scratch_blockaudit/cmp_encode.cpp`、`cmp_fit.cpp`）**：

- `encode()`：12 个向量（空键、`a`、`ab`、单字节 NUL、`NUL+a`、`0x80`、`0xFF`、`0x80+a`、
  8 字节键的截断、4095 字节键、前缀偏移 2）**全部与上游逐位相同**；
- `fit(block_size=1, bytes=7, segments=8)`：6 组输入（顺序键、64 字节公共前缀、真正的坐标碰撞族、
  二进制键、单键、空集）的分段表 `(x_min,x_max,y_min,y_max,slope,intercept)` **逐字段相同**
  （double 精确相等）；唯一差异是 **n=1**：上游拟合 1 段，我们返回 `TooFewKeys` 且答案固定为组 0
  （有意不同，已在头文件与门里写明）。

**门 `check_block_rank`（三语料 131072 键，双后端同一状态）**：`checks = 324 / 315 / 325`，
`failures = 0`。覆盖：硬编码 encode 向量、fit 向量（含「相等坐标不切分」的碰撞族）、
逐区域单调性（放置组按无符号字节序非降）、**旋转负对照**（同一检查器必须能抓到人为错序）、
空键/NUL/高位字节/4096 字节键/`region_keys=8` 的多区域域套件、`Route::Interval` 与
`ModelKind::Unigram` 的拒绝、以及 2000 插入 / 1000 擦除后的重查与重单调检查。

**sanitizer（当前门源码重跑）**：`san_check_block_rank` 在 mixed 8192 / skew 2048 / tranco 4096
三格全绿（`checks = 204 / 199 / 201`，`failures = 0`），ASan/UBSan 无诊断。

**独立审查**：`REVIEW_BLOCK_MODEL_CONTRACT_20261011.md`（未参与实现的代理，冻结副本 + 自写参考
公式 + 负对照）225 项检查 0 失败；结论「模型契约成立，未发现阻断性缺陷」，并提出下面的 §5 机制。

## 4. 入口诊断（P2 的读数）

同一状态、同一查询队列、两后端同一组数推导（组数只由 n 与后缀字节决定，与模型无关）：

| 语料 | 区数 | 主块访问 参考 → rank | 区域中位比值 | P90 | 最大 | 变差区域 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| mixed | 64 | 1,467,679 → 2,226,426 | 2.84 | 5.46 | 7.86 | 62/64 |
| skew | 64 | 2,173,636 → 8,753,277 | 3.79 | 5.54 | 6.58 | 64/64 |
| tranco | 64 | 327,879 → 654,006 | 2.05 | 2.61 | 4.12 | 64/64 |

| 语料 | 完整键比较 参考 → rank | 进入溢出 参考 → rank | 组负载 最大 / 平均 |
| --- | ---: | ---: | ---: |
| mixed | 170,119 → 193,474 | 2,547 → 4,828 | 1366 / 6.17 |
| skew | 205,280 → 438,279 | 5,033 → 6,577 | 1431 / 6.50 |
| tranco | 132,475 → 134,157 | 0 → 0 | 564 / 11.98 |

读数边界：这些是**计数**，不是时间；本轮没有 A/B，不能把计数差折算成加速比。但它们与
`matrix4s`（同样的入口、同样的查询模式）里我们两臂的读数方向一致，且在两后端**逐区域配对**，
所以它足以支持「不要用这一版去跑矩阵」。

## 5. 机制：两个反例族（同一审查的独立装置）

1. **坐标饱和**：区域前缀之后的**第 8 个字节不进模型**。因此「共享前 7 个后缀字节」的键全部
   塌进同一组。纯反例 C3：`pfx/` + 16×`a` + 编号，2000 个不同键 → **1 个坐标 → 1 个组**，
   11/12 组空置。skew 2048 的 sanitizer 单元把这一点放大到极端：493 组里最大组 1887/2048 条记录，
   主块访问比 40 倍。
2. **分箱不均匀**：坐标各不相同也会偏。C6（5002 键 / 64 组）：32 组空置、最大组 665（平均 78 的
   8.5 倍）。原因是 `clamp(intercept + slope·dx)` 的线性预测**对段内 x 的分布不敏感**：x 聚在段内
   一侧时，y 也聚在该侧的少数分箱里。

这两条都是**拟合映射本身**的性质，不是接入错误，也不是语料噪声。

## 6. 结论与处置

- 保持默认关闭；**不开 P3 A/B**（计划 §6「映射造成明显集中 → 先报告瓶颈，不直接启动八臂大矩阵」）。
- 给模型负责人的反馈（§7.3 的交付物）：①坐标饱和反例（C3）与量化，②分箱不均匀反例（C6），
  ③在他们自己的 `block_size/segments/max_error` 证书下，这两族分别对应「编码窗口不足」与
  「段内分布未建模」，而不是参数没调好。
- 本轮**没有**做的事：不改他们的学习算法、不追加参数搜索、不搬 residual/delta、不跑 A/B、
  不改默认配置。
- 下一步的三个方向（任选其一，一轮只做一件）：
  1. **结构侧候选（本轮证据直接指向它）**：把「均匀按排名分箱」换成**等深阈值目录**——用区域内
     键的 63-bit 坐标直接构造 G 个阈值（每组的 x 上界），查询做二分。它按构造既保序又均衡，
     且是模型无关的（≈2 KB/区域），不需要队友改模型。
  2. **模型侧**：把 §5 的两族反例交给队友，等他们的编码窗口/段内分布修正后再接一次。
  3. **顺延项**：回到 `M4`（模型无关的有序段归并），按计划在新后端可用后以它做第二后端验证。

## 7. 复现入口

```bash
cd 02_Code_and_Experiments/validation_e25_20260918
python3 tools_build.py check_block_rank          # 12 向量 + fit 向量 + 域套件 + 负对照
python3 tools_build.py run check_block_rank mixed 131072 ''
python3 tools_build.py run check_block_rank skew  131072 ''
python3 tools_build.py run check_block_rank tranco 131072 ../validation_e5_20260913/data/tranco_XN6XN.csv
bash run_blockrank_gate.sh                        # 三语料 + 三个 sanitizer 单元（拒绝覆盖已有日志）
```

失败留档：`logs/blockrank_gate_20261011a_failed_test_bug.log` 是门自身的两处测试错误（碰撞族把
非碰撞邻居算进断言、擦除检查把「已消失」记成失败）——保留而不是删除，修的是门，不是索引。

## 8. 并发编辑事故与恢复（记录，不改结论）

本轮与一个并行的独立审查代理同时进行，它在中途**改写了本轮的三个文件**（`index.hpp` 的
`#ifdef E25_DIAG` 拟合计时、`check_block_rank.cpp` 的整体重写、`tools_build.py` 的目标登记），
随后被中断。处置与理由：

- **`index.hpp`（已回退）**：它加了一个 `fit_ns` 计数器与两处 `steady_clock` 取样（`#ifdef E25_DIAG`，
  发货构建编译不到）。该路径**从未被执行过**，因此身份里不留未经验证的代码；已删除，index.hpp 由
  `46fdf520…` 变为 `f553630d5cc31296a5c654c67127e7123c04056a3c214b2e91200f5ae05fa78e`，四个适配包 manifest 与 `diagnostics/run_owner_lifecycle.py` 同步重钉。
- **`check_block_rank.cpp`（已重建）**：改写版是「按契约独立重实现 + 逐键对照 + 扫描检查 + 旋转负对照」
  的草稿，方向正确但**不完整**——它引用 `Region::diag_group`（树中不存在）与一份尚不存在的契约文档，
  且出现在本轮证据登记之后。已保留为 `scratch_blockaudit/agent_draft_check_block_model_backend.cpp`
  （41,899 B），本轮改用从补丁原文精确重建的门（`b5cf16fc77c74348945d7a2ff21ae772f4bed574cfe010a976a3ebf545831fdd`）。
- **恢复后逐格重跑**：三语料 `checks = 324 / 315 / 325`，三个 sanitizer 单元 `204 / 199 / 201`，
  `failures = 0`，且 checksum、probes、比值与恢复前**逐位相同**——重建是语义等价的。

这件事本身的教训与项目既有的 S0/G0 记录同类：**并发的写入者必须被点名，否则身份声明会先于事实**；
本轮的最后一次构建记录（`build/check_block_rank.built.json`）与本表哈希一致，可作为绑定证据。
