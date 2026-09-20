// Focused UB probe for the pinned armon/libart (E12 baseline preparation).
//
// Why this exists: validation_e4_20260911/native.hpp::NativeArt appends the NUL
// terminator to every key (key_len = size + 1) and *also* rejects keys that
// contain a NUL byte (native.hpp::ascii_key).  Those two look like belt and
// braces, but only the second one is what keeps this library in bounds:
//
//   * The "+1" is a *semantic* fix, not a memory-safety one.  With key_len ==
//     size libart takes the leaf-split path in recursive_insert and labels the
//     prefix key's edge with l->key[key_len] -- a byte past the copied key, but
//     still inside the leaf allocation, because offsetof(art_leaf, key) == 12
//     while sizeof(art_leaf) == 16 leaves 4 bytes of struct-padding slack, and
//     calloc zeroed them.  It happens to work for ASCII keys handed in as
//     c_str(); the "+1" makes the edge explicit and deterministic instead of
//     depending on zeroed padding.
//
//   * The embedded-NUL guard is load-bearing.  With key_len = size + 1, a key
//     K+'\0' is a strict prefix of K'+'\0' exactly when K' has a NUL at offset
//     |K|.  libart then walks past the end of the short key: art_search does
//     find_child(n, key[depth]) at art.c:286 with depth == key_len, reading one
//     byte past the caller's buffer.  AddressSanitizer confirms it below.
//
// Probe: heap-allocated pair (so the overrun lands outside the std::string SSO
// buffer and ASan can see it).
//   LONG  = "0123456789abcdefghij\0XYZ"   (24 bytes)
//   SHORT = "0123456789abcdefghij"        (20 bytes)
// SHORT + '\0' is a strict prefix of LONG + '\0' because LONG[20] == '\0'.
//
// Build (ASan):
//   gcc -std=c99 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
//       -c vendor/libart/src/art.c -o build/art_asan.o
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
//       probe_nul_prefix.cpp build/art_asan.o -o build/probe_nul_prefix
//   ASAN_OPTIONS=detect_leaks=0 ./build/probe_nul_prefix   # exits non-zero on report
#include "vendor/libart/src/art.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

static const std::string LONG_K =
    std::string("0123456789abcdefghij", 20) + std::string("\0", 1) + std::string("XYZ", 3);
static const std::string SHORT_K = std::string("0123456789abcdefghij", 20);

static void* put(art_tree* t, const std::string& k, std::uint64_t v) {
    return art_insert(t, reinterpret_cast<const unsigned char*>(k.c_str()),
                      static_cast<int>(k.size()) + 1,
                      reinterpret_cast<void*>(static_cast<std::uintptr_t>(v)));
}

int main() {
    std::printf("pinned libart @ 301046804af165269e37da6725f5a4aec9ecc881\n");
    std::printf("offsetof(art_leaf,key)=%zu sizeof(art_leaf)=%zu\n",
                offsetof(art_leaf, key), sizeof(art_leaf));
    std::printf("LONG.size=%zu SHORT.size=%zu\n", LONG_K.size(), SHORT_K.size());

    art_tree t;
    art_tree_init(&t);
    put(&t, LONG_K, 11);
    put(&t, SHORT_K, 22);

    // SHORT_K + '\0' is a strict prefix of LONG_K + '\0', so this walks to
    // depth == key_len and reads key[key_len], one byte past the key buffer.
    void* b = art_search(&t, reinterpret_cast<const unsigned char*>(SHORT_K.c_str()),
                         static_cast<int>(SHORT_K.size()) + 1);
    std::printf("search SHORT -> %s (want 22)\n", b ? "FOUND" : "MISS");

    art_tree_destroy(&t);
    std::printf("done\n");
    return 0;
}
