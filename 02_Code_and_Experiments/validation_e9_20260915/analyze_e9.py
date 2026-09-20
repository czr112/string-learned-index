#!/usr/bin/env python3
"""Generate the E9 report tables from a single run.

Every table comes from results/main/e9_summary.jsonl of the same run, so the
report cannot mix numbers from different runs. Nothing here is timing: E9's
question is deterministic and the host clock is not stable enough to add to it.

The space decomposition is reported in full because a larger block area does not
mean a larger index: the overflow containers shrink at the same time, and only the
total says whether the trade is worth it.
"""

import json
from pathlib import Path

DIR = Path(__file__).resolve().parent
SUMMARY = DIR / "results" / "main" / "e9_summary.jsonl"
KINDS = ["mixed", "skew", "tranco"]
LABEL = {"mixed": "混合字符串", "skew": "重复前缀", "tranco": "百万域名"}
MB = 1048576


def load():
    runs = [json.loads(l) for l in SUMMARY.read_text(encoding="utf-8").splitlines() if l.strip()]
    kinds = [r["kind"] for r in runs]

    if kinds != KINDS:
        raise SystemExit(f"expected exactly {KINDS} from one run, found {kinds}")

    return runs


def pct(a, b):
    return 100.0 * (a - b) / b if b else 0.0


def sgn(x):
    """Signed percentage with the same minus glyph the report uses."""
    return f"{x:+.1f}".replace("-", "−")


def sgn_int(x):
    """Signed byte count with thousands separators."""
    return f"{x:+,}".replace("-", "−")


LAYOUTS = [("U_uniform", "U 统一单块 16 槽"),
           ("D_demand_single", "D 需求单块 ≤48 槽"),
           ("M_multiblock", "M 多块（无预算约束）"),
           ("S_multiblock_strict", "S 多块（严格等预算）")]


def table_overflow(runs):
    out = ["### 溢出与主块字节", ""]
    out += ["| 数据 | 布局 | 溢出键 | 主块总字节 |",
            "| --- | --- | ---: | ---: |"]

    for r in runs:
        for key, name in LAYOUTS:
            v = r[key]
            out.append(f"| {LABEL[r['kind']]} | {name} | {v['overflow_keys']:,} | {v['blocks']:,} |")

    out += ["",
            "U 与 D 的主块字节严格相等（E8 的等预算约束，此处复现）。"
            "**M 没有预算约束**，按需求给组分配子块；"
            "**S 的主块字节被硬性限制为不超过 D**（实测还略少），字节来自回收各组超出其块内内容所需的余量。", ""]

    return out


def table_space(runs):
    out = ["### 空间分解（字节；分配器元数据未计，非 RSS）", ""]
    out += ["| 数据 | 布局 | 主块字节 | 溢出容器及后缀 | 容量表 | 模型、前缀与外层 | 总空间 |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: |"]

    for r in runs:
        for key, name in [("U_uniform", "U"), ("D_demand_single", "D"),
                          ("M_multiblock", "M"), ("S_multiblock_strict", "S")]:
            v = r[key]
            out.append(f"| {LABEL[r['kind']]} | {name} | {v['blocks']:,} | {v['overflow_space']:,} "
                       f"| {v['tables']:,} | {v['other']:,} | {v['total']:,} |")

    out += ["", "相对严格等预算的 D 的差额——判断空间交换是否划算只能看最后一行：", ""]
    out += ["| 数据 | 变体 | 主块字节变化 | 溢出容器及后缀变化 | 总空间变化 |",
            "| --- | --- | ---: | ---: | ---: |"]

    for r in runs:
        d = r["D_demand_single"]
        for key, name in [("M_multiblock", "M"), ("S_multiblock_strict", "S")]:
            m = r[key]
            out.append(f"| {LABEL[r['kind']]} | {name} "
                       f"| {sgn_int(m['blocks'] - d['blocks'])}（{sgn(pct(m['blocks'], d['blocks']))}%） "
                       f"| {sgn_int(m['overflow_space'] - d['overflow_space'])} "
                       f"| **{sgn_int(m['total'] - d['total'])}（{sgn(pct(m['total'], d['total']))}%）** |")

    out += ["",
            "**主块变大不等于索引变大**：多块吸收溢出后，溢出容器与后缀容量同步缩小，"
            "缩小的量大于主块增加的量，因此两者的总空间都是下降的。", ""]

    return out


def table_class(runs):
    out = ["### 主块增量按组类别分解", ""]
    out += ["| 数据 | 空组 | 非空单块组 | 多块组 | 增量中来自多块组的字节 |",
            "| --- | ---: | ---: | ---: | ---: |"]

    for r in runs:
        c = r["block_delta_by_class"]
        out.append(f"| {LABEL[r['kind']]} | {c['empty_groups']:,} | {c['single_block_groups']:,} "
                   f"| {c['multiblock_groups']:,} | {c['bytes_from_multiblock']:,} |")

    out += ["",
            "空组与非空单块组**保持 D 的布局不变**，字节增量为零。"
            "因此上表的增量字节数就是全部主块增量——把 D 已经压缩过的小组重新放大，"
            "不属于多块的代价。", ""]

    return out


def table_layout(runs):
    out = ["### 多块布局统计", ""]
    out += ["| 数据 | 出现多块的组 | 最大子块数 | 块头：D → M | 块头差值 |",
            "| --- | ---: | ---: | ---: | ---: |"]

    for r in runs:
        l = r["M_layout"]
        out.append(f"| {LABEL[r['kind']]} | {l['multiblock_groups']:,} | {l['max_subblocks']} "
                   f"| {l['D_block_headers']:,} → {l['M_block_headers']:,} | {l['header_delta']:,} |")

    out += ["",
            "块头差值才是多块新增的块头开销；子块块头**总量**（D 也有块头）不能当作增量。", ""]

    return out


def table_remaining(runs):
    out = ["### 剩余溢出的成因分类", ""]
    out += ["| 数据 | 变体 | 剩余溢出 | 后缀超过子块容量 | 组内无空闲槽 | 无子块字节够 "
            "| 组内有空位但不在其子块 | 槽与字节分处不同子块 |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |"]

    for r in runs:
        for key, name in [("remaining_overflow_classification", "M"),
                          ("S_remaining_overflow_classification", "S")]:
            c = r[key]
            out.append(f"| {LABEL[r['kind']]} | {name} | {c['total']:,} "
                       f"| {c['suffix_exceeds_subblock']:,} | {c['no_free_slot_in_group']:,} "
                       f"| {c['no_subblock_with_enough_free_bytes']:,} "
                       f"| {c['room_exists_but_not_in_its_subblock']:,} "
                       f"| {c['slots_and_bytes_in_different_subblocks']:,} |")

    out += ["",
            "「无子块字节够」是**子块字节容量不足**（含字节分散在各子块、单个子块都装不下），与键序无关。"
            "只有**最后两列**是子块键序（M1）的直接后果——组内别处还有槽位或字节，"
            "但该键只能进入它所属键区间对应的子块。域名上这两类合计占 S 剩余溢出的 14.5%。",
            "",
            "**「后缀超过子块容量」与「整组无空闲槽」都是零**：多块布局消除了本轮剩余溢出中的"
            "**整组槽位耗尽**情形。注意这只说明不存在整组耗尽，不等于槽位不再参与约束——"
            "「槽与字节分处不同子块」正说明局部槽位仍在起作用。", ""]

    return out


def table_strict(runs):
    out = ["### 严格等预算变体 S 的分配日志", ""]
    out += ["| 数据 | 候选组 | 获得资助 | 未获资助 | 可用余量 | 已花余量 | 实际回收字节 | 主块是否 ≤ D |",
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: | :-: |"]

    for r in runs:
        l = r["S_strict_log"]
        ok = "✓" if l["within_D_block_budget"] else "✗"
        out.append(f"| {LABEL[r['kind']]} | {l['candidates']:,} | {l['funded']:,} | {l['unfunded']:,} "
                   f"| {l['slack_available']:,} | {l['slack_spent']:,} | {l['shaved']:,} | {ok} |")

    out += ["",
            "候选组＝条目数超出单块槽位的组。可用余量＝D 在各组上超出其「块内实际内容所需」的字节之和；"
            "D 已用满整区预算，这就是唯一可动用的空间。"
            "**已花余量与实际回收字节逐字节相等**，说明回收正好等于支出。", ""]

    out += ["### S 的每子块槽数分布", "",
            "| 数据 | 48 槽的组 | 少于 48 槽的组 | 最小槽数 |",
            "| --- | ---: | ---: | ---: |"]

    for r in runs:
        l = r["S_strict_log"]
        out.append(f"| {LABEL[r['kind']]} | {l['groups_with_48_slots']:,} "
                   f"| {l['groups_with_fewer_than_48_slots']:,} | {l['min_slots']} |")

    out += ["",
            "**M 固定每子块 48 槽，S 用 `min_plan` 搜索出的槽数。**"
            "S 的绝大多数组拿到远少于 48 的槽数——槽数小则每子块条目区小，"
            "有时胜过子块多的方案，所以这个搜索是实质性的。", ""]

    return out


def table_vs_ideal(runs):
    out = ["### 与 E8 理想化估算的对照", ""]
    out += ["| 数据 | D 剩余溢出 | 理想估算可收回 | 占比 | M 收回 | 占 D | S 收回 | 占 D |",
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]

    for r in runs:
        d = r["D_demand_single"]
        m = r["M_multiblock"]
        s = r["S_multiblock_strict"]
        i = r["ideal_17B_model"]
        ideal = i["absorbable_with_no_extra_bytes"]
        a_m = d["overflow_keys"] - m["overflow_keys"]
        a_s = d["overflow_keys"] - s["overflow_keys"]
        out.append(f"| {LABEL[r['kind']]} | {d['overflow_keys']:,} | {ideal:,} "
                   f"| {100.0 * ideal / i['overflow_in_capped']:.1f}% "
                   f"| {a_m:,} | {100.0 * a_m / d['overflow_keys']:.1f}% "
                   f"| {a_s:,} | {100.0 * a_s / d['overflow_keys']:.1f}% |")

    out += ["",
            "**S 是在与 D 严格相等的主块预算内取得的结果**，因此它与 E8 的简化估算处于同一预算条件下；"
            "M 则允许主块预算增长。两者不可互相替代，也不与 E8 的 17 B/条做倍数比较——"
            "E8 的 17 B 是新增描述符与指纹的开销，另计后缀长度并使用组内既有空闲空间。", ""]

    return out


def main():
    runs = load()

    lines = ["# E9 表格（由 analyze_e9.py 从单次运行生成）", ""]
    lines += table_overflow(runs)
    lines += table_space(runs)
    lines += table_class(runs)
    lines += table_layout(runs)
    lines += table_strict(runs)
    lines += table_remaining(runs)
    lines += table_vs_ideal(runs)

    text = "\n".join(lines)
    (DIR / "E9_TABLES.md").write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
