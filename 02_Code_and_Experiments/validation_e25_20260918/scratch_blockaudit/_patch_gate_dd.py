import hashlib, os
P = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\check_depth_dir.cpp'
s = open(P, 'r', encoding='utf-8', newline='').read()

def sub(a, r, label):
    global s
    if s.count(a) != 1:
        raise SystemExit('ANCHOR FAIL %s (%d)' % (label, s.count(a)))
    s = s.replace(a, r)

# 1) range probes must stay INSIDE the accepted domain (a shorter key is refused by
#    prefix_matches before any model is consulted).
sub('''    // Boundary conditions on the same synthetic set.
    require(r.placement_group("aaaa") == 0, "key below the range lands in group 0");
    require(r.placement_group("zzzz") == 7, "key above the range lands in the last group");''',
'''    // Range probes: both stay INSIDE the region's prefix domain (a key shorter than the prefix
    // never reaches a model -- prefix_matches refuses it first), so these test the count rule,
    // not the domain.
    require(r.placement_group("synthetic!!!!") == 0, "key below the range lands in group 0");
    require(r.placement_group("syntheticZZZZ") == 7, "key above the range lands in the last group");''',
    'range probes')

# 2) domain suite + refusals
sub('''}  // namespace

int main(int argc, char** argv) {''',
'''// ---------------------------------------------------------------- domain + refusals
void check_domain_and_refusals() {
    std::vector<std::string> domain;
    domain.emplace_back("");
    domain.push_back(std::string(1, '\\0'));
    domain.push_back(std::string("\\0\\0", 2));
    domain.push_back(std::string(1, static_cast<char>(0x80)));
    domain.push_back(std::string(1, static_cast<char>(0xff)));
    domain.push_back(std::string(1, static_cast<char>(0x80)) + "tail");
    domain.push_back(std::string(4096, 'L'));
    domain.push_back(std::string(4095, 'L') + "M");
    for (int i = 0; i < 40; ++i)
        domain.push_back(std::string("common-prefix-64-bytes-common-prefix-64-bytes-common-prefix") +
                         static_cast<char>('a' + i % 26) + char('0' + i % 10));
    for (int i = 0; i < 20; ++i)
        domain.push_back(std::string("a") + "0123456" + static_cast<char>('a' + i));

    std::sort(domain.begin(), domain.end());
    domain.erase(std::unique(domain.begin(), domain.end()), domain.end());
    std::vector<Pair> pairs;
    for (std::size_t i = 0; i < domain.size(); ++i) pairs.emplace_back(domain[i], i + 1);

    Config c = e25sw::baseline_config(32);
    c.model_backend = ModelBackend::EqualDepthDir;
    c.multiblock = true;
    c.reuse_group_ids = true;
    c.region_keys = 8;   // several small regions, including single-key ones

    Index idx(c);
    idx.bulk_load(pairs);

    const Mode full{Route::Full, Prefetch::Off, true, false, OvSearch::Hybrid, 32, false};
    const Mode interval{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};
    for (const auto& kv : pairs) {
        const auto a = idx.find(kv.first, full);
        const auto b = idx.find(kv.first, interval);
        require(a.found && a.value == kv.second, "domain find (full route)");
        require(b.found && b.value == kv.second, "domain find (interval route)");
    }
    for (const auto& kv : pairs) {
        const std::string absent = kv.first + "~absent";
        require(!idx.find(absent, interval).found, "domain absent probe is a miss");
    }

    std::vector<Pair> three{{"aa", 1}, {"ab", 2}, {"ac", 3}};
    {
        Config bad = c;
        bad.canonical = Route::Interval;
        bool threw = false;
        try { Index t(bad); t.bulk_load(three); } catch (const std::invalid_argument&) { threw = true; }
        require(threw, "Route::Interval is refused by the directory backend");
    }
    {
        Config bad = c;
        bad.model_kind = ModelKind::Unigram;
        bool threw = false;
        try { Index t(bad); t.bulk_load(three); } catch (const std::invalid_argument&) { threw = true; }
        require(threw, "ModelKind::Unigram is refused by the directory backend");
    }
}

// ---------------------------------------------------------------- dynamic
void check_dynamic(const std::string& kind, const std::string& trcsv) {
    std::vector<Pair> keys = make_data(kind, 32768, trcsv);
    std::sort(keys.begin(), keys.end(),
              [](const Pair& a, const Pair& b) { return a.first < b.first; });

    Config c = e25sw::baseline_config(32);
    c.model_backend = ModelBackend::EqualDepthDir;
    Index idx(c);
    idx.bulk_load(keys);
    const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};

    std::vector<std::string> added;
    std::uint64_t bad = 0;
    for (std::size_t i = 0; i < 1500; ++i) {
        const std::string k = "insert-" + std::to_string(i) + "-" + keys[i % keys.size()].first;
        added.push_back(k);
        if (!idx.insert(k, 900000 + i)) ++bad;
    }
    for (std::size_t i = 0; i < 700; ++i)
        if (!idx.erase(added[i])) ++bad;
    for (std::size_t i = 700; i < added.size(); ++i) {
        const auto r = idx.find(added[i], qmode);
        if (!r.found || r.value != 900000 + i) ++bad;
    }
    std::uint64_t survivors = 0;
    for (std::size_t i = 0; i < 700; ++i)
        if (idx.find(added[i], qmode).found) ++survivors;
    for (const auto& kv : keys)
        if (!idx.find(kv.first, qmode).found) ++bad;

    require(bad == 0, "dynamic insert/erase/re-find answers are exact");
    require(survivors == 0, "erased keys are gone");
}

}  // namespace

int main(int argc, char** argv) {''', 'domain/dynamic')

# 3) N from argv
sub('''    std::vector<Pair> keys = make_data(kind, std::stoull("131072"), trcsv);''',
    '''    std::vector<Pair> keys = make_data(kind, kN, trcsv);''', 'k n')
sub('''void check_corpus(const std::string& kind, const std::string& trcsv, std::uint32_t thr) {''',
    '''void check_corpus(const std::string& kind, const std::string& trcsv, std::uint32_t thr,
                  std::size_t kN) {''', 'k n sig')

sub('''        check_synthetic_depth();
        check_duplicate_keys();
        check_corpus(kind, trcsv, thr);''',
'''        check_synthetic_depth();
        check_duplicate_keys();
        check_domain_and_refusals();
        check_dynamic(kind, trcsv);
        check_corpus(kind, trcsv, thr, std::stoull(argv[2]));''', 'main calls')

# 4) drop the unused worst counter
sub('''    std::size_t worst = 0;
    for (std::size_t i = 0; i < dir_region.size() && i < ref_region.size(); ++i)
        if (ref_region[i] && (dir_region[i] * 2 < ref_region[i] * 3)) ++worst;

''', '', 'worst')

open(P, 'w', encoding='utf-8', newline='').write(s)
print('gate patched', len(s), hashlib.sha256(open(P,'rb').read()).hexdigest())

# register
T = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\tools_build.py'
t = open(T, 'r', encoding='utf-8', newline='').read()
a = '''       "diag_threshold_entry": "diag_threshold_entry.cpp",'''
r = a + '''
       # EqualDepthDir: one source, two targets (gate + sanitizer twin).
       "check_depth_dir": "check_depth_dir.cpp",
       "san_check_depth_dir": "check_depth_dir.cpp",'''
if t.count(a) != 1:
    raise SystemExit('SRC anchor %d' % t.count(a))
t = t.replace(a, r)
a2 = '''    "check_block_rank": ["-DE12_COUNT"],
    "san_check_block_rank": ["-DE12_COUNT"],'''
r2 = a2 + '''
    # EqualDepthDir gate: it reports the entry counters (primary probes, comparisons, overflow),
    # which are written through E12_TICK -- a no-op in a shipped build.
    "check_depth_dir": ["-DE12_COUNT"],
    "san_check_depth_dir": ["-DE12_COUNT"],'''
if t.count(a2) != 1:
    raise SystemExit('EXTRA anchor %d' % t.count(a2))
t = t.replace(a2, r2)
a3 = '''    "san_check_block_rank",
}'''
r3 = '''    "san_check_block_rank",
    "san_check_depth_dir",
}'''
if t.count(a3) != 1:
    raise SystemExit('SAN anchor %d' % t.count(a3))
t = t.replace(a3, r3)
open(T, 'w', encoding='utf-8', newline='').write(t)
print('tools_build.py', hashlib.sha256(open(T,'rb').read()).hexdigest())
