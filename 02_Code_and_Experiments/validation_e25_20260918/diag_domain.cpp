// E38 / P0: the data-domain audit for the LITS HPT adapter round.
//
// WHY THIS PROGRAM EXISTS.
//
// The pinned LITS snapshot has a FIXED input domain, and none of it is a property of
// this index's keys:
//
//   MAX_CH = 128, NUL-terminated      lits_base.hpp
//   a 256-entry weight array          lits_model.hpp
//   the global common prefix kept in a uint8_t
//   a training path that reads keys[1], and a prediction path that reads the byte
//   before position 0
//
// The task book forbids silently truncating or dropping keys to fit ("不得为了通过而默默
// 截断或删掉长键"), and it requires the lengths, byte range, NUL count, empty keys and
// duplicates to be counted BEFORE any formal run. So the domain is measured here
// rather than assumed.
//
// WHAT IT COVERS. Every key the round will feed the model:
//
//   build      the initial key set, and the region prefixes derived from it
//   query      the hit population (a sample of the build set) and the miss keys
//   update     both replacement families the long workload generates
//
// WHAT IT DOES NOT DO. No index is searched and nothing is predicted; the only index
// object built is a Region per chunk, and only to read back the REAL prefix the
// constructor computed rather than a replica of that rule. No timing, and no
// diagnostic counters -- this program answers one question and cannot answer a
// performance one.
//
// THE MISS GENERATOR IS A CHOICE, NOT A DEFAULT. Two families exist in this tree and
// they differ in the byte domain: data.hpp's make_queries appends '\0', while the
// E34-E37 benchmarks append "~absent". This program measures the latter, because the
// LITS round is derived from bench_migrate/bench_tag and inherits their generator. The
// NUL-appending family is reported too, on the same keys, so the difference is on the
// record instead of being an assumption about which program ran.

#include "data.hpp"

#include <algorithm>
#include <deque>
#include <iostream>
#include <sstream>
#include <unordered_set>

namespace {

using validation::Pair;

// One population's shape. The byte counters are per KEY ("how many keys contain a byte
// like this"), not per byte, because that is the unit the domain restriction applies to:
// one key with a byte >= 128 is one key LITS's tables cannot index.
struct Domain {
    std::size_t n = 0;
    std::size_t len_min = 0;
    std::size_t len_max = 0;
    double len_mean = 0.0;
    std::size_t len_p50 = 0;
    std::size_t len_p95 = 0;
    std::size_t len_p99 = 0;
    std::size_t len_over128 = 0;
    std::size_t len_over255 = 0;
    std::size_t with_high = 0;    // contains a byte >= 128
    std::size_t with_ctl = 0;     // contains a byte < 32
    std::size_t with_nul = 0;
    std::size_t empty = 0;
    unsigned max_byte = 0;
};

// Nearest-rank on the sorted length vector. With a population this size the choice of
// definition moves a percentile by at most one byte, and the number that actually
// decides the domain question -- the maximum -- is exact either way.
std::size_t pick(const std::vector<std::size_t>& sorted, double q) {
    if (sorted.empty()) return 0;

    auto idx = static_cast<std::size_t>(q * double(sorted.size() - 1) + 0.5);
    if (idx >= sorted.size()) idx = sorted.size() - 1;

    return sorted[idx];
}

// `get(i)` returns the i-th key as a view, so the same measurement runs over vector<Pair>
// and over vector<string> without copying either one.
template <class Get>
Domain measure(std::size_t n, Get get) {
    Domain d;
    d.n = n;

    std::vector<std::size_t> lens;
    lens.reserve(n);

    std::uint64_t total = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::string_view k = get(i);
        const std::size_t len = k.size();

        lens.push_back(len);
        total += len;

        if (len == 0) ++d.empty;
        if (len > 128) ++d.len_over128;
        if (len > 255) ++d.len_over255;

        bool high = false, ctl = false, nul = false;
        for (unsigned char c : k) {
            if (c > d.max_byte) d.max_byte = c;
            if (c >= 128) high = true;
            if (c < 32) ctl = true;
            if (c == 0) nul = true;
        }
        if (high) ++d.with_high;
        if (ctl) ++d.with_ctl;
        if (nul) ++d.with_nul;
    }

    if (n) d.len_mean = double(total) / double(n);

    std::sort(lens.begin(), lens.end());
    d.len_min = lens.empty() ? 0 : lens.front();
    d.len_max = lens.empty() ? 0 : lens.back();
    d.len_p50 = pick(lens, 0.50);
    d.len_p95 = pick(lens, 0.95);
    d.len_p99 = pick(lens, 0.99);

    return d;
}

void emit(std::ostream& o, const char* section, const Domain& d) {
    o << "{\"section\":\"" << section << "\""
      << ",\"n\":" << d.n
      << ",\"len_min\":" << d.len_min
      << ",\"len_mean\":" << d.len_mean
      << ",\"len_p50\":" << d.len_p50
      << ",\"len_p95\":" << d.len_p95
      << ",\"len_p99\":" << d.len_p99
      << ",\"len_max\":" << d.len_max
      << ",\"len_over128\":" << d.len_over128
      << ",\"len_over255\":" << d.len_over255
      << ",\"with_high\":" << d.with_high
      << ",\"with_ctl\":" << d.with_ctl
      << ",\"with_nul\":" << d.with_nul
      << ",\"empty\":" << d.empty
      << ",\"max_byte\":" << d.max_byte
      << "}" << std::endl;
}

// The long workload's replacement rule, copied from bench_long.cpp: anchor on the victim
// with any previous generated suffix stripped, then either append "~r<serial>" (grow) or
// change the last byte until the key is unused (same). Only the KEY TEXT matters here, so
// the values and the timing are not reproduced -- but the membership test is, because it
// decides which byte the same-length arm settles on.
std::vector<std::string> update_keys(const std::vector<Pair>& keys, const std::string& mode,
                                     double frac) {
    std::deque<std::string> live;
    std::unordered_set<std::string> live_set;
    std::unordered_set<std::string> ref;

    for (const auto& kv : keys) {
        live.push_back(kv.first);
        live_set.insert(kv.first);
        ref.insert(kv.first);
    }

    const auto target = static_cast<std::size_t>(double(keys.size()) * frac);

    std::vector<std::string> out;
    out.reserve(target);

    std::uint64_t serial = 0;
    for (std::size_t i = 0; i < target; ++i) {
        const std::string victim = live.front();
        live.pop_front();
        live_set.erase(victim);

        std::string base = victim;
        const auto pos = base.find("~r");
        if (pos != std::string::npos) base.resize(pos);

        std::string fresh;
        if (mode == "grow") {
            fresh = base + "~r" + std::to_string(serial);
        } else {
            fresh = base;
            bool ok = false;
            for (int t = 0; t < 64 && !fresh.empty(); ++t) {
                fresh.back() = static_cast<char>('a' + ((serial + t) % 26));
                if (fresh != victim && ref.find(fresh) == ref.end() &&
                    live_set.find(fresh) == live_set.end()) {
                    ok = true;
                    break;
                }
            }
            if (!ok) throw std::runtime_error("same-length replacement exhausted its alphabet");
        }

        ++serial;
        out.push_back(fresh);
        live.push_back(fresh);
        live_set.insert(fresh);
        ref.erase(victim);
        ref.insert(fresh);
    }

    return out;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("diag_domain KIND N [TRANCOCSV] [FRAC]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const double frac = argc > 4 ? std::stod(argv[4]) : 1.0;

        const auto keys = validation::make_data(kind, nkeys, trcsv);

        std::cerr << "diag_domain[" << kind << " n=" << keys.size() << " frac=" << frac
                  << "]" << std::endl;

        const Domain build_domain = measure(keys.size(),
            [&](std::size_t i) { return std::string_view(keys[i].first); });
        emit(std::cout, "build", build_domain);

        // The region prefixes, read back from a REAL Region per chunk. Replicating
        // compute_prefix here would report a rule; building the object reports what the
        // constructor actually does, including the chunking it does it under.
        {
            validation::Config cfg;
            const auto chunk = cfg.region_keys ? cfg.region_keys : std::max<std::size_t>(1, keys.size());

            std::vector<std::size_t> plens;
            std::vector<std::uint32_t> groups;

            for (std::size_t p = 0; p < keys.size(); p += chunk) {
                std::vector<Pair> part(keys.begin() + p, keys.begin() + std::min(p + chunk, keys.size()));
                validation::Region r(part, cfg);
                plens.push_back(r.prefix().size());
                groups.push_back(r.groups());
            }

            Domain pd;
            pd.n = plens.size();
            std::uint64_t total = 0;
            for (std::size_t i = 0; i < plens.size(); ++i) {
                total += plens[i];
                if (plens[i] > 128) ++pd.len_over128;
                if (plens[i] > 255) ++pd.len_over255;
            }
            if (pd.n) pd.len_mean = double(total) / double(pd.n);

            std::sort(plens.begin(), plens.end());
            pd.len_min = plens.empty() ? 0 : plens.front();
            pd.len_max = plens.empty() ? 0 : plens.back();
            pd.len_p50 = pick(plens, 0.50);
            pd.len_p95 = pick(plens, 0.95);
            pd.len_p99 = pick(plens, 0.99);

            emit(std::cout, "region_prefix", pd);

            // WHY A COUNT RATHER THAN THE MINIMUM. `len_min == 0` proves that AT LEAST ONE region
            // has a zero-length prefix; it says nothing about how often that happens, and the
            // adapter's `gcpl == 0` path is a branch whose cost depends on frequency, not on
            // existence. So the frequency is counted directly, alongside a coarse histogram so
            // the shape of the distribution is on the record too.
            {
                std::size_t p0 = 0;
                std::size_t hist[6] = {0, 0, 0, 0, 0, 0};   // 0, 1-31, 32-63, 64-95, 96-127, >=128

                for (auto v : plens) {
                    if (v == 0) ++p0;
                    else if (v <= 31) ++hist[1];
                    else if (v <= 63) ++hist[2];
                    else if (v <= 95) ++hist[3];
                    else if (v <= 127) ++hist[4];
                    else ++hist[5];
                }
                hist[0] = p0;

                std::cout << "{\"section\":\"region_prefix_shape\""
                          << ",\"regions\":" << plens.size()
                          << ",\"regions_p0\":" << p0
                          << ",\"hist_0\":" << hist[0]
                          << ",\"hist_1_31\":" << hist[1]
                          << ",\"hist_32_63\":" << hist[2]
                          << ",\"hist_64_95\":" << hist[3]
                          << ",\"hist_96_127\":" << hist[4]
                          << ",\"hist_ge128\":" << hist[5]
                          << ",\"empty_keys\":" << build_domain.empty
                          << "}" << std::endl;
            }

            std::uint64_t gtotal = 0;
            std::uint32_t gmax = 0;
            std::uint32_t gmin = groups.empty() ? 0 : groups.front();
            for (auto g : groups) {
                gtotal += g;
                if (g > gmax) gmax = g;
                if (g < gmin) gmin = g;
            }

            std::cout << "{\"section\":\"region_groups\""
                      << ",\"regions\":" << groups.size()
                      << ",\"groups_total\":" << gtotal
                      << ",\"groups_mean\":" << (groups.empty() ? 0.0 : double(gtotal) / double(groups.size()))
                      << ",\"groups_min\":" << gmin
                      << ",\"groups_max\":" << gmax
                      << ",\"region_keys\":" << chunk
                      << "}" << std::endl;

            // The suffix is what the CURRENT model reads; the full key is what the LITS
            // HPT needs. Reporting both makes the coordinate change a measured quantity.
            std::vector<std::string> suffixes;
            suffixes.reserve(keys.size());
            for (std::size_t p = 0; p < keys.size(); p += chunk) {
                std::vector<Pair> part(keys.begin() + p, keys.begin() + std::min(p + chunk, keys.size()));
                validation::Region r(part, cfg);
                const auto plen = r.prefix().size();
                for (const auto& kv : part) suffixes.push_back(kv.first.substr(plen));
            }

            emit(std::cout, "build_suffix", measure(suffixes.size(),
                 [&](std::size_t i) { return std::string_view(suffixes[i]); }));
        }

        // Query keys. The hit population is a sample of the build set, so it is not
        // re-measured; the miss keys are, because the generator ADDS bytes.
        {
            std::vector<std::string> miss;
            miss.reserve(keys.size());
            for (const auto& kv : keys) miss.push_back(kv.first + "~absent");

            emit(std::cout, "query_miss_tilde", measure(miss.size(),
                 [&](std::size_t i) { return std::string_view(miss[i]); }));

            std::vector<std::string> nul;
            nul.reserve(keys.size());
            for (const auto& kv : keys) nul.push_back(kv.first + std::string(1, '\0'));

            emit(std::cout, "query_miss_nul", measure(nul.size(),
                 [&](std::size_t i) { return std::string_view(nul[i]); }));
        }

        // Update keys, both replacement families, at the fraction the long workload is
        // asked to run. 200 per cent is the checkpoint the round's acceptance uses.
        for (const char* mode : {"same", "grow"}) {
            const auto fresh = update_keys(keys, mode, frac);

            std::string name = "update_";
            name += mode;

            emit(std::cout, name.c_str(), measure(fresh.size(),
                 [&](std::size_t i) { return std::string_view(fresh[i]); }));
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "diag_domain failed: " << e.what() << std::endl;
        return 1;
    }
}
