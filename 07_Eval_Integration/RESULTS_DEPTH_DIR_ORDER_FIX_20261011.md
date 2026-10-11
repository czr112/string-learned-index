# 等深目录字节序缺陷：复现、修复与验收（P0）

日期：2026-10-11。依据：[核查记录](AUDIT_DEPTH_DIR_ORDER_20261011.md) 与
[截断目录之后：先补正确性，再限额诊断，回到结构优化](../docs/NEXT_ACTION_PLAN_20261011_POST_TRUNCDIR.md) §3。
本文件是该计划 P0 批次的交付：修复、会失败的门、验收运行与新身份。**本轮不接任何计时。**

## 0. 判定

**缺陷已复现、已修复、已用会失败的门与 sanitizer 验收；两种等深目录的语义契约（组号范围、
阈值计数、单调性与任意查询的区间不变量）在完整字节域上成立。** 修复是显式无符号字节比较，
不是 `-funsigned-char`；对字节全部 < 0x80 的输入，新旧实现逐分支等价——这一点由继承门的
输出**逐字不变**直接证实（见 §4.3），因此三语料的历史计时与 TRUNCDIR 轮 A/B 记录继续有效，
只是绑定在修复前的身份上。默认后端仍为 Context16，两种目录仍默认关闭。

## 1. 缺陷

`model/equal_depth_dir.hpp` 的 `predict_group()` 在第一个不同字节处用 `sep[i] < q[i]` 判定
“阈值不大于查询”。`char` 在本机 WSL 与 MinGW 构建上均为 signed，于是 **≥ 0x80 的字节被排在
< 0x80 的字节之下**，与文件声明的无符号字节序（以及 `compare()` 使用的 memcmp）相反。
`EqualDepthDir` 与 `EqualDepthDirTrunc` 同受影响；Context16 不经过这条路径。

审计探针（`scratch_truncdir/order_audit_20261011/probe_unsigned_order.cpp`，四键
`01 7f 80 ff`、四组、前缀 0）：遍历 `00..ff`，两种目录各 **128/256 组号不符、1 处相邻组号下降**。

**为什么既有门没抓到。** 放置与查找共用同一张错图（find 对自己一致）；三语料是 ASCII；
既有域套件包含高位键但只要求 find 成功。整数形式的独立阈值计数是唯一能看见它的参考。

## 2. 修复

```cpp
const bool sep_le_q =
    (i == n) ? (sep.size() <= q.size())
             : (static_cast<unsigned char>(sep[i]) < static_cast<unsigned char>(q[i]));
```

长度相等/前缀结束规则不变，截断规则不变，阈值不变，只有“首字节判序”改成显式无符号。
对全部字节 < 0x80 的输入，`unsigned(a) < unsigned(b)` 与 `a < b` 相同，故 ASCII 语料上的
路由、组分布与计时行为逐分支等价；≥ 0x80 才出现差异。

## 3. 门（会失败的正式门）

### 3.1 新门 `check_depth_dir_order.cpp`（+ sanitizer 孪生，`tools_build.py` 已登记）

对两种目录分别检查：

1. **审计反例**：四键 fixture 遍历 256 个单字节查询，与**独立整数计数**逐项比对；同时统计
   “有符号副本与正确计数不同的查询数”，>0 才算覆盖到了缺陷所在的分支。
2. **带前缀扫描**：共享前缀、在 `7f/80` 处决序、16 键 8 组的 fixture，同样遍历 256 个字节，
   要求零失配、零逆序。
3. **区域级**：键含空串、内嵌 NUL、`7f/80/ff`、共享前缀、重复段与 4096 字节键；
   每个键的放置组 = 独立计数；组号范围；非降序；对全域探针的区间不变量（括号性质）。
4. **完整 Index**：`region_keys=64` 多区域；空键、NUL、`7f/80/ff`、共享前缀族；全部键
   以两种 route 找回原值、缺席探针落空；**从零与任意起点（含非存储键）的 scan 与独立参考
   逐行一致**；插入 200 / 擦除 100 / 值更新 50 / 删后重插 50 之后再验；**输入向量释放后**再验
   （目录与 demand preset 不得指向调用方内存）。
5. **负对照**：保留有符号比较的目录副本必须被同一参考抓出——检查器不是空转。

门还断言自己的 fixture 按**索引自身的无符号序**升序：`bulk_load` 接收的是已排序输入，
`std::string::operator<`（signed char）不是合法的排序器，ASCII 语料掩盖了这一点。

### 3.2 既有门补齐

`check_depth_dir.cpp`（未截断）与 `check_depth_dir_trunc.cpp`（截断）各新增 §3.1(1) 的反例检查：
检查数 131,626 → **131,628**、1,731 → **1,733**。

### 3.3 敏感性（门确实会失败）

把有符号比较**临时**放回源码（脚本在 `finally` 中必然还原，并校验还原前后的 SHA-256 相同），
新门随即报 **98 项失败**（含 §3.1(1) 与 (2) 的全部条目）。修复版重新编译后 **0 失败**。

## 4. 验收（记录工具链：WSL `-O3 -march=x86-64-v2 -static`）

### 4.1 正常门 × 三语料（131,072）

| 门 | mixed | skew | tranco |
| --- | --- | --- | --- |
| `check_depth_dir_order` | 132 / 0 | 132 / 0 | 132 / 0 |
| `check_depth_dir` | 131,628 / 0 | 131,628 / 0 | 131,628 / 0 |
| `check_depth_dir_trunc` | 1,733 / 0 | 1,733 / 0 | 1,733 / 0 |

（“检查数 / 失败数”；日志 `logs/order_fix_gate_20261011.log`，全部 rc=0。）

### 4.2 ASan/UBSan（九格，全部 rc=0，无诊断）

| 门 | mixed 8192 | skew 2048 | tranco 4096 |
| --- | --- | --- | --- |
| `san_check_depth_dir_order` | 132 / 0 | 132 / 0 | 132 / 0 |
| `san_check_depth_dir` | 8,628 / 0 | 2,478 / 0 | 4,528 / 0 |
| `san_check_depth_dir_trunc` | 773 / 0 | 725 / 0 | 741 / 0 |

### 4.3 回归（受影响的目录门、扫描门与集成契约门）

`logs/order_fix_inherited_20261011.log`：`check_g1_contract`（1,410,962 元素比较）、
`check_compact_routing`（928,119 项）、`check_sround`（2,600,812 项；200% 检查点
adv_cmps B/S/W/WS = 3243/99/3240/189）、`check_hole_reuse`（2,283 项；h_placed 2635、
w_moved_bytes 31,557,440 → 30,532,963）全部 rc=0、0 失败；`bench_lits2x2` 重建 rc=0。
**这些输出与修复前的同轮运行逐字相同**，是“ASCII 输入上逐分支等价”的直接证据。
另有 `check_block_rank`（228 项，0 失败）。

## 5. 身份

| 文件 | sha256 |
| --- | --- |
| `validation_e25_20260918/model/equal_depth_dir.hpp` | `8d490657cc266e1f1b8dbc5db8d577ec1d6c5485da560588d70becac4ae69905`（原 `6755f3ca…`） |
| `validation_e25_20260918/check_depth_dir_order.cpp`（新门） | `bfb6214f7d7e8e6710697f2467ad627ccfd1eca53df403937865ddfa0a63bd8c` |
| `validation_e25_20260918/check_depth_dir.cpp` | `00407259d37f529d7962c503ddbface56cb44c0bf9ede51a7fe2712e8af29eb0` |
| `validation_e25_20260918/check_depth_dir_trunc.cpp` | `f78f0942fc6cbd71bd991cec2ab684858410f92561df3ba89bced704cf8e4185` |
| `validation_e25_20260918/tools_build.py` | `d2fb0bbb1a5c4ae4d71333ea2e60a8f9e8ecc92a028a2e586278142d0c6dd001` |
| `validation_e25_20260918/run_order_fix_gate.sh` | `55f5c107858fd9ef7098698693789fb591d5618ab3fa2d19b2e495ea20cd1345` |
| `validation_e25_20260918/run_order_fix_inherited.sh` | `d145e98a3c897c3bc9e044d752b6926eb13e8de081843f0088c7789702934d83` |

四个适配 manifest（`our_index`、`our_index_lits`、`our_index_routing`、`our_index_lits_routing`）
的 `index_hpp_note` 已追加本轮段落并列出上表哈希；`index.hpp` 与 `switches.hpp` 本轮**未变**，
`run_owner_lifecycle.py` 的钉住值无需改动。

**历史计时与本次修复的关系。** TRUNCDIR 轮的 A/B 与既有三语料计时绑定修复前的二进制；由于语料
全部为 ASCII（每个字节 < 0x80），新旧比较逐分支等价，且上文继承门输出逐字不变，故这些计时**不因
本次修复而失效**，也无需为 ASCII 重跑性能矩阵。任何未来包含 ≥ 0x80 字节的运行必须以新身份重新
测量；本轮的修复结论不声称任何性能收益或损失。

## 6. 复现

```bash
cd 02_Code_and_Experiments/validation_e25_20260918
bash run_order_fix_gate.sh        # 三门 × 三语料 + 九个 sanitizer 格
bash run_order_fix_inherited.sh   # 重建并重跑受影响的契约/扫描/路由/空洞门
# 敏感性：把有符号比较临时放回后重跑新门，必须报失败（脚本在 finally 中还原并校验哈希）
python3 _order_gate_sensitivity.py
```

## 7. 边界与下一步

- 本次只修“首字节判序”这一处已复现缺陷；未做任何预测器、摘要或结构改动，目录后端保持默认关闭。
- 未覆盖：≥ 0x80 字节键在 WSL 上的**性能**（本轮不接计时，也不声称结论）；
  以及 `std::string::operator<` 作为排序器这一调用约定陷阱已在门中注明，但未改动 API。
- 下一步按 [计划](../docs/NEXT_ACTION_PLAN_20261011_POST_TRUNCDIR.md) §4 执行 **P1 读路径入口诊断**
  （不接计时；tranco 为诊断重点，mixed/skew 作机制对照），入口不足则转 M4 有序段归并。
