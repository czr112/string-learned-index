# GD 入口结果（热组几何描述符）

日期：2026-10-11。承接 [长期路线](../../docs/STRUCTURE_OPTIMIZATION_ROADMAP_20261011.md) M1 与
[入口计划](../../docs/NEXT_ACTION_PLAN_20261011_GEOMETRY.md) §2。生产源码未改动：
`index.hpp` = `640660a32f1fd1476d0d889d541c007883e5597d70e476524e3bcf2556ec8aa1`、
`switches.hpp` = `5486909fffc61c1eef940c058eae3fe036e970f0db21c988b3b41800803f0d4e`。
原始记录：`results/geometry_entry_20261011/`；装置 `diagnostics/geometry_entry.cpp`、
生成器 `diagnostics/make_geometry_prototype.py`、运行器 `diagnostics/run_geometry_entry.py`、
形态检查 `diagnostics/check_geometry_shape.py`、独立重算 `diagnostics/verify_geometry_entry.py`。

---

## 0. 结论

**入口成立（六格全过），它只授权一个原型，不授权任何默认改动。**

三条支持它的事实，全部是可复算的记录：

1. **几何是「构造期一次确定」的。** 全文件对 `sg_` / `bb_` / `off_` 的写入只有 4 处，全部在
   `Region` 构造函数里（3065、3066、3126、3149，行号见 §2）；`storage_` 只赋值一次（3164）。
   此后任何几何变化都走「替换整个 Region」而不是就地改写（5549/5552、5646/5650、6138、6155/6156/6162）。
   compact、repack、reclaim、erase、scan 都不写这三张表。
2. **描述符是真实几何的保序重编码，不是近似。** 六格逐组字段零不符、逐块地址零不符
   （共 33,529 / 31,474 / 34,775 / 34,522 / 33,669 / 30,357 对）；四臂
   B0/B1（原路径，locate 关/开）与 G0/G1（描述符路径）在同一索引状态上，对全部 10,000 条原始查询
   和全部语料键给出相同答案（interchange oracle），且 14 项逐查询计数逐格全零差异。
3. **空间与代码形态都在预算内。** 新增常驻 1.20%–1.51%（门槛 ≤5%）；无计数优化构建里
   baseline `find_impl<true,false>` 有 1 条整数除法、候选 `find_impl<true,true>` 为 0 条，
   指令数 1308 → 1253。

**本轮没有计时。** 没有任何数字可以转换成速度结论，入口也没有声称描述符是主导段。
按计划顺序，现在被授权的是 §3 的隔离原型与 §4 的固定 A/B。

---

## 1. 装置、口径与身份

- 六个计数格：mixed / skew / tranco × Context16 / LITS；`baseline_config(128)` +
  `pin_frozen_switches`，C 开、W/S/H、子块回收、空洞复用关，`compact_plan_commit` 开；
  普通 `bulk_load`（非 R1）、matrix2 原 rows/queries、每格 10,000 条查询、90% 命中。
- 探针不是生产源码：`make_geometry_prototype.py` 从 pinned `index.hpp` 生成一份副本，做**有界**替换
  （`Mode.hot_geom`、24 B `GeomSlot`、`prepare_hot_geom()`、`find_impl<Hash,Hot>` 的八处几何读取
  三元分支、`Region::find` 分派、E25_DIAG 访问器）。每个锚点断言唯一，源漂移会硬失败。
- 两个构建：计数（`-DE12_COUNT -DE25_DIAG`）与形态（`-DGD_SHAPE_PROBE`，无计数）。
  12 次运行全部 `rc=0`、stderr 空；两种构建的四臂 checksum 逐格相同（§3）。
- 身份：计数二进制 `380d52b879ef18f40a0b3ad49cda2de7e96e5b830196b26dd3f77205ab902a90`、
  形态二进制 `77e47d84a6c5642e8c84afddf4dc68e0aee940d90aac90dceb169ced6c6ef694`、
  g++ 13.3.0、`taskset -c 0`；17 个源/输入哈希见 `identity.json`，跑前跑后各复核一次。
- **独立重算**：`verify_geometry_entry.py` 与 runner 不共享代码，从 `work.json` 原始字段重算
  全部判据，并重算 `identity.json` 里每个哈希与两个二进制。结果：`all_pass=true`、
  `problems=[]`、`identity_mismatches=[]`（`results/geometry_entry_20261011/verify.json`）。
  诚实标注：本轮未能取得第三方 agent 复核（子代理通道当日持续不可用），这份重算是自写自跑，
  它独立于 runner 的实现，但不独立于人。

---

## 2. 几何写入入口与生命周期审计（计划 §2 第 1 条）

**写入点（全文件，pinned 行号）**。前四行是三张几何表本身，后三行（局部表、`storage_`、`dirs_`）是同处构造的相邻写入，一并列出以免读者以为遗漏。

| 行 | 代码 | 位置 | 触发 |
|---|---|---|---|
| 3060–3061 | `sg_in.assign(groups_, cfg.slots)` / `bb_in.assign(groups_, cfg.block_bytes)` | 构造（局部表） | 均匀布局 |
| 3065–3066 | `sg_ = std::move(sg_in); bb_ = std::move(bb_in);` | 构造 | 均匀或计划容量 |
| 3126 | `off_.assign(groups_ + 1, 0)` | 构造 | 偏移目录推导 |
| 3149 | `off_[g+1] = off_[g] + nb * bb_[g]` | 构造 | 每组的块数预算 |
| 3164 | `storage_ = ::operator new(off_.back(), 64)` | 构造（唯一一次） | 存储分配 |
| 3175 | `dirs_.assign(groups_, {})` | 构造 | `multiblock` |

字段范围校验就在写入旁边：3129 要求 `slots ∈ [1,48]`、`bb % 64 == 0`、`bb > start_of`、
`bb ≤ 65535`；3147 要求 `nb ≤ 65535`；3156–3157 在固定地址模式下要求每组恰好一个子块。

**几何改变的方式**：不是就地改写，而是替换 Region 对象 ——
`bulk_load` 逐块构造（5549、5552）、`bulk_load_with_context`（5646、5650，计划容量表）、
`rebuild_with_caps`（6138）、`insert` 分裂（6155、6156、6162）。
`compact()` / `repack_subblock()` / `erase()` / `scan()` 的全文对这三张表零写入。

**查询路径的几何消费者（pinned 行号）**

| 行 | 读取 | 用途 |
|---|---|---|
| 3929 | `base_of(g, 0)` | 首块地址（也供 prefetch） |
| 3981 | `subblocks(g)` = `(off_[g+1]-off_[g]) / bb_[g]` | **每查询一次整数除法** |
| 3989 | `header(g, blk)` → `base_of` → `off_[g] + blk*bb_[g]` | 块头 |
| 3998–3999 | `slots_of(g)`（两次） | SIMD 活槽上限 |
| 3954 / 3958 | `entries(g, blk)` / `suffix(g, blk, e)` | 记录与完整键比较 |
| 4029 | `overflow(g)` → `header(g,0).overflow_pointer` | 溢出容器 |
| 1859 | `suffix()` 内 `assert` 读 `start_of(g)` 与 `bb_[g]` | 两种构建都未定义 `NDEBUG`，断言在计数与形态构建里都求值 |

**失效规则**：几何只在构造期确定，区域被替换时新 Region 自然带上新描述符；因此集成版只需在
Region 构造完成后生成一次描述符、并保证「替换」覆盖全部几何改变路径，不需要调用点列表。
**未测**：固定地址模式（`offset_directory=false` 时 `base_of` 忽略 `off_`，第 1827–1835 行）本轮
完全没有覆盖；本轮的六格全部在 `offset_directory=true` 下。原型与集成必须按计划 §2 的约束
「固定地址模式按原 `base_of` 语义生成」，或明确不入学该模式。

---

## 3. 六格记录

规模与空间（`work.json`）：

| 语料 | 后端 | 区域 | 组 | 块 | 多块组 | 最大块/组 | 组/区域 | 描述符常驻 B | 占索引 | 溢出记录 |
|---|---|---:|---:|---:|---:|---:|---|---:|---:|---:|
| mixed | Context16 | 64 | 21,251 | 33,529 | 4,971 | 307 | 171–613 | 511,560 | 1.30% | 2,547 |
| mixed | LITS | 64 | 21,251 | 31,474 | 2,818 | 293 | 171–613 | 511,560 | 1.39% | 3,144 |
| skew | Context16 | 64 | 20,163 | 34,775 | 894 | 112 | 171–493 | 485,448 | 1.20% | 5,033 |
| skew | LITS | 64 | 20,163 | 34,522 | 1,061 | 202 | 171–493 | 485,448 | 1.21% | 4,653 |
| tranco | Context16 | 128 | 21,888 | 33,669 | 5,007 | 43 | 171 | 528,384 | 1.37% | 0 |
| tranco | LITS | 128 | 21,888 | 30,357 | 4,013 | 43 | 171 | 528,384 | 1.51% | 0 |

块数直方图 `nb_hist`（分箱 1 / 2 / 3 / 4 / 5–8 / 9–16 / ≥17）：

| 语料 / 后端 | 1 | 2 | 3 | 4 | 5–8 | 9–16 | ≥17 |
|---|---:|---:|---:|---:|---:|---:|---:|
| mixed / Context16 | 16,280 | 3,393 | 1,070 | 259 | 76 | 45 | 128 |
| mixed / LITS | 18,433 | 2,478 | 81 | 13 | 42 | 46 | 158 |
| skew / Context16 | 19,269 | 130 | 98 | 52 | 80 | 110 | 424 |
| skew / LITS | 19,102 | 202 | 48 | 20 | 236 | 301 | 254 |
| tranco / Context16 | 16,881 | 2,441 | 1,153 | 557 | 669 | 170 | 17 |
| tranco / LITS | 17,875 | 2,499 | 679 | 269 | 424 | 120 | 22 |

差分与「描述符确实被读了」：

| 语料 / 后端 | 四臂 checksum（b0=b1=g0=g1） | counter_diffs 14 项 | 语料键差分 | hot_reads（G 臂） | hot_reads（B 臂） | 描述符路径一致 |
|---|---|---|---:|---:|---:|---|
| mixed / Context16 | 5076600105132327806 | 全 0 | 131,072 键零不符 | 194,012 | 0 | ✓ |
| mixed / LITS | 5076600105132327806 | 全 0 | 131,072 键零不符 | 238,590 | 0 | ✓ |
| skew / Context16 | 5076600105132327806 | 全 0 | 131,072 键零不符 | 260,091 | 0 | ✓ |
| skew / LITS | 5076600105132327806 | 全 0 | 131,072 键零不符 | 361,974 | 0 | ✓ |
| tranco / Context16 | 2527145298674069018 | 全 0 | 262,144 键零不符 | 98,882 | 0 | ✓ |
| tranco / LITS | 2527145298674069018 | 全 0 | 262,144 键零不符 | 94,803 | 0 | ✓ |

`hot_reads` 是探针里 `hot_base()` 的计数（只有描述符路径能走它）：G 臂每次查询都读、B 臂一次不读，
所以「候选路径真的读了描述符」不是断言而是计数。跨构建检查：形态构建（无计数）与计数构建的
四臂 checksum、`checksum`、`row_checksum` 逐格相同。

---

## 4. 代码形态证据（计划 §2 第 4 条）

`check_geometry_shape.py` 只读反汇编，用法与限制都写在脚本 docstring 里；自检先在合成程序上
验证「能数出整数除法、且不会把浮点除法当整数除法」。

| 符号（去 demangle 后缀） | 指令数 | x86 整数除法 |
|---|---:|---:|
| `Region::find_impl<true, false>`（原路径实例） | 1,308 | **1**（`div`） |
| `Region::find_impl<true, true>`（描述符实例） | 1,253 | **0** |

`GD_SHAPE_PROBE` 的两个 `noinline` 包装只保证两个实例各自成为一个符号，它们本身只是尾调用；
形态构建因此没有把函数内联进调用者而让符号消失。**这里只确认代码形态**：没有时间份额换算，
不认定 cache miss。候选实例少 55 条指令这一事实也不当作收益。

---

## 5. 逐条对照入口门槛（计划 §2 末段）

| 门槛 | 结果 | 证据 |
|---|---|---|
| 六格语义差分零不符 | ✓ 6/6 | 四臂答案 + 14 项逐查询计数全零差异；语料键差分零不符；跨构建 checksum 相同 |
| 几何消费者 / 失效规则完整 | ✓ | §2 的写入点、消费者、替换规则清单；描述符覆盖 3929/3981/3989/3954/3958/4029 与 1859 断言 |
| 新增常驻记账各格 ≤5% | ✓ 1.20%–1.51% | 描述符 capacity + Region 内 vector 对象，原三表不扣除 |
| 优化构建确认至少消除一类几何推导 | ✓ | 1 条整数除法 → 0；且未引入每查询建表（`prepare_hot_geom()` 只在装载后调用一次），未引入间接搜索 |
| 编译器已把相关工作全部消除？ | 否 | baseline 实例仍有 1 条 `div` |
| 空间超线？读只语义不成立？ | 否 | 空间 1.2%–1.5%；描述符只读、由 Region 拥有，不改变任何写入路径 |

`entry_all_pass = true`（`summary.json`）。按计划 §2，入口成立**只授权一个原型**，
不声称它是主导段、也不授权默认值翻转。

---

## 6. 未测与限制（必须与结论一起读）

- **没有计时**：本轮的计数构建不能导出速度结论；§4 的 5 进程 × 4 平衡重复 A/B 尚未运行。
- **只有静态、普通 `bulk_load` 状态**：插入 / 删除 / 分裂 / 重建 / 前缀重建后的描述符行为
  只有源码级论证（几何只写于构造、改变即替换区域），没有跑动态门。
- **只有 `offset_directory=true`**：固定地址模式未测（§2 末）。
- **空间记账口径**：`resident = capacity + 对象字节`；对象字节（24 B/区域）同时已计入
  `index_bytes`（`memory_breakdown().self = sizeof(Region)`），所以 resident 与 index_bytes
  对这部分**轻微重复计量**（≤1.5 KB/格），方向是偏保守。
- **未做 sanitizer**、未试描述符宽度（4/12/32 B）、未动模型 / CRC / 映射 / 主块几何 / 默认开关。
- **LITS 两格不是对照臂**：描述符与模型无关，六格都入学；本轮没有「控制臂」概念。
- 长尾很重的组（如 mixed 有 128 个组 ≥17 块、最大 307 块）在入口数据里可见，但本轮**没有**
  据此提出任何新候选——那是长期路线 M2（跨子块连续标签）与更大粒度结构的问题。

---

## 7. 对下一步的授权与交付

按计划 §3 进入**隔离原型**（生产源码不动；B0/B1 与 G0/G1 在同一索引状态切换；
空键 / NUL / 0x80 / 0xff / 4096 字节键 / 冲突 / 单块与多块 / 空满块 / 组与区域边界 / 溢出命中 /
重建后换新描述符 / ASan+UBSan 全绿），随后按 §4 的固定判据做一次 A/B：
主指标 Context16/tranco 的 G/B ≤0.90、至少 4/5 更快、且超过该格散布底；mixed/skew 为风险格，
LITS 三格全部重放。LITS 即使另有收益也不改判主指标。

交付：

- 结果与原始记录：`RESULTS_GEOMETRY_ENTRY_20261011.md`、`results/geometry_entry_20261011/{work,clean,shape,summary,identity,verify}.json`、`commands.log`、`probe/index.hpp`
- 装置：`diagnostics/{geometry_entry.cpp, make_geometry_prototype.py, run_geometry_entry.py, check_geometry_shape.py, verify_geometry_entry.py}`

**未进入的阶段明确标为未测**：原型、动态门、A/B、集成、四方复测全部未做。