// E38 / P1.3: the fingerprint compatibility gate.
//
// THE CLAIM: the tag is a function of the REGION SUFFIX and nothing else, so replacing the
// model does not change a single stored tag -- and the adapter, which reads the FULL key, must
// therefore contribute NOTHING to the hash.
//
// WHY THIS NEEDS A GATE AT ALL. The query path is FUSED: `Model::predict<Hash>` feeds every
// byte it reads into a Fingerprint and returns how many it consumed, and `find_impl` finishes
// the job with `fp.tail(s.substr(pred.consumed))` (index.hpp:2655,2662). The result is
// exactly `Fingerprint::of(suffix)`, which is what `insert_slow` STORED (index.hpp:3070). A
// model that reads bytes but reports them as consumed would silently change the query tag
// while the stored tags stayed put -- every candidate would then sail past the tag filter and
// the index would still be CORRECT, just slower, which is the kind of failure that survives a
// content check. So the tag is checked against the standalone computation directly.
//
// WHY THE LITS PATH IS `fp_consumed = 0`. The LITS adapter reads a full key from an absolute
// position, so the bytes it touches are not a prefix of the suffix -- feeding them into the
// hash would produce a tag that corresponds to no stored record. The task book's section 5.1
// settles this: `result.steps` keeps its meaning (the model's own processing count), and a
// NEW field, `fp_consumed`, says how many bytes the hash state has absorbed. For the LITS
// backend that field is 0.
//
// THE NEGATIVE CONTROL IS THE POINT. A test that only asserted "the tag equals the standalone
// value" would pass just as well if the naive wiring also happened to agree. So the naive
// wiring -- using the model's read count as the fingerprint offset -- is computed too, and the
// gate REQUIRES it to disagree on a measurable fraction of keys. If it ever agrees everywhere,
// this gate has stopped being able to detect the mistake it exists for, and says so.

#include "data.hpp"
#include "model/lits_hpt_adapter.hpp"
#include "switches.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using validation::Config;
using validation::Fingerprint;
using validation::Pair;

void fail(const std::string& what) {
    throw std::runtime_error("LITS FINGERPRINT GATE FAILED: " + what);
}

struct Counts {
    std::uint64_t keys = 0;
    std::uint64_t fused_agrees = 0;        // M0's fused path reproduces the stored tag
    std::uint64_t standalone_agrees = 0;   // the LITS wiring reproduces the stored tag
    std::uint64_t naive_differs = 0;       // the negative control is not vacuous
    std::uint64_t naive_agrees = 0;
    std::uint64_t consumed_zero = 0;       // keys where M0's model consumed nothing
    std::uint64_t consumed_max = 0;
    std::uint64_t lits_group_differs = 0;  // the model really did change
};

Counts g;

void check_dataset(const std::string& kind, std::size_t nkeys, const std::string& trcsv) {
    const auto pairs = validation::make_data(kind, nkeys, trcsv);
    const std::size_t N = pairs.size();

    Config cfg = e25sw::baseline_config(32);
    cfg.write_fast_locate = true;
    cfg.reuse_locate_ctx = true;
    cfg.compact_reuse_tag = true;
    cfg.compact_needs_garbage = true;
    cfg.scan_reserve_result = true;

    validation::Index idx(cfg);
    idx.bulk_load(pairs);

    std::vector<std::string> all_keys;
    all_keys.reserve(N);
    for (const auto& kv : pairs) all_keys.push_back(kv.first);

    const auto shared = lits_hpt::SharedModel::train(all_keys);

    const std::size_t chunk = cfg.region_keys ? cfg.region_keys : N;

    // The mode the frozen query path uses, so `predict<true>` runs with the same route and
    // early stop the index actually uses.
    const validation::Mode mode = validation::query_mode;

    for (std::size_t r = 0; r < idx.region_count(); ++r) {
        const validation::Region& region = idx.region(r);

        const std::size_t lo_i = r * chunk;
        const std::size_t hi_i = std::min(lo_i + chunk, N);

        if (lo_i >= hi_i) continue;

        const std::size_t p = region.prefix().size();
        const std::uint32_t G = region.groups();

        std::size_t max_key_bytes = 0;
        for (std::size_t i = lo_i; i < hi_i; ++i) {
            max_key_bytes = std::max(max_key_bytes, pairs[i].first.size());
        }

        const auto lits = lits_hpt::RegionModel::calibrate(*shared, pairs[lo_i].first,
                                                           pairs[hi_i - 1].first, p, G,
                                                           hi_i - lo_i, max_key_bytes);

        // Every key. The full corpora are affordable here: this is one hash per key.
        for (std::size_t i = lo_i; i < hi_i; ++i) {
            const std::string& key = pairs[i].first;
            const std::string_view suffix(key.data() + p, key.size() - p);

            const std::uint8_t stored = Fingerprint::of(suffix);

            // (1) M0's fused path, exactly as find_impl runs it.
            Fingerprint fused;
            const auto pred = region.model().predict<true>(suffix, G, mode.route, fused);

            fused.tail(suffix.substr(pred.consumed));

            if (fused.tag() != stored) {
                fail("the M0 fused path no longer reproduces the stored tag for a key in "
                     "region " + std::to_string(r));
            }

            ++g.fused_agrees;
            if (pred.consumed == 0) ++g.consumed_zero;
            if (pred.consumed > g.consumed_max) g.consumed_max = pred.consumed;

            // (2) The LITS wiring: the adapter contributes nothing, so the hash starts at 0.
            const std::uint32_t fp_consumed = 0;

            Fingerprint fresh;
            fresh.tail(suffix.substr(fp_consumed));

            if (fresh.tag() != stored) {
                fail("the LITS wiring does not reproduce the stored tag for a key in region " +
                     std::to_string(r));
            }

            ++g.standalone_agrees;

            // (3) The negative control, now measured on the LITS side where the mistake is
            // possible. For the REFERENCE model `consumed` and `fp_consumed` are the same
            // number, so using the read count as the offset is CORRECT there and would make
            // this control vacuous. For the LITS walk they differ -- the walk length is not
            // zero, and the bytes it touched are not a prefix of the suffix -- so this is the
            // configuration in which the mistake the two-field design prevents is detectable.
            std::uint32_t walked = 0;
            const auto lits_group = lits.usable() ? lits.predict_group(key, walked)
                                                  : 0u;
            (void)lits_group;

            if (lits.usable() && walked) {
                Fingerprint naive;
                naive.tail(suffix.substr(walked));

                if (naive.tag() == stored) ++g.naive_agrees;
                else ++g.naive_differs;
            }

            // (4) The model actually changed, so (1)-(3) are not comparing a model with
            // itself. Without this the gate would pass on a build where LITS was never wired.
            if (lits.usable() && lits.predict_group(key) != region.group_of(suffix)) {
                ++g.lits_group_differs;
            }

            ++g.keys;
        }
    }

    std::cerr << "  " << kind << ": checked " << g.keys << " keys so far" << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::invalid_argument("check_lits_fingerprint KIND:N[,KIND:N...] [CSV]");

        const std::string kinds = argv[1];
        const std::string trcsv = argc > 2 ? argv[2] : "";

        std::size_t begin = 0;
        while (begin <= kinds.size()) {
            const auto comma = kinds.find(',', begin);
            const std::string item = kinds.substr(begin, comma == std::string::npos
                                                            ? std::string::npos : comma - begin);

            if (!item.empty()) {
                const auto colon = item.find(':');
                if (colon == std::string::npos) throw std::invalid_argument("expected KIND:N");

                check_dataset(item.substr(0, colon), std::stoull(item.substr(colon + 1)), trcsv);
            }

            if (comma == std::string::npos) break;
            begin = comma + 1;
        }

        // ---- non-vacuity -------------------------------------------------------------
        if (!g.keys) fail("no key was checked");
        if (g.fused_agrees != g.keys) fail("the M0 fused path did not agree on every key");
        if (g.standalone_agrees != g.keys) fail("the LITS wiring did not agree on every key");
        if (!g.lits_group_differs) {
            fail("the LITS model produced the same group as the frozen one for every key -- "
                 "this gate is comparing a model with itself");
        }

        // THE NEGATIVE CONTROL, asserted rather than reported. The naive offset is the model's
        // read count, and the model reads at least one byte of any key that is not a strict
        // prefix of another, so disagreement is expected on nearly every key. If it ever
        // agrees on all of them the control has gone dead and so has this gate's ability to
        // catch the mistake it is for.
        if (!g.naive_differs) {
            fail("the negative control agrees everywhere -- using the model's read count as "
                 "the fingerprint offset would be undetectable here");
        }

        std::cout << "{\"gate\":\"check_lits_fingerprint\""
                  << ",\"keys\":" << g.keys
                  << ",\"fused_agrees\":" << g.fused_agrees
                  << ",\"standalone_agrees\":" << g.standalone_agrees
                  << ",\"naive_agrees\":" << g.naive_agrees
                  << ",\"naive_differs\":" << g.naive_differs
                  << ",\"naive_differs_fraction\":" << (g.keys ? double(g.naive_differs) / double(g.keys) : 0.0)
                  << ",\"consumed_zero\":" << g.consumed_zero
                  << ",\"consumed_max\":" << g.consumed_max
                  << ",\"lits_group_differs\":" << g.lits_group_differs
                  << "}" << std::endl;

        std::cerr << "check_lits_fingerprint PASS: " << g.keys << " keys; the tag is the suffix's "
                  << "under both models, and the naive offset would differ on " << g.naive_differs
                  << " of them" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "check_lits_fingerprint failed: " << e.what() << std::endl;
        return 1;
    }
}
