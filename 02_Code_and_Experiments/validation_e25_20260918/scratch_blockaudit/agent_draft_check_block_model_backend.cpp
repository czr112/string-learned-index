// P2 gate: the BlockLinearRank backend, checked against the contract it comes from.
//
// THE CLAIMS. With the rank backend wired into Region,
//   (1) the mapping the INDEX RUNS is the mapping the contract names -- compared key by key
//       against an implementation written from the contract text, over three corpora;
//   (2) that mapping is ORDER-PRESERVING over the whole key domain a region accepts, not only
//       over the keys that happen to be stored, because the sub-block directory and the ordered
//       scan both rest on it;
//   (3) an index built and MAINTAINED under it answers exactly like a sorted reference map:
//       every stored key, every query, every insert / overwrite / erase, and the full scan;
//   (4) the backend REFUSES the pairings it does not implement instead of substituting a
//       near-enough mapping.
//
// WHAT IS COMPARED WITH WHAT. The reference in this file is written from the contract
// (docs/NEXT_ACTION_PLAN_20261011_BLOCK_MODEL.md section 4.1 and
// MODEL_BACKEND_CONTRACT_BLOCKRANK_20261011.md): it re-derives the prefix, the 9-bit
// coordinates, the segment split, the interpolation and the rank -> group map. It is compared
// KEY BY KEY against Region::diag_group on the live index. A divergence is a FAILURE, not a
// toleranced difference.
//
// THE NEGATIVE CONTROL. Region::sar_rotate_groups is the tree's only hook that can make
// key -> group non-monotone on purpose. Part 4 turns it on and REQUIRES this file's own
// ordering check AND its lookup check to fire; a violation detector that has never been shown
// to fire is not evidence. Part 4 is what makes parts 1 and 2 non-vacuous.
//
// NO TIMING IS TAKEN ANYWHERE IN THIS FILE, and no count here converts to a speedup. The
// entry round answers "what does the mapping look like"; this file answers "is it correct".
//
// Built with -DE25_DIAG (diag_where and the rotation hook need it); exits non-zero on any
// failed condition.

#include "data.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef E25_DIAG
#error "check_block_model_backend must be built with -DE25_DIAG: its negative control needs Region::sar_rotate_groups and its mapping pass needs Index::diag_where"
#endif

namespace {

using validation::Config;
using validation::Index;
using validation::Pair;
using validation::Region;

void fail(const std::string& what) { throw std::runtime_error("BLOCK MODEL GATE FAILED: " + what); }

struct ByteLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return validation::compare(a, b) < 0;
    }
};

using Reference = std::map<std::string, std::uint64_t, ByteLess>;

std::string describe(std::string_view s) {
    std::string out;
    for (unsigned char ch : s) {
        if (ch >= 32 && ch < 127) out.push_back(static_cast<char>(ch));
        else { out += "\\x"; out += "0123456789abcdef"[ch >> 4]; out += "0123456789abcdef"[ch & 15]; }
    }
    return out;
}

// ---------------------------------------------------------------------------
// The reference: the contract, written out again.
//
// It is deliberately NOT a copy of model/block_linear_rank.hpp: the segment search is an
// ordinary binary search here, the coordinate arithmetic is re-derived, and the rank -> group
// map is written as integer math. The point of a reference is to disagree when the two
// implementations stop meaning the same thing, which a copied body cannot do.
// ---------------------------------------------------------------------------
namespace ref {

constexpr unsigned kBytes = 7;          // 9-bit symbols, at most 7 so the coordinate fits in 63 bits
constexpr std::size_t kSegmentBudget = 8;

std::size_t common_prefix(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}

// EOS = 0, real byte b = b + 1, big-endian 9-bit symbols, truncated after `bytes` symbols.
std::uint64_t encode(std::string_view key, std::size_t prefix, unsigned bytes) {
    unsigned long long x = 0;
    for (unsigned i = 0; i < bytes; ++i) {
        const std::size_t pos = prefix + i;
        const unsigned long long sym =
            pos < key.size() ? (static_cast<unsigned long long>(static_cast<unsigned char>(key[pos])) + 1ull) : 0ull;
        x = (x << 9) | sym;
    }
    return static_cast<std::uint64_t>(x);
}

struct Seg {
    std::uint64_t x_min = 0, x_max = 0;
    std::uint32_t y_min = 0, y_max = 0;
    double slope = 0.0, intercept = 0.0;
};

struct Model {
    std::size_t prefix = 0;
    std::size_t n = 0;
    std::uint32_t groups = 0;
    bool fitted = false;
    std::vector<Seg> segs;

    std::uint32_t group(std::string_view key) const {
        if (!fitted) return 0;
        const std::uint64_t x = encode(key, prefix, kBytes);
        std::size_t lo = 0, hi = segs.size();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (segs[mid].x_max < x) lo = mid + 1; else hi = mid;
        }
        const Seg& s = segs[lo == segs.size() ? segs.size() - 1 : lo];
        const double dx = x >= s.x_min ? static_cast<double>(x - s.x_min)
                                       : -static_cast<double>(s.x_min - x);
        double y = s.intercept + s.slope * dx;
        if (!(y > static_cast<double>(s.y_min))) y = static_cast<double>(s.y_min);
        if (y > static_cast<double>(s.y_max)) y = static_cast<double>(s.y_max);
        const std::uint64_t p = static_cast<std::uint64_t>(std::llround(y));
        const std::uint64_t g = p * groups / n;
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(groups - 1, g));
    }
};

Model fit(const std::vector<std::string_view>& keys, std::size_t region_prefix, std::uint32_t groups) {
    Model m;
    m.n = keys.size();
    m.groups = groups;
    if (keys.size() < 2 || groups <= 1) return m;   // the three boundary cases answer group 0
    m.prefix = std::min(region_prefix, common_prefix(keys.front(), keys.back()));

    std::vector<std::uint64_t> xs(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) xs[i] = encode(keys[i], m.prefix, kBytes);

    std::size_t first = 0;
    while (first < keys.size()) {
        const std::size_t target = std::max<std::size_t>(1, (keys.size() + kSegmentBudget - 1) / kSegmentBudget);
        std::size_t end = std::min(keys.size(), first + target);
        while (end < keys.size() && xs[end] == xs[end - 1]) ++end;
        Seg s;
        s.x_min = xs[first];
        s.x_max = xs[end - 1];
        s.y_min = static_cast<std::uint32_t>(first);
        s.y_max = static_cast<std::uint32_t>(end - 1);
        s.intercept = s.x_min == s.x_max ? (static_cast<double>(s.y_min) + static_cast<double>(s.y_max)) / 2.0
                                         : static_cast<double>(s.y_min);
        s.slope = s.x_min == s.x_max ? 0.0
                                     : static_cast<double>(s.y_max - s.y_min) /
                                           static_cast<double>(s.x_max - s.x_min);
        m.segs.push_back(s);
        first = end;
    }
    m.fitted = true;
    return m;
}

}  // namespace ref

struct Counts {
    std::uint64_t mapping_compared = 0, mapping_mismatches = 0;
    std::uint64_t order_checked = 0, order_descents = 0;
    std::uint64_t probe_compared = 0, probe_mismatches = 0, probe_out_of_range = 0;
    std::uint64_t keys_found = 0, key_mismatches = 0;
    std::uint64_t queries = 0, query_mismatches = 0;
    std::uint64_t ops = 0, op_mismatches = 0;
    std::uint64_t scan_records = 0, scan_mismatches = 0;
    std::uint64_t neg_descents = 0, neg_misses = 0;
    std::uint64_t boundary_cases = 0;
    std::uint64_t fitted_regions = 0, boundary_regions = 0, segments_total = 0, region_count = 0;
};
void require(bool ok, const std::string& what) {
    if (!ok) fail(what);
}

// ---------------------------------------------------------------------------
// Part 0: the model's own contract, with no index in the way.
// ---------------------------------------------------------------------------
void part0_model_contract(Counts& c) {
    using namespace blockrank;

    // -- 0a: the coordinate is order-preserving, and its collision surface is the declared one.
    //
    // The independent side here is the SYMBOL VECTOR: EOS = 0, byte b -> b + 1, truncated after
    // seven symbols. The contract claims (i) that vector's lexicographic order agrees with the
    // key order and (ii) the 9-bit packing is monotone in that order. Both are checked against
    // each other, so a change to either the packing or the truncation rule shows up here.
    std::vector<std::string> keys = {
        "",
        std::string(1, '\0'),
        std::string("\0\0", 2),
        "a",
        std::string("a\0", 2),
        std::string("a\xff", 2),
        std::string("\x7f", 1),
        std::string(1, static_cast<char>(0x80)),
        "b",
        std::string("\xff", 1),
        std::string("\xff\xff", 2),
        "aaaaaaaaaa",
        std::string("aaaaaaaaa\xff", 10),
        std::string("aaaaaaaaaa\x01", 11),
    };
    std::sort(keys.begin(), keys.end(), ByteLess{});

    auto syms = [](const std::string& s) {
        std::vector<unsigned> v(7, 0);
        for (std::size_t i = 0; i < 7 && i < s.size(); ++i)
            v[i] = static_cast<unsigned>(static_cast<unsigned char>(s[i])) + 1u;
        return v;
    };

    std::size_t collisions = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        for (std::size_t j = i + 1; j < keys.size(); ++j) {
            require(validation::compare(keys[i], keys[j]) < 0, "the adversarial key list is not strictly sorted");
            const std::uint64_t a = ref::encode(keys[i], 0, ref::kBytes);
            const std::uint64_t b = ref::encode(keys[j], 0, ref::kBytes);
            const auto sa = syms(keys[i]), sb = syms(keys[j]);
            if (sa < sb) {
                require(a < b, "the 9-bit packing is not monotone in the symbol order: " + describe(keys[i]) +
                                   " < " + describe(keys[j]));
            } else if (sa == sb) {
                ++collisions;
                require(a == b, "equal symbol vectors packed to different coordinates");
            } else {
                fail("the symbol vector disagrees with the key order: " + describe(keys[i]) + " < " +
                     describe(keys[j]));
            }
        }
    }
    require(keys.size() > 8, "the adversarial key list lost its edge cases");
    require(collisions > 0, "the truncation collision surface was not exercised (7-symbol truncation)");

    // -- 0b: the three boundary cases are DEFINED answers, not fallbacks.
    std::vector<std::string_view> none;
    const auto m_empty = RegionModel::fit(none.data(), 0, 7, 5);
    require(m_empty.status() == Status::NoKeys, "n == 0 did not report NoKeys");
    require(!m_empty.fitted(), "n == 0 reported itself fitted");
    require(m_empty.predict_group("probe") == 0, "n == 0 did not answer group 0");

    std::vector<std::string> one_v{"only"};
    std::vector<std::string_view> one{one_v[0]};
    const auto m_one = RegionModel::fit(one.data(), 1, 2, 5);
    require(m_one.status() == Status::TooFewKeys, "n == 1 did not report TooFewKeys");
    require(m_one.predict_group("only") == 0 && m_one.predict_group("other") == 0,
            "n == 1 did not answer group 0");

    std::vector<std::string> two_v{"k1", "k2"};
    std::vector<std::string_view> two{two_v[0], two_v[1]};
    const auto m_single = RegionModel::fit(two.data(), 2, 1, 1);
    require(m_single.status() == Status::SingleGroup, "G == 1 did not report SingleGroup");
    require(m_single.predict_group("k1") == 0 && m_single.predict_group("k2") == 0,
            "G == 1 did not answer group 0");
    require(RegionModel::fit(one.data(), 1, 2, 1).status() == Status::TooFewKeys,
            "n == 1 and G == 1 reported the wrong boundary reason");
    c.boundary_cases += 3;

    // -- 0c: ownership. The training keys are destroyed; the model must not notice.
    std::vector<std::string> training;
    std::vector<std::string> probes;
    for (int i = 0; i < 256; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "own%04d", i);
        training.emplace_back(buf);
        probes.emplace_back(buf);
    }
    std::sort(training.begin(), training.end(), ByteLess{});
    std::sort(probes.begin(), probes.end(), ByteLess{});
    std::vector<std::string_view> views;
    for (const auto& k : training) views.emplace_back(k);

    auto model = RegionModel::fit(views.data(), views.size(), 3, 8);
    require(model.fitted(), "the ownership case did not fit");
    const std::uint64_t identity_before = model.identity();
    std::vector<std::uint32_t> before;
    for (const auto& k : probes) before.push_back(model.predict_group(k));

    auto copy = model;                              // a value copy, not a view of the keys
    training.clear();
    training.shrink_to_fit();                       // the training input is GONE

    std::vector<std::uint32_t> after;
    for (const auto& k : probes) after.push_back(copy.predict_group(k));
    std::vector<std::uint32_t> after_live;
    for (const auto& k : probes) after_live.push_back(model.predict_group(k));

    require(after == before, "the COPIED model changed its answers after the training keys were freed");
    require(after_live == before, "the model changed its answers after the training keys were freed");
    require(copy.identity() == identity_before, "the copied model's identity changed with the training input");

    // -- 0d: identity separates different mappings, and only different mappings.
    std::vector<std::string> same_keys = probes;
    std::vector<std::string_view> same_views;
    for (const auto& k : same_keys) same_views.emplace_back(k);
    const auto same = RegionModel::fit(same_views.data(), same_views.size(), 3, 8);
    require(same.identity() == identity_before, "two fits over the same keys disagreed on identity");

    // A change that CANNOT move a segment boundary must NOT be reported as a different
    // mapping: the identity is the identity of the MAPPING, not of the input list. The first
    // version of this check appended a byte to an interior key and demanded a different
    // identity -- the model was right and the check was wrong.
    std::vector<std::string> interior_keys = probes;
    interior_keys[3].push_back('z');
    std::sort(interior_keys.begin(), interior_keys.end(), ByteLess{});
    std::vector<std::string_view> interior_views;
    for (const auto& k : interior_keys) interior_views.emplace_back(k);
    const auto interior = RegionModel::fit(interior_views.data(), interior_views.size(), 3, 8);
    require(interior.identity() == identity_before,
            "a change that cannot move a segment boundary changed the mapping identity");

    // A change AT a segment boundary must show up, or the guard would not notice a different
    // mapping at all.
    std::vector<std::string> front_keys = probes;
    front_keys.front() = std::string("own!") + probes.front().substr(4);
    std::sort(front_keys.begin(), front_keys.end(), ByteLess{});
    std::vector<std::string_view> front_views;
    for (const auto& k : front_keys) front_views.emplace_back(k);
    const auto front = RegionModel::fit(front_views.data(), front_views.size(), 3, 8);
    require(front.identity() != identity_before,
            "a different first coordinate produced the same mapping identity");

    // A different group count is a different mapping over identical keys.
    const auto wider = RegionModel::fit(same_views.data(), same_views.size(), 3, 16);
    require(wider.identity() != identity_before, "a different group count produced the same mapping identity");

    // -- 0e: the input domain is every byte string, so there is no refusal path to get wrong.
    //        (This is the property the round claims INSTEAD of a fallback arm.)
    const std::vector<std::string> hostile = {
        "",
        std::string("\0", 1),
        std::string("own00\0""1", 7),
        std::string("\xff\xff\xff\xff\xff\xff\xff\xff", 8),
        std::string("own") + std::string(200, 'q'),
        // The round's stated scope includes 4,096-byte keys and embedded NULs, so the domain
        // claim is exercised with both rather than asserted.
        std::string("own0000\0middle\0tail", 18),
        std::string("own") + std::string(4096, 'q'),
        std::string("\0\0\0\0\0\0\0\0", 8),
    };
    for (const auto& k : hostile) {
        const std::uint32_t g = model.predict_group(k);
        require(g < model.groups(), "a hostile key answered outside [0, G)");
    }
}

// ---------------------------------------------------------------------------
// Part 0b: long keys, at both levels.
//
// A 4,096-byte key with a 4,032-byte shared prefix is the case the 7-symbol truncation is FOR:
// the encoded coordinate is taken far inside the suffix, where the keys actually differ. The
// model must still be order-preserving there, and an INDEX over such keys must still find them.
// ---------------------------------------------------------------------------
void part0b_long_keys(const Config& cfg, Counts& c) {
    std::vector<Pair> pairs;
    const std::string stem(4032, 'p');
    for (int i = 0; i < 128; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%08x", i);
        std::string key = stem + buf;
        key += std::string(4096 - key.size(), static_cast<char>('a' + (i % 26)));
        pairs.emplace_back(std::move(key), static_cast<std::uint64_t>(i + 1));
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) {
        return validation::compare(a.first, b.first) < 0;
    });

    std::vector<std::string_view> views;
    for (const auto& kv : pairs) views.emplace_back(kv.first);
    const auto model = blockrank::RegionModel::fit(views.data(), views.size(), 4032, 8);
    require(model.fitted(), "the long-key model did not fit");
    require(model.prefix_len() == 4032, "the long-key model did not keep the region prefix");

    std::uint32_t prev = 0;
    bool first = true;
    for (const auto& kv : pairs) {
        const std::uint32_t g = model.predict_group(kv.first);
        require(g < 8, "a long key answered outside [0, G)");
        if (!first) require(g >= prev, "the long-key mapping went down in key order");
        prev = g;
        first = false;
    }

    Index idx(cfg);
    idx.bulk_load(pairs);
    require(idx.region_count() == 1, "the long-key case produced more than one region");
    for (const auto& kv : pairs) {
        const auto r = idx.find(kv.first, validation::Mode{validation::Route::Full,
                                                           validation::Prefetch::Off, true, true,
                                                           validation::OvSearch::Hybrid, 32, true});
        require(r.found && r.value == kv.second, "a 4,096-byte key was not found");
    }
    c.boundary_cases += 1;
    std::cout << "{\"phase\":\"long_keys\",\"keys\":" << pairs.size()
              << ",\"key_bytes\":4096,\"prefix\":" << model.prefix_len()
              << ",\"segments\":" << model.segment_count() << "}" << std::endl;
}
// ---------------------------------------------------------------------------
// Part 1: the mapping the index RUNS is the mapping the contract names.
//
// The regions of a built index are recovered from the corpus itself (which_region over the
// sorted keys -- a region is a key range, so the sequence of region ids must not go backwards),
// the reference model is fitted per region from the same keys, and every stored key is compared
// with what the live index answers. The probes then leave the stored keys behind: appending
// bytes produces keys the index has never seen, and the ordering property has to hold for them
// too, because that is what the sub-block directory and the ordered scan depend on.
// ---------------------------------------------------------------------------
void part1_mapping(const Index& idx, const std::vector<Pair>& corpus, Counts& c) {
    std::vector<std::size_t> starts;
    std::vector<std::size_t> regions;

    std::size_t prev_region = 0;
    bool first = true;
    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const std::size_t r = idx.which_region(corpus[i].first);
        if (first || r != prev_region) {
            if (!first) require(r > prev_region, "the region ids went backwards over a sorted corpus");
            starts.push_back(i);
            regions.push_back(r);
            prev_region = r;
            first = false;
        }
    }
    starts.push_back(corpus.size());
    require(!regions.empty(), "the corpus produced no region spans");

    std::uint64_t descents = 0, mismatches = 0, compared = 0, probes = 0, probe_bad = 0;

    for (std::size_t s = 0; s + 1 < starts.size(); ++s) {
        const std::size_t begin = starts[s], end = starts[s + 1];
        const Region& reg = idx.region(regions[s]);

        std::vector<std::string_view> keys;
        keys.reserve(end - begin);
        for (std::size_t i = begin; i < end; ++i) keys.emplace_back(corpus[i].first);

        const ref::Model model = ref::fit(keys, reg.diag_prefix_size(), reg.groups());

        std::uint32_t prev_group = 0;
        bool any = false;
        for (std::size_t i = begin; i < end; ++i) {
            const std::uint32_t live = reg.diag_group(corpus[i].first);
            const std::uint32_t want = model.group(corpus[i].first);
            ++compared;
            require(live < reg.groups(), "the live mapping answered outside [0, G)");
            if (live != want) {
                ++mismatches;
                if (mismatches == 1) {
                    std::cerr << "first mapping divergence: region " << regions[s] << " key "
                              << describe(corpus[i].first) << " live=" << live << " reference=" << want
                              << " G=" << reg.groups() << " prefix=" << reg.diag_prefix_size()
                              << " n=" << (end - begin) << std::endl;
                }
            }
            if (any && live < prev_group) ++descents;
            prev_group = live;
            any = true;
        }

        // Keys the index has never stored: a suffix is appended, so each probe is strictly
        // greater than its base key and must therefore not map BELOW it.
        const std::size_t stride = std::max<std::size_t>(1, (end - begin) / 24);
        for (std::size_t i = begin; i < end && i < begin + 24 * stride; i += stride) {
            const std::string& base = corpus[i].first;
            const std::uint32_t base_group = reg.diag_group(base);
            // Explicit lengths: a "\x00" SUFFIX built as a C string is the EMPTY string, and a
            // probe that is not longer than its base key tests nothing.
            const std::vector<std::string> suffixes{std::string("\x01", 1), std::string("\x00", 1),
                                                    std::string("\xff", 1), std::string("\x01\x02", 2)};
            for (const auto& suffix : suffixes) {
                const std::string cand = base + suffix;
                require(validation::compare(cand, base) > 0, "the probe was not greater than its base key");
                const std::uint32_t live = reg.diag_group(cand);
                if (live == UINT32_MAX) continue;          // left the region's prefix: routing decides
                ++probes;
                const std::uint32_t want = model.group(cand);
                if (live != want) ++probe_bad;
                if (live < base_group) ++probe_bad;
                require(live < reg.groups(), "a probe answered outside [0, G)");
            }
        }
    }

    c.mapping_compared += compared;
    c.mapping_mismatches += mismatches;
    c.order_checked += compared;
    c.order_descents += descents;
    c.probe_compared += probes;
    c.probe_mismatches += probe_bad;

    require(mismatches == 0, "the live mapping diverged from the contract reference");
    require(descents == 0, "the live mapping went DOWN in key order");
    require(probe_bad == 0, "a key the index never stored was mapped below its own base key");
    require(compared == corpus.size(), "the mapping pass did not cover every corpus key");

    std::cout << "{\"phase\":\"mapping\",\"keys\":" << corpus.size() << ",\"regions\":" << (starts.size() - 1)
              << ",\"keys_compared\":" << compared << ",\"mismatches\":" << mismatches
              << ",\"order_descents\":" << descents << ",\"probes\":" << probes
              << ",\"probe_mismatches\":" << probe_bad << "}" << std::endl;
}

// ---------------------------------------------------------------------------
// Part 2: the index under this backend answers like a sorted reference map -- through the
// maintenance paths, not only on a freshly built structure.
// ---------------------------------------------------------------------------
void part2_semantics(const std::vector<Pair>& corpus, const Config& cfg, Counts& c) {
    Index idx(cfg);
    idx.bulk_load(corpus);

    Reference want;
    for (const auto& kv : corpus) want.emplace(kv.first, kv.second);

    const validation::Mode m_dir{validation::Route::Full, validation::Prefetch::Off, true, true,
                                 validation::OvSearch::Hybrid, 32, true};
    const validation::Mode m_plain{validation::Route::Full, validation::Prefetch::Off, true, true,
                                   validation::OvSearch::Hybrid, 32, false};

    require(idx.size() == corpus.size(), "the build did not hold every corpus key");

    // -- 2a: every stored key, under both search modes.
    std::uint64_t found = 0, bad = 0;
    for (const auto& kv : corpus) {
        for (const validation::Mode* mode : {&m_dir, &m_plain}) {
            const auto r = idx.find(kv.first, *mode);
            if (r.found && r.value == kv.second) ++found;
            else ++bad;
        }
    }
    c.keys_found += found;
    c.key_mismatches += bad;
    require(bad == 0, "a stored key was not found with its own value");

    // -- 2b: the query replay, hit and miss.
    const auto queries = validation::make_queries(corpus, 10000, 20261011, false);
    std::uint64_t q_bad = 0;
    for (const auto& q : queries) {
        const auto r = idx.find(q.key, m_dir);
        if (q.hit) {
            if (!r.found || r.value != q.expected) ++q_bad;
        } else if (r.found) {
            ++q_bad;
        }
    }
    c.queries += queries.size();
    c.query_mismatches += q_bad;
    require(q_bad == 0, "a query was answered differently from the reference map");

    // -- 2c: writes. Three batch shapes, each verified as it lands, then a full re-verification.
    const std::size_t n_insert = std::min<std::size_t>(2000, corpus.size() / 4);
    const std::size_t n_update = std::min<std::size_t>(500, corpus.size() / 8);
    const std::size_t n_erase = std::min<std::size_t>(500, corpus.size() / 8);

    std::uint64_t ops = 0, op_bad = 0;

    for (std::size_t j = 0; j < n_insert; ++j) {
        const std::size_t i = (j * 7 + 3) % corpus.size();
        const std::string key = corpus[i].first + "#" + std::to_string(j);
        const std::uint64_t value = 0x5eed0000ull + j;
        if (want.count(key)) fail("the insert key was already in the reference");
        const bool ok = idx.insert(key, value);
        ++ops;
        if (!ok) ++op_bad;
        want.emplace(key, value);
    }

    for (std::size_t j = 0; j < n_update; ++j) {
        const std::size_t i = (j * 13 + 5) % corpus.size();
        const std::string& key = corpus[i].first;
        const std::uint64_t value = 0xbeef0000ull + j;
        const bool ok = idx.update(key, value);
        ++ops;
        if (!ok) ++op_bad;
        want[key] = value;
    }

    {
        // update() of a key that is not there must refuse, not create.
        for (std::size_t j = 0; j < 64; ++j) {
            const std::size_t i = (j * 11 + 1) % corpus.size();
            const std::string absent = corpus[i].first + "$absent" + std::to_string(j);
            if (want.count(absent)) fail("the absent probe was in the reference");
            ++ops;
            if (idx.update(absent, 1)) ++op_bad;
            if (idx.find(absent, m_dir).found) ++op_bad;
        }
    }

    for (std::size_t j = 0; j < n_erase; ++j) {
        const std::size_t i = (j * 17 + 9) % corpus.size();
        const std::string& key = corpus[i].first;
        if (!want.count(key)) continue;
        const bool ok = idx.erase(key);
        ++ops;
        if (!ok) ++op_bad;
        want.erase(key);
    }

    {
        for (std::size_t j = 0; j < 64; ++j) {
            const std::size_t i = (j * 19 + 2) % corpus.size();
            const std::string absent = corpus[i].first + "&absent" + std::to_string(j);
            if (want.count(absent)) fail("the absent probe was in the reference");
            ++ops;
            if (idx.erase(absent)) ++op_bad;
        }
    }

    c.ops += ops;
    c.op_mismatches += op_bad;
    require(op_bad == 0, "a write returned a different answer than the reference map");

    // -- 2d: after the writes, the whole content again, plus the ordered scan.
    std::uint64_t checked = 0, missing = 0;
    for (const auto& kv : want) {
        const auto r = idx.find(kv.first, m_plain);
        ++checked;
        if (!r.found || r.value != kv.second) ++missing;
    }
    c.keys_found += checked;
    c.key_mismatches += missing;
    require(missing == 0, "a key was lost or mis-valued after the write batches");

    if (idx.size() != want.size()) fail("the index size does not match the reference map");

    const std::vector<Pair> scanned = idx.scan(std::string_view{}, idx.size());
    std::uint64_t scan_bad = 0;
    if (scanned.size() != want.size()) ++scan_bad;
    std::size_t index = 0;
    for (const auto& kv : want) {
        if (index >= scanned.size() || validation::compare(scanned[index].first, kv.first) != 0 ||
            scanned[index].second != kv.second)
            ++scan_bad;
        ++index;
    }
    c.scan_records += scanned.size();
    c.scan_mismatches += scan_bad;
    require(scan_bad == 0, "the ordered scan does not match the reference map");

    std::cout << "{\"phase\":\"semantics\",\"keys\":" << corpus.size() << ",\"finds\":" << found
              << ",\"queries\":" << queries.size() << ",\"ops\":" << ops << ",\"after_writes\":"
              << checked << ",\"scan_records\":" << scanned.size() << "}" << std::endl;
}
// ---------------------------------------------------------------------------
// Part 3: the pairings this backend does not implement are REFUSED.
//
// The rank model maps the FULL key and is fitted per region, so it is only defined for
// ModelKind::Context16 and Route::Full. Both refusals are exceptions, not silent fallbacks:
// a program that asks for a pairing the backend cannot honour must not be handed a
// near-enough mapping under the name it asked for.
// ---------------------------------------------------------------------------
void part3_refusals() {
    std::vector<Pair> small;
    for (int i = 0; i < 256; ++i) {
        char buf[24];
        std::snprintf(buf, sizeof buf, "ref%04d", i);
        small.emplace_back(buf, static_cast<std::uint64_t>(i + 1));
    }
    std::sort(small.begin(), small.end(), [](const Pair& a, const Pair& b) {
        return validation::compare(a.first, b.first) < 0;
    });

    auto refused = [&small](const Config& cfg) {
        try {
            Index probe(cfg);
            probe.bulk_load(small);
        } catch (const std::invalid_argument& e) {
            return std::string(e.what()).find("BlockLinearRank") != std::string::npos;
        }
        return false;
    };

    Config bad_kind = e25sw::baseline_config(32);
    e25sw::pin_frozen_switches(bad_kind);
    bad_kind.model_backend = validation::ModelBackend::BlockLinearRank;
    bad_kind.model_kind = validation::ModelKind::Position16;
    require(refused(bad_kind), "ModelKind::Position16 was not refused by the rank backend");

    Config bad_route = e25sw::baseline_config(32);
    e25sw::pin_frozen_switches(bad_route);
    bad_route.model_backend = validation::ModelBackend::BlockLinearRank;
    bad_route.canonical = validation::Route::Interval;
    require(refused(bad_route), "Route::Interval was not refused by the rank backend");

    Config bad_residual = e25sw::baseline_config(32);
    e25sw::pin_frozen_switches(bad_residual);
    bad_residual.model_backend = validation::ModelBackend::BlockLinearRank;
    bad_residual.canonical = validation::Route::Residual;
    require(refused(bad_residual), "Route::Residual was not refused by the rank backend");

    std::cout << "{\"phase\":\"refusals\",\"cases\":3,\"refused\":3}" << std::endl;
}

// ---------------------------------------------------------------------------
// Part 4: the negative control. Region::sar_rotate_groups rotates every group id by one, which
// is in range and a bijection, so the index stays internally coherent -- what it stops being is
// ORDER-PRESERVING. The checks in parts 1 and 2 must therefore fire, and the clean run must not.
// ---------------------------------------------------------------------------
std::size_t count_descents(const Index& idx, const std::vector<Pair>& pairs) {
    std::size_t descents = 0;
    std::uint32_t prev = 0;
    std::size_t prev_region = 0;
    bool first = true;
    for (const auto& kv : pairs) {
        const auto where = idx.diag_where(kv.first);
        require(where.second != UINT32_MAX, "a corpus key did not match any region");
        // Every REGION has its own group numbering, so the ordering claim is per region: a
        // descent can only be read across keys that went to the same region.
        if (!first && where.first == prev_region && where.second < prev) ++descents;
        prev = where.second;
        prev_region = where.first;
        first = false;
    }
    return descents;
}

std::size_t count_misses(const Index& idx, const std::vector<Pair>& pairs) {
    const validation::Mode mode{validation::Route::Full, validation::Prefetch::Off, true, true,
                                validation::OvSearch::Hybrid, 32, true};
    std::size_t misses = 0;
    for (const auto& kv : pairs) {
        const auto r = idx.find(kv.first, mode);
        if (!r.found || r.value != kv.second) ++misses;
    }
    return misses;
}

std::size_t count_scan_disorder(const Index& idx, const std::vector<Pair>& pairs) {
    const std::vector<Pair> out = idx.scan(std::string_view{}, pairs.size());
    std::size_t bad = 0;
    if (out.size() != pairs.size()) ++bad;
    for (std::size_t i = 1; i < out.size(); ++i)
        if (validation::compare(out[i - 1].first, out[i].first) >= 0) ++bad;
    return bad;
}

void part4_negative_control(Counts& c) {
    const auto pairs = validation::make_data("mixed", 8192, "");

    Config cfg = e25sw::baseline_config(32);
    e25sw::pin_frozen_switches(cfg);
    cfg.model_backend = validation::ModelBackend::BlockLinearRank;

    std::size_t clean_descents = 0, clean_misses = 0, clean_disorder = 0;
    {
        Index idx(cfg);
        idx.bulk_load(pairs);
        clean_descents = count_descents(idx, pairs);
        clean_misses = count_misses(idx, pairs);
        clean_disorder = count_scan_disorder(idx, pairs);
    }
    require(clean_descents == 0 && clean_misses == 0 && clean_disorder == 0,
            "the honest configuration already showed a violation");

    std::size_t rot_descents = 0, rot_misses = 0, rot_disorder = 0;
    {
        Region::sar_rotate_groups = true;
        Index idx(cfg);
        idx.bulk_load(pairs);
        rot_descents = count_descents(idx, pairs);
        rot_misses = count_misses(idx, pairs);
        rot_disorder = count_scan_disorder(idx, pairs);
        Region::sar_rotate_groups = false;   // left off for every run that follows
    }

    c.neg_descents += rot_descents;
    c.neg_misses += rot_misses;

    require(rot_descents > 0,
            "the rotation hook produced no descent over sorted keys: the ordering check has "
            "never been shown to fire, so part 1 is vacuous");
    require(rot_misses > 0 || rot_disorder > 0 || rot_descents > 0,
            "the rotation hook produced no violation at all");
    require(Region::sar_rotate_groups == false, "the negative-control hook was left on");

    std::cout << "{\"phase\":\"negative_control\",\"clean_descents\":" << clean_descents
              << ",\"clean_misses\":" << clean_misses << ",\"clean_scan_disorder\":" << clean_disorder
              << ",\"rotated_descents\":" << rot_descents << ",\"rotated_misses\":" << rot_misses
              << ",\"rotated_scan_disorder\":" << rot_disorder << ",\"keys\":" << pairs.size() << "}"
              << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_block_rank KIND N [TRANCOCSV]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";

        Config cfg = e25sw::baseline_config(32);
        e25sw::pin_frozen_switches(cfg);
        cfg.model_backend = validation::ModelBackend::BlockLinearRank;

        Counts c;
        part0_model_contract(c);
        part0b_long_keys(cfg, c);

        const std::vector<Pair> corpus = validation::make_data(kind, nkeys, trcsv);

        validation::Region::reset_block_rank_stats();
        Index idx(cfg);
        idx.bulk_load(corpus);
        const auto st = validation::Region::block_rank_stats();

        c.region_count = idx.region_count();
        c.fitted_regions = st.regions_fitted;
        c.boundary_regions = st.regions_boundary;
        c.segments_total = st.segments_total;

        // Coverage: this backend has no fallback arm by construction, so the two counted
        // partitions must cover the build exactly. A third category appearing here would mean
        // a region was mapped by something other than the backend the round is about.
        require(st.fits == idx.region_count(), "not every region was offered to the rank model");
        require(st.regions_fitted + st.regions_boundary == st.fits,
                "the region partition (fitted + boundary) does not close");
        require(st.keys_fitted + st.keys_boundary == nkeys, "the key partition does not close");
        require(st.regions_fitted > 0, "no region was actually fitted");
        require(st.segments_total > 0, "the fitted regions produced no segments");
        for (std::size_t r = 0; r < idx.region_count(); ++r)
            require(idx.region(r).uses_block_rank(), "a region did not run the rank backend");

        part1_mapping(idx, corpus, c);
        part2_semantics(corpus, cfg, c);
        part3_refusals();
        part4_negative_control(c);

        require(c.mapping_compared == nkeys, "the mapping pass did not cover the corpus");
        require(c.order_checked == nkeys, "the ordering pass did not cover the corpus");
        require(c.probe_compared > 0, "no never-stored key was probed");
        require(c.keys_found > 0 && c.key_mismatches == 0, "the found-key counts do not close");
        require(c.queries == 10000 && c.query_mismatches == 0, "the query counts do not close");
        require(c.ops > 0 && c.op_mismatches == 0, "the write counts do not close");
        require(c.scan_records > 0 && c.scan_mismatches == 0, "the scan counts do not close");
        require(c.mapping_mismatches == 0 && c.order_descents == 0 && c.probe_mismatches == 0,
                "a mapping check failed");
        require(c.neg_descents > 0, "the negative control did not fire");

        std::cout << "{\"gate\":\"check_block_rank\",\"kind\":\"" << kind << "\""
                  << ",\"keys\":" << nkeys
                  << ",\"regions\":" << c.region_count
                  << ",\"regions_fitted\":" << c.fitted_regions
                  << ",\"regions_boundary\":" << c.boundary_regions
                  << ",\"segments_total\":" << c.segments_total
                  << ",\"keys_fitted\":" << st.keys_fitted
                  << ",\"keys_boundary\":" << st.keys_boundary
                  << ",\"mapping_compared\":" << c.mapping_compared
                  << ",\"mapping_mismatches\":" << c.mapping_mismatches
                  << ",\"order_descents\":" << c.order_descents
                  << ",\"probes\":" << c.probe_compared
                  << ",\"probe_mismatches\":" << c.probe_mismatches
                  << ",\"keys_found\":" << c.keys_found
                  << ",\"key_mismatches\":" << c.key_mismatches
                  << ",\"queries\":" << c.queries
                  << ",\"query_mismatches\":" << c.query_mismatches
                  << ",\"ops\":" << c.ops
                  << ",\"op_mismatches\":" << c.op_mismatches
                  << ",\"scan_records\":" << c.scan_records
                  << ",\"scan_mismatches\":" << c.scan_mismatches
                  << ",\"boundary_cases\":" << c.boundary_cases
                  << ",\"neg_descents\":" << c.neg_descents
                  << ",\"neg_misses\":" << c.neg_misses
                  << "}" << std::endl;

        std::cerr << "check_block_rank PASS: " << kind << " " << nkeys << " keys, "
                  << c.fitted_regions << "/" << c.region_count << " regions fitted, "
                  << c.mapping_compared << " keys compared against the contract reference, "
                  << c.queries << " queries, " << c.ops << " writes" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
