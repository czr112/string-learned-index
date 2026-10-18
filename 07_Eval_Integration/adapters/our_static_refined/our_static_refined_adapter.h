#pragma once
#include "index_contract.h"
std::unique_ptr<string_index_bench::v1::Index> make_our_static_refined_index(
    const string_index_bench::v1::Parameters& parameters);
