#!/usr/bin/env python3
"""Generate the E13 comparison tables.

The round's question is narrow: does sub-block range location pay off end to end,
and how much of the gap to ART does it close? S and S_range are the SAME Index
object -- same layout, same directory, same space, same build time -- and differ
only in Mode::locate. So the S vs S_range columns are a clean one-variable A/B, and
their space/build numbers are identical by construction, not by coincidence.

Queries are reported in three classes. An aggregate can hide a shift between the
classes, and range location is exactly the kind of change that could trade a miss
saving for a hit cost.
"""

import csv
import json
import statistics as st
from pathlib import Path

DIR = Path(__file__).resolve().parent
MAIN = DIR / "results" / "main"

KINDS = ["mixed", "skew", "tranco"]
LABEL = {"mixed": "混合字符串", "skew": "重复前缀", "tranco": "百万域名"}

PASSES = [("e13_timings.csv", "第 1 次进程"), ("e13_repeat_1.csv", "第 2 次进程"),
          ("e13_repeat_2.csv", "第 3 次进程")]

ORDER = ["sorted_array", "ART_libart", "D_demand_single", "S_multiblock_strict",
         "S_range_locate", "sorted_leaf_prefix_compressed"]
SHORT = {"sorted_array": "sorted_array", "ART_libart": "ART_libart",
         "D_demand_single": "D", "S_multiblock_strict": "S",
         "S_range_locate": "S_range", "sorted_leaf_prefix_compressed": "sorted_leaf"}

CLASSES = ["all", "hit", "miss"]
CLS_LABEL = {"all": "全部查询", "hit": "仅命中", "miss": "仅未命中"}


def load(path):
    rows = []
    for r in csv.DictReader(path.open(encoding="utf-8")):
        if r["kind"] != "kind":
            rows.append(r)
    return rows


def vals(rows, kind, cls, method):
    return [float(r["ns_per_op"]) for r in rows
            if r["kind"] == kind and r["class"] == cls and r["method"] == method]


def merged(passes, kind, cls, method):
    return st.median([v for p in passes for v in vals(p, kind, cls, method)])


def load_build():
    out = {}
    for line in (MAIN / "e13_build.jsonl").read_text(encoding="utf-8").splitlines():
        if line.strip():
            d = json.loads(line)
            out[d["kind"]] = d
    return out


def load_diag():
    out = {}
    for line in (MAIN / "e13_diag.jsonl").read_text(encoding="utf-8").splitlines():
        if line.strip():
            d = json.loads(line)
            out[d["kind"]] = d
    return out


def table_headline(passes):
    out = ["### 本轮的两个问题", ""]
    out += ["| 数据 | 类别 | S | S_range | **S_range ÷ S** | ART | **S_range ÷ ART** | S ÷ ART |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |"]

    for kind in KINDS:
        for cls in CLASSES:
            s = merged(passes, kind, cls, "S_multiblock_strict")
            sr = merged(passes, kind, cls, "S_range_locate")
            art = merged(passes, kind, cls, "ART_libart")
            out.append(f"| {LABEL[kind]} | {CLS_LABEL[cls]} | {s:.1f} | {sr:.1f} "
                       f"| **{sr / s:.3f}×** | {art:.1f} | **{sr / art:.3f}×** | {s / art:.3f}× |")

    out += ["",
            "< 1 表示左侧更快。S 与 S_range 是**同一个 Index 对象**，只有 `Mode::locate` 不同，",
            "因此这两列是单变量对照；两者的空间与构建时间**按构造完全相同**。", ""]

    return out


def table_full(passes):
    out = ["### 完整时间表（ns/次；每格为 15 批批均值的**中位数**，合并三次独立进程共 45 批）", ""]

    for kind in KINDS:
        for cls in CLASSES:
            art = merged(passes, kind, cls, "ART_libart")
            out += [f"**{LABEL[kind]} · {CLS_LABEL[cls]}**", ""]
            out += ["| 方法 | 中位数 ns | 相对 ART |", "| --- | ---: | ---: |"]
            for m in ORDER:
                v = merged(passes, kind, cls, m)
                note = " ⚠︎" if m == "sorted_leaf_prefix_compressed" else ""
                out.append(f"| {SHORT[m]}{note} | {v:.1f} | {v / art:.2f}× |")
            out += [""]

    out += ["⚠︎ `sorted_leaf` 带外层路由与 per-region 前缀压缩，与 `sorted_array` 不是同一个基线。",
            "ART 只接受无 NUL 的 ASCII 键，因此只在 ASCII 子集上被评估。",
            "**这是批均值的分布，不是单请求延迟，也不含显著性检验。**", ""]

    return out


def table_space(build):
    out = ["### 空间与构建时间", ""]
    out += ["| 数据 | 方法 | 空间字节 | 相对 ART | 构建 ms |", "| --- | --- | ---: | ---: | ---: |"]

    for kind in KINDS:
        by = {m["name"]: m for m in build[kind]["methods"]}
        art = by["ART_libart"]["bytes"]
        for m in ORDER:
            b = by[m]
            out.append(f"| {LABEL[kind]} | {SHORT[m]} | {b['bytes']:,} | {b['bytes'] / art:.2f}× "
                       f"| {b['build_ms']:.1f} |")

    out += ["",
            "**S 与 S_range 的空间和构建时间逐字相同**——边界目录在构建时对每个多块布局都会生成，",
            "`locate` 只改查找路径。**因此 E12 报告的 S 空间已经包含边界目录**；",
            "相对于「无目录的 S」，E12／E13 的 S 数字是被轻微惩罚的。",
            "", "空间口径同 E12：各方案含什么不同，总数不可脱离口径直接比较。", ""]

    return out


def table_stability(named):
    out = ["### 排序稳定性（快 → 慢，三次独立进程分列）", ""]

    for kind in KINDS:
        for cls in CLASSES:
            out += [f"**{LABEL[kind]} · {CLS_LABEL[cls]}**", ""]
            out += ["| 进程 | 快 → 慢 |", "| --- | --- |"]
            for rows, name in named:
                med = {m: st.median(vals(rows, kind, cls, m)) for m in ORDER}
                o = sorted(ORDER, key=lambda m: med[m])
                out.append(f"| {name} | {' < '.join(SHORT[x] for x in o)} |")
            out += [""]

    return out


def table_diag(diag):
    out = ["### 独立诊断：定位把工作量搬到了哪里（每查询平均）", ""]
    out += ["| 数据 | 模式 | 类别 | 子块访问 | SIMD批次 | 边界比较 | 边界比较字节 | tag候选 | 溢出探测 |",
            "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |"]

    for kind in KINDS:
        bc = diag[kind]["by_class"]
        for mode in ["S_locate_off", "S_range_locate_on"]:
            for cls in ["all", "hit", "miss"]:
                v = bc[mode][cls]
                out.append(f"| {LABEL[kind]} | {mode.replace('_locate_off','').replace('_locate_on','')} "
                           f"| {CLS_LABEL[cls]} | {v['subblocks_visited']:.3f} | {v['simd_batches']:.3f} "
                           f"| {v['locate_probes']:.3f} | {v['locate_cmp_bytes']:.1f} "
                           f"| {v['tag_candidates']:.3f} | {v['overflow_probes']:.3f} |")

    out += ["",
            "计数**不可相加**：一次边界比较、一次 SIMD 标签批次和一次子块访问的时间并不相等。",
            "「边界比较字节」是 memcmp 首次相异即停走过的字节数，**不是内存流量**。",
            "溢出探测在两种模式下**逐字相同**，确认定位没有触碰溢出路径。", ""]

    return out


def main():
    named = [(load(MAIN / f), name) for f, name in PASSES]
    passes = [rows for rows, _ in named]
    build = load_build()
    diag = load_diag()

    lines = ["# E13 表格（由 analyze_e13.py 从三次独立进程生成）", ""]
    lines += table_headline(passes)
    lines += table_diag(diag)
    lines += table_stability(named)
    lines += table_full(passes)
    lines += table_space(build)

    text = "\n".join(lines)
    (DIR / "E13_TABLES.md").write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
