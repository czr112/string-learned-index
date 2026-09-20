// E9 capacity accounting (2026-09-15).
//
// E8 left overflow capped by the 48-slot-per-block limit. E9 lets a logical
// group own several equal-sized sub-blocks, so this benchmark measures what that
// recovers and what it costs. No timing: the question is deterministic, and the
// host clock is not stable enough to add anything (see E8 report section 5.3).
//
// Layouts, same keys, same run:
//   U  uniform single-block, 16 slots, 1024 B            (= E8 V2)
//   D  demand single-block, slots <= 48                  (= E8 V3)
//   M  demand multi-block, 48 slots per sub-block, for groups that need it
//
// M leaves every group that already fits in one block on its D layout. Giving
// those groups a 48-slot sub-block would inflate them, and that inflation is a
// property of the allocator rather than a cost of multi-block -- it has to be
// accounted separately, not folded into the multi-block delta.
//
// Note on budgets: D and U share a strictly equal block budget. M does not; it is
// free to spend more, and the extra is reported. This cannot show that a
// strict-budget layout is impossible -- only what this allocator costs.

#include "data.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace validation;

static constexpr std::size_t SLOTS_PER_SUBBLOCK = 48;

static std::uint64_t align64(std::uint64_t x) { return (x + 63) & ~std::uint64_t{63}; }

static std::uint64_t start_bytes(std::size_t slots) {
    return sizeof(Header) + std::uint64_t(slots) * sizeof(Entry);
}

static std::uint64_t min_block(std::size_t slots) {
    return align64(start_bytes(slots) + 1);
}

struct Alloc {
    std::vector<std::uint16_t> sg;
    std::vector<std::uint32_t> bb;
};

struct Space {
    std::uint64_t blocks = 0, overflow = 0, tables = 0, other = 0, total = 0;

    void take(const Index& idx) {
        const auto b = idx.memory_breakdown();
        blocks = b.blocks;
        overflow = b.overflow;
        tables = b.tables;
        other = b.model + b.prefix + b.regions_self + b.fences + b.self;
        total = blocks + overflow + tables + other;
    }
};

// ---------------------------------------------------------------------------
// D: E8's strict-budget demand allocator, reproduced so U and D here reproduce
// the E8 v3 numbers.
// ---------------------------------------------------------------------------

static Alloc demand_single(const std::vector<GroupStats>& gs, std::uint64_t budget) {
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
// M: only groups whose entries do not all fit in one block get sub-blocks. The
// rest keep D's allocation byte for byte, so the measured delta is the cost of
// multi-block and not the cost of re-inflating groups that D had compressed.
// ---------------------------------------------------------------------------

struct ClassDelta {
    std::uint64_t empty = 0, single = 0, multi = 0;
    std::uint64_t n_empty = 0, n_single = 0, n_multi = 0;
};

// Smallest legal layout that holds n entries and `suffix` bytes, searched over
// every slot count. Choosing sg smaller can win: fewer slots per sub-block means
// a smaller entries area per sub-block, which can beat the extra sub-blocks it
// costs. Every candidate keeps the derived sub-block count equal to `nb`.
struct Plan {
    std::uint16_t sg = 1;
    std::uint32_t bb = 0;
    std::uint64_t total = 0;
    std::uint64_t nb = 0;
};

static Plan min_plan(std::uint64_t n, std::uint64_t suffix) {
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

struct StrictLog {
    std::size_t candidates = 0, funded = 0, unfunded = 0;
    std::uint64_t slack_available = 0, slack_spent = 0, shaved = 0;
};

// Strictly budget-respecting multi-block: the region block total never exceeds
// D's. Say what D spends beyond what each group's in-block contents need -- that
// spare suffix room is the only slack that exists, since D already uses the whole
// budget. Spend it on sub-blocks for the groups with the most overflow per byte,
// and shave the donors to free exactly what was spent.
static Alloc demand_strict(const std::vector<GroupStats>& gs, const Alloc& base, StrictLog& log,
                           std::vector<std::uint16_t>& caps) {
    const std::size_t G = gs.size();
    caps.assign(G, 1);   // every group is single-block unless it gets funded below

    // Slack is per region: D fills each region's budget completely, so the only
    // room to fund sub-blocks is what a group spends beyond its own contents.
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

    // Free exactly what this region spent, taking it from its largest-slack groups.
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

static Alloc demand_multi(const std::vector<GroupStats>& gs, const Alloc& base, ClassDelta& delta) {
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

int main(int argc, char** argv) {
    try {
        if (argc < 2)
            throw std::invalid_argument("bench_e9 KIND N [TRANCOCSV]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const auto keys = make_data(kind, nkeys, trcsv);

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;

        Index idx(c);
        idx.bulk_load(keys);

        std::vector<std::vector<GroupStats>> demand;
        std::vector<std::uint64_t> budget;
        for (std::size_t i = 0; i < idx.region_count(); ++i) {
            demand.push_back(idx.region(i).group_stats());
            budget.push_back(std::uint64_t(demand.back().size()) * 1024);
        }

        Space sp_u;
        sp_u.take(idx);
        const std::size_t ov_u = idx.overflow_count();

        // --- D ------------------------------------------------------------------
        std::vector<Alloc> d_alloc;
        for (std::size_t i = 0; i < idx.region_count(); ++i)
            d_alloc.push_back(demand_single(demand[i], budget[i]));

        for (std::size_t i = 0; i < idx.region_count(); ++i)
            idx.rebuild_with_caps(i, idx.region(i).all(), d_alloc[i].sg, d_alloc[i].bb);

        Space sp_d;
        sp_d.take(idx);
        const std::size_t ov_d = idx.overflow_count();

        // E8's idealised estimate, recomputed on THIS run's D layout.
        std::uint64_t capped = 0, capped_full = 0, ov_in_capped = 0, ideal = 0, capped_free = 0;
        for (std::size_t i = 0; i < idx.region_count(); ++i) {
            const auto& reg = idx.region(i);
            const auto& sg = reg.slot_table();
            const auto gs = reg.group_stats();
            for (std::uint32_t g = 0; g < gs.size(); ++g) {
                if (sg[g] != SLOTS_PER_SUBBLOCK) continue;
                ++capped;
                if (gs[g].primary_entries == SLOTS_PER_SUBBLOCK) ++capped_full;
                ov_in_capped += gs[g].overflow_entries;
                if (gs[g].overflow_entries == 0) continue;

                capped_free += gs[g].free_bytes;
                auto lens = reg.overflow_lengths(g);
                std::sort(lens.begin(), lens.end());
                std::uint64_t running = gs[g].free_bytes, k = 0;
                for (std::uint32_t len : lens) {
                    const std::uint64_t cost = std::uint64_t(len) + 17;
                    if (running >= cost) { running -= cost; ++k; }
                    else break;
                }
                ideal += k;
            }
        }

        std::uint64_t d_headers = 0, d_groups = 0;
        for (std::size_t i = 0; i < idx.region_count(); ++i) {
            d_groups += idx.region(i).groups();
            d_headers += std::uint64_t(idx.region(i).groups()) * sizeof(Header);
        }

        // --- M ------------------------------------------------------------------
        Config cm = c;
        cm.multiblock = true;

        std::vector<Alloc> m_alloc;
        ClassDelta delta;
        for (std::size_t i = 0; i < idx.region_count(); ++i)
            m_alloc.push_back(demand_multi(demand[i], d_alloc[i], delta));

        Index midx(cm);
        midx.bulk_load(keys);
        for (std::size_t i = 0; i < midx.region_count(); ++i)
            midx.rebuild_with_caps(i, midx.region(i).all(), m_alloc[i].sg, m_alloc[i].bb);

        Space sp_m;
        sp_m.take(midx);
        const std::size_t ov_m = midx.overflow_count();

        std::uint64_t m_headers = 0, mb_groups = 0, max_nb = 0;
        for (std::size_t i = 0; i < midx.region_count(); ++i) {
            const auto& reg = midx.region(i);
            for (std::uint32_t g = 0; g < reg.groups(); ++g) {
                const std::uint64_t nb = reg.subblock_count(g);
                if (nb > 1) ++mb_groups;
                max_nb = std::max(max_nb, nb);
                m_headers += nb * sizeof(Header);
            }
        }

        // --- S: strictly budget-respecting multi-block --------------------------
        StrictLog slog;
        std::vector<Alloc> s_alloc;
        std::vector<std::vector<std::uint16_t>> s_caps;
        for (std::size_t i = 0; i < idx.region_count(); ++i) {
            std::vector<std::uint16_t> caps;
            s_alloc.push_back(demand_strict(demand[i], d_alloc[i], slog, caps));
            s_caps.push_back(std::move(caps));
        }

        Index sidx(cm);
        sidx.bulk_load(keys);
        for (std::size_t i = 0; i < sidx.region_count(); ++i)
            sidx.rebuild_with_caps(i, sidx.region(i).all(), s_alloc[i].sg, s_alloc[i].bb, s_caps[i]);

        Space sp_s;
        sp_s.take(sidx);
        const std::size_t ov_s = sidx.overflow_count();

        std::uint64_t s_mb_groups = 0, s_max_nb = 0;
        std::uint64_t s_slots_48 = 0, s_slots_below_48 = 0, s_min_slots = 255;
        for (std::size_t i = 0; i < sidx.region_count(); ++i) {
            const auto& reg = sidx.region(i);
            for (std::uint32_t g = 0; g < reg.groups(); ++g) {
                const std::uint64_t nb = reg.subblock_count(g);
                if (nb > 1) ++s_mb_groups;
                s_max_nb = std::max(s_max_nb, nb);
            }
            // M fixes 48 slots per sub-block; S takes whatever min_plan chose,
            // so the two layouts differ in slot count and this shows by how much.
            for (auto sg : reg.slot_table()) {
                if (sg < 48) { ++s_slots_below_48; s_min_slots = std::min<std::uint64_t>(s_min_slots, sg); }
                else ++s_slots_48;
            }
        }
        if (!s_slots_below_48) s_min_slots = 48;

        const bool s_within = sp_s.blocks <= sp_d.blocks;
        if (!s_within)
            throw std::runtime_error("strict variant exceeded the D block budget");

        // Why is a remaining overflow record still overflow? "Room somewhere in
        // the group" is not enough on its own: M1 ordering means a record only
        // goes into the sub-block its key range belongs to, so it can overflow
        // while free slots and free bytes exist elsewhere in the same group.
        struct OvClass {
            std::uint64_t oversized = 0, no_slot = 0, no_bytes = 0;
            std::uint64_t ordering = 0, split = 0;
        };

        auto classify = [](const Index& ix) {
            OvClass c;
            for (std::size_t i = 0; i < ix.region_count(); ++i) {
                const auto& reg = ix.region(i);
                const auto& sg = reg.slot_table();
                const auto& bb = reg.block_table();

                for (std::uint32_t g = 0; g < reg.groups(); ++g) {
                    const auto lens = reg.overflow_lengths(g);
                    if (lens.empty()) continue;

                    const std::uint64_t per_slot = sg[g];
                    const std::uint64_t start = start_bytes(per_slot);
                    const std::uint64_t nb = reg.subblock_count(g);

                    for (std::uint32_t len : lens) {
                        if (len > bb[g] - start) { ++c.oversized; continue; }

                        bool room_here = false, any_slot = false, any_bytes = false;
                        for (std::uint64_t k = 0; k < nb; ++k) {
                            const auto st = reg.subblock_stats(g, k);
                            const std::uint64_t free_slots = per_slot - st.entries;
                            if (free_slots > 0) any_slot = true;
                            if (st.free_bytes >= len) any_bytes = true;
                            if (free_slots > 0 && st.free_bytes >= len) room_here = true;
                        }

                        if (room_here) ++c.ordering;          // room, but not in its sub-block
                        else if (!any_slot) ++c.no_slot;
                        else if (!any_bytes) ++c.no_bytes;
                        else ++c.split;                       // slots and bytes in different sub-blocks
                    }
                }
            }
            return c;
        };

        const OvClass cls_m = classify(midx);
        const OvClass cls_s = classify(sidx);

        {
            std::ofstream out("results/main/e9_summary.jsonl", std::ios::app);
            out.exceptions(std::ios::badbit | std::ios::failbit);
            out << std::setprecision(12);

            const auto space = [&](const char* name, const Space& s, std::size_t ov) {
                out << "\"" << name << "\":{\"overflow_keys\":" << ov
                    << ",\"blocks\":" << s.blocks
                    << ",\"overflow_space\":" << s.overflow
                    << ",\"tables\":" << s.tables
                    << ",\"other\":" << s.other
                    << ",\"total\":" << s.total << "}";
            };

            const auto ov_class = [&](const char* name, const OvClass& c, std::size_t total) {
                out << "\"" << name << "\":{\"suffix_exceeds_subblock\":" << c.oversized
                    << ",\"no_free_slot_in_group\":" << c.no_slot
                    << ",\"no_subblock_with_enough_free_bytes\":" << c.no_bytes
                    << ",\"room_exists_but_not_in_its_subblock\":" << c.ordering
                    << ",\"slots_and_bytes_in_different_subblocks\":" << c.split
                    << ",\"total\":" << total << "}";
            };

            out << "{\"kind\":\"" << kind << "\",\"keys\":" << keys.size() << ",";
            space("U_uniform", sp_u, ov_u);
            out << ",";
            space("D_demand_single", sp_d, ov_d);
            out << ",";
            space("M_multiblock", sp_m, ov_m);
            out << ",\"M_layout\":{\"multiblock_groups\":" << mb_groups
                << ",\"max_subblocks\":" << max_nb
                << ",\"M_block_headers\":" << m_headers
                << ",\"D_block_headers\":" << d_headers
                << ",\"header_delta\":" << (m_headers > d_headers ? m_headers - d_headers : 0)
                << "}"
                << ",";
            space("S_multiblock_strict", sp_s, ov_s);
            out << ",\"S_strict_log\":{\"candidates\":" << slog.candidates
                << ",\"funded\":" << slog.funded
                << ",\"unfunded\":" << slog.unfunded
                << ",\"slack_available\":" << slog.slack_available
                << ",\"slack_spent\":" << slog.slack_spent
                << ",\"shaved\":" << slog.shaved
                << ",\"multiblock_groups\":" << s_mb_groups
                << ",\"max_subblocks\":" << s_max_nb
                << ",\"groups_with_48_slots\":" << s_slots_48
                << ",\"groups_with_fewer_than_48_slots\":" << s_slots_below_48
                << ",\"min_slots\":" << s_min_slots
                << ",\"within_D_block_budget\":" << (s_within ? "true" : "false")
                << "}"
                << ",\"block_delta_by_class\":{\"empty_groups\":" << delta.n_empty
                << ",\"single_block_groups\":" << delta.n_single
                << ",\"multiblock_groups\":" << delta.n_multi
                << ",\"bytes_from_multiblock\":" << delta.multi << "}"
                << ",\"ideal_17B_model\":{\"capped_groups\":" << capped
                << ",\"capped_and_full\":" << capped_full
                << ",\"overflow_in_capped\":" << ov_in_capped
                << ",\"capped_free_bytes\":" << capped_free
                << ",\"absorbable_with_no_extra_bytes\":" << ideal << "}"
                << ",";
            ov_class("remaining_overflow_classification", cls_m, ov_m);
            out << ",";
            ov_class("S_remaining_overflow_classification", cls_s, ov_s);
            out << "}" << std::endl;

            std::cerr << "U ov=" << ov_u << " (" << sp_u.blocks << " B)"
                      << "  D ov=" << ov_d << " (" << sp_d.blocks << " B)"
                      << "  M ov=" << ov_m << " (" << sp_m.blocks << " B)"
                      << "  S ov=" << ov_s << " (" << sp_s.blocks << " B)"
                      << "  ideal=" << ideal << "\n";
        }

        std::cerr << "COMPLETE " << kind << "\n";
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << "\n";
        return 1;
    }
}
