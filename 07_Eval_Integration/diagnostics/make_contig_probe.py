"""Generate the M2 entry probe: one diagnostic accessor, nothing else.

The M2 entry measures which 64-byte lines the production block loop would touch, so it
needs the REAL address of each sub-block -- the production header exposes counts, tags and
suffixes but not the address itself. This generator copies the pinned index.hpp and adds
exactly one E25_DIAG-only accessor:

    std::uintptr_t diag_block_address(std::uint32_t g, std::size_t i) const;

No behaviour, no layout, no switch changes: the probe differs from production only by that
read-only function, and the entry that uses it runs no timing. Every replacement asserts
its anchor is unique, so source drift fails loudly.
"""
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "02_Code_and_Experiments/validation_e25_20260918"
PIN_INDEX = "640660a32f1fd1476d0d889d541c007883e5597d70e476524e3bcf2556ec8aa1"
PIN_SWITCHES = "5486909fffc61c1eef940c058eae3fe036e970f0db21c988b3b41800803f0d4e"

ANCHOR = "    std::size_t diag_subblocks(std::uint32_t g) const { return subblocks(g); }\n"
ACCESSOR = (
    "    std::uintptr_t diag_block_address(std::uint32_t g, std::size_t i) const {\n"
    "        return reinterpret_cast<std::uintptr_t>(base_of(g, i));\n"
    "    }\n")


def generate(destination):
    raw = (UPSTREAM / "index.hpp").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN_INDEX:
        raise RuntimeError("contig probe source drift")
    if hashlib.sha256((UPSTREAM / "switches.hpp").read_bytes()).hexdigest() != PIN_SWITCHES:
        raise RuntimeError("switches drift")
    source = raw.decode()
    if source.count(ANCHOR) != 1:
        raise RuntimeError("contig probe anchor is not unique")
    source = source.replace(ANCHOR, ANCHOR + ACCESSOR)
    destination.mkdir(exist_ok=False)
    path = destination / "index.hpp"
    path.write_text(source)
    return path


if __name__ == "__main__":
    p = generate(Path(sys.argv[1]))
    print(p, hashlib.sha256(p.read_bytes()).hexdigest())