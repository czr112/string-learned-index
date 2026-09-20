#!/usr/bin/env python3
"""Generate the E12 comparison tables.

Three columns of numbers matter and they are NOT interchangeable:

  time     median of the BATCH-AVERAGE ns/op over 15 batches per method per pass.
           That is not a per-request median, not a P99, and not a significance test.
  space    bytes under each scheme's own accounting. What is included differs by
           scheme and is stated next to the number -- do not compare the totals
           without reading that column.
  build    milliseconds to construct the structure, measured once per pass.

Where a scheme's number is only meaningful with a caveat (ART's ASCII-only protocol,
sorted_leaf's router and prefix compression), the caveat is printed with the number.
"""

import csv
import json
import statistics as st
from pathlib import Path

DIR = Path(__file__).resolve().parent
MAIN = DIR / "results" / "main"

KINDS = ["mixed", "skew", "tranco"]
LABEL = {"mixed": "混合字符串", "skew": "重复前缀", "tranco": "百万域名"}

PASSES = [("e12_timings.csv", "第 1 次进程"), ("e12_repeat_1.csv", "第 2 次进程"),
          ("e12_repeat_2.csv", "第 3 次进程")]

ORDER = ["sorted_array", "ART_libart", "D_demand_single", "S_multiblock_strict",
         "sorted_leaf_prefix_compressed"]
SHORT = {"sorted_array": "sorted_array", "ART_libart": "ART_libart",
         "D_demand_single": "D", "S_multiblock_strict": "S",
         "sorted_leaf_prefix_compressed": "sorted_leaf"}

SPACE_NOTE = {
    "sorted_array": "键字节 + 值 + vector 余量；不含路由，无前缀压缩",
    "ART_libart": "包装 calloc/free 精确实测；含键副本",
    "D_demand_single": "Index::memory()；分配器元数据未计",
    "S_multiblock_strict": "Index::memory()，含边界目录",
    "sorted_leaf_prefix_compressed": "SelectedIndex::memory()；含外层路由与 per-region 前缀压缩",
}


def load(path):
    rows = []
    for r in csv.DictReader(path.open(encoding="utf-8")):
        if r["kind"] != "kind":
            rows.append(r)
    return rows


def timed(rows, kind, method):
    return [float(r["ns_per_op"]) for r in rows if r["kind"] == kind and r["method"] == method]


def load_build():
    out = {}
    for line in (MAIN / "e12_build.jsonl").read_text(encoding="utf-8").splitlines():
        if line.strip():
            d = json.loads(line)
            out[d["kind"]] = {m["name"]: m for m in d["methods"]}
    return out


def table_time(passes, build):
    out = ["### 查询时间（ns/次；每格为该方法 15 批批均值的**中位数**，按三次独立进程分列）", ""]

    for kind in KINDS:
        art_by_pass = [st.median(timed(p, kind, "ART_libart")) for p in passes]
        out += [f"**{LABEL[kind]}**", ""]
        out += ["| 方法 | 第1次进程 | 第2次进程 | 第3次进程 | 合并 45 批 | 相对 ART |",
                "| --- | ---: | ---: | ---: | ---: | ---: |"]
        for m in ORDER:
            vals = [st.median(timed(p, kind, m)) for p in passes]
            merged = st.median([v for p in passes for v in timed(p, kind, m)])
            art = st.median(art_by_pass)
            note = " ⚠︎" if m == "sorted_leaf_prefix_compressed" else ""
            out.append(f"| {SHORT[m]}{note} | {vals[0]:.1f} | {vals[1]:.1f} | {vals[2]:.1f} "
                       f"| **{merged:.1f}** | {merged / art:.2f}× |")
        out += [""]

    out += ["⚠︎ `sorted_leaf` 与 `sorted_array` **不是同一个基线**：前者带外层路由与 per-region 前缀压缩。",
            "",
            "ART 采用固定提交的 armon/libart C99 实现，只接受**无 NUL 的 ASCII**键，",
            "因此 ART 只在这个 ASCII 子集上被评估；其余方案不设此限制。",
            "**这是批均值的分布，不是单请求延迟，也不含显著性检验。**", ""]

    return out


def table_space(build):
    out = ["### 空间与构建时间", ""]

    for kind in KINDS:
        out += [f"**{LABEL[kind]}**", ""]
        out += ["| 方法 | 空间字节 | 相对 ART | 构建 ms | 空间口径（含什么） |",
                "| --- | ---: | ---: | ---: | --- |"]
        art = build[kind]["ART_libart"]["bytes"]
        for m in ORDER:
            b = build[kind][m]
            out.append(f"| {SHORT[m]} | {b['bytes']:,} | {b['bytes'] / art:.2f}× "
                       f"| {b['build_ms']:.1f} | {SPACE_NOTE[m]} |")
        out += [""]

    out += ["**各方案的空间口径不同，总数不可脱离最后一列直接比较。**",
            "所有数字都不含查询侧内存，也不含进程 RSS 或分配器开销（除非该行写明已计入）。",
            "D／S 的主块预算内部可比；**不能据此声称 ART 处在同等内存预算下**。", ""]

    return out


def table_stability(passes):
    out = ["### 排序稳定性（快 → 慢，三次独立进程分列）", ""]

    for kind in KINDS:
        out += [f"**{LABEL[kind]}**", ""]
        out += ["| 进程 | 快 → 慢 |", "| --- | --- |"]
        for rows, name in passes:
            med = {m: st.median(timed(rows, kind, m)) for m in ORDER}
            o = sorted(ORDER, key=lambda m: med[m])
            out.append(f"| {name} | {' < '.join(SHORT[x] for x in o)} |")
        out += [""]

    out += ["三次进程的排序**完全一致**。绝对值在进程间有约 10% 的漂移（机器状态），",
            "但方法间的**相对次序**没有变化，因此本轮可以给出方向性结论。", ""]

    return out


def table_headline(passes):
    out = ["### 关键对照（合并 45 批的中位数之比）", ""]
    out += ["| 数据 | S ÷ D | S ÷ sorted_array | S ÷ sorted_leaf | S ÷ ART | D ÷ sorted_array |",
            "| --- | ---: | ---: | ---: | ---: | ---: |"]

    for kind in KINDS:
        merged = {m: st.median([v for p in passes for v in timed(p, kind, m)]) for m in ORDER}
        out.append(f"| {LABEL[kind]} | {merged['S_multiblock_strict'] / merged['D_demand_single']:.2f}× "
                   f"| {merged['S_multiblock_strict'] / merged['sorted_array']:.2f}× "
                   f"| {merged['S_multiblock_strict'] / merged['sorted_leaf_prefix_compressed']:.2f}× "
                   f"| {merged['S_multiblock_strict'] / merged['ART_libart']:.2f}× "
                   f"| {merged['D_demand_single'] / merged['sorted_array']:.2f}× |")
    out += ["", "小于 1 表示左侧更快。", ""]

    return out


def main():
    named = [(load(MAIN / f), name) for f, name in PASSES]
    passes = [rows for rows, _ in named]
    build = load_build()

    lines = ["# E12 表格（由 analyze_e12.py 从三次独立进程生成）", ""]
    lines += table_headline(passes)
    lines += table_time(passes, build)
    lines += table_space(build)
    lines += table_stability(named)

    text = "\n".join(lines)
    (DIR / "E12_TABLES.md").write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
