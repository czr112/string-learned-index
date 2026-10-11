import io
p = "bench_depth_dir.cpp"
t = io.open(p, encoding="utf-8", newline="").read()
old = "                const int arm = order[oi];\n"
new = ("                const int arm = order[oi];\n"
       "\n"
       "                // The directory counters are PROCESS-WIDE statics; reset them before this\n"
       "                // arm's build so the two reported fields describe THIS arm's fits. Without\n"
       "                // this a second arm in the same process reports the SUM of both arms.\n"
       "                Region::reset_depth_dir_stats();\n")
assert t.count(old) == 1, t.count(old)
io.open(p, "w", encoding="utf-8", newline="").write(t.replace(old, new))

p = "run_depth_dir_ab.py"
t = io.open(p, encoding="utf-8", newline="").read()
old = ("            if mb:\n"
       "                entry[\"model_bytes_%s\" % arm] = statistics.median(mb)\n")
new = ("            if mb:\n"
       "                entry[\"model_bytes_%s\" % arm] = statistics.median(mb)\n"
       "            db = [r.get(\"dir_threshold_bytes\", 0) for r in recs if r[\"arm\"] == arm]\n"
       "            if db and any(db):\n"
       "                entry[\"dir_separator_bytes_%s\" % arm] = statistics.median(db)\n")
assert t.count(old) == 1, t.count(old)
io.open(p, "w", encoding="utf-8", newline="").write(t.replace(old, new))
print("patched bench + analyzer")
