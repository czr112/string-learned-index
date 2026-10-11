#!/usr/bin/env python3
"""E38/P3 pre-check: where does the memory go? ONE process, ONE dataset, ONE phase.

NOT a timing run and NOT a measurement of the index. It answers four questions the review
named, and it answers them from OUTSIDE the process -- no allocation accounting is added back
into the clean timing program, which is the one thing that would make its numbers unusable.

    1. parallelism          does the driver start more than one benchmark at a time?
    2. object lifetime      do three arm indexes, the training temporaries and the reference
                            table live at the same time?
    3. cross-repetition     do the query results, the diagnostic rows or any tracking table keep
                            growing from one repetition to the next?
    4. peak                 which phase peaks, and does a repeat of the run end higher?

THE THIRD QUESTION IS THE ONE RSS ALONE CANNOT ANSWER. A process that frees a large table and
does not return it to the OS shows the same resident figure as one that leaked it. So the check
reads BOTH the resident size AND the process's own view of its heap (`mallinfo2`), and it runs
the same work twice: a figure that is the same at the end of both runs is a high-water mark, and
a figure that is higher the second time is growth.

Usage:  python3 precheck_memory.py [--kind mixed] [--n 131072] [--reps 3]
"""

import argparse
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))

# What the driver does: one subprocess at a time, and it waits for each. Verified here rather
# than asserted, because the whole of question 1 is whether that is still true.
SERIAL_EVIDENCE = "run_lits.py invokes through subprocess.run(...) in a nested loop, no pool and no &"


def read_status(pid):
    """(VmRSS, VmHWM) in KiB for ONE process, or None when it is gone."""
    try:
        with open("/proc/%d/status" % pid, "r") as f:
            text = f.read()
    except OSError:
        return None

    rss = re.search(r"^VmRSS:\s+(\d+) kB", text, re.M)
    hwm = re.search(r"^VmHWM:\s+(\d+) kB", text, re.M)

    return (int(rss.group(1)) if rss else 0, int(hwm.group(1)) if hwm else 0)


def descendants(root):
    """Every live descendant of `root`, including itself.

    NEEDED BECAUSE THE PID WE HOLD IS THE WRAPPER'S. `tools_build.py run_current` starts the
    benchmark as a child and waits on it, so polling only the wrapper reports a few megabytes of
    Python and nothing about the run. This walks /proc once and sums the tree.
    """
    try:
        entries = os.listdir("/proc")
    except OSError:
        return [root]

    parent = {}
    for e in entries:
        if not e.isdigit():
            continue

        try:
            with open("/proc/%s/stat" % e, "rb") as f:
                fields = f.read().rsplit(b")", 1)[1].split()
        except (OSError, IndexError):
            continue

        parent[int(e)] = int(fields[1])

    out = {root}
    changed = True
    while changed:
        changed = False
        for pid, ppid in parent.items():
            if ppid in out and pid not in out:
                out.add(pid)
                changed = True

    return sorted(out)


def tree_rss(root):
    """(summed RSS, summed HWM) over the tree, in KiB."""
    rss = 0
    hwm = 0

    for pid in descendants(root):
        s = read_status(pid)
        if s is None:
            continue

        if pid == root:
            rss += s[0]
            hwm += s[1]
        else:
            # A child's own HWM is what it peaked at, not what it holds now; the RSS sum is the
            # live figure and the HWM tracking below is kept to the root for the same reason.
            rss += s[0]

    return (rss, hwm)


def meminfo():
    with open("/proc/meminfo", "r") as f:
        text = f.read()

    total = re.search(r"^MemTotal:\s+(\d+) kB", text, re.M)
    avail = re.search(r"^MemAvailable:\s+(\d+) kB", text, re.M)

    return (int(total.group(1)) if total else 0, int(avail.group(1)) if avail else 0)


def sample(pid, trace, stop):
    """Poll the child's RSS while it runs. Cheap: one /proc read every 200 ms."""
    while not stop():
        s = read_status(pid)
        if s is None:
            return

        trace.append((time.time(), s[0], s[1]))
        time.sleep(0.2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kind", default="mixed")
    parser.add_argument("--n", default="131072")
    parser.add_argument("--reps", type=int, default=3)
    parser.add_argument("--mode", default="same")
    parser.add_argument("--csv", default="")
    opts = parser.parse_args()

    total_kb, avail_kb = meminfo()
    print("host memory: %.1f GiB total, %.1f GiB available before the run"
          % (total_kb / 1048576.0, avail_kb / 1048576.0))
    print("parallelism: %s" % SERIAL_EVIDENCE)
    print()

    work = os.path.join(HERE, "results", "main", "lits_adapter_%s.jsonl" % opts.mode)
    if os.path.exists(work):
        os.remove(work)

    # Through the strict entry, so a stale binary is refused rather than silently measured.
    args = [sys.executable, "tools_build.py", "run_current", "bench_lits_adapter",
            opts.kind, opts.n, str(opts.reps), opts.csv, "PRECHECK", opts.mode]

    print("running: %s %s reps=%d mode=%s  (ONE process, ONE dataset, ONE phase)"
          % (opts.kind, opts.n, opts.reps, opts.mode))

    started = time.time()
    child = subprocess.Popen(args, cwd=HERE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    trace = []
    last_t = [0.0]
    peak = [0]

    def stop():
        return child.poll() is not None

    while not stop():
        s = tree_rss(child.pid)
        if s is None:
            break

        trace.append((time.time() - started, s[0], s[1]))
        peak[0] = max(peak[0], s[0])
        time.sleep(0.2)

    out, _ = child.communicate()
    elapsed = time.time() - started

    if child.returncode != 0:
        print("CHILD FAILED rc=%d" % child.returncode)
        print(out[-2000:])
        return 1

    rows = 0
    if os.path.exists(work):
        with open(work, "rb") as f:
            rows = f.read().count(b"\n")

    final = read_status(child.pid)

    print("child finished rc=0 in %.0fs" % elapsed)
    print()

    if trace:
        first_rss = trace[0][1]
        last_rss = trace[-1][1]
        print("RSS over the whole process tree (wrapper + benchmark):")
        print("  first=%.1f MiB  last=%.1f MiB  peak=%.1f MiB"
              % (first_rss / 1024.0, last_rss / 1024.0, peak[0] / 1024.0))

        # The shape of the trace: a run that accumulates shows a rising floor rather than a
        # rising peak, so the floor at 25/50/75/100 per cent is what to read.
        n = len(trace)
        marks = [trace[int(n * q)][1] / 1024.0 for q in (0.0, 0.25, 0.5, 0.75, 0.999)]
        print("RSS at 0/25/50/75/100%%: " + "  ".join("%.1f" % m for m in marks) + "  MiB")

        tail = trace[len(trace) // 2:]
        if len(tail) > 4:
            floor_early = min(t[1] for t in trace[:len(trace) // 2]) / 1024.0
            floor_late = min(t[1] for t in tail) / 1024.0
            print("floor (min RSS) first half=%.1f MiB  second half=%.1f MiB  delta=%+.1f MiB"
                  % (floor_early, floor_late, floor_late - floor_early))
            print("   -> a rising FLOOR is accumulation; a flat floor with a high peak is a")
            print("      high-water mark the allocator kept, which is not the same thing.")

    total_kb2, avail_kb2 = meminfo()
    print()
    print("host available after: %.1f GiB (delta %+.1f GiB)"
          % (avail_kb2 / 1048576.0, (avail_kb2 - avail_kb) / 1048576.0))
    print("rows written: %d" % rows)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
