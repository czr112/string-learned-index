#pragma once
// E12 native baselines. NativeArt is copied from validation_e4_20260911/native.hpp
// and kept semantically identical: the ascii_key guard, the terminator included in
// key_len, and the value+1 encoding with UINT64_MAX reserved. LITS is dropped --
// E4 excluded it (sanitizer gate still failing) and this round does not re-open it.
//
// The ascii_key guard is LOAD-BEARING, not defensive. With key_len = size+1 a prefix
// key lets libart step past the key end and read one byte out of bounds; E12's
// preparation reproduced that heap overflow under ASan. Every dataset here is
// NUL-free ASCII, so the adapter is correct for these inputs -- and the protocol
// limit has to be stated, because ART is therefore only being compared on an ASCII
// subset while the prototype handles arbitrary bytes.

#include "data.hpp"
#include "third_party/vendor/libart/src/art.h"

#include <malloc.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace e12 {

using namespace validation;

inline void ascii_key(const std::string& k) {
    if (k.empty()) throw std::invalid_argument("empty native key");
    for (unsigned char c : k)
        if (c == 0 || c > 127)
            throw std::invalid_argument("native protocol requires ASCII without NUL");
}

// ---------------------------------------------------------------------------
// Exact space accounting for libart. libart allocates only through calloc and
// releases only through free, so wrapping those two is enough for a true byte
// count. malloc_usable_size is used on BOTH sides so the counter cannot drift:
// it counts the same rounded block that the allocator actually handed out, which
// is the right analogue to the prototype's capacity-based accounting.
// ---------------------------------------------------------------------------

extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void __real_free(void*);

namespace detail {
inline bool accounting = false;
inline std::size_t live = 0;
inline std::size_t peak = 0;
}

extern "C" void* __wrap_calloc(std::size_t n, std::size_t m) {
    void* p = __real_calloc(n, m);
    if (detail::accounting && p) {
        detail::live += malloc_usable_size(p);
        if (detail::live > detail::peak) detail::peak = detail::live;
    }
    return p;
}

extern "C" void __wrap_free(void* p) {
    if (detail::accounting && p) detail::live -= malloc_usable_size(p);
    __real_free(p);
}

// ---------------------------------------------------------------------------

class NativeArt {
    art_tree tree_{};

public:
    std::size_t bytes = 0;   // measured, not estimated

    explicit NativeArt(const std::vector<Pair>& data) {
        detail::accounting = true;
        detail::live = 0;
        detail::peak = 0;

        art_tree_init(&tree_);
        for (const auto& kv : data) {
            ascii_key(kv.first);
            upsert(kv.first, kv.second);
        }

        bytes = sizeof(tree_) + detail::peak;
        detail::accounting = false;
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

    bool erase(const std::string& key) {
        return art_delete(&tree_, reinterpret_cast<const unsigned char*>(key.c_str()),
                          static_cast<int>(key.size()) + 1) != nullptr;
    }
};

}  // namespace e12
