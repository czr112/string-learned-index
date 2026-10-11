# -*- coding: utf-8 -*-
"""TRUNCDIR A/B runner: pair selection as a run parameter, default unchanged."""
import io, os
HERE = os.path.dirname(os.path.abspath(__file__))
p = os.path.join(HERE, "run_depth_dir_ab.py")
t = io.open(p, encoding="utf-8", newline="").read()

def sub(old, new, what):
    global t
    if t.count(old) != 1:
        raise SystemExit("FATAL: %s not found exactly once (%d)" % (what, t.count(old)))
    t = t.replace(old, new)

sub('''Usage:  python3 run_depth_dir_ab.py --run [--procs 5] [--reps 4] [--stem depth_ab]
        python3 run_depth_dir_ab.py --analyze [--stem depth_ab]
''',
'''The ARM PAIR is a run parameter (TRUNCDIR design section 6): the main comparison is C16 vs
DDirT and the mechanism comparison DDir vs DDirT, and both use this analyzer unchanged. The
default pair is the untruncated one, so a run without --arms reproduces the previous round.

Usage:  python3 run_depth_dir_ab.py --run [--procs 5] [--reps 4] [--stem depth_ab] [--arms C16,DDir]
        python3 run_depth_dir_ab.py --analyze [--stem depth_ab] [--arms C16,DDir]
''', "docstring")

sub('''CORPORA = [("mixed", 131072, ""), ("skew", 131072, ""), ("tranco", 262144, TR)]
ARMS = ["C16", "DDir"]
METRICS = ["static_ns", "post_ns", "build_ns", "maint_ns"]
''',
'''CORPORA = [("mixed", 131072, ""), ("skew", 131072, ""), ("tranco", 262144, TR)]
# The bench knows these names; the pair given here is handed to it verbatim (argv[7]).
KNOWN_ARMS = ("C16", "DDir", "DDirT")
DEFAULT_ARMS = "C16,DDir"
METRICS = ["static_ns", "post_ns", "build_ns", "maint_ns"]


def parse_arms(spec):
    names = [s.strip() for s in spec.split(",")]
    if len(names) != 2:
        raise SystemExit("REFUSED: --arms must name exactly two arms: %s" % spec)
    for n in names:
        if n not in KNOWN_ARMS:
            raise SystemExit("REFUSED: unknown arm %s (known: %s)" %
                             (n, ", ".join(KNOWN_ARMS)))
    if names[0] == names[1]:
        raise SystemExit("REFUSED: --arms names the same arm twice: %s" % spec)
    return names
''', "constants + parse_arms")

sub('''def run(procs, reps, stem):
''', '''def run(procs, reps, stem, arms_spec):
''', "run signature")

sub('''            cmd = ["python3", "tools_build.py", "run", "bench_depth_dir", kind, str(n),
                   str(reps), csv, tag, "%s_%s" % (stem, kind)]
''', '''            cmd = ["python3", "tools_build.py", "run", "bench_depth_dir", kind, str(n),
                   str(reps), csv, tag, "%s_%s" % (stem, kind), arms_spec]
''', "run command")

sub('''def analyze(stem):
    problems = []
    summary = []
''', '''def analyze(stem, arms_spec):
    arm_names = parse_arms(arms_spec)
    ref_name, cand_name = arm_names[0], arm_names[1]
    problems = []
    summary = []
''', "analyze header")

sub('''            if set(arms) != set(ARMS):
                incomplete += 1
                continue
            a, b = arms["C16"], arms["DDir"]
''', '''            if set(arms) != set(arm_names):
                incomplete += 1
                continue
            a, b = arms[ref_name], arms[cand_name]
''', "pair lookup")

sub('''        per_proc = {}
        for (tag, rep), arms in sorted(by_key.items()):
            if set(arms) != set(ARMS):
                continue
            a, b = arms["C16"], arms["DDir"]
''', '''        per_proc = {}
        for (tag, rep), arms in sorted(by_key.items()):
            if set(arms) != set(arm_names):
                continue
            a, b = arms[ref_name], arms[cand_name]
''', "per_proc pair")

sub('''        entry = {"kind": kind, "pairs": len([k for k, v in by_key.items() if set(v) == set(ARMS)]),
                 "incomplete": incomplete}
''', '''        entry = {"kind": kind, "arms": arm_names,
                 "pairs": len([k for k, v in by_key.items() if set(v) == set(arm_names)]),
                 "incomplete": incomplete}
''', "entry")

sub('''                "faster_than_C16_processes": sum(1 for v in vals if v < 1.0),
''', '''                "candidate_faster_processes": sum(1 for v in vals if v < 1.0),
''', "direction field")

sub('''        for arm in ARMS:
            mb = [r["model_bytes"] for r in recs if r["arm"] == arm]
''', '''        for arm in arm_names:
            mb = [r["model_bytes"] for r in recs if r["arm"] == arm]
''', "model bytes loop")

sub('''    ap.add_argument("--stem", default="depth_ab")
    a = ap.parse_args()
    if a.run:
        return run(a.procs, a.reps, a.stem)
    return analyze(a.stem)
''', '''    ap.add_argument("--stem", default="depth_ab")
    ap.add_argument("--arms", default=DEFAULT_ARMS,
                    help="reference,candidate arm names, e.g. C16,DDirT or DDir,DDirT")
    a = ap.parse_args()
    parse_arms(a.arms)   # refuse an unusable pair before running or analyzing anything
    if a.run:
        return run(a.procs, a.reps, a.stem, a.arms)
    return analyze(a.stem, a.arms)
''', "main")

io.open(p, "w", encoding="utf-8", newline="\n").write(t)
print("patched", p)
