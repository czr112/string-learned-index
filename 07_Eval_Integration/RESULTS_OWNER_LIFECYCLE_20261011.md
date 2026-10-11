# M3 生命周期补验（P0）结果

日期：2026-10-11。计划：[NEXT_ACTION_PLAN_20261011_MERGE.md](../docs/NEXT_ACTION_PLAN_20261011_MERGE.md) §2（P0）；
被补的缺口：[REVIEW_OWNER_INTEGRATION_20261011.md](REVIEW_OWNER_INTEGRATION_20261011.md) §3.1。
装置：`diagnostics/check_owner_lifecycle.cpp`（9de2b6b3…）、`diagnostics/run_owner_lifecycle.py`（5236d143…）；
记录：`results/owner_lifecycle_20261011/`（raw/san/decision/identity/commands.log，独占创建，未覆盖任何旧结果）。

生产身份（本轮）：`index.hpp` b20c83ca…（未动）、`switches.hpp` 72856bdc…（未动）、
`model/lits_hpt_adapter.hpp` **97195dd7… → 51c52d68…**（本轮修复，见 §3）。

---

## 0. 判定

补门完成：复核指出的验收缺口全部补上，六格（3 语料 × 2 后端）在计数与 ASan/UBSan 两套构建下**全部通过**，
sanitizer 记录与计数记录逐字节一致（28,319 B，identical=True），无一处 mismatch、无一处所有权违规。

期间 ASan 在 LITS 查询路径抓到一处**真实堆越界读**并修复（影响身份，§3）。
因此：M3 集成轮的空间/时间数字仍绑定旧适配器身份，**采用（M5）前必须在新身份上重验**；
M4 及其后实验一律在新身份上执行。

---

## 1. 门覆盖了什么（逐条对上 review §3.1）

同一串更新程序（插入/擦除/覆盖更新）同时作用在 OFF、ON 与独立无符号字节序参考表三个对象上；
每个检查点做完整键/值枚举、size、存在/缺席探针与跨区域扫描（Index 级，非单 Region）。

| 复核指出的缺口 | 本门证据（六格一致，数字为三语料相同值） |
|---|---|
| `gate_split` 未要求真实分裂 | `Index::splits` 增量 2/2（OFF/ON 相等），`Index::rebuilds` 增量 2/2；分裂后左右子区各自收到插入/更新/删除（`child_left_ok`/`child_right_ok`），并有跨子区扫描 8192 行对照 |
| 仅 ON 收到额外插入、无同状态对照 | 6,501 插入 / 952 擦除 / 618 覆盖更新全部三对象同序列；`op_result_mismatch=0` |
| 分裂/重建后无完整正确性 | 每检查点全键值枚举：mixed/skew 1,062,425 次 find、tranco 2,111,001 次；`full_find_mismatch=0`、`off_on_mismatch=0` |
| 前缀重建未正触发 | 三个确定性前缀破坏用例（迟到键/空键/含 NUL 与高位的键）：OFF/ON `rebuilds` 增量各为 1、`splits=0`，全部 mismatch 0 |
| LITS 所有权只验静态 | 分裂后与终态两处：LITS 区域 0 个持表、fallback 区域全部持表（mixed 63/3、skew 65/1、tranco 129/1），OFF 侧无任何区域失表 |
| `group_of()` 拒绝未验 | ON：拒绝且消息含 `lits_skip_reference_table`；OFF 对照正常作答（`values_equal` 仅记录：LITS 映射与参考模型对同一键不必同组） |
| 有效 preset 拒绝未验（且不得用 null context 冒充） | `bulk_load_reusing_ids` 的**真实 BuildContext**（含逐区域 digest）：OFF 接受并全量扫描一致；ON 以 `lits_skip_reference_table` 消息拒绝。null-context 路径（`bulk_load`）在 ON 上照常成功——这正是"拒绝只针对有效 preset"的对照 |
| 空键/NUL/高位/长键按后端真实域组织 | 见 §2 域探针；拒绝均为**规定拒绝**（"supported domain"/"accumulation bound"），OFF/ON 对称，成功部分与参考逐键一致 |

## 2. 六格记录摘要

| 语料/后端 | 检查点 | splits | LITS 区域/fallback | 所有权违规 | 域拒绝 | gate_pass |
|---|---:|---:|---|---:|---:|---|
| mixed/context16 | 8 | 2 | 0 / 66 | 0 | 0 | true |
| mixed/lits | 8 | 2 | 63 / 3 | 0 | 2 | true |
| skew/context16 | 8 | 2 | 0 / 66 | 0 | 0 | true |
| skew/lits | 8 | 2 | 65 / 1 | 0 | 2 | true |
| tranco/context16 | 8 | 2 | 0 / 130 | 0 | 0 | true |
| tranco/lits | 8 | 2 | 129 / 1 | 0 | 2 | true |

域探针（lits，三语料一致）：
- `domain-empty-nul-high`：**构建**即拒绝（`key 1 is outside the supported domain (embedded NUL)`），OFF/ON 同消息——共享表训练对域外键是整体拒绝，不是逐区域回退；
- `domain-long-keys`：构建通过，4097/5000 字节插入以 `key is longer than the accumulation bound was written for` 拒绝；
- `domain-empty-base`：单键边界区域不拒绝（不建共享表、不 walk），行为与参考一致；
- `domain-support-control`：域内集合全过（防止"拒绝一切"式假阳性）；
- 查询域（新）：6 个含 NUL/≥0x80 的**查询键**在 ON/OFF 上全部、同消息拒绝；context16 对同样 6 个键全部作答且与参考一致。

## 3. 发现的真实缺陷与修复（本轮最重要）

**触发**（最小复现）：LITS 索引，共享表训练自 `{"a", 4096×'x'}`；`Index::scan("a\x80\xff")`。

**ASan**：`heap-buffer-overflow READ of size 8`，地址恰在训练表 2048 字节块末尾之外；
栈：`lits_hpt::RegionModel::predict_group` ← `Region::canonical_group` ← `Region::scan` ← `Index::scan`。

**机制**：适配器的 walk 用**无符号字节**索引 `m[i & PS_MASK][fc][key[i]]`，而每个 (ps,fc) 表块只有 0..127 项；
构建侧（`train()`/`calibrate()`）确实拒绝 NUL 与 ≥0x80，但**查询键从不经过这两条路**——
适配器注释里"适配器拒绝这些键，所以 walk 不会见到"的假设在查询路径上不成立。
`0x80` 是确定性越界；`0xff` 等是否越界取决于 (ps,fc) 分支与早退位置，此前"干净"纯属布局运气。

**修复**：`model/lits_hpt_adapter.hpp` 在 `predict_group` 的 walk 之前加同域拒绝（复用 `unsupported_reason`），
消息 `lits_hpt: key is outside the supported domain (embedded NUL|byte >= 128)`。
修复后参数化复现（`diagnostics/repro_lits_query_domain.cpp`，14 组字节组合）全部受控拒绝、无任何 ASan 报告。

**边界（如实写）**：拒绝是**区域级**的——只有真正 walk 的 LITS 成功区域拒绝域外键；
fallback/边界区域经参考模型作答，其字节域不受限。对外评测的 LITS 包已声明高位/NUL 不支持（ADAPTER_NOTES §域表），
因此对外路径不受此修复影响；受影响的只有"域外查询键打在本索引 LITS 区域上"这一内部情形。

**身份**：适配器 97195dd7… → 51c52d68…；`index.hpp`/`switches.hpp` 未动。
旧 M3 空间/时间数字绑定旧适配器；采用前重验（计划 §2 明文要求）。

装置自身的两次修正（如实记录）：
1. 首版 verify() 用"最长键+1 字节"作扫描起点——正是它把本缺陷抓了出来；修复后该起点不再用于常规数据对照，
   域外查询由显式查询域探针检查；
2. 分裂子区探针最初用含内嵌 NUL 的键，修复后该键被（正确地）拒绝，探针改为域内 ASCII 键（NUL/高位的
   行为由域探针与查询域探针覆盖）。

## 4. 判定口径与限制

- 本门**不产时间**；唯一 pass 条件是零 mismatch、全部正触发、所有权不变量与 sanitizer 干净。
- analyzer 自检 11 项（缺格、门假、无分裂、所有权违规、无 fallback、跨语料不一致、未拒组号、未拒 preset 等）
  全部被拒收后才开始计时无关的执行。
- 三语料六格记录 spread 极小（除语料规模外逐字段一致）：本门是正确性装置，不用于排名。

## 5. 下一步

1. M4 入口（P1）在新身份上执行；`NEXT_ACTION_PLAN_20261011_MERGE.md` §3 的"起始源码"补记适配器哈希。
2. M5 采用阶段：在新身份上重验 M3 空间与时间（预期不变，但必须实测，不能搬旧数字）。
3. `check_lits_model` 等既有 LITS 门在新适配器上复跑一遍（其域外样本此前走的是"幸运不越界"路径）。