"""Export the frozen pre-model external benchmark evidence; never rerun timings."""
import csv
import hashlib
import json
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'EXTERNAL_INTERFACE_BASELINE_PRE_MODEL_20261010.md'
EVIDENCE = OUT.with_suffix('.evidence.json')
ARMS = ['ours-context16', 'ours-context16-routing', 'ours-lits',
        'ours-lits-routing', 'art-libart', 'hot']
LABELS = ['Context16', 'Context16+C', 'LITS', 'LITS+C', 'ART', 'HOT']
CORPORA = ['mixed', 'skew', 'tranco']
IDENTITY = 'f573480d5ab602cb29de68ffc8e1ff480f4ad9b89c4a5049cea0bf69d0687f0a'


def read_round(name):
    folder = ROOT / 'results' / name
    report = json.loads((folder / 'report.json').read_text(encoding='utf-8'))
    with (folder / 'summary.csv').open(encoding='utf-8', newline='') as f:
        summary = {r['implementation_id']: r for r in csv.DictReader(f)}
    with (folder / 'samples.csv').open(encoding='utf-8', newline='') as f:
        raw = [r for r in csv.DictReader(f) if r['sample_type'] == 'throughput']
    repeats = report['workload']['repeats']
    assert set(summary) == set(ARMS)
    checksums = set()
    samples = {}
    for arm in ARMS:
        r = summary[arm]
        result = next(x for x in report['results'] if x['implementation_id'] == arm)
        assert r['status'] == result['status'] == 'passed'
        checksums.add(result['runner_result']['checksum'])
        values = {int(x['repeat']): int(x['lookup_ns']) for x in raw
                  if x['implementation_id'] == arm}
        assert len(values) == repeats
        assert len([x for x in raw if x['implementation_id'] == arm]) == repeats
        assert median(values.values()) == int(r['median_lookup_ns'])
        builds = [int(x['build_ns']) for x in raw if x['implementation_id'] == arm]
        assert median(builds) == int(r['median_build_ns'])
        ops = int(r['operations'])
        throughput = [ops * 1e9 / x for x in values.values()]
        assert abs(median(throughput) - float(r['median_throughput_ops_per_second'])) < 1e-6
        if arm.startswith('ours-'):
            assert result['build_identity']['adapter_declared_source_identity']['index_hpp_sha256'] == IDENTITY
        samples[arm] = values
    assert len(checksums) == 1
    return report, summary, samples


def table(headers, rows):
    return '\n'.join(['| ' + ' | '.join(headers) + ' |',
                      '|' + '|'.join(['---'] * len(headers)) + '|'] +
                     ['| ' + ' | '.join(map(str, r)) + ' |' for r in rows])


def wide(rounds, field, scale, digits):
    rows = []
    for corpus in CORPORA:
        summary = rounds[corpus][1]
        rows.append([corpus] + [f'{float(summary[a][field]) / scale:.{digits}f}' for a in ARMS])
    return table(['语料'] + LABELS, rows)


def main():
    if OUT.exists() or EVIDENCE.exists():
        raise SystemExit('REFUSED: existing report/evidence; choose a new output identity.')
    rounds = {c: read_round('matrix3_' + c) for c in CORPORA}
    strong = read_round('matrix3s_tranco')
    assert rounds['tranco'][0]['workload']['queries_sha256'] == strong[0]['workload']['queries_sha256']
    dataset_rows = []
    for c in CORPORA:
        d = rounds[c][0]['dataset']
        dataset_rows.append([c, f"{d['unique_keys']:,}",
                             f"{d['total_unique_key_bytes']/d['unique_keys']:.2f}",
                             f"{d['min_key_bytes']}–{d['max_key_bytes']}"])
    ratios = []
    for c in CORPORA:
        s = rounds[c][1]
        t = lambda a: float(s[a]['median_throughput_ops_per_second'])
        ratios.append([c] + [f'{t(a)/t(b):.3f}' for a, b in
                             [('ours-context16', 'art-libart'), ('ours-lits', 'art-libart'),
                              ('ours-context16', 'hot'), ('ours-lits', 'hot')]])
    latency_rows = []
    memory_rows = []
    for c in CORPORA:
        s = rounds[c][1]
        latency_rows.append([c] + [s[a]['latency_p50_ns'] + ' / ' + s[a]['latency_p99_ns'] for a in ARMS])
        memory_rows.append([c] + [f"{int(s[a]['index_owned_bytes'])/2**20:.3f}" for a in ARMS[:4]] + ['不可用', '不可用'])
    ss = strong[1]
    strong_rows = [[label, f"{int(ss[a]['median_lookup_ns'])/1e6:.3f}",
                    f"{float(ss[a]['median_throughput_ops_per_second'])/1e6:.3f}",
                    f"{int(ss[a]['median_build_ns'])/1e6:.2f}"] for a, label in zip(ARMS, LABELS)]
    pair_rows = []
    paired_evidence = {}
    for off, on, label in [('ours-context16', 'ours-context16-routing', 'Context16'),
                           ('ours-lits', 'ours-lits-routing', 'LITS')]:
        a, b = strong[2][off], strong[2][on]
        ratios_paired = [b[r]/a[r] for r in sorted(a)]
        faster = sum(r < 1 for r in ratios_paired)
        pair_rows.append([label, f'{median(ratios_paired):.3f}', f'{faster}/15',
                          f'{min(ratios_paired):.3f}–{max(ratios_paired):.3f}'])
        paired_evidence[label] = {'ratios_by_repeat': ratios_paired,
                                  'median': median(ratios_paired), 'faster': faster}
    text = f'''# 字符串学习索引：接入新模型前的外部接口基准报告

测试日期：2026-10-10。整理日期：2026-10-11。范围：统一外部接口下的单线程、内存、静态点查询。

## 1. 主要结果

**接入与本轮查询正确性验证全部通过，但尚未证明我方索引在三个语料上稳定超过 ART/HOT。**
在三次重复的同轮记录中，默认 Context16 的吞吐为 ART 的 84.7%（mixed）、125.3%（skew）、59.5%（tranco）。
因此 mixed 和 tranco 是仍需改进的场景；skew 的本轮读数较好。上述比例是样本描述，不能据此宣布显著胜出。

紧凑外层区域导航 C 在 tranco 的 15 次重复中观察到改善方向：按重复编号对应计算的耗时比中位数为
0.913（Context16）和 0.919（LITS），分别有 12/15、10/15 次低于关闭配置。
**C 仍为默认关闭候选，默认后端仍为 Context16。**

本报告采用新模型接入之前最后一轮完整外部复测 `matrix3`，以及其 tranco 补充轮 `matrix3s`。
不包含后来的 BlockLearn、BlockLinearRank、EqualDepthDir，也不把之后的源码视为本次测试版本。

## 2. 接口、配置与测试装置

入口为评测仓库 `403de52` 的 `scripts/benchmark.py`，通过统一 `Index` 接口调用 `bulk_load` 和 `find`。
各臂使用同一份排序去重后的键值和查询序列；所有唯一键参与构建，按 unsigned byte 字典序处理键。
查询结果与独立参考答案比较，构建、查询计时与数据准备分开。

| 对比配置 | 模型/实现 | 紧凑区域导航 C | 定位 |
|---|---|---|---|
| Context16 | 我方 Context16 | 关闭 | 默认基线 |
| Context16+C | 我方 Context16 | 开启 | 结构候选 |
| LITS | 我方 LitsHpt（部分区域回退 Context16） | 关闭 | 既有模型对照 |
| LITS+C | 同上 | 开启 | 结构候选 |
| ART | armon/libart | — | 外部基线 |
| HOT | speedskater/hot | — | 外部基线 |

我方四臂共用适配器与索引源码，C 的开关在构造点明确设置。固定六个既有开关：
`write_fast_locate`、`reuse_locate_ctx`、`compact_reuse_tag`、`compact_needs_garbage`、
`scan_reserve_result`、`compact_plan_commit`。其他候选依本轮身份记录关闭。
`ov_threshold` 对三个语料统一为 128；skew 项目内调优值为 64，**本报告的 skew 不是该调优配置**。

| 装置项 | 本轮设置 |
|---|---|
| 硬件 | Intel Core Ultra 9 275HX；WSL 可见 24 个逻辑 CPU |
| 系统 | WSL2，Linux 6.6.87.2，glibc 2.39，x86_64 |
| 编译器 | GCC 13.3.0，C++17；优化 `-O3` |
| ISA | 适配器与 runner 均为 `-march=x86-64-v3`；HOT 另有其构建要求的标志 |
| 断言 | runner 为 Release，带 `NDEBUG`；适配目标未定义 `NDEBUG` |
| 并发/绑核 | 单线程 runner；未绑核 |
| 工作负载 | 固定种子 42，均匀选键；每轮 10,000 次点查，9,000 命中、1,000 未命中 |
| 重复 | matrix3 每臂 3 次；matrix3s/tranco 每臂 15 次 |
| 预热与延迟 | 无独立预热；另建实例测每轮 1,000 个确定性分散样本 |
| 臂运行顺序 | 每臂完成全部重复后再运行下一臂；没有跨臂轮转 |

未命中键由源键加确定性 ASCII 后缀及计数构造，并检查确实缺失。
它是一种固定负载，不能代表全部未命中键形态；语料名 skew 也不表示本轮使用热点查询分布。

## 3. 语料与验证

{table(['语料', '唯一键数', '平均键长（字节）', '键长范围（字节）'], dataset_rows)}

三份文件均无重复记录、空键、内嵌 NUL 或高位字节键。tranco 比另外两份语料的键数多一倍，
故不能把跨语料性能差异单独归因于键长。

matrix3 的 **18/18 个语料×配置组合通过**；tranco 补充轮 **6/6 配置通过**。
原始 runner 记录每组合完整语料行数及 10,000 条查询的验证，六臂最终 checksum 在各轮内一致。
该通过状态针对本入口覆盖的静态 `bulk_load`/`find`，不等同于更新、扫描等全部接口的性能或正确性认证。

## 4. 三语料完整同轮结果（matrix3，3 次重复）

### 4.1 点查吞吐

单位：**百万次查询/秒（M ops/s）**；取各臂三次重复的中位数，越大越好。

{wide(rounds, 'median_throughput_ops_per_second', 1e6, 3)}

默认/既有后端相对两个外部基线的吞吐比（我方/外部；>1 表示本轮我方读数较高）：

{table(['语料', 'Context16/ART', 'LITS/ART', 'Context16/HOT', 'LITS/HOT'], ratios)}

mixed 的 ART 样本吞吐最高；skew 的最高读数为 LITS+C；tranco 的 ART 样本吞吐最高。
默认 Context16 在 tranco 的吞吐约为 ART 的 59.5%，即本轮低约 40.5%。
三次重复不足以区分小幅收益或确认这些名次在不同机器状态下稳定。

### 4.2 点查总耗时

单位：**ms / 10,000 次查询**；中位数，越小越好。这是吞吐计时区间的总耗时，不是单次查询 P50。

{wide(rounds, 'median_lookup_ns', 1e6, 3)}

### 4.3 索引构建耗时

单位：**ms**；中位数。包含工厂构造与 `bulk_load`，不含 C++ 编译、文件读取、排序去重和查询生成。

{wide(rounds, 'median_build_ns', 1e6, 2)}

### 4.4 查询延迟分位数

每格为 **P50 / P99，单位 ns**。另建实例测样，合并三轮样本后计算；包含计时器开销。

{table(['语料'] + LABELS, latency_rows)}

吞吐与延迟来自不同实例、不同执行序列，汇总方法也不同。
P50/P99 不约束完整均值，不能用其倒数推导吞吐，或要求两类指标给出相同名次。

### 4.5 常驻空间

单位：**MiB（1 MiB = 1,048,576 B）**；仅索引自有内存 `index_owned_bytes`。

{table(['语料'] + LABELS, memory_rows)}

我方另报适配对象 72 B，保留输入为 0；构建转换缓冲在 `bulk_load` 后释放。
ART/HOT 没有完整索引字节账目，其适配器虽报告部分自有内存，**仍无法计算可比较的总常驻空间**。
本表不用于三方空间排名，也不表示 ART/HOT 占用为零。

## 5. tranco 补充记录（matrix3s，15 次重复）

此轮仅将重复数由 3 改为 15，数据与查询哈希和 matrix3/tranco 相同；机器状态与前一轮不同。
以下所有臂均来自该补充轮，不与上一节跨轮拼接。

{table(['配置', '点查中位 ms / 10k', '吞吐中位 M ops/s', '构建中位 ms'], strong_rows)}

C 的效应按相同重复编号对应：先求每个编号的开启/关闭耗时比，再取比值中位数。
这与“两个耗时中位数相除”不同。

{table(['后端', 'C 开启/关闭耗时比中位', '开启较快次数', '逐编号比值范围'], pair_rows)}

对应编号使用相同数据与查询，但两臂是**分别运行的 runner 进程**，并非时间上紧邻的严格配对。
增加重复未补上绑核、跨臂轮转或多独立进程块；因此可报告“观察到约 8–9% 的耗时改善方向”，
不能把它写成已确认的稳定加速比，也不能拿它取代项目内控制更严格的 A/B。
ART 在本补充轮的点查中位耗时仍低于我方四个配置。

## 6. 可比性与结论边界

- **比较的是统一外部接口总成本。** 我方/ART 的 `find` 接受指针+长度；HOT 需要 NUL 结尾，
  适配器每次查询复制到复用缓冲并补终止符。这份成本保留在计时中，结果不能当作扣除适配成本的库内部速度。
- **LITS 是混合后端。** 既有诊断记录约 17%–18% 区域回退到 Context16；不是纯 LITS 模型效果证明。
  此处只报告既有对照配置。
- **构建、点查、空间须分别评价。** 更快构建不自动证明更快查询；部分内存账目不能支撑完整空间优势。
- **本轮仅测静态点查。** 没有覆盖扫描、插入/更新/删除、更新后读、混合负载、并发和持久化，
  命中率与访问分布也只测了本报告的一档。
- **历史结果不代表当前源码。** 后续接入新模型及其他结构改动后的表现需要对应版本的新实验。

现阶段可对外陈述：统一接口接入与本轮静态验证完成；我方默认配置在 mixed/tranco 的读数仍落后 ART；
紧凑导航在 tranco 观察到正向效应并保留为场景候选。**尚无证据支持“全面超过 ART/HOT”或将 C 统一设为默认。**

## 7. 身份、原始证据与复核

| 项 | 本轮身份 |
|---|---|
| 外部评测入口 | 仓库 commit `403de52` |
| 我方 index.hpp | SHA-256 `{IDENTITY}` |
| 我方 switches.hpp | SHA-256 `e75c7fad79a1944af4b1ecc48761b8d8180568aa473d7f0a0f0872ef16d0880f` |
| ART | commit `301046804af165269e37da6725f5a4aec9ecc881`；适配器 v2 |
| HOT | commit `96bf6fb7103b27e50e16a6026db8974c090ee84a`；适配器 v2 |

源码身份采用当时 `report.json` 的适配器声明，入口不递归哈希外部源码树；
原始报告还保留实际编译器、runner 二进制哈希与适配 manifest 哈希。
本报告未重跑实验，数字由汇总和逐轮样本重新核对；18 个普通轮及 6 个补充轮配置的中位耗时、
构建和吞吐均与原始样本一致，并核对每轮六臂 checksum 一致。

原始证据（相对于本报告目录）：

- [mixed 原始报告](results/matrix3_mixed/report.json)、[逐轮样本](results/matrix3_mixed/samples.csv)。
- [skew 原始报告](results/matrix3_skew/report.json)、[逐轮样本](results/matrix3_skew/samples.csv)。
- [tranco 原始报告](results/matrix3_tranco/report.json)、[逐轮样本](results/matrix3_tranco/samples.csv)。
- [tranco 15 次重复报告](results/matrix3s_tranco/report.json)、[逐轮样本](results/matrix3s_tranco/samples.csv)。
- [历史复测分析](RESULTS_MATRIX3_20261010.md)、[接口与适配说明](adapters/BASELINE_ADAPTER_NOTES.md)。
- [本报告取证记录](EXTERNAL_INTERFACE_BASELINE_PRE_MODEL_20261010.evidence.json)：来源文件哈希、数据/查询身份与重新计算的比值。

10 月 8 日 `matrix2` 存在 runner ISA 未完全统一等已记录问题，本报告不将其作为主表或与本轮拼接。
报告的环境信息取当时记录及已核实的硬件说明。复测需恢复以上历史源码/适配版本，选择新输出目录；
直接运行当前工作树不会自动复现该历史版本。分享本 Markdown 可直接阅读正文，审计时请同时提供链接所指的原始证据。
'''
    source_names = ['matrix3_' + c for c in CORPORA] + ['matrix3s_tranco']
    sources = {}
    for name in source_names:
        for file in ['report.json', 'summary.csv', 'samples.csv']:
            p = ROOT / 'results' / name / file
            sources[p.relative_to(ROOT).as_posix()] = hashlib.sha256(p.read_bytes()).hexdigest()
    evidence = {
        'scope': 'frozen pre-new-model external static point lookup',
        'index_hpp_sha256_declared_in_historical_reports': IDENTITY,
        'source_files_sha256': sources,
        'rounds': {name: {'dataset': read_round(name)[0]['dataset'],
                         'workload': read_round(name)[0]['workload']} for name in source_names},
        'routing_ratios_by_corresponding_repeat_number': paired_evidence,
        'verification': '24 arm summaries recomputed from throughput samples; statuses and per-round checksums agree',
    }
    OUT.write_text(text, encoding='utf-8')
    EVIDENCE.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Written: {OUT}\nWritten: {EVIDENCE}')


if __name__ == '__main__':
    main()
