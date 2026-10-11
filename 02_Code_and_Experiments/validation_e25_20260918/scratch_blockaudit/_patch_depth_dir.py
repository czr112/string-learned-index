import hashlib, os
IDX = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\index.hpp'
s = open(IDX, 'r', encoding='utf-8', newline='').read()
print('before len', len(s))

def sub(a, r, label):
    global s
    if s.count(a) != 1:
        raise SystemExit('ANCHOR FAIL %s (%d)' % (label, s.count(a)))
    s = s.replace(a, r)

# ---- 1 include ----
sub('#include "model/block_linear_rank.hpp"\n',
    '''#include "model/block_linear_rank.hpp"

// EqualDepthDir (2026-10-11): the fourth backend. A region-scoped SEPARATOR DIRECTORY -- the
// empirical quantiles of the region's own keys -- chosen by the entry diagnosis
// (RESULTS_THRESHOLD_ENTRY_20261011.md): max group load 1366/824/546 -> 12/12/12, empty groups
// -> 0, and 0.6-5.0 KB per region once separators are truncated. The 63-bit COORDINATE form of
// the same idea was measured and rejected (skew: 64/64 regions with duplicate thresholds).
#include "model/equal_depth_dir.hpp"
''', 'include')

# ---- 2 enum ----
sub('enum class ModelBackend { Context16, LitsHpt, BlockLinearRank };',
    'enum class ModelBackend { Context16, LitsHpt, BlockLinearRank, EqualDepthDir };', 'enum')

sub('''//   BlockLinearRank a per-region segmented rank model (model/block_linear_rank.hpp),
//                   ported from the teammate model and fixed at 7 encoded bytes / 8 segments''',
'''//   BlockLinearRank a per-region segmented rank model (model/block_linear_rank.hpp),
//                   ported from the teammate model and fixed at 7 encoded bytes / 8 segments
//   EqualDepthDir   a per-region separator directory at equal key RANKS
//                   (model/equal_depth_dir.hpp): balances the group load by construction''', 'enum doc')

# ---- 3 stats struct ----
sub('''    std::uint64_t segments_total = 0;   // sum of segment counts over the fitted regions
    std::uint64_t fits = 0;             // fit() calls, boundary early-outs included
};''',
'''    std::uint64_t segments_total = 0;   // sum of segment counts over the fitted regions
    std::uint64_t fits = 0;             // fit() calls, boundary early-outs included
};

// EqualDepthDir: the same accounting for the directory backend. `duplicate_thresholds` counts
// the regions where two thresholds are equal, i.e. where the depth could NOT be spread -- the
// one place the balance is limited by the keys themselves rather than by the map.
struct DepthDirStats {
    std::uint64_t regions_fitted = 0;
    std::uint64_t keys_fitted = 0;
    std::uint64_t regions_boundary = 0;
    std::uint64_t keys_boundary = 0;
    std::uint64_t thresholds_total = 0;
    std::uint64_t regions_with_duplicate_thresholds = 0;
    std::uint64_t fits = 0;
};''', 'stats struct')

# ---- 4 region members ----
sub('''    bool use_block_rank_ = false;
    inline static BlockRankStats block_rank_stats_{};''',
'''    bool use_block_rank_ = false;
    inline static BlockRankStats block_rank_stats_{};

    // ---- EqualDepthDir (2026-10-11) -----------------------------------------------------
    // A region-scoped separator directory. It owns its arena and offsets; nothing it keeps
    // points into the training keys. Same lifetime rule as the rank model above.
    equivdir::RegionModel depth_dir_;
    bool use_depth_dir_ = false;
    inline static DepthDirStats depth_dir_stats_{};''', 'region members')

# ---- 5 canonical_group ----
sub('''        Fingerprint dummy;
        const std::uint32_t group = use_block_rank_
            ? block_rank_.predict_group(full_key)
            : use_lits_
                ? lits_.predict_group(full_key)
                : model_.predict<false>(full_key.substr(prefix_.size()), groups_,
                                        config_.canonical, dummy).group;''',
'''        Fingerprint dummy;
        const std::uint32_t group = use_depth_dir_
            ? depth_dir_.predict_group(full_key)
            : use_block_rank_
                ? block_rank_.predict_group(full_key)
                : use_lits_
                    ? lits_.predict_group(full_key)
                    : model_.predict<false>(full_key.substr(prefix_.size()), groups_,
                                            config_.canonical, dummy).group;''', 'canonical_group')

# ---- 6 predict_for ----
sub('''        if (use_block_rank_) {
            // Same two-field split as the LITS branch, for the same reason: the rank model reads''',
'''        if (use_depth_dir_) {
            // The directory reads the full key from arbitrary positions of the suffix, so it
            // contributes nothing to the fingerprint -- same split as the two branches below.
            Model::Prediction p;
            p.group = depth_dir_.predict_group(full_key, p.consumed);
            p.fp_consumed = 0;

            return p;
        }

        if (use_block_rank_) {
            // Same two-field split as the LITS branch, for the same reason: the rank model reads''', 'predict_for')

# ---- 7 calibrate + ctor call ----
sub('''        } else if (config_.model_backend == ModelBackend::BlockLinearRank) {
            calibrate_block_rank(sorted);
        }''',
'''        } else if (config_.model_backend == ModelBackend::BlockLinearRank) {
            calibrate_block_rank(sorted);
        } else if (config_.model_backend == ModelBackend::EqualDepthDir) {
            calibrate_depth_dir(sorted);
        }''', 'ctor call')

# ---- 8 calibrate_depth_dir body (insert before the trailing public:) ----
sub('''        if (block_rank_.fitted()) {
            ++block_rank_stats_.regions_fitted;
            block_rank_stats_.keys_fitted += sorted.size();
            block_rank_stats_.segments_total += block_rank_.segment_count();
        } else {
            ++block_rank_stats_.regions_boundary;
            block_rank_stats_.keys_boundary += sorted.size();
        }
    }

public:''',
'''        if (block_rank_.fitted()) {
            ++block_rank_stats_.regions_fitted;
            block_rank_stats_.keys_fitted += sorted.size();
            block_rank_stats_.segments_total += block_rank_.segment_count();
        } else {
            ++block_rank_stats_.regions_boundary;
            block_rank_stats_.keys_boundary += sorted.size();
        }
    }

    // EqualDepthDir (2026-10-11): build this region's separator directory.
    //
    // Runs where the other two calibrations run -- after `prefix_` and `groups_` exist and
    // BEFORE the demand pass, which asks canonical_group for every key. The plan it computes has
    // to describe the layout this region will build.
    //
    // No fallback arm: the directory accepts every byte string (it compares arbitrary suffixes),
    // so the only non-fitted outcomes are the three DEFINED boundary cases, all of which answer
    // group 0. Combination refusals mirror the other backends.
    void calibrate_depth_dir(const std::vector<Pair>& sorted) {
        if (config_.model_kind != ModelKind::Context16) {
            throw std::invalid_argument("EqualDepthDir supports ModelKind::Context16 only");
        }
        if (config_.canonical != Route::Full) {
            throw std::invalid_argument("EqualDepthDir supports Route::Full only");
        }

        std::vector<std::string_view> keys;
        keys.reserve(sorted.size());
        for (const auto& kv : sorted) keys.push_back(kv.first);

        depth_dir_ = equivdir::RegionModel::fit(keys.data(), keys.size(), prefix_.size(), groups_);
        use_depth_dir_ = true;
        ++depth_dir_stats_.fits;

        if (depth_dir_.fitted()) {
            ++depth_dir_stats_.regions_fitted;
            depth_dir_stats_.keys_fitted += sorted.size();
            depth_dir_stats_.thresholds_total += depth_dir_.threshold_count();

            // A duplicate threshold means the depth band boundary fell inside a run of equal
            // suffixes: that group can hold no key, and no map of the suffix alone can fix it.
            // Counted here so a round can say whether the balance was limited by the keys.
            std::vector<std::string_view> seps;
            for (std::size_t j = 1; j <= depth_dir_.threshold_count(); ++j) {
                const auto rank = std::min(sorted.size() - 1, (j * sorted.size()) / groups_);
                seps.push_back(sorted[rank].first);
            }
            std::sort(seps.begin(), seps.end(), [](std::string_view a, std::string_view b) {
                return equivdir::compare(a, b) < 0;
            });
            for (std::size_t j = 1; j < seps.size(); ++j)
                if (equivdir::compare(seps[j - 1], seps[j]) == 0) {
                    ++depth_dir_stats_.regions_with_duplicate_thresholds;
                    break;
                }
        } else {
            ++depth_dir_stats_.regions_boundary;
            depth_dir_stats_.keys_boundary += sorted.size();
        }
    }

public:''', 'calibrate_depth_dir')

# ---- 9 accessors ----
sub('''    blockrank::Status block_rank_status() const { return block_rank_.status(); }
    static const BlockRankStats& block_rank_stats() { return block_rank_stats_; }
    static void reset_block_rank_stats() { block_rank_stats_ = BlockRankStats{}; }''',
'''    blockrank::Status block_rank_status() const { return block_rank_.status(); }
    static const BlockRankStats& block_rank_stats() { return block_rank_stats_; }
    static void reset_block_rank_stats() { block_rank_stats_ = BlockRankStats{}; }
    bool uses_depth_dir() const { return use_depth_dir_; }
    equivdir::Status depth_dir_status() const { return depth_dir_.status(); }
    std::size_t depth_dir_thresholds() const { return depth_dir_.threshold_count(); }
    static const DepthDirStats& depth_dir_stats() { return depth_dir_stats_; }
    static void reset_depth_dir_stats() { depth_dir_stats_ = DepthDirStats{}; }''', 'accessors')

# ---- 10 mapping_identity: param + branch ----
sub('''    static std::uint64_t mapping_identity(const Model& m, std::uint32_t groups, Route canonical,
                                          ModelBackend backend,
                                          const blockrank::RegionModel& block_rank) {
        std::uint64_t h = model_identity(m, groups, canonical);
        if (backend != ModelBackend::BlockLinearRank) return h;
        const std::uint64_t tag = block_rank.identity();''',
'''    static std::uint64_t mapping_identity(const Model& m, std::uint32_t groups, Route canonical,
                                          ModelBackend backend,
                                          const blockrank::RegionModel& block_rank,
                                          const equivdir::RegionModel& depth_dir) {
        std::uint64_t h = model_identity(m, groups, canonical);
        if (backend == ModelBackend::EqualDepthDir) {
            const std::uint64_t dtag = depth_dir.identity();
            auto mix = [&h](const void* p, std::size_t n) {
                const auto* b = static_cast<const unsigned char*>(p);
                for (std::size_t i = 0; i < n; ++i) {
                    h ^= b[i];
                    h *= 1099511628211ULL;
                }
            };
            mix(&dtag, sizeof(dtag));
            return h;
        }
        if (backend != ModelBackend::BlockLinearRank) return h;
        const std::uint64_t tag = block_rank.identity();''', 'mapping_identity')

sub('''                               preset->model_digest ==
                                   mapping_identity(model_, groups_, config_.canonical,
                                                    config_.model_backend, block_rank_);''',
'''                               preset->model_digest ==
                                   mapping_identity(model_, groups_, config_.canonical,
                                                    config_.model_backend, block_rank_,
                                                    depth_dir_);''', 'preset guard')

sub('''        rd.model_digest =
            mapping_identity(model, groups, cfg.canonical, cfg.model_backend, block_rank);''',
'''        rd.model_digest =
            mapping_identity(model, groups, cfg.canonical, cfg.model_backend, block_rank,
                             depth_dir);''', 'demand digest')

# ---- 11 demand pass ----
sub('''        bool use_block_rank = false;
        blockrank::RegionModel block_rank;''',
'''        bool use_block_rank = false;
        blockrank::RegionModel block_rank;
        // EqualDepthDir: the same directory the region will build, from the same key range,
        // prefix and group count. Measuring demand with one map and placing with another is the
        // mistake the E38 comment above names.
        bool use_depth_dir = false;
        equivdir::RegionModel depth_dir;''', 'demand decl')

sub('''            block_rank = blockrank::RegionModel::fit(block_keys.data(), block_keys.size(),
                                                     prefix.size(), groups);
            use_block_rank = true;
        }''',
'''            block_rank = blockrank::RegionModel::fit(block_keys.data(), block_keys.size(),
                                                     prefix.size(), groups);
            use_block_rank = true;
        }

        if (cfg.model_backend == ModelBackend::EqualDepthDir) {
            std::vector<std::string_view> depth_keys;
            depth_keys.reserve(n);
            for (const Pair* it = first; it != last; ++it) depth_keys.push_back(it->first);
            depth_dir = equivdir::RegionModel::fit(depth_keys.data(), depth_keys.size(),
                                                   prefix.size(), groups);
            use_depth_dir = true;
        }''', 'demand fit')

sub('''            const std::uint32_t g = use_block_rank
                ? block_rank.predict_group(first[i].first)
                : use_lits
                    ? lits.predict_group(first[i].first)
                    : model.predict<false>(s, groups, cfg.canonical, dummy).group;''',
'''            const std::uint32_t g = use_depth_dir
                ? depth_dir.predict_group(first[i].first)
                : use_block_rank
                    ? block_rank.predict_group(first[i].first)
                    : use_lits
                        ? lits.predict_group(first[i].first)
                        : model.predict<false>(s, groups, cfg.canonical, dummy).group;''',
    'demand dispatch')

# ---- 12 memory ----
sub('''        m.model = model_.allocated_bytes() + block_rank_.owned_bytes();''',
'''        m.model = model_.allocated_bytes() + block_rank_.owned_bytes() +
                  depth_dir_.owned_bytes();''', 'memory')

open(IDX, 'w', encoding='utf-8', newline='').write(s)
print('index.hpp patched:', len(s), 'chars, sha256',
      hashlib.sha256(open(IDX,'rb').read()).hexdigest())
