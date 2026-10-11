import hashlib, os
D = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918'
P = os.path.join(D, 'diag_threshold_entry.cpp')
s = open(P, 'r', encoding='utf-8', newline='').read()

def sub(t, a, r, label):
    if t.count(a) != 1:
        raise SystemExit('ANCHOR FAIL %s (%d)' % (label, t.count(a)))
    return t.replace(a, r)

# --- helpers: real compare cost + separator truncation ---
s = sub(s, '''struct LoadStats {''',
'''// Bytes a memcmp-style comparison actually walks: everything up to and including the first
// differing byte, or the shorter length plus the length decision when one is a prefix of the
// other. This is the honest charge for a separator search -- min(len)+1 is a worst case that a
// search over DISTINCT separators almost never pays.
std::size_t compare_cost(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i + 1;
}

std::size_t lcp(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}

struct LoadStats {''', 'compare helpers')

# --- counters ---
s = sub(s, '''        std::uint64_t fixed_steps = 0, fixed_cmp_bytes = 0;
        std::uint64_t bytes_steps = 0, bytes_cmp_bytes = 0;''',
'''        std::uint64_t fixed_steps = 0, fixed_cmp_bytes = 0;
        std::uint64_t bytes_steps = 0, bytes_cmp_bytes = 0;
        std::uint64_t truncated_threshold_bytes = 0;   // separators cut at lcp(prev,next)+1
        std::uint64_t ctx_subblocks = 0, bytes_subblocks = 0;   // the layout consequence''', 'counters')

# --- per-region truncated separators (needs the SORTED separator list) ---
s = sub(s, '''            fixed_threshold_bytes += tf.size() * sizeof(std::uint64_t);''',
'''            fixed_threshold_bytes += tf.size() * sizeof(std::uint64_t);
            // The classic separator compression: a boundary only has to be stored up to one byte
            // past what it shares with its SORTED neighbours. This is a memory account, not a
            // lookup model -- the integrated round would measure the search itself.
            for (std::size_t j = 0; j < tb.size(); ++j) {
                const std::size_t prev = j ? lcp(tb[j - 1], tb[j]) : 0;
                const std::size_t next = (j + 1 < tb.size()) ? lcp(tb[j], tb[j + 1]) : 0;
                const std::size_t keep = std::min(tb[j].size(), std::max(prev, next) + 1);
                truncated_threshold_bytes += keep;
            }''', 'truncated separators')

# --- charge the real compare cost in the BYTES search ---
s = sub(s, '''                        bytes_cmp_bytes += std::min(key_suffix.size(), tb[mid].size()) + 1;''',
'''                        bytes_cmp_bytes += compare_cost(key_suffix, tb[mid]);''', 'byte charge')

# --- sub-block counts: measured for ctx16, computed for the balanced map ---
s = sub(s, '''                ++lf[std::min(gf, std::size_t(G - 1))];
                ++lb[std::min(gb, std::size_t(G - 1))];
            }''',
'''                ++lf[std::min(gf, std::size_t(G - 1))];
                ++lb[std::min(gb, std::size_t(G - 1))];
            }

            // The layout consequence: how many sub-blocks each map needs, with the index's own
            // rule (max of the entry and byte constraints). Context16's numbers are MEASURED,
            // from its group_stats; the balanced map's are computed from its loads and the real
            // suffix bytes. Directory size -- and therefore locate cost -- follows from this.
            {
                const std::size_t slots = base.slots;
                const std::size_t per_block =
                    std::size_t(base.block_bytes) - sizeof(Header) - slots * sizeof(Entry);
                const auto blocks = [&](std::size_t entries, std::size_t bytes) {
                    if (!entries) return std::size_t(0);
                    const std::size_t by_entries = (entries + slots - 1) / slots;
                    const std::size_t by_bytes = (bytes + per_block - 1) / per_block;
                    return std::max<std::size_t>(std::max(by_entries, by_bytes), 1);
                };
                for (const auto& gs : rr.group_stats())
                    ctx_subblocks += blocks(gs.primary_entries, gs.primary_bytes);
                std::vector<std::size_t> lb_bytes(G, 0);
                for (std::size_t i = 0; i < n; ++i) lb_bytes[std::min(gb_of[i], std::size_t(G - 1))] +=
                    (mine[i].size() - plen);
                for (std::uint32_t g = 0; g < G; ++g)
                    bytes_subblocks += blocks(lb[g], lb_bytes[g]);
            }''', 'subblocks')

# --- remember each key's balanced group so the sub-block pass can reuse it ---
s = sub(s, '''            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t gf = static_cast<std::size_t>(
                    std::upper_bound(tf.begin(), tf.end(), xf[i]) - tf.begin());''',
'''            std::vector<std::size_t> gb_of(n, 0);
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t gf = static_cast<std::size_t>(
                    std::upper_bound(tf.begin(), tf.end(), xf[i]) - tf.begin());''', 'gb_of decl')
s = sub(s, '''                ++lf[std::min(gf, std::size_t(G - 1))];
                ++lb[std::min(gb, std::size_t(G - 1))];''',
'''                gb_of[i] = std::min(gb, std::size_t(G - 1));
                ++lf[std::min(gf, std::size_t(G - 1))];
                ++lb[gb_of[i]];''', 'gb_of fill')

# --- JSON additions ---
s = sub(s, '''                  << ",\\"bytes_threshold_bytes\\":" << bytes_threshold_bytes''',
'''                  << ",\\"bytes_threshold_bytes\\":" << bytes_threshold_bytes
                  << ",\\"bytes_truncated_threshold_bytes\\":" << truncated_threshold_bytes
                  << ",\\"ctx_subblocks\\":" << ctx_subblocks
                  << ",\\"bytes_subblocks\\":" << bytes_subblocks''', 'json')

open(P, 'w', encoding='utf-8', newline='').write(s)
print('diag patched', hashlib.sha256(open(P,'rb').read()).hexdigest())
