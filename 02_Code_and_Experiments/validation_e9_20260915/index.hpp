#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__SSE4_2__)
#include <nmmintrin.h>
#endif

namespace validation {

// ---------------------------------------------------------------------------
// Ordering contract: unsigned-byte lexicographic comparison, shared by every
// structure (model grouping, in-block ordering, outer routing, reference map).
// ---------------------------------------------------------------------------

inline int compare(std::string_view a, std::string_view b) {
    const auto n = std::min(a.size(), b.size());
    const int c = n ? std::memcmp(a.data(), b.data(), n) : 0;
    return c ? c : (a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0);
}

struct Less {
    bool operator()(std::string_view a, std::string_view b) const {
        return compare(a, b) < 0;
    }
};

using Pair = std::pair<std::string, std::uint64_t>;

inline bool pair_less(const Pair& a, const Pair& b) {
    return compare(a.first, b.first) < 0;
}

// ---------------------------------------------------------------------------
// Fingerprint: CRC-32C tag used only as a candidate filter; a matching tag never
// proves equality, every hit goes through the exact compare.
// ---------------------------------------------------------------------------

struct Fingerprint {
    std::uint32_t state = 0x9e3779b9U;

    void byte(std::uint8_t x) {
#if defined(__SSE4_2__)
        state = _mm_crc32_u8(state, x);
#else
        state ^= x;
        for (int i = 0; i < 8; ++i)
            state = (state >> 1) ^ (0x82f63b78U & (0U - (state & 1U)));
#endif
    }

    void tail(std::string_view x) {
#if defined(__SSE4_2__)
        while (x.size() >= 8) {
            std::uint64_t w;
            std::memcpy(&w, x.data(), 8);
            state = static_cast<std::uint32_t>(_mm_crc32_u64(state, w));
            x.remove_prefix(8);
        }
#endif
        for (unsigned char c : x) byte(c);
    }

    std::uint8_t tag() const { return static_cast<std::uint8_t>(state); }

    static std::uint8_t of(std::string_view s) {
        Fingerprint f;
        f.tail(s);
        return f.tag();
    }
};

// ---------------------------------------------------------------------------
// Lookup configuration and per-query result.
// ---------------------------------------------------------------------------

enum class Route { Full, Interval, Residual };
enum class Prefetch { Off, Early, Late };

// E5-2: mode.simd selects the batched SSE2 tag filter; identical results by
// construction.
// E7: overflow point-lookup policy rides on the per-query Mode (not Config):
// the overflow records and their sorted order are identical for every policy,
// so all variants measure the same records with a different lookup algorithm.
enum class OvSearch { Linear, Binary, Hybrid };

struct Mode {
    Route route;
    Prefetch prefetch;
    bool fingerprint;
    bool simd = false;
    OvSearch ov = OvSearch::Linear;
    std::uint32_t ov_thr = 32;
};

struct Result {
    bool found = false;
    std::uint64_t value = 0;
    std::uint32_t steps = 0;
    std::uint32_t overflow_checks = 0;
    std::uint32_t str_cmps = 0;   // E7: full string comparisons for this lookup
    std::uint8_t source = 0;      // E7: 0 miss, 1 primary hit, 2 overflow hit
    std::uint64_t cmp_bytes = 0;  // E7 diag: bytes walked by compares (E7_COUNT_BYTES builds)
};

// E5-1: slots is an explicit layout parameter (default 16 reproduces the E3 behavior).
enum class ModelKind { Context16, Position16, Unigram };

struct Config {
    std::uint32_t block_bytes = 1024;
    double load = .75;
    std::size_t region_keys = 2048;
    Route canonical = Route::Full;
    std::uint32_t fixed_groups = 0;
    ModelKind model_kind = ModelKind::Context16;
    std::uint32_t slots = 16;
    // E8-11: false = legacy fixed-stride addressing (base + g*block_bytes, uniform
    // capacity only); true = offset-directory addressing (required for demand caps).
    bool offset_directory = true;
    // E9: when true, a group whose demand exceeds one sub-block grows to several
    // equal-sized sub-blocks within the same table shape. Off by default, so the
    // single-block path stays bit-identical to E5-E8 and their gates keep passing.
    bool multiblock = false;
};

#ifdef E7_COUNT_BYTES
// Diagnostic builds only: mirrors compare() while counting bytes actually walked.
// memcmp stops at the first difference, so one call does not imply a full scan.
inline int compare_bytes(std::string_view a, std::string_view b, std::uint64_t& bytes) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && static_cast<unsigned char>(a[i]) == static_cast<unsigned char>(b[i])) ++i;
    bytes += (i < n) ? i + 1 : n;
    int c = 0;
    if (i < n)
        c = static_cast<unsigned char>(a[i]) < static_cast<unsigned char>(b[i]) ? -1 : 1;
    else
        c = a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0;
    return c;
}
#endif

// ---------------------------------------------------------------------------
// Conditional model: integer interval arithmetic over quantized probabilities.
// Q = 2^47 position scale, MASS = 2^16 probability mass, 16 context rows.
// ---------------------------------------------------------------------------

class Model {
public:
    static constexpr std::uint64_t Q = 1ULL << 47;
    static constexpr std::uint32_t MASS = 1U << 16;
    static constexpr std::size_t ROWS = 16;

    using Table = std::vector<std::array<std::uint32_t, 258>>;

    Table cumulative;
    ModelKind kind = ModelKind::Context16;

    std::size_t row(std::size_t pos, unsigned previous) const {
        if (kind == ModelKind::Unigram) return 0;
        if (kind == ModelKind::Position16) return std::min<std::size_t>(15, pos);
        return ((pos & 3U) << 2U) | (previous & 3U);
    }

    std::size_t allocated_bytes() const {
        return cumulative.capacity() * sizeof(Table::value_type);
    }

    void train(const std::vector<Pair>& keys, std::size_t prefix,
               ModelKind choice = ModelKind::Context16) {
        kind = choice;
        const auto rows = kind == ModelKind::Unigram ? 1 : ROWS;
        cumulative.assign(rows, {});

        std::vector<std::array<std::uint64_t, 257>> counts(rows);
        for (auto& r : counts) r.fill(1);  // finite smoothing, including EOS and byte 0

        for (const auto& kv : keys) {
            const auto s = std::string_view(kv.first).substr(prefix);
            unsigned previous = 0;
            const auto lim = std::min<std::size_t>(16, s.size());
            for (std::size_t p = 0; p < lim; ++p) {
                unsigned d = static_cast<unsigned char>(s[p]) + 1U;
                ++counts[row(p, previous)][d];
                previous = d;
            }
            if (s.size() <= 16) ++counts[row(s.size(), previous)][0];
        }

        for (std::size_t r = 0; r < rows; ++r) {
            std::uint64_t total = 0;
            for (auto x : counts[r]) total += x;

            std::array<std::uint32_t, 257> frequency{};
            std::uint32_t sum = 0;
            for (std::size_t d = 0; d < 257; ++d) {
                frequency[d] = static_cast<std::uint32_t>(counts[r][d] * MASS / total);
                sum += frequency[d];
            }
            // Deterministic remainder assignment; at most 256 residual units.
            for (std::uint32_t d = 0; d < MASS - sum; ++d) ++frequency[d];

            cumulative[r][0] = 0;
            for (std::size_t d = 0; d < 257; ++d)
                cumulative[r][d + 1] = cumulative[r][d] + frequency[d];
            assert(cumulative[r][257] == MASS);
        }
    }

    // Interval step; both endpoints derive from the pre-update lo to avoid
    // ordering mistakes. W <= 2^47 and C <= 2^16, so the product fits uint64.
    static void step(const std::array<std::uint32_t, 258>& c, unsigned d,
                     std::uint64_t& lo, std::uint64_t& hi) {
        const std::uint64_t origin = lo, w = hi - lo;
        lo = origin + ((w * c[d]) >> 16);
        hi = origin + ((w * c[d + 1]) >> 16);
    }

    // Physical mapping h: monotone in x, hence group order follows key order.
    static std::uint32_t bucket(std::uint64_t x, std::uint32_t groups) {
        const auto g = static_cast<std::uint64_t>((static_cast<unsigned __int128>(x) * groups) >> 47);
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(groups - 1, g));
    }

    struct Prediction {
        std::uint32_t group;
        std::uint32_t consumed;
    };

    template <bool Hash>
    Prediction predict(std::string_view s, std::uint32_t groups, Route route,
                       Fingerprint& fp) const {
        std::uint64_t lo = 0, hi = Q;
        unsigned previous = 0;
        std::size_t p = 0;
        for (; p < s.size(); ++p) {
            const auto ch = static_cast<unsigned char>(s[p]);
            step(cumulative[row(p, previous)], ch + 1U, lo, hi);
            previous = ch + 1U;
            if constexpr (Hash) fp.byte(ch);

            const bool stop =
                (lo == hi) ||
                (route == Route::Interval && bucket(lo, groups) == bucket(hi, groups)) ||
                (route == Route::Residual && static_cast<unsigned __int128>(hi - lo) * groups < Q);
            if (stop) { ++p; break; }
        }
        // EOS has cumulative 0, so the final coordinate is the current lower bound.
        return {bucket(lo, groups), static_cast<std::uint32_t>(p)};
    }
};

// ---------------------------------------------------------------------------
// On-block layout: one block per group, 64B-aligned. The suffix arena grows
// downward from the block end; tags sit at offset 16 (16B-aligned for SIMD).
// ---------------------------------------------------------------------------

struct Entry {
    std::uint32_t offset;
    std::uint32_t length;
    std::uint64_t value;
};

// E5-1: tags[48] keeps the header at 64B for every supported slot count (S<=48);
// tags start at offset 16, so every 16-tag chunk is 16-byte aligned for SIMD loads.
struct alignas(64) Header {
    std::uint64_t overflow_pointer;
    std::uint16_t count, bottom, flags, reserved0;
    std::uint8_t tags[48];
};

static_assert(sizeof(Entry) == 16 && sizeof(Header) == 64);
static_assert(sizeof(std::uintptr_t) == 8, "This validation layout assumes 64-bit pointers");

struct GroupStats {
    std::uint32_t group = 0;
    std::size_t primary_entries = 0, primary_bytes = 0, overflow_entries = 0, overflow_bytes = 0;
    std::size_t free_entries = 0, free_bytes = 0, garbage_bytes = 0;
    std::size_t blocked_slots_only = 0, blocked_bytes_only = 0, blocked_both = 0, oversized = 0, fits_now = 0;
};

// ---------------------------------------------------------------------------
// Region: one contiguous lexicographic interval = model + capacity table + blocks.
// ---------------------------------------------------------------------------

class Region {
    // E5-1: the entry-slot count S and the entries-area end are per-region values now.
    static constexpr std::size_t MAX_SLOTS = 48;

    struct OverflowRecord {
        std::string suffix;
        std::uint64_t value;
        std::uint8_t tag;
    };
    using Overflow = std::vector<OverflowRecord>;

    Config config_;

    // E8: per-group capacity table. Uniform builds fill every group with the same
    // (slots, block bytes); demand builds give crowded groups more of both.
    //
    // E9: a group owns subblocks(g) consecutive, EQUAL-SIZED sub-blocks, and the
    // tables below describe ONE sub-block rather than the whole group. The count
    // is derived from off_ and bb_, so multi-block layouts add no locator metadata
    // at all. nb == 1 reproduces the E8 behaviour exactly.
    std::vector<std::uint16_t> sg_;   // slots per sub-block (<= MAX_SLOTS)
    std::vector<std::uint32_t> bb_;   // bytes per sub-block (64-aligned)
    std::vector<std::size_t> off_;    // group start offsets, size groups_+1

    std::string prefix_;
    Model model_;
    std::byte* storage_ = nullptr;
    std::uint32_t groups_ = 1;
    std::size_t size_ = 0, overflow_count_ = 0;
    std::size_t compactions = 0;   // E9 gate instrumentation: cross-sub-block repacks
    std::vector<std::unique_ptr<Overflow>> overflow_owners_;

    // --- per-group geometry ---

    std::size_t slots_of(std::uint32_t g) const { return sg_[g]; }

    std::size_t start_of(std::uint32_t g) const {
        return sizeof(Header) + std::size_t(sg_[g]) * sizeof(Entry);
    }

    // E9: sub-blocks of a group are consecutive and equal-sized, so their number
    // and base addresses follow from the two existing tables. No new metadata.
    std::size_t subblocks(std::uint32_t g) const {
        return (off_[g + 1] - off_[g]) / bb_[g];
    }

    std::size_t subblock_offset(std::uint32_t g, std::size_t i) const {
        return off_[g] + i * bb_[g];
    }

    std::byte* base_of(std::uint32_t g, std::size_t i) {
        return config_.offset_directory
            ? storage_ + subblock_offset(g, i)
            : storage_ + std::size_t(g) * config_.block_bytes;
    }

    const std::byte* base_of(std::uint32_t g, std::size_t i) const {
        return config_.offset_directory
            ? storage_ + subblock_offset(g, i)
            : storage_ + std::size_t(g) * config_.block_bytes;
    }

    Header& header(std::uint32_t g, std::size_t i) {
        return *reinterpret_cast<Header*>(base_of(g, i));
    }
    const Header& header(std::uint32_t g, std::size_t i) const {
        return *reinterpret_cast<const Header*>(base_of(g, i));
    }

    Entry* entries(std::uint32_t g, std::size_t i) {
        return reinterpret_cast<Entry*>(base_of(g, i) + sizeof(Header));
    }
    const Entry* entries(std::uint32_t g, std::size_t i) const {
        return reinterpret_cast<const Entry*>(base_of(g, i) + sizeof(Header));
    }

    // The overflow container belongs to the logical group and is anchored in
    // sub-block 0; the other sub-blocks leave overflow_pointer at zero.
    Overflow* overflow(std::uint32_t g) const {
        return reinterpret_cast<Overflow*>(static_cast<std::uintptr_t>(header(g, 0).overflow_pointer));
    }

    std::string_view suffix(std::uint32_t g, std::size_t i, const Entry& e) const {
        assert(e.offset >= start_of(g) && std::uint64_t(e.offset) + e.length <= bb_[g]);
        return {reinterpret_cast<const char*>(base_of(g, i) + e.offset), e.length};
    }

    bool prefix_matches(std::string_view s) const {
        return s.size() >= prefix_.size() &&
               (!prefix_.size() || std::memcmp(s.data(), prefix_.data(), prefix_.size()) == 0);
    }

    std::uint32_t canonical_group(std::string_view s) const {
        Fingerprint dummy;
        return model_.predict<false>(s, groups_, config_.canonical, dummy).group;
    }

    // --- write paths ---

    // Insert into one sub-block at its sorted position; false if it has no room.
    bool insert_into(std::uint32_t g, std::size_t i, std::string_view s,
                     std::uint64_t value, std::uint8_t tag) {
        auto& h = header(g, i);
        if (h.count >= slots_of(g) || s.size() > h.bottom - start_of(g)) return false;

        auto* es = entries(g, i);
        std::size_t p = 0;
        while (p < h.count && compare(suffix(g, i, es[p]), s) < 0) ++p;
        for (std::size_t j = h.count; j > p; --j) {
            es[j] = es[j - 1];
            h.tags[j] = h.tags[j - 1];
        }
        h.bottom = static_cast<std::uint16_t>(h.bottom - s.size());
        if (!s.empty()) std::memcpy(base_of(g, i) + h.bottom, s.data(), s.size());
        es[p] = {h.bottom, static_cast<std::uint32_t>(s.size()), value};
        h.tags[p] = tag;
        ++h.count;
        return true;
    }

    // E9: a group's sub-blocks are rank-ordered, so every key in sub-block i is
    // smaller than every key in sub-block i+1 (invariant M1, which range scan
    // relies on). Placement below keeps M1 without ever shifting across blocks:
    // s goes into the first sub-block whose upper bound is >= s, or into the gap
    // just before it when that sub-block is full, or after every existing key.
    bool put_primary(std::uint32_t g, std::string_view s, std::uint64_t value, std::uint8_t tag) {
        const std::size_t nb = subblocks(g);
        std::size_t prev = SIZE_MAX;   // last non-empty sub-block whose max < s

        for (std::size_t i = 0; i < nb; ++i) {
            const auto& h = header(g, i);
            if (h.count == 0) continue;

            auto* es = entries(g, i);
            if (compare(suffix(g, i, es[h.count - 1]), s) < 0) { prev = i; continue; }

            // s is at or below this sub-block's upper bound: it belongs here, or
            // in the gap immediately before it when it sorts below the first key.
            if (prev != SIZE_MAX && compare(s, suffix(g, i, es[0])) < 0 &&
                insert_into(g, prev, s, value, tag))
                return true;
            return insert_into(g, i, s, value, tag);
        }

        // Greater than every existing key: append to the first sub-block with room
        // at or after `prev`. Starting at `prev` itself is correct (appending there
        // keeps the rank order) and covers the single-sub-block case.
        for (std::size_t j = prev == SIZE_MAX ? 0 : prev; j < nb; ++j)
            if (insert_into(g, j, s, value, tag)) return true;
        return false;
    }

    void put_overflow(std::uint32_t g, std::string_view s, std::uint64_t value, std::uint8_t tag) {
        auto* o = overflow(g);
        if (!o) {
            overflow_owners_.push_back(std::make_unique<Overflow>());
            o = overflow_owners_.back().get();
            header(g, 0).overflow_pointer = reinterpret_cast<std::uintptr_t>(o);
        }
        auto it = std::lower_bound(o->begin(), o->end(), s,
            [](const OverflowRecord& a, std::string_view b) { return compare(a.suffix, b) < 0; });
        o->insert(it, {std::string(s), value, tag});
        ++overflow_count_;
    }

    void compact(std::uint32_t g) {
        ++compactions;
        const std::size_t nb = subblocks(g);
        std::vector<Pair> live;
        for (std::size_t i = 0; i < nb; ++i)
            live.reserve(live.size() + header(g, i).count);

        // Sub-blocks are rank-ordered, so collecting them in order yields the
        // group's keys already sorted; replaying them refills from sub-block 0.
        for (std::size_t i = 0; i < nb; ++i) {
            const auto* es = entries(g, i);
            for (std::size_t j = 0; j < header(g, i).count; ++j)
                live.emplace_back(suffix(g, i, es[j]), es[j].value);
        }

        for (std::size_t i = 0; i < nb; ++i) {
            header(g, i).count = 0;
            header(g, i).bottom = static_cast<std::uint16_t>(bb_[g]);
        }

        for (const auto& kv : live) {
            bool ok = put_primary(g, kv.first, kv.second, Fingerprint::of(kv.first));
            if (!ok) throw std::logic_error("compaction lost capacity");
        }
    }

public:
    Region(const std::vector<Pair>& sorted, Config cfg) : Region(sorted, cfg, {}, {}, {}) {}

    // nb_cap_in is an optional per-group ceiling on the sub-block count, used by
    // the strict-budget allocator to keep an unfunded group on its single-block
    // layout. It is build-time configuration only: nothing is stored, and
    // subblocks(g) still derives from off_ and bb_.
    Region(const std::vector<Pair>& sorted, Config cfg,
           std::vector<std::uint16_t> sg_in, std::vector<std::uint32_t> bb_in,
           std::vector<std::uint16_t> nb_cap_in = {})
        : config_(cfg) {
        if (cfg.block_bytes > 65535 || cfg.block_bytes % 64 || cfg.load <= 0 || cfg.load >= 1)
            throw std::invalid_argument("layout config");

        if (!sorted.empty()) {
            const auto& a = sorted.front().first;
            const auto& b = sorted.back().first;
            std::size_t p = 0;
            while (p < std::min(a.size(), b.size()) && a[p] == b[p]) ++p;
            prefix_ = a.substr(0, p);
        }

        model_.train(sorted, prefix_.size(), config_.model_kind);

        std::uint64_t suffix_bytes = 0;
        for (const auto& kv : sorted) suffix_bytes += kv.first.size() - prefix_.size();

        // All suffix bytes count in this average-capacity estimate, including long ones.
        const double count_groups = sorted.size() / (double(cfg.slots) * cfg.load);
        const double byte_groups = suffix_bytes /
            ((cfg.block_bytes - sizeof(Header) - std::size_t(cfg.slots) * sizeof(Entry)) * cfg.load);
        const auto estimate = static_cast<std::uint64_t>(std::max(count_groups, byte_groups)) + 1;
        if (estimate > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("too many groups");
        groups_ = cfg.fixed_groups
            ? cfg.fixed_groups
            : static_cast<std::uint32_t>(std::max<std::uint64_t>(1, estimate));

        // E8: per-group capacity table (uniform default or explicit demand table)
        if (sg_in.empty()) {
            sg_in.assign(groups_, static_cast<std::uint16_t>(cfg.slots));
            bb_in.assign(groups_, cfg.block_bytes);
        }
        if (sg_in.size() != groups_ || bb_in.size() != groups_)
            throw std::invalid_argument("capacity table size");
        sg_ = std::move(sg_in);
        bb_ = std::move(bb_in);

        // E9 pass 1: each group's real demand (entries and suffix bytes) decides
        // how many equal-sized sub-blocks it needs. Both constraints must hold,
        // and the resulting count stays derivable from off_ and bb_ alone. This
        // pass runs only in multi-block mode; otherwise every group has nb == 1.
        std::vector<std::uint32_t> entries_need;
        std::vector<std::uint64_t> bytes_need;
        if (cfg.multiblock) {
            entries_need.assign(groups_, 0);
            bytes_need.assign(groups_, 0);
            for (const auto& kv : sorted) {
                auto s = std::string_view(kv.first).substr(prefix_.size());
                const auto g = canonical_group(s);
                ++entries_need[g];
                bytes_need[g] += s.size();
            }
        }

        off_.assign(std::size_t(groups_) + 1, 0);
        for (std::uint32_t g = 0; g < groups_; ++g) {
            const auto so = sizeof(Header) + std::size_t(sg_[g]) * sizeof(Entry);
            if (sg_[g] == 0 || sg_[g] > MAX_SLOTS || bb_[g] % 64 || bb_[g] <= so || bb_[g] > 65535)
                throw std::invalid_argument("layout config");

            std::uint64_t nb = 1;
            if (cfg.multiblock) {
                if (!nb_cap_in.empty() && nb_cap_in.size() != groups_)
                    throw std::invalid_argument("sub-block cap size");

                const std::uint64_t per_block = bb_[g] - so;
                nb = (entries_need[g] + sg_[g] - 1) / sg_[g];
                nb = std::max<std::uint64_t>(nb, (bytes_need[g] + per_block - 1) / per_block);
                nb = std::max<std::uint64_t>(nb, 1);

                // 0 means uncapped; any positive value is the ceiling. The strict
                // allocator uses 1 to pin an unfunded group to a single block.
                if (!nb_cap_in.empty() && nb_cap_in[g] != 0)
                    nb = std::min<std::uint64_t>(nb, nb_cap_in[g]);

                if (nb > 65535) throw std::length_error("too many sub-blocks");
            }
            off_[g + 1] = off_[g] + std::size_t(nb) * bb_[g];
        }

        // Legacy fixed-stride addressing cannot express more than one block per
        // group, so a multi-block layout requires the offset directory.
        if (!config_.offset_directory)
            for (std::uint32_t g = 0; g < groups_; ++g)
                if (subblocks(g) != 1)
                    throw std::invalid_argument("fixed stride requires one sub-block per group");

        storage_ = static_cast<std::byte*>(::operator new(off_.back(), std::align_val_t{64}));
        for (std::uint32_t g = 0; g < groups_; ++g) {
            for (std::size_t i = 0, nb = subblocks(g); i < nb; ++i) {
                new (base_of(g, i)) Header{};
                header(g, i).bottom = static_cast<std::uint16_t>(bb_[g]);
                for (std::size_t j = 0; j < sg_[g]; ++j) new (entries(g, i) + j) Entry{};
            }
        }

        for (const auto& kv : sorted) {
            auto s = std::string_view(kv.first).substr(prefix_.size());
            const auto g = canonical_group(s);
            const auto tag = Fingerprint::of(s);
            if (!put_primary(g, s, kv.second, static_cast<std::uint8_t>(tag)))
                put_overflow(g, s, kv.second, static_cast<std::uint8_t>(tag));
            ++size_;
        }
    }

    ~Region() { ::operator delete(storage_, std::align_val_t{64}); }
    Region(const Region&) = delete;
    Region& operator=(const Region&) = delete;

    // --- read-only accessors ---

    std::size_t size() const { return size_; }
    std::size_t overflow_count() const { return overflow_count_; }
    // E9 gate: lets a test assert that cross-sub-block compaction really ran.
    std::size_t compaction_count() const { return compactions; }
    std::size_t prefix_length() const { return prefix_.size(); }
    const std::string& prefix() const { return prefix_; }
    std::uint32_t groups() const { return groups_; }
    std::size_t slots() const { return sg_.empty() ? 0 : sg_[0]; }

    std::size_t block_bytes_total() const { return off_.back(); }

    // E8-6: read-only exposure of the capacity tables for structural validation.
    const std::vector<std::uint16_t>& slot_table() const { return sg_; }
    const std::vector<std::uint32_t>& block_table() const { return bb_; }
    const std::vector<std::size_t>& offset_table() const { return off_; }

    // E9: the sub-block count is derived, not stored, so there is no table to
    // expose — the gate recomputes it from off_/bb_ and compares.
    std::size_t subblock_count(std::uint32_t g) const { return subblocks(g); }

    std::size_t subblock_entries(std::uint32_t g, std::size_t i) const {
        return header(g, i).count;
    }

    // Keys of one sub-block in stored order, as suffixes (all sub-blocks of a
    // group share the region prefix, so suffixes compare like full keys).
    std::vector<std::string> subblock_keys(std::uint32_t g, std::size_t i) const {
        std::vector<std::string> out;
        const auto& h = header(g, i);
        const auto* es = entries(g, i);
        for (std::size_t j = 0; j < h.count; ++j) out.emplace_back(suffix(g, i, es[j]));
        return out;
    }

    struct SubBlockStats {
        std::size_t entries, bytes, free_bytes, garbage_bytes;
    };

    SubBlockStats subblock_stats(std::uint32_t g, std::size_t i) const {
        const auto& h = header(g, i);
        const auto* es = entries(g, i);
        SubBlockStats s{};
        s.entries = h.count;
        for (std::size_t j = 0; j < h.count; ++j) s.bytes += es[j].length;
        s.free_bytes = h.bottom - start_of(g);
        s.garbage_bytes = bb_[g] - h.bottom - s.bytes;
        return s;
    }

    struct MemBreakdown {
        std::size_t blocks, model, prefix, overflow, tables, self;
    };

    MemBreakdown memory_breakdown() const {
        MemBreakdown m{};
        // E9: bb_ describes one sub-block, so the group's bytes come from off_.
        m.blocks = off_.back();
        m.model = model_.allocated_bytes();
        m.prefix = prefix_.capacity() + 1;
        m.overflow = overflow_owners_.capacity() * sizeof(void*);
        for (const auto& o : overflow_owners_) {
            m.overflow += sizeof(Overflow) + o->capacity() * sizeof(OverflowRecord);
            for (const auto& e : *o) m.overflow += e.suffix.capacity() + 1;
        }
        m.tables = off_.capacity() * sizeof(std::size_t) +
                   sg_.capacity() * sizeof(std::uint16_t) +
                   bb_.capacity() * sizeof(std::uint32_t);
        m.self = sizeof(*this);
        return m;
    }

    const Model& model() const { return model_; }
    std::string_view residual(std::string_view key) const { return key.substr(prefix_.size()); }
    bool accepts_prefix(std::string_view key) const { return prefix_matches(key); }

    // E8-12: per-record overflow suffix lengths (E9 feasibility diagnostics).
    std::vector<std::uint32_t> overflow_lengths(std::uint32_t g) const {
        std::vector<std::uint32_t> out;
        if (auto* o = overflow(g))
            for (const auto& e : *o) out.push_back(static_cast<std::uint32_t>(e.suffix.size()));
        return out;
    }

    std::size_t memory() const {
        // E8-6 fix: the offset/slot/block-size tables are now counted (they were
        // missing before; the omission is what the space-decomposition audit found).
        const auto mb = memory_breakdown();
        return mb.self + mb.model + mb.prefix + mb.overflow + mb.blocks + mb.tables;
    }

    std::vector<GroupStats> group_stats() const {
        std::vector<GroupStats> out;
        out.reserve(groups_);

        for (std::uint32_t g = 0; g < groups_; ++g) {
            GroupStats s;
            s.group = g;

            // E9: aggregate the sub-blocks so the per-group totals keep the same
            // meaning they had with a single block (free/garbage are sums).
            for (std::size_t i = 0, nb = subblocks(g); i < nb; ++i) {
                const auto& h = header(g, i);
                const auto* es = entries(g, i);
                s.primary_entries += h.count;
                for (std::size_t j = 0; j < h.count; ++j) s.primary_bytes += es[j].length;
                s.free_entries += slots_of(g) - h.count;
                s.free_bytes += h.bottom - start_of(g);
                s.garbage_bytes += bb_[g] - h.bottom;
            }
            s.garbage_bytes -= s.primary_bytes;

            if (const auto* o = overflow(g)) {
                for (const auto& e : *o) {
                    ++s.overflow_entries;
                    s.overflow_bytes += e.suffix.size();
                    const bool slots = s.free_entries == 0, bytes = e.suffix.size() > s.free_bytes;
                    if (slots && bytes) ++s.blocked_both;
                    else if (slots) ++s.blocked_slots_only;
                    else if (bytes) ++s.blocked_bytes_only;
                    else ++s.fits_now;
                    if (e.suffix.size() > bb_[g] - start_of(g)) ++s.oversized;
                }
            }
            out.push_back(s);
        }
        return out;
    }

    // --- lookup ---

    template <bool Hash>
    Result find_impl(std::string_view key, Mode mode) const {
        if (!size_ || !prefix_matches(key)) return {};
        if ((config_.canonical == Route::Residual) != (mode.route == Route::Residual))
            throw std::logic_error("incompatible mapping");

        const auto s = key.substr(prefix_.size());
        Fingerprint fp;
        const auto pred = model_.predict<Hash>(s, groups_, mode.route, fp);
        const auto g = pred.group;
        const std::byte* address = base_of(g, 0);

        if (mode.prefetch == Prefetch::Early) __builtin_prefetch(address, 0, 3);
        std::uint8_t tag = 0;
        if constexpr (Hash) {
            fp.tail(s.substr(pred.consumed));
            tag = fp.tag();
        }
        if (mode.prefetch == Prefetch::Late) __builtin_prefetch(address, 0, 3);

        Result result;
        result.steps = pred.consumed;

        auto exact = [&](std::size_t blk, std::size_t i) {
            const auto* es = entries(g, blk);
            if (es[i].length == s.size()) {
                ++result.str_cmps;
#ifdef E7_COUNT_BYTES
                if (compare_bytes(suffix(g, blk, es[i]), s, result.cmp_bytes) == 0)
#else
                if (compare(suffix(g, blk, es[i]), s) == 0)
#endif
                {
                    result.found = true;
                    result.value = es[i].value;
                    result.source = 1;
                }
            }
        };

#if defined(__SSE2__)
        constexpr bool has_simd = true;
#else
        constexpr bool has_simd = false;
#endif

        // E9: a group may span several sub-blocks; each is tag-filtered in turn.
        // The tag filter keeps the extra blocks cheap (no full-key compares).
        for (std::size_t blk = 0, nb = subblocks(g); blk < nb && !result.found; ++blk) {
            const auto& h = header(g, blk);

            if constexpr (Hash && has_simd) {
                if (mode.simd) {
                    // E5-2: batched tag filter. Mask bits are clamped to live entries, so
                    // stale tag bytes beyond count can never produce a false candidate.
                    const __m128i vt = _mm_set1_epi8(static_cast<char>(tag));
                    for (std::size_t c = 0; c * 16 < slots_of(g); ++c) {
                        const std::size_t lo = c * 16;
                        const std::size_t top = std::min<std::size_t>(std::min(slots_of(g), lo + 16), h.count);
                        if (top <= lo) break;
                        const __m128i tv = _mm_load_si128(reinterpret_cast<const __m128i*>(h.tags + lo));
                        std::uint32_t mask = static_cast<std::uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(tv, vt)));
                        mask &= (1u << unsigned(top - lo)) - 1u;
                        while (mask && !result.found) {
                            exact(blk, lo + unsigned(__builtin_ctz(mask)));
                            mask &= mask - 1;
                        }
                        if (result.found) break;
                    }
                    continue;
                }
            }
            if constexpr (Hash) {
                for (std::size_t i = 0; i < h.count && !result.found; ++i) {
                    if (h.tags[i] != tag) continue;
                    exact(blk, i);
                }
            } else {
                for (std::size_t i = 0; i < h.count && !result.found; ++i) exact(blk, i);
            }
        }

        if (result.found) return result;

        if (auto* o = overflow(g)) {
            // E7: three lookup policies over the same sorted overflow vector.
            const bool use_bin = mode.ov == OvSearch::Binary ||
                (mode.ov == OvSearch::Hybrid && o->size() >= mode.ov_thr);

            if (use_bin) {
                std::size_t lo = 0, hi = o->size();
                while (lo < hi) {
                    const auto mid = lo + (hi - lo) / 2;
                    ++result.overflow_checks;
                    ++result.str_cmps;
#ifdef E7_COUNT_BYTES
                    if (compare_bytes((*o)[mid].suffix, s, result.cmp_bytes) < 0) lo = mid + 1;
                    else hi = mid;
#else
                    if (compare((*o)[mid].suffix, s) < 0) lo = mid + 1;
                    else hi = mid;
#endif
                }
                if (lo < o->size()) {
                    ++result.str_cmps;
#ifdef E7_COUNT_BYTES
                    if (compare_bytes((*o)[lo].suffix, s, result.cmp_bytes) == 0)
#else
                    if (compare((*o)[lo].suffix, s) == 0)
#endif
                    {
                        result.found = true;
                        result.value = (*o)[lo].value;
                        result.source = 2;
                    }
                }
            } else {
                for (const auto& e : *o) {
                    ++result.overflow_checks;
                    if constexpr (Hash) {
                        if (e.tag != tag) continue;
                    }
                    ++result.str_cmps;
#ifdef E7_COUNT_BYTES
                    if (compare_bytes(e.suffix, s, result.cmp_bytes) == 0)
#else
                    if (compare(e.suffix, s) == 0)
#endif
                    {
                        result.found = true;
                        result.value = e.value;
                        result.source = 2;
                        break;
                    }
                }
            }
        }
        return result;
    }

    Result find(std::string_view key, Mode mode) const {
        return mode.fingerprint ? find_impl<true>(key, mode) : find_impl<false>(key, mode);
    }

    // --- update paths ---

    // E9: whether any sub-block of the group could still take a record this long.
    // Used only to decide whether compaction is worth attempting before overflow.
    bool group_has_room(std::uint32_t g, std::size_t len) const {
        for (std::size_t i = 0, nb = subblocks(g); i < nb; ++i) {
            const auto& h = header(g, i);
            if (h.count < slots_of(g) && len <= h.bottom - start_of(g)) return true;
        }
        return false;
    }

    bool insert(std::string_view key, std::uint64_t value) {
        if (!prefix_matches(key)) throw std::invalid_argument("prefix requires rebuild");

        Mode m{config_.canonical, Prefetch::Off, true, false};
        if (find(key, m).found) return false;

        auto s = key.substr(prefix_.size());
        const auto g = canonical_group(s);
        auto tag = Fingerprint::of(s);

        if (!put_primary(g, s, value, tag)) {
            if (group_has_room(g, s.size())) compact(g);
            if (!put_primary(g, s, value, tag)) put_overflow(g, s, value, tag);
        }
        ++size_;
        return true;
    }

    bool update(std::string_view key, std::uint64_t value) {
        if (!size_ || !prefix_matches(key)) return false;
        auto s = key.substr(prefix_.size());
        const auto g = canonical_group(s);

        for (std::size_t blk = 0, nb = subblocks(g); blk < nb; ++blk) {
            auto* es = entries(g, blk);
            for (std::size_t i = 0; i < header(g, blk).count; ++i)
                if (compare(suffix(g, blk, es[i]), s) == 0) { es[i].value = value; return true; }
        }
        if (auto* o = overflow(g))
            for (auto& e : *o)
                if (compare(e.suffix, s) == 0) { e.value = value; return true; }
        return false;
    }

    bool erase(std::string_view key) {
        if (!size_ || !prefix_matches(key)) return false;
        auto s = key.substr(prefix_.size());
        const auto g = canonical_group(s);

        for (std::size_t blk = 0, nb = subblocks(g); blk < nb; ++blk) {
            auto& h = header(g, blk);
            auto* es = entries(g, blk);
            for (std::size_t i = 0; i < h.count; ++i) {
                if (compare(suffix(g, blk, es[i]), s) == 0) {
                    for (std::size_t j = i + 1; j < h.count; ++j) {
                        es[j - 1] = es[j];
                        h.tags[j - 1] = h.tags[j];
                    }
                    --h.count;
                    --size_;
                    return true;
                }
            }
        }
        if (auto* o = overflow(g))
            for (auto it = o->begin(); it != o->end(); ++it)
                if (compare(it->suffix, s) == 0) {
                    o->erase(it);
                    --size_;
                    --overflow_count_;
                    return true;
                }
        return false;
    }

    // Ordered merge of primary entries and overflow; the group count is monotone
    // in the key order, so scanning groups in ascending order yields sorted output.
    void scan(std::string_view start, std::size_t limit, std::vector<Pair>& out) const {
        if (!size_ || out.size() >= limit) return;

        bool matches = prefix_matches(start);
        if (!matches && compare(start, prefix_) > 0) return;
        const auto s = matches ? start.substr(prefix_.size()) : std::string_view{};
        const auto first = matches ? canonical_group(s) : 0;

        for (std::uint32_t g = first; g < groups_ && out.size() < limit; ++g) {
            const auto* o = overflow(g);
            const std::size_t nb = subblocks(g);
            std::size_t blk = 0, a = 0, b = 0;

            if (g == first && matches) {
                // Advance the primary cursor past every entry < s. Sub-blocks are
                // rank-ordered, so this skips whole sub-blocks at a time.
                while (blk < nb) {
                    while (a < header(g, blk).count &&
                           compare(suffix(g, blk, entries(g, blk)[a]), s) < 0)
                        ++a;
                    if (a < header(g, blk).count) break;
                    ++blk;
                    a = 0;
                }
                if (o) while (b < o->size() && compare((*o)[b].suffix, s) < 0) ++b;
            }

            while (out.size() < limit) {
                const bool have_a = blk < nb && a < header(g, blk).count;
                const bool have_b = o && b < o->size();
                if (!have_a && !have_b) break;

                const bool take_a = have_a &&
                    (!have_b || compare(suffix(g, blk, entries(g, blk)[a]), (*o)[b].suffix) < 0);
                if (take_a) {
                    out.emplace_back(prefix_ + std::string(suffix(g, blk, entries(g, blk)[a])),
                                     entries(g, blk)[a].value);
                    if (++a == header(g, blk).count) { ++blk; a = 0; }
                } else {
                    out.emplace_back(prefix_ + (*o)[b].suffix, (*o)[b].value);
                    ++b;
                }
            }
        }
    }

    std::vector<Pair> all() const {
        std::vector<Pair> out;
        out.reserve(size_);
        scan({}, size_, out);
        return out;
    }
};

// ---------------------------------------------------------------------------
// Index: outer router over ordered region fences.
// ---------------------------------------------------------------------------

class Index {
    Config config_;
    struct Node {
        std::string fence;
        std::unique_ptr<Region> region;
    };
    std::vector<Node> nodes_;
    std::size_t size_ = 0;

    // E6-1: bumped on every fence-changing event so auxiliary routing metadata
    // (built outside the index) can detect staleness and rebuild.
    std::uint64_t structure_version_ = 0;

public:
    std::size_t rebuilds = 0, splits = 0;

    explicit Index(Config c) : config_(c) {}

    const std::string& fence(std::size_t i) const { return nodes_[i].fence; }
    std::uint64_t structure_version() const { return structure_version_; }

    void bulk_load(const std::vector<Pair>& pairs) {
        nodes_.clear();
        size_ = pairs.size();
        const auto chunk = config_.region_keys ? config_.region_keys : std::max<std::size_t>(1, pairs.size());
        for (std::size_t p = 0; p < pairs.size(); p += chunk) {
            std::vector<Pair> part(pairs.begin() + p, pairs.begin() + std::min(p + chunk, pairs.size()));
            nodes_.push_back({part.front().first, std::make_unique<Region>(part, config_)});
        }
        if (nodes_.empty())
            nodes_.push_back({{}, std::make_unique<Region>(std::vector<Pair>{}, config_)});
        ++structure_version_;
    }

    std::size_t locate(std::string_view key) const {
        if (nodes_.empty()) throw std::logic_error("bulk_load must initialize index");
        auto it = std::upper_bound(nodes_.begin(), nodes_.end(), key,
            [](std::string_view k, const Node& n) { return compare(k, n.fence) < 0; });
        return it == nodes_.begin() ? 0 : static_cast<std::size_t>(it - nodes_.begin() - 1);
    }

    const Region& region(std::size_t i) const { return *nodes_[i].region; }
    Result find(std::string_view key, Mode m) const { return region(locate(key)).find(key, m); }
    Result find_known(std::size_t r, std::string_view key, Mode m) const { return region(r).find(key, m); }
    std::size_t size() const { return size_; }
    std::size_t region_count() const { return nodes_.size(); }

    // E8-9: memory decomposition across regions plus index-level metadata.
    struct MemBreakdown {
        std::size_t blocks = 0, model = 0, prefix = 0, overflow = 0, tables = 0,
                    regions_self = 0, fences = 0, self = 0;
    };

    MemBreakdown memory_breakdown() const {
        MemBreakdown m;
        for (const auto& x : nodes_) {
            const auto b = x.region->memory_breakdown();
            m.blocks += b.blocks;
            m.model += b.model;
            m.prefix += b.prefix;
            m.overflow += b.overflow;
            m.tables += b.tables;
            m.regions_self += b.self;
            m.fences += x.fence.capacity() + 1;
        }
        m.self = sizeof(*this) + nodes_.capacity() * sizeof(Node);
        return m;
    }

    std::size_t memory() const {
        std::size_t n = sizeof(*this) + nodes_.capacity() * sizeof(Node);
        for (const auto& x : nodes_) n += x.fence.capacity() + 1 + x.region->memory();
        return n;
    }

    std::size_t overflow_count() const {
        std::size_t n = 0;
        for (const auto& x : nodes_) n += x.region->overflow_count();
        return n;
    }

    std::size_t compaction_count() const {
        std::size_t n = 0;
        for (const auto& x : nodes_) n += x.region->compaction_count();
        return n;
    }

    // E8: replace a region with an explicit per-group capacity table (no split;
    // caller guarantees data fits the region size). Caps only change capacity,
    // never semantics.
    void rebuild_with_caps(std::size_t i, std::vector<Pair> data,
                           std::vector<std::uint16_t> sg, std::vector<std::uint32_t> bb,
                           std::vector<std::uint16_t> nb_cap = {}) {
        nodes_[i].region = std::make_unique<Region>(data, config_, std::move(sg), std::move(bb),
                                                    std::move(nb_cap));
        ++rebuilds;
        ++structure_version_;
    }

    void rebuild(std::size_t i, std::vector<Pair> data) {
        if (config_.region_keys && data.size() > 2 * config_.region_keys) {
            const auto mid = data.size() / 2;
            std::vector<Pair> left(data.begin(), data.begin() + mid), right(data.begin() + mid, data.end());
            auto a = std::make_unique<Region>(left, config_);
            auto b = std::make_unique<Region>(right, config_);
            const auto fence = right.front().first;
            nodes_[i].region = std::move(a);
            nodes_.insert(nodes_.begin() + i + 1, Node{fence, std::move(b)});
            ++splits;
        } else {
            auto fresh = std::make_unique<Region>(data, config_);
            nodes_[i].region = std::move(fresh);
        }
        ++rebuilds;
        ++structure_version_;
    }

    bool insert(std::string_view key, std::uint64_t value) {
        const auto i = locate(key);
        auto& r = *nodes_[i].region;
        if (!r.accepts_prefix(key)) {
            auto data = r.all();
            auto it = std::lower_bound(data.begin(), data.end(), key,
                [](const Pair& a, std::string_view b) { return compare(a.first, b) < 0; });
            if (it != data.end() && compare(it->first, key) == 0) return false;
            data.insert(it, {std::string(key), value});
            rebuild(i, std::move(data));
            ++size_;
            return true;
        }
        if (!r.insert(key, value)) return false;
        ++size_;
        if (config_.region_keys && r.size() > 2 * config_.region_keys) rebuild(i, r.all());
        return true;
    }

    bool update(std::string_view key, std::uint64_t v) { return nodes_[locate(key)].region->update(key, v); }

    bool erase(std::string_view key) {
        bool ok = nodes_[locate(key)].region->erase(key);
        if (ok) --size_;
        return ok;
    }

    bool upsert(std::string_view key, std::uint64_t value) {
        if (update(key, value)) return false;
        return insert(key, value);
    }

    void rebuild_all() {
        for (std::size_t i = 0; i < nodes_.size(); ++i) rebuild(i, nodes_[i].region->all());
    }

    std::vector<Pair> scan(std::string_view start, std::size_t count) const {
        std::vector<Pair> out;
        out.reserve(std::min(count, size_));
        for (std::size_t i = locate(start); i < nodes_.size() && out.size() < count; ++i)
            nodes_[i].region->scan(i == locate(start) ? start : std::string_view{}, count, out);
        return out;
    }
};

}  // namespace validation
