# E4：可运行的跨区域选择器与原生基线适配

2026-09-11。先读 `FINDINGS.md` 与 `RESULTS_TABLE.md`，协议见 `PROTOCOL.md`。

本目录继承相邻 `validation_e3_20260906` 的存储实现；两目录须保持并列。主代码是 `selector.hpp`、`native.hpp` 和 `bench.cpp`。`Registry::validate` 接收独立验证查询，冻结后的选择保存在每次运行的 `frozen.json`；后续查找调用所选 `SelectedIndex::find`。它是可复用的离线配置流程，尚未增加在线漂移检测和并发发布。

## 复现

Linux x86-64、GCC/G++、Python 3、`taskset`，CPU 需支持依赖使用的 `-march=native` 指令。

```bash
python validation_e4_20260911/reproduce.py
python validation_e4_20260911/reproduce.py --full --tranco /absolute/path/tranco_XN6XN.csv
```

第二条命令将完整新实验写到 `results/reproduced`，不会覆盖本次 `results/main`。若不传 `--tranco`，自动下载并校验固定快照。输出目录已存在时拒绝重跑，避免误覆盖；另用 `--output` 指定新位置。

第三方源码不重复装入此验证包，`fetch_sources.py` 从固定 GitHub 提交获取 62 个文件并逐一核对 SHA-256；`apply_lits_patch.py` 应用已记录的小补丁。初次复现需要网络。源码来源和逐文件摘要见 `sources.json`，下载内容中保留原始声明，ART 的 LICENSE 随下载取得。

```bash
python validation_e4_20260911/fetch_sources.py
python validation_e4_20260911/apply_lits_patch.py
python validation_e4_20260911/analyze.py
```

最后一条审计并重新汇总交付的主实验数据；分析不同输出目录使用 `--input`。它也检查运行时记录的本地源码摘要，不应在改源后直接把旧结果当作新结果。

## 文件与接口

| 文件 | 内容 |
|---|---|
| `selector.hpp` | 查询身份三分、局部／跨区域候选测量、配置去重、预算内验证选择 |
| `native.hpp` | LITS 与 armon/libart 的键值语义适配和对象生命周期 |
| `bench.cpp` | 先验证并写出冻结配置，再测试；正式计时外逐查询核对 |
| `check_selector.cpp` | 查询身份隔离、命中比例、配置去重、验证最小值与预算检查 |
| `check_native.cpp` | 前缀键及零值，随机 upsert／删除／查找，与 `std::map` 核对 |
| `run.py` | 固定三组参数、CPU 绑定、源码摘要、CSV 完整性门槛 |
| `results/checks` | 成功和失败的真实检查日志 |
| `results/smoke` | 正式运行前的小规模连通性验证，不纳入性能结论 |
| `results/main` | 候选矩阵、验证计时、冻结配置、测试计时、运行与完成记录 |
| `summary.json` | 别名解析、空间估计、逐种子中位数、测试后诊断 |

所有输入串在本轮均为不含 NUL 的 ASCII，50% 精确命中；未命中为源键追加 `~missing~`，在计时外确认不存在。ART 的 key_len 包括终止字节，避免前缀键歧义；以 value+1 保存不解引用的 opaque 值，保留零值，适配器不支持 UINT64_MAX。LITS 调用其原生 API，并在析构时显式 `destroy()`。

空间预算只约束本原型的建成叶容量估计。原生 ART/LITS 缺少本轮统一的完整空间计量，CSV 中 `bytes_known=0`；零表示未知，绝不是零内存。数组构建耗时记为零仅表示复用已排序输入，不能据此与原生构建耗时排名。

多轮批次计时用于展示重复性，不等价于独立机器实验，也不提供请求 P99。没有 PMU 计数，不能证明缓存未命中降低。与前轮不同的未命中生成规则及命中比例使跨轮 ns/op 不可直接比较。
