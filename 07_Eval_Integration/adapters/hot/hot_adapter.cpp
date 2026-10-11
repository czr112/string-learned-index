// string-index/v1 adapter for HOT.
//
// TWO HARD CONSTRAINTS OF HOT SHAPE THIS ADAPTER, and both were found by compiling rather
// than by reading:
//
//   1. HOT'S VALUE TYPE MUST FIT IN A POINTER. Its interface static-asserts this. A
//      std::pair<const char*, uint64_t> is sixteen bytes and does not fit, so the trie cannot
//      store a key and a value together. It stores a POINTER TO A RECORD instead, and
//      PairPointerKeyExtractor pulls `.first` back out of that record as the key.
//
//   2. HOT'S KEY TYPE IS A NUL-TERMINATED const char*. The library walks a key by its
//      terminator, so stored keys carry one and a lookup has to present one. The contract
//      hands over a std::string_view, which carries a length and no terminator -- so find()
//      copies the queried key into a reusable scratch buffer. That is ONE MEMCPY PER LOOKUP
//      and no allocation after warm-up, and it is the floor for a terminator-addressed
//      structure. This is the single largest difference between this adapter and the ART one,
//      which needs no per-query copy at all.
//
// The alternative -- assuming the caller's string_view happens to be followed by a NUL in
// memory -- would read one byte past the view for every key that sits at the end of a buffer
// and would be right most of the time. "Right most of the time" is not a correctness
// argument, so it is not used.
//
// OWNERSHIP. HOT stores the pointers it is given, in both the trie (record pointers) and in
// each record (key pointers). So the arena of key bytes and the array of records must both
// stay put, and a growth of either one forces a rebuild with every pointer re-derived.
//
// SEMANTICS PINNED TO THE LIBRARY'S OWN PRIMITIVES:
//   * insert uses HOTSingleThreaded::insert, which returns FALSE for a key already present
//     and does not overwrite it -- the contract's "duplicate keys keep the old value".
//     HOT's upsert() is the OVERWRITING form and is deliberately not used.
//   * update must not insert: HOT has no update primitive, so this adapter looks the key up
//     first and only then removes and re-inserts.
//   * range_sum has no HOT primitive and is declared unsupported.

#include "hot_adapter.h"

#include <hot/singlethreaded/HOTSingleThreaded.hpp>

#include <idx/contenthelpers/OptionalValue.hpp>
#include <idx/contenthelpers/PairPointerKeyExtractor.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

// PAIR-LIKE, because PairPointerKeyExtractor reads `.first` through a pointer to it. A
// bespoke struct with `.key` and `.value` compiles fine right up to the point the extractor
// asks for `.first` -- and then the only remaining error is "has no member named lookup",
// which says nothing about the real cause. `first` points into the arena; `second` is the
// contract's value.
using HotRecord = std::pair<const char*, std::uint64_t>;

// HOT instantiates KeyExtractor<ValueType>, so the extractor's template argument is the
// POINTER type and its KeyType resolves to `const char*`. PairKeyExtractor (the non-pointer
// one) would resolve KeyType to the pointer itself and the trie would compare records by
// address.
using HotValue = HotRecord*;
using HotTrie =
    hot::singlethreaded::HOTSingleThreaded<HotValue, idx::contenthelpers::PairPointerKeyExtractor>;

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

class HotIndex final : public sib::Index {
 public:
  sib::Descriptor descriptor() const override {
    sib::Descriptor descriptor;
    descriptor.implementation = "HOT (Height Optimized Trie)";
    descriptor.implementation_version =
        "speedskater/hot commit 96bf6fb7103b27e50e16a6026db8974c090ee84a";
    descriptor.adapter_version = "hot-adapter-v2";

    sib::Capabilities& capabilities = descriptor.capabilities;
    capabilities.bulk_load = sib::Support::native;
    capabilities.find = sib::Support::adapted;
    capabilities.insert = sib::Support::native;
    capabilities.update = sib::Support::adapted;
    capabilities.erase = sib::Support::native;
    capabilities.scan = sib::Support::adapted;
    capabilities.range_sum = sib::Support::unsupported;

    // HOT addresses a key as a NUL-terminated C string, so a key CONTAINING a NUL cannot be
    // represented: the terminator would be indistinguishable from the byte. That is a limit
    // of the library, not a choice of this adapter, and it is declared rather than
    // discovered -- no corpus in this round contains a NUL, so nothing here would notice.
    capabilities.keys.min_bytes = 0;
    capabilities.keys.max_bytes = std::nullopt;
    capabilities.keys.fixed_bytes = std::nullopt;
    capabilities.keys.allows_nul = false;
    capabilities.keys.allows_high_bit = true;
    capabilities.keys.allows_prefix_pairs = true;

    descriptor.adaptation_notes =
        "HOT's value type must fit in a pointer (its interface static-asserts this), so the "
        "trie stores pointers to adapter-owned records and PairPointerKeyExtractor supplies "
        "the key. HOT's key type is a NUL-terminated const char*, so stored keys are copied "
        "into an arena that carries a terminator after each one and find() copies the queried "
        "key into a reusable scratch buffer to add one: one memcpy per lookup, no allocation "
        "after warm-up. That per-lookup copy is the cost of addressing by terminator and it is "
        "not zero. A key containing a NUL cannot be represented and is declared unsupported. "
        "update is adapted: HOT has no update primitive, so this adapter looks up first and "
        "then removes and re-inserts, which keeps update from inserting. scan uses HOT's "
        "native lower_bound, so its cost is the returned rows rather than the keys below the "
        "bound; the only adaptation there is a NUL-terminated copy of the bound, because the "
        "key type is a C string.";

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
      if (row.key.find('\0') != std::string::npos) {
        throw std::invalid_argument(
            "HOT addresses keys as NUL-terminated strings; an embedded NUL cannot be stored");
      }
    }

    std::size_t total = 0;

    for (const auto& row : rows) {
      total += row.key.size() + 1;
    }

    // Headroom for inserts, so the first mutation does not immediately force a rebuild.
    arena_.reserve(total + total / 2 + 64);
    records_.reserve(rows.size() + rows.size() / 2 + 64);

    for (const auto& row : rows) {
      records_.push_back(HotRecord{append_to_arena(row.key), row.value});
    }

    for (auto& record : records_) {
      trie_.insert(&record);
    }

    loaded_ = true;
  }

  std::optional<sib::Value> find(std::string_view key) const override {
    require_loaded();

    const idx::contenthelpers::OptionalValue<HotValue> found = trie_.lookup(terminated(key));

    if (!found.mIsValid) {
      return std::nullopt;
    }

    return found.mValue->second;
  }

  sib::InsertResult insert(std::string_view key, sib::Value value) override {
    require_loaded();

    if (key.find('\0') != std::string::npos) {
      throw std::invalid_argument("HOT cannot store a key containing NUL");
    }

    // GROW FIRST, APPEND SECOND. Both containers may move, and HOT holds pointers into both,
    // so a reallocation after the append would leave the trie holding addresses that are no
    // longer valid with no way to notice.
    if (arena_.size() + key.size() + 1 > arena_.capacity() ||
        records_.size() + 1 > records_.capacity()) {
      arena_.reserve(std::max(arena_.size() + key.size() + 1, 2 * arena_.capacity() + 64));
      records_.reserve(std::max(records_.size() + 1, 2 * records_.capacity() + 64));

      rebuild();
    }

    records_.push_back(HotRecord{append_to_arena(key), value});

    // HOT's insert returns false for a key that is already present and leaves the stored
    // value alone, which is this contract's "duplicate keys keep the old value".
    if (!trie_.insert(&records_.back())) {
      // The appended record is now unreachable, but it must not be reported as inserted.
      // HOT offers no way to remove a single pair, so the record is dropped from the array
      // and the rebuild path below keeps the arena and the trie consistent.
      records_.pop_back();
      rebuild();

      return sib::InsertResult::already_exists;
    }

    return sib::InsertResult::inserted;
  }

  sib::UpdateResult update(std::string_view key, sib::Value value) override {
    require_loaded();

    const idx::contenthelpers::OptionalValue<HotValue> found = trie_.lookup(terminated(key));

    if (!found.mIsValid) {
      return sib::UpdateResult::not_found;
    }

    // The lookup above is what keeps update from inserting: an absent key never reaches here.
    const std::string terminated_key = terminated_copy(key);

    if (arena_.size() + key.size() + 1 > arena_.capacity() ||
        records_.size() + 1 > records_.capacity()) {
      arena_.reserve(std::max(arena_.size() + key.size() + 1, 2 * arena_.capacity() + 64));
      records_.reserve(std::max(records_.size() + 1, 2 * records_.capacity() + 64));
    }

    trie_.remove(terminated_key.c_str());

    records_.push_back(HotRecord{append_to_arena(key), value});

    if (!trie_.insert(&records_.back())) {
      return sib::UpdateResult::not_found;
    }

    return sib::UpdateResult::updated;
  }

  sib::EraseResult erase(std::string_view key) override {
    require_loaded();

    const std::string terminated_key = terminated_copy(key);
    const bool removed = trie_.remove(terminated_key.c_str());

    return removed ? sib::EraseResult::erased : sib::EraseResult::not_found;
  }

  std::vector<sib::Row> scan(std::string_view lower_bound,
                             std::size_t limit) const override {
    require_loaded();

    std::vector<sib::Row> out;

    if (limit == 0) {
      return out;
    }

    // HOT HAS A NATIVE lower_bound, and an earlier version of this adapter did not use it:
    // it walked from begin() and discarded everything below the bound, which charged HOT for
    // a linear scan the library does not require. lower_bound(searchKey) seeks directly, so
    // the cost here is the returned rows and not the keys below them.
    //
    // (HOT's own scan() member is NOT used: it advances one position past the located element
    // and returns a single value, which does not match the contract's inclusive lower bound.)
    const std::string bound_copy = terminated_copy(lower_bound);
    auto it = trie_.lower_bound(bound_copy.c_str());

    for (; it != trie_.end(); ++it) {
      const HotRecord* record = *it;

      if (record == nullptr) {
        continue;
      }

      out.push_back(sib::Row{std::string(record->first), record->second});

      if (out.size() >= limit) {
        break;
      }
    }

    return out;
  }

  sib::Value range_sum(std::string_view, std::string_view) const override {
    throw sib::UnsupportedOperation("range_sum has no HOT primitive");
  }

  sib::MemoryUsage memory_usage() const override {
    sib::MemoryUsage usage;

    // HOT publishes node-type statistics but no byte accounting, and this adapter will not
    // turn a node count into a byte figure -- that would be an estimate wearing a
    // measurement's clothes.
    usage.index_owned_bytes = std::nullopt;

    usage.adapter_owned_bytes = arena_.capacity() + records_.capacity() * sizeof(HotRecord);

    usage.retained_input_bytes = 0;

    usage.accounting_notes =
        "index_owned_bytes is unavailable: HOT reports node-type statistics but no byte "
        "accounting, and deriving bytes from node counts would be an estimate rather than a "
        "measurement. adapter_owned_bytes is exact and covers the NUL-terminated key arena and "
        "the record array -- the two things this adapter must own because HOT stores the "
        "pointers it is given and addresses keys by terminator. retained_input_bytes is 0: the "
        "harness rows are copied into the arena and not referenced afterwards. Because the "
        "index's own bytes are unavailable here, THIS ADAPTER'S MEMORY FIGURE IS NOT "
        "COMPARABLE with an implementation that reports its own.";

    return usage;
  }

 private:
  void require_loaded() const {
    if (!loaded_) {
      throw std::logic_error("operation called before bulk_load");
    }
  }

  // Appends a NUL-terminated copy of `key` to the arena and returns its address. The arena
  // must already have room: callers check and reserve before calling.
  const char* append_to_arena(std::string_view key) {
    const std::size_t offset = arena_.size();
    arena_.insert(arena_.end(), key.begin(), key.end());
    arena_.push_back('\0');

    return arena_.data() + offset;
  }

  // A standalone NUL-terminated copy, for the operations that need the key to stay valid
  // across a call that may grow the arena.
  static std::string terminated_copy(std::string_view key) {
    std::string out(key);
    out.push_back('\0');

    return out;
  }

  // The lookup form. Reuses a member buffer so no allocation happens after warm-up.
  const char* terminated(std::string_view key) const {
    scratch_.assign(key.data(), key.size());
    return scratch_.c_str();
  }

  // Rebuilds the trie from the record array, re-deriving every key pointer. This is the only
  // correct response to a growth of the arena or the record array, because HOT stores the
  // pointers it was given.
  void rebuild() {
    HotTrie fresh;

    for (auto& record : records_) {
      fresh.insert(&record);
    }

    trie_ = std::move(fresh);
  }

  // Mutable because HOT's lookup() and begin() are not const-qualified, although they do not
  // modify the trie. The contract declares find() and scan() const, so the qualifier is
  // discarded here rather than weakening the adapter's interface.
  mutable HotTrie trie_;
  bool loaded_ = false;

  std::vector<char> arena_;
  std::vector<HotRecord> records_;

  mutable std::string scratch_;
};

}  // namespace

std::unique_ptr<sib::Index> make_hot_index(const sib::Parameters& parameters) {
  validate_parameters(parameters);
  return std::make_unique<HotIndex>();
}
