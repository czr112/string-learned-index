# -*- coding: utf-8 -*-
"""Build the minimal safety-patched LITS model from the archived pristine source.

THE PATCH AND WHY IT IS THE WHOLE PATCH.

The prediction and CDF loops start at `i = gcpl` and read `key[i - 1]`:

    for (int i = gcpl; key[i] && ps >= 1; ++i) {
        const auto &uni = m[i & PS_MASK][key[i - 1] & FC_MASK][key[i]];

When `gcpl == 0` the first iteration reads `key[-1]`. That is one byte BEFORE the key, and it
is not a read the caller can make legal: the adapter's contract is "the key starts at
position 0". The fix substitutes the same "no previous character" sentinel that the HPT's
training pass already uses at `b == 0` (lits_model.hpp:119: `int _fc = b == 0 ? 0 : ...`), so
training and prediction agree on what the context is at the string's first byte.

WHY NOTHING ELSE IS PATCHED. The task book's P1 step 1 asks for the MINIMAL safety fix and
explicitly forbids changing the HPT's memory layout in the same step. So this script does not
touch the table shape, the `UNI` type, the mask widths, the weight array or the early-stop
condition. `getPos_woGCPL` is deliberately NOT patched: it reads `key[0]` explicitly and then
loops from `i = 1`, so `key[i - 1]` is `key[0]` there and it can never reach `key[-1]`.

WHAT IS NOT FIXED, AND IS LEFT VISIBLE. lits_model.hpp:170 indexes the table with a raw
`signed` char, while the training pass at line 120 uses `unsigned char`. A byte >= 128 is
therefore a NEGATIVE index at prediction time. That is a real defect in the pinned source,
but fixing it is a change of behaviour rather than a boundary fix for our input domain, and
our corpus never reaches it (diag_domain measured max_byte = 126 over every population of all
three datasets). The adapter REJECTS high bytes instead, so the fault stays unreachable
rather than silently papered over -- see LITS_AUDIT.md section 11.4a.

VERIFICATION. The patched file's SHA-256 is compared against the hash E4 recorded for its own
patched build. Equal bytes means this is the same fix E4 validated, not a lookalike.

THE THIRD EDIT (added 2026-10-10, on the E4-verified result): `1UL << 52` -> `1ULL << 52` in
`getCdf`'s `min_double`. On LP64 the two are the SAME VALUE (2^-52, unsigned long is 64-bit
there), so the WSL identity of this header is value-identical under the change. On LLP64
(Windows: MinGW-w64, MSVC) `unsigned long` is 32 bits and the shift is out of range, which is a
hard compile ERROR in GCC -- there is no `-fpermissive` reading of it that is safe, because the
out-of-range shift folds to a different constant and that constant is the early-stop threshold.
So the portability fix cannot be a compiler flag; it is one token here, and its result is hashed
separately below (`WINDOWS_LLP64_SHA`) so a reader can always see WHICH of the two files is in
the tree. The E4 assertion above still runs first and still has to pass: the LLP64 edit is
applied ON TOP OF the E4-validated bytes, never instead of them.
"""

import difflib
import hashlib
import io
import os

HERE = os.path.dirname(os.path.abspath(__file__))

ORIGINAL = os.path.join(HERE, "vendor", "lits_model_original", "lits_model.hpp")
PATCHED = os.path.join(HERE, "vendor", "lits_model_safe", "lits_model.hpp")
PATCHFILE = os.path.join(HERE, "patches", "lits_model_safety.patch")

PRISTINE_SHA = "d5ac9b88f796c476259ac89bd9d52a81199e51a0567dc0e304e9af733529fd73"
E4_PATCHED_SHA = "b5a1c28251da7e74fd011e6b14a40927481c65ed24c837cc4892f26740af4c18"

# The E4-verified bytes with the one-token LLP64 portability edit applied (see the header).
# Recorded so the tree's own file can be told apart from E4's from the hash alone.
WINDOWS_LLP64_SHA = "4f8674b576f6d4ce2045e5b5a2106c7a61cb830c4df88febfdfbd34641ac93e3"
LLP64_OLD = "static constexpr double min_double = 1. / (1UL << 52);"
LLP64_NEW = "static constexpr double min_double = 1. / (1ULL << 52);"

# The headers the adapter's include chain actually needs, copied VERBATIM so that
# vendor/lits_model_safe/ is self-contained. Deliberately NOT lits_node.hpp: it pulls in
# HOT, cnode, iter and PMSS, and the task book forbids importing the full LITS node tree on
# the way to a model-only round. It stays in the archive for reference -- that is where the
# Calibration convention documented in the adapter was read from.
VERBATIM = ["lits_base.hpp", "lits_gpkl.hpp"]

# (loop head, function it belongs to). The body line is shared by both.
TARGETS = [
    ("for (int i = gcpl; key[i] && ps >= 1; ++i) {", "getPos"),
    ("for (int i = gcpl; key[i] && pro >= min_double; ++i) {", "getCdf"),
]

OLD_BODY = "const auto &uni = m[i & PS_MASK][key[i - 1] & FC_MASK][key[i]];"
NEW_BODY = "const auto &uni = m[i & PS_MASK][i == 0 ? 0 : (key[i - 1] & FC_MASK)][key[i]];"


def sha256_text(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def main():
    with io.open(ORIGINAL, encoding="utf-8", newline="") as f:
        original = f.read()

    got = sha256_text(original)
    if got != PRISTINE_SHA:
        raise SystemExit("ABORT: archive is not the pristine file\n  want %s\n  got  %s"
                         % (PRISTINE_SHA, got))

    patched = original

    for head, where in TARGETS:
        old = head + "\n            " + OLD_BODY
        new = head + "\n            " + NEW_BODY

        if new in patched:
            print("  %-8s already patched" % where)
            continue

        n = patched.count(old)
        if n != 1:
            raise SystemExit("ABORT: %s patch target occurs %d times" % (where, n))

        patched = patched.replace(old, new, 1)
        print("  %-8s patched" % where)

    got = sha256_text(patched)
    if got != E4_PATCHED_SHA:
        raise SystemExit("ABORT: patched file does not match E4's recorded patch\n"
                         "  want %s\n  got  %s" % (E4_PATCHED_SHA, got))
    print("  %-8s E4-verified bytes reproduced  %s" % ("stage 2", got[:16]))

    if LLP64_NEW in patched:
        print("  %-8s already applied" % "llp64")
    else:
        n = patched.count(LLP64_OLD)
        if n != 1:
            raise SystemExit("ABORT: the LLP64 line occurs %d times" % n)
        patched = patched.replace(LLP64_OLD, LLP64_NEW, 1)
        print("  %-8s 1UL -> 1ULL (value-identical on LP64)" % "llp64")

    got = sha256_text(patched)
    if got != WINDOWS_LLP64_SHA:
        raise SystemExit("ABORT: LLP64 edit produced unexpected bytes\n"
                         "  want %s\n  got  %s" % (WINDOWS_LLP64_SHA, got))

    os.makedirs(os.path.dirname(PATCHED), exist_ok=True)
    with io.open(PATCHED, "w", encoding="utf-8", newline="") as f:
        f.write(patched)

    for name in VERBATIM:
        src = os.path.join(HERE, "vendor", "lits_model_original", name)
        dst = os.path.join(HERE, "vendor", "lits_model_safe", name)

        with io.open(src, encoding="utf-8", newline="") as f:
            body = f.read()
        with io.open(dst, "w", encoding="utf-8", newline="") as f:
            f.write(body)

        # Verbatim means the archive's hash and this copy's hash are the same number, so a
        # reader can tell "copied unchanged" from "looks similar".
        print("  %-16s copied verbatim  %s" % (name, sha256_text(body)[:16]))

    os.makedirs(os.path.dirname(PATCHFILE), exist_ok=True)
    diff = difflib.unified_diff(
        original.splitlines(keepends=True),
        patched.splitlines(keepends=True),
        fromfile="a/lits_model.hpp",
        tofile="b/lits_model.hpp",
    )
    with io.open(PATCHFILE, "w", encoding="utf-8", newline="") as f:
        f.writelines(diff)

    print("pristine  %s" % PRISTINE_SHA)
    print("E4 patch  %s" % E4_PATCHED_SHA)
    print("in tree   %s  == E4 patch + LLP64 token (WINDOWS_LLP64_SHA)" % got)
    print("wrote     vendor/lits_model_safe/lits_model.hpp")
    print("wrote     patches/lits_model_safety.patch")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
