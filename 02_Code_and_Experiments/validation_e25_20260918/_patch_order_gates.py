# -*- coding: utf-8 -*-
"""Add the audit's byte-order counterexample to the two existing directory gates.

Each gate gets a small self-contained check: the four-key fixture swept over all 256 single-byte
queries, against a hard-coded independent integer count of its three thresholds. It fails on a
signed char comparison and passes on the explicit unsigned one (see check_depth_dir_order.cpp for
the full battery, which also covers the prefix sweep, the full index and the negative control).
"""
import io, os
HERE = os.path.dirname(os.path.abspath(__file__))

FUNC = '''
// ---------------------------------------------------------------- byte order (audit control)
//
// AUDIT_DEPTH_DIR_ORDER_20261011.md: the search used to compare `sep[i] < q[i]` on plain char,
// which is signed on x86, so every byte >= 0x80 sorted below every byte < 0x80. Placement and
// lookup shared that map, so a find-only check could not see it. This is the audit's four-key
// fixture: ranks 1,2,3 of 0x01 / 0x7f / 0x80 / 0xff are the one-byte thresholds 0x7f / 0x80 /
// 0xff, and the expected group of a query byte is the independent INTEGER count of those
// thresholds below it.
void check_high_byte_order() {
    std::vector<std::string> owned;
    for (unsigned b : {1u, 127u, 128u, 255u}) owned.emplace_back(1, static_cast<char>(b));
    std::vector<std::string_view> keys(owned.begin(), owned.end());
    const auto model = equivdir::RegionModel::fit(keys.data(), keys.size(), 0, 4, %s);

    std::uint64_t mismatches = 0, inversions = 0;
    std::uint32_t previous = 0;
    for (unsigned b = 0; b < 256; ++b) {
        const std::string q(1, static_cast<char>(b));
        const std::uint32_t expected =
            static_cast<std::uint32_t>((127u <= b) + (128u <= b) + (255u <= b));
        const std::uint32_t actual = model.predict_group(q);
        if (actual != expected) ++mismatches;
        if (b && actual < previous) ++inversions;
        previous = actual;
    }
    require(mismatches == 0,
            "byte order: every single-byte query is placed by the unsigned count (" +
                std::to_string(mismatches) + " mismatches)");
    require(inversions == 0, "byte order: no group inversion over 0x00..0xff");
}
'''
CALL = '''
        check_high_byte_order();
'''

def patch(name, trunc_literal, anchor):
    p = os.path.join(HERE, name)
    t = io.open(p, encoding="utf-8", newline="").read()
    if "check_high_byte_order" in t:
        print("already present:", name)
        return
    func = FUNC % trunc_literal
    # insert the function just before the closing of the anonymous namespace
    marker = "}  // namespace\n"
    if t.count(marker) != 1:
        raise SystemExit("FATAL %s: namespace marker count %d" % (name, t.count(marker)))
    t = t.replace(marker, func + "\n" + marker)
    if t.count(anchor) != 1:
        raise SystemExit("FATAL %s: call anchor count %d" % (name, t.count(anchor)))
    t = t.replace(anchor, anchor + CALL)
    io.open(p, "w", encoding="utf-8", newline="\n").write(t)
    print("patched", p)

patch("check_depth_dir.cpp", "false",
      '        check_synthetic_depth();\n')
patch("check_depth_dir_trunc.cpp", "true",
      '        check_synthetic_depth();\n')
