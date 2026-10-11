#!/usr/bin/env python3
"""Fetch the pinned tlx/tlx B+ tree headers for the G5 external baseline.

The literature task book (§十) requires a mature B+ tree that supports the same string keys,
value ownership and scan semantics as the prototype. TLX is the maintained successor of the
STX B+ tree, and its licence is BSL-1.0 (permissive) -- which matters, because the STX
repository itself is GPL-3.0 and vendoring that would relicense this project, a decision that
belongs to the project owner and not to this script.

Only three headers are needed. The include closure was resolved from the pinned commit and is
exactly: btree_map.hpp, btree.hpp, die/core.hpp.

Every file is verified by byte length AND SHA-256 before it is accepted; a mismatch aborts.
Mirrors third_party/fetch_libart.py in structure, so the two vendored baselines are recorded
the same way.

Pinned commit: 2dd63ab839909f0b43cd02108a1869c4670b2c8e
Repo:          https://github.com/tlx/tlx
Licence:       BSL-1.0 (see vendor/tlx/LICENSE)
"""

from pathlib import Path
import hashlib
import sys
import urllib.request

COMMIT = "2dd63ab839909f0b43cd02108a1869c4670b2c8e"
ROOT = Path(__file__).resolve().parent

# path -> (sha256, bytes, upstream url)
FILES = [
    (
        "vendor/tlx/tlx/container/btree.hpp",
        "566bc3f8ba7199f6e926b1225f0b67358a5d4d9deec9f5af0293293188fc1629",
        130659,
        f"https://raw.githubusercontent.com/tlx/tlx/{COMMIT}/tlx/container/btree.hpp",
    ),
    (
        "vendor/tlx/tlx/container/btree_map.hpp",
        "4c640eaa8bf01f64179c31bfdf36dafe0b870c5e94d4776ab2612582355b74a2",
        18442,
        f"https://raw.githubusercontent.com/tlx/tlx/{COMMIT}/tlx/container/btree_map.hpp",
    ),
    (
        "vendor/tlx/tlx/die/core.hpp",
        "5c4e954c19dfb3a581ee187a6a23a12b89a53d40e5f1830753833e5ca303fc68",
        16108,
        f"https://raw.githubusercontent.com/tlx/tlx/{COMMIT}/tlx/die/core.hpp",
    ),
    (
        "vendor/tlx/LICENSE",
        "c9bff75738922193e67fa726fa225535870d2aa1059f91452c411736284ad566",
        1338,
        f"https://raw.githubusercontent.com/tlx/tlx/{COMMIT}/LICENSE",
    ),
    (
        "vendor/tlx/README.md",
        "01215241acaca6c9dadd69045130d74882b089b038f1930515a408b01ec271c8",
        4201,
        f"https://raw.githubusercontent.com/tlx/tlx/{COMMIT}/README.md",
    ),
]


def main():
    for rel, want_sha, want_len, url in FILES:
        dest = ROOT / rel

        if dest.exists():
            data = dest.read_bytes()
            if hashlib.sha256(data).hexdigest() == want_sha and len(data) == want_len:
                print("ok       %s (already present, hash matches)" % rel)
                continue

        with urllib.request.urlopen(url, timeout=60) as r:
            data = r.read()

        got_sha = hashlib.sha256(data).hexdigest()

        if len(data) != want_len or got_sha != want_sha:
            print("MISMATCH %s\n  want sha256 %s (%d bytes)\n  got  sha256 %s (%d bytes)"
                  % (rel, want_sha, want_len, got_sha, len(data)), file=sys.stderr)
            return 1

        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        print("fetched  %s  %d bytes  sha256 %s" % (rel, len(data), got_sha[:16]))

    print("done: pinned at %s" % COMMIT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
