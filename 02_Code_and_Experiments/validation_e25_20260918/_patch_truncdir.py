# -*- coding: utf-8 -*-
"""TRUNCDIR (EqualDepthDirTrunc) patch: model + index wiring + switches + build registry.

Every replacement is asserted to apply exactly once, so a silently-missing edit fails the
script instead of producing a half-patched tree.
"""
import io, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
def read(p):
    with io.open(p, encoding="utf-8", newline="") as f: return f.read()
def write(p, s):
    with io.open(p, "w", encoding="utf-8", newline="\n") as f: f.write(s)
def sub(text, old, new, what):
    n = text.count(old)
    if n != 1:
        raise SystemExit("FATAL: %s: expected 1 occurrence, found %d" % (what, n))
    return text.replace(old, new)

# ---------------------------------------------------------------- model/equal_depth_dir.hpp
p = os.path.join(HERE, "model", "equal_depth_dir.hpp")
t = read(p)
t = sub(t,
"// coordinates. The truncation is NOT in this first version: the search below compares full\n"
"// suffixes, which makes exactness independent of any compression argument. Memory is therefore\n"
"// the untruncated figure, and it is measured rather than assumed.\n",
"// coordinates. The TRUNCATED form is this version's single variable (design:\n"
"// TRUNCDIR_ENTRY_DESIGN.md): fit(..., truncate_separators = true) stores for each threshold the\n"
"// SHORTEST prefix of its suffix that still compares greater than the key immediately to its\n"
"// left, which leaves the routing answer of every stored key identical (lemma A of the design)\n"
"// while dropping the bytes the binary search never had to read. The default (false) is the\n"
"// untruncated directory, byte for byte.\n",
"model header comment")

t = sub(t,
"    static RegionModel fit(const std::string_view* keys, std::size_t n,\n"
"                           std::size_t prefix_len, std::uint32_t groups) {\n"
"        RegionModel m;\n"
"        m.groups_ = groups;\n"
"        m.keys_ = n;\n",
"    static RegionModel fit(const std::string_view* keys, std::size_t n,\n"
"                           std::size_t prefix_len, std::uint32_t groups,\n"
"                           bool truncate_separators = false) {\n"
"        RegionModel m;\n"
"        m.groups_ = groups;\n"
"        m.keys_ = n;\n"
"        m.truncate_ = truncate_separators;\n",
"model fit signature")

t = sub(t,
"        const std::size_t sep_count = static_cast<std::size_t>(groups) - 1;\n"
"        m.offset_.assign(sep_count + 1, 0);\n"
"        std::size_t total = 0;\n"
"        for (std::size_t j = 1; j < groups; ++j) {\n"
"            const std::size_t rank = std::min(n - 1, (j * n) / groups);\n"
"            const std::string_view suffix = keys[rank].substr(m.prefix_len_);\n"
"            total += suffix.size();\n"
"            m.offset_[j] = static_cast<std::uint32_t>(total);\n"
"        }\n"
"        if (total > static_cast<std::size_t>(0xffffffffu))\n"
"            throw std::length_error(\"equal-depth directory: separator arena exceeds 4 GiB\");\n"
"\n"
"        m.arena_.resize(total);\n"
"        for (std::size_t j = 1; j < groups; ++j) {\n"
"            const std::size_t rank = std::min(n - 1, (j * n) / groups);\n"
"            const std::string_view suffix = keys[rank].substr(m.prefix_len_);\n"
"            std::memcpy(m.arena_.data() + m.offset_[j - 1], suffix.data(), suffix.size());\n"
"        }\n",
"        // Section 2 of the design, verbatim and with no free parameter: the stored threshold\n"
"        // t_j is the shortest prefix of s_j that is still > the key at rank-1; when s_j is\n"
"        // itself a prefix of that left key (a run of duplicate suffixes at the band boundary)\n"
"        // no shorter prefix can win, so s_j is kept as-is and the case is counted rather than\n"
"        // special-cased away.\n"
"        const std::size_t sep_count = static_cast<std::size_t>(groups) - 1;\n"
"        std::vector<std::string_view> stored;\n"
"        stored.reserve(sep_count);\n"
"        for (std::size_t j = 1; j < groups; ++j) {\n"
"            const std::size_t rank = std::min(n - 1, (j * n) / groups);\n"
"            const std::string_view suffix = keys[rank].substr(m.prefix_len_);\n"
"            std::string_view t = suffix;\n"
"            if (m.truncate_ && rank > 0) {\n"
"                const std::string_view left = keys[rank - 1].substr(m.prefix_len_);\n"
"                const std::size_t c = common_prefix(left, suffix);\n"
"                if (c < suffix.size()) t = suffix.substr(0, c + 1);\n"
"            }\n"
"            stored.push_back(t);\n"
"        }\n"
"\n"
"        m.offset_.assign(sep_count + 1, 0);\n"
"        std::size_t total = 0;\n"
"        for (std::size_t j = 0; j < sep_count; ++j) {\n"
"            total += stored[j].size();\n"
"            if (total > static_cast<std::size_t>(0xffffffffu))\n"
"                throw std::length_error(\"equal-depth directory: separator arena exceeds 4 GiB\");\n"
"            m.offset_[j + 1] = static_cast<std::uint32_t>(total);\n"
"        }\n"
"\n"
"        m.arena_.resize(total);\n"
"        for (std::size_t j = 0; j < sep_count; ++j) {\n"
"            const std::string_view t = stored[j];\n"
"            if (!t.empty())\n"
"                std::memcpy(m.arena_.data() + m.offset_[j], t.data(), t.size());\n"
"        }\n",
"model separator build")

t = sub(t,
"    Status status() const { return status_; }\n"
"    bool fitted() const { return status_ == Status::Ok; }\n",
"    Status status() const { return status_; }\n"
"    bool fitted() const { return status_ == Status::Ok; }\n"
"    // Whether this directory stores the truncated separators of section 2.\n"
"    bool truncated() const { return truncate_; }\n"
"    // Stored threshold idx (0 .. threshold_count()-1), i.e. j = idx + 1 of the design.\n"
"    std::string_view separator(std::size_t idx) const {\n"
"        if (idx + 1 >= offset_.size()) return std::string_view();\n"
"        return std::string_view(arena_.data() + offset_[idx], offset_[idx + 1] - offset_[idx]);\n"
"    }\n"
"    // Arena bytes actually stored (the truncated figure the round reports).\n"
"    std::size_t separator_bytes() const { return arena_.size(); }\n",
"model accessors")

t = sub(t,
"        const std::uint64_t st = static_cast<std::uint64_t>(status_);\n"
"        const std::uint64_t n = keys_;\n",
"        const std::uint64_t st = static_cast<std::uint64_t>(status_);\n"
"        const std::uint64_t tr = truncate_ ? 1u : 0u;\n"
"        const std::uint64_t n = keys_;\n",
"model identity flag")

t = sub(t,
"        mix(&st, sizeof(st));\n"
"        mix(&n, sizeof(n));\n",
"        mix(&st, sizeof(st));\n"
"        mix(&tr, sizeof(tr));\n"
"        mix(&n, sizeof(n));\n",
"model identity mix")

t = sub(t,
"    std::uint32_t groups_ = 0;\n"
"    Status status_ = Status::NoKeys;\n",
"    std::uint32_t groups_ = 0;\n"
"    Status status_ = Status::NoKeys;\n"
"    bool truncate_ = false;\n",
"model member")
write(p, t)
print("patched", p)

# ---------------------------------------------------------------- index.hpp
p = os.path.join(HERE, "index.hpp")
t = read(p)

t = sub(t,
"enum class ModelBackend { Context16, LitsHpt, BlockLinearRank, EqualDepthDir };\n",
"enum class ModelBackend { Context16, LitsHpt, BlockLinearRank, EqualDepthDir,\n"
"                          EqualDepthDirTrunc };\n"
"\n"
"// EqualDepthDirTrunc is the SAME directory as EqualDepthDir with section 2's truncated\n"
"// separators (model/equal_depth_dir.hpp). The mapping function has the same shape, so every\n"
"// wiring point asks this question instead of comparing against one enumerator; the two backends\n"
"// differ only in the bytes the directory stores, and the model identity mixes that flag in.\n"
"inline bool is_depth_dir_backend(ModelBackend b) {\n"
"    return b == ModelBackend::EqualDepthDir || b == ModelBackend::EqualDepthDirTrunc;\n"
"}\n",
"index enum + predicate")

t = sub(t,
"//   EqualDepthDir   a per-region separator directory at equal key RANKS\n"
"//                   (model/equal_depth_dir.hpp): balances the group load by construction\n",
"//   EqualDepthDir   a per-region separator directory at equal key RANKS\n"
"//                   (model/equal_depth_dir.hpp): balances the group load by construction\n"
"//   EqualDepthDirTrunc  the same directory with the truncated separators of\n"
"//                   TRUNCDIR_ENTRY_DESIGN.md section 2: shortest prefix that still beats the\n"
"//                   left neighbour, so stored keys route exactly as in EqualDepthDir\n",
"index backend comment")

t = sub(t,
"    std::uint64_t regions_with_duplicate_thresholds = 0;\n"
"    std::uint64_t fits = 0;\n"
"};",
"    std::uint64_t regions_with_duplicate_thresholds = 0;\n"
"    std::uint64_t threshold_bytes_total = 0;   // arena bytes stored, truncated or not\n"
"    std::uint64_t regions_truncated = 0;       // fitted regions whose separators were truncated\n"
"    std::uint64_t fits = 0;\n"
"};",
"index DepthDirStats fields")

t = sub(t,
"        if (config_.model_kind != ModelKind::Context16) {\n"
"            throw std::invalid_argument(\"EqualDepthDir supports ModelKind::Context16 only\");\n"
"        }\n"
"        if (config_.canonical != Route::Full) {\n"
"            throw std::invalid_argument(\"EqualDepthDir supports Route::Full only\");\n"
"        }\n",
"        if (config_.model_kind != ModelKind::Context16) {\n"
"            throw std::invalid_argument(\"the depth directory supports ModelKind::Context16 only\");\n"
"        }\n"
"        if (config_.canonical != Route::Full) {\n"
"            throw std::invalid_argument(\"the depth directory supports Route::Full only\");\n"
"        }\n",
"index refusal text")

t = sub(t,
"        depth_dir_ = equivdir::RegionModel::fit(keys.data(), keys.size(), prefix_.size(), groups_);\n",
"        // The truncated form is the same directory built from the same keys: one flag, fixed by\n"
"        // the backend the configuration names, so there is no per-region or per-run degree of\n"
"        // freedom here (the design's section 3 argues why the rule is not a parameter).\n"
"        const bool truncate = config_.model_backend == ModelBackend::EqualDepthDirTrunc;\n"
"        depth_dir_ = equivdir::RegionModel::fit(keys.data(), keys.size(), prefix_.size(), groups_,\n"
"                                                truncate);\n",
"index calibrate fit call")

t = sub(t,
"            depth_dir_stats_.thresholds_total += depth_dir_.threshold_count();\n",
"            depth_dir_stats_.thresholds_total += depth_dir_.threshold_count();\n"
"            depth_dir_stats_.threshold_bytes_total += depth_dir_.separator_bytes();\n"
"            if (depth_dir_.truncated()) ++depth_dir_stats_.regions_truncated;\n",
"index calibrate stats")

t = sub(t,
"        } else if (config_.model_backend == ModelBackend::EqualDepthDir) {\n"
"            calibrate_depth_dir(sorted);\n"
"        }\n",
"        } else if (is_depth_dir_backend(config_.model_backend)) {\n"
"            calibrate_depth_dir(sorted);\n"
"        }\n",
"index constructor dispatch")

t = sub(t,
"        if (backend == ModelBackend::EqualDepthDir) {\n",
"        if (is_depth_dir_backend(backend)) {\n",
"index mapping_identity")

t = sub(t,
"        if (cfg.model_backend == ModelBackend::EqualDepthDir) {\n"
"            std::vector<std::string_view> depth_keys;\n"
"            depth_keys.reserve(n);\n"
"            for (const Pair* it = first; it != last; ++it) depth_keys.push_back(it->first);\n"
"            depth_dir = equivdir::RegionModel::fit(depth_keys.data(), depth_keys.size(),\n"
"                                                   prefix.size(), groups);\n"
"            use_depth_dir = true;\n"
"        }\n",
"        if (is_depth_dir_backend(cfg.model_backend)) {\n"
"            std::vector<std::string_view> depth_keys;\n"
"            depth_keys.reserve(n);\n"
"            for (const Pair* it = first; it != last; ++it) depth_keys.push_back(it->first);\n"
"            depth_dir = equivdir::RegionModel::fit(\n"
"                depth_keys.data(), depth_keys.size(), prefix.size(), groups,\n"
"                cfg.model_backend == ModelBackend::EqualDepthDirTrunc);\n"
"            use_depth_dir = true;\n"
"        }\n",
"index demand fit")
write(p, t)
print("patched", p)

# ---------------------------------------------------------------- switches.hpp
p = os.path.join(HERE, "switches.hpp")
t = read(p)
t = sub(t,
"        case validation::ModelBackend::EqualDepthDir: return \"EqualDepthDir\";\n",
"        case validation::ModelBackend::EqualDepthDir: return \"EqualDepthDir\";\n"
"        case validation::ModelBackend::EqualDepthDirTrunc: return \"EqualDepthDirTrunc\";\n",
"switches backend_name")
write(p, t)
print("patched", p)

# ---------------------------------------------------------------- tools_build.py
p = os.path.join(HERE, "tools_build.py")
t = read(p)
t = sub(t, "    \"san_check_depth_dir\",\n", "    \"san_check_depth_dir\",\n    \"san_check_depth_dir_trunc\",\n", "SAN_TARGETS")
t = sub(t,
"    \"check_depth_dir\": [\"-DE12_COUNT\"],\n"
"    \"san_check_depth_dir\": [\"-DE12_COUNT\"],\n",
"    \"check_depth_dir\": [\"-DE12_COUNT\"],\n"
"    \"san_check_depth_dir\": [\"-DE12_COUNT\"],\n"
"    # TRUNCDIR gate: same entry counters over the TRUNCATED directory, so the same macro rule.\n"
"    \"check_depth_dir_trunc\": [\"-DE12_COUNT\"],\n"
"    \"san_check_depth_dir_trunc\": [\"-DE12_COUNT\"],\n",
"EXTRA")
t = sub(t,
"       \"check_depth_dir\": \"check_depth_dir.cpp\",\n"
"       \"san_check_depth_dir\": \"check_depth_dir.cpp\",\n",
"       \"check_depth_dir\": \"check_depth_dir.cpp\",\n"
"       \"san_check_depth_dir\": \"check_depth_dir.cpp\",\n"
"       # TRUNCDIR: one source, two targets -- the gate and its sanitizer twin.\n"
"       \"check_depth_dir_trunc\": \"check_depth_dir_trunc.cpp\",\n"
"       \"san_check_depth_dir_trunc\": \"check_depth_dir_trunc.cpp\",\n",
"SRC")
write(p, t)
print("patched", p)
print("OK")
