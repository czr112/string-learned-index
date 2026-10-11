# CODE_AUDIT：当前文件、函数、配置、来源与未决问题

日期：2026-09-23。目录：`validation_e25_20260918`。

原任务书 §12.1 把 `CODE_AUDIT.md` 列为推荐文件职责之一（"当前文件、函数、配置、来源与未决问题"），此前**没有交付**。本文件补齐。它是一份**导航**，不是复述——细节在各轮的 `RESULTS_E*.md` 与 `MODEL_AND_MAPPING.md` / `INVARIANTS.md` / `LITS_AUDIT.md` 里。

**身份**：`index.hpp` **4,697 行**，SHA-256 `269b65cf37b3f82c51f0241f…`（2026-09-23 的 G3 改动之后）。`switches.hpp` 150 行。

---

## 1. 目录与文件职责

| 位置 | 内容 | 数量 |
|---|---|---:|
| `index.hpp` | **全部实现**：`Model`／`Region`／`Index`／`Config`／计数器／诊断钩子 | 4,579 行 |
| `switches.hpp` | 四张开关登记表（`baseline_config` / `pin_frozen_switches` / `switches()` / `require_only_switches_differ` 的字段白名单） | 146 行 |
| `data.hpp` | 语料生成（`make_data`）、`Pair`、`Less` | — |
| `e9_alloc.hpp` | 确定性容量规划（`demand_single` / `demand_strict` / `Alloc`） | — |
| `selected.hpp` | `query_mode` 等全局常量 | — |
| `native*.hpp` | ART 适配器（`native.hpp` 与 e20/e22/e23 各版） | 4 |
| `model/lits_hpt_adapter.hpp` | **LITS-HPT 适配器**（E38）：`SharedModel` / `RegionModel` / `walk_position` / `predict_group` | — |
| `vendor/lits_model_original/` | LITS 固定提交的**原样** 8 文件（未打补丁） | 8 |
| `vendor/lits_model_safe/` | **打了安全补丁**的 3 文件（补丁见 `patches/lits_model_safety.patch`） | 3 |
| `third_party/vendor/libart/` | ART 基线（C99） | — |
| `check_*.cpp` | **正确性门** | 22 |
| `bench_*.cpp` | **计时基准**（含 PGO/LTO 的三个同源变体） | 18 |
| `diag_*.cpp` | **只读诊断** | 12 |
| `analyze_*.py` | 分析脚本（＋`analyze_common.py` 共享 loader） | 20 |
| `run_*.py` | 驱动 | 20 |
| `tools_build.py` | 构建/运行闭环（唯一入口） | — |
| `logs/build_*.json` | 构建记录（源哈希、编译命令、exe 哈希） | 71 |
| `results/main/*.jsonl` | 原始记录 | — |

**规则（跨轮反复生效）**：**派生新程序，不改产出过已交付结果的那个源与其构建记录**。本轮的 `check_g1_contract.cpp` 就是派生物。

---

## 2. 核心类型与函数

### 2.1 `Model`（`index.hpp:770`）

表驱动的**参考模型**：把区域**后缀**映射到逻辑组号。

| 成员 | 位置 | 说明 |
|---|---|---|
| `enum class ModelKind` | `:588` | `Context16 / Position16 / Unigram` |
| `MASS = 1U << 16`、`Q = 2^47`、16 个上下文行 | `:767–773` | 量化概率表 |
| `train(const std::vector<Pair>&, prefix, kind)` | `:794` | 按区域后缀训练 |
| `predict(s, groups, route, fp)` | `:870` | 返回 `Prediction` |
| `struct Prediction` | `:855` 起 | 含 `consumed`（模型处理的字节 → `result.steps`）与 **`fp_consumed`**（哈希续接点，E38 新增） |

**`consumed` 与 `fp_consumed` 不可合并**（`index.hpp:890–893` 注释）：合并会把 `fp_consumed` 清零，正是"把融合得到的成本误记为哈希消失"。

### 2.2 `Region`（`index.hpp:1040`）

一个区域 = 若干逻辑组 → 每组若干子块 → 槽位 + 溢出容器。

| 概念 | 位置 | 说明 |
|---|---|---|
| `canonical_group(full_key)` | `:1450` | **唯一的映射汇聚点**；只接完整键，内部自接后缀；LITS 分派在 `:1454–1457` |
| `predict_for(full_key, suffix, route, fp)` | `:1483` | 查询侧融合入口（E38） |
| `compute_prefix` | `:1560` | 首末键最长公共前缀 |
| `first_candidate` / `repack_subblock` | `:1663` / `:1685` | E31 子块内原地回收（与 `put_primary` **共用枚举**，防漂移） |
| `drain_overflow_bounded` | `:1725` | E37 有界迁回 |
| `compact(g)` | `:1870` | 整组重放；**准备（收集）与提交（原地重放）两段分明** |
| `calibrate_lits` | `:1945` | E38：区域校准或整体回退 Context16 |
| `Memory` 分解 | `:2531` 起 | `MemBreakdown{blocks,model,prefix,overflow,tables,directory,self}` |
| `group_stats()` | `:2593` 起 | 逐组 entries/bytes/free/garbage/blocked 等 |
| `Located` / `Where` | `:2837` / `:2839` | 四态：`NotLocated / Absent / Primary / Overflow` |
| `find_impl` | `:2911` | 走 `predict_for`（`:2920`），收尾 `fp.tail`（`:2930`） |
| `worth_compacting` | `:3204` | C2 字节闸门 ＋ C3 预算闸门 |
| `place_with_ctx` / `insert_slow` | `:3255` / `:3329` | 放置失败 → 子块回收 → 整组压缩 → 有界迁回 → 溢出 |
| `scan` | `:3811` | **按组号升序遍历**（扫描正确性依赖映射保序，见 I2） |

### 2.3 `Index`（`index.hpp:3907` 起）

区域目录、路由（`locate`，`:4173`）、公开 API（`insert/update/erase/upsert/scan`）、`bulk_load` / `bulk_load_with_plan`、以及给门用的只读访问器（`region(i)` / `region_count()` / `memory_breakdown()` / `diag_where` / `e24_report` 等）。

**两处训练点**：Region 构造（`:2036`）与需求探针（`:2715`）；LITS 后端下两者**共用同一份 `shared_ptr<const SharedModel>`**（全索引一张表）。

---

## 3. 配置面（`Config`，`index.hpp:600–745`）

| 类别 | 字段 | 默认 |
|---|---|---|
| **发布配置（冻结 2026-09-20，ON）** | `write_fast_locate` / `reuse_locate_ctx` / `compact_reuse_tag` / `compact_needs_garbage` / `scan_reserve_result` | **true** |
| 候选（全部 off） | `compact_repack_budget`（E29 C3）/ `subblock_reclaim`（E31）/ `reclaim_hysteresis`（E31H）/ `reclaim_skip_repeat`（E31P）/ `scan_cache_start`（E27 S2）/ `overflow_stable_store`（O1）/ `overflow_inline_tag`（T8）/ `overflow_migrate`（E37） | false |
| 模型 | `model_kind`（Context16）/ **`model_backend`**（Context16 / LitsHpt，E38） | Context16 |
| 其它 | `block_bytes=1024`、`slots=16`、`offset_directory=true`、`multiblock`（**Config 默认 false**，`baseline_config` 设 true）、`reuse_group_ids`（同上）、`write_simd=true`、`write_locate=false`、`ov_threshold=32` | — |

**两条必须记住的口径**：

1. **`Config{}` 裸默认不是发布配置**——它的 `multiblock=false` / `reuse_group_ids=false` 是 E5–E8 的单块路径。发布配置由 `pin_frozen_switches()` 钉住；`check_freeze` 的 SHIPPED 臂另设 R1 字段。
2. **`offset_directory`、`write_simd`、`write_locate` 三个 bool 既不在白名单、也不在"非开关字段相等"断言里**——两条臂若只差这三者，`require_only_switches_differ` 会静默通过。目前没有实验翻转它们，所以不构成已发生的错误，但它是那个检查要防的情形。

---

## 4. 计数器与诊断钩子

| 机制 | 位置 | 开关 |
|---|---|---|
| `validation::e25::Counts`（`-DE25_DIAG` 才编译，否则 no-op） | `index.hpp:225–400` | `E25_DIAG` |
| E30 计时窗口（`E30_T0` / `E30_ADD`） | `:402` 起 | `E30_TIME` |
| `sar_rotate_groups`（**故意非保序**映射，供 `check_scan_contract` 的负对照） | `:3713` 区 | `E25_DIAG` |
| **`inject_at` / `inject_point` / `maybe_inject`**（G1 失败注入） | 同上区块 | `E25_DIAG` |

**都是诊断专用**：不新增 Config 开关，四张开关表不动，所以发布配置看不见它们；计时构建里连分支都不存在。

---

## 5. 外部来源（固定版本）

| 来源 | 提交/版本 | 用途 | 边界 |
|---|---|---|---|
| **libart** | `301046804af165269e37da6725f5a4aec9ecc881`（`third_party/fetch_libart.py:18`） | 外部对照基线 | 无匹配的范围扫描适配器 → **不进扫描排名**；对照限定无 NUL ASCII 域 |
| **LITS** | `6f4793dff0dcf66daada49e2f4ae1b1838bfa9cc`（`fetch_lits_pinned.py`） | 临时参考**模型**后端 | 只取 HPT 模型与最小依赖；**完整 LITS 索引故意不引入**；字节 ≥128 与嵌入 NUL 一律拒绝 |
| **参考模型 M0** | 本项目自研（表驱动） | 发布配置的后端 | 不是 LITS 官方实现 |

**许可证**：libart 与 LITS 的许可状态仍未决（`LITS_AUDIT.md §11.5` 登记在案）。

---

## 6. 未决问题（截至 2026-09-23）

| # | 问题 | 登记处 |
|---|---|---|
| 1 | **I10 的提交阶段不原子**：提交中途失败会丢记录，且 `size` 失真。原子性只建立在 `repack_budget_fits` 这个必要条件之上 | `INVARIANTS.md` §I10、`RESULTS_G1_CONTRACT.md` §4 |
| 2 | **LITS 后端在 I2/I3/I4/I5/I7 上没有不变量级断言**，只有内容一致间接覆盖 | `INVARIANTS.md` 末节 |
| 3 | **`MODEL_AND_MAPPING.md` 的八问只描述 Context16 链**（已在文首加了补注，未重写） | `MODEL_AND_MAPPING.md` 补注 |
| 4 | **分析脚本的身份校验只接进了 `analyze_lits.py`**；其余 17 个仍是旧 loader | `analyze_common.py` |
| 5 | **长期替换下的空间不趋稳**（到 200% 周期溢出条数接近活跃数），机制指向容量规划与组分布漂移后的失配 | `RESULTS_LONG.md`、`RESULTS_FREEZE.md §6a` |
| 6 | **`garbage_bytes` 是减法派生值**，没有"区间排序 + 间隙合并"的独立账本，也没有声明它是派生值 | `PRIOR_WORK_AUDIT.md` A-4-19 |
| 7 | **`check_tag_store` 没有留存运行日志**（只有 LIVE 构建记录） | `PRIOR_WORK_AUDIT.md` A-3-5 |
| 8 | **PGO/LTO 从未做过**，且 E32 提到的那份补丁脚本在工作区与临时目录里**都已不存在** | `PRIOR_WORK_AUDIT.md` D-lit-14 |
| 9 | **没有 B+ tree 外部基线**（任务书 §十 要求增加） | `PRIOR_WORK_AUDIT.md` D-lit-25 |
| 10 | **扫描游标复用未实现**（E32 的起点缓存与 E27 的结果构造都不等价于它） | `PRIOR_WORK_AUDIT.md` B-4-5-8 |
| 11 | **完整成本检验（§10.3）未做**：没有按声明比例交错的更新/点查/扫描混合序列 | `PRIOR_WORK_AUDIT.md` A-10-13-2 |
| 12 | 并发、持久化、反向扫描、更宽 SIMD 均未做（任务书明确排除或列为后续） | `RESULTS_FREEZE.md §8` |

---

## 7. 怎么跑

```bash
# 构建（逐目标；失败即硬停并删掉旧二进制与旧记录）
python3 tools_build.py <target>

# 运行：run = run_current = 严格入口（源哈希与记录不符即拒绝 rc=2）
python3 tools_build.py run <target> KIND N [CSV] [TAG] [MODE]

# 历史复验（显式声明不代表当前源码）
python3 tools_build.py run_recorded <target> ...

# 一轮完整实验
python3 run_lits.py --run-id TAG
python3 analyze_lits.py --run-id TAG

# G3/A1'：四臂 S0/E31/WIDE/ART 的长期替换
python3 run_widest.py --processes 5 --reps 4 --run-id TAG
python3 analyze_widest.py --run-id TAG

# G4/E1：PGO/LTO 的四构建对照（含训练、四构建、profile 验证与评估 sweep）
python3 run_pgo.py --processes 5 --reps 4 --run-id TAG
python3 analyze_pgo.py --run-id TAG
```

**工具链**：WSL2 Ubuntu 24.04 + g++ 13.3.0，`-O3 -march=x86-64-v2 -static`，单核绑定；sanitizer 目标改用 `-O1` 且去掉 `-static`（`tools_build.py` 的 `CXX_SAN`）。

### 7.1 `tools_build.py` 的两处非显然机制

**`archive_stale_record()`（G1 新增）**：写 `logs/build_<target>.json` 之前，把**将被替换**的那条按 `build_id` 归档到 `logs/archive/`。因为那个路径是固定的、写入用 `"w"`——重建同一个目标会**就地覆盖**上一条身份（`build_id`、exe 哈希、源哈希）。本项目已经真的丢过一次（`RESULTS_FREEZE.md §7` 引用的 `check_e8_hetero` 身份）。幂等、不删任何东西。

**`STABLE_TMP`（G4/E1 新增）**：三个 PGO 相关目标（`bench_pgo_gen`／`bench_pgo`／`bench_pgo_lto`）编译到**同一个固定对象路径**，而不是那个唯一的临时名。

- **为什么**：g++ 从**对象输出路径**派生 `.gcda` 的文件名。编译到 `<target>.<pid>.<ns>.tmp.exe` 时，消费方会去找一个**没有任何先前构建可能产生过**的名字——于是 PGO 静默地不生效，**二进制照常可用**。
- **发现它的方式**：`-Wmissing-profile` 的警告。**"链接成功"不是 PGO 生效的证据**，这条是真的会骗人的。
- **原子性未变**：二进制仍然只在成功时出现（同一个 `os.replace`）。变的是**同一目标的两次并发构建会互相覆盖**——本项目构建是串行的，PGO 那一对由同一个脚本驱动。
- **`bench_lto` 不在这张表里**：它没有 profile 要找，保留唯一临时名。
