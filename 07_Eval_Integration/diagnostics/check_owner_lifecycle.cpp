// M3 lifecycle correctness gate -- P0 of docs/NEXT_ACTION_PLAN_20261011_MERGE.md.
//
// WHY THIS FILE EXISTS. The prototype gate this replaces (owner_proto.cpp, gate_split)
// counted every table-less LITS region and the region-count delta, never required
// Index::splits to grow, and ran the extra inserts on the ON arm only. So it could not
// tell "a real split happened" from "nothing happened", and it never compared the two
// arms on the same update sequence. REVIEW_OWNER_INTEGRATION_20261011.md section 3.1
// records that finding; this gate is the pre-registered close-out.
//
// SHAPE. For each (corpus, backend) cell, ONE update program (inserts, erases,
// overwrite-updates) is applied to THREE objects: OFF (lits_skip_reference_table=false),
// ON (true) and an independent unsigned-byte ordered reference map. At every checkpoint
// the complete key/value set, size, present/absent probes and cross-region scans are
// compared. Splits and prefix rebuilds must POSITIVELY trigger via Index::splits /
// Index::rebuilds deltas; a split must be followed by operations that land in BOTH child
// regions. LITS model ownership (LITS regions must not hold the reference table, fallback
// regions must) is checked after the split and at the end. The two explicit refusals --
// group_of() on a table-less region and a VALID preset offered to a LITS region -- are
// each exercised, with the OFF arm as the accepting control. bulk_load_with_plan() is
// never used as the preset test: it passes context=nullptr and cannot deliver a preset;
// the ON corpus build doing exactly that and SUCCEEDING is part of the contrast.
//
// Key-domain probes (empty key, embedded NUL, high bytes, 4096/5000-byte keys) run on
// both backends. LITS refuses unsupported bytes when they enter through bulk_load (its
// shared table refuses to train on them) and refuses over-limit keys at the point of use;
// both refusals must be recorded with their specified reason, must be identical on OFF
// and ON, and everything that DOES succeed must answer exactly like the reference.
//
// Output: one JSON line. Counting build only; this program produces no timings.
#define main previous_lookup_main
#include "lookup_structure.cpp"
#undef main
#include "e9_alloc.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ownerlife {

using validation::Config;
using validation::Index;
using validation::Mode;
using validation::Pair;
using validation::RegionDemand;
using validation::Result;

inline std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
        else if (u < 0x20) { out += "?"; }
        else out.push_back(c);
    }
    return out;
}

// The integration config of the M3 round: baseline + frozen switches + compact routing,
// with only lits_skip_reference_table as the arm variable (owner_proto::proto_config).
inline Config life_config(bool lits, bool skip) {
    Config cfg = e25sw::baseline_config(128);
    e25sw::pin_frozen_switches(cfg);
    cfg.model_backend = lits ? validation::ModelBackend::LitsHpt
                             : validation::ModelBackend::Context16;
    cfg.compact_region_routing = true;
    cfg.lits_skip_reference_table = skip;
    return cfg;
}

// The statistics replay of bulk_load_reusing_ids covers the SINGLE-BLOCK layout only
// (E16), so the stats config must not ask for multiblock. The target index keeps its own
// config; only the replay is single-block.
inline Config life_stats_config(bool lits) {
    Config cfg = life_config(lits, false);
    cfg.multiblock = false;
    return cfg;
}

struct RefLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return compare(a, b) < 0;
    }
};
using RefMap = std::map<std::string, std::uint64_t, RefLess>;

struct Tally {
    std::uint64_t checkpoints = 0;
    std::uint64_t inserts = 0, erases = 0, updates = 0;
    std::uint64_t size_mismatch = 0;
    std::uint64_t full_find_mismatch = 0;
    std::uint64_t absent_mismatch = 0;
    std::uint64_t scan_mismatch = 0;
    std::uint64_t scan_refusals = 0;
    std::uint64_t off_on_mismatch = 0;
    std::uint64_t op_result_mismatch = 0;
    std::uint64_t full_finds = 0, absent_probes = 0, scan_checks = 0, scan_rows = 0;
};

struct Own {
    std::uint64_t regions = 0;
    std::uint64_t lits = 0, lits_with_table = 0, lits_without_table = 0;
    std::uint64_t fallback = 0, fallback_with_table = 0, fallback_without_table = 0;
};

inline Own own_of(const Index& idx) {
    Own o;
    o.regions = idx.region_count();
    for (std::size_t r = 0; r < idx.region_count(); ++r) {
        const auto& reg = idx.region(r);
        const bool resident = reg.reference_table_resident();
        if (reg.uses_lits()) {
            ++o.lits;
            if (resident) ++o.lits_with_table; else ++o.lits_without_table;
        } else {
            ++o.fallback;
            if (resident) ++o.fallback_with_table; else ++o.fallback_without_table;
        }
    }
    return o;
}
inline std::vector<Pair> scan_oracle(const RefMap& ref, const std::string& start,
                                     std::size_t count) {
    std::vector<Pair> want;
    for (auto it = ref.lower_bound(start); it != ref.end() && want.size() < count; ++it)
        want.emplace_back(it->first, it->second);
    return want;
}

inline bool scan_compare(Index& off, Index& on, const RefMap& ref, const std::string& start,
                         std::size_t count, Tally& t) {
    const auto want = scan_oracle(ref, start, count);
    ++t.scan_checks;
    t.scan_rows += want.size();
    try {
        const auto a = off.scan(start, count);
        const auto b = on.scan(start, count);
        return a == want && b == want && a == b;
    } catch (const std::exception&) {
        ++t.scan_refusals;
        return false;
    }
}

// Complete key/value set, size, present/absent probes and cross-region scans at every
// checkpoint (full_keys is always true here; the flag only makes that explicit).
inline void verify(Index& off, Index& on, const RefMap& ref, const Mode& mode,
                   const std::vector<std::string>& deleted, Tally& t, bool full_keys) {
    ++t.checkpoints;
    if (off.size() != ref.size() || on.size() != ref.size()) ++t.size_mismatch;

    if (full_keys) {
        for (const auto& kv : ref) {
            const Result a = off.find(kv.first, mode);
            const Result b = on.find(kv.first, mode);
            ++t.full_finds;
            if (!a.found || a.value != kv.second) ++t.full_find_mismatch;
            if (!b.found || b.value != kv.second) ++t.full_find_mismatch;
            if (a.found != b.found || a.value != b.value) ++t.off_on_mismatch;
        }
    }

    for (std::size_t i = 0; i < deleted.size(); ++i) {
        const std::string& k = deleted[i];
        if (ref.find(k) != ref.end()) continue;   // re-inserted later: not absent
        const Result a = off.find(k, mode);
        const Result b = on.find(k, mode);
        ++t.absent_probes;
        if (a.found || b.found) ++t.absent_mismatch;
    }
    if (!ref.empty()) {
        const std::string derived = ref.begin()->first + "#never-existed";
        if (derived.size() <= 4096 && ref.find(derived) == ref.end()) {
            const Result a = off.find(derived, mode);
            const Result b = on.find(derived, mode);
            ++t.absent_probes;
            if (a.found || b.found) ++t.absent_mismatch;
        }
    }

    // Cross-region scans (Index-level, all regions). In-domain starts only: extension keys
    // (last + a byte) can exceed the LITS length ceiling when the stored key already sits at
    // it, which is a question about the QUERY domain -- probed explicitly by the domain
    // scenarios -- not a data question this checkpoint asks.
    std::vector<std::string> starts;
    starts.emplace_back();
    if (!ref.empty()) {
        const std::size_t n = ref.size();
        std::size_t idx = 0;
        for (auto it = ref.begin(); it != ref.end(); ++it, ++idx) {
            if (idx == 0 || idx == n / 3 || idx == (2 * n) / 3 || idx + 1 == n)
                starts.push_back(it->first);
        }
        const std::string& first = ref.begin()->first;
        const std::string& last = ref.rbegin()->first;
        if (first.size() > 1) starts.push_back(first.substr(0, first.size() - 1));
        if (last.size() > 1) starts.push_back(last.substr(0, last.size() - 1));
        // No high-byte start here: out-of-domain query keys are the query-domain probe's
        // subject, where their refusal is checked as a contract instead of aborting a data
        // comparison.
    }
    if (!deleted.empty()) starts.push_back(deleted.back());

    for (const auto& s : starts)
        for (const std::size_t count : {std::size_t(64), std::size_t(256)})
            if (!scan_compare(off, on, ref, s, count, t)) ++t.scan_mismatch;
}
struct OpResult {
    bool threw_off = false, threw_on = false;
    std::string what_off, what_on;
};

enum class OpKind { Insert, Erase, Update };

inline OpResult apply_op(Index& off, Index& on, OpKind kind, const std::string& key,
                         std::uint64_t value, bool& ok_off, bool& ok_on) {
    OpResult r;
    auto run = [&](Index& idx, bool& ok, bool& threw, std::string& what) {
        try {
            switch (kind) {
                case OpKind::Insert: ok = idx.insert(key, value); break;
                case OpKind::Erase: ok = idx.erase(key); break;
                case OpKind::Update: ok = idx.update(key, value); break;
            }
        } catch (const std::exception& e) {
            threw = true;
            if (what.empty()) what = e.what();
        }
    };
    run(off, ok_off, r.threw_off, r.what_off);
    run(on, ok_on, r.threw_on, r.what_on);
    return r;
}

struct PrefixCase {
    std::string name;
    std::uint64_t rebuilds_off_delta = 0, rebuilds_on_delta = 0;
    std::uint64_t splits_off_delta = 0, splits_on_delta = 0;
    bool threw_off = false, threw_on = false;
    std::uint64_t mismatch = 0;
    std::uint64_t lits_on = 0, lits_with_table_on = 0;
    std::uint64_t fallback_on = 0, fallback_with_table_on = 0;
    std::string what;
};

inline PrefixCase run_prefix_case(bool lits, const std::string& name,
                                  const std::vector<Pair>& base, const std::string& op) {
    PrefixCase out;
    out.name = name;
    Index off(life_config(lits, false));
    Index on(life_config(lits, true));
    off.bulk_load(base);
    on.bulk_load(base);
    RefMap ref;
    for (const auto& kv : base) ref.emplace(kv.first, kv.second);

    const std::uint64_t rb_off0 = off.rebuilds, rb_on0 = on.rebuilds;
    const std::uint64_t sp_off0 = off.splits, sp_on0 = on.splits;
    bool ok_off = false, ok_on = false;
    const OpResult r = apply_op(off, on, OpKind::Insert, op, 999, ok_off, ok_on);
    out.threw_off = r.threw_off;
    out.threw_on = r.threw_on;
    out.what = r.what_off.empty() ? r.what_on : r.what_off;
    if (!r.threw_off && !ok_off) ++out.mismatch;
    if (!r.threw_on && !ok_on) ++out.mismatch;
    if (!r.threw_off) ref.emplace(op, 999);
    out.rebuilds_off_delta = off.rebuilds - rb_off0;
    out.rebuilds_on_delta = on.rebuilds - rb_on0;
    out.splits_off_delta = off.splits - sp_off0;
    out.splits_on_delta = on.splits - sp_on0;

    Tally t;
    std::vector<std::string> none;
    verify(off, on, ref, ::mode(false), none, t, true);
    out.mismatch += t.full_find_mismatch + t.size_mismatch + t.absent_mismatch +
                    t.scan_mismatch + t.scan_refusals + t.off_on_mismatch +
                    t.op_result_mismatch;
    const Own own = own_of(on);
    out.lits_on = own.lits;
    out.lits_with_table_on = own.lits_with_table;
    out.fallback_on = own.fallback;
    out.fallback_with_table_on = own.fallback_with_table;
    return out;
}
struct GroupOfProbe {
    bool lits_present = false;
    bool on_refused = false, message_ok = false;
    bool off_ok = false, values_equal = false;
    std::uint64_t mismatch = 0;
    std::string on_what;
};

// group_of() is the reference-model API; on a region whose table the switch dropped it must
// refuse loudly, and on the OFF arm it must answer the same group the ON arm's region
// reports from its LITS mapping -- the mapping is a property of the build, not of the table.
inline GroupOfProbe run_group_of_probe(bool lits, const std::vector<Pair>& base) {
    GroupOfProbe out;
    if (!lits) return out;   // the refusal exists only when a LITS region dropped its table
    Index off(life_config(lits, false));
    Index on(life_config(lits, true));
    off.bulk_load(base);
    on.bulk_load(base);
    // Use a stored key whose located region is LITS-mapped, so the refusal is asked of a
    // region that really dropped its table.
    for (const auto& kv : base) {
        const std::size_t r = off.locate(kv.first);
        if (!on.region(r).uses_lits()) continue;
        out.lits_present = true;
        const std::string suffix =
            kv.first.substr(std::min(kv.first.size(), on.region(r).prefix().size()));
        // The REFERENCE-model accessor must refuse: the region holds no table.
        try {
            (void)on.region(r).group_of(suffix);
            ++out.mismatch;   // answered although the table is gone
        } catch (const std::logic_error& e) {
            out.on_refused = true;
            out.on_what = e.what();
        } catch (const std::exception&) {
        }
        out.message_ok = out.on_refused &&
                         out.on_what.find("lits_skip_reference_table") != std::string::npos;
        // OFF control: the reference table answers for the same region, and the LITS
        // mapping of the same key agrees with it (check_lits_model asserts this per key).
        try {
            const auto g_off = off.region(r).group_of(suffix);
            const auto g_on = on.region(r).diag_group(kv.first);
            out.off_ok = true;
            out.values_equal = (g_off == g_on);
        } catch (const std::exception&) {
            ++out.mismatch;
        }
        break;
    }
    return out;
}

struct QueryDomainProbe {
    std::uint64_t probes = 0, refused_off = 0, refused_on = 0, asymmetric = 0;
    std::uint64_t answer_mismatch = 0;
    bool message_ok = false;
    bool lits_present = false;
    std::string what_off, what_on;
};

// Out-of-domain LOOKUP keys. The LITS backend refuses bytes >= 0x80 and NUL (the declared
// key domain; model/lits_hpt_adapter.hpp unsupported_reason): the query path refuses them
// explicitly, before the table walk -- ASan caught that walk reading one table cell past
// the end of the trained block for a byte >= 0x80 on 2026-10-11. Context16 accepts the
// same bytes, so the two expectations differ by backend and are both pre-registered.
inline QueryDomainProbe run_query_domain_probe(bool lits, const std::vector<Pair>& base,
                                               const std::vector<std::string>& keys) {
    QueryDomainProbe out;
    Index off(life_config(lits, false));
    Index on(life_config(lits, true));
    off.bulk_load(base);
    on.bulk_load(base);
    RefMap ref;
    for (const auto& kv : base) ref.emplace(kv.first, kv.second);
    for (std::size_t r = 0; r < on.region_count(); ++r)
        if (on.region(r).uses_lits()) { out.lits_present = true; break; }

    const Mode mode = ::mode(false);
    for (const auto& k : keys) {
        ++out.probes;
        bool ro = false, rn = false;
        std::string wo, wn;
        try { (void)off.scan(k, 4); (void)off.find(k, mode); }
        catch (const std::exception& e) { ro = true; wo = e.what(); }
        try { (void)on.scan(k, 4); (void)on.find(k, mode); }
        catch (const std::exception& e) { rn = true; wn = e.what(); }
        if (ro) { ++out.refused_off; if (out.what_off.empty()) out.what_off = wo; }
        if (rn) { ++out.refused_on; if (out.what_on.empty()) out.what_on = wn; }
        if (ro != rn) ++out.asymmetric;
        if (!ro && !rn) {
            try {
                const auto want = scan_oracle(ref, k, 4);
                const auto a = off.scan(k, 4);
                const auto b = on.scan(k, 4);
                if (a != want || b != want || a != b) ++out.answer_mismatch;
            } catch (const std::exception&) {
                ++out.answer_mismatch;
            }
        }
    }
    {
        Tally t;
        std::vector<std::string> none;
        verify(off, on, ref, mode, none, t, true);
        out.answer_mismatch += t.full_find_mismatch + t.size_mismatch + t.absent_mismatch +
                               t.scan_mismatch + t.scan_refusals + t.off_on_mismatch +
                               t.op_result_mismatch;
    }
    out.message_ok = out.refused_off == out.probes && out.refused_on == out.probes &&
                     out.what_off.find("supported domain") != std::string::npos &&
                     out.what_off == out.what_on;
    return out;
}

struct DomainProbe {
    std::string name;
    bool build_threw_off = false, build_threw_on = false;
    bool threw_off = false, threw_on = false;
    std::uint64_t ops = 0, refused_ops = 0, mismatch = 0;
    std::string build_what_off, build_what_on, what_off, what_on;
};

// Two outcomes are possible for a key set that the backend cannot map, and BOTH are
// legitimate here: the whole build refuses (LITS trains its shared table from every key and
// throws on an unsupported byte) or the build succeeds and a later operation falls back per
// region. The gate does not decide which is correct in advance; it requires that OFF and ON
// do the SAME thing, that a refusal names the specified reason, and that whatever succeeds
// answers exactly like the reference.
inline DomainProbe run_domain_probe(bool lits, const std::string& name,
                                    const std::vector<Pair>& base,
                                    const std::vector<Pair>& ops) {
    DomainProbe out;
    out.name = name;
    std::unique_ptr<Index> off, on;
    RefMap ref;
    try {
        off.reset(new Index(life_config(lits, false)));
        off->bulk_load(base);
        for (const auto& kv : base) ref.emplace(kv.first, kv.second);
    } catch (const std::exception& e) {
        out.build_threw_off = true;
        out.build_what_off = e.what();
    }
    try {
        on.reset(new Index(life_config(lits, true)));
        on->bulk_load(base);
    } catch (const std::exception& e) {
        out.build_threw_on = true;
        out.build_what_on = e.what();
    }
    ++out.ops;   // the build itself is the first "operation" the probe reports on
    if (out.build_threw_off || out.build_threw_on) {
        if (out.build_threw_off != out.build_threw_on) ++out.mismatch;
        if (out.build_what_off != out.build_what_on) ++out.mismatch;
        ++out.refused_ops;
        return out;
    }

    for (const auto& op : ops) {
        bool ok_off = false, ok_on = false;
        const OpResult r = apply_op(*off, *on, OpKind::Insert, op.first, op.second, ok_off, ok_on);
        ++out.ops;
        out.threw_off = out.threw_off || r.threw_off;
        out.threw_on = out.threw_on || r.threw_on;
        if (out.what_off.empty() && !r.what_off.empty()) out.what_off = r.what_off;
        if (out.what_on.empty() && !r.what_on.empty()) out.what_on = r.what_on;
        if (r.threw_off != r.threw_on) ++out.mismatch;   // the switch must not change the domain
        if (r.threw_off || r.threw_on) {
            ++out.refused_ops;
            if (r.what_off != r.what_on) ++out.mismatch; // same reason on both arms
            continue;                                    // the arms must be unchanged (verify below)
        }
        if (!ok_off || !ok_on) ++out.mismatch;
        ref.emplace(op.first, op.second);
    }

    Tally t;
    std::vector<std::string> none;
    verify(*off, *on, ref, ::mode(false), none, t, true);
    out.mismatch += t.full_find_mismatch + t.size_mismatch + t.absent_mismatch +
                    t.scan_mismatch + t.scan_refusals + t.off_on_mismatch +
                    t.op_result_mismatch;
    return out;
}
inline Index::CapacityPlan life_plan(const std::vector<RegionDemand>& regions) {
    Index::CapacityPlan p;
    for (const auto& rd : regions) {
        const std::uint64_t budget = std::uint64_t(rd.stats.size()) * 1024;
        const auto d = validation::e9alloc::demand_single(rd.stats, budget);
        validation::e9alloc::StrictLog log;
        std::vector<std::uint16_t> caps;
        const auto s = validation::e9alloc::demand_strict(rd.stats, d, log, caps);
        p.sg.push_back(s.sg);
        p.bb.push_back(s.bb);
        p.caps.push_back(caps);
    }
    return p;
}

struct PresetProbe {
    bool off_ok = false, on_threw = false, message_ok = false, scan_equal = false;
    bool on_ok = false;                 // context16: the ON arm must accept the valid preset
    std::string off_what, on_what;
};

// The preset must be REAL: bulk_load_reusing_ids builds a genuine BuildContext with
// per-region digests and offers it through the documented one-call entry. The null-context
// path (bulk_load / bulk_load_with_plan) is exercised elsewhere in this gate and SUCCEEDS on
// the ON arm, which is what makes the refusal specific to a valid preset.
inline PresetProbe run_preset_probe(bool lits, const std::vector<Pair>& pairs) {
    PresetProbe out;
    std::vector<Pair> sorted_ref(pairs.begin(), pairs.end());
    std::sort(sorted_ref.begin(), sorted_ref.end(),
              [](const Pair& a, const Pair& b) { return compare(a.first, b.first) < 0; });

    Index off(life_config(lits, false));
    Index plain(life_config(lits, false));
    plain.bulk_load(pairs);
    try {
        off.bulk_load_reusing_ids(pairs, life_stats_config(lits), life_plan);
        out.off_ok = true;
    } catch (const std::exception& e) {
        out.off_what = e.what();
    }
    if (out.off_ok) {
        const auto a = off.scan({}, sorted_ref.size() + 8);
        const auto b = plain.scan({}, sorted_ref.size() + 8);
        out.scan_equal = (a == sorted_ref) && (b == sorted_ref);
    }

    Index on(life_config(lits, true));
    try {
        on.bulk_load_reusing_ids(pairs, life_stats_config(lits), life_plan);
        out.on_ok = true;
    } catch (const std::logic_error& e) {
        out.on_threw = true;
        out.on_what = e.what();
    } catch (const std::exception& e) {
        out.on_what = std::string("unexpected: ") + e.what();
    }
    out.message_ok = out.on_threw &&
                     out.on_what.find("lits_skip_reference_table") != std::string::npos;
    return out;
}

}  // namespace ownerlife
int main(int argc, char** argv) {
    std::string section = "start";
    try {
        if (argc != 3) throw std::invalid_argument("rows.bin context16|lits");
        const std::string backend = argv[2];
        const bool lits = backend == "lits";
        if (!lits && backend != "context16") throw std::invalid_argument("backend");

        section = "load";
        auto row_input = buffered_input(argv[1]);
        const auto rows_in = unified::read_rows(row_input);
        if (rows_in.empty()) throw std::invalid_argument("empty rows");
        std::vector<Pair> rows;
        rows.reserve(rows_in.size());
        for (const auto& row : rows_in) rows.emplace_back(row.key, row.value);
        std::sort(rows.begin(), rows.end(),
                  [](const Pair& a, const Pair& b) { return compare(a.first, b.first) < 0; });
        for (std::size_t i = 1; i < rows.size(); ++i)
            if (!(compare(rows[i - 1].first, rows[i].first) < 0))
                throw std::runtime_error("rows are not strictly ascending");

        const Mode mode = ::mode(false);
        ownerlife::Tally t;
        std::vector<std::string> deleted;

        section = "build";
        ownerlife::Index off(ownerlife::life_config(lits, false));
        ownerlife::Index on(ownerlife::life_config(lits, true));
        off.bulk_load(rows);
        on.bulk_load(rows);
        ownerlife::RefMap ref;
        for (const auto& kv : rows) ref.emplace(kv.first, kv.second);

        section = "checkpoint-0";
        ownerlife::verify(off, on, ref, mode, deleted, t, true);   // checkpoint 0

        section = "phases";
        const auto op_insert = [&](const std::string& k, std::uint64_t v) {
            const bool fresh = ref.emplace(k, v).second;
            const bool a = off.insert(k, v);
            const bool b = on.insert(k, v);
            if (!fresh || !a || !b) ++t.op_result_mismatch;
            ++t.inserts;
        };
        const auto op_erase = [&](const std::string& k) {
            const auto n = ref.erase(k);
            const bool a = off.erase(k);
            const bool b = on.erase(k);
            if (n != 1 || !a || !b) ++t.op_result_mismatch;
            if (deleted.size() < 64) deleted.push_back(k);
            ++t.erases;
        };
        const auto op_update = [&](const std::string& k, std::uint64_t v) {
            auto it = ref.find(k);
            if (it == ref.end()) { ++t.op_result_mismatch; return; }
            it->second = v;
            const bool a = off.update(k, v);
            const bool b = on.update(k, v);
            if (!a || !b) ++t.op_result_mismatch;
            ++t.updates;
        };

        // Phase 1: inserts spread over the whole key space, derived from corpus keys.
        std::vector<std::string> inserted;
        inserted.reserve(1300);
        for (int i = 0; i < 1200; ++i) {
            const std::string k =
                rows[(std::size_t(i) * 2654435u) % rows.size()].first + "#n" + std::to_string(i);
            op_insert(k, 0x10000000ull + std::uint64_t(i));
            inserted.push_back(k);
            if (i == 599) ownerlife::verify(off, on, ref, mode, deleted, t, true);
        }
        ownerlife::verify(off, on, ref, mode, deleted, t, true);   // checkpoint 1

        // Phase 2: erase half of the inserted keys plus corpus keys.
        for (int i = 0; i < 600; ++i) op_erase(inserted[std::size_t(i)]);
        std::size_t corpus_erased = 0, probe = 0;
        while (corpus_erased < 300 && probe < rows.size() * 4) {
            const std::string& k = rows[(probe * 7919u) % rows.size()].first;
            ++probe;
            if (ref.find(k) == ref.end()) continue;
            op_erase(k);
            ++corpus_erased;
        }
        if (corpus_erased != 300) ++t.op_result_mismatch;
        ownerlife::verify(off, on, ref, mode, deleted, t, true);   // checkpoint 2

        // Phase 3: overwrite-updates of surviving inserted and corpus keys.
        for (int i = 0; i < 300; ++i) {
            const std::string& k = inserted[600 + std::size_t((i * 2) % 600)];
            op_update(k, 0x11000000ull + std::uint64_t(i));
        }
        std::size_t idx = 0, corpus_updated = 0;
        for (auto it = ref.begin(); it != ref.end() && corpus_updated < 300; ++it, ++idx)
            if (idx % 211 == 0) { op_update(it->first, it->second * 2 + 1); ++corpus_updated; }
        if (corpus_updated != 300) ++t.op_result_mismatch;
        ownerlife::verify(off, on, ref, mode, deleted, t, true);   // checkpoint 3

        // Phase 4: mixed tail (survivors of the even-index erases only).
        std::vector<std::string> mixed;
        for (int i = 0; i < 100; ++i) {
            const std::string k =
                rows[(std::size_t(i) * 4093u) % rows.size()].first + "#m" + std::to_string(i);
            op_insert(k, 0x12000000ull + std::uint64_t(i));
            mixed.push_back(k);
        }
        for (int i = 0; i < 100; i += 2) op_erase(mixed[std::size_t(i)]);
        for (int i = 3; i < 100; i += 6)
            op_update(mixed[std::size_t(i)], 0x13000000ull + std::uint64_t(i));
        ownerlife::verify(off, on, ref, mode, deleted, t, true);   // checkpoint 4

        section = "split";
        // Phase 5: split trigger. Ascending keys above every corpus key land in the last
        // region; the last region starts at ~region_keys keys, so ~region_keys+ inserts
        // push it past 2*region_keys and Index::insert must call rebuild -> splits.
        const std::string stem = rows.back().first;
        const std::uint64_t s_off0 = off.splits, s_on0 = on.splits;
        const std::uint64_t rb_off0 = off.rebuilds, rb_on0 = on.rebuilds;
        const std::size_t split_ops = 5200;
        for (std::size_t i = 0; i < split_ops; ++i)
            op_insert(stem + "-m3life-" + std::to_string(1000000 + i).substr(1),
                      0x20000000ull + i);
        const std::uint64_t s_off_delta = off.splits - s_off0;
        const std::uint64_t s_on_delta = on.splits - s_on0;
        const std::uint64_t rb_off_delta = off.rebuilds - rb_off0;
        const std::uint64_t rb_on_delta = on.rebuilds - rb_on0;
        const ownerlife::Own own_split_off = ownerlife::own_of(off);
        const ownerlife::Own own_split_on = ownerlife::own_of(on);
        ownerlife::verify(off, on, ref, mode, deleted, t, true);   // checkpoint 5
        section = "children";
        // Operations landing in BOTH split children: a key just above the first inserted
        // key (left child) and one above the last (right child), plus a scan crossing the
        // boundary between them.
        const std::string firstk = stem + "-m3life-000000";
        const std::string lastk = stem + "-m3life-005199";
        const std::size_t r_first_on = on.locate(firstk), r_last_on = on.locate(lastk);
        const std::size_t r_first_off = off.locate(firstk), r_last_off = off.locate(lastk);
        const bool child_split = (r_first_on != r_last_on) && (r_first_off != r_last_off);
        // In-domain (ASCII) key just above firstk: sorts after it and before "-000001", so
        // it lands in the LEFT child. The NUL-bearing probe lives in the query-domain and
        // domain sections, where the backend's refusal is the thing being checked.
        const std::string left_probe = firstk + "!";
        op_insert(left_probe, 0x30000000ull);
        const std::size_t r_left_on = on.locate(left_probe);
        const bool child_left_ok = r_left_on == r_first_on;
        op_update(lastk, 0x30000001ull);
        const bool child_right_ok = on.locate(lastk) == r_last_on &&
                                    off.locate(lastk) == r_last_off;
        {
            section = "children-scan";
            const std::size_t before = t.scan_mismatch;
            if (!ownerlife::scan_compare(off, on, ref, firstk, 8192, t)) ++t.scan_mismatch;
            (void)before;
        }
        op_erase(left_probe);
        op_erase(lastk);
        section = "checkpoint-final";
        ownerlife::verify(off, on, ref, mode, deleted, t, true);   // checkpoint 6 (final)

        const ownerlife::Own own_final_off = ownerlife::own_of(off);
        const ownerlife::Own own_final_on = ownerlife::own_of(on);

        section = "prefix";
        // ---- prefix rebuild: three deterministic positive triggers -------------------
        const auto base_keys = [](const std::string& prefix, std::size_t n) {
            std::vector<Pair> out;
            for (std::size_t i = 0; i < n; ++i) {
                std::string k = prefix + "-";
                std::string num = std::to_string(i);
                while (num.size() < 4) num.insert(num.begin(), '0');
                k += num;
                out.emplace_back(k, 1 + i);
            }
            return out;
        };
        const std::string high_op = std::string("\x00\xf0", 2);
        std::vector<ownerlife::PrefixCase> prefix_cases;
        prefix_cases.push_back(ownerlife::run_prefix_case(lits, "prefix-break-late",
                                                          base_keys("aaa", 64), "bbb-0000"));
        prefix_cases.push_back(ownerlife::run_prefix_case(lits, "prefix-break-empty",
                                                          base_keys("zzz", 32), std::string()));
        prefix_cases.push_back(ownerlife::run_prefix_case(lits, "prefix-break-high",
                                                          base_keys("mmm", 32), high_op));

        section = "groupof";
        const ownerlife::GroupOfProbe group_probe = ownerlife::run_group_of_probe(lits, rows);

        section = "domain";
        // ---- key-domain probes ---------------------------------------------------------
        // LITS trains its shared table from EVERY key, so a key set containing an
        // unsupported byte (NUL or >=128) is refused as a WHOLE at build time; an
        // over-limit key is refused at the point of use; a region rebuilt after such an
        // insert falls back to the reference model instead. The probes record which of
        // these happened; the gate requires OFF/ON symmetry, a specified refusal reason,
        // and a clean supported-set control.
        std::vector<ownerlife::DomainProbe> domain;
        {
            std::vector<Pair> base;
            base.emplace_back(std::string(), 1);
            base.emplace_back(std::string(1, char(0)), 2);
            base.emplace_back("a", 3);
            base.emplace_back("aa", 4);
            base.emplace_back(std::string("\x80\xff", 2), 5);
            std::vector<Pair> ops;
            ops.emplace_back(std::string("\xff\xff", 2), 6);
            ops.emplace_back(std::string("\x00\x00", 2), 7);
            ops.emplace_back("b", 8);
            domain.push_back(ownerlife::run_domain_probe(lits, "domain-empty-nul-high", base, ops));
        }
        {
            std::vector<Pair> base;
            base.emplace_back("a", 1);
            base.emplace_back(std::string(4096, 'x'), 2);
            std::vector<Pair> ops;
            ops.emplace_back(std::string(4096, 'x') + "y", 3);      // 4097 bytes
            ops.emplace_back(std::string(5000, 'y'), 4);            // 5000 bytes
            domain.push_back(ownerlife::run_domain_probe(lits, "domain-long-keys", base, ops));
        }
        {
            std::vector<Pair> base;
            base.emplace_back(std::string(), 1);
            std::vector<Pair> ops;
            ops.emplace_back("a", 2);
            ops.emplace_back(std::string("\xff", 1), 3);
            domain.push_back(ownerlife::run_domain_probe(lits, "domain-empty-base", base, ops));
        }
        {
            std::vector<Pair> base;
            base.emplace_back("a", 1);
            base.emplace_back("aa", 2);
            base.emplace_back("b", 3);
            std::vector<Pair> ops;
            ops.emplace_back("c", 4);
            ops.emplace_back("ab", 5);
            domain.push_back(ownerlife::run_domain_probe(lits, "domain-support-control", base, ops));
        }

        section = "query-domain";
        // Out-of-domain LOOKUP keys against a LITS-walked region (and the Context16 control
        // that accepts the same bytes). See run_query_domain_probe.
        std::vector<std::string> query_domain_keys;
        query_domain_keys.push_back(std::string("\x80", 1));
        query_domain_keys.push_back(std::string("a\x80", 2));
        query_domain_keys.push_back(std::string("a\xff", 2));
        query_domain_keys.push_back(std::string("\x00", 1));
        query_domain_keys.push_back(std::string("a\x00", 2));
        query_domain_keys.push_back(std::string("a\x80\xff", 3));
        std::vector<Pair> qd_base;
        qd_base.emplace_back("a", 1);
        qd_base.emplace_back(std::string(4096, 'x'), 2);
        const ownerlife::QueryDomainProbe query_probe =
            ownerlife::run_query_domain_probe(lits, qd_base, query_domain_keys);

        section = "preset";
        // ---- valid-preset refusal ------------------------------------------------------
        const ownerlife::PresetProbe preset = ownerlife::run_preset_probe(lits, rows);

        section = "verdicts";
        // ---- verdicts ------------------------------------------------------------------
        const bool gate_full = t.full_find_mismatch == 0;
        const bool gate_size = t.size_mismatch == 0;
        const bool gate_absent = t.absent_mismatch == 0;
        const bool gate_scan = t.scan_mismatch == 0 && t.scan_refusals == 0;
        const bool gate_ops = t.op_result_mismatch == 0 && t.off_on_mismatch == 0 &&
                              off.region_count() == on.region_count();
        const bool gate_split = s_on_delta > 0 && s_off_delta == s_on_delta && child_split &&
                                child_left_ok && child_right_ok && rb_on_delta >= s_on_delta;
        const std::uint64_t own_violations =
            own_split_on.lits_with_table + own_split_on.fallback_without_table +
            own_final_on.lits_with_table + own_final_on.fallback_without_table +
            own_final_off.lits_without_table + own_final_off.fallback_without_table;
        const bool gate_ownership =
            own_violations == 0 && (!lits || own_final_on.lits > 0);
        bool gate_prefix = true;
        for (const auto& pc : prefix_cases) {
            if (pc.threw_off != pc.threw_on) gate_prefix = false;
            if (!lits && pc.threw_on) gate_prefix = false;
            if (pc.threw_on) { if (pc.mismatch != 0) gate_prefix = false; }
            else if (pc.rebuilds_on_delta == 0 || pc.splits_on_delta != 0 ||
                     pc.rebuilds_off_delta == 0 || pc.mismatch != 0)
                gate_prefix = false;
            if (pc.lits_on > 0 && pc.lits_with_table_on != 0) gate_prefix = false;
        }
        const bool gate_groupof = lits
            ? (group_probe.lits_present && group_probe.on_refused && group_probe.message_ok &&
               group_probe.off_ok && group_probe.mismatch == 0)
            : true;
        bool gate_domain = true;
        std::uint64_t domain_refusals = 0;
        for (const auto& dp : domain) {
            if (dp.build_threw_off != dp.build_threw_on) gate_domain = false;
            if (dp.threw_off != dp.threw_on) gate_domain = false;
            if (dp.mismatch != 0) gate_domain = false;
            const bool refused = dp.build_threw_off || dp.build_threw_on ||
                                 dp.threw_off || dp.threw_on;
            if (!lits && refused) gate_domain = false;   // the reference backend supports the domain
            if (refused) {
                ++domain_refusals;
                const std::string& msg = dp.build_threw_off ? dp.build_what_off : dp.what_off;
                const bool named = msg.find("supported domain") != std::string::npos ||
                                   msg.find("accumulation bound") != std::string::npos;
                if (!named) gate_domain = false;   // a refusal must be the SPECIFIED one
            }
        }
        if (lits && domain_refusals == 0) gate_domain = false;
        bool support_control_ok = false;
        for (const auto& dp : domain)
            if (dp.name == "domain-support-control")
                support_control_ok = !dp.build_threw_off && !dp.build_threw_on &&
                                     !dp.threw_off && !dp.threw_on && dp.mismatch == 0;
        if (!support_control_ok) gate_domain = false;
        const bool gate_query = lits
            ? (query_probe.lits_present && query_probe.message_ok &&
               query_probe.asymmetric == 0 && query_probe.answer_mismatch == 0)
            : (query_probe.refused_off == 0 && query_probe.refused_on == 0 &&
               query_probe.asymmetric == 0 && query_probe.answer_mismatch == 0);
        const bool gate_preset = lits
            ? (preset.off_ok && preset.on_threw && preset.message_ok && preset.scan_equal)
            : (preset.off_ok && preset.on_ok && preset.scan_equal);
        const bool gate_pass = gate_full && gate_size && gate_absent && gate_scan && gate_ops &&
                               gate_split && gate_ownership && gate_prefix && gate_groupof &&
                               gate_domain && gate_query && gate_preset;

        std::cout << "{\"phase\":\"lifecycle\",\"backend\":\"" << backend << "\""
                  << ",\"rows\":" << rows.size()
                  << ",\"regions_off\":" << off.region_count()
                  << ",\"regions_on\":" << on.region_count()
                  << ",\"checkpoints\":" << t.checkpoints
                  << ",\"inserts\":" << t.inserts << ",\"erases\":" << t.erases
                  << ",\"updates\":" << t.updates
                  << ",\"full_finds\":" << t.full_finds
                  << ",\"absent_probes\":" << t.absent_probes
                  << ",\"scan_checks\":" << t.scan_checks
                  << ",\"scan_refusals\":" << t.scan_refusals
                  << ",\"scan_rows\":" << t.scan_rows
                  << ",\"size_mismatch\":" << t.size_mismatch
                  << ",\"full_find_mismatch\":" << t.full_find_mismatch
                  << ",\"absent_mismatch\":" << t.absent_mismatch
                  << ",\"scan_mismatch\":" << t.scan_mismatch
                  << ",\"off_on_mismatch\":" << t.off_on_mismatch
                  << ",\"op_result_mismatch\":" << t.op_result_mismatch
                  << ",\"splits_off_delta\":" << s_off_delta
                  << ",\"splits_on_delta\":" << s_on_delta
                  << ",\"rebuilds_off_delta\":" << rb_off_delta
                  << ",\"rebuilds_on_delta\":" << rb_on_delta
                  << ",\"child_split\":" << (child_split ? "true" : "false")
                  << ",\"child_left_ok\":" << (child_left_ok ? "true" : "false")
                  << ",\"child_right_ok\":" << (child_right_ok ? "true" : "false")
                  << ",\"child_regions\":[" << r_first_on << "," << r_last_on << ","
                  << r_left_on << "]"
                  << ",\"own_split_on\":{\"regions\":" << own_split_on.regions
                  << ",\"lits\":" << own_split_on.lits
                  << ",\"lits_with_table\":" << own_split_on.lits_with_table
                  << ",\"fallback\":" << own_split_on.fallback
                  << ",\"fallback_without_table\":" << own_split_on.fallback_without_table << "}"
                  << ",\"own_final_on\":{\"regions\":" << own_final_on.regions
                  << ",\"lits\":" << own_final_on.lits
                  << ",\"lits_with_table\":" << own_final_on.lits_with_table
                  << ",\"lits_without_table\":" << own_final_on.lits_without_table
                  << ",\"fallback\":" << own_final_on.fallback
                  << ",\"fallback_with_table\":" << own_final_on.fallback_with_table
                  << ",\"fallback_without_table\":" << own_final_on.fallback_without_table << "}"
                  << ",\"own_final_off\":{\"regions\":" << own_final_off.regions
                  << ",\"lits\":" << own_final_off.lits
                  << ",\"lits_without_table\":" << own_final_off.lits_without_table
                  << ",\"fallback_without_table\":" << own_final_off.fallback_without_table << "}"
                  << ",\"own_violations\":" << own_violations
                  << ",\"groupof\":{\"lits_present\":" << (group_probe.lits_present ? "true" : "false")
                  << ",\"on_refused\":" << (group_probe.on_refused ? "true" : "false")
                  << ",\"message_ok\":" << (group_probe.message_ok ? "true" : "false")
                  << ",\"off_ok\":" << (group_probe.off_ok ? "true" : "false")
                  << ",\"values_equal\":" << (group_probe.values_equal ? "true" : "false")
                  << ",\"mismatch\":" << group_probe.mismatch << "}"
                  << ",\"prefix\":[";
        for (std::size_t i = 0; i < prefix_cases.size(); ++i) {
            const auto& pc = prefix_cases[i];
            if (i) std::cout << ",";
            std::cout << "{\"name\":\"" << pc.name << "\""
                      << ",\"rebuilds_off_delta\":" << pc.rebuilds_off_delta
                      << ",\"rebuilds_on_delta\":" << pc.rebuilds_on_delta
                      << ",\"splits_off_delta\":" << pc.splits_off_delta
                      << ",\"splits_on_delta\":" << pc.splits_on_delta
                      << ",\"threw_off\":" << (pc.threw_off ? "true" : "false")
                      << ",\"threw_on\":" << (pc.threw_on ? "true" : "false")
                      << ",\"mismatch\":" << pc.mismatch
                      << ",\"lits_on\":" << pc.lits_on
                      << ",\"lits_with_table_on\":" << pc.lits_with_table_on
                      << ",\"fallback_on\":" << pc.fallback_on
                      << ",\"fallback_with_table_on\":" << pc.fallback_with_table_on << "}";
        }
        std::cout << "],\"domain\":[";
        for (std::size_t i = 0; i < domain.size(); ++i) {
            const auto& dp = domain[i];
            if (i) std::cout << ",";
            std::cout << "{\"name\":\"" << dp.name << "\""
                      << ",\"ops\":" << dp.ops
                      << ",\"refused_ops\":" << dp.refused_ops
                      << ",\"build_threw_off\":" << (dp.build_threw_off ? "true" : "false")
                      << ",\"build_threw_on\":" << (dp.build_threw_on ? "true" : "false")
                      << ",\"threw_off\":" << (dp.threw_off ? "true" : "false")
                      << ",\"threw_on\":" << (dp.threw_on ? "true" : "false")
                      << ",\"mismatch\":" << dp.mismatch
                      << ",\"build_what_off\":\"" << ownerlife::json_escape(dp.build_what_off) << "\""
                      << ",\"what_off\":\"" << ownerlife::json_escape(dp.what_off) << "\""
                      << "}";
        }
        std::cout << "],\"query_domain\":{\"probes\":" << query_probe.probes
                  << ",\"refused_off\":" << query_probe.refused_off
                  << ",\"refused_on\":" << query_probe.refused_on
                  << ",\"asymmetric\":" << query_probe.asymmetric
                  << ",\"answer_mismatch\":" << query_probe.answer_mismatch
                  << ",\"message_ok\":" << (query_probe.message_ok ? "true" : "false")
                  << ",\"lits_present\":" << (query_probe.lits_present ? "true" : "false")
                  << "}"
                  << ",\"domain_refusals\":" << domain_refusals
                  << ",\"support_control_ok\":" << (support_control_ok ? "true" : "false")
                  << ",\"preset\":{\"off_ok\":" << (preset.off_ok ? "true" : "false")
                  << ",\"on_threw\":" << (preset.on_threw ? "true" : "false")
                  << ",\"message_ok\":" << (preset.message_ok ? "true" : "false")
                  << ",\"on_ok\":" << (preset.on_ok ? "true" : "false")
                  << ",\"scan_equal\":" << (preset.scan_equal ? "true" : "false")
                  << ",\"off_what\":\"" << ownerlife::json_escape(preset.off_what) << "\""
                  << ",\"on_what\":\"" << ownerlife::json_escape(preset.on_what) << "\""
                  << "}"
                  << ",\"gate_full\":" << (gate_full ? "true" : "false")
                  << ",\"gate_size\":" << (gate_size ? "true" : "false")
                  << ",\"gate_absent\":" << (gate_absent ? "true" : "false")
                  << ",\"gate_scan\":" << (gate_scan ? "true" : "false")
                  << ",\"gate_ops\":" << (gate_ops ? "true" : "false")
                  << ",\"gate_split\":" << (gate_split ? "true" : "false")
                  << ",\"gate_ownership\":" << (gate_ownership ? "true" : "false")
                  << ",\"gate_prefix\":" << (gate_prefix ? "true" : "false")
                  << ",\"gate_groupof\":" << (gate_groupof ? "true" : "false")
                  << ",\"gate_query\":" << (gate_query ? "true" : "false")
                  << ",\"gate_domain\":" << (gate_domain ? "true" : "false")
                  << ",\"gate_preset\":" << (gate_preset ? "true" : "false")
                  << ",\"gate_pass\":" << (gate_pass ? "true" : "false")
                  << "}\n" << std::flush;
        return gate_pass ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << " [section=" << section << "]\n";
        return 2;
    }
}