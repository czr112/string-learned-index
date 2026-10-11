import hashlib, os
D = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918'
P = os.path.join(D, 'diag_threshold_entry.cpp')
s = open(P, 'r', encoding='utf-8', newline='').read()

def sub(t, a, r, label):
    if t.count(a) != 1:
        raise SystemExit('ANCHOR FAIL %s (%d)' % (label, t.count(a)))
    return t.replace(a, r)

# 1) string_view prefix helper (no temporaries)
s = sub(s, '''std::size_t common_prefix_len(const std::string& a, const std::string& b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}''',
'''std::size_t common_prefix_len(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}

// The region each key belongs to, computed ONCE: the per-region loop below would otherwise
// pay one routing search per region per key.
std::vector<std::size_t> bucket_of(Index& idx, const std::vector<Pair>& keys) {
    std::vector<std::size_t> out(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) out[i] = idx.locate(keys[i].first);
    return out;
}''', 'prefix helper')

# 2) region loop: use the precomputed buckets and an index loop
s = sub(s, '''        for (std::size_t r = 0; r < ref.region_count(); ++r) {
            const Region& rr = ref.region(r);
            const std::uint32_t G = rr.groups();
            if (G == 0) continue;

            // The region's own key range, taken from the index's own routing so the partition
            // under test is the partition the index builds.
            std::vector<std::pair<std::string_view, std::uint64_t>> mine;
            for (const auto& kv : keys) {
                if (ref.locate(kv.first) == r) mine.emplace_back(kv.first, kv.second);
            }
            const std::size_t n = mine.size();
            if (!n) continue;''',
'''        const std::vector<std::size_t> bucket = bucket_of(ref, keys);

        for (std::size_t r = 0; r < ref.region_count(); ++r) {
            const Region& rr = ref.region(r);
            const std::uint32_t G = rr.groups();
            if (G == 0) continue;

            // The region's own key range, in the order the index routed it, so the partition
            // under test is the partition the index builds.
            std::vector<std::string_view> mine;
            for (std::size_t i = 0; i < keys.size(); ++i)
                if (bucket[i] == r) mine.push_back(keys[i].first);
            const std::size_t n = mine.size();
            if (!n) continue;''', 'region loop')

s = sub(s, '''            const std::size_t plen = common_prefix_len(std::string(mine.front().first),
                                                      std::string(mine.back().first));''',
'''            const std::size_t plen = common_prefix_len(mine.front(), mine.back());''', 'prefix call')

s = sub(s, '''            std::vector<std::uint64_t> xf(n);
            for (std::size_t i = 0; i < n; ++i) xf[i] = blockrank::encode(mine[i].first, plen, 7);''',
'''            std::vector<std::uint64_t> xf(n);
            for (std::size_t i = 0; i < n; ++i) xf[i] = blockrank::encode(mine[i], plen, 7);''', 'encode call')

s = sub(s, '''                tf.push_back(xf[rank]);
                tb.push_back(mine[rank].first.substr(plen));''',
'''                tf.push_back(xf[rank]);
                tb.push_back(mine[rank].substr(plen));''', 'threshold push')

s = sub(s, '''            distinct_fixed += static_cast<std::uint64_t>(
                std::unique(tf.begin(), tf.end()) - tf.begin());
            if (tf.size() != static_cast<std::size_t>(G - 1) &&
                std::unique(tf.begin(), tf.end()) - tf.begin() < static_cast<long>(G - 1))
                ++truncated_regions;''',
'''            const std::size_t distinct =
                static_cast<std::size_t>(std::unique(tf.begin(), tf.end()) - tf.begin());
            distinct_fixed += distinct;
            // The fixed encoding cannot separate the target depths when two thresholds share a
            // coordinate: those groups are unreachable, and the region's depth is capped.
            if (distinct < tf.size()) ++truncated_regions;''', 'distinct count')

s = sub(s, '''            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t gf = static_cast<std::size_t>(
                    std::upper_bound(tf.begin(), tf.end(), xf[i]) - tf.begin());
                const std::size_t gb = static_cast<std::size_t>(
                    std::upper_bound(tb.begin(), tb.end(), mine[i].first.substr(plen), byte_less) -
                    tb.begin());
                ++lf[std::min(gf, std::size_t(G - 1))];
                ++lb[std::min(gb, std::size_t(G - 1))];
            }''',
'''            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t gf = static_cast<std::size_t>(
                    std::upper_bound(tf.begin(), tf.end(), xf[i]) - tf.begin());
                const std::size_t gb = static_cast<std::size_t>(
                    std::upper_bound(tb.begin(), tb.end(), mine[i].substr(plen), byte_less) -
                    tb.begin());
                ++lf[std::min(gf, std::size_t(G - 1))];
                ++lb[std::min(gb, std::size_t(G - 1))];
            }''', 'load assign')

s = sub(s, '''            // ---- the candidate's own lookup cost, over the region's keys ----
            for (const auto& kv : mine) {
                ++queries;
                // FIXED: binary search on 63-bit coordinates, one 8-byte compare per step.
                {
                    std::size_t lo = 0, hi = tf.size();
                    while (lo < hi) {
                        const std::size_t mid = (lo + hi) / 2;
                        ++fixed_steps;
                        fixed_cmp_bytes += sizeof(std::uint64_t);
                        if (tf[mid] <= xf[static_cast<std::size_t>(&kv - mine.data())]) lo = mid + 1;
                        else hi = mid;
                    }
                }
                // BYTES: the same search with the index's own length-aware comparison.
                {
                    const std::string_view s = kv.first.substr(plen);
                    std::size_t lo = 0, hi = tb.size();
                    while (lo < hi) {
                        const std::size_t mid = (lo + hi) / 2;
                        ++bytes_steps;
                        bytes_cmp_bytes += std::min(s.size(), tb[mid].size()) + 1;
                        if (!byte_less(s, tb[mid])) lo = mid + 1;
                        else hi = mid;
                    }
                }
            }''',
'''            // ---- the candidate's own lookup cost, over the region's keys ----
            for (std::size_t i = 0; i < n; ++i) {
                ++queries;
                // FIXED: binary search on 63-bit coordinates, one 8-byte compare per step.
                {
                    std::size_t lo = 0, hi = tf.size();
                    while (lo < hi) {
                        const std::size_t mid = (lo + hi) / 2;
                        ++fixed_steps;
                        fixed_cmp_bytes += sizeof(std::uint64_t);
                        if (tf[mid] <= xf[i]) lo = mid + 1;
                        else hi = mid;
                    }
                }
                // BYTES: the same search with the index's own length-aware comparison. The
                // compared cost is charged as min(len) + 1, i.e. the bytes a memcmp walks plus
                // the length decision.
                {
                    const std::string_view key_suffix = mine[i].substr(plen);
                    std::size_t lo = 0, hi = tb.size();
                    while (lo < hi) {
                        const std::size_t mid = (lo + hi) / 2;
                        ++bytes_steps;
                        bytes_cmp_bytes += std::min(key_suffix.size(), tb[mid].size()) + 1;
                        if (!byte_less(key_suffix, tb[mid])) lo = mid + 1;
                        else hi = mid;
                    }
                }
            }''', 'lookup cost')

open(P, 'w', encoding='utf-8', newline='').write(s)
print('diag patched', hashlib.sha256(open(P,'rb').read()).hexdigest())

# register the target
T = os.path.join(D, 'tools_build.py')
t = open(T, 'r', encoding='utf-8', newline='').read()
a = '''       "check_block_rank": "check_block_rank.cpp",
       "san_check_block_rank": "check_block_rank.cpp",'''
r = a + '''
       # Equal-depth threshold directory: the entry diagnosis. One source, no counters (it reads
       # Region::groups()/group_stats() and does its own arithmetic), no timing.
       "diag_threshold_entry": "diag_threshold_entry.cpp",'''
if t.count(a) != 1:
    raise SystemExit('SRC anchor %d' % t.count(a))
open(T, 'w', encoding='utf-8', newline='').write(t.replace(a, r))
print('tools_build.py registered', hashlib.sha256(open(T,'rb').read()).hexdigest())
