#pragma once
// E23 ART adapter: native_e22.hpp plus a read-only view of the raw tree, which the
// range-scan DIAGNOSTIC needs because libart has no seek-to-key iterator. This is
// not a scan capability being added to ART -- it is what a full-traversal adapter
// has to reach for, and the diagnostic says so.
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
namespace e23mem {
extern bool on;
extern long long live;
extern long long peak;
}  // namespace e23mem
#endif

namespace e23art {

using namespace validation;

inline void ascii_key(const std::string& k) {
    if (k.empty()) throw std::invalid_argument("empty native key");
    for (unsigned char c : k)
        if (c == 0 || c > 127)
            throw std::invalid_argument("native protocol requires ASCII without NUL");
}

class NativeArt {
    // NOTE: art_delete is only valid on a key that is present; the E22 driver only
    // ever calls it for keys the reference says are live.
    art_tree tree_{};

public:
    std::size_t bytes = 0;   // measured only in the -DE20_ART_MEM build

    explicit NativeArt(const std::vector<Pair>& data) {
#ifdef E20_ART_MEM
        e23mem::on = true;
        const long long base = e23mem::live;
        e23mem::peak = e23mem::live;
#endif
        art_tree_init(&tree_);
        for (const auto& kv : data) {
            ascii_key(kv.first);
            upsert(kv.first, kv.second);
        }
#ifdef E20_ART_MEM
        bytes = sizeof(tree_) + std::size_t(e23mem::peak - base);
        e23mem::on = false;
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

    // Added for E22. Returns true if the key was present and removed.
    bool erase(const std::string& key) {
        return art_delete(&tree_, reinterpret_cast<const unsigned char*>(key.c_str()),
                          static_cast<int>(key.size()) + 1) != nullptr;
    }

    // DIAGNOSTIC ONLY. libart exposes whole-tree traversal (`art_iter`) and prefix
    // iteration, and nothing that seeks to an arbitrary key, so any range scan built
    // on it has to walk the tree and stop early. That cost is the adapter's, not a
    // statement about ART's range capability.
    art_tree& tree() { return tree_; }

    void upsert(const std::string& key, std::uint64_t v) {
        if (v == UINT64_MAX) throw std::invalid_argument("ART adapter reserved value");
        art_insert(&tree_, reinterpret_cast<const unsigned char*>(key.c_str()),
                   static_cast<int>(key.size()) + 1,
                   reinterpret_cast<void*>(static_cast<std::uintptr_t>(v + 1)));
    }
};

}  // namespace e23art
