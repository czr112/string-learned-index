// string-index/v1 adapter for libart.
//
// WHAT LIBART ACTUALLY OWNS, which an earlier version of this adapter got wrong in a way
// that cost 26 MB of redundant keys:
//
//   * THE KEY IS COPIED. make_leaf() does calloc(sizeof(art_leaf) + key_len) and memcpy's the
//     key into the leaf (vendored art.c:359-364). So the caller's bytes do not have to
//     outlive the call, and this adapter does NOT need to keep a key arena at all.
//   * THE VALUE IS NOT COPIED. libart stores the void* it is given. So the caller must keep
//     whatever that pointer addresses alive.
//
// The earlier version kept both `key_arena_` (redundant) and `values_`, plus `offsets_` and
// `key_lengths_` to rebuild the tree when either container moved. None of that is needed:
// only the VALUE pointer has to stay put, and a std::deque guarantees that -- push_back on a
// deque never invalidates references to existing elements. So the tree never has to be
// rebuilt, and there is no second copy of any key.
//
// A std::vector was the wrong container precisely because it DOES move its elements, and the
// rebuild machinery existed only to work around that.
//
// NO PER-QUERY COPY. libart takes a pointer and a length, and the contract hands over a
// string_view with exactly that, so find() passes the view's bytes straight through. This is
// a real difference from a NUL-terminated-key library, where every lookup would need a copy
// to add a terminator.
//
// SCAN IS ADAPTED, AND THE COST IS REAL. libart offers in-order traversal (art_iter) but no
// "start at a key", so reaching lower_bound means walking from the first key and discarding
// everything below it. The work is O(keys below the lower bound) rather than O(returned
// rows). It is recorded rather than hidden, and it is why this adapter's scan numbers are not
// comparable with an implementation that can seek. The walk stops as soon as the requested
// rows are collected.
//
// SEMANTICS PINNED TO THE LIBRARY'S OWN PRIMITIVES:
//   * insert uses art_insert_no_replace, which does NOT overwrite and returns the old value,
//     which is exactly the contract's "duplicate keys keep the old value".
//   * update must not insert: this adapter searches first and only then writes through the
//     pointer art_search returned, so an absent key cannot become present.
//   * range_sum has no libart primitive and is declared unsupported rather than summed by an
//     adapter-side walk that would be measured as ART's cost.

#include "art_adapter.h"

extern "C" {
#include "art.h"
}

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sib = string_index_bench::v1;

namespace {

struct UnsignedByteLess {
  bool operator()(std::string_view left, std::string_view right) const {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(),
        [](char a, char b) {
          return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
        });
  }
};

void validate_parameters(const sib::Parameters& parameters) {
  std::set<std::string> names;

  for (const auto& parameter : parameters) {
    if (!names.insert(parameter.first).second) {
      throw std::invalid_argument("duplicate parameter: " + parameter.first);
    }

    throw std::invalid_argument("unknown parameter: " + parameter.first);
  }
}

class ArtIndex final : public sib::Index {
 public:
  ArtIndex() {
    if (art_tree_init(&tree_) != 0) {
      throw std::runtime_error("art_tree_init failed");
    }

    initialized_ = true;
  }

  ~ArtIndex() override {
    if (initialized_) {
      art_tree_destroy(&tree_);
    }
  }

  ArtIndex(const ArtIndex&) = delete;
  ArtIndex& operator=(const ArtIndex&) = delete;

  sib::Descriptor descriptor() const override {
    sib::Descriptor descriptor;
    descriptor.implementation = "libart (Adaptive Radix Tree)";
    descriptor.implementation_version =
        "armon/libart commit 301046804af165269e37da6725f5a4aec9ecc881";
    descriptor.adapter_version = "art-adapter-v2";

    sib::Capabilities& capabilities = descriptor.capabilities;
    capabilities.bulk_load = sib::Support::native;
    capabilities.find = sib::Support::native;
    capabilities.insert = sib::Support::native;
    capabilities.update = sib::Support::adapted;
    capabilities.erase = sib::Support::native;
    capabilities.scan = sib::Support::adapted;
    capabilities.range_sum = sib::Support::unsupported;

    capabilities.keys.min_bytes = 0;
    capabilities.keys.max_bytes = std::nullopt;
    capabilities.keys.fixed_bytes = std::nullopt;
    capabilities.keys.allows_nul = true;
    capabilities.keys.allows_high_bit = true;
    capabilities.keys.allows_prefix_pairs = true;

    descriptor.adaptation_notes =
        "libart COPIES the key into its leaf (make_leaf memcpys it) but stores the VALUE as "
        "the pointer it is given, so this adapter owns only a stable array of values and no "
        "key copies at all. find() requires no conversion: libart takes (pointer, length) and "
        "the contract supplies exactly that, so there is no scratch buffer, no terminator and "
        "no per-query copy. update is adapted: libart has no update primitive, so this adapter "
        "searches and then writes through the returned pointer, which keeps update from "
        "inserting. scan is adapted and NOT seekable: libart iterates in order from the first "
        "key, so reaching lower_bound costs a walk over every key below it -- O(keys below the "
        "bound), not O(returned rows) -- and the walk stops as soon as the requested rows are "
        "collected.";

    return descriptor;
  }

  void bulk_load(const std::vector<sib::Row>& rows) override {
    if (loaded_) {
      throw std::logic_error("bulk_load called more than once");
    }

    for (std::size_t i = 1; i < rows.size(); ++i) {
      if (!UnsignedByteLess{}(rows[i - 1].key, rows[i].key)) {
        throw std::invalid_argument(
            "bulk_load rows are not strictly unsigned-byte sorted and unique");
      }
    }

    for (const auto& row : rows) {
      values_.push_back(row.value);

      // The key is passed straight from the caller's row: libart copies it, so nothing here
      // has to outlive the call.
      art_insert(&tree_, reinterpret_cast<const unsigned char*>(row.key.data()),
                 static_cast<int>(row.key.size()), &values_.back());
    }

    loaded_ = true;
  }

  std::optional<sib::Value> find(std::string_view key) const override {
    require_loaded();

    void* found = art_search(&tree_,
                             reinterpret_cast<const unsigned char*>(key.data()),
                             static_cast<int>(key.size()));

    if (found == nullptr) {
      return std::nullopt;
    }

    return *static_cast<const sib::Value*>(found);
  }

  sib::InsertResult insert(std::string_view key, sib::Value value) override {
    require_loaded();

    // Checking first is what keeps a refused insert from growing the adapter's footprint.
    const bool present = art_search(&tree_,
                                    reinterpret_cast<const unsigned char*>(key.data()),
                                    static_cast<int>(key.size())) != nullptr;

    if (present) {
      return sib::InsertResult::already_exists;
    }

    // A deque never moves its existing elements, so the pointers libart already holds stay
    // valid and the tree never has to be rebuilt.
    values_.push_back(value);

    art_insert_no_replace(&tree_, reinterpret_cast<const unsigned char*>(key.data()),
                          static_cast<int>(key.size()), &values_.back());

    return sib::InsertResult::inserted;
  }

  sib::UpdateResult update(std::string_view key, sib::Value value) override {
    require_loaded();

    void* found = art_search(&tree_, reinterpret_cast<const unsigned char*>(key.data()),
                             static_cast<int>(key.size()));

    if (found == nullptr) {
      return sib::UpdateResult::not_found;
    }

    // Writing through the pointer art_search returned updates the stored value in place and
    // cannot insert, which is what the contract requires of update.
    *static_cast<sib::Value*>(found) = value;

    return sib::UpdateResult::updated;
  }

  sib::EraseResult erase(std::string_view key) override {
    require_loaded();

    void* removed = art_delete(&tree_, reinterpret_cast<const unsigned char*>(key.data()),
                               static_cast<int>(key.size()));

    return removed != nullptr ? sib::EraseResult::erased : sib::EraseResult::not_found;
  }

  std::vector<sib::Row> scan(std::string_view lower_bound,
                             std::size_t limit) const override {
    require_loaded();

    std::vector<sib::Row> out;

    if (limit == 0) {
      return out;
    }

    ScanState state;
    state.lower_bound = lower_bound;
    state.limit = limit;
    state.out = &out;

    // art_iter is declared `art_tree*` although it only reads: libart's C-era API is not
    // const-correct, and the callback receives `const unsigned char*` and `void*` for the
    // same reason. The cast is confined here, and this callback never writes to the tree --
    // it appends to a vector the caller owns.
    art_iter(const_cast<art_tree*>(&tree_), &ArtIndex::collect, &state);

    return out;
  }

  sib::Value range_sum(std::string_view, std::string_view) const override {
    throw sib::UnsupportedOperation("range_sum has no libart primitive");
  }

  sib::MemoryUsage memory_usage() const override {
    sib::MemoryUsage usage;

    // libart exposes no byte accounting of its own -- no allocation hook, no size query.
    // Reporting a number here would be an invention, so the index's own bytes are reported
    // as UNAVAILABLE rather than estimated from node counts.
    usage.index_owned_bytes = std::nullopt;

    // The adapter owns exactly one thing: the array of values libart points at. There is no
    // key arena, because libart copies keys.
    usage.adapter_owned_bytes = values_.size() * sizeof(sib::Value);

    usage.retained_input_bytes = 0;

    usage.accounting_notes =
        "index_owned_bytes is unavailable: libart has no allocation hook and no byte "
        "accounting, and estimating it from node counts would be an invention rather than a "
        "measurement. adapter_owned_bytes covers exactly one container -- the deque of values "
        "that libart's leaves point at, counted at its live size because a deque's element "
        "storage is not its capacity. There is no key arena: libart copies each key into its "
        "own leaf, so keeping one would be a redundant second copy of every key. "
        "retained_input_bytes is 0: the harness rows are passed straight to libart, which "
        "copies what it needs. Because the index's own bytes are unavailable here, THIS "
        "ADAPTER'S MEMORY FIGURE IS NOT COMPARABLE with an implementation that reports its own.";

    return usage;
  }

 private:
  struct ScanState {
    std::string_view lower_bound;
    std::size_t limit = 0;
    std::vector<sib::Row>* out = nullptr;
  };

  static int collect(void* data, const unsigned char* key, std::uint32_t key_len, void* value) {
    auto* state = static_cast<ScanState*>(data);
    const std::string_view candidate(reinterpret_cast<const char*>(key), key_len);

    if (UnsignedByteLess{}(candidate, state->lower_bound)) {
      return 0;  // keep walking: art_iter is in order, so everything below is below
    }

    state->out->push_back(
        sib::Row{std::string(candidate), *static_cast<const sib::Value*>(value)});

    // A non-zero return stops the iteration, which is how the limit is enforced without
    // walking the rest of the tree.
    return state->out->size() >= state->limit ? 1 : 0;
  }

  void require_loaded() const {
    if (!loaded_) {
      throw std::logic_error("operation called before bulk_load");
    }
  }

  art_tree tree_{};
  bool initialized_ = false;
  bool loaded_ = false;

  // std::deque, NOT std::vector: libart stores the address of each value, and a deque's
  // push_back never invalidates references to existing elements. A vector would force the
  // adapter to rebuild the whole tree whenever it grew -- which is what the previous version
  // did, along with keeping a redundant copy of every key to rebuild from.
  std::deque<sib::Value> values_;
};

}  // namespace

std::unique_ptr<sib::Index> make_art_index(const sib::Parameters& parameters) {
  validate_parameters(parameters);
  return std::make_unique<ArtIndex>();
}
