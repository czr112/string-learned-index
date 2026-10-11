import io, sys, os

base = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918"
def read(p): return io.open(os.path.join(base, p), encoding="utf-8", newline="").read()
def write(p, s): io.open(os.path.join(base, p), "w", encoding="utf-8", newline="\n").write(s)

# ---------------- index.hpp ----------------
p = "index.hpp"
s = read(p)
n0 = len(s)

# (a) new diagnostic counter fields
a = "    std::uint64_t cmp_bytes = 0;\n    std::uint64_t place_attempts = 0;      // insert_into calls\n"
b = ("    std::uint64_t cmp_bytes = 0;\n"
     "    // P1 read-path entry diagnosis (2026-10-11): site-split compare work, outer-navigation\n"
     "    // work and the fingerprint's fused-vs-tail split. The totals above stay the totals, so\n"
     "    // `primary + overflow == cmp_calls/cmp_bytes` on the read path is checked, not assumed.\n"
     "    std::uint64_t locate_cmp_calls = 0;    // Index::locate boundary probes (outer routing)\n"
     "    std::uint64_t locate_cmp_bytes = 0;    // ... logical bytes walked by those probes\n"
     "    std::uint64_t fp_fused_bytes = 0;      // bytes the MODEL fed to the hash during prediction\n"
     "    std::uint64_t cmp_calls_primary = 0;   // exact compares against primary entries\n"
     "    std::uint64_t cmp_bytes_primary = 0;\n"
     "    std::uint64_t cmp_calls_overflow = 0;  // exact compares against overflow records\n"
     "    std::uint64_t cmp_bytes_overflow = 0;\n"
     "    std::uint64_t place_attempts = 0;      // insert_into calls\n")
assert s.count(a) == 1, "anchor a"
s = s.replace(a, b)

# (b) site-split compare macros, placed after compare_bytes (diagnostic builds only)
a = ("    return c;\n}\n#endif\n\n"
     "// ---------------------------------------------------------------------------\n"
     "// Conditional model: integer interval arithmetic over quantized probabilities.\n")
b = ("    return c;\n}\n#endif\n\n"
     "#ifdef E25_DIAG\n"
     "// P1 (2026-10-11): site-split compare counting for the read path. Diagnostic builds\n"
     "// only; in every other build both macros are plain compare().\n"
     "inline int compare_bytes_site(std::string_view a, std::string_view b,\n"
     "                              std::uint64_t& total, std::uint64_t& site) {\n"
     "    const std::uint64_t before = total;\n"
     "    const int c = compare_bytes(a, b, total);\n"
     "    site += total - before;\n"
     "    return c;\n"
     "}\n"
     "#define E25_CMP_P(a, b)                                                        \\\n"
     "    (++::validation::e25::counts().cmp_calls,                                  \\\n"
     "     ++::validation::e25::counts().cmp_calls_primary,                          \\\n"
     "     compare_bytes_site((a), (b), ::validation::e25::counts().cmp_bytes,       \\\n"
     "                        ::validation::e25::counts().cmp_bytes_primary))\n"
     "#define E25_CMP_O(a, b)                                                        \\\n"
     "    (++::validation::e25::counts().cmp_calls,                                  \\\n"
     "     ++::validation::e25::counts().cmp_calls_overflow,                         \\\n"
     "     compare_bytes_site((a), (b), ::validation::e25::counts().cmp_bytes,       \\\n"
     "                        ::validation::e25::counts().cmp_bytes_overflow))\n"
     "#else\n"
     "#define E25_CMP_P(a, b) compare((a), (b))\n"
     "#define E25_CMP_O(a, b) compare((a), (b))\n"
     "#endif\n\n"
     "// ---------------------------------------------------------------------------\n"
     "// Conditional model: integer interval arithmetic over quantized probabilities.\n")
assert s.count(a) == 1, "anchor b"
s = s.replace(a, b)

# (c) fused fingerprint bytes at the model's hash site
a = "            if constexpr (Hash) fp.byte(ch);\n"
b = ("            if constexpr (Hash) {\n"
     "                fp.byte(ch);\n"
     "                E25_ADD(fp_fused_bytes, 1);\n"
     "            }\n")
assert s.count(a) == 1, "anchor c"
s = s.replace(a, b)

# (d) outer navigation counting in locate_scanned
a = ("        auto it = std::upper_bound(nodes_.begin(), nodes_.end(), key,\n"
     "            [](std::string_view k, const Node& n) { return compare(k, n.fence) < 0; });\n")
b = ("#ifdef E25_DIAG\n"
     "        // P1 read-path diagnosis: the same comparator, counting the boundary probes and the\n"
     "        // logical bytes they walk. Compiled out of every other build.\n"
     "        auto it = std::upper_bound(nodes_.begin(), nodes_.end(), key,\n"
     "            [](std::string_view k, const Node& n) {\n"
     "                E25_INC(locate_cmp_calls);\n"
     "                const int c =\n"
     "                    compare_bytes(k, n.fence, ::validation::e25::counts().locate_cmp_bytes);\n"
     "                return c < 0;\n"
     "            });\n"
     "#else\n"
     "        auto it = std::upper_bound(nodes_.begin(), nodes_.end(), key,\n"
     "            [](std::string_view k, const Node& n) { return compare(k, n.fence) < 0; });\n"
     "#endif\n")
assert s.count(a) == 1, "anchor d"
s = s.replace(a, b)

# (e) Region-level traced directory hook
a = ("    std::uint32_t placement_group(std::string_view full_key) const {\n"
     "        return canonical_group(full_key);\n"
     "    }\n")
b = a + ("#ifdef E25_DIAG\n"
         "    // P1 read-path diagnosis: run THIS region's directory search under a trace for the same\n"
         "    // full key the find path receives. False when the region is not on the directory backend\n"
         "    // or its model did not fit, in which case the production path would not search a\n"
         "    // directory either.\n"
         "    bool diag_depth_dir_trace(std::string_view full_key, equivdir::SearchTrace& tr,\n"
         "                              std::uint32_t& group, std::uint32_t& consumed) const {\n"
         "        if (!use_depth_dir_ || !depth_dir_.fitted()) return false;\n"
         "        group = depth_dir_.predict_group_traced(full_key, consumed, tr);\n"
         "        return true;\n"
         "    }\n"
         "#endif\n")
assert s.count(a) == 1, "anchor e"
s = s.replace(a, b)

write(p, s)
print("index.hpp patched:", n0, "->", len(s))

# ---------------- model/equal_depth_dir.hpp ----------------
p = "model/equal_depth_dir.hpp"
s = read(p)
n0 = len(s)

a = "class RegionModel {\n"
b = ("#ifdef E25_DIAG\n"
     "// P1 (2026-10-11) read-path diagnosis: the traced shadow of predict_group(). Diagnostic\n"
     "// builds only, so the shipped search keeps exactly one implementation; the diagnosis\n"
     "// asserts per query that the trace reproduces the production group and the walked-byte\n"
     "// count, so a drifting shadow fails the run instead of being reported.\n"
     "struct SearchTrace {\n"
     "    std::uint32_t probes = 0;           // binary-search iterations\n"
     "    std::uint64_t compared = 0;         // bytes examined (same definition as `walked`)\n"
     "    std::uint32_t end_first_diff = 0;   // iterations decided at the first differing byte\n"
     "    std::uint32_t end_sep_le_q = 0;     // overlap equal, separator <= query\n"
     "    std::uint32_t end_sep_gt_q = 0;     // overlap equal, separator > query\n"
     "    std::uint32_t sep_prefix_of_q = 0;  // separator is a strict prefix of the query\n"
     "};\n"
     "#endif\n\n"
     "class RegionModel {\n")
assert s.count(a) == 1, "anchor f"
s = s.replace(a, b)

a = ("    std::uint32_t predict_group(std::string_view full_key) const {\n"
     "        std::uint32_t consumed = 0;\n"
     "        return predict_group(full_key, consumed);\n"
     "    }\n")
b = a + ("\n#ifdef E25_DIAG\n"
         "    std::uint32_t predict_group_traced(std::string_view full_key, std::uint32_t& consumed,\n"
         "                                       SearchTrace& tr) const {\n"
         "        if (status_ != Status::Ok) { consumed = 0; return 0; }\n"
         "        const std::string_view q = full_key.size() > prefix_len_\n"
         "            ? full_key.substr(prefix_len_) : std::string_view();\n"
         "        const std::size_t sep_count = offset_.size() - 1;\n"
         "        std::uint64_t walked = 0;\n"
         "        std::size_t lo = 0, hi = sep_count;\n"
         "        while (lo < hi) {\n"
         "            ++tr.probes;\n"
         "            const std::size_t mid = lo + (hi - lo) / 2;\n"
         "            const std::string_view sep(\n"
         "                arena_.data() + offset_[mid], offset_[mid + 1] - offset_[mid]);\n"
         "            const std::size_t n = std::min(q.size(), sep.size());\n"
         "            std::size_t i = 0;\n"
         "            while (i < n && q[i] == sep[i]) ++i;\n"
         "            walked += i + 1;\n"
         "            bool sep_le_q = false;\n"
         "            if (i == n) {\n"
         "                sep_le_q = sep.size() <= q.size();\n"
         "                if (sep_le_q) ++tr.end_sep_le_q; else ++tr.end_sep_gt_q;\n"
         "                if (sep.size() < q.size()) ++tr.sep_prefix_of_q;\n"
         "            } else {\n"
         "                sep_le_q = static_cast<unsigned char>(sep[i]) <\n"
         "                           static_cast<unsigned char>(q[i]);\n"
         "                ++tr.end_first_diff;\n"
         "            }\n"
         "            if (sep_le_q) lo = mid + 1;\n"
         "            else hi = mid;\n"
         "        }\n"
         "        tr.compared = walked;\n"
         "        consumed = static_cast<std::uint32_t>(\n"
         "            std::min<std::uint64_t>(walked, 0xffffffffull));\n"
         "        const std::uint64_t g = lo;\n"
         "        return static_cast<std::uint32_t>(std::min<std::uint64_t>(groups_ - 1, g));\n"
         "    }\n"
         "#endif\n")
assert s.count(a) == 1, "anchor g"
s = s.replace(a, b)
write(p, s)
print("equal_depth_dir.hpp patched:", n0, "->", len(s))
