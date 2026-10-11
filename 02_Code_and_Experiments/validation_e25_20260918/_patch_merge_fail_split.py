import io
p = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\index.hpp"
s = io.open(p, encoding="utf-8", newline="").read()

a = ("    std::uint64_t probe_ok = 0, probe_fail = 0;\n"
     "    std::uint64_t seg_rows = 0;           // rows emitted through a proven segment\n")
b = ("    std::uint64_t probe_ok = 0, probe_fail = 0;\n"
     "    std::uint64_t probe_fail_equal = 0;   // failures that were a boundary TIE (c == 0)\n"
     "    std::uint64_t seg_rows = 0;           // rows emitted through a proven segment\n")
assert s.count(a) == 1, "field"
s = s.replace(a, b)

a = ("        probe_ok += o.probe_ok; probe_fail += o.probe_fail; seg_rows += o.seg_rows;\n")
b = ("        probe_ok += o.probe_ok; probe_fail += o.probe_fail;\n"
     "        probe_fail_equal += o.probe_fail_equal; seg_rows += o.seg_rows;\n")
assert s.count(a) == 1, "add()"
s = s.replace(a, b)

a = ("                        m.probe_fail += 1;\n"
     "                        m.probe_fail_bytes += bytes;\n")
b = ("                        m.probe_fail += 1;\n"
     "                        if (c == 0) m.probe_fail_equal += 1;\n"
     "                        m.probe_fail_bytes += bytes;\n")
assert s.count(a) == 1, "count"
s = s.replace(a, b)
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("shadow: probe_fail_equal added")

p = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\diag_merge_entry.cpp"
s = io.open(p, encoding="utf-8", newline="").read()
a = ("                    << \",\\\"probes\\\":\" << sc.probes << \",\\\"probe_ok\\\":\" << sc.probe_ok\n"
     "                    << \",\\\"probe_fail\\\":\" << sc.probe_fail << \",\\\"probe_bytes\\\":\" << sc.probe_bytes\n")
b = ("                    << \",\\\"probes\\\":\" << sc.probes << \",\\\"probe_ok\\\":\" << sc.probe_ok\n"
     "                    << \",\\\"probe_fail\\\":\" << sc.probe_fail\n"
     "                    << \",\\\"probe_fail_equal\\\":\" << sc.probe_fail_equal\n"
     "                    << \",\\\"probe_bytes\\\":\" << sc.probe_bytes\n")
assert s.count(a) == 1, "call row"
s = s.replace(a, b)

a = ("                << \",\\\"probes\\\":\" << probes_t << \",\\\"probe_ok\\\":\" << ok_t\n"
     "                << \",\\\"probe_fail\\\":\" << fail_t\n")
b = ("                << \",\\\"probes\\\":\" << probes_t << \",\\\"probe_ok\\\":\" << ok_t\n"
     "                << \",\\\"probe_fail\\\":\" << fail_t << \",\\\"probe_fail_equal\\\":\" << fail_eq_t\n")
assert s.count(a) == 1, "checkpoint row"
s = s.replace(a, b)

a = ("            std::uint64_t rows_t = 0, seg_t = 0, probes_t = 0, ok_t = 0, fail_t = 0;\n")
b = ("            std::uint64_t rows_t = 0, seg_t = 0, probes_t = 0, ok_t = 0, fail_t = 0;\n"
     "            std::uint64_t fail_eq_t = 0;\n")
assert s.count(a) == 1, "accum"
s = s.replace(a, b)

a = ("                fail_t += sc.probe_fail;\n")
b = ("                fail_t += sc.probe_fail;\n"
     "                fail_eq_t += sc.probe_fail_equal;\n")
assert s.count(a) == 1, "accum2"
s = s.replace(a, b)

a = ("                      << \" probes ok/fail=\" << ok_t << \"/\" << fail_t\n")
b = ("                      << \" probes ok/fail(eq)=\" << ok_t << \"/\" << fail_t << \"(\" << fail_eq_t << \")\"\n")
assert s.count(a) == 1, "summary"
s = s.replace(a, b)
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("device: equal split recorded")
