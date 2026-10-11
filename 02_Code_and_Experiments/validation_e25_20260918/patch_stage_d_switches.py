#!/usr/bin/env python3
"""Stage D probe + switch discipline (2026-10-10).

TWO THINGS, both demanded by evidence already on disk:

1. The S0 snapshot audit (S0_SNAPSHOT.md section 3, CODE_AUDIT.md) found that
   `offset_directory`, `write_simd` and `write_locate` are in NEITHER the
   require_only_switches_differ white list NOR its non-switch equality check -- so two
   arms differing only in one of them would pass the assertion VACUOUSLY. The audit's
   own instruction is that any experiment touching them must add them first. Stage D is
   about to touch `write_locate`, so they go in now, together with their line in
   switches() so a row always says which of them was set.

2. A probe build flag for the timing entry (DR_WRITE_LOCATE): the write path's search
   runs with the boundary directory ON. This prices the one thing the count build cannot
   see -- the search's per-block loop -- BEFORE a candidate is defined. It is a probe,
   not a candidate: nothing here changes any default.
"""
import sys

EDITS = []


def sub(path, old, new, note):
    EDITS.append((path, old, new, note))


sub(
    "switches.hpp",
    """    c.compact_region_routing = false;   // structural candidate, not in the frozen set
    c.reclaim_low_activity = false;     // structural candidate, not in the frozen set""",
    """    c.compact_region_routing = false;   // structural candidate, not in the frozen set
    c.reclaim_low_activity = false;     // structural candidate, not in the frozen set
    // S0 audit: these three were in neither the white list nor the equality check, so an
    // arm pair differing only in one of them asserted nothing. They are named here for the
    // same reason everything else is: a baseline may not inherit a switch it does not state.
    c.offset_directory = true;          // Config default, stated explicitly
    c.write_simd = true;                // Config default, stated explicitly
    c.write_locate = false;             // Config default (E11/E13: a selectable component, not a default)""",
    "baseline_config",
)

sub(
    "switches.hpp",
    """    c.compact_region_routing = false;  // NOT part of the frozen set; the routing candidate
    c.reclaim_low_activity = false;    // NOT part of the frozen set; the n<2 reclaim candidate""",
    """    c.compact_region_routing = false;  // NOT part of the frozen set; the routing candidate
    c.reclaim_low_activity = false;    // NOT part of the frozen set; the n<2 reclaim candidate
    c.offset_directory = true;         // shipped value
    c.write_simd = true;               // shipped value
    c.write_locate = false;            // shipped value (the write path searches without the directory)""",
    "pin_frozen_switches",
)

sub(
    "switches.hpp",
    """           ",reclaim_low_activity=" + (c.reclaim_low_activity ? "1" : "0") +""",
    """           ",reclaim_low_activity=" + (c.reclaim_low_activity ? "1" : "0") +
           ",offset_directory=" + (c.offset_directory ? "1" : "0") +
           ",write_simd=" + (c.write_simd ? "1" : "0") +
           ",write_locate=" + (c.write_locate ? "1" : "0") +""",
    "switches() text",
)

sub(
    "switches.hpp",
    """        {"reclaim_low_activity", a.reclaim_low_activity, b.reclaim_low_activity},""",
    """        {"reclaim_low_activity", a.reclaim_low_activity, b.reclaim_low_activity},
        // S0 audit: these three were missing from this table, which made an arm pair that
        // differed only in one of them pass vacuously (the exact failure the table exists to
        // catch, and the one G0 found once already in a different form).
        {"offset_directory", a.offset_directory, b.offset_directory},
        {"write_simd", a.write_simd, b.write_simd},
        {"write_locate", a.write_locate, b.write_locate},""",
    "white list",
)

sub(
    "diag_write_residual.cpp",
    """        e25sw::require_only_switches_differ(cfgB, cfgW, {"subblock_reclaim", "reclaim_widest"});""",
    """        e25sw::require_only_switches_differ(cfgB, cfgW, {"subblock_reclaim", "reclaim_widest"});
#ifdef DR_WRITE_LOCATE
        // Stage D probe (not a candidate): the write path's SEARCH with the boundary
        // directory on. The count build cannot see the search's per-block loop, so the
        // only way to price it before defining a candidate is to time it.
        cfgW.write_locate = true;
        std::cerr << "PROBE DR_WRITE_LOCATE: cfgW.write_locate = true\\n";
#endif""",
    "probe flag",
)


def main():
    for path, old, new, note in EDITS:
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        if new in text:
            print("ALREADY APPLIED (%s)" % note, file=sys.stderr)
            sys.exit(2)
        if text.count(old) != 1:
            print("ANCHOR NOT FOUND exactly once (%s): %d" % (note, text.count(old)),
                  file=sys.stderr)
            sys.exit(2)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text.replace(old, new, 1))
        print("patched %s: %s" % (path, note))


if __name__ == "__main__":
    main()
