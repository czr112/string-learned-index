#!/usr/bin/env python3
"""Generate the E10 diagnosis tables from a single run.

The counters are not commensurable -- a SIMD tag batch, a string compare and a
sub-block visit do not cost the same -- so they are reported side by side. Nothing
here is summed into a single score, and nothing here is a timing.
"""

import json
from pathlib import Path

DIR = Path(__file__).resolve().parent
SUMMARY = DIR / "results" / "main" / "e10_summary.jsonl"
KINDS = ["mixed", "skew", "tranco"]
LABEL = {"mixed": "混合字符串", "skew": "重复前缀", "tranco": "百万域名"}

LAYOUTS = [("D_demand_single", "D"), ("M_multiblock", "M"), ("S_multiblock_strict", "S")]
COHORTS = [("primary_hit", "主块命中"), ("overflow_hit", "溢出命中"), ("miss", "未命中")]

COLS = [("subblocks_visited", "子块访问"),
        ("simd_batches", "SIMD批次"),
        ("tag_candidates", "tag候选"),
        ("str_cmps_primary", "主块比较"),
        ("str_cmps_overflow", "溢出比较"),
        ("overflow_entered", "溢出进入"),
        ("overflow_probes", "溢出探测"),
        ("logical_cmp_bytes", "逻辑比较字节")]


def load():
    runs = [json.loads(l) for l in SUMMARY.read_text(encoding="utf-8").splitlines() if l.strip()]
    kinds = [r["kind"] for r in runs]

    if kinds != KINDS:
        raise SystemExit(f"expected exactly {KINDS} from one run, found {kinds}")

    return runs


def fmt(v):
    return f"{v:.3f}" if isinstance(v, float) else str(v)


def table_overall(runs):
    out = ["### 总体（每查询平均；同一条查询序列，逐条核对 found/value）", ""]
    head = "| 数据 | 布局 | " + " | ".join(t for _, t in COLS) + " |"
    out += [head, "| --- | --- | " + " | ".join(["---:"] * len(COLS)) + " |"]

    for r in runs:
        for key, name in LAYOUTS:
            v = r["overall"][key]
            out.append(f"| {LABEL[r['kind']]} | {name} | "
                       + " | ".join(fmt(v[c]) for c, _ in COLS) + " |")

    out += ["", "尾部（子块访问与溢出探测）：", ""]
    out += ["| 数据 | 布局 | 子块访问 P95 | 子块访问 最大 | 溢出探测 P95 | 溢出探测 最大 |",
            "| --- | --- | ---: | ---: | ---: | ---: |"]

    for r in runs:
        for key, name in LAYOUTS:
            v = r["overall"][key]
            out.append(f"| {LABEL[r['kind']]} | {name} | {v['subblocks_visited_p95']} "
                       f"| {v['subblocks_visited_max']} | {v['overflow_probes_p95']} "
                       f"| {v['overflow_probes_max']} |")

    out += ["",
            "**「逻辑比较字节」是 memcmp 首次相异即停所走过的字节数，不是内存流量或缓存行读取量。**", ""]

    return out


def table_cohort(runs):
    out = ["### 固定队列：按查询在 D 中的来源分组，再看同一批查询在 M／S 中的成本", "",
            "这一张才是关键表。各布局各自的「主块命中平均成本」比较的可能不是同一批键——",
            "同一个键在 D 中可能命中溢出区，在 M／S 中已经进入主块。", ""]

    for r in runs:
        for ckey, cname in COHORTS:
            cg = r["cohort_by_D_source"][ckey]
            out += [f"**{LABEL[r['kind']]} · D 中{cname}的 {cg['count']:,} 条查询**", ""]
            out += ["| 布局 | 子块访问 | SIMD批次 | tag候选 | 主块比较 | 溢出比较 | 溢出探测 | 逻辑比较字节 |",
                    "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
            for key, name in LAYOUTS:
                v = cg[key]
                out.append(f"| {name} | "
                           + " | ".join(fmt(v[c]) for c, _ in COLS if c != "overflow_entered") + " |")
            out += [""]

    return out


def table_source_shift(runs):
    out = ["### 命中来源迁移（布局改变的直接结果）", ""]
    out += ["| 数据 | 布局 | 主块命中 | 溢出命中 | 未命中 | 溢出命中占比 |",
            "| --- | --- | ---: | ---: | ---: | ---: |"]

    for r in runs:
        for key, name in LAYOUTS:
            b = r["by_own_source"][key]
            p, o, m = b["primary_hit"]["count"], b["overflow_hit"]["count"], b["miss"]["count"]
            out.append(f"| {LABEL[r['kind']]} | {name} | {p:,} | {o:,} | {m:,} "
                       f"| {100.0 * o / (p + o):.2f}% |")

    out += ["",
            "**跨布局不要求命中来源相同**——这正是布局改变的结果，也是本轮要观察的对象。", ""]

    return out


def main():
    runs = load()

    lines = ["# E10 表格（由 analyze_e10.py 从单次运行生成）", ""]
    lines += table_overall(runs)
    lines += table_cohort(runs)
    lines += table_source_shift(runs)

    text = "\n".join(lines)
    (DIR / "E10_TABLES.md").write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
