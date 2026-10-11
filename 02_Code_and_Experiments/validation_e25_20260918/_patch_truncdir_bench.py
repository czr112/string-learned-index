# -*- coding: utf-8 -*-
"""TRUNCDIR A/B device: make the arm pair a run parameter (design section 6)."""
import io, os
HERE = os.path.dirname(os.path.abspath(__file__))

def edit(name, pairs):
    p = os.path.join(HERE, name)
    t = io.open(p, encoding="utf-8", newline="").read()
    for old, new, what in pairs:
        if t.count(old) != 1:
            raise SystemExit("FATAL %s: %s not found exactly once (%d)" %
                             (name, what, t.count(old)))
        t = t.replace(old, new)
    io.open(p, "w", encoding="utf-8", newline="\n").write(t)
    print("patched", p)

edit("bench_depth_dir.cpp", [
("const Arm ARMS[2] = {\n"
 "    {\"C16\", ModelBackend::Context16},\n"
 "    {\"DDir\", ModelBackend::EqualDepthDir},\n"
 "};\n"
 "\n"
 "constexpr int kArms = 2;\n",
 "// The arm pair is a RUN PARAMETER (TRUNCDIR design section 6): the round's main comparison is\n"
 "// C16 vs DDirT and its mechanism comparison is DDir vs DDirT, and both use THIS device -- the\n"
 "// only difference between the two runs is the pair of names on the command line. The default\n"
 "// stays the untruncated pair, so a call without the argument reproduces the previous round's\n"
 "// device exactly.\n"
 "const Arm ARM_TABLE[] = {\n"
 "    {\"C16\", ModelBackend::Context16},\n"
 "    {\"DDir\", ModelBackend::EqualDepthDir},\n"
 "    {\"DDirT\", ModelBackend::EqualDepthDirTrunc},\n"
 "};\n"
 "\n"
 "// \"A,B\", exactly two known and DIFFERENT arms, in the order they are to be paired (ref first).\n"
 "std::vector<Arm> parse_arms(const std::string& spec) {\n"
 "    std::vector<Arm> out;\n"
 "    std::size_t start = 0;\n"
 "    while (true) {\n"
 "        const std::size_t comma = spec.find(',', start);\n"
 "        const std::string name = spec.substr(\n"
 "            start, comma == std::string::npos ? std::string::npos : comma - start);\n"
 "        bool known = false;\n"
 "        for (const auto& a : ARM_TABLE) {\n"
 "            if (name == a.name) { out.push_back(a); known = true; }\n"
 "        }\n"
 "        if (!known) throw std::invalid_argument(\"unknown arm name: \" + name);\n"
 "        if (comma == std::string::npos) break;\n"
 "        start = comma + 1;\n"
 "    }\n"
 "    if (out.size() != 2)\n"
 "        throw std::invalid_argument(\"arm spec must name exactly two arms: \" + spec);\n"
 "    if (out[0].backend == out[1].backend)\n"
 "        throw std::invalid_argument(\"arm spec names the same backend twice: \" + spec);\n"
 "    return out;\n"
 "}\n"
 "\n"
 "constexpr int kArms = 2;\n",
 "arm table + parser"),
("        if (argc < 4)\n"
 "            throw std::invalid_argument(\"bench_depth_dir KIND N REPS [TRANCOCSV] [RUNTAG] [OUT]\");\n",
 "        if (argc < 4)\n"
 "            throw std::invalid_argument(\n"
 "                \"bench_depth_dir KIND N REPS [TRANCOCSV] [RUNTAG] [OUT] [ARMS]\");\n",
 "usage string"),
("        const std::string out_stem = argc > 6 ? argv[6] : \"depth_ab\";\n",
 "        const std::string out_stem = argc > 6 ? argv[6] : \"depth_ab\";\n"
 "        const std::string arm_spec = argc > 7 ? argv[7] : \"C16,DDir\";\n"
 "        const std::vector<Arm> arms = parse_arms(arm_spec);\n",
 "arm spec parse"),
("            cfg[a].model_backend = ARMS[a].backend;\n",
 "            cfg[a].model_backend = arms[a].backend;\n",
 "cfg arm backend"),
("                    << \"\\\",\\\"arm\\\":\\\"\" << ARMS[arm].name\n"
 "                    << \"\\\",\\\"backend\\\":\\\"\" << e25sw::backend_name(ARMS[arm].backend)\n",
 "                    << \"\\\",\\\"arm\\\":\\\"\" << arms[arm].name\n"
 "                    << \"\\\",\\\"backend\\\":\\\"\" << e25sw::backend_name(arms[arm].backend)\n",
 "json arm name"),
("                    << \",\\\"dir_thresholds\\\":\" << rd.thresholds_total\n"
 "                    << \"}\" << std::endl;\n",
 "                    << \",\\\"dir_thresholds\\\":\" << rd.thresholds_total\n"
 "                    << \",\\\"dir_threshold_bytes\\\":\" << rd.threshold_bytes_total\n"
 "                    << \",\\\"dir_regions_truncated\\\":\" << rd.regions_truncated\n"
 "                    << \"}\" << std::endl;\n",
 "json dir stats"),
])
