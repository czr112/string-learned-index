#!/usr/bin/env python3
"""Generate the E8 report tables from a single run.

Every table below is derived from results/main/e8_summary.jsonl and the matching
results/main/e8_phases_<kind>.csv produced by the same run. Nothing is copied by
hand, so the report can no longer silently mix numbers from different runs.

The timing table reports both statistical readings and, for each, how many seeds
agree in sign — a difference of medians and a median of paired differences are
not the same quantity, and with three seeds the sign count is the only thing
that says whether the direction is consistent at all.
"""

import csv
import json
import statistics as st
from pathlib import Path

DIR = Path(__file__).resolve().parent
MAIN = DIR / "results" / "main"
DIAG = DIR / "results" / "diag"
KINDS = ["mixed", "skew", "tranco"]
LABEL = {"mixed": "混合字符串", "skew": "重复前缀", "tranco": "百万域名"}

VARIANTS = [
    ("V1_fixed_uniform", "V1 固定步长＋统一"),
    ("V2_dir_uniform", "V2 目录＋统一"),
    ("V3_dir_demand", "V3 目录＋需求"),
]


def sgn(x):
    """Signed number with the same minus glyph the report uses."""
    return f"{x:+.1f}".replace("-", "−")


def load_summary():
    text = (MAIN / "e8_summary.jsonl").read_text(encoding="utf-8")
    runs = [json.loads(line) for line in text.splitlines() if line.strip()]

    kinds = [r["kind"] for r in runs]
    if kinds != KINDS:
        raise SystemExit(f"expected exactly {KINDS} from one run, found {kinds}")

    return {r["kind"]: r for r in runs}


def load_phases(kind):
    path = MAIN / f"e8_phases_{kind}.csv"
    by_phase = {}

    for row in csv.DictReader(path.open(encoding="utf-8")):
        by_phase.setdefault(row["phase"], {})[int(row["seed"])] = float(row["ns_per_op"])

    return by_phase


def load_repeats():
    path = DIAG / "repeatability_skew.jsonl"
    if not path.exists():
        return []

    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


# ---------------------------------------------------------------------------
# space
# ---------------------------------------------------------------------------

def table_space(summary):
    out = ["### 空间分解（字节；逐行相加即得合计，不含分配器元数据，非 RSS）", ""]
    out += ["| 数据 | 变体 | 主块总字节 | 模型 | 前缀 | 溢出容器与后缀 | 容量表 "
            "| 区域自身＋栅栏＋索引自身 | 合计 | 对账 |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :-: |"]

    for kind in KINDS:
        for key, name in VARIANTS:
            s = summary[kind][key]["space"]
            outer = s["regions_self"] + s["fences"] + s["self"]
            ok = "✓" if s["reconciles"] else "✗"
            out.append(f"| {LABEL[kind]} | {name} | {s['blocks']:,} | {s['model']:,} | {s['prefix']:,} "
                       f"| {s['overflow_containers']:,} | {s['tables']:,} | {outer:,} "
                       f"| {s['total']:,} | {ok} |")

    out += ["",
            "对账口径：八个分项各自独立累加后与 `Index::memory()` 相加比较，后者走的是逐区域求和的另一条代码路径。"
            "分项在相减之前逐个与合计比较，任何无符号回绕会立刻违反该条件。"
            "**`offset_directory=false` 的 V1 仍然分配并计入容量表**（见上表 V1 的容量表与 V2 相同），"
            "它只切换寻址方式，因此 V1 与 V2 的空间逐字节相同是预期结果。", ""]

    return out


# ---------------------------------------------------------------------------
# timing
# ---------------------------------------------------------------------------

def compare(by_phase, a, b):
    ma = st.median(by_phase[a].values())
    mbv = st.median(by_phase[b].values())
    seeds = sorted(by_phase[a])
    paired = [by_phase[a][s] - by_phase[b][s] for s in seeds]
    negative = sum(1 for d in paired if d < 0)

    return ma - mbv, st.median(paired), negative, len(seeds), paired


def table_timing(summary):
    out = ["### 端到端计时（ns/op，同一次运行内比较）", ""]
    out += ["| 数据 | 变体 | 种子 0 | 种子 1 | 种子 2 | 中位数 | 离散度 |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: |"]

    for kind in KINDS:
        by_phase = load_phases(kind)
        for key, name in VARIANTS:
            short = key.split("_")[0]
            v = by_phase[short]
            seeds = [v[s] for s in sorted(v)]
            med = st.median(seeds)
            spread = (max(seeds) - min(seeds)) / med * 100
            cells = " | ".join(f"{x:.1f}" for x in seeds)
            out.append(f"| {LABEL[kind]} | {name} | {cells} | {med:.1f} | {spread:.1f}% |")

    out += ["", "| 数据 | 比较 | 中位数之差 | 配对差的中位数 | 配对方向一致 | 逐种子配对差 |",
            "| --- | --- | ---: | ---: | :-: | --- |"]

    for kind in KINDS:
        by_phase = load_phases(kind)
        for a, b in [("V3", "V2"), ("V2", "V1"), ("V3", "V1")]:
            dm, pm, neg, total, paired = compare(by_phase, a, b)
            agree = neg if dm < 0 else total - neg
            cells = " / ".join(sgn(d) for d in paired)
            out.append(f"| {LABEL[kind]} | {a}−{b} | {sgn(dm)} | {sgn(pm)} | {agree}/{total} | {cells} |")

    out += ["",
            "两个口径不可混用：**中位数之差**是两个变体各自中位数的差，**配对差的中位数**是先逐种子作差再取中位数。"
            "方向一致列统计的是与“中位数之差”符号相同的种子数。"
            "每种子只有一批查询，且三种子同进程、同机器、非独立重复，胜出批数只作方向参考。", ""]

    return out


# ---------------------------------------------------------------------------
# E9 feasibility
# ---------------------------------------------------------------------------

def table_e9(summary):
    out = ["### E9 可行性：理想化容量估算", ""]
    out += ["| 数据 | 配置 48 槽的组 | 其中全占 48 槽 | 封顶组内剩余溢出 | 现预算内可收回 | 占比 |",
            "| --- | ---: | ---: | ---: | ---: | ---: |"]

    for kind in KINDS:
        e = summary[kind]["e9_feasibility"]
        capped = e["overflow_records_in_capped_groups"]
        absorb = e["absorbable_within_current_budget_17B_per_slot"]
        share = absorb / capped * 100 if capped else 0.0
        out.append(f"| {LABEL[kind]} | {e['groups_configured_48slots']:,} "
                   f"| {e['groups_with_all_48_slots_occupied']:,} | {capped:,} | {absorb:,} | {share:.1f}% |")

    out += ["",
            "口径：逐组取溢出后缀长度升序，在该组自身空闲后缀字节内贪心装箱，每条记录预留 17 B"
            "（新槽的 Entry 16 B ＋ 指纹 1 B）。**未计入**多块组的块头、64 B 对齐与子块定位元数据，"
            "因此上表是当前简化假设下的可容纳比例，不是多块组已经能够兑现的回收率。", ""]

    return out


# ---------------------------------------------------------------------------
# repeatability
# ---------------------------------------------------------------------------

def skew_medians(run):
    v1, v2, v3 = (run[k]["median_ns"] for k in
                  ("V1_fixed_uniform", "V2_dir_uniform", "V3_dir_demand"))
    return v1, v2, v3


def skew_from_jsonl(path):
    if not path.exists():
        return None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip() and json.loads(line)["kind"] == "skew":
            return skew_medians(json.loads(line))
    return None


def skew_from_phases(path):
    if not path.exists():
        return None
    phases = {}
    for row in csv.DictReader(path.open(encoding="utf-8")):
        phases.setdefault(row["phase"], []).append(float(row["ns_per_op"]))
    return (st.median(phases["V1"]), st.median(phases["V2"]), st.median(phases["V3"]))


def table_repeat(summary):
    # Platform and protocol are properties of the archived runs, not derivable
    # from the numbers, so they are stated here; the numbers themselves are read.
    archive = [
        ("归档 v3（2026-09-13）", "Windows + g++ 15.2", "无预热",
         skew_from_jsonl(MAIN.parent / "archive_v3_20260913" / "e8_summary.jsonl")),
        ("归档 Windows 重跑（2026-09-14）", "Windows + g++ 15.2", "无预热",
         skew_from_jsonl(MAIN.parent / "archive_win_20260914" / "e8_summary.jsonl")),
        ("归档 WSL 第一次（2026-09-14）", "WSL2 + g++ 13.3", "有预热",
         skew_from_phases(MAIN.parent / "archive_wsl_run1_20260914" / "e8_phases_transcribed.csv")),
    ]

    live = [("正式运行", "WSL2 + g++ 13.3", "有预热", skew_medians(summary["skew"]))]
    for i, run in enumerate(load_repeats(), 1):
        live.append((f"重复 {i}", "WSL2 + g++ 13.3", "有预热", skew_medians(run)))

    out = ["### 重复性：各次 skew 观测（仅列出，不合并统计）", ""]
    out += ["| 运行 | 平台 | 协议 | V1 中位数 | V2 中位数 | V3 中位数 | V3−V2 |",
            "| --- | --- | --- | ---: | ---: | ---: | ---: |"]

    for name, platform, protocol, med in archive + live:
        if med is None:
            out.append(f"| {name} | {platform} | {protocol} | — | — | — | （数据不可用） |")
            continue
        v1, v2, v3 = med
        out.append(f"| {name} | {platform} | {protocol} | {v1:.1f} | {v2:.1f} | {v3:.1f} | {sgn(v3 - v2)} |")

    out += ["",
            "归档观测包含不同平台和协议版本，**不合并计算胜率**。各次结果存在方向及幅度变化；"
            "当前预热协议下的独立重复证据仍不足，尚不能给出稳定数值。"
            "本表仅供追溯，不作胜率或显著性计算。", ""]

    return out


# ---------------------------------------------------------------------------

def main():
    summary = load_summary()
    lines = ["# E8 表格（由 analyze_e8.py 从单次运行生成）", ""]
    lines += table_space(summary)
    lines += table_timing(summary)
    lines += table_e9(summary)
    lines += table_repeat(summary)

    text = "\n".join(lines)
    (DIR / "E8_TABLES.md").write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
