import json, io
rows = {}
for kind in ("mixed","skew","tranco"):
    for line in io.open("results/main/depth_read_path_%s.jsonl" % kind, encoding="utf-8"):
        d = json.loads(line)
        rows[(kind, d["arm"], d["phase"], d["stratum"])] = d

def q(d, f): return d[f]/d["queries"] if d["queries"] else 0.0

print("== static hits: per-query segment table ==")
hdr = ("corpus arm    | regB  modB  fpF   fpT   dirP  subb  simdB tagC  exB   ovB   ovE")
print(hdr)
for kind in ("mixed","skew","tranco"):
    for arm in ("C16","DDirT"):
        d = rows[(kind,arm,"static","hit")]
        print("%-6s %-6s | %5.1f %5.1f %5.1f %5.1f %5.2f %5.2f %5.2f %5.2f %5.1f %5.1f %5.2f" % (
            kind, arm, q(d,"region_bytes"), q(d,"model_bytes"), q(d,"fp_fused"), q(d,"fp_tail"),
            q(d,"dir_probes"), q(d,"subblocks"), q(d,"simd_batches"), q(d,"tag_candidates"),
            q(d,"exact_bytes"), q(d,"ov_bytes"), q(d,"ov_entered")))
print()
print("== reconciliation (all rows) ==")
bad = 0
for k,d in rows.items():
    if d["cmp_calls_total"] != d["exact_calls"]+d["ov_calls"]: bad+=1; print("cmp split mismatch",k)
    if d["mismatches"] or d["oracle_violations"] or d["mode_violations"] or d["total_violations"] or d["trace_violations"] or d["group_violations"]: bad+=1; print("assertion violation",k)
print("row-level assertion violations:", bad)
for kind in ("mixed","skew","tranco"):
    a = rows[(kind,"C16","static","hit")]; b = rows[(kind,"DDirT","static","hit")]
    print("%-6s fp_total C16=%d DDirT=%d equal=%s ; dir_compared==model_bytes(DirT)=%s ; C16 fused==steps=%s" % (
        kind, a["fp_total"], b["fp_total"], a["fp_total"]==b["fp_total"],
        b["dir_compared"]==b["model_bytes"], a["fused_ne_steps"]==0))
print()
print("== static hits: naive logical-compare-byte totals (region+model+exact+ov) ==")
for kind in ("mixed","skew","tranco"):
    for arm in ("C16","DDirT"):
        d = rows[(kind,arm,"static","hit")]
        tot = d["region_bytes"]+d["model_bytes"]+d["exact_bytes"]+d["ov_bytes"]
        print("%-6s %-6s totalB/q=%6.2f  (region %5.2f + model %5.2f + exact %5.2f + ov %5.2f)" % (
            kind, arm, q(d,"region_bytes")+q(d,"model_bytes")+q(d,"exact_bytes")+q(d,"ov_bytes"),
            q(d,"region_bytes"), q(d,"model_bytes"), q(d,"exact_bytes"), q(d,"ov_bytes")))
