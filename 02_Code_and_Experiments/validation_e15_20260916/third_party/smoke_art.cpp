// E12 baseline preparation: correctness smoke test for the pinned armon/libart.
//
// Target: vendor/libart/src/art.c @ commit 301046804af165269e37da6725f5a4aec9ecc881
//         (sha256 b65f20fdc160a6e4c0b1b390086f682d381e3638d8690594158ad75cda33396e)
//
// Purpose: pin down the *adapter* semantics that
// 02_Code_and_Experiments/validation_e4_20260911/native.hpp::NativeArt relies on,
// and expose the cases where those semantics do NOT hold.  This file does not
// benchmark anything; it only asserts correctness of insert/search/delete.
//
// E4 NativeArt contract, replicated below verbatim in spirit:
//   * key_len handed to libart is  key.size() + 1   (the NUL terminator is part
//     of the key, so that a key which is a strict prefix of another key gets its
//     own terminal edge instead of collapsing into the longer key's leaf);
//   * value is stored as  (void*)(uintptr_t)(v + 1)  and read back as
//     (uintptr_t)ptr - 1 , so that a stored 0 is never confused with the NULL
//     that art_search returns for "not found"; UINT64_MAX is therefore reserved;
//   * the caller guarantees ASCII keys with no NUL byte and length >= 1
//     (native.hpp::ascii_key); the raw C layer has no such restriction and is
//     probed separately in section 3.
//
// Build:
//   gcc -std=c99 -O3 -march=x86-64-v2 -c vendor/libart/src/art.c -o art.o
//   g++ -std=c++17 -O3 -march=x86-64-v2 -static smoke_art.cpp art.o -o smoke_art
//
// Add -DE12_SPACE_WRAP and link with -Wl,--wrap=calloc -Wl,--wrap=free to also
// print exact allocator byte accounting for the tree (section 6).

#include "vendor/libart/src/art.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// tiny test harness
// ---------------------------------------------------------------------------
static int g_checks = 0;
static int g_fails = 0;
static int g_known = 0;

static void expect(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_fails;
        std::printf("  FAIL  %s\n", what.c_str());
    }
}

// A violation that is *outside* the contract E4's adapter declares
// (native.hpp::ascii_key allows only NUL-free ASCII).  Counted separately so a
// pass/fail line stays meaningful, but never silently dropped.
static void known(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_known;
        std::printf("  KNOWN %s   <-- outside ascii_key contract\n", what.c_str());
    }
}

static std::string show(const std::string& k) {
    std::string out = "\"";
    for (unsigned char c : k) {
        if (c >= 0x20 && c < 0x7f) out.push_back(static_cast<char>(c));
        else { char buf[8]; std::snprintf(buf, sizeof buf, "\\x%02x", c); out += buf; }
    }
    return out + "\"";
}

// ---------------------------------------------------------------------------
// section 1/2: the E4 convention (key_len = size + 1, value + 1)
// ---------------------------------------------------------------------------
static void* art_put(art_tree* t, const std::string& k, std::uint64_t v) {
    return art_insert(t, reinterpret_cast<const unsigned char*>(k.c_str()),
                      static_cast<int>(k.size()) + 1,
                      reinterpret_cast<void*>(static_cast<std::uintptr_t>(v + 1)));
}

static bool art_get(art_tree* t, const std::string& k, std::uint64_t* out) {
    void* p = art_search(t, reinterpret_cast<const unsigned char*>(k.c_str()),
                         static_cast<int>(k.size()) + 1);
    if (!p) return false;
    *out = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p) - 1);
    return true;
}

static void check_suite(const char* title, const std::vector<std::string>& keys,
                        const std::vector<std::string>& probes) {
    std::printf("[%s]\n", title);
    art_tree t;
    art_tree_init(&t);
    std::map<std::string, std::uint64_t> ref;
    std::uint64_t v = 1000;
    for (const auto& k : keys) { art_put(&t, k, v); ref[k] = v; v += 7; }

    for (const auto& k : probes) {
        std::uint64_t got = 0;
        bool found = art_get(&t, k, &got);
        auto it = ref.find(k);
        bool want = it != ref.end();
        expect(found == want, "presence " + show(k) +
                                  " got=" + (found ? "yes" : "no") +
                                  " want=" + (want ? "yes" : "no"));
        if (found && want)
            expect(got == it->second, "value " + show(k));
    }
    // A search must never hand back a value that was never inserted.
    for (int i = 0; i < 64; ++i) {
        std::string miss = "zzz" + std::to_string(i);
        std::uint64_t got = 0;
        expect(!art_get(&t, miss, &got), "absent probe " + show(miss));
    }
    art_tree_destroy(&t);
    std::printf("  keys=%zu probes=%zu\n", keys.size(), probes.size());
}

// The other convention, kept as a contrast: passing key.size() WITHOUT the
// terminator.  This still resolves correctly for ASCII keys in this build --
// the leaf-split edge label falls on offsetof(art_leaf,key)+key_len, which is
// inside the allocation because sizeof(art_leaf)==16 > offsetof(key)==12, and
// calloc zeroed it.  That is exactly the "works by luck" behaviour the +1 in
// native.hpp replaces with an explicit terminal edge.  Checked for exact
// results here; a PASS does NOT mean the terminator is unnecessary.
static void check_no_terminator_trap() {
    std::printf("[2. contrast: key_len == size (the convention the +1 replaces)]\n");
    art_tree t;
    art_tree_init(&t);
    const std::vector<std::string> keys = {"a", "aa", "aaa", "ab"};
    std::uint64_t v = 10;
    for (const auto& k : keys) {
        art_insert(&t, reinterpret_cast<const unsigned char*>(k.c_str()),
                   static_cast<int>(k.size()),
                   reinterpret_cast<void*>(static_cast<std::uintptr_t>(v + 1)));
        v += 10;
    }
    for (const auto& k : keys) {
        void* p = art_search(&t, reinterpret_cast<const unsigned char*>(k.c_str()),
                             static_cast<int>(k.size()));
        std::uint64_t got = p ? static_cast<std::uint64_t>(
                                    reinterpret_cast<std::uintptr_t>(p) - 1)
                              : 0;
        bool ok = p && got == 10u * (std::uint64_t)(&k - &keys[0] + 1);
        std::printf("  search %-6s -> %s (value=%llu, expected=%llu)\n",
                    show(k).c_str(), p ? "found" : "MISS",
                    (unsigned long long)got,
                    (unsigned long long)(10u * (std::uint64_t)(&k - &keys[0] + 1)));
        expect(ok, "no-terminator key " + show(k));
    }
    std::printf("  tree size=%llu\n", (unsigned long long)art_size(&t));
    art_tree_destroy(&t);
}

// ---------------------------------------------------------------------------
// section 3: raw C layer beyond the E4 ascii_key guard
// ---------------------------------------------------------------------------
// Reports the raw lookup in full, so that a failure distinguishes "miss" from
// "found the wrong value" (silent corruption is the dangerous case).
static void probe(const char* label, art_tree* t, const std::string& k,
                  std::uint64_t want, bool in_contract) {
    std::uint64_t got = 0;
    bool found = art_get(t, k, &got);
    bool ok = found && got == want;
    std::string what = std::string(label) + " " + show(k) + " -> " +
                       (found ? "value=" + std::to_string(got) : "MISS") +
                       " want=" + std::to_string(want);
    if (in_contract) expect(ok, what); else known(ok, what);
    if (!ok)
        std::printf("        %s\n", what.c_str());
}

static void check_raw_edge_cases() {
    std::printf("[3. raw libart edge cases (beyond the E4 ascii_key guard)]\n");

    // (a) high bytes (0x80..0xff) and DEL, but NO NUL byte anywhere.
    //     Still inside the +1 contract: a key+terminator can only be a strict
    //     prefix of another key+terminator if the longer key holds a NUL.
    {
        art_tree t;
        art_tree_init(&t);
        const std::vector<std::string> keys = {
            std::string("\x80", 1), std::string("\xff", 1),
            std::string("\xff\xff", 2), std::string("\xc3\xa9", 2),
            std::string("\x80\x81", 2), std::string("\x7f", 1)};
        for (std::size_t i = 0; i < keys.size(); ++i)
            art_put(&t, keys[i], 500 + i);
        for (std::size_t i = 0; i < keys.size(); ++i)
            probe("high-byte, NUL-free", &t, keys[i], 500 + i, true);
        expect(art_size(&t) == keys.size(), "high-byte tree size");
        art_tree_destroy(&t);
        std::printf("  (a) high bytes without NUL: in contract\n");
    }

    // (b) embedded NUL bytes.  The +1 convention puts the terminator exactly
    //     where the longer key has its embedded NUL, so the short key becomes a
    //     strict prefix of the long one and libart descends past the end of the
    //     short key (find_child(n, key[depth]) with depth == key_len).
    {
        art_tree t;
        art_tree_init(&t);
        const std::vector<std::string> keys = {
            std::string("a\0b", 3), std::string("a\0c", 3), std::string("a\0", 2),
            std::string("\0\0", 2), std::string("a", 1)};
        for (std::size_t i = 0; i < keys.size(); ++i)
            art_put(&t, keys[i], 1 + i);
        for (std::size_t i = 0; i < keys.size(); ++i)
            probe("embedded-NUL", &t, keys[i], 1 + i, false);
        art_tree_destroy(&t);
        std::printf("  (b) embedded NUL: out of contract, read past key_len\n");
    }

    // (c) the empty key.  NUL-free, but ascii_key rejects zero-length keys.
    {
        art_tree t;
        art_tree_init(&t);
        std::uint64_t got = 0;
        expect(!art_get(&t, std::string(), &got), "empty lookup on empty tree");
        art_put(&t, std::string(), 42);
        expect(art_get(&t, std::string(), &got) && got == 42,
               "empty key round-trip via raw C layer");
        expect(art_size(&t) == 1, "empty-key tree size");
        art_tree_destroy(&t);
        std::printf("  (c) empty key: raw C layer OK, rejected by ascii_key\n");
    }
}

// ---------------------------------------------------------------------------
// section 4: adapter replica vs std::map, mirroring e4/check_native.cpp
// ---------------------------------------------------------------------------
namespace adapter {

// faithful copy of validation_e4_20260911/native.hpp::ascii_key (lines 8-11)
inline void ascii_key(const std::string& k) {
    if (k.empty()) throw std::invalid_argument("empty native key");
    for (unsigned char c : k)
        if (c == 0 || c > 127)
            throw std::invalid_argument("native protocol requires ASCII without NUL");
}

class NativeArt {  // native.hpp lines 27-38
    art_tree tree_{};

public:
    explicit NativeArt() { art_tree_init(&tree_); }
    NativeArt(const NativeArt&) = delete;
    ~NativeArt() { art_tree_destroy(&tree_); }

    bool find(const std::string& key, std::uint64_t* out) {
        void* p = art_search(&tree_,
                             reinterpret_cast<const unsigned char*>(key.c_str()),
                             static_cast<int>(key.size()) + 1);
        if (!p) return false;
        *out = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p) - 1);
        return true;
    }
    void upsert(const std::string& key, std::uint64_t v) {
        if (v == UINT64_MAX) throw std::invalid_argument("ART adapter reserved value");
        art_insert(&tree_, reinterpret_cast<const unsigned char*>(key.c_str()),
                   static_cast<int>(key.size()) + 1,
                   reinterpret_cast<void*>(static_cast<std::uintptr_t>(v + 1)));
    }
    bool erase(const std::string& key) {
        return art_delete(&tree_,
                          reinterpret_cast<const unsigned char*>(key.c_str()),
                          static_cast<int>(key.size()) + 1) != nullptr;
    }
};

}  // namespace adapter

static void check_guard() {
    std::printf("[3d. ascii_key guard is load-bearing, not merely defensive]\n");
    const std::vector<std::pair<std::string, const char*>> bad = {
        {std::string(), "empty"},
        {std::string("a\0b", 3), "embedded NUL"},
        {std::string("\x80", 1), "byte 0x80 (non-ASCII)"},
        {std::string("\xff", 1), "byte 0xff (non-ASCII)"},
    };
    for (const auto& kv : bad) {
        bool threw = false;
        try { adapter::ascii_key(kv.first); } catch (const std::exception&) { threw = true; }
        expect(threw, std::string("ascii_key rejects ") + kv.second);
    }
    for (const char* ok : {"a", "a.com", "host-1.example.com"}) {
        bool threw = false;
        try { adapter::ascii_key(ok); } catch (const std::exception&) { threw = true; }
        expect(!threw, std::string("ascii_key accepts ") + ok);
    }
}

static void check_random_ops() {
    std::printf("[4. randomized upsert/erase/find vs std::map]\n");
    adapter::NativeArt art;
    std::map<std::string, std::uint64_t> ref;
    std::mt19937_64 rng(2026091601ull);

    std::vector<std::string> pool;
    for (int i = 0; i < 8192; ++i) {
        std::string k = "k" + std::to_string(i);
        pool.push_back(k);
        std::uint64_t v = rng() % 1000000;
        art.upsert(k, v);
        ref[k] = v;
    }
    // prefix-shaped keys, the shape the +1 convention exists for
    for (const char* k : {"a", "aa", "aaa", "aab", "ab", "b", "ba", "z"}) {
        std::uint64_t v = rng() % 1000000;
        art.upsert(k, v);
        ref[k] = v;
    }

    for (int t = 0; t < 20000; ++t) {
        const std::string& k = pool[rng() % pool.size()];
        unsigned op = rng() % 3;
        if (op == 0) {
            std::uint64_t v = rng() % 1000000;
            art.upsert(k, v);
            ref[k] = v;
        } else if (op == 1) {
            bool a = art.erase(k);
            bool b = ref.erase(k) > 0;
            expect(a == b, "erase agreement " + show(k));
        }
        std::uint64_t got = 0;
        bool found = art.find(k, &got);
        auto it = ref.find(k);
        expect(found == (it != ref.end()), "random presence " + show(k));
        if (found && it != ref.end())
            expect(got == it->second, "random value " + show(k));
    }
    for (const auto& kv : ref) {
        std::uint64_t got = 0;
        expect(art.find(kv.first, &got) && got == kv.second,
               "final sweep " + show(kv.first));
    }
    std::printf("  remaining=%zu\n", ref.size());
}

// ---------------------------------------------------------------------------
// section 5: value encoding boundary
// ---------------------------------------------------------------------------
static void check_value_encoding() {
    std::printf("[5. value encoding (v+1 / NULL-is-absent)\n");
    adapter::NativeArt art;
    art.upsert("zero", 0);
    art.upsert("one", 1);
    art.upsert("max_minus_1", UINT64_MAX - 1);
    std::uint64_t got = 0;
    expect(art.find("zero", &got) && got == 0, "v=0 survives the +1 trick");
    expect(art.find("one", &got) && got == 1, "v=1 round-trip");
    expect(art.find("max_minus_1", &got) && got == UINT64_MAX - 1,
           "v=UINT64_MAX-1 round-trip");
    bool threw = false;
    try { art.upsert("reserved", UINT64_MAX); } catch (const std::exception&) { threw = true; }
    expect(threw, "v=UINT64_MAX is rejected");
    // duplicate upsert must overwrite, not append
    art.upsert("zero", 99);
    expect(art.find("zero", &got) && got == 99, "upsert overwrites");
}

// ---------------------------------------------------------------------------
// section 6 (optional): exact allocator accounting
// ---------------------------------------------------------------------------
#ifdef E12_SPACE_WRAP
// libart touches only calloc/free (art.c: alloc_node -> calloc, make_leaf ->
// calloc(sizeof(art_leaf)+key_len), destroy -> free).  Wrapping those two entry
// points therefore yields the exact number of bytes libart holds.
#include <cstdlib>

extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void __real_free(void*);

namespace {
constexpr std::size_t TBITS = 20;
constexpr std::size_t TSIZE = 1u << TBITS;
struct Slot { void* p; std::size_t n; };
Slot g_tab[TSIZE];
bool g_record = false;
std::size_t g_live = 0, g_peak = 0, g_allocs = 0;
std::size_t g_leaf_bytes = 0, g_node_bytes = 0;

// art.c allocates nodes as calloc(1, sizeof(art_nodeN)) and leaves as
// calloc(1, sizeof(art_leaf) + key_len), so the request size identifies which.
bool is_node_size(std::size_t n) {
    return n == sizeof(art_node4) || n == sizeof(art_node16) ||
           n == sizeof(art_node48) || n == sizeof(art_node256);
}

std::size_t hashp(void* p) { return (reinterpret_cast<std::uintptr_t>(p) >> 4) * 2654435761u & (TSIZE - 1); }
void tab_put(void* p, std::size_t n) {
    std::size_t i = hashp(p);
    for (std::size_t s = 0; s < TSIZE; ++s) {
        Slot& sl = g_tab[(i + s) & (TSIZE - 1)];
        if (!sl.p) { sl.p = p; sl.n = n; return; }
    }
}
bool tab_take(void* p, std::size_t* n) {
    std::size_t i = hashp(p);
    for (std::size_t s = 0; s < TSIZE; ++s) {
        Slot& sl = g_tab[(i + s) & (TSIZE - 1)];
        if (!sl.p) return false;
        if (sl.p == p) { *n = sl.n; sl.p = nullptr; return true; }
    }
    return false;
}
}  // namespace

extern "C" void* __wrap_calloc(std::size_t nmemb, std::size_t sz) {
    void* p = __real_calloc(nmemb, sz);
    if (g_record && p) {
        std::size_t n = nmemb * sz;
        tab_put(p, n);
        g_live += n;
        ++g_allocs;
        if (is_node_size(n)) g_node_bytes += n; else g_leaf_bytes += n;
        if (g_live > g_peak) g_peak = g_live;
    }
    return p;
}

extern "C" void __wrap_free(void* p) {
    if (g_record && p) {
        std::size_t n = 0;
        if (tab_take(p, &n)) g_live -= n;
    }
    __real_free(p);
}
#endif

int main() {
    std::printf("libart smoke test @ 301046804af165269e37da6725f5a4aec9ecc881\n");
    std::printf("sizeof(art_tree)=%zu sizeof(art_node)=%zu sizeof(art_leaf)=%zu "
                "sizeof(void*)=%zu\n",
                sizeof(art_tree), sizeof(art_node), sizeof(art_leaf), sizeof(void*));

    // ASCII suite that the E4 protocol actually feeds in
    check_suite("1a. E4 ASCII suite (Kai-style keys)",
                {"", "a", "aa", "aaa", "aab", "ab", "b", "ba", "z", "az", "azz"},
                {"", "a", "aa", "aaa", "aab", "ab", "b", "ba", "z", "az", "azz",
                 "aaaa", "c", "aaab", "b0", "zzz"});
    check_suite("1b. E4 ASCII suite (Tranco-style hostnames)",
                {"a.com", "aa.com", "aaa.com", "ab.com", "b.com", "ba.com"},
                {"a.com", "aa.com", "aaa.com", "ab.com", "b.com", "ba.com",
                 "a.co", "com", "aaa.co", ".com", "x.com"});

    check_no_terminator_trap();
    check_raw_edge_cases();
    check_guard();
    check_random_ops();
    check_value_encoding();

#ifdef E12_SPACE_WRAP
    std::printf("[6. allocator accounting (--wrap=calloc/free)]\n");
    {
        // Representative Tranco-shaped load.  The point of this section is that
        // libart's footprint is *measurable* (it only ever calls calloc/free),
        // so a comparison never has to record ART memory as 0 / "unknown".
        std::vector<std::string> keys;
        for (int i = 0; i < 100000; ++i)
            keys.push_back("host" + std::to_string(i) + ".example.com");

        g_record = true;
        adapter::NativeArt art;
        for (std::size_t i = 0; i < keys.size(); ++i) art.upsert(keys[i], i);
        std::size_t final_live = g_live, final_allocs = g_allocs;
        std::size_t peak = g_peak;
        std::size_t leaf_bytes = g_leaf_bytes, node_bytes = g_node_bytes;
        g_record = false;

        std::printf("  n=%zu calloc_calls=%zu\n", keys.size(), final_allocs);
        std::printf("  final_live_bytes=%zu  bytes_per_key=%.2f\n", final_live,
                    double(final_live) / double(keys.size()));
        std::printf("  peak_live_bytes =%zu  (peak_bytes/key=%.2f)\n", peak,
                    double(peak) / double(keys.size()));
        std::printf("  breakdown: leaf=%zu (%.2f/key)  node=%zu (%.2f/key)\n",
                    leaf_bytes, double(leaf_bytes) / double(keys.size()),
                    node_bytes, double(node_bytes) / double(keys.size()));
        std::printf("  note: leaf bytes include the stored key bytes; measured on a\n"
                    "        synthetic Tranco-shaped set, re-measure per real dataset\n");
    }
#else
    std::printf("[6. allocator accounting skipped (rebuild with -DE12_SPACE_WRAP "
                "-Wl,--wrap=calloc -Wl,--wrap=free)\n");
#endif

    std::printf("\nchecks=%d failures=%d outside_contract=%d -> %s\n", g_checks,
                g_fails, g_known, g_fails == 0 ? "PASS" : "FAIL");
    if (g_known)
        std::printf("(%d violations were outside the ascii_key contract; see section 3b)\n",
                    g_known);
    return g_fails == 0 ? 0 : 1;
}
