#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build a publishable mirror of the working tree.

The working tree (D:\\StringLearnedIndex_Workspace) is a research workspace: it holds
386 MB of compiled binaries, a 21 MB third-party domain list, the course materials it
was given, and a historical ZIP. None of that belongs in a public git repository, and
the workspace's own dated manifests are supposed to stay valid, so nothing is written
back into it -- the mirror is built on the side.

Usage:
    python sync_repo.py --dry-run     # print what would be copied
    python sync_repo.py --apply       # build the mirror

Rules are in RULES below; each carries the reason it is there.
"""
import argparse
import io
import os
import shutil
import sys

SRC = r"D:\StringLearnedIndex_Workspace"
DST = r"D:\StringLearnedIndex_repo"

# Directories never descended into, with the reason.
SKIP_DIRS = {
    ".git": "the mirror's own history",
    "build": "compiled artifacts",
    "__pycache__": "python bytecode",
    "03_Source_Materials": "course materials uploaded to the project, not the author's work",
    "05_Historical_Versions": "historical ZIP of an earlier validation round",
    "output": "generated packaging output",
    "consolidation": "generated packaging output",
    "upload": "generated packaging output",
}

# Suffixes that are source, documentation, evidence or closure records.
KEEP_SUFFIX = {
    ".md", ".hpp", ".cpp", ".h", ".c", ".py", ".jsonl", ".json", ".log", ".txt",
    ".sha256", ".sh", ".out", ".summary",
}
# Named files with no useful suffix that must still travel.
KEEP_NAMES = {"MANIFEST.sha256", "LICENSE", "README", "README.md"}

# Suffixes that are machine output or third-party data, never published.
DROP_SUFFIX = {
    ".exe", ".o", ".obj", ".a", ".lib", ".so", ".dll", ".dylib", ".pdb", ".bin",
    ".zip", ".7z", ".tar", ".gz", ".docx", ".pptx", ".pdf", ".png", ".jpg",
}

# Individual paths (relative, forward slashes) excluded by name.
DROP_PATHS = {
    # 21.7 MB third-party domain list; the workspace README says it is fetched and
    # checksum-verified by script, so shipping it here would be redundant and is a
    # redistribution of someone else's dataset.
    "02_Code_and_Experiments/validation_e5_20260913/data/tranco_XN6XN.csv",
    # 3.1 MB result table regenerable by re-running E3's selector.
    "02_Code_and_Experiments/validation_e3_20260906/results/main/tranco_1000000/joint_groups.csv",
}

# Extensionless executables left behind by smoke builds (ELF, no suffix).
DROP_NAME_PREFIX = ("smoke_art", "probe_nul_prefix")

MAX_CSV_BYTES = 256 * 1024          # keep small result CSV, drop bulk data


def keep(rel, name, size):
    """Return (True, reason) or (False, reason)."""
    parts = rel.split("/")
    for d in parts[:-1]:
        if d in SKIP_DIRS:
            return False, "dir " + d + ": " + SKIP_DIRS[d]
    if rel in DROP_PATHS:
        return False, "named in DROP_PATHS"
    ext = os.path.splitext(name)[1].lower()
    if ext in DROP_SUFFIX:
        return False, "machine output or third-party binary (" + ext + ")"
    if name.startswith(DROP_NAME_PREFIX) and ext == "":
        return False, "extensionless smoke-build executable"
    if ext == ".csv":
        return (size <= MAX_CSV_BYTES), "csv over %d bytes" % MAX_CSV_BYTES
    if ext in KEEP_SUFFIX or name in KEEP_NAMES:
        return True, "kept"
    if ext == "":
        return False, "extensionless file that is not a known record"
    return False, "suffix not in the keep list"


def walk():
    for dirpath, dirnames, filenames in os.walk(SRC):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
        for fn in sorted(filenames):
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, SRC).replace(os.sep, "/")
            try:
                size = os.path.getsize(full)
            except OSError:
                continue
            ok, why = keep(rel, fn, size)
            yield rel, full, size, ok, why


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--dry-run", action="store_true")
    g.add_argument("--apply", action="store_true")
    a = ap.parse_args()

    kept, dropped = [], []
    for rel, full, size, ok, why in walk():
        (kept if ok else dropped).append((rel, full, size, why))

    total = sum(s for _, _, s, _ in kept)
    print("mirror plan: %d files kept (%.1f MB), %d files skipped"
          % (len(kept), total / 1048576.0, len(dropped)))

    by_dir = {}
    for rel, _, size, _ in kept:
        top = rel.split("/")[0]
        d = by_dir.setdefault(top, [0, 0])
        d[0] += 1
        d[1] += size
    print("\nper top-level directory:")
    for k in sorted(by_dir):
        n, s = by_dir[k]
        print("  %-32s %5d files  %8.1f MB" % (k, n, s / 1048576.0))

    print("\nskip reasons:")
    reasons = {}
    for _, _, size, why in dropped:
        r = reasons.setdefault(why, [0, 0])
        r[0] += 1
        r[1] += size
    for why in sorted(reasons, key=lambda w: -reasons[w][1]):
        n, s = reasons[why]
        print("  %-58s %5d files  %8.1f MB" % (why, n, s / 1048576.0))

    if a.dry_run:
        biggest = sorted(kept, key=lambda t: -t[2])[:12]
        print("\nlargest files that WOULD be published:")
        for rel, _, size, _ in biggest:
            print("  %8.2f MB  %s" % (size / 1048576.0, rel))
        return

    if os.path.isdir(DST):
        shutil.rmtree(DST)
    for rel, full, _, _ in kept:
        out = os.path.join(DST, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copy2(full, out)
    print("\nmirror written to " + DST)


if __name__ == "__main__":
    sys.exit(main())
