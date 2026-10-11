# 外部基线适配说明：ART（libart）与 HOT

两个包都在 `07_Eval_Integration/adapters/` 下，与 `our_index` 经过**同一套接口验收程序**
（`adapters/common/contract_acceptance.cpp`）。三方共用一个验收程序是刻意的：
各写一份测试就是三把不同的尺子，而对比结论会落在最松的那把上。

| 包 | `id` | 实现 | 身份 |
|---|---|---|---|
| `art/` | `art-libart` | `armon/libart` | commit `301046804af165269e37da6725f5a4aec9ecc881`，`art.c`／`art.h` 逐文件 SHA-256 钉死 |
| `hot/` | `hot` | `speedskater/hot` | commit `96bf6fb7103b27e50e16a6026db8974c090ee84a`，**ISC 许可** |

---

## 1. ISA 基线：**HOT 决定了整轮的编译下限**

**HOT 需要 AVX2 + BMI2**（其 `CMakeLists.txt` 用 `-march=haswell`／`-march=native`），
**`x86-64-v2` 根本编不出来**——而本项目历轮用的正是 v2。

**处置**：**三个臂全部在 `-march=x86-64-v3` 编译**，而不是各用各的级别。
理由是可比性来自**同一级别**，不是来自继承历史级别。
`x86-64-v3` 恰是 AVX2 + BMI2 + LZCNT，是能承载 HOT 的最低档。

**不用 `-march=native`**：它让二进制依赖构建它的那台机器，结果文件既录不下也复现不了。

**本机已核实**：Intel Core Ultra 9 275HX，`avx2`／`bmi1`／`bmi2`／`adx` 均在（不是假定）。
**这是先查机器再决定的，不是先决定再假设机器支持。**

### 1.1 更正：**发出 v3 不等于全链路都收到 v3**

> **2026-10-09 核实。** 「三个臂统一在 `-march=x86-64-v3`」这句话在初版里**只对适配目标成立**。
> 实际的三个 runner `flags.make`：
>
> | 臂 | 适配目标 | **runner（内含查询循环与计时）** |
> |---|---|---|
> | ours-context16 / ours-lits | `-march=x86-64-v3` | **`-march=x86-64-v3`** |
> | **art-libart** | `-march=x86-64-v3` | **无 v3** |
>
> 原因是传播域：我们的 CMakeLists 用了 `target_compile_options(... PUBLIC ...)`，
> 而 ART／HOT 用了 `PRIVATE`——**PUBLIC 会经 `target_link_libraries` 传给 runner，PRIVATE 不会**。
> 于是**同一个测量循环在不同臂上编在不同 ISA 级别**，而表里的差异会被读成实现差异。
>
> **已把三处的 `-march` 统一为 `PUBLIC`**；HOT 的分配器标志
> （`-fno-builtin-malloc` 等）**保持 `PRIVATE`**——它们关乎 HOT 自己的分配器，不该污染 runner。
>
> **`NDEBUG` 的差异保留并披露**：runner 走 CMake Release 默认带 `-DNDEBUG`，适配目标不带。
> 本项目历轮的计时构建一直是不定义 `NDEBUG` 的，所以**不为了对齐而给适配器加上它**；
> 这个差异写在这一行，而不是留给人去猜。

---

## 2. ART 适配：**键被拷贝、值不被拷贝**

> **2026-10-09 更正**：本节初版写「libart 存的是指针，**键值都不拷贝**」——**半错**。
> 核实 vendored `art.c:359-364`：`make_leaf()` 做
> `calloc(1, sizeof(art_leaf) + key_len)` 并 **`memcpy(l->key, key, key_len)`**——
> **键是被拷进叶子的**；**只有值仍存外部指针**（`l->value = value`）。

| 项 | 事实 | 后果 |
|---|---|---|
| **键** | **libart 拷贝进自己的叶子** | **适配层不需要保留键**——初版的键 arena 是**冗余的第二份拷贝** |
| **值** | libart **只存指针** | 适配层必须让值的地址稳定 |
| 点查 | `art_search(t, ptr, len)` | **完全不需要转换**——契约给的 `string_view` 就是「指针+长度」，**无暂存、无拷贝、无分配** |
| `insert` | `art_insert_no_replace` 返回**旧值**且**不覆写** | **正好是契约的「重复键保留旧值」** |
| `update` | libart **没有 update 原语** | 适配层先 `search`，再往返回的指针里写；**因此 update 不可能插入** |
| `scan` | 只有 `art_iter` 全序遍历，**没有「从某键开始」** | **代价真实**：到达 `lower_bound` 要走完它下面的所有键——**O(下界以下的键数)，不是 O(返回行数)**。已记入适配说明 |
| `range_sum` | 无原语 | 声明 unsupported 并抛异常 |
| 内存 | **libart 无分配钩子、无字节账目** | `index_owned_bytes` 报 **unavailable**，**不拿节点数折算字节** |

**容器选择是这里唯一的正确性要点**：既然**只有值**需要地址稳定，
用 **`std::deque<Value>`** 就够了——deque 的 `push_back` **不会使已有元素的引用失效**，
所以**树永远不需要重建**。

> **初版错在哪**：用 `std::vector` 存值，于是向量增长会移动元素 ⇒ 必须重建整棵树 ⇒
> 而重建又需要键 ⇒ 于是**再保留一份键 arena**。
> **一个容器选错，连锁长出三样东西**（arena、offset 表、长度表）和 `adapter_version` v1 的 26 MB 冗余。
> 改用 deque 后**全部删除**，`adapter_version` 升到 **v2**，内存账目只报一个容器。

---

## 3. HOT 适配：两条硬约束，**都是编译时才发现的**

### 3.1 值类型必须装得进一个指针

HOT 的接口有 `static_assert`：**值类型必须能存进指针**。
`std::pair<const char*, uint64_t>` 是 16 字节，**装不下**——所以 trie 存的是**指向适配层记录的指针**，
再由 `PairPointerKeyExtractor` 从记录里取回键。

**踩过的坑**：我起初自定义了 `struct HotRecord { const char* key; ... }`，
而 `PairPointerKeyExtractor` 要的是 **pair-like 的 `.first`**。
编译报错最后停在 `has no member named 'lookup'`——**完全没提 `.first` 才是真因**。
改用 `std::pair<const char*, uint64_t>` 作记录类型后通过。

### 3.2 键类型是 NUL 结尾的 `const char*`

**这是两个基线之间最大的适配代价差异**：

| | ART | **HOT** |
|---|---|---|
| 点查是否需要拷贝 | **否**（指针+长度） | **是**：`string_view` 不带终止符，**每次查询拷进一个复用缓冲**加 NUL |
| 含 NUL 的键 | 支持 | **无法表示**（终止符与字节无法区分）→ **声明为不支持** |
| 常驻副本 | 键 arena | 键 arena（**每个键多一个终止字节**）|

复用缓冲是成员，所以**热起来以后每次查询是一次 memcpy、零分配**——这是「按终止符寻址」结构的地板，
**如实计入，不当作零**。

**没有采用的做法**：假设调用方的 `string_view` 在内存里后面正好跟着一个 NUL。
那会在每个位于缓冲末尾的键上**多读一个字节**，而且**大多数时候是对的**——
「大多数时候对」不是正确性论证，所以不用。

### 3.3 其余映射

| 项 | HOT 的做法 |
|---|---|
| `insert` | `HOTSingleThreaded::insert` 对已存在键返回 **false 且不覆写** → 契约的 `already_exists`。**`upsert()` 是覆盖式，刻意不用** |
| `update` | **无 update 原语**，先 `lookup` 再 `remove`+`insert`；先查保证**不插入** |
| `erase` | `remove(key)` |
| `scan` | **用 HOT 原生的 `lower_bound()` 直接定位**（`HOTSingleThreaded.hpp:339`）；代价是返回行数，**不是下界以下的键数** |
| `range_sum` | 无原语，声明 unsupported |
| 内存 | 只有节点类型统计、**无字节账目** → `index_owned_bytes` 报 unavailable |

> **2026-10-09 更正**：本节初版把 HOT 的扫描写成「从首键遍历越过下界，**代价同 ART**」——**错**。
> 钉定源码 `HOTSingleThreaded.hpp:339` **提供 `lower_bound()`**，而初版适配器
> （`hot_adapter.cpp` 旧版）**从 `begin()` 遍历**——**那是适配器自己造成的线性代价，不是库的限制**。
> **一个未被使用的原生能力被写成了库的缺陷。**
> 已改用 `lower_bound`，`adapter_version` 升到 **v2**。
> **注意**：HOT 自带的 `scan()` 成员**不能用**——它定位后**多前进一格**并只返回单个值，
> 与契约的包含式下界语义不符。
>
> **另一个要点**：**HOT 与 ART 在这件事上不同**。libart 真的没有「从某键开始」，
> 所以 ART 的扫描线性代价**是库的性质**；HOT 的不**是**。
> 两者并列时**不能都记成「适配成本」**——一个该算库的，一个该算适配器的。

**HOT 的 `lookup`／`begin` 没有 const 限定符**，而契约的 `find`／`scan` 是 const；
适配层用 `mutable` 丢弃限定符，**而不是把接口改宽**。

---

## 4. 三方语义对齐的结果

| 包 | 接口验收 |
|---|---|
| `ours-context16` | **100/100** |
| `ours-lits` | **101/101**（多一条长度上限探针）|
| `art-libart` | **99/99** |
| `hot` | **99/99** |

**验收程序在过程中改过三次，每次都是因为它把某一方的实现选择当成了契约要求**——
记录在此以免再犯：

1. 它一度断言每个操作都必须是 `native`。libart 没有 update 原语、没有可寻址遍历，
   **声明 `adapted` 才是对的**；错的是测试。改为检查**声明与行为一致**。
2. 它一度要求 `index_owned_bytes` 必须为正。**libart 与 HOT 都没有字节账目**，
   报 unavailable 是诚实的；错的是测试。改为要求**口径被写明**。
3. 它一度用 `"\x80\xff\x00\xfe"` 当高位字节探针——**那个串里含一个 NUL**，
   于是一个「允许高位字节、不允许内嵌 NUL」的实现会**因为与高位字节无关的原因**失败。
   **一条探针只测一项权限。**
