"""Read one optimised, counter-free binary and report the integer division in each arm.

The GD entry fixes one code-shape question in advance: the production point query
re-derives a group's sub-block count as `(off_[g+1]-off_[g]) / bb_[g]`, a run-time
division; the descriptor path stores that count and must not divide. This script
reads the disassembly of one symbol per arm and counts x86 INTEGER divisions
(div/idiv and their operand-size forms). Floating-point divides are excluded by
matching the mnemonic exactly, never as a substring.

Usage:
    python3 check_geometry_shape.py <elf> --pair <baseline-substr> <candidate-substr> [...]

Output: one JSON object. Exit code 0 only when every substring matched exactly one
symbol; a missing or ambiguous symbol is a device error, not a finding. A pair with
`eliminated_any_div == false` is a legitimate RESULT (it means this build did not
remove the division), so it does not fail the script.
"""
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

DIV_RE = re.compile(r"^i?div[blwq]?$")
LABEL_RE = re.compile(r"^([0-9a-f]+) <(.+)>:$")
INSN_RE = re.compile(r"^\s+[0-9a-f]+:\s+(?:[0-9a-f]{2}\s)+\s*([a-z][a-z0-9.]*)")


def _tool(name, *args):
    try:
        out = subprocess.run([name, *args], capture_output=True, text=True, timeout=300)
    except FileNotFoundError as exc:
        raise SystemExit("device error: %s is not installed (%s)" % (name, exc))
    if out.returncode:
        raise SystemExit("device error: %s failed: %s" % (name, out.stderr.strip()))
    return out.stdout


def disassemble(binary):
    text = _tool("objdump", "-d", "--demangle", str(binary))
    functions = {}
    current = None
    for line in text.splitlines():
        label = LABEL_RE.match(line)
        if label:
            current = label.group(2)
            functions.setdefault(current, [])
            continue
        if current is None:
            continue
        insn = INSN_RE.match(line)
        if insn:
            functions[current].append(insn.group(1))
    return functions


def analyse(binary, pairs):
    functions = disassemble(binary)
    result = {"binary": str(binary),
              "binary_sha256": hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
              "objdump": _tool("objdump", "--version").splitlines()[0],
              "nm": _tool("nm", "--version").splitlines()[0],
              "pairs": []}
    for baseline_substr, candidate_substr in pairs:
        row = {"baseline_substr": baseline_substr, "candidate_substr": candidate_substr}
        counts = {}
        for role, needle in (("baseline", baseline_substr), ("candidate", candidate_substr)):
            # `[clone .cold]` symbols are GCC's out-of-line exception landing pads:
            # they carry no hot-path work, and counting them would make every match
            # ambiguous. The body under test is the primary clone.
            matches = [name for name in functions
                       if needle in name and "[clone .cold]" not in name]
            if len(matches) != 1:
                raise SystemExit("device error: %r matches %d symbols %r"
                                 % (needle, len(matches), matches[:6]))
            insns = functions[matches[0]]
            divs = [m for m in insns if DIV_RE.match(m)]
            counts[role] = {"symbol": matches[0], "instructions": len(insns),
                            "div_total": len(divs), "div_mnemonics": sorted(set(divs))}
        row.update(counts)
        row["div_delta"] = counts["baseline"]["div_total"] - counts["candidate"]["div_total"]
        row["eliminated_any_div"] = counts["candidate"]["div_total"] < counts["baseline"]["div_total"]
        result["pairs"].append(row)
    return result


def selftest():
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        src = tmp / "shape_selftest.cpp"
        src.write_text(
            '__attribute__((noinline)) long divides(long a, long b) { return a / b; }\n'
            '__attribute__((noinline)) long precomputed(long a, long b) { (void)b; return a >> 3; }\n'
            'int main(int argc, char**) { return (int)(divides(argc, 7) + precomputed(argc, 7)); }\n')
        exe = tmp / "shape_selftest"
        build = subprocess.run(["g++", "-O3", "-std=c++17", str(src), "-o", str(exe)],
                               capture_output=True, text=True)
        if build.returncode:
            raise SystemExit("selftest build failed: " + build.stderr)
        report = analyse(exe, [("divides", "precomputed")])
        pair = report["pairs"][0]
        assert pair["baseline"]["div_total"] == 1, pair
        assert pair["candidate"]["div_total"] == 0, pair
        assert pair["eliminated_any_div"] is True, pair
        # a missing symbol must be a device error, and must not be swallowed
        try:
            analyse(exe, [("divides", "not_a_symbol_anywhere")])
        except SystemExit as exc:
            report["missing_symbol_error"] = str(exc)
        else:
            raise SystemExit("selftest: missing symbol was not rejected")
        print(json.dumps(report, indent=2))


def main(argv):
    if len(argv) == 2 and argv[1] == "--selftest":
        selftest()
        return 0
    if len(argv) < 4:
        raise SystemExit(__doc__)
    binary = argv[1]
    pairs = []
    i = 2
    while i < len(argv):
        if argv[i] != "--pair" or i + 2 >= len(argv):
            raise SystemExit("expected --pair <baseline-substr> <candidate-substr>")
        pairs.append((argv[i + 1], argv[i + 2]))
        i += 3
    print(json.dumps(analyse(binary, pairs), indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))