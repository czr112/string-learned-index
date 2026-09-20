#!/usr/bin/env python3
"""Fetch the pinned armon/libart sources for E12 baseline work.

Mirrors validation_e4_20260911/sources.json (libart entries only) so that the
bytes are identical to the E4-recorded artifacts.  Every file is verified by
byte length AND SHA-256 before it is accepted; a mismatch aborts.

Pinned commit: 301046804af165269e37da6725f5a4aec9ecc881
Repo:          https://github.com/armon/libart
"""
from pathlib import Path
import concurrent.futures
import hashlib
import json
import sys
import urllib.request

COMMIT = "301046804af165269e37da6725f5a4aec9ecc881"
ROOT = Path(__file__).resolve().parent

# path -> (sha256, bytes, upstream url)
FILES = [
    (
        "vendor/libart/src/art.c",
        "b65f20fdc160a6e4c0b1b390086f682d381e3638d8690594158ad75cda33396e",
        29668,
        f"https://raw.githubusercontent.com/armon/libart/{COMMIT}/src/art.c",
    ),
    (
        "vendor/libart/src/art.h",
        "4443de6fd42581982167f7826f63bd956d21a958f204575e52512e007deab5af",
        4994,
        f"https://raw.githubusercontent.com/armon/libart/{COMMIT}/src/art.h",
    ),
    (
        "vendor/libart/LICENSE",
        "642c3e41db4ebb7774e5e8d5f34cbdb8e1ba6c13ca26ace875fb307994f21f9a",
        1492,
        f"https://raw.githubusercontent.com/armon/libart/{COMMIT}/LICENSE",
    ),
    (
        "vendor/libart/README.md",
        "efe93fd3502c3a2be186fc3023d29d9f4d0a6ed1b810e6dfea43815396daf04a",
        1600,
        f"https://raw.githubusercontent.com/armon/libart/{COMMIT}/README.md",
    ),
]


def fetch(item):
    rel, want_sha, want_len, url = item
    p = ROOT / rel
    if p.exists():
        blob = p.read_bytes()
        if len(blob) == want_len and hashlib.sha256(blob).hexdigest() == want_sha:
            return f"cached-ok   {rel}"
    req = urllib.request.Request(url, headers={"User-Agent": "e12-baseline-prep"})
    with urllib.request.urlopen(req, timeout=60) as r:
        payload = r.read()
    got_sha = hashlib.sha256(payload).hexdigest()
    if len(payload) != want_len or got_sha != want_sha:
        raise RuntimeError(
            f"source hash mismatch {rel}: got {len(payload)}B {got_sha}, "
            f"want {want_len}B {want_sha}"
        )
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(payload)
    return f"downloaded  {rel}"


def main():
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for line in pool.map(fetch, FILES):
            print(line)
    print(f"Verified {len(FILES)} pinned libart source files at commit {COMMIT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
