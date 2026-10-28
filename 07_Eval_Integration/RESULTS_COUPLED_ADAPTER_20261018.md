# F6A：联合页动态类型接入统一接口

F6A实验string-index/v1 bridge已完成能力与语义验收：普通O2与ASan/UBSan各99/99共用验收，
三语料每种构建合计6,506,714项对照检查，输出完全一致，独立复核通过。
冻结F5核心未改；本轮没有动态性能计时，没有生产注册或默认采用。

入口在[F5必要读门通过](RESULTS_COUPLED_PAGE_20261018.md)后执行。
协议：[F6A预登记](diagnostics/F6A_COUPLED_ADAPTER_SPEC_20261018.md)，
实现：[coupled_page_adapter.hpp](diagnostics/coupled_page_adapter.hpp)。
后续：[F6动态矩阵与完整分配计划](../docs/NEXT_ACTION_PLAN_20261018_DYNAMIC_MATRIX.md)。

## 接口与成本

独立工厂`make_coupled_page_index`只接受空参数，固定F5联合点查及15/16初始占用率。
bulk_load/find/insert/update/erase/owning scan支持，range_sum明确抛unsupported。
重复insert保留旧值，缺席update不插入，缺席erase不变；0/MAX不是缺席哨兵。
声明并验证空键、NUL、高位字节、前缀对及长键；仍有页内32位arena/offset限制。

bulk_load把contract Row复制进临时核心行向量，然后构建自有页；scan把自有键移动到contract Row。
这些构建/扫描成本必须在后续账本中保留。显式dirty compact是实验扩展，v1没有通用compact操作；
后续单列并计入维护成本，不能假定Context16/ART/HOT有相同接口。
核心及adapter分别绑定源码bundle哈希与文件哈希，工厂不接受数据集/路径参数。

自计空间分开报告核心与adapter对象、输入保留为0；仍排除map/set节点链接、分配器元数据、
builder内部scratch及完整分配峰值。该bridge没有补齐完整堆口径。

## 真实语料对照

每份语料从相同输入构建bridge、冻结F5裸核心和独立std::map。
执行1024既有键update，再做600步混合场景（含重复insert、删除后缺席update与重插），
合计2224次mutation API调用。前后及compact后核对全量find、完整owning scan、常驻自计及根发布。
1024纯值update期间根只初建一次。结果每种构建如下：

| 语料 | 检查项 | 整份轨迹根发布数（含初建） |
| --- | ---: | ---: |
| tranco64 | 1,265,582 | 99 |
| mixed | 2,581,870 | 171 |
| skew | 2,659,262 | 171 |

共用验收程序与ART/HOT使用同一冻结`adapters/common/contract_acceptance.cpp`，
包括能力声明、参数、bulk_load、值语义、扫描结果在销毁后仍有效、字节域和unsupported行为。
本轮没有重新计时或修改外部基线，不能据此报告它们的最新性能。

## 证据

[身份与命令](results/f6a_coupled_adapter_20261018/identity.json)、
[普通共用验收](results/f6a_coupled_adapter_20261018/acceptance_normal.txt)、
[sanitizer共用验收](results/f6a_coupled_adapter_20261018/acceptance_sanitized.txt)、
[三语料原始门记录](results/f6a_coupled_adapter_20261018/gates.json)、
[执行结果](results/f6a_coupled_adapter_20261018/verification.json)、
[独立复核](results/f6a_coupled_adapter_20261018/independent_verification.json)。
源码归档、完整命令日志及四个二进制哈希均保留，构建产物按规则忽略。

```bash
python3 -B 07_Eval_Integration/diagnostics/run_coupled_adapter.py --output UNIQUE_NEW_DIR
python3 -B 07_Eval_Integration/diagnostics/verify_coupled_adapter.py 07_Eval_Integration/results/UNIQUE_NEW_DIR
```

F6B真实动态轨迹/完整分配入口、F6C同能力动态计时、E剩余泛化及G配置收口尚未完成。
默认仍为Context16，F4单因素null与历史失败判定均保留。
