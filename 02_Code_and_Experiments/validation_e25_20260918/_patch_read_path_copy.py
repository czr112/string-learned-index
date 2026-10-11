import io
p = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\diag_depth_dir_read_path.cpp"
s = io.open(p, encoding="utf-8", newline="").read()
a = ("                    const Result r = s->find(p.key, qmode);\n"
     "                    const auto& c = ::validation::e25::counts();\n"
     "                    const std::size_t reg = s->locate(p.key);\n")
b = ("                    const Result r = s->find(p.key, qmode);\n"
     "                    // Value copy, not a reference: the shadow locate below also ticks the\n"
     "                    // counters, and the query's own navigation is what must be reported.\n"
     "                    const ::validation::e25::Counts c = ::validation::e25::counts();\n"
     "                    const std::size_t reg = s->locate(p.key);\n")
assert s.count(a) == 1, "diag anchor"
s = s.replace(a, b)
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("diag value-copy fix applied")
