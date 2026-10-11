import re, sys, hashlib

IDX = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\index.hpp'
MOD = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\model\block_linear_rank.hpp'

# normalize the new header to LF
raw = open(MOD, 'rb').read()
raw = raw.replace(b'\r\n', b'\n')
open(MOD, 'wb').write(raw)

src = open(IDX, 'r', encoding='utf-8', newline='').read()
orig_len = len(src)
assert '\r\n' not in src, 'index.hpp unexpectedly has CRLF'

def sub(text, anchor, repl, count=1, label=''):
    n = text.count(anchor)
    if n != count:
        raise SystemExit('ANCHOR FAIL %s: found %d, expected %d\n---\n%s' % (label, n, count, anchor[:400]))
    return text.replace(anchor, repl)

# ---------------- E1: include ----------------
a = '#include "model/lits_hpt_adapter.hpp"\n'
r = a + '''
// BlockLinearRank (2026-10-11): the third backend. Region-scoped -- it fits from the region's own
// sorted keys and keeps no shared state -- so unlike the LITS adapter it is included for the same
// structural reason (Region::calibrate_block_rank must exist) but brings no vendor dependency.
#include "model/block_linear_rank.hpp"
'''
src = sub(src, a, r, label='include')

# ---------------- E2: ModelBackend ----------------
a = 'enum class ModelBackend { Context16, LitsHpt };'
r = ('enum class ModelBackend { Context16, LitsHpt, BlockLinearRank };\n'
     '\n'
     '// The three backends answer the same question -- "which logical group does this key belong\n'
     '// to" -- with different machinery, and the index refuses combinations it does not implement\n'
     '// rather than substituting a near-enough mapping:\n'
     '//   Context16       the reference table below; trained per region on suffixes\n'
     '//   LitsHpt         one index-wide pinned HPT table, calibrated per region\n'
     '//   BlockLinearRank a per-region segmented rank model (model/block_linear_rank.hpp),\n'
     '//                   ported from the teammate model and fixed at 7 encoded bytes / 8 segments')
src = sub(src, a, r, label='enum')

# ---------------- E3: BlockRankStats ----------------
a = '''    std::uint64_t keys_calibrated = 0;
    std::uint64_t keys_fallback = 0;
    std::uint64_t keys_boundary = 0;
};
'''
r = a + '''
// BlockLinearRank: how the rank backend covered the regions it was asked to map. There is no
// fallback arm to report -- the 9-bit encoding accepts every byte string, so the only non-fitted
// outcomes are the three DEFINED boundary cases (no keys / one key / one group), and all three
// answer group 0, which is the answer the fit would give anyway. Counted for the same reason
// LitsStats is: a round must be able to say what mapping it actually timed.
struct BlockRankStats {
    std::uint64_t regions_fitted = 0;
    std::uint64_t keys_fitted = 0;
    std::uint64_t regions_boundary = 0;
    std::uint64_t keys_boundary = 0;
    std::uint64_t segments_total = 0;   // sum of segment counts over the fitted regions
    std::uint64_t fits = 0;             // fit() calls, boundary early-outs included
};
'''
src = sub(src, a, r, label='BlockRankStats')

# ---------------- E4: Region members ----------------
a = '''    bool use_lits_ = false;
'''
r = '''    bool use_lits_ = false;

    // ---- BlockLinearRank (2026-10-11) ---------------------------------------------------
    // A region-SCOPED rank model: no shared table, fitted from this region's own sorted keys,
    // and it owns everything it keeps -- a prefix LENGTH plus its segments, never a view into
    // the training keys. That lifetime rule is the one the LITS round had to fix, applied
    // before the fact instead of after.
    blockrank::RegionModel block_rank_;
    // Resolved ONCE, in the constructor, exactly like use_lits_: every later decision (group
    // mapping, demand pass, query path) reads this flag rather than re-deriving it. All four
    // model statuses define an answer, so this is set on every construction path.
    bool use_block_rank_ = false;
    inline static BlockRankStats block_rank_stats_{};
'''
src = sub(src, a, r, label='region members')

# ---------------- E5: canonical_group dispatch ----------------
a = '''        Fingerprint dummy;
        const std::uint32_t group = use_lits_
            ? lits_.predict_group(full_key)
            : model_.predict<false>(full_key.substr(prefix_.size()), groups_, config_.canonical,
                                    dummy).group;
'''
r = '''        Fingerprint dummy;
        const std::uint32_t group = use_block_rank_
            ? block_rank_.predict_group(full_key)
            : use_lits_
                ? lits_.predict_group(full_key)
                : model_.predict<false>(full_key.substr(prefix_.size()), groups_,
                                        config_.canonical, dummy).group;
'''
src = sub(src, a, r, label='canonical_group')

# ---------------- E6: predict_for dispatch ----------------
a = '''    template <bool Hash>
    Model::Prediction predict_for(std::string_view full_key, std::string_view suffix, Route route,
                                  Fingerprint& fp) const {
        if (use_lits_) {
'''
r = '''    template <bool Hash>
    Model::Prediction predict_for(std::string_view full_key, std::string_view suffix, Route route,
                                  Fingerprint& fp) const {
        if (use_block_rank_) {
            // Same two-field split as the LITS branch, for the same reason: the rank model reads
            // the full key from an absolute position, so the bytes it touches are NOT a prefix of
            // the suffix, and hashing them would produce a tag no stored record has. It reports
            // fp_consumed = 0 and the caller hashes the whole suffix.
            Model::Prediction p;
            p.group = block_rank_.predict_group(full_key, p.consumed);
            p.fp_consumed = 0;

            return p;
        }

        if (use_lits_) {
'''
src = sub(src, a, r, label='predict_for')

# ---------------- E7: constructor call ----------------
a = '''        if (config_.model_backend == ModelBackend::LitsHpt) {
            calibrate_lits(sorted, lits_shared_);
        }
'''
r = '''        if (config_.model_backend == ModelBackend::LitsHpt) {
            calibrate_lits(sorted, lits_shared_);
        } else if (config_.model_backend == ModelBackend::BlockLinearRank) {
            calibrate_block_rank(sorted);
        }
'''
src = sub(src, a, r, label='ctor call')

# ---------------- E8: calibrate_block_rank ----------------
a = '''            ++lits_stats_.regions_fallback;
            lits_stats_.keys_fallback += sorted.size();
        }
    }

public:
'''
r = '''            ++lits_stats_.regions_fallback;
            lits_stats_.keys_fallback += sorted.size();
        }
    }

    // BlockLinearRank (2026-10-11): fit this region's rank model.
    //
    // WHERE THIS RUNS. After `prefix_` and `groups_` exist and BEFORE the demand pass, exactly
    // where the LITS calibration runs and for the same reason: the pass below asks
    // canonical_group for every key, and the plan it computes has to describe the layout this
    // region will actually build.
    //
    // NO FALLBACK ARM. The rank model's input domain is every byte string -- empty key, embedded
    // NUL, high bytes, arbitrary length -- so there is no admission decision to get wrong and no
    // region is silently mapped by the reference model under this backend's name. The three
    // boundary statuses still answer group 0, which is a defined answer rather than a failure.
    //
    // The combination refusals mirror calibrate_lits: a pairing this backend does not implement
    // is REFUSED, not mapped to something near enough.
    void calibrate_block_rank(const std::vector<Pair>& sorted) {
        if (config_.model_kind != ModelKind::Context16) {
            throw std::invalid_argument("BlockLinearRank supports ModelKind::Context16 only");
        }
        if (config_.canonical != Route::Full) {
            throw std::invalid_argument("BlockLinearRank supports Route::Full only");
        }

        std::vector<std::string_view> keys;
        keys.reserve(sorted.size());
        for (const auto& kv : sorted) keys.push_back(kv.first);

        block_rank_ = blockrank::RegionModel::fit(keys.data(), keys.size(), prefix_.size(), groups_);
        use_block_rank_ = true;
        ++block_rank_stats_.fits;

        if (block_rank_.fitted()) {
            ++block_rank_stats_.regions_fitted;
            block_rank_stats_.keys_fitted += sorted.size();
            block_rank_stats_.segments_total += block_rank_.segment_count();
        } else {
            ++block_rank_stats_.regions_boundary;
            block_rank_stats_.keys_boundary += sorted.size();
        }
    }

public:
'''
src = sub(src, a, r, label='calibrate_block_rank')

# ---------------- E9: accessors ----------------
a = '''    // True when this region was mapped by the LITS table rather than by the reference model.
    bool uses_lits() const { return use_lits_; }
'''
r = a + '''    // True when this region was mapped by the rank backend.
    bool uses_block_rank() const { return use_block_rank_; }
    blockrank::Status block_rank_status() const { return block_rank_.status(); }
    static const BlockRankStats& block_rank_stats() { return block_rank_stats_; }
    static void reset_block_rank_stats() { block_rank_stats_ = BlockRankStats{}; }
'''
src = sub(src, a, r, label='accessors')

# ---------------- E10: mapping_identity ----------------
a = '''        for (const auto& row : m.cumulative)
            mix_bytes(row.data(), row.size() * sizeof(std::uint32_t));
        return h;
    }
'''
r = a + '''
    // The identity the R1/R2 preset guard compares.
    //
    // For the reference and LITS backends this is the Context16 table digest it has always been
    // (the LITS table is index-wide, so both sides hold the identical object). For
    // BlockLinearRank the region's OWN fitted model is mixed in, because under that backend the
    // rank model IS the mapping: two different rank models that happened to leave the reference
    // table equal must not look like the same mapping to the guard.
    static std::uint64_t mapping_identity(const Model& m, std::uint32_t groups, Route canonical,
                                          ModelBackend backend,
                                          const blockrank::RegionModel& block_rank) {
        std::uint64_t h = model_identity(m, groups, canonical);
        if (backend != ModelBackend::BlockLinearRank) return h;
        const std::uint64_t tag = block_rank.identity();
        auto mix_bytes = [&h](const void* p, std::size_t n) {
            const auto* b = static_cast<const unsigned char*>(p);
            for (std::size_t i = 0; i < n; ++i) {
                h ^= b[i];
                h *= 1099511628211ULL;
            }
        };
        mix_bytes(&tag, sizeof(tag));
        return h;
    }
'''
src = sub(src, a, r, label='mapping_identity')

# ---------------- E11: preset guard uses mapping_identity ----------------
a = '''                               preset->model_digest ==
                                   model_identity(model_, groups_, config_.canonical);
'''
r = '''                               preset->model_digest ==
                                   mapping_identity(model_, groups_, config_.canonical,
                                                    config_.model_backend, block_rank_);
'''
src = sub(src, a, r, label='preset guard')

# ---------------- E12: demand pass needs the same model ----------------
a = '''        bool use_lits = false;
        lits_hpt::RegionModel lits;
'''
r = '''        bool use_lits = false;
        lits_hpt::RegionModel lits;
        // BlockLinearRank: the SAME fit the region will make, over the same key range, prefix and
        // group count. Measuring demand with one mapping and placing with another is the mistake
        // the E38 comment above names, and it would not show up as an error -- only as a plan
        // that fits the wrong demand.
        bool use_block_rank = false;
        blockrank::RegionModel block_rank;
'''
src = sub(src, a, r, label='demand decl')

a = '''            // A region that fell back to the reference model in the real build must fall back
            // HERE too, or the plan would fund a layout the build will not produce.
            use_lits = lits.usable();
        }
'''
r = a + '''
        if (cfg.model_backend == ModelBackend::BlockLinearRank) {
            std::vector<std::string_view> block_keys;
            block_keys.reserve(n);
            for (const Pair* it = first; it != last; ++it) block_keys.push_back(it->first);
            block_rank = blockrank::RegionModel::fit(block_keys.data(), block_keys.size(),
                                                     prefix.size(), groups);
            use_block_rank = true;
        }
'''
src = sub(src, a, r, label='demand fit')

a = '''            const std::uint32_t g = use_lits
                ? lits.predict_group(first[i].first)
                : model.predict<false>(s, groups, cfg.canonical, dummy).group;
'''
r = '''            const std::uint32_t g = use_block_rank
                ? block_rank.predict_group(first[i].first)
                : use_lits
                    ? lits.predict_group(first[i].first)
                    : model.predict<false>(s, groups, cfg.canonical, dummy).group;
'''
src = sub(src, a, r, label='demand dispatch')

# ---------------- E13: demand digest ----------------
a = '        rd.model_digest = model_identity(model, groups, cfg.canonical);'
r = ('        rd.model_digest =\n'
     '            mapping_identity(model, groups, cfg.canonical, cfg.model_backend, block_rank);')
src = sub(src, a, r, label='demand digest')

# ---------------- E14: memory breakdown ----------------
a = '        m.model = model_.allocated_bytes();'
r = ('        // The rank backend\'s segments are resident model memory too, so they belong in the\n'
     '        // same field instead of being left out of the decomposition.\n'
     '        m.model = model_.allocated_bytes() + block_rank_.owned_bytes();')
src = sub(src, a, r, label='memory breakdown')

open(IDX, 'w', encoding='utf-8', newline='').write(src)
print('index.hpp patched:', orig_len, '->', len(src), 'chars')
print('sha256', hashlib.sha256(open(IDX,'rb').read()).hexdigest())
