#pragma once

// string-index/v1 adapter for HOT (Height Optimized Trie).
//
// The library identity is pinned in the adapter manifest: speedskater/hot at a specific
// commit. HOT is not interchangeable with "a trie" -- its node widths, its SIMD searches and
// its update path are what this adapter measures, and its AVX2/BMI2 requirement is a hard
// constraint on how the whole round is compiled.

#include "index_contract.h"

#include <memory>

std::unique_ptr<string_index_bench::v1::Index> make_hot_index(
    const string_index_bench::v1::Parameters& parameters);
