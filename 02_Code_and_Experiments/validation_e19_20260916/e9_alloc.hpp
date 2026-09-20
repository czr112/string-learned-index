// E9's capacity allocators, extracted verbatim from validation_e9_20260915 /
// bench_e9.cpp so E10 can build the same D, M and S layouts without duplicating
// the logic. E10 does not change any of this: it only measures what querying the
// three layouts costs.
//
//   U  uniform single-block, 16 slots, 1024 B            (not built here)
//   D  demand single-block, slots <= 48
//   M  demand multi-block, 48 slots per sub-block, uncapped budget
//   S  demand multi-block, slots chosen per group, block bytes capped at D's

#pragma once

#include "index.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace validation {
namespace e9alloc {

inline constexpr std::size_t SLOTS_PER_SUBBLOCK = 48;

inline std::uint64_t align64(std::uint64_t x) { return (x + 63) & ~std::uint64_t{63}; }

inline std::uint64_t start_bytes(std::size_t slots) {
    return sizeof(Header) + std::uint64_t(slots) * sizeof(Entry);
}

inline std::uint64_t min_block(std::size_t slots) {
    return align64(start_bytes(slots) + 1);
}

struct Alloc {
    std::vector<std::uint16_t> sg;
    std::vector<std::uint32_t> bb;
};

struct ClassDelta {
    std::uint64_t empty = 0, single = 0, multi = 0;
    std::uint64_t n_empty = 0, n_single = 0, n_multi = 0;
};

struct StrictLog {
    std::size_t candidates = 0, funded = 0, unfunded = 0;
    std::uint64_t slack_available = 0, slack_spent = 0, shaved = 0;
};

// ---------------------------------------------------------------------------
// D
// ---------------------------------------------------------------------------

inline Alloc demand_single(const std::vector<GroupStats>& gs, std::uint64_t budget) {
    const std::size_t G = gs.size();
    Alloc a;
    a.sg.resize(G);
    a.bb.resize(G);

    std::vector<std::uint64_t> sfx(G);
    std::uint64_t fixed = 0, sfx_demand = 0;

    for (std::size_t g = 0; g < G; ++g) {
        const std::uint64_t ent = gs[g].primary_entries + gs[g].overflow_entries;
        a.sg[g] = static_cast<std::uint16_t>(
            std::min<std::uint64_t>(std::max<std::uint64_t>(ent, 1), SLOTS_PER_SUBBLOCK));
        sfx[g] = std::min<std::uint64_t>(gs[g].primary_bytes + gs[g].overflow_bytes, 60000);
        fixed += start_bytes(a.sg[g]);
        sfx_demand += sfx[g];
    }

    const std::uint64_t avail = budget > fixed ? budget - fixed : 0;
    const double scale = sfx_demand ? double(avail) / double(sfx_demand) : 0.0;

    std::uint64_t used = 0;
    for (std::size_t g = 0; g < G; ++g) {
        const std::uint64_t so = start_bytes(a.sg[g]);
        const std::uint64_t sf = std::min<std::uint64_t>(sfx[g], std::uint64_t(double(sfx[g]) * scale));
        a.bb[g] = static_cast<std::uint32_t>(std::max(align64(so + sf), min_block(a.sg[g])));
        used += a.bb[g];
    }

    while (used > budget) {
        std::size_t best = SIZE_MAX;
        std::uint64_t best_slack = 0;
        for (std::size_t g = 0; g < G; ++g) {
            const std::uint64_t slack = a.bb[g] - min_block(a.sg[g]);
            if (slack > best_slack) { best_slack = slack; best = g; }
        }
        if (best == SIZE_MAX || best_slack == 0) break;
        const std::uint64_t step = align64(std::min<std::uint64_t>(64, best_slack));
        a.bb[best] -= static_cast<std::uint32_t>(step);
        used -= step;
    }

    std::size_t guard = 0;
    while (used < budget && guard++ < G * 1024)
        for (std::size_t g = 0; g < G && used < budget; ++g) { a.bb[g] += 64; used += 64; }

    return a;
}

// ---------------------------------------------------------------------------
// M
// ---------------------------------------------------------------------------

inline Alloc demand_multi(const std::vector<GroupStats>& gs, const Alloc& base, ClassDelta& delta) {
    const std::size_t G = gs.size();
    Alloc a;
    a.sg.resize(G);
    a.bb.resize(G);

    for (std::size_t g = 0; g < G; ++g) {
        const std::uint64_t n = gs[g].primary_entries + gs[g].overflow_entries;
        const std::uint64_t suffix = gs[g].primary_bytes + gs[g].overflow_bytes;

        if (n <= base.sg[g]) {
            a.sg[g] = base.sg[g];
            a.bb[g] = base.bb[g];
            (n == 0 ? delta.n_empty : delta.n_single) += 1;
            continue;
        }

        const std::uint64_t nb = (n + SLOTS_PER_SUBBLOCK - 1) / SLOTS_PER_SUBBLOCK;
        const std::uint64_t per = (suffix + nb - 1) / nb;
        a.sg[g] = static_cast<std::uint16_t>(SLOTS_PER_SUBBLOCK);
        a.bb[g] = static_cast<std::uint32_t>(
            std::max(min_block(SLOTS_PER_SUBBLOCK), align64(start_bytes(SLOTS_PER_SUBBLOCK) + per)));

        const std::uint64_t cost = nb * a.bb[g];
        delta.multi += cost > base.bb[g] ? cost - base.bb[g] : 0;
        delta.n_multi += 1;
    }

    return a;
}

// ---------------------------------------------------------------------------
// S
// ---------------------------------------------------------------------------

// Smallest legal layout that holds n entries and `suffix` bytes, searched over
// every slot count.
struct Plan {
    std::uint16_t sg = 1;
    std::uint32_t bb = 0;
    std::uint64_t total = 0;
    std::uint64_t nb = 0;
};

inline Plan min_plan(std::uint64_t n, std::uint64_t suffix) {
    Plan best;
    best.total = ~std::uint64_t{0};

    for (std::uint64_t sg = 1; sg <= SLOTS_PER_SUBBLOCK; ++sg) {
        const std::uint64_t start = start_bytes(sg);
        const std::uint64_t nb = n ? (n + sg - 1) / sg : 1;
        const std::uint64_t per = (suffix + nb - 1) / nb;
        const std::uint64_t bb = std::max(min_block(sg), align64(start + per));
        const std::uint64_t total = nb * bb;

        if (total < best.total) best = Plan{static_cast<std::uint16_t>(sg),
                                            static_cast<std::uint32_t>(bb), total, nb};
    }

    return best;
}

inline Alloc demand_strict(const std::vector<GroupStats>& gs, const Alloc& base, StrictLog& log,
                           std::vector<std::uint16_t>& caps) {
    const std::size_t G = gs.size();
    caps.assign(G, 1);   // every group is single-block unless it gets funded below

    std::vector<std::uint64_t> need(G);
    std::uint64_t available = 0;
    for (std::size_t g = 0; g < G; ++g) {
        const std::uint64_t start = start_bytes(base.sg[g]);
        need[g] = std::max(min_block(base.sg[g]), align64(start + gs[g].primary_bytes));
        if (base.bb[g] > need[g]) available += base.bb[g] - need[g];
    }

    struct Cand {
        std::size_t g;
        std::uint64_t sg;
        std::uint64_t bb;
        std::uint64_t cost;
        std::uint64_t benefit;
    };

    std::vector<Cand> cands;
    for (std::size_t g = 0; g < G; ++g) {
        const std::uint64_t n = gs[g].primary_entries + gs[g].overflow_entries;
        if (n <= base.sg[g]) continue;

        const Plan p = min_plan(n, gs[g].primary_bytes + gs[g].overflow_bytes);
        const std::uint64_t cost = p.total > base.bb[g] ? p.total - base.bb[g] : 0;
        cands.push_back({g, p.sg, p.bb, cost, gs[g].overflow_entries});
    }
    log.candidates += cands.size();

    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        return a.cost * b.benefit < b.cost * a.benefit;   // benefit per byte, descending
    });

    Alloc a = base;
    std::vector<std::size_t> funded;
    std::uint64_t spent = 0;
    for (const auto& c : cands) {
        if (spent + c.cost > available) { ++log.unfunded; continue; }
        spent += c.cost;
        funded.push_back(c.g);
        a.sg[c.g] = static_cast<std::uint16_t>(c.sg);
        a.bb[c.g] = static_cast<std::uint32_t>(c.bb);
        caps[c.g] = 0;   // uncapped: the plan's own sub-block count applies
        ++log.funded;
    }

    std::vector<std::size_t> donors(G);
    for (std::size_t g = 0; g < G; ++g) donors[g] = g;
    std::sort(donors.begin(), donors.end(), [&](std::size_t x, std::size_t y) {
        return base.bb[x] - need[x] > base.bb[y] - need[y];
    });

    std::uint64_t remaining = spent;
    for (std::size_t g : donors) {
        if (!remaining) break;
        if (std::find(funded.begin(), funded.end(), g) != funded.end()) continue;

        const std::uint64_t slack = base.bb[g] > need[g] ? base.bb[g] - need[g] : 0;
        const std::uint64_t take = std::min(slack, remaining);
        if (!take) continue;

        a.bb[g] = static_cast<std::uint32_t>(base.bb[g] - take);
        log.shaved += take;
        remaining -= take;
    }

    log.slack_available += available;
    log.slack_spent += spent;

    return a;
}

}  // namespace e9alloc
}  // namespace validation
