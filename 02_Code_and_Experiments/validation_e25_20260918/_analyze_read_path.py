import json, io
paths = {k: "results/main/depth_read_path_%s.jsonl" % k for k in ("mixed","skew","tranco")}
rows = {}
for kind, p in paths.items():
    for line in io.open(p, encoding="utf-8"):
        d = json.loads(line)
        rows[(kind, d["arm"], d["phase"], d["stratum"])] = d

def per_query(d, field, denom="queries"):
    return d[field] / d[denom] if d[denom] else 0.0

print("%-6s %-5s %-6s %-4s | %7s %7s %8s %7s %7s %7s %7s %7s %7s" % (
    "corpus","arm","phase","str","q","regP/q","model/q","dirP/q","fpF/q","fpT/q","subb/q","exq","ovq"))
for kind in ("mixed","skew","tranco"):
    for arm in ("C16","DDirT"):
        for phase in ("static","post"):
            for st in ("hit","miss"):
                d = rows.get((kind,arm,phase,st))
                if not d: continue
                print("%-6s %-5s %-6s %-4s | %7d %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %7.3f %7.3f" % (
                    kind, arm, phase, st, d["queries"],
                    per_query(d,"region_probes"), per_query(d,"model_bytes"),
                    per_query(d,"dir_probes"), per_query(d,"fp_fused"), per_query(d,"fp_tail"),
                    per_query(d,"subblocks"), per_query(d,"exact_calls"), per_query(d,"ov_calls")))
