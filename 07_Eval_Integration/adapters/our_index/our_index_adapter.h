#pragma once

// string-index/v1 adapter for this project's string learned index.
//
// The factory symbol is referenced by the adapter manifest, and the header is the one the
// evaluator's generated runner includes. Everything else lives in the .cpp next to it.
//
// TWO PACKAGES SHARE THIS IMPLEMENTATION, and the split is deliberate: the evaluator's
// runner passes an EMPTY parameter list today (docs/UNIFIED_BENCHMARK.md: "适配器工厂目前
// 接收空参数列表"), so the model backend has to be fixed at COMPILE time. Rather than let a
// missing parameter silently select a backend, the backend is a compile definition and the
// two packages differ in exactly that. A backend that is merely absent is how a comparison
// ends up measuring the wrong index.

#include "index_contract.h"

#include <memory>

std::unique_ptr<string_index_bench::v1::Index> make_our_index_index(
    const string_index_bench::v1::Parameters& parameters);
