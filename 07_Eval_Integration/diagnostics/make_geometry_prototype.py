"""Generate the immutable GD (hot-group geometry descriptor) probe from one exact source.

The probe is a copy of the pinned index.hpp plus a bounded set of machine-checked
replacements; production source is never touched:

  * Mode gains `hot_geom` (default false), selecting the descriptor path.
  * Region gains a 24-byte per-group descriptor table (`GeomSlot`) plus a one-shot
    `prepare_hot_geom()` built from the SAME off_/bb_/sg_ tables the production path
    reads, and descriptor-based accessors (base/header/entries/suffix/overflow).
  * find_impl gains a second template parameter `Hot`; every geometry read in the
    point-query body becomes `Hot ? hot_x(g) : original_x(g)`, so the baseline
    instantiation keeps the original expressions in place and the descriptor
    instantiation reads only the 24-byte table.
  * Region::find dispatches on `mode.hot_geom`; find_located keeps the baseline.
  * E25_DIAG accessors let the entry device compare descriptor fields and block
    addresses against the production ones; `gd_hot_geom_reads` counts descriptor
    reads so a device can prove the candidate instantiation consumed the table.
  * `GD_SHAPE_PROBE` adds two noinline wrappers (one per instantiation) so the
    disassembly check can be read per arm without depending on inlining decisions.

Every replacement asserts its anchor is unique, so source drift fails loudly.
"""
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "02_Code_and_Experiments/validation_e25_20260918"
PIN_INDEX = "640660a32f1fd1476d0d889d541c007883e5597d70e476524e3bcf2556ec8aa1"
PIN_SWITCHES = "5486909fffc61c1eef940c058eae3fe036e970f0db21c988b3b41800803f0d4e"

DESCRIPTOR_BLOCK = '''
    // --- GD prototype (2026-10-11): contiguous query geometry -----------------
    //
    // One 24-byte descriptor per group, in group order: the four numbers a point
    // query reads from the geometry tables (group start offset, sub-block count,
    // bytes per sub-block, slots per sub-block). Every field is derived from the
    // same tables the production path reads, so the only differences are WHERE
    // the numbers come from and that the sub-block count is stored instead of
    // re-derived by division. The three original tables stay in place, are still
    // written by every existing path, and are NOT deducted from any accounting.
    struct GeomSlot {
        std::uint64_t start = 0;   // off_[g]
        std::uint32_t nb = 0;      // (off_[g+1]-off_[g]) / bb_[g], stored not derived
        std::uint32_t bb = 0;      // bb_[g]
        std::uint16_t slots = 0;   // sg_[g]
        std::uint16_t pad0 = 0;
        std::uint16_t pad1 = 0;
        std::uint16_t pad2 = 0;
    };
    static_assert(sizeof(GeomSlot) == 24, "GD descriptor must stay 24 bytes");
    static_assert(alignof(GeomSlot) == 8, "GD descriptor must stay 8-byte aligned");

    mutable std::vector<GeomSlot> hot_geom_;

    // Caller promises no geometry change after this point; the entry device
    // prepares every region immediately after bulk_load and before any query.
    void prepare_hot_geom() const {
        if (!hot_geom_.empty())
            throw std::logic_error("GD descriptors were already prepared");
        hot_geom_.resize(groups_);
        for (std::uint32_t g = 0; g < groups_; ++g) {
            const std::size_t nb = (off_[g + 1] - off_[g]) / bb_[g];
            if (nb == 0 || nb > 0xffffffffu)
                throw std::logic_error("GD sub-block count out of range");
            hot_geom_[g] = GeomSlot{static_cast<std::uint64_t>(off_[g]),
                                    static_cast<std::uint32_t>(nb), bb_[g], sg_[g], 0, 0, 0};
        }
    }
    std::size_t hot_geom_capacity_bytes() const {
        return hot_geom_.capacity() * sizeof(GeomSlot);
    }
    std::size_t hot_geom_object_bytes() const { return sizeof(hot_geom_); }
    std::size_t hot_geom_extra_bytes() const {
        return hot_geom_object_bytes() + hot_geom_capacity_bytes();
    }
    bool hot_geom_ready() const { return hot_geom_.size() == std::size_t(groups_); }

    std::size_t hot_nb(std::uint32_t g) const { return hot_geom_[g].nb; }
    std::size_t hot_bb(std::uint32_t g) const { return hot_geom_[g].bb; }
    std::size_t hot_slots(std::uint32_t g) const { return hot_geom_[g].slots; }
    std::size_t hot_start_of(std::uint32_t g) const {
        return sizeof(Header) + std::size_t(hot_geom_[g].slots) * sizeof(Entry);
    }
    const std::byte* hot_base(std::uint32_t g, std::size_t i) const {
#ifdef E25_DIAG
        E25_INC(gd_hot_geom_reads);
#endif
        const auto& d = hot_geom_[g];
        return storage_ + std::size_t(d.start) + i * std::size_t(d.bb);
    }
    const Header& hot_header(std::uint32_t g, std::size_t i) const {
        return *reinterpret_cast<const Header*>(hot_base(g, i));
    }
    const Entry* hot_entries(std::uint32_t g, std::size_t i) const {
        return reinterpret_cast<const Entry*>(hot_base(g, i) + sizeof(Header));
    }
    std::string_view hot_suffix(std::uint32_t g, std::size_t i, const Entry& e) const {
        assert(e.offset >= hot_start_of(g) &&
               std::uint64_t(e.offset) + e.length <= hot_bb(g));
        return {reinterpret_cast<const char*>(hot_base(g, i) + e.offset), e.length};
    }
    Overflow* hot_overflow(std::uint32_t g) const {
        return reinterpret_cast<Overflow*>(
            static_cast<std::uintptr_t>(hot_header(g, 0).overflow_pointer));
    }
#ifdef GD_SHAPE_PROBE
    // Code-shape probes only; nothing else in the tree defines GD_SHAPE_PROBE.
    // noinline so the disassembly has one symbol per instantiation even when the
    // surrounding call chain would otherwise be inlined into its caller.
    __attribute__((noinline)) Result shape_probe_baseline(std::string_view key, Mode mode) const {
        return find_impl<true, false>(key, mode);
    }
    __attribute__((noinline)) Result shape_probe_hot(std::string_view key, Mode mode) const {
        return find_impl<true, true>(key, mode);
    }
#endif

'''

DIAG_BLOCK = '''
    // GD probe: the descriptor fields as the candidate path reads them, and the
    // block addresses both paths compute, so the device compares numbers instead of
    // arguing that two expressions look equal. Compiled out of timing builds with
    // the rest of E25_DIAG.
    std::size_t diag_hot_geom_groups() const { return hot_geom_.size(); }
    static constexpr std::size_t diag_max_slots() { return MAX_SLOTS; }
    std::uint64_t diag_hot_geom_start(std::uint32_t g) const { return hot_geom_[g].start; }
    std::uint32_t diag_hot_geom_nb(std::uint32_t g) const { return hot_geom_[g].nb; }
    std::uint32_t diag_hot_geom_bb(std::uint32_t g) const { return hot_geom_[g].bb; }
    std::uint16_t diag_hot_geom_slots(std::uint32_t g) const { return hot_geom_[g].slots; }
    std::uintptr_t diag_geom_block_address(std::uint32_t g, std::size_t i) const {
        return reinterpret_cast<std::uintptr_t>(base_of(g, i));
    }
    std::uintptr_t diag_hot_geom_block_address(std::uint32_t g, std::size_t i) const {
        const auto& d = hot_geom_[g];
        return reinterpret_cast<std::uintptr_t>(
            storage_ + std::size_t(d.start) + i * std::size_t(d.bb));
    }
'''


def _replace(source, old, new, count=1, what=""):
    found = source.count(old)
    if found != count:
        raise RuntimeError(
            "prototype anchor %r: expected %d match(es), found %d: %r"
            % (what, count, found, old[:90]))
    return source.replace(old, new)


def _scope(source):
    """Return (start, end) of the find_impl body in the already-renamed source."""
    marker = "    template <bool Hash, bool Hot>\n    Result find_impl(std::string_view key, Mode mode, Located* loc = nullptr) const {"
    start = source.index(marker)
    end = source.index("\n    Result find(std::string_view key, Mode mode) const {", start)
    return start, end


def generate(destination):
    raw = (UPSTREAM / "index.hpp").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN_INDEX:
        raise RuntimeError("prototype source drift")
    if hashlib.sha256((UPSTREAM / "switches.hpp").read_bytes()).hexdigest() != PIN_SWITCHES:
        raise RuntimeError("switches drift")
    source = raw.decode()

    # 1. Mode gains the descriptor selector. Appended last; the two aggregate
    #    initialisations in the file list fewer fields and keep their meaning.
    source = _replace(source,
        "    bool locate = false;\n};",
        "    bool locate = false;\n    bool hot_geom = false;   // GD probe only; selects the descriptor path.\n};",
        what="Mode.hot_geom")

    # 2. One diagnostic counter so a device can prove the candidate path read the table.
    source = _replace(source,
        "    std::uint64_t scan_canonical_group = 0;  // group-number re-derivations in scan()",
        "    std::uint64_t scan_canonical_group = 0;  // group-number re-derivations in scan()\n"
        "    std::uint64_t gd_hot_geom_reads = 0;     // GD probe (diag builds): descriptor reads",
        what="e25 counter")

    # 3. The descriptor table and its accessors, inside Region.
    # The descriptor API must be public: the entry device prepares it and reads it
    # back. The `private:` after the block restores the access state the class had
    # at this point, so nothing else changes hands.
    source = _replace(source,
        "    // --- per-group geometry ---\n",
        "public:\n" + DESCRIPTOR_BLOCK + "private:\n    // --- per-group geometry ---\n",
        what="Region geometry block")

    # 4. find_impl takes the geometry source as a template parameter.
    source = _replace(source,
        "    template <bool Hash>\n    Result find_impl(std::string_view key, Mode mode, Located* loc = nullptr) const {",
        "    template <bool Hash, bool Hot>\n    Result find_impl(std::string_view key, Mode mode, Located* loc = nullptr) const {",
        what="find_impl signature")

    # 5. Dispatch: Region::find honours mode.hot_geom; the write-path search stays baseline.
    source = _replace(source,
        "    Result find(std::string_view key, Mode mode) const {\n"
        "        return mode.fingerprint ? find_impl<true>(key, mode) : find_impl<false>(key, mode);\n"
        "    }",
        "    Result find(std::string_view key, Mode mode) const {\n"
        "        if (mode.hot_geom) {\n"
        "            return mode.fingerprint ? find_impl<true, true>(key, mode)\n"
        "                                    : find_impl<false, true>(key, mode);\n"
        "        }\n"
        "        return mode.fingerprint ? find_impl<true, false>(key, mode)\n"
        "                                : find_impl<false, false>(key, mode);\n"
        "    }",
        what="Region::find dispatch")
    source = _replace(source,
        "        return m.fingerprint ? find_impl<true>(key, m, &loc) : find_impl<false>(key, m, &loc);",
        "        return m.fingerprint ? find_impl<true, false>(key, m, &loc)\n"
        "                             : find_impl<false, false>(key, m, &loc);",
        what="find_located call")

    # 6. Inside the point-query body, every geometry read becomes arm-dependent.
    start, end = _scope(source)
    scope = source[start:end]
    scope = _replace(scope,
        "        const auto g = pred.group;\n        const std::byte* address = base_of(g, 0);",
        "        const auto g = pred.group;\n"
        "        const std::byte* address = Hot ? hot_base(g, 0) : base_of(g, 0);",
        what="R1 address")
    scope = _replace(scope,
        "        std::size_t lo_blk = 0, hi_blk = subblocks(g);",
        "        std::size_t lo_blk = 0, hi_blk = Hot ? hot_nb(g) : subblocks(g);",
        what="R2 subblock count")
    scope = _replace(scope,
        "            const auto& h = header(g, blk);",
        "            const Header& h = Hot ? hot_header(g, blk) : header(g, blk);",
        what="R3 header")
    scope = _replace(scope,
        "                    const __m128i vt = _mm_set1_epi8(static_cast<char>(tag));\n"
        "                    for (std::size_t c = 0; c * 16 < slots_of(g); ++c) {\n"
        "                        const std::size_t lo = c * 16;\n"
        "                        const std::size_t top = std::min<std::size_t>(std::min(slots_of(g), lo + 16), h.count);",
        "                    const __m128i vt = _mm_set1_epi8(static_cast<char>(tag));\n"
        "                    for (std::size_t c = 0; c * 16 < (Hot ? hot_slots(g) : slots_of(g)); ++c) {\n"
        "                        const std::size_t lo = c * 16;\n"
        "                        const std::size_t top = std::min<std::size_t>(\n"
        "                            std::min<std::size_t>(Hot ? hot_slots(g) : slots_of(g), lo + 16), h.count);",
        what="R4 SIMD live range")
    scope = _replace(scope,
        "        auto exact = [&](std::size_t blk, std::size_t i) {\n"
        "            const auto* es = entries(g, blk);",
        "        auto exact = [&](std::size_t blk, std::size_t i) {\n"
        "            const auto* es = Hot ? hot_entries(g, blk) : entries(g, blk);",
        what="R5a entries")
    scope = _replace(scope,
        "                if (E25_CMP(suffix(g, blk, es[i]), s) == 0) {",
        "                if (E25_CMP(Hot ? hot_suffix(g, blk, es[i]) : suffix(g, blk, es[i]), s) == 0) {",
        what="R5b suffix")
    scope = _replace(scope,
        "        if (auto* o = overflow(g)) {",
        "        if (auto* o = Hot ? hot_overflow(g) : overflow(g)) {",
        what="R6 overflow")

    # Post-check inside the patched body: the candidate arm must not touch the
    # three geometry tables directly any more, and the baseline arm must keep the
    # original expressions (they are the false branches of the eight ternaries).
    for banned in ("off_[", "bb_[", "sg_["):
        if banned in scope:
            raise RuntimeError("patched find_impl still reads " + banned)
    if scope.count("Hot ?") != 8:
        raise RuntimeError("expected 8 arm ternaries in find_impl, found %d" % scope.count("Hot ?"))
    for kept in ("base_of(g, 0)", "subblocks(g)", "header(g, blk)", "slots_of(g)",
                 "entries(g, blk)", "suffix(g, blk, es[i])", "overflow(g)"):
        if kept not in scope:
            raise RuntimeError("baseline expression disappeared: " + kept)
    source = source[:start] + scope + source[end:]

    # 7. Diagnostic accessors (inside the existing E25_DIAG block).
    source = _replace(source,
        "    std::size_t diag_subblocks(std::uint32_t g) const { return subblocks(g); }\n",
        "    std::size_t diag_subblocks(std::uint32_t g) const { return subblocks(g); }\n"
        + DIAG_BLOCK,
        what="diag accessors")

    destination.mkdir(exist_ok=False)
    path = destination / "index.hpp"
    path.write_text(source)
    return path


if __name__ == "__main__":
    import sys as _sys
    print(generate(Path(_sys.argv[1])))
