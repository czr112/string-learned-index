// E23: range scan on the frozen default.
//
// The last missing piece of the functional picture. Semantics are fixed and shared:
//
//   from the first record with key >= start, return AT MOST n records in key order,
//   as complete key/value pairs.
//
// Both ranked methods produce the SAME result form -- `std::vector<Pair>` with the
// keys copied -- so neither pays a cost the other does not. A method that returned
// views would be measuring a different thing.
//
// ART IS NOT IN THE RANKING. libart offers `art_iter` (whole-tree traversal) and
// `art_iter_prefix`, and no way to seek to an arbitrary key. A full-traversal
// adapter would measure the adapter, not ART's range capability, so it is recorded
// as a clearly-labelled diagnostic and kept out of the ordering.
//
// Three states are measured: the initial one, after inserts to 25%, and after
// replacements to 25%. Each is built from its own fresh initial state. The
// value-only workload is not repeated: E22 established that it changes nothing
// structural.
//
// Correctness is checked on EVERY scan, outside the timed span, against a reference
// map: that covers cross-region boundaries, cross-sub-block merge, the ordering of
// primary against overflow records, and starts that fall between two keys. The
// replacement state is also inspected for an EMPTY INTERMEDIATE sub-block, which is
// the shape that exposed E9's scan defect, and the finding is reported either way.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "native_e23.hpp"
#include "third_party/vendor/libart/src/art.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sched.h>
#endif

using namespace validation;
using namespace validation::e9alloc;

using Clock = std::chrono::steady_clock;
using Reference = std::map<std::string, std::uint64_t, Less>;

static double ns(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::nano>(b - a).count();
}
static volatile std::uint64_t sink = 0;

static std::uint64_t checksum(const std::vector<Pair>& v) {
    std::uint64_t s = 0;
    for (const auto& kv : v) s += kv.first.size() * 131 + kv.second + 0x9e3779b97f4a7c15ULL;
    return s;
}

// The expected range, from the reference: the first key >= start, at most n of them.
static std::vector<Pair> expected_range(const Reference& ref, const std::string& start, std::size_t n) {
    std::vector<Pair> out;
    for (auto it = ref.lower_bound(start); it != ref.end() && out.size() < n; ++it)
        out.push_back(*it);
    return out;
}

struct Pipeline {
    std::vector<Alloc> s_alloc;
    std::vector<std::vector<std::uint16_t>> s_caps;
};
static Pipeline make_plan(const std::vector<std::vector<GroupStats>>& demand) {
    Pipeline p;
    for (const auto& gs : demand) {
        const std::uint64_t budget = std::uint64_t(gs.size()) * 1024;
        const Alloc d = demand_single(gs, budget);
        StrictLog log;
        std::vector<std::uint16_t> caps;
        p.s_alloc.push_back(demand_strict(gs, d, log, caps));
        p.s_caps.push_back(std::move(caps));
    }
    return p;
}
static auto sg_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint16_t>> o;
    for (const auto& a : v) o.push_back(a.sg);
    return o;
}
static auto bb_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint32_t>> o;
    for (const auto& a : v) o.push_back(a.bb);
    return o;
}

// ART full-traversal adapter. DIAGNOSTIC ONLY -- see the file comment.
struct ArtScanCollector {
    std::string start;
    std::size_t want = 0;
    std::vector<Pair> out;
};
static int art_scan_cb(void* data, const unsigned char* key, uint32_t key_len, void* value) {
    auto* c = static_cast<ArtScanCollector*>(data);
    if (key_len == 0) return 0;
    // The adapter stores keys with an explicit terminator and values as value+1.
    std::string k(reinterpret_cast<const char*>(key), key_len - 1);
    if (compare(k, c->start) < 0) return 0;      // keep walking until we are in range
    c->out.emplace_back(std::move(k), reinterpret_cast<std::uintptr_t>(value) - 1);
    return c->out.size() >= c->want ? 1 : 0;      // non-zero stops the traversal
}

int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        SetProcessAffinityMask(GetCurrentProcess(), 1);
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#elif defined(__linux__)
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(0, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0)
            std::cerr << "affinity pin failed; continuing unpinned\n";
#endif
        if (argc < 3) throw std::invalid_argument("bench_e23 KIND N [TRANCOCSV] [RUNTAG]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string run_tag = argc > 4 ? argv[4] : "";
        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;
        Config cm = c;
        cm.multiblock = true;
        cm.reuse_group_ids = true;   // the frozen default is R1

        // The operation sequences that produce the two updated states, identical to
        // E22's so the states are the same ones.
        const std::size_t quarter = N / 4;
        std::vector<std::pair<int, std::string>> ins, rep;   // kind 1 insert, 2 erase
        for (std::size_t j = 0; j < quarter; ++j) {
            ins.push_back({1, keys[(j * 7) % N].first + "~new" + std::to_string(j)});
            rep.push_back({2, keys[(j * 3) % N].first});
            rep.push_back({1, keys[(j * 3) % N].first + "~rep" + std::to_string(j)});
        }

        const std::vector<std::size_t> NS{16, 128, 1024};
        const std::size_t nstarts = 4096;

        std::ofstream out("results/main/e23_scan.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        for (const char* st : {"initial", "insert25", "replace25"}) {
            const std::string state = st;

            // ---- fresh state ----------------------------------------------------
            Reference ref;
            for (const auto& kv : keys) ref[kv.first] = kv.second;
            if (state != "initial") {
                const auto& ops = (state == "insert25") ? ins : rep;
                for (const auto& [k, key] : ops) {
                    if (k == 1) ref[key] = 1;
                    else ref.erase(key);
                }
            }

            Pipeline plan;
            {
                Index probe(c);
                probe.bulk_load(keys);
                std::vector<std::vector<GroupStats>> demand;
                for (std::size_t i = 0; i < probe.region_count(); ++i)
                    demand.push_back(probe.region(i).group_stats());
                plan = make_plan(demand);
            }
            auto s = std::make_unique<Index>(cm);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            if (state != "initial") {
                const auto& ops = (state == "insert25") ? ins : rep;
                for (const auto& [k, key] : ops) {
                    if (k == 1) s->insert(key, 1);
                    else s->erase(key);
                }
            }
            if (s->size() != ref.size()) throw std::runtime_error("state construction drift");

            auto art = std::make_unique<e23art::NativeArt>(std::vector<Pair>(ref.begin(), ref.end()));

            // Everything the scan compares against must be the frozen keys plus the
            // state's own changes, in one sorted vector, for the sorted-array baseline.
            std::vector<Pair> flat(ref.begin(), ref.end());

            // ---- empty intermediate sub-blocks, if any ---------------------------
            std::size_t empty_mid = 0, multi = 0, max_nb = 0;
            for (std::size_t i = 0; i < s->region_count(); ++i) {
                const auto& reg = s->region(i);
                for (std::uint32_t g = 0; g < reg.groups(); ++g) {
                    const std::size_t nb = reg.subblock_count(g);
                    if (nb > 1) { ++multi; max_nb = std::max(max_nb, nb); }
                    for (std::size_t k = 0; k + 1 < nb; ++k)
                        if (reg.subblock_entries(g, k) == 0) ++empty_mid;
                }
            }

            // ---- start keys ------------------------------------------------------
            std::vector<std::string> starts;
            {
                std::mt19937_64 rng(2026091801);
                for (std::size_t i = 0; i < nstarts; ++i) {
                    const std::size_t r = rng() % flat.size();
                    if (i % 5 == 4) {
                        // Near the end, so the range is short: the returned count must
                        // be recorded, and must NOT be read as a full scan.
                        const std::size_t back = rng() % std::min<std::size_t>(2048, flat.size());
                        starts.push_back(flat[flat.size() - 1 - back].first);
                    } else if (i % 5 == 3) {
                        // Between two keys: exercises the "start is not present" path.
                        starts.push_back(flat[r].first + "~s");
                    } else {
                        starts.push_back(flat[r].first);
                    }
                }
            }

            for (std::size_t n : NS) {
                // ---- verification, outside the timed span ------------------------
                std::size_t checked = 0, returned_total = 0, short_scans = 0;
                std::size_t min_ret = SIZE_MAX;
                std::uint64_t want_sum = 0;
                for (const auto& stk : starts) {
                    const auto want = expected_range(ref, stk, n);
                    const auto got = s->scan(stk, n);
                    if (got != want) throw std::runtime_error("prototype scan mismatch");
                    const auto base = [&] {
                        std::vector<Pair> r;
                        auto it = std::lower_bound(flat.begin(), flat.end(), stk,
                            [](const Pair& a, const std::string& b) { return compare(a.first, b) < 0; });
                        for (; it != flat.end() && r.size() < n; ++it) r.push_back(*it);
                        return r;
                    }();
                    if (base != want) throw std::runtime_error("sorted_array scan mismatch");
                    want_sum += checksum(want);
                    returned_total += got.size();
                    min_ret = std::min(min_ret, got.size());
                    if (got.size() < n) ++short_scans;
                    ++checked;
                }

                // ---- timed, per method --------------------------------------------
                double ns_s = 0, ns_arr = 0, ns_art = 0;
                std::size_t art_step = 64;
                {
                    const auto a0 = Clock::now();
                    std::uint64_t sum = 0;
                    for (const auto& stk : starts) sum += checksum(s->scan(stk, n));
                    const auto a1 = Clock::now();
                    if (sum != want_sum) throw std::runtime_error("scan checksum (prototype)");
                    ns_s = ns(a0, a1) / double(starts.size());
                }
                {
                    const auto a0 = Clock::now();
                    std::uint64_t sum = 0;
                    for (const auto& stk : starts) {
                        std::vector<Pair> r;
                        auto it = std::lower_bound(flat.begin(), flat.end(), stk,
                            [](const Pair& a, const std::string& b) { return compare(a.first, b) < 0; });
                        for (; it != flat.end() && r.size() < n; ++it) r.push_back(*it);
                        sum += checksum(r);
                    }
                    const auto a1 = Clock::now();
                    if (sum != want_sum) throw std::runtime_error("scan checksum (sorted_array)");
                    ns_arr = ns(a0, a1) / double(starts.size());
                }
                {
                    // DIAGNOSTIC, and SAMPLED. The adapter walks the WHOLE tree and stops
                    // early; that is the adapter's shape, not ART's range capability.
                    //
                    // It runs on every 64th start rather than all of them: at ~3 ms per
                    // scan it dominated the round's wall clock by two orders of magnitude,
                    // and an order of magnitude is all this datum is for. The sample is
                    // taken over the SAME start list, in the same order.
                    std::vector<Pair> art_ref;
                    for (std::size_t i = 0; i < starts.size(); i += art_step) {
                        ArtScanCollector col{starts[i], n, {}};
                        art_iter(&art->tree(), art_scan_cb, &col);
                        // Verified here, outside the timed span, against the reference --
                        // a sampled diagnostic still has to be right.
                        if (col.out != expected_range(ref, starts[i], n))
                            throw std::runtime_error("ART diagnostic scan mismatch");
                        art_ref.insert(art_ref.end(), col.out.begin(), col.out.end());
                    }
                    const auto a0 = Clock::now();
                    std::uint64_t sum = 0;
                    for (std::size_t i = 0; i < starts.size(); i += art_step) {
                        ArtScanCollector col{starts[i], n, {}};
                        art_iter(&art->tree(), art_scan_cb, &col);
                        sum += checksum(col.out);
                    }
                    const auto a1 = Clock::now();
                    if (sum != checksum(art_ref)) throw std::runtime_error("scan checksum (ART diagnostic)");
                    ns_art = ns(a0, a1) / double((starts.size() + art_step - 1) / art_step);
                }

                out << "{\"kind\":\"" << kind << "\",\"state\":\"" << state
                    << "\",\"n\":" << n << ",\"run_tag\":\"" << run_tag << "\""
                    << ",\"keys\":" << N << ",\"live\":" << ref.size()
                    << ",\"scans\":" << checked
                    << ",\"returned\":{\"total\":" << returned_total
                    << ",\"min\":" << (min_ret == SIZE_MAX ? 0 : min_ret)
                    << ",\"short_scans\":" << short_scans << "}"
                    << ",\"subblocks\":{\"multiblock_groups\":" << multi
                    << ",\"empty_intermediate\":" << empty_mid << ",\"max_subblocks\":" << max_nb << "}"
                    << ",\"ns_per_scan\":{\"prototype\":" << ns_s
                    << ",\"sorted_array\":" << ns_arr
                    << ",\"art_fulltraverse_diagnostic\":" << ns_art
                    << ",\"art_diagnostic_step\":" << art_step << "}"
                    << "}" << std::endl;
            }
            std::cerr << "state " << state << " complete for " << kind
                      << " (multiblock groups=" << multi << ", empty intermediate sub-blocks="
                      << empty_mid << ")" << std::endl;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}
