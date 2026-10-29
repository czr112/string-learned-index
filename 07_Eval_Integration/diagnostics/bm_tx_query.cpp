// Reuse the frozen, correctly grouped query harness with an explicit candidate seam.
// Load the old header first so the function alias cannot redefine its factory.
#include "bm_split_adapter.hpp"
#include "bm_tx_adapter.hpp"
#define make_bm_split_experiment_index make_bm_tx_experiment_index
#undef BM_SPLIT_ADAPTER_SHA256
#define BM_SPLIT_ADAPTER_SHA256 BM_TX_ADAPTER_SHA256
#include "bm_query_recheck.cpp"
