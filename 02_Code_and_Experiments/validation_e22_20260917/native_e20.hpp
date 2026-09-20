#pragma once
// E20 ART adapter.
//
// Semantically identical to validation_e4_20260911 / E12's native.hpp: the
// ascii_key guard, the terminator included in key_len, and the value+1 encoding
// with UINT64_MAX reserved. The ascii_key guard is LOAD-BEARING, not defensive --
// with key_len = size+1 a prefix key lets libart step past the key end and read one
// byte out of bounds, which E12 reproduced under ASan. Every dataset here is
// NUL-free ASCII, so the guard is satisfied; that limit still has to be stated.
//
// The ONE change: the allocator accounting is compiled out unless -DE20_ART_MEM.
//
// native.hpp turns accounting on for the whole of NativeArt's constructor, which is
// exactly the region being timed. E16 established that wrapping the allocator
// inside a timed region inflates whichever path allocates most, and here that path
// is ART itself. So the timing build carries no wrappers at all, and the footprint
// comes from a separate build. native.hpp stays byte-identical and unused.

#include "data.hpp"
#include "third_party/vendor/libart/src/art.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// The wrappers themselves live in the benchmark, next to the prototype's own
// accounting -- one place owns them, so there is no duplicate __wrap_free. This
// header only opens and closes the accounting window around ART's construction.
#ifdef E20_ART_MEM
namespace e20mem {
extern bool on;
extern long long live;
extern long long peak;
}  // namespace e20mem
#endif

namespace e20art {

using namespace validation;

inline void ascii_key(const std::string& k) {
    if (k.empty()) throw std::invalid_argument("empty native key");
    for (unsigned char c : k)
        if (c == 0 || c > 127)
            throw std::invalid_argument("native protocol requires ASCII without NUL");
}

class NativeArt {
    art_tree tree_{};

public:
    std::size_t bytes = 0;   // measured only in the -DE20_ART_MEM build

    explicit NativeArt(const std::vector<Pair>& data) {
#ifdef E20_ART_MEM
        e20mem::on = true;
        const long long base = e20mem::live;
        e20mem::peak = e20mem::live;
#endif
        art_tree_init(&tree_);
        for (const auto& kv : data) {
            ascii_key(kv.first);
            upsert(kv.first, kv.second);
        }
#ifdef E20_ART_MEM
        bytes = sizeof(tree_) + std::size_t(e20mem::peak - base);
        e20mem::on = false;
#endif
    }

    NativeArt(const NativeArt&) = delete;
    NativeArt& operator=(const NativeArt&) = delete;
    ~NativeArt() { art_tree_destroy(&tree_); }

    // Include the terminator to give prefix keys an explicit terminal edge.
    // Values + 1 are opaque, never dereferenced; UINT64_MAX is excluded.
    Result find(const std::string& key) const {
        auto* p = art_search(&tree_, reinterpret_cast<const unsigned char*>(key.c_str()),
                             static_cast<int>(key.size()) + 1);
        return p ? Result{true, reinterpret_cast<std::uintptr_t>(p) - 1} : Result{};
    }

    void upsert(const std::string& key, std::uint64_t v) {
        if (v == UINT64_MAX) throw std::invalid_argument("ART adapter reserved value");
        art_insert(&tree_, reinterpret_cast<const unsigned char*>(key.c_str()),
                   static_cast<int>(key.size()) + 1,
                   reinterpret_cast<void*>(static_cast<std::uintptr_t>(v + 1)));
    }
};

}  // namespace e20art
