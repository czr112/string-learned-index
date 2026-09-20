# 字符串学习型索引：三轮验证代码结构讲解

整理日期：2026-09-13。适用范围：资料包 `02_Code_and_Experiments/` 下的三轮验证代码（`validation_20260906`、`validation_e3_20260906`、`validation_e4_20260911`），共 178 个文件，均通过 `MANIFEST.sha256` 逐文件校验。本文解释每部分代码"是什么、为什么这样写、维持了什么不变量"，供后续修改代码（如可变槽数改造）和交接给模型/并发负责人时使用。

结果数字与结论边界见《StringLearnedIndex_Literature_Review_and_Task2_Plan.md》第 15–17 节与各轮 `FINDINGS.md`，本文不重复。

---

## 1. 总体骨架

```
Index（外层路由：有序分隔键数组，二分定位区域）
 └─ Region（一段连续字典序区间 = 重建/选型的单位）
     ├─ prefix_    本区域所有键的共享前缀，只存一份
     ├─ model_     后缀模型：把后缀映射到 [0, 2^47) 的整数区间
     ├─ storage_   groups_ 个连续内存块（64B 对齐），每块一个"组"
     │    每块布局： [Header 64B][Entry×16 = 256B][空闲][后缀字节区 ←bottom][块尾]
     │                             ▲条目数组        ▲从块尾向下生长
     └─ overflow_owners_  每组一个有序 vector，放不进主块的记录
```

一次查找的完整路径：

1. 外层 `Index::locate` 按字典序二分定位区域；
2. `Region::find` 验证查询键确实以本区域共享前缀开头（前缀字节由此跳过，不再读取）；
3. `Model::predict` 逐字节推进后缀模型，输出位置区间 `[lo, hi)`；
4. **区间两端落入同一块号时提前停止**（保证与完整模型同组号）；
5. 对块基地址发预取提示，同时补算剩余指纹；
6. 用 1 字节指纹 tag 筛选本组条目，tag 命中再做完整键比较；
7. 主块没有则查本组溢出容器。

课题两大创新在代码中的落点：

| 创新 | 代码位置 |
|---|---|
| ①共享前缀压缩＋差异化后缀学习 | `Region::prefix_`＋`Model`（train/predict）；`ModelKind` 三类参考模型；最终替换为项目模型 |
| ②预测驱动物理布局＋预取 | `canonical_group()`（构建/插入/查找共用同一分组函数）＋块式连续存储＋`find_impl` 中的 `__builtin_prefetch` 与 `Route::Interval` 提前停止 |

---

## 2. 第一轮 `validation_20260906`：机制原型

### 2.1 比较序与指纹（index.hpp:21–50）

- `compare()`：`memcmp` 实现的**无符号字节字典序**，长度不同时短的在前。全项目所有排序（模型分组、块内条目、外层路由、参照 `std::map` 的 `Less`）共用这一比较器——这是"模型分组顺序＝真实键序"的前提。
- `Fingerprint`：CRC-32C 指纹。`__SSE4_2__` 时用硬件指令 `_mm_crc32_u8/_mm_crc32_u64`，否则退回软件逐位循环（多项式 `0x82f63b78` 即 CRC-32C 反转形式，结果一致）。`byte()` 逐字节喂、`tail()` 批量喂（8 字节步进走 64 位指令）、`tag()` 取状态最低 8 位。
- **为什么需要指纹**：命中某组后最多 16 个条目，逐条完整比较字符串太贵；先比 1 字节 tag 可跳过绝大多数非候选。**tag 相同不证明键相同**，tag 命中后仍精确比较——指纹只做筛选，不参与正确性。
- `check.cpp` 专门验证"分两段喂（byte→tail）与一次喂完（of）结果一致"（`check.cpp:22–24`），因为查找时模型吃前几个字节、指纹补算剩余部分，两段拼接必须无缝。

### 2.2 查询模式开关（index.hpp:52–56）

```cpp
enum class Route { Full, Interval, Residual };  // 模型算多深
enum class Prefetch { Off, Early, Late };       // 预取时机
struct Mode { Route route; Prefetch prefetch; bool fingerprint; };
```

- `Full`：完整算完模型（区间宽度为 0 时也允许停，避免做无效功）；
- `Interval`：**区间两端算出同一块号就停**——本课题核心候选机制；
- `Residual`：参照方法，"剩余区间宽度×组数 < Q 就停"。它借鉴已有提前停止思想但分组函数不同，因此 bench 中 `residual` 变体使用**单独构建**的第二份索引（`bench.cpp:44`）；
- `Early`＝组号确定后立即预取、再补算指纹；`Late`＝先算完指纹再预取（对照组）；`fingerprint=false`＝不走 tag 筛选直接精确比较（`interval_direct` 对照，防止把"指纹收益"误记成"预取收益"）。
- `Result` 除 found/value 外带两个诊断计数：`steps`（模型吃了几字节后缀）、`overflow_checks`（查了几条溢出记录）。这两列进 CSV，是"模型字节下降 ≠ 提速"结论的数据来源。

### 2.3 整数区间模型 Model（index.hpp:59–114）

这是报告 14.5 节"可验证的整数实现"的落地，全项目最数学的一段。

**三个常数**（index.hpp:61–63）：

| 常数 | 值 | 含义 |
|---|---|---|
| `Q` | 2^47 | 位置尺度：模型输出区间 `[lo, hi) ⊆ [0, Q)`，近似后缀在区域内按字典序的累计位置×Q |
| `MASS` | 2^16 | 概率量化精度：每行累计概率表值域 `[0, 2^16]` |
| `ROWS` | 16 | 上下文行数：`row(p,previous)=((p&3)<<2)\|(previous&3)`，即"后缀第几字节(mod 4)＋前一字节(mod 4)"组合出 16 种上下文 |

W ≤ 2^47、C ≤ 2^16，单步乘积 ≤ 2^63，`uint64` 恰好装下——选 47/16 两个位数的理由。**用整数不用 double 的原因**：浮点舍入可能把恰好跨界的区间误判成同组；整数版本每步区间严格嵌套，正确性可逐条检查。

**`train()`**（index.hpp:67–97）：
- 对区域内每个键，取去掉共享前缀的后缀，只采集**前 16 个后缀字节**计数；符号编码 `d = 字节+1`，**0 保留给结束符 EOS**——嵌入零字节（`\0`）与字符串结束因此可区分；
- `+1` 平滑保证任何符号概率非零（包括 EOS 和 0x00）；
- 量化：`frequency[d] = counts×MASS/total`，余数按确定性方式补给前面的符号，保证每行累计恰好等于 2^16（`assert(cumulative[r][257]==MASS)`）；
- 键长 ≤16 时在 `row(len)` 行给 EOS 记一笔——模型由此学习"字符串大概多长结束"；
- 表宽 258：`cumulative[r][0]=0`，`cumulative[r][d+1]=cumulative[r][d]+frequency[d]`。

**`step()`**（index.hpp:89–93）——区间递推，全部整数运算：

```
lo' = lo + floor((hi-lo) × C[d]   / 2^16)
hi' = lo + floor((hi-lo) × C[d+1] / 2^16)     // 两端点都用更新前的 lo，避免顺序错误
```

**`bucket()`**（index.hpp:94–97）——物理映射 h：`g = floor(x×G / 2^47)`，用 `unsigned __int128` 做中间乘法防溢出，再夹取到 `[0, G-1]`。**它是非递减函数**，这是后面所有正确性的根。

**`predict<Hash>()`**（index.hpp:100–123）：
- 逐字节推进：①更新区间；②`if constexpr(Hash)` 把该字节喂进指纹；③检查停止条件：区间宽度为 0，或（Interval）`bucket(lo)==bucket(hi)`，或（Residual）`(hi-lo)×G < Q`；
- **模板参数 Hash 的意义**：构建/插入不需要指纹，同一函数 `<false>` 复用；查找时 `<true>` 边预测边喂指纹，省一遍字符串扫描；
- 返回 `{group = bucket(lo), consumed = 已处理字节数}`；调用方用 `fp.tail(s.substr(consumed))` 补算剩余指纹——总指纹覆盖完整后缀；
- EOS 从不喂指纹；区间走到底时最终输出取 `lo`（EOS 累计概率为 0，见 index.hpp:111 注释）。

> **提前停止为何安全**：区间每步嵌套（新区间 ⊆ 旧区间）＋ bucket 单调 ⇒ 一旦 `bucket(lo)==bucket(hi)`，无论后续再算多少字节，最终落点必在同组。由此**构建时的分组与查找时的分组永远一致**（报告 14.3 不变量）。`check.cpp` 用穷举 1365 个短串＋周期抽查验证 full 与 interval 组号相等。

### 2.4 块存储布局（index.hpp:116–134）

```cpp
struct Entry { uint32_t offset, length; uint64_t value; };  // 16B：块内偏移、长度、值
struct alignas(64) Header {                                  // 64B：每块头部
    uint64_t overflow_pointer;     // 溢出 vector 的堆指针（0 = 无溢出）
    uint16_t count, bottom, ...;   // count=条目数；bottom=后缀区起点（自块尾向下生长）
    uint8_t tags[16];              // 与 16 个条目一一对应的指纹标签
};
static_assert(sizeof(Entry)==16 && sizeof(Header)==64);
```

- `START = 64 + 16×16 = 320`：条目区结束位置；后缀字节存放在 `[bottom, B)`，从块尾向下生长；空闲字节 = `bottom − START`；垃圾字节 = `B − bottom − Σlength`；
- **一个块 = 一个组**：块 g 地址 = `storage_ + g×block_bytes`，"预测值→物理地址"只需一次乘加，预取可直接对块首地址发提示；
- 布局隐含约束：`block_bytes ≤ 65535`（bottom 是 uint16）、64 的倍数（对齐）、`B > START`（构造函数检查，index.hpp:174）。

### 2.5 Region 写入路径（index.hpp:137–199）

- **构造函数**（index.hpp:173）：①取排序后首尾键的最长公共前缀（排序区间首尾的公共前缀即全区的公共前缀）→ `prefix_`；②在去前缀后的后缀上训练模型；③**按平均容量估组数**：`max(键数/(16×0.75), 后缀总字节/((B−320)×0.75)) + 1`——只查平均、不看逐组分布，这是后来各组挤爆、溢出率高的根源（E3 因此加逐组统计）；④分配 `groups_×B` 的 64B 对齐内存并初始化；⑤逐键 `canonical_group()` 定组，先试主块、失败进溢出。
- **`put_primary()`**（index.hpp:150）：按后缀字典序找插入位 p，条目数组与 tags 整体后移一格，`bottom -= 长度`，后缀字节 memcpy 到 bottom 处。两个失败条件：`count ≥ 16`（**槽满**）或 `长度 > bottom−START`（**字节满**）——E3 容量分类围绕这两个条件展开。
- **`put_overflow()`**（index.hpp:160）：懒分配该组私有的 `vector<OverflowRecord>`，`lower_bound` 按**后缀有序**插入。有序是为了 scan 能把主块与溢出归并出正确键序；溢出按组私有而非塞进邻组，是为了维持"属于 g 的键只存于 g 的主块或 g 的溢出"（报告 14.3 不变量 2）。
- **`compact()`**（index.hpp:166）：erase 留下的后缀区字节空洞由 compact 消除（活条目重放一遍）；重放后反而放不下会抛异常（逻辑上不应发生，防御检查）。

### 2.6 Region 查找路径（index.hpp:215–240）——创新②主战场

`find_impl<Hash>` 的分步：

1. 前缀不匹配直接返回未命中（前缀字节根本不看——前缀压缩省的就是这些字节）；
2. `predict` 算组号（Route 决定多早停；Hash 模式下顺带喂指纹）；
3. `Early` 预取：组号一确定即对块地址发 `__builtin_prefetch(address,0,3)`，随后**补算剩余指纹**——重叠的就是这两件事；`Late` 则在指纹之后才预取；
4. 遍历本组条目：tag 不同跳过 → 长度相等再 memcmp 后缀 → 命中返回；
5. 主块没有则走溢出 vector（计 `overflow_checks`）。

另有守卫（index.hpp:218）：以 `Residual` 构建的索引只接受 `Residual` 查询，否则抛异常——两种分组函数不兼容，不允许混查。**预取只是提示，正确性完全不依赖它**。

### 2.7 Region 更新路径（index.hpp:241–287）

- `insert`：先 find 去重 → 定组 → 放主块；失败但"有空槽且压缩后放得下"（`count<16 && 长度≤B−START`）就先 compact 再试；仍失败进溢出。**不做任何邻组搬迁**；
- `update/erase`：组内线性扫描（update 不走指纹过滤，正确性优先）；erase 前移删除条目、留下字节空洞；
- `scan`（index.hpp:267）：从任意 `start`（可以是不存在的键，lower_bound 语义）开始：起始组 `first = canonical_group(start后缀)`，组内用比较跳过 `< start` 的条目，然后从 first 组往后**每组归并"有序条目＋有序溢出"**。正确性前提正是 bucket 单调 ⇒ 组号沿键序非递减 ⇒ 组序＝键序。空串 start、跨组、组序边界均被此逻辑覆盖；`all()` 直接复用 scan 实现重建时收集。

### 2.8 Index 外层路由（index.hpp:290–349）

- `nodes_`：`{fence, Region}` 数组，fence＝区域第一个键；`locate()` 用 `upper_bound` 找"fence ≤ key 的最后一个"；
- `bulk_load`：排序键按 `region_keys`（默认 2048）切片建 Region；
- `insert` 遇**前缀冲突**（新键不共享区域前缀）：收齐全部键、有序插入新键、整体 rebuild——重建后前缀自动变短；区域键数 > `2×region_keys` 也触发 rebuild，rebuild 内部**对半分裂**并插入新 fence（index.hpp:319–327）；
- `rebuilds/splits` 计数器进实验记录（12000 次动态操作中的 13 次重建即来自这里）。

### 2.9 bench.cpp：计时方法学（后续每轮沿用）

- **数据**（bench.cpp:14–33）：`short`＝"user/"+32 hex（37B）；`long`＝URL 形式前缀＋随机后缀垫到 156B；`skew`＝不同长度的重复 'a' 段（制造重复前缀偏斜）；`tranco`＝固定快照 CSV 真实域名（XN6XN，2026-09-05）。统一排序去重后 bulk_load；
- **查询**（bench.cpp:46–52）：90% 命中（引用生成键——**别名问题**：有序数组基线存的就是这些键，查询字节与被查记录同内存，可能偏向基线；故补 `query_copy_probe` 用独立副本重测）；10% 未命中＝键尾加 `\0`；
- **变体表**（bench.cpp:53–63）：`full / full_pf / interval / interval_early_pf / interval_late_pf / interval_direct / residual / residual_pf` ＋ `sorted_array`；
- **计时纪律**：每轮**随机打乱方法顺序**（防系统性偏向）→ 每方法先**预热 ≤20000 条并逐条核对答案** → 计时整批并累加校验和 → **校验和必须等于期望值**否则抛错。校验和一箭双雕：防编译器删除无副作用查询＋保证计时期间答案全对；
- 另采 4096 条样本统计 `mean_model_bytes / mean_overflow_checks`（非计时段）；p50/p99 是暖缓存逐条采样，明确不当总延迟结论（bench.cpp:80–82）。

### 2.10 check.cpp：差分验证验什么

- 键池（check.cpp:12–20）：空串、`a/aa/aaa` 前缀链、嵌入 `\0`、`0xFF`、4096 字节长键、2500 个随机字节串（⅓共享长前缀）；
- 5 个 trial 覆盖不同块宽/路由/`fixed_groups=1`（单组强制大量溢出）配置，15000 步随机 7 类操作**每步**与 `std::map` 对账（返回值、值、size）；
- 每 503 步：全量扫描＋30 个随机键验证 **full 与 interval 组号一致**（check.cpp:46–54）；
- 每 997 步：`rebuild_all()` 后再全量扫描（重建不丢不改）；
- 穷举检查（check.cpp:60–65）：字母表 `{0x00,0x61,0x62,0xFF}` 上长度 0–5 的全部 **1365 个**串——单区域逐键验证"组号一致＋沿键序非递减"，把报告 14.10 的 Python 数学检查在真实 C++ 中重做一遍。

### 2.11 updates.cpp：动态维护观测

2048 个初始键、`region_keys=256`、12000 步固定节拍：30% 聚集插入（同前缀 `/new/序号`，制造热点）、10% 删、10% 改、10% 扫 10 条、40% 查。每步逐操作计时并与 `std::map` 核对，输出 `step,operation,ns,rebuilds,splits,size`。第一轮"插入中位数 278ns、P99.9 132μs、触发重建的操作约 130μs"出自这里；注意这是**含时钟开销的整次操作时间**，不是剥离出的纯重建耗时。

### 2.12 两个定向 probe（独立副本，保留原始证据不被覆盖）

- `overflow_probe/`：溢出容器 >32 条时改用精确二分查找的独立版本（`overflow_probe/index.hpp`），同协议重跑 63000 次正确性；结论：中位数变化 +1.3%/−0.07%，无可靠改善；
- `query_copy_probe/`：全部查询使用独立字符串副本的四方法对照，消除别名对有序数组基线的影响；域名上原型快于数组 17.4% 但估计占用 1.79 倍。

---

## 3. 第二轮 E3：模型×块宽联合选择

### 3.1 三类模型与动态表（index.hpp:56–73）

`ModelKind {Context16, Position16, Unigram}`：`row()` 分别为 `((p&3)<<2)|(prev&3)`、`min(p,15)`（只看字节位置）、恒 0（单行 unigram）。概率表从 `std::array` 改为 `vector` 动态分配并新增 `allocated_bytes()`——unigram 只有 1 行，**模型大小差异必须真实计入内存**供选择器权衡。

### 3.2 GroupStats：溢出原因分类（index.hpp:136–141, 232–251）

逐组统计主块条目/字节、空闲、垃圾，并把每条**溢出记录分类**：

| 分类 | 判定 |
|---|---|
| 仅槽不足 | `free_entries==0` 且后缀长度 ≤ 剩余字节 |
| 仅字节不足 | 有空槽但后缀长度 > 剩余字节 |
| 两者不足 | 同时满足 |
| 当前能放入 | 两个条件都不满足（布局现状问题） |
| 超长 | 后缀长度 > B−320，任何该型主块都放不下 |

**这一步纠正了"溢出率高"的笼统解释**：Tranco 域名 574,477 条溢出全部是槽不足、0 条字节不足；skew 长键 87.65% 是字节不足。两种病要用不同的药——这正是下一步可变槽数改造的直接证据。

### 3.3 SelectedLeaf / SelectedIndex：异质区域与有序叶回退（selected.hpp）

- **候选编码**（selected.hpp:7–14）：`id 0–8` ＝ 模型(id/3) × 块宽(512<<(id%3))；`id 9`＝`SORTED_LEAF` 有序叶回退；`REFERENCE=1` 即默认 context16＋1024；
- **`SelectedLeaf`**：把"模型叶"或"有序叶"包成同一接口（find/insert/update/erase/scan/all/memory）。有序叶同样提取区域共享前缀、只存后缀——"全部有序叶"基线**也享受前缀压缩**，防止把前缀的功劳错记给学习模型；
- **`SelectedIndex`**：`bulk_load(data, choices, r)` 接受**每区域一个候选 id**——同一索引里 A 区域可用 unigram＋2048、B 区域可用有序叶；`reconfigure(i,c)` 整体替换某区域模型和布局（替换＝重建该 Region，同步完成）；插入遇前缀冲突重建时**保持原候选**（selected.hpp:137–146）。

### 3.4 select()：离散拉格朗日预算选择器（selected.hpp:172–193）

在总容量预算下给每区域挑候选：对偶变量 λ 二分 60 次；给定 λ，每区域独立选 `w_i·tune_ns + λ·bytes` 最小者（同代价偏好更小字节）；总字节超预算则加大 λ。**明确不是最优背包求解器**：返回过程中最好的可行方案，且保证不差于传入的初始可行解（`check.cpp:63–74` 用 100 个随机小问题对照枚举最优，最大差距 12.9 个合成单位，如实记录为"近似"）。

### 3.5 data.hpp：数据构造与查询身份

- `mixed` 数据设计：`i%3` 决定键型（短/长/重复前缀），键首加 `"0/"、"1/"、"2/"` 前缀——**三类键在字典序上各自连续**，"哪些区域该选什么模型"因此有真实答案（类型边界区域最难）；
- `make_queries(..., tuning, offset)`：排序名次 `%4==0` 的键做调参查询，其余做评估——**键身份不重叠**，选择器没见过评估键。

### 3.6 E3 bench.cpp 流程与断言

每区域：依次构建 10 个候选叶 → 逐条核对 → 3 轮×4 遍×512 条调参查询计时取中位数 → 连同容量统计写 `candidates.csv`。然后：预算＝默认配置实际容量估计；`uniform`＝预算内最优单一配置；`sequential`＝先固定块宽选模型、再固定模型选块宽（分步法）；`joint/hybrid`＝拉格朗日。**关键断言**（bench.cpp:68）：构建后 `payload_memory() == selected_bytes(...)`——估计与实建必须一致，防止选型器基于错误容量表决策。

### 3.7 E3 check.cpp 的守恒检查

每 113 步对每个模型叶断言：`primary_entries+free_entries==16`；`primary_bytes+free_bytes+garbage_bytes == B−320`；溢出分类四项之和 == 溢出总数；逐组键数守恒等于 `leaf.size()`。每 223 步随机 `reconfigure`（验证模型叶↔有序叶双向替换正确）。注意 **320 与 16 两处硬编码**——可变槽数改造时必须同步修改（见第 8 节）。

---

## 4. 第三轮 E4：独立验证选择与原生基线

### 4.1 查询身份三分（selector.hpp:18）

`partition(rank) = rank%8`：0/1 调参、2/3 **验证**、4–7 **测试**。E3 的教训是"代理估计排序与真实排序反转"（FINDINGS §6），本轮加独立验证集：所有政策先在验证查询上实测比武，选出胜者并写出 `frozen.json` **冻结**，然后才允许生成测试查询——杜绝"看了测试结果再选参数"。

### 4.2 warm / cross 两类成本代理（selector.hpp:36–63 `measure()`）

- **warm（局部暖代理）**：对每个候选构建整索引后，在单个叶上反复打它自己区域的 128 条查询——E3 的做法，热缓存、无路由开销；
- **cross（跨区域代理）**：同一候选索引上**随机交错**打全部调参查询、经完整外层路由，逐查询计时并按归属区域归集——E3 修正建议的实现；
- 还实测**空时钟开销**（10000 对 `now()` 取中位数 ≈20ns）写入结果——逐查询计时本身会扰动访问节奏，这个数字用于解释偏差来源。

**结果回顾**：cross 代理在混合数据上仍排序反转（预测 498.95 vs 571.95，实测 591.98 vs 537.21）——"跨区域测量是已实现但尚未可靠改进排序的代理"。

### 4.3 Registry：去重、验证、冻结（selector.hpp:72–90）

- `add()` 对**相同配置向量去重**——"default"与"uniform_1"若 id 相同则复用同一份索引和同一组计时，不把同一个东西当两种算法报两次；
- `build()` 批量构建；`validate()` 先逐条核对每个政策在验证查询上的答案（计时外），再 5 轮乱序批次计时，**在预算内**选中位数最优者记为 `validated_policy`；
- 测试查询在物理上进不了这个类（接口只收验证查询）。

### 4.4 native.hpp：LITS/ART 适配的语义细节（每个细节都是坑）

- **`ascii_key()`**：强制无 NUL 的 ASCII。LITS 是 C 字符串 API（`\0` 结尾），前两轮"键尾加 `\0` 构造未命中"的协议无法沿用——这就是**三轮之间 ns/op 不可直接比较**的原因之一；
- **`NativeArt`**：`art_insert/art_search` 的 `key_len` 用 `key.size()+1`——**终止字节算进键**，否则 `"a"` 与 `"aa"` 前缀键无法区分；值存 `value+1` 的 opaque 指针、不解引用——零值得以保留，代价是保留 `UINT64_MAX` 不给用户；
- **`NativeLits`**：直接包原版 `bulkload/lookup/upsert/remove`，析构显式 `destroy()`。发布构建返回值全对，但 ASan/UBSan 依次暴露三个上游问题（PMSS 表越界 → HPT 读 `key[-1]` → 内嵌 HOT 未对齐读取），故**未进正式计时**——失败日志、补丁脚本、复现命令都在 `NATIVE_STATUS.md`。

### 4.5 E4 bench.cpp 全流程（严格时序）

`measure()` 产出两个代理矩阵 → 预算＝默认配置容量 → 注册 9 类政策（去重）→ 全部构建并**断言实建字节＝估计字节**（bench.cpp:21）→ 在 131072 条**验证查询**上比武 → 写 `frozen.json`（胜者、别名、逐区域选择向量）→ **此刻才生成测试查询**（split 2）→ 释放落选政策索引（控内存）→ 构建 `NativeArt` → 3 种子×5 轮：先对每个方法逐条核对全部 262144 条查询（计时外，共 2202 万次核对），再预热 20000、乱序计时、校验和比对。`ART_libart` 的 `bytes=0` 在 CSV 中对应 `bytes_known=0`——**0 表示"未知"，不是零内存**。

### 4.6 两个 check

- `check_native.cpp`：ART/LITS 适配器 10000 次随机 upsert/删除/查找与 `std::map` 对账，**特意加一组值为 0 的前缀键**（`"a","aa","aaa"...`）——正是打 value+1 与终止字节两个语义坑；
- `check_selector.cpp`：三分区键身份两两不相交（含 `~missing~` 变体）、命中率精确 50%、去重生效、验证胜者不超预算且确为可行者中最快。

---

## 5. 辅助脚本与结果目录

| 脚本/目录 | 作用 |
|---|---|
| 各轮 `reproduce.py` | `python reproduce.py`＝编译＋全部正确性检查；`--full`＝完整正式实验（需下载 Tranco XN6XN） |
| E3/E4 `run.py` | 固定实验参数、CPU 绑核、记录源码 SHA-256、CSV 完整性门槛（不完整尝试整体归档 `results/incomplete`，不补造数据） |
| `analyze.py` | 审计原始 CSV 并重新汇总 `summary.json`/结果表 |
| E4 `fetch_sources.py` ＋ `apply_lits_patch.py` | 按固定提交下载 libart/LITS 共 62 文件并逐一核对 SHA-256；应用两处已记录的 LITS 边界补丁 |
| `results/main|smoke|incomplete` | 正式结果 / 连通性试跑（不入结论）/ 不完整尝试（整体排除） |

---

## 6. 贯穿全部代码的三条主线

1. **一个分组函数管到底**：`canonical_group()` 同时被构建、插入、查找、扫描使用；模型、块数、布局任一变化都必须整体重建发布（Region 是原子替换单位）。
2. **预测永远可以被纠正**：指纹只筛选、组号只是入口、最终靠精确比较；扫描正确性靠"bucket 单调 ⇒ 组序＝键序"这条性质，而不是靠模型准。
3. **测量纪律**：乱序、预热、校验和、逐查询核对、身份隔离、诚实标注未知量（`bytes_known=0`）——这套纪律是三轮结论可信的原因，后续改造必须原样继承。

---

## 7. 阅读顺序建议

第一轮 `index.hpp`（从上到下：Model → 布局 → Region → Index）→ `check.cpp`（看它在防什么）→ `bench.cpp` → E3 `selected.hpp` → E4 `selector.hpp`。动手改代码前再读 E4 的 `README.md`、`PROTOCOL.md`、`NATIVE_STATUS.md`。

---

## 8. 下一轮改造接线清单（可变槽数 S 的全部影响点）

把固定 `K=16` 改为 `Config` 参数 S（候选 8/16/32）时，需要同步修改的位置：

| # | 位置 | 改动 |
|---|---|---|
| 1 | `Config`（E3 index.hpp:57） | 增加 `slots` 字段；校验 `B > sizeof(Header)+S×sizeof(Entry)` 且 B 为 64 倍数 |
| 2 | `Header::tags[16]`（E3 index.hpp:130） | 改为 `tags[S]`；S=32 时需 32B，仍在 64B Header 内（8+8+32=48，剩 reserved 缩小）；S>40 才需扩 Header 到 128B |
| 3 | `Region::K`（E3 index.hpp:144） | `K` 改为从 config 取；`START` 变为 `sizeof(Header)+S×sizeof(Entry)` |
| 4 | `put_primary` / `compact` / `find_impl` / `scan` / `group_stats` | 所有 `16`、`K` 的字面量替换 |
| 5 | 组数估算（构造函数） | `键数/(K×load)` 中的 K 用 S |
| 6 | E3 `check.cpp:39` | **硬编码 `block_bytes-320` 改为按布局计算**；守恒断言中的 `16` 同步 |
| 7 | E3 `selected.hpp choice()` | 候选编码从 10 个 id 变为 模型×B×S（约 3×2×2=12＋有序叶）；候选集别膨胀太多，可用容量估算剪枝 |
| 8 | 候选测量与冻结 | 新候选进 `candidates.csv`/`frozen.json`，沿用同一验证流程 |

S×B 的相互约束要在候选集设计时算清：S=32 时 `START=576`，B=512 已不合法；S 越大后缀区越小（B=1024、S=32 时后缀区仅 448B）——"加槽"与"省字节"是同一块内存的再分配，这正是 Tranco（槽瓶颈）与 skew（字节瓶颈）需要不同配置的原因。

---

## 9. 术语速查

| 术语 | 含义 |
|---|---|
| Q / MASS | 位置尺度 2^47 / 概率量化 2^16 |
| row | 概率表行号＝上下文选择（位置 mod 4、前字节 mod 4 等变体） |
| bucket / h | 模型输出 → 组号的非递减映射 `floor(x·G/2^47)` |
| Interval / Residual | 两端同组号即停 / 剩余宽度不足一块即停（后者为参照方法） |
| START | 块内条目区结束偏移＝64+S×16（当前 S=16 即 320） |
| bottom | 后缀区起点，自块尾向下生长 |
| 槽满 / 字节满 | `count≥S` / `后缀长度 > bottom−START`——两类溢出原因 |
| fence | 区域首键，外层路由分隔键 |
| frozen.json | 验证集选定并冻结的逐区域配置，写出后才允许生成测试查询 |
