import io
p = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\scratch_merge\probe_b_arm.cpp"
s = io.open(p, encoding="utf-8", newline="").read()

a = ("                for (const auto& m : moves) s->erase(m.first);\n"
     "                for (const auto& m : moves) s->insert(m.second, ref.at(m.second));\n")
b = ("                // Interleaved exactly like bench_combo's grow arm (its documented shape).\n"
     "                for (const auto& m : moves) {\n"
     "                    s->erase(m.first);\n"
     "                    s->insert(m.second, ref.at(m.second));\n"
     "                }\n")
assert s.count(a) == 1, "interleave"
s = s.replace(a, b)

a = ("                std::uint64_t merge = 0, rows = 0, outp = 0, outo = 0;\n"
     "                for (const auto& st : starts) {\n"
     "                    ::validation::e25::counts().reset();\n"
     "                    const auto out = s->scan(st, 256);\n"
     "                    merge += ::validation::e25::counts().scan_merge_cmps;\n"
     "                    outp += ::validation::e25::counts().scan_out_primary;\n"
     "                    outo += ::validation::e25::counts().scan_out_overflow;\n"
     "                    rows += out.size();\n"
     "                }\n"
     "                std::cout << \"variant=\" << variant << \" frac=\" << frac << \" merge=\" << merge\n"
     "                          << \" rows=\" << rows << \" outp=\" << outp << \" outo=\" << outo\n"
     "                          << \" recl=\" << ::validation::e25::counts().subblock_reclaims\n"
     "                          << \" wide=\" << ::validation::e25::counts().reclaim_widened << std::endl;\n")
b = ("                std::uint64_t merge = 0, rows = 0, outp = 0, outo = 0;\n"
     "                MergeShadowCounts tot;\n"
     "                std::uint64_t sh_mismatch = 0;\n"
     "                for (const auto& st : starts) {\n"
     "                    ::validation::e25::counts().reset();\n"
     "                    const auto out = s->scan(st, 256);\n"
     "                    merge += ::validation::e25::counts().scan_merge_cmps;\n"
     "                    outp += ::validation::e25::counts().scan_out_primary;\n"
     "                    outo += ::validation::e25::counts().scan_out_overflow;\n"
     "                    rows += out.size();\n"
     "                    std::vector<Pair> sh;\n"
     "                    const MergeShadowCounts sc = s->diag_scan_merge_shadow(st, 256, sh);\n"
     "                    tot.add(sc);\n"
     "                    if (sh != out) ++sh_mismatch;\n"
     "                }\n"
     "                std::cout << \"variant=\" << variant << \" frac=\" << frac << \" merge=\" << merge\n"
     "                          << \" rows=\" << rows << \" outp=\" << outp << \" outo=\" << outo\n"
     "                          << \" recl=\" << ::validation::e25::counts().subblock_reclaims\n"
     "                          << \" wide=\" << ::validation::e25::counts().reclaim_widened\n"
     "                          << \" shadow_cmps=\" << tot.merge_cmps\n"
     "                          << \" probes=\" << tot.probes << \" ok=\" << tot.probe_ok\n"
     "                          << \" fail=\" << tot.probe_fail << \" eq=\" << tot.probe_fail_equal\n"
     "                          << \" seg_rows=\" << tot.seg_rows << \" sh_mismatch=\" << sh_mismatch\n"
     "                          << std::endl;\n")
assert s.count(a) == 1, "shadow"
s = s.replace(a, b)
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("probe extended: interleaved + shadow")
