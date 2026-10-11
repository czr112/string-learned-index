# 适配说明：`our_index`（string-index/v1）

对应 `02_Code_and_Experiments/validation_e25_20260918/index.hpp`，
SHA-256 `4fb0a25fba8f16a339d87f67ead6e3501ce1bb6c9d4fccb8a0dddfe9180c40f7`，5,423 行。

两个固定包：**`our_index`**（`id=ours-context16`，诊断对照）与 **`our_index_lits`**（`id=ours-lits`，临时参考后端）。
**两者共用同一份实现与同一套构建规则**，只差一个编译定义——后端不可能被"忘了传参数"而静默选错。

---

## 1. 接口映射

| 接口 | 映射到什么 | 适配层做了什么 |
|---|---|---|
| `descriptor()` | 常量 ＋ 编译期后端名 ＋ `index.hpp` 哈希 | 无 |
| `bulk_load(rows)` | `Index::bulk_load(vector<Pair>)` | **一次 `Row`→`Pair` 转换拷贝**（见 §3） |
| `find(key)` | `Index::find(key, Mode)` | **无转换、无暂存、无分配** |
| `insert(key,value)` | `Index::insert` | `bool`→枚举 |
| `update(key,value)` | `Index::update` | `bool`→枚举 |
| `erase(key)` | `Index::erase` | `bool`→枚举 |
| `scan(lower,limit)` | `Index::scan(start,count)` | 把已有 `vector<Pair>` 的键**移动**成 `Row` |
| `range_sum` | **无实现** | 抛 `UnsupportedOperation`，并在能力里声明 unsupported |
| `memory_usage()` | `Index::memory_breakdown()` 九个字段求和 | 加适配层自身占用，声明口径 |

**查询 `Mode` 逐字段复制自本项目自己的基准**（`bench_widest.cpp`、`bench_i10.cpp`）：
`Route::Interval`、`Prefetch::Off`、`fingerprint=true`、`simd=true`、`OvSearch::Hybrid`、`locate=false`。
**改任何一项，这一轮的数就与本项目历轮不可比。**

---

## 2. 键域审计：**抓到一个真实缺陷**（本阶段最有价值的产出）

计划要求「分别检查 Context16 与 LITS 后端对空键、NUL、高位字节、前缀键和长键的支持，
**语料里没出现某类键，不代表实现支持它。限制必须进入能力声明。**」

**初版把 `allows_high_bit` 与 `allows_nul` 无条件写成 `true`，而 LITS 后端拒绝高位字节**——
验收程序第一次运行就抛了 `lits_hpt: key 4 is outside the supported domain (byte >= 128)`。

| 键类 | Context16 | **LitsHpt** | 依据 |
|---|---|---|---|
| 空键 | 接受 | 接受 | 实测 |
| **内嵌 NUL** | 接受 | **拒绝** | `model/lits_hpt_adapter.hpp:87` |
| **高位字节 ≥0x80** | 接受 | **拒绝** | `model/lits_hpt_adapter.hpp:88` |
| **长度上限** | 无声明上限 | **4096** | `kMaxKeyBytes`（同文件 `:167`） |
| 前缀键对 | 接受 | 接受 | 实测 |

**能力声明已按后端分开，并且从两侧钉住**：声明允许的键类必须真被接受，
声明不允许的必须真被拒绝（否则声明只是注释）。

**本轮三份语料都没触及这两个限制**：最大字节 **122**、最长键 **158**。
**所以这个限制是"声明里有、数据里没有"**——正是必须写进声明而不是从语料反推的情形。
评测方的运行前检查会据此在键域不符时把该组合记为 `unsupported`，不会偷偷删数据。

**另一处踩过的坑（已写进验收程序注释）**：域检查在 `SharedModel::train` 里，
而该函数**键数 < 2 时提前返回**——所以**单键探针根本不会触发域检查**，
会为「实现其实拒绝的键」报出「接受」。初版三个「拒绝」探针全是单键，**读起来是三个通过的检查、实际什么都没验**。

---

## 3. 所有权与适配开销

| 项 | 事实 | 计费 |
|---|---|---|
| `find` 的 `string_view` | **不保存**，只在调用内使用 | 无 |
| 插入后保存的键 | 实现在 `insert_into` 里 `memcpy` 进块，**自己持有** | 归 `index_owned_bytes` |
| `scan` 结果 | 实现在 `Index::scan` 里已返回**自持有**的 `vector<Pair>`，适配层把键**移出** | 归调用者，不计入常驻 |
| `bulk_load` 的 `Row`→`Pair` 转换 | **一次全量键拷贝**（类型不同，无法零拷贝），`bulk_load` 返回后**立即释放** | 构建期成本，**非常驻** |
| 点查路径 | **不新增字符串复制、不新增分配、不重复路由、不重复判重** | 无 |

**验收程序直接测了"扫描结果在索引销毁后仍有效"**——返回视图的实现会通过上面每一项、只在那一项上失败。

---

## 4. 验收结果

`adapter_acceptance.cpp`（`OUR_INDEX_BUILD_ACCEPTANCE=ON` 时构建）驱动契约声明的**每一个**操作，
以独立 `std::map` 为参照，覆盖：描述符自洽、参数校验（未知/重复/非法值）、
`bulk_load`（空输入、乱序拒绝、重复拒绝、二次调用拒绝）、
`find`（**值为 0 与 `UINT64_MAX` 不得与"缺席"混淆**）、
`insert`（**重复键保留旧值**，这正是覆盖式 `upsert` 会漏的那条）、
`update`（缺席不得插入）、`erase`（缺席、最后一个键）、
`scan`（起点不存在、`limit=0`、末尾不足、包含式下界、**结果自持有**）、
键域双向、不支持操作抛异常、内存口径。

| 后端 | 结果 |
|---|---|
| `ours-context16` | **99/99 通过** |
| `ours-lits` | **100/100 通过**（多一条：长度上限探针） |

---

## 5. 已知偏差（**必须在报告里带着**）

1. **`ov_threshold` 固定为 128**。契约不传数据集名，所以适配层够不到本项目按语料冻结的表
   （mixed 128、**skew 64**、tranco 128）。**skew 那一格不是本项目的调优配置。**
   适配层已接受 `ov_threshold` 参数（供将来命令行接入口），但当前入口传空参数列表。
2. **LITS 臂有 17%–18% 的区域回退到 Context16**（E38 记录），**它不是纯 LITS 结果**。
3. **ISA 固定在 `x86-64-v3`**。三个臂必须同级别才可比，而 HOT 的硬约束就是不能低于 v3。
4. **断言保持开启**（不定义 `NDEBUG`），与本项目历轮计时构建一致；
   CMake 的 Release 默认会关掉它们，所以本目录单独覆盖了 `CMAKE_CXX_FLAGS_RELEASE`。
5. **不改上游**：适配层没有为接入修改 `index.hpp` 的任何一行。

---

## 6. 复现

```bash
# 评测入口（构建 + 正确性检查 + 计时）
python3 -B scripts/benchmark.py \
  --dataset ../datasets/mixed.keys \
  --implementation ../adapters/our_index \
  --implementation ../adapters/our_index_lits \
  --output ../results/<name>

# 接口验收（评测入口不检查可选操作，这一项要单独跑）
cmake -S adapters/our_index -B build/acc -DOUR_INDEX_BUILD_ACCEPTANCE=ON \
      -DSTRING_INDEX_CONTRACT_INCLUDE_DIR=<repo>/src
cmake --build build/acc -j4 && ./build/acc/our_index_adapter_acceptance
```

**本轮补齐的环境前提**：WSL 里原本没有 `cmake`（评测入口因此报
`cmake executable is unavailable`）。已装**用户级** cmake 3.30.5 到 `~/.local/opt` 并软链到
`~/.local/bin`（该目录已在 `~/.profile` 的 PATH 里），**不需要 root、不影响系统包**。
必须用 WSL 的 g++：本索引在 Windows MinGW 下编不过（`lits_model.hpp` 的 `1UL << 52`）。
