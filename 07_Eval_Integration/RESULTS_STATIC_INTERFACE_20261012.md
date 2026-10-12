# 只读接口与长前缀回退：本轮结果

日期：2026-10-12，起点 ca11873，最终计时为 `results/static_interface_20261012c/`。

**已完成只读接口和正确性验收；回退在skew有效，但整包采用门未通过，默认不改。**

## 直观比较

以下是新种子、90%命中均匀查询的同轮完整点查耗时比；小于1表示更快。
“回退接口”指显式 `lookup=wide_directory`，不是生产动态 Index 或默认参数。

| 语料 / 数据规模 | 查询数 | 回退接口 / ART | 回退接口 / HOT | 回退 / 正式learned | 正式learned / 旧原型 |
|---|---:|---:|---:|---:|---:|
| tranco / 262k | 10k | 1.145 | 0.686 | 0.991 | 1.042 |
| tranco / 262k | 100k | **0.938** | 0.566 | 0.945 | 1.043 |
| mixed / 131k | 10k | 1.232 | 0.760 | 1.026 | 1.049 |
| mixed / 131k | 100k | **1.066** | 0.696 | 1.024 | 1.032 |
| skew / 131k | 10k | 1.021 | 0.912 | **0.844** | 1.041 |
| skew / 131k | 100k | **0.810** | 0.813 | **0.859** | **1.078** |
| tranco / 随机64k子集 | 10k | 1.385 | 0.804 | 0.987 | 1.014 |
| tranco / 随机64k子集 | 100k | 1.368 | 0.750 | 0.992 | 1.046 |

100k队列上，回退接口相对ART：tranco约少6%耗时，mixed约多7%，skew约少19%，方向分别5/5、0/5、5/5更快。
这是重复均匀队列的暖态结果；10k队列和64k数据规模的结果明显不同，不能宣称全面超过ART，也没有冷读或尾延迟结论。

## 本轮做了哪些改进

1. 新增独立 `StaticPhysicalIndex`，脱离动态Index、LITS和评测Row类型依赖，继承前轮的活条目物理块布局与保守预测证书。
2. 新增正式适配包 `adapters/our_static/`：一次bulk_load、自持有find、unsigned-byte有序owning scan。
   释放建造输入后仍可查，扫描结果可跨索引析构使用；写操作和range_sum明确抛UnsupportedOperation。
3. 唯一新读优化：证书宽度超过3块时，仅在证书范围内二分完整后缀fence，再查一个块。
   不新增目录副本、不按语料名选策略，不调整预测器、坐标宽度或槽数。
4. 共用验收按声明支持检查可选操作，修正了“只读也必须支持全部写操作”的错误要求。
   大于1MiB的键长声明上限不再使小验收程序分配数GiB；明确记录未以分配方式测试32-bit长度极限，实际长键门覆盖4096字节。

| 100k查询 | learned访问块 | 回退访问块 | 触发回退的查询 |
|---|---:|---:|---:|
| tranco / 262k | 229,402 | 220,866 | 1.012% |
| mixed / 131k | 764,708 | 169,701 | 35.990% |
| skew / 131k | 3,806,530 | 100,070 | 99.931% |

访问块大幅减少不等于完整查询同比加速：mixed新增二分比较抵消了收益；不能仅靠计数宣布优化成功。
核心常驻记账与旧原型相同：tranco/mixed/skew为8.12/13.11/12.92 MB，适配对象另16B。
计容量与对象，分配器/RSS/构建峰值未知；ART完整空间仍未知，本轮不作空间优于ART的结论。

## 验收和采用决定

- 共用验收：只读128/128；现有Context16、ART、HOT各99/99。CMake独立适配包也128/128。
- 四份完整结构门（含64k）分别10,371,955 / 6,767,316 / 7,851,594 / 7,982,764项通过；四份ASan/UBSan rc=0、stderr=0。
  含空集/空键、NUL、高位字节、前缀关系、4096长键、2000碰撞键、两种区域粒度、两轮两字节穷举、块/区域邻点和扫描跨析构。
- 每个进程另查全量语料键和全部查询，六臂答案与所有计时checksum一致。
- skew回退/learned为0.844/0.859，两队列都5/5更快，超过各自A/A带1.136/1.048，**回退的场景收益门通过**。
- 但skew/100k正式learned/旧原型=1.0775，4/5更慢，超过5%门槛和F=1.0479，**集成回归门失败**。
  其他七格未触发该停止条件；这不等于证明每格零成本。
- 最终 `retain_wide_directory=false`：保留源码及显式实验参数供复现，整包没有采用资格。
  只读适配器空参数仍选learned；生产默认仍Context16，C/W/S默认及动态Index源码均未改。

下一步先定位7.8%的集成回归，再减少短键/小规模的固定导航成本，详见
[后续计划](../docs/NEXT_ACTION_PLAN_20261012_AFTER_STATIC_S3.md)。不增加重复或改变判据来挽救本轮。

## 可复现证据与本轮修正

最终c：5进程×6次偶数Williams顺序×6臂，每臂立即等量预热，位置与有向前驱都平衡；
10k×20遍或100k×2遍均200k次查询。先取同重复配对比值，再取每进程和5进程中位数。
同一个learned实例构成真实A/A；F仅为本轮观测带，不是置信区间。
query seed=2026101202，64k均匀抽样seed=2026101203；编译g++13.3.0，-O3/-std=c++17/-march=x86-64-v3，CPU0。

- a在计时前停止：误要求O2与O1+san工作量计数完全相同。两者答案、窗口和扫描验收均通过，tranco块计数仅差3。
  修正为各构建独立验证语义并核对覆盖量，完整保留两份工作计数；未改核心来使计数相同。
- b旧对照误用内嵌核心，原已测PackedView使用shared_ptr堆核心，集成比值不具资格。
  b原驱动、原判定、原记录保留，review.json撤回采用资格。c仅恢复正确的堆对象包装，不改候选或通过条件。
  b回退/learned的skew两队列亦5/5更快，但小队列未超A/A带；不选择性用它替换c的判定。
- c复用b的已通过结构门：核心/适配器/全部门依赖、编译器、输入与门二进制哈希均一致；只重建计时驱动并逐进程重验全量答案。
  独立复核通过8格、1,440计时行、1,440配对比值及168份源码身份；原始日志无损gzip保存。
- CMake构建成功，缩进告警保存在package_build.log；sanitizer的stderr为空不代表所有编译日志零告警。

实现身份：核心 `bb4befc3fa1a3ad5…`；适配器摘要 `38a0e8303c9bf86c…`。CMake按实际源码自动绑定descriptor，manifest与之相符。
生产index.hpp仍 `c8a1ae3c73737148…`，旧S2原型/驱动及报告保持原样。

源码、门、驱动与独立复核：`static_physical_index.hpp`、`adapters/our_static/`、`diagnostics/{check,bench,run,verify}_static_interface.*`。
证据：`results/static_interface_20261012c/{identity,gates,raw,summary,decision,verification}.json`。
重跑需原S2固定版本外部依赖及其build中的ART/HOT对象；输入依赖matrix2的rows.bin，生成输入和构建二进制不提交。

```bash
python3 -B 07_Eval_Integration/diagnostics/run_static_interface.py --output static_interface_new_name
python3 -B 07_Eval_Integration/diagnostics/verify_static_interface.py 07_Eval_Integration/results/static_interface_new_name
```

不传reuse-gates时完整编译/执行验收；传入旧S3证据目录时只有依赖与输入哈希完全一致才复用。输出目录禁止覆盖。
