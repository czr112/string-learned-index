import io
p = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\diag_merge_entry.cpp"
s = io.open(p, encoding="utf-8", newline="").read()

a = ("            for (const auto& m : seg) s->erase(m.victim);\n"
     "            for (const auto& m : seg) s->insert(m.fresh, m.value);\n"
     "            done = target;\n")
b = ("            // Interleaved, one replacement at a time -- bench_combo's documented shape:\n"
     "            // batching the erases first leaves the primary blocks full of holes and makes\n"
     "            // the widening branch unreachable, which is a DIFFERENT structure from the one\n"
     "            // this entry is defined on. (The first draft of this device batched; it was\n"
     "            // caught by comparing C_old against the recorded combo_se run, 2116 at 100%.)\n"
     "            for (const auto& m : seg) {\n"
     "                s->erase(m.victim);\n"
     "                s->insert(m.fresh, m.value);\n"
     "            }\n"
     "            done = target;\n")
assert s.count(a) == 1, "interleave"
s = s.replace(a, b)

a = ("            const std::uint64_t recl = ::validation::e25::counts().subblock_reclaims - prev_reclaims;\n"
     "            const std::uint64_t wide = ::validation::e25::counts().reclaim_widened - prev_widened;\n"
     "            prev_reclaims = ::validation::e25::counts().subblock_reclaims;\n"
     "            prev_widened = ::validation::e25::counts().reclaim_widened;\n")
b = ("            const std::uint64_t recl = ::validation::e25::counts().subblock_reclaims - prev_reclaims;\n"
     "            const std::uint64_t wide = ::validation::e25::counts().reclaim_widened - prev_widened;\n"
     "            prev_reclaims = ::validation::e25::counts().subblock_reclaims;\n"
     "            prev_widened = ::validation::e25::counts().reclaim_widened;\n"
     "            // The plan requires W's branches to have really fired on this workload; the\n"
     "            // cumulative counts are asserted at the target checkpoints below.\n")
assert s.count(a) == 1, "recl note"
s = s.replace(a, b)

a = ("            if (s->size() != ref.size())\n"
     "                throw std::runtime_error(\"checkpoint live population diverged from the reference\");\n")
b = ("            if (s->size() != ref.size())\n"
     "                throw std::runtime_error(\"checkpoint live population diverged from the reference\");\n"
     "            if (::validation::e25::counts().subblock_reclaims +\n"
     "                    ::validation::e25::counts().reclaim_widened ==\n"
     "                0)\n"
     "                throw std::runtime_error(\n"
     "                    \"W's reclaim branches never fired on this workload; the entry state would not \"\n"
     "                    \"be the one the plan froze\");\n")
assert s.count(a) == 1, "W assert"
s = s.replace(a, b)

io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("diag_merge_entry: interleaved schedule + W-fired assertion")
