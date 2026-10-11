"""Generator v2: the entry's generator plus two diagnostic setters for the prototype.

`make_geometry_prototype.py` (v1) is FROZEN: it is the provenance recorded in the GD
entry round, so the entry's identity record keeps verifying byte-for-byte. This v2
imports it, runs it unchanged, and applies one more bounded, anchor-checked edit to
the produced header:

  * `diag_hot_geom_set_nb(g, v)` / `diag_hot_geom_set_start(g, v)` / `diag_hot_geom_set_slots(g, v)`
    -- E25_DIAG-only injection points, so the prototype's gate can deliberately
    corrupt a descriptor field and prove the B0..G1 differential REJECTS the wrong
    geometry instead of silently answering from the old one.

Everything else -- the descriptor, the eight arm ternaries, the dispatch -- is v1's.
"""
import hashlib
from pathlib import Path

import make_geometry_prototype as v1

ROOT = v1.ROOT
UPSTREAM = v1.UPSTREAM
PIN_INDEX = v1.PIN_INDEX
PIN_SWITCHES = v1.PIN_SWITCHES

ANCHOR = (
    "    std::uintptr_t diag_hot_geom_block_address(std::uint32_t g, std::size_t i) const {\n"
    "        const auto& d = hot_geom_[g];\n"
    "        return reinterpret_cast<std::uintptr_t>(\n"
    "            storage_ + std::size_t(d.start) + i * std::size_t(d.bb));\n"
    "    }\n")

SETTERS = '''
    // Prototype injection points. The gate writes a wrong value here, requires the
    // arm differential to report a disagreement, then restores the true geometry.
    void diag_hot_geom_set_nb(std::uint32_t g, std::uint32_t v) const { hot_geom_[g].nb = v; }
    void diag_hot_geom_set_start(std::uint32_t g, std::uint64_t v) const { hot_geom_[g].start = v; }
    void diag_hot_geom_set_slots(std::uint32_t g, std::uint16_t v) const { hot_geom_[g].slots = v; }
'''


GUARD_ANCHOR = (
    "    const std::byte* hot_base(std::uint32_t g, std::size_t i) const {\n"
    "#ifdef E25_DIAG\n"
    "        E25_INC(gd_hot_geom_reads);\n"
    "#endif\n")

GUARD = (
    "    const std::byte* hot_base(std::uint32_t g, std::size_t i) const {\n"
    "#ifdef E25_DIAG\n"
    "        E25_INC(gd_hot_geom_reads);\n"
    "        if (g >= hot_geom_.size())\n"
    "            throw std::logic_error(\"GD descriptor missing for this group\");\n"
    "#endif\n")


def generate(destination):
    path = v1.generate(Path(destination))
    text = path.read_text()
    text = v1._replace(text, ANCHOR, ANCHOR + SETTERS, what="prototype injection setters")
    text = v1._replace(text, GUARD_ANCHOR, GUARD, what="prototype descriptor guard")
    path.write_text(text)
    return path


if __name__ == "__main__":
    import sys
    p = generate(Path(sys.argv[1]))
    print(p, hashlib.sha256(p.read_bytes()).hexdigest())