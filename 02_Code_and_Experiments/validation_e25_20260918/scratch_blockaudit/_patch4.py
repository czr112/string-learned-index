import hashlib
GATE = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\check_block_rank.cpp'

def sub(path, a, r, label):
    s = open(path, 'r', encoding='utf-8', newline='').read()
    if s.count(a) != 1:
        raise SystemExit('ANCHOR FAIL %s (%d)' % (label, s.count(a)))
    open(path, 'w', encoding='utf-8', newline='').write(s.replace(a, r))

# per-region probe accounting, both arms
sub(GATE,
'''        std::uint64_t ref_bad = 0, blk_bad = 0, checksum_ref = 0, checksum_blk = 0;
        std::uint64_t blk_probes = 0, blk_cmps = 0, blk_ovf = 0;
        std::uint64_t ref_probes = 0, ref_cmps = 0, ref_ovf = 0;
        for (const auto& kv : keys) {
            const auto a = ref.find(kv.first, qmode);
            const auto b = blk.find(kv.first, qmode);''',
'''        std::uint64_t ref_bad = 0, blk_bad = 0, checksum_ref = 0, checksum_blk = 0;
        std::uint64_t blk_probes = 0, blk_cmps = 0, blk_ovf = 0;
        std::uint64_t ref_probes = 0, ref_cmps = 0, ref_ovf = 0;
        // Per-region probe totals, so the aggregate can be read as "everywhere a little" or
        // "a few regions a lot". The two arms have the SAME G per region (the group count
        // comes from n and the suffix bytes, not from the model), so a per-region comparison
        // is apples to apples.
        std::vector<std::uint64_t> ref_region_probes(ref.region_count(), 0);
        std::vector<std::uint64_t> blk_region_probes(blk.region_count(), 0);
        for (const auto& kv : keys) {
            const auto ra = ref.locate(kv.first);
            const auto rb = blk.locate(kv.first);
            const auto a = ref.find(kv.first, qmode);
            const auto b = blk.find(kv.first, qmode);
            ref_region_probes[ra] += a.subblocks_visited;
            blk_region_probes[rb] += b.subblocks_visited;''',
 'per-region probes')

# summary + top offenders, printed after the aggregate line
sub(GATE,
'''        std::cout << "checks=" << checks << " failures=" << failures << "\\n";''',
'''        {
            // Ratios over the regions that saw traffic. A median near 1 with a long right
            // tail is a different finding from "every region costs more".
            std::vector<double> ratios;
            for (std::size_t i = 0; i < blk_region_probes.size() && i < ref_region_probes.size(); ++i) {
                if (!ref_region_probes[i]) continue;
                ratios.push_back(double(blk_region_probes[i]) / double(ref_region_probes[i]));
            }
            std::sort(ratios.begin(), ratios.end());
            const auto at = [&ratios](double q) {
                return ratios.empty() ? 0.0 : ratios[std::min(ratios.size() - 1,
                    static_cast<std::size_t>(q * double(ratios.size() - 1)))];
            };
            std::size_t worse = 0;
            for (double r : ratios) if (r > 1.0) ++worse;
            std::cout << "{\\"kind\\":\\"" << kind << "\\",\\"probe_ratio_regions\\":" << ratios.size()
                      << ",\\"probe_ratio_median\\":" << at(0.5) << ",\\"probe_ratio_p90\\":" << at(0.9)
                      << ",\\"probe_ratio_max\\":" << (ratios.empty() ? 0.0 : ratios.back())
                      << ",\\"probe_ratio_worse_regions\\":" << worse << "}\\n";
        }

        std::cout << "checks=" << checks << " failures=" << failures << "\\n";''',
 'ratio summary')

print('gate sha256', hashlib.sha256(open(GATE,'rb').read()).hexdigest())
