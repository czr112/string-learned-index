#pragma once
// E25/E26/E27 switch discipline.
//
// The Config defaults used to BE the experimental baseline: every gate built a
// Config from the defaults and then flipped only the switches it was studying. That
// works until a default changes -- and at freeze time one did. A gate written that
// way would then silently compare a different baseline than the round it belongs to,
// which is exactly the kind of quiet redefinition the project's review discipline
// forbids.
//
// So every gate now builds its baseline from `baseline_config()`, which sets ALL of
// the switches this project added, explicitly. Flipping a Config default can no
// longer move a gate underneath itself.
//
// It also prints the EFFECTIVE switch values into the gate's output, and asserts the
// arms differ in exactly the switches the gate claims to be studying -- so a gate
// that accidentally compares two identical configurations fails instead of passing.
//
// Include AFTER index.hpp.

#include <initializer_list>
#include <stdexcept>
#include <string>

namespace e25sw {

// The pre-E25 configuration: everything this project added is OFF. Field-by-field
// explicit; nothing here reads a Config default.
inline validation::Config baseline_config(std::uint32_t ov_threshold) {
    validation::Config c;
    c.block_bytes = 1024;
    c.slots = 16;
    c.model_kind = validation::ModelKind::Context16;
    c.multiblock = true;
    c.reuse_group_ids = true;      // the frozen R1 default, unchanged since E20
    c.ov_threshold = ov_threshold;
    // ---- every switch added since E24, all explicit ----
    c.write_fast_locate = false;      // E25 V1
    c.reuse_locate_ctx = false;       // E25 V2
    c.compact_reuse_tag = false;      // E26 C1
    c.compact_needs_garbage = false;  // E26 C2
    c.scan_reserve_result = false;    // E27 S1
    c.subblock_reclaim = false;       // E31 (default off; the baseline is pre-E31)
    return c;
}

// The FROZEN combination (2026-09-20): every switch on. Gates that were written
// before the freeze and simply built a Config from defaults now call this, so their
// experimental condition is stated in their own source instead of being inherited
// from a header that may change again.
//
// These gates test the SHIPPED configuration. That is a deliberate choice: an
// invariant that only holds on the old default is not an invariant worth keeping.
inline void pin_frozen_switches(validation::Config& c) {
    c.write_fast_locate = true;
    c.reuse_locate_ctx = true;
    c.compact_reuse_tag = true;
    c.compact_needs_garbage = true;
    c.scan_reserve_result = true;
    c.subblock_reclaim = false;   // NOT part of the frozen set; E31 is a candidate
}

// The effective switch values, for the gate's own output. A reader should never have
// to infer which configuration produced a line.
inline std::string switches(const validation::Config& c) {
    return std::string("write_fast_locate=") + (c.write_fast_locate ? "1" : "0") +
           ",reuse_locate_ctx=" + (c.reuse_locate_ctx ? "1" : "0") +
           ",compact_reuse_tag=" + (c.compact_reuse_tag ? "1" : "0") +
           ",compact_needs_garbage=" + (c.compact_needs_garbage ? "1" : "0") +
           ",compact_repack_budget=" + (c.compact_repack_budget ? "1" : "0") +
           ",subblock_reclaim=" + (c.subblock_reclaim ? "1" : "0") +
           ",scan_reserve_result=" + (c.scan_reserve_result ? "1" : "0");
}

// Fail unless the two configurations differ in EXACTLY the named switches, and agree
// everywhere else.
//
// THE FIELD TABLE BELOW IS A HAND-MAINTAINED WHITELIST. Every new Config switch MUST
// be added to it AND to switches(). This table fell behind once -- compact_repack_budget
// was missing, so the C3 arm-equality assertion in check_c3/bench_long passed
// vacuously and verified nothing. A switch that is absent here cannot be checked, and
// the gate that names it will silently succeed. Without this, an arm that silently became a copy of another would
// make a gate report "passed" for a comparison that never happened.
inline void require_only_switches_differ(const validation::Config& a,
                                         const validation::Config& b,
                                         std::initializer_list<const char*> which) {
    struct Field { const char* name; bool x; bool y; };
    const Field fields[] = {
        {"write_fast_locate", a.write_fast_locate, b.write_fast_locate},
        {"reuse_locate_ctx", a.reuse_locate_ctx, b.reuse_locate_ctx},
        {"compact_reuse_tag", a.compact_reuse_tag, b.compact_reuse_tag},
        {"compact_needs_garbage", a.compact_needs_garbage, b.compact_needs_garbage},
        {"compact_repack_budget", a.compact_repack_budget, b.compact_repack_budget},
        {"subblock_reclaim", a.subblock_reclaim, b.subblock_reclaim},
        {"scan_reserve_result", a.scan_reserve_result, b.scan_reserve_result},
    };
    for (const auto& f : fields) {
        bool expected = false;
        for (const char* w : which) if (std::string(w) == f.name) expected = true;
        if ((f.x != f.y) != expected) {
            throw std::runtime_error(std::string("arm configuration is wrong: switch '") +
                                     f.name + "' should " + (expected ? "" : "not ") +
                                     "differ between the two arms");
        }
    }
    // The non-switch fields must agree, or the comparison is not controlled.
    if (a.block_bytes != b.block_bytes || a.slots != b.slots ||
        a.model_kind != b.model_kind || a.multiblock != b.multiblock ||
        a.reuse_group_ids != b.reuse_group_ids || a.ov_threshold != b.ov_threshold) {
        throw std::runtime_error("arm configuration is wrong: a non-switch field differs");
    }
}

}  // namespace e25sw
