import io
p = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\diag_merge_entry.cpp"
s = io.open(p, encoding="utf-8", newline="").read()

a = "#include <iostream>\n#include <memory>\n"
b = "#include <iostream>\n#include <map>\n#include <memory>\n"
assert s.count(a) == 1, "map include"
s = s.replace(a, b)

a = ("                const Alloc d = demand_single(gs, budget);\n"
     "                StrictLog log;\n"
     "                std::vector<std::uint16_t> c;\n"
     "                sg.push_back(demand_strict(gs, d, log, c));\n"
     "                bb.push_back(d.sg.empty() ? std::vector<std::uint32_t>() : d.bb);\n"
     "                caps.push_back(std::move(c));\n")
b = ("                const Alloc d = demand_single(gs, budget);\n"
     "                StrictLog log;\n"
     "                std::vector<std::uint16_t> c;\n"
     "                const Alloc strict = demand_strict(gs, d, log, c);\n"
     "                sg.push_back(strict.sg);\n"
     "                bb.push_back(strict.bb);\n"
     "                caps.push_back(std::move(c));\n")
assert s.count(a) == 1, "bb fix"
s = s.replace(a, b)

a = ("        std::ofstream out((\"results/main/\" + out_stem + \".jsonl\").c_str(), std::ios::app);\n")
b = ("        {\n"
     "            std::ifstream existing(\"results/main/\" + out_stem + \".jsonl\");\n"
     "            if (existing.good())\n"
     "                throw std::runtime_error(\"refusing to overwrite existing output: results/main/\" +\n"
     "                                         out_stem + \".jsonl\");\n"
     "        }\n"
     "        std::ofstream out((\"results/main/\" + out_stem + \".jsonl\").c_str(), std::ios::app);\n")
assert s.count(a) == 1, "refuse overwrite"
s = s.replace(a, b)

a = "                require(d.cursor_starts > 0);\n"
b = "                require(d.cursor_starts > 0 || expect == 0);\n"
assert s.count(a) == 1, "cursor assert"
s = s.replace(a, b)

io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("diag_merge_entry fixes applied")
