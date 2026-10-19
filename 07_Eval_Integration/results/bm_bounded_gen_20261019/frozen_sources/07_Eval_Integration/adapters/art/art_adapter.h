#pragma once

// string-index/v1 adapter for libart (Adaptive Radix Tree).
//
// The library identity is pinned in the adapter manifest: armon/libart at a specific commit,
// with per-file digests. "ART" is not one implementation and the report must not treat it as
// one -- libart's node layout, its allocation behaviour and its update path are what this
// adapter measures.

#include "index_contract.h"

#include <memory>

std::unique_ptr<string_index_bench::v1::Index> make_art_index(
    const string_index_bench::v1::Parameters& parameters);
