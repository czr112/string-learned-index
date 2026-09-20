#!/usr/bin/env bash
# E12 baseline prep: fetch + verify + build + smoke-test the pinned armon/libart.
#
# Verified on WSL2 Ubuntu 24.04, g++/gcc 13.3.0, x86-64.
# Pinned commit 301046804af165269e37da6725f5a4aec9ecc881, matching the commit
# recorded in validation_e4_20260911/sources.json.
#
#   bash build_and_check.sh
#
# Nothing outside this directory is read or written.
set -euo pipefail
cd "$(dirname "$0")"

CC=${CC:-gcc}
CXX=${CXX:-g++}
CFLAGS_COMMON="-O3 -march=x86-64-v2"
CXXFLAGS_COMMON="-std=c++17 -O3 -march=x86-64-v2"

echo "== 1. fetch + SHA-256 verify pinned libart =="
python3 fetch_libart.py
sha256sum -c <<'SUMS'
b65f20fdc160a6e4c0b1b390086f682d381e3638d8690594158ad75cda33396e  vendor/libart/src/art.c
4443de6fd42581982167f7826f63bd956d21a958f204575e52512e007deab5af  vendor/libart/src/art.h
SUMS

mkdir -p build

echo
echo "== 2. compile libart (C99, exact E4 recipe) =="
# libart is C99. Compile it with gcc, exactly as validation_e4_20260911/reproduce.py
# line 9 does, then link the C++17 side against the object. g++ also accepts
# art.c directly (-x c++), but the C99 object is the E4-consistent recipe.
$CC -std=c99 $CFLAGS_COMMON -Wall -Wextra -c vendor/libart/src/art.c -o build/art.o

echo
echo "== 3. build + run the release smoke test =="
$CXX $CXXFLAGS_COMMON -Wall -Wextra -static smoke_art.cpp build/art.o -o build/smoke_art
./build/smoke_art

echo
echo "== 4. build + run the exact-space-accounting variant =="
# libart allocates only through calloc/free, so wrapping those two symbols yields
# its exact footprint.  This is how ART memory stops being an unknown.
$CXX $CXXFLAGS_COMMON -Wall -Wextra -DE12_SPACE_WRAP -static \
    smoke_art.cpp build/art.o -Wl,--wrap=calloc -Wl,--wrap=free \
    -o build/smoke_art_space
./build/smoke_art_space

echo
echo "== 5. ASan/UBSan probe: NUL-containing key read past key_len =="
echo "   (documents why native.hpp::ascii_key forbids NUL bytes)"
$CC -std=c99 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -c vendor/libart/src/art.c -o build/art_asan.o
$CXX -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    probe_nul_prefix.cpp build/art_asan.o -o build/probe_nul_prefix
set +e
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ./build/probe_nul_prefix
rc=$?
set -e
echo "probe_nul_prefix exit=$rc  (non-zero == the overrun was detected, as expected)"

echo
echo "== 6. ASan smoke test =="
$CXX -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    smoke_art.cpp build/art_asan.o -o build/smoke_art_asan
ASAN_OPTIONS=detect_leaks=0 ./build/smoke_art_asan | tail -5

echo
echo "All steps completed."
