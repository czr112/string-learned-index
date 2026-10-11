// string-index/v1 adapter for this project's string learned index.
//
// WHAT THIS FILE IS AND IS NOT. It is a thin translation layer: every operation forwards to
// the implementation, and the only work it does itself is (a) converting between the
// contract's Row and the implementation's Pair, (b) validating inputs the contract requires
// it to validate, and (c) declaring what the implementation can and cannot do. It performs
// no key encoding, no auxiliary search and no duplicate suppression of its own -- if any of
// those were needed, the comparison would be measuring the adapter rather than the index.
//
// REWRITTEN 2026-10-11 AFTER AN ACCIDENTAL OVERWRITE. The 2026-10-09 revision of this file
// was destroyed by a botched scripted edit during the M3 integration round (a PowerShell
// loop read the file, failed to read the three SIBLING packages' copies -- which never
// existed, because all four packages build THIS file -- and then wrote the stale buffer
// over it, finally leaving it zero-filled). No copy survived in the workspace, so this is a
// faithful reconstruction from the interfaces the file has to satisfy, all of which were
// intact: `our_index_adapter.h`, the four CMakeLists' compile definitions, the shared
// `adapters/common/contract_acceptance.cpp`, ADAPTER_NOTES.md, and the descriptor every
// recorded result.json carries verbatim. The acceptance suite is the oracle: both arms must
// reproduce the recorded scores (our_index 100/100, our_index_lits 101/101), and the
// descriptor strings below are copied from the recorded results rather than re-invented.
// The incident and its blast radius are documented in RESULTS_OWNER_INTEGRATION_20261011.md.
//
// THE THREE ADAPTATION COSTS, all of them stated here rather than discovered later:
//
//   1. bulk_load converts std::vector<Row> to std::vector<Pair>. The two types are
//      layout-compatible in the fields that matter but not the same type, so the conversion
//      is a real copy of every key; the buffer is released immediately afterwards, which is
//      why retained_input_bytes is 0.
//   2. scan moves the implementation's owning Pair vector into the contract's Row vector
//      (one string move per row, no key-byte copy).
//   3. range_sum has no implementation, so it is DECLARED unsupported and throws rather
//      than returning a plausible zero.
//
// WHAT THE DESCRIPTOR SAYS, AND WHY EACH CLAIM IS TRUE. The two packages that share this
// source differ in exactly one compile definition (OUR_INDEX_BACKEND), and the declared key
// domain follows it: the reference model handles any byte string, while the LITS backend
// refuses bytes >= 0x80, embedded NULs and keys above its 4096-byte accumulation bound (the
// adapter's own `unsupported_reason` and `kMaxKeyBytes`). Declaring one domain for both
// would have been a claim the LITS arm cannot honour.

#include "our_index_adapter.h"

#include "index.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sib = string_index_bench::v1;
namespace validation = ::validation;

namespace {

// THE LABEL MUST TRACK benchmark-adapter.json's source_identity.index_hpp_sha256. It drifted
// once already -- the manifests were moved to a new hash while a descriptor string kept the
// old one, so a run would have carried a claim about an index it did not measure. Anything
// that re-pins index.hpp must update this line in the same change.
constexpr char kImplementationVersion[] =
    "index.hpp sha256 b20c83cab42787078508d5af6dd8a9e3c512fb0731618b9b0b81dc8fc64056d6";

#if OUR_INDEX_BACKEND == 1
constexpr bool kLits = true;
constexpr char kBackendName[] = "LitsHpt";
#else
constexpr bool kLits = false;
constexpr char kBackendName[] = "Context16";
#endif

std::uint32_t read_ov_threshold(const sib::Parameters& parameters) {
  // THE CONTRACT'S LIST RULES, enforced here rather than assumed: a name the adapter does
  // not know, or the same name twice, is an invalid_argument -- not a silently ignored
  // entry. `ov_threshold` is the one parameter this adapter accepts.
  std::uint32_t value = 0;
  bool seen = false;

  for (const auto& parameter : parameters) {
    if (parameter.first != "ov_threshold") {
      throw std::invalid_argument("unknown parameter: " + parameter.first);
    }

    if (seen) {
      throw std::invalid_argument("parameter given twice: ov_threshold");
    }

    seen = true;

    const std::string& text = parameter.second;
    std::uint64_t parsed = 0;

    for (const char c : text) {
      if (c < '0' || c > '9') {
        throw std::invalid_argument("ov_threshold is not a decimal integer: " + text);
      }

      parsed = parsed * 10 + static_cast<std::uint64_t>(c - '0');

      if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("ov_threshold does not fit in 32 bits: " + text);
      }
    }

    if (text.empty() || parsed == 0) {
      throw std::invalid_argument("ov_threshold is not a positive decimal integer: " + text);
    }

    value = static_cast<std::uint32_t>(parsed);
  }

  // The empty list is legal and means the shipped default. Every corpus in this round is
  // run with 128 (disclosed in the notes); skew's project-tuned value is 64 and is NOT
  // silently applied.
  return seen ? value : 128;
}

// The query mode the four-way harness uses: interval routing, no prefetch, fused
// fingerprint, SIMD tag filter, hybrid overflow search with the threshold this adapter was
// given, and no boundary-directory lookahead. The routing packages differ in the Config
// switch, not here.
validation::Mode query_mode(std::uint32_t ov_threshold) {
  validation::Mode mode;
  mode.route = validation::Route::Interval;
  mode.prefetch = validation::Prefetch::Off;
  mode.fingerprint = true;
  mode.simd = true;
  mode.ov = validation::OvSearch::Hybrid;
  mode.ov_thr = ov_threshold;
  mode.locate = false;
  return mode;
}

class OurIndexIndex final : public sib::Index {
 public:
  explicit OurIndexIndex(const sib::Parameters& parameters)
      : ov_threshold_(read_ov_threshold(parameters)) {
    validation::Config config = e25sw::baseline_config(ov_threshold_);
    e25sw::pin_frozen_switches(config);

    config.model_backend = kLits ? validation::ModelBackend::LitsHpt
                                 : validation::ModelBackend::Context16;

    // THE ROUTING ARM IS A COMPILE-TIME CHOICE OF THE PACKAGE, not a runtime default change.
    // The baseline packages (our_index, our_index_lits) do not define OUR_INDEX_ROUTING_ENABLED
    // and ASSERT the switch stayed off, so a future edit to baseline_config() or
    // pin_frozen_switches() fails here instead of silently measuring the candidate. The
    // *_routing packages define it and turn the candidate on at this one site, which is the
    // only difference between the two pairs.
#ifdef OUR_INDEX_ROUTING_ENABLED
    config.compact_region_routing = true;

    if (!config.compact_region_routing) {
      throw std::logic_error("routing arm failed to enable compact_region_routing");
    }
#else
    if (config.compact_region_routing) {
      throw std::logic_error(
          "compact_region_routing is on in a package that did not request the routing arm: "
          "baseline_config()/pin_frozen_switches() no longer keep it off");
    }
#endif

    // M3 (2026-10-11): the LITS reference-table residency candidate is OFF in every package.
    // Nothing here turns it on; this assert makes a future edit to baseline_config() or
    // pin_frozen_switches() fail HERE rather than silently measuring a different index.
    if (config.lits_skip_reference_table) {
      throw std::logic_error(
          "lits_skip_reference_table is on in a package that did not request it: "
          "baseline_config()/pin_frozen_switches() no longer keep it off");
    }

    // I10 is pinned rather than assumed. It is `true` in three places (the Config field
    // default, baseline_config, and pin_frozen_switches), so nothing here is closing a gap --
    // the value lives in another header, and this assert makes a future edit to any of the
    // three fail HERE rather than silently measuring a different index.
    if (!config.compact_plan_commit) {
      throw std::logic_error(
          "compact_plan_commit is off: pin_frozen_switches() did not take effect");
    }

    index_ = std::make_unique<validation::Index>(config);
    mode_ = query_mode(ov_threshold_);
  }

  sib::Descriptor descriptor() const override {
    sib::Descriptor descriptor;
    descriptor.implementation = "string learned index (shared prefix + conditional reference)";
    descriptor.implementation_version = kImplementationVersion;
    descriptor.adapter_version = "our-index-adapter-v1";

    sib::Capabilities& capabilities = descriptor.capabilities;
    capabilities.keys.min_bytes = 0;
    capabilities.keys.allows_prefix_pairs = true;

    if (kLits) {
      // The LITS backend's own admission rules, mirrored here: `unsupported_reason` refuses
      // bytes >= 0x80 and embedded NULs, and the accumulation bound refuses keys above 4096
      // bytes. The reference model has none of those restrictions, so the two packages
      // declare different domains -- and that difference is the honest one.
      capabilities.keys.max_bytes = 4096;
      capabilities.keys.allows_nul = false;
      capabilities.keys.allows_high_bit = false;
    } else {
      capabilities.keys.max_bytes = std::nullopt;
      capabilities.keys.allows_nul = true;
      capabilities.keys.allows_high_bit = true;
    }

    capabilities.max_value = std::numeric_limits<sib::Value>::max();
    capabilities.bulk_load = sib::Support::native;
    capabilities.find = sib::Support::native;
    capabilities.insert = sib::Support::native;
    capabilities.update = sib::Support::native;
    capabilities.erase = sib::Support::native;
    capabilities.scan = sib::Support::native;
    capabilities.range_sum = sib::Support::unsupported;

    descriptor.adaptation_notes =
        std::string("model backend: ") + kBackendName +
        "; frozen switch set pinned by pin_frozen_switches(), compact_plan_commit asserted "
        "on; ov_threshold fixed at 128 for every corpus (skew's project-tuned value is 64; "
        "disclosed, not hidden); bulk_load copies Row->Pair once at build time and releases "
        "it; scan moves keys out of the implementation's owning vector (a descriptor move "
        "per row, no key-byte copy); find adds no conversion, no scratch buffer and no "
        "allocation; range_sum has no implementation and is declared unsupported rather than "
        "stubbed";
#ifdef OUR_INDEX_ROUTING_ENABLED
    descriptor.adaptation_notes += "; compact_region_routing ENABLED (candidate arm)";
#endif
    return descriptor;
  }

  void bulk_load(const std::vector<sib::Row>& rows) override {
    // The contract says "exactly once on a fresh instance", and a second call would
    // otherwise silently rebuild the index underneath a caller that still held results. The
    // precondition is enforced here rather than documented: the acceptance suite checks it.
    if (loaded_) {
      throw std::logic_error("bulk_load may be called exactly once on a fresh instance");
    }

    loaded_ = true;

    // The contract fixes the precondition -- "sorted unique rows" -- and checking it here is
    // what turns a violated precondition into an invalid_argument instead of an index that
    // silently answers differently. The order is the contract's unsigned-byte order, which is
    // exactly what the implementation's own comparison uses.
    for (std::size_t i = 1; i < rows.size(); ++i) {
      const int order = ::validation::compare(rows[i - 1].key, rows[i].key);

      if (order == 0) {
        throw std::invalid_argument("bulk_load input contains a duplicate key: " +
                                    rows[i].key);
      }

      if (order > 0) {
        throw std::invalid_argument("bulk_load input is not sorted at row " +
                                    std::to_string(i));
      }
    }

    std::vector<validation::Pair> pairs;
    pairs.reserve(rows.size());

    for (const auto& row : rows) {
      pairs.emplace_back(row.key, row.value);
    }

    index_->bulk_load(pairs);

    // The conversion buffer is released here, and that is what makes retained_input_bytes a
    // real zero rather than an optimistic one: the implementation holds its own copy.
    std::vector<validation::Pair>().swap(pairs);
  }

  std::optional<sib::Value> find(std::string_view key) const override {
    const validation::Result result = index_->find(key, mode_);

    if (!result.found) {
      return std::nullopt;
    }

    return result.value;
  }

  sib::InsertResult insert(std::string_view key, sib::Value value) override {
    // The contract's rule is that an existing key KEEPS its value, so `Index::insert`'s
    // false return maps to already_exists and no separate read is needed.
    return index_->insert(key, value) ? sib::InsertResult::inserted
                                      : sib::InsertResult::already_exists;
  }

  sib::UpdateResult update(std::string_view key, sib::Value value) override {
    return index_->update(key, value) ? sib::UpdateResult::updated
                                      : sib::UpdateResult::not_found;
  }

  sib::EraseResult erase(std::string_view key) override {
    return index_->erase(key) ? sib::EraseResult::erased : sib::EraseResult::not_found;
  }

  std::vector<sib::Row> scan(std::string_view lower_bound, std::size_t limit) const override {
    std::vector<validation::Pair> found = index_->scan(lower_bound, limit);
    std::vector<sib::Row> rows;
    rows.reserve(found.size());

    for (auto& pair : found) {
      rows.push_back(sib::Row{std::move(pair.first), pair.second});
    }

    return rows;
  }

  sib::Value range_sum(std::string_view lower_bound, std::string_view upper_bound) const override {
    (void)lower_bound;
    (void)upper_bound;
    throw sib::UnsupportedOperation("range_sum is not implemented by the string learned index");
  }

  sib::MemoryUsage memory_usage() const override {
    sib::MemoryUsage usage;
    usage.index_owned_bytes = index_->memory();
    usage.adapter_owned_bytes = sizeof(*this) + sizeof(validation::Mode);
    usage.retained_input_bytes = 0;
    usage.accounting_notes =
        "index_owned_bytes is the sum of memory_breakdown()'s nine fields (block bytes, "
        "model tables, region prefixes, overflow containers, capacity tables, boundary "
        "directories, per-region objects, fences, index-level metadata); the fields are "
        "disjoint by construction, so the sum is not double-counted. adapter_owned_bytes "
        "covers only the adapter object and its query Mode. retained_input_bytes is 0 "
        "because the Row->Pair conversion buffer is released after bulk_load and the "
        "implementation holds its own copy. Returned rows from scan are owned by the caller "
        "and are not counted here.";
    return usage;
  }

 private:
  bool loaded_ = false;
  std::uint32_t ov_threshold_ = 128;
  std::unique_ptr<validation::Index> index_;
  validation::Mode mode_;
};

}  // namespace

std::unique_ptr<string_index_bench::v1::Index> make_our_index_index(
    const string_index_bench::v1::Parameters& parameters) {
  return std::make_unique<OurIndexIndex>(parameters);
}