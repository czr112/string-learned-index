// Reuse the frozen, correctly grouped query harness with an explicit candidate seam.
// Load the old header first so the function alias cannot redefine its factory.
#include "bm_split_adapter.hpp"
#include "bm_experiment_adapter.hpp"
#include "bm_tx_adapter.hpp"
#ifdef BM_TX_CONTROL_BM1
static std::unique_ptr<string_index_bench::v1::Index> tx_query_control(
        const string_index_bench::v1::Parameters& parameters) {
    return make_bm_split_experiment_index(parameters);
}
#define make_bm_experiment_index tx_query_control
#undef BM_EXPERIMENT_ADAPTER_SHA256
#define BM_EXPERIMENT_ADAPTER_SHA256 BM_QUERY_BM1_ADAPTER_SHA256
#endif
#define make_bm_split_experiment_index make_bm_tx_experiment_index
#undef BM_SPLIT_ADAPTER_SHA256
#define BM_SPLIT_ADAPTER_SHA256 BM_TX_ADAPTER_SHA256
#include "bm_query_recheck.cpp"
