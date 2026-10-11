# 集成轮：LITS 参考表常驻（默认关闭开关）

日期：2026-10-11。承接 [M3 入口](../07_Eval_Integration/RESULTS_OWNER_ENTRY_20261011.md)、
[原型](../07_Eval_Integration/RESULTS_OWNER_PROTOTYPE_20261011.md) 与 [长期路线](STRUCTURE_OPTIMIZATION_ROADMAP_20261011.md)。

## 1. 改动（生产源码）

`Config::lits_skip_reference_table`（默认 false）：为真时，**LITS 校准成功**的区域在映射确定后
释放它永远不会读的 Context16 参考表。为了不改变默认路径的任何行为，训练**照旧执行**、随后释放
（不移动构造顺序）——因此这是一个**纯空间候选**：常驻省下 16,512 B/区域，构建时间不变。
代价与边界如实写入代码注释：R1/preset 与「释放」的组合**显式抛错**（其 digest 依赖该表），
`group_of()` 在无表区域**显式抛错**（需要参考答案的调用方必须持有独立对象）。

开关在 `switches.hpp` 的**四处**全部登记：`baseline_config`、`pin_frozen_switches`、`switches()`、
`require_only_switches_differ` 的字段表。默认值**不翻转**。

## 2. 开跑前固定的判据

1. **OFF 逐位不变**：用生产头文件的入口装置重跑六格，记录必须与 `results/owner_entry_20261011/work.json`
   **逐字段相同**（不是「门还绿」）。
2. **ON 复现原型**：用生产头文件重建原型装置（不使用探针），六格门全过；节省 = 理论可省字节且 ≥90%；
   Context16 控制臂节省 0、答案不变；构建/点查/更新/扫描四阶段 × 六格无「可分辨 >5% 回归」
   （判据与原型轮同一份代码，不另立口径）。
3. **开关表**：四处登记各自唯一命中；`switches()` 输出包含该字段名。
4. **sanitizer**：ON 的门在 ASan/UBSan 下六格全绿、stderr 空。
5. **适配身份**：四个适配 manifest 同步新的 index.hpp/switches.hpp 哈希，检查器通过。
6. 默认值不翻转；采用与有限泛化验收留给路线图 §6。

## 3. 边界

- 只测普通 `bulk_load`、标准查询模式、插入/擦除/扫描；R1/preset 与固定地址模式不在本轮。
- 参考诊断**没有**在本轮获得独立参考对象；OFF 路径不受影响，ON 路径上它们会显式抛错。
- 不追求构建时间收益（训练照旧）；若将来要「不训练」以省构建时间，那是另一个候选，需另行预注册。