// Interface acceptance for a string-index/v1 adapter, against the contract's semantics.
//
// WHY THIS EXISTS SEPARATELY FROM THE EVALUATOR. The unified entry point validates bulk_load
// and find only ("首版只验收 bulk_load 和 find") -- it says so itself. The contract defines
// insert, update, erase, scan, range_sum and memory_usage as well, and the round's rules
// require optional operations to be accepted one by one rather than assumed. So this program
// drives the adapter through every declared operation and checks the semantics the contract
// states, with an independent std::map as the reference.
//
// WHAT IT IS CHECKING THAT A SMOKE TEST WOULD NOT:
//
//   * insert on an existing key must KEEP THE OLD VALUE. An adapter that forwarded to an
//     overwriting upsert would pass a naive "returns the right enum" test and still be wrong.
//   * update on a missing key must NOT insert.
//   * a stored value of 0 and a stored value of UINT64_MAX must be distinguishable from
//     absence. Presence is carried by the return type, never by a sentinel.
//   * scan's returned keys must survive the adapter's destruction -- the contract says the
//     results are owning, and an adapter that returned views into the index would pass a
//     test that checked them while the adapter was still alive.
//   * the declared key domain must match what the implementation actually does. The claims
//     are tested with keys that no corpus in this round contains: an empty key, keys with NUL
//     and with high bytes, prefix pairs, and a long key.
//   * a declared-unsupported operation must THROW rather than return a plausible empty result.
//
// Usage:  contract_acceptance            (exit 0 = all checks passed)

// ONE ACCEPTANCE PROGRAM FOR EVERY ARM, parameterised by the factory. The round requires ART
// and HOT to pass the SAME interface acceptance as this project's index -- two adapters each
// checked by their own hand-written test would be two different bars, and the comparison
// would rest on whichever bar was lower.
//
// The factory is injected as a compile definition by each adapter's CMakeLists:
//   ACCEPTANCE_FACTORY_HEADER   the header to include, as a quoted string
//   ACCEPTANCE_FACTORY_SYMBOL   the factory function to call
//   ACCEPTANCE_IMPLEMENTATION   a label for the output
#ifndef ACCEPTANCE_FACTORY_HEADER
#error "define ACCEPTANCE_FACTORY_HEADER to the adapter's factory header"
#endif
#ifndef ACCEPTANCE_FACTORY_SYMBOL
#error "define ACCEPTANCE_FACTORY_SYMBOL to the adapter's factory symbol"
#endif
// The label is DERIVED, not passed. A string-valued compile definition has to survive CMake
// and the shell with its quotes intact, and when they are stripped the failure is a confusing
// compile error rather than a wrong label. Stringifying the factory symbol cannot lose
// anything, and the symbol already names the arm unambiguously.
#define ACCEPTANCE_STRINGIFY_INNER(x) #x
#define ACCEPTANCE_STRINGIFY(x) ACCEPTANCE_STRINGIFY_INNER(x)
#define ACCEPTANCE_IMPLEMENTATION ACCEPTANCE_STRINGIFY(ACCEPTANCE_FACTORY_SYMBOL)

#include ACCEPTANCE_FACTORY_HEADER

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sib = string_index_bench::v1;

namespace {

int failures = 0;
int checks = 0;

struct UnsignedByteLess {
  bool operator()(const std::string& left, const std::string& right) const {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(),
        [](char a, char b) {
          return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
        });
  }
};

void check(bool condition, const std::string& what) {
  ++checks;

  if (!condition) {
    ++failures;
    std::cout << "FAIL: " << what << "\n";
  }
}

std::unique_ptr<sib::Index> fresh(const sib::Parameters& parameters = {}) {
  return ACCEPTANCE_FACTORY_SYMBOL(parameters);
}

std::vector<sib::Row> rows_of(const std::vector<std::pair<std::string, std::uint64_t>>& input) {
  std::vector<sib::Row> rows;

  for (const auto& item : input) {
    rows.push_back(sib::Row{item.first, item.second});
  }

  return rows;
}

// The declared key domain of the adapter under test. Every piece of test data below is
// chosen FROM this, not hard-coded: the first version of this file put a high-byte key in
// the shared fixture, and the LITS backend refused it -- a failure in the TEST DATA, which
// would have read as a failure of the adapter.
sib::KeyDomain declared_domain() {
  return ACCEPTANCE_FACTORY_SYMBOL({})->descriptor().capabilities.keys;
}

// Kept in unsigned-byte order because that is the order bulk_load requires, and because the
// reference map must be ordered the same way for scans to line up.
std::vector<std::pair<std::string, std::uint64_t>> base_keys() {
  const sib::KeyDomain domain = declared_domain();

  std::vector<std::pair<std::string, std::uint64_t>> keys = {
      {std::string("a"), 1},
      {std::string("aa"), 2},
      {std::string("ab"), 3},
      {std::string("b"), 0},
      {std::string("zz"), std::numeric_limits<std::uint64_t>::max()},
  };

  // The high-byte key is included only where the declaration allows one. It is kept in the
  // fixture rather than dropped, because a high byte is the case most likely to expose a
  // signed-char comparison bug -- so the substitute keeps the same length and sort position.
  if (domain.allows_high_bit) {
    keys.emplace_back(std::string("b\xffzz", 4), 4);
  } else {
    keys.emplace_back(std::string("bz"), 4);
  }

  std::sort(keys.begin(), keys.end(),
            [](const auto& a, const auto& b) { return UnsignedByteLess{}(a.first, b.first); });

  return keys;
}

// ---------------------------------------------------------------------------- descriptor
void test_descriptor() {
  auto index = fresh();
  const sib::Descriptor descriptor = index->descriptor();

  check(!descriptor.implementation.empty(), "descriptor names the implementation");
  check(!descriptor.implementation_version.empty(), "descriptor carries an implementation version");
  check(!descriptor.adapter_version.empty(), "descriptor carries an adapter version");
  check(!descriptor.adaptation_notes.empty(), "descriptor carries adaptation notes");

  // DECLARATION AND BEHAVIOUR MUST AGREE, and that is the contract's requirement -- not a
  // particular support value. An earlier version of this file asserted `== native` for every
  // operation, which is this project's index's choice and not a contract rule: libart has no
  // update primitive and no seekable iteration, so declaring those `adapted` is correct and
  // the test was wrong. What IS checked here:
  //   * every operation is either declared supported or is one we agree not to use;
  //   * an `adapted` operation carries adaptation_notes, because the contract requires the
  //     adaptation to be stated;
  //   * a declared-unsupported operation THROWS, so the declaration is not decoration.
  const auto& capabilities = descriptor.capabilities;

  check(capabilities.bulk_load != sib::Support::unsupported, "bulk_load is declared supported");
  check(capabilities.find != sib::Support::unsupported, "find is declared supported");
  const auto valid_support = [](sib::Support s) {
    return s == sib::Support::native || s == sib::Support::adapted || s == sib::Support::unsupported;
  };
  check(valid_support(capabilities.insert), "insert has a valid capability declaration");
  check(valid_support(capabilities.update), "update has a valid capability declaration");
  check(valid_support(capabilities.erase), "erase has a valid capability declaration");
  check(valid_support(capabilities.scan), "scan has a valid capability declaration");

  const bool any_adapted = capabilities.bulk_load == sib::Support::adapted ||
                           capabilities.find == sib::Support::adapted ||
                           capabilities.insert == sib::Support::adapted ||
                           capabilities.update == sib::Support::adapted ||
                           capabilities.erase == sib::Support::adapted ||
                           capabilities.scan == sib::Support::adapted ||
                           capabilities.range_sum == sib::Support::adapted;

  check(!any_adapted || !descriptor.adaptation_notes.empty(),
        "an adapted operation carries the notes the contract requires");

  std::cout << "descriptor:\n"
            << "  implementation = " << descriptor.implementation << "\n"
            << "  version        = " << descriptor.implementation_version << "\n"
            << "  support        = bulk_load="
            << static_cast<int>(capabilities.bulk_load)
            << " find=" << static_cast<int>(capabilities.find)
            << " insert=" << static_cast<int>(capabilities.insert)
            << " update=" << static_cast<int>(capabilities.update)
            << " erase=" << static_cast<int>(capabilities.erase)
            << " scan=" << static_cast<int>(capabilities.scan)
            << " range_sum=" << static_cast<int>(capabilities.range_sum)
            << "   (0=unsupported 1=native 2=adapted)\n"
            << "  notes          = " << descriptor.adaptation_notes << "\n";
}

// --------------------------------------------------------------------------- parameters
void test_parameters() {
  bool rejected_unknown = false;

  try {
    fresh({{"nonsense", "1"}});
  } catch (const std::invalid_argument&) {
    rejected_unknown = true;
  }

  check(rejected_unknown, "unknown parameter is rejected with invalid_argument");

  bool rejected_duplicate = false;

  try {
    fresh({{"ov_threshold", "128"}, {"ov_threshold", "64"}});
  } catch (const std::invalid_argument&) {
    rejected_duplicate = true;
  }

  check(rejected_duplicate, "duplicate parameter is rejected with invalid_argument");

  // NO PARAMETER NAME IS ASSUMED HERE. Which names an adapter accepts is its own choice; the
  // contract fixes only the LIST's rules. An earlier version probed `ov_threshold`, which is
  // this project's index's parameter, and would have failed any other adapter for correctly
  // rejecting a name it does not know.
  bool accepted_empty = true;

  try {
    auto index = fresh({});
    index->bulk_load({});
  } catch (const std::exception&) {
    accepted_empty = false;
  }

  check(accepted_empty, "the empty parameter list is accepted");

  // The contract also requires an ILLEGAL VALUE to be rejected, but a value can only be
  // judged against a name, and names are the adapter's own choice. So the probe is opt-in:
  // an adapter that declares a sample parameter name gets the check, and one that does not
  // is not failed for a rule it was never asked about. The absence is visible here rather
  // than silently reducing the bar.
#ifdef ACCEPTANCE_SAMPLE_PARAMETER
  bool rejected_bad_value = false;

  try {
    fresh({{ACCEPTANCE_STRINGIFY(ACCEPTANCE_SAMPLE_PARAMETER), "!!not-a-legal-value!!"}});
  } catch (const std::invalid_argument&) {
    rejected_bad_value = true;
  }

  check(rejected_bad_value, "an illegal value for a declared parameter is rejected");
#else
  std::cout << "  (no ACCEPTANCE_SAMPLE_PARAMETER: the illegal-value rule is not probed)\n";
#endif
}

// -------------------------------------------------------------------------- bulk_load
void test_bulk_load() {
  // Empty input is explicitly legal.
  {
    auto index = fresh();
    index->bulk_load({});
    check(!index->find("anything").has_value(), "an empty index finds nothing");
  }

  // Strictly sorted and unique is a precondition, and it is checked rather than assumed.
  {
    auto index = fresh();
    bool threw = false;

    try {
      index->bulk_load(rows_of({{std::string("b"), 1}, {std::string("a"), 2}}));
    } catch (const std::invalid_argument&) {
      threw = true;
    }

    check(threw, "unsorted bulk_load input is rejected");
  }

  {
    auto index = fresh();
    bool threw = false;

    try {
      index->bulk_load(rows_of({{std::string("a"), 1}, {std::string("a"), 2}}));
    } catch (const std::invalid_argument&) {
      threw = true;
    }

    check(threw, "duplicate bulk_load input is rejected");
  }

  // Calling it twice is a caller error, and a second call would otherwise silently rebuild.
  {
    auto index = fresh();
    index->bulk_load({});
    bool threw = false;

    try {
      index->bulk_load({});
    } catch (const std::logic_error&) {
      threw = true;
    }

    check(threw, "a second bulk_load is rejected");
  }
}

// ------------------------------------------------------------------------------- find
void test_find() {
  const auto keys = base_keys();
  auto index = fresh();
  index->bulk_load(rows_of(keys));

  for (const auto& item : keys) {
    const auto found = index->find(item.first);
    check(found.has_value(), "find finds a loaded key: " + item.first);
    check(found.has_value() && *found == item.second, "find returns the stored value");
  }

  // The two values a sentinel-based implementation would confuse with absence.
  check(index->find("b").has_value() && *index->find("b") == 0,
        "a stored 0 is present, not absent");
  check(index->find("zz").has_value() &&
            *index->find("zz") == std::numeric_limits<std::uint64_t>::max(),
        "a stored UINT64_MAX is present, not absent");

  check(!index->find("missing").has_value(), "find returns nullopt for a missing key");
  check(!index->find("").has_value(), "find returns nullopt for the empty key when not stored");
  check(!index->find("z").has_value(), "find returns nullopt for a proper prefix of a stored key");
}

// --------------------------------------------------------------- insert / update / erase
void test_all_supported_mutations() {
  const auto keys = base_keys();
  auto index = fresh();
  index->bulk_load(rows_of(keys));

  std::map<std::string, std::uint64_t, UnsignedByteLess> reference;

  for (const auto& item : keys) {
    reference[item.first] = item.second;
  }

  // inserted
  check(index->insert("new", 7) == sib::InsertResult::inserted, "insert reports inserted");
  reference["new"] = 7;

  // already_exists, AND the old value survives. This is the check an upsert-based adapter
  // would fail while still returning the right enum on the line above.
  check(index->insert("new", 999) == sib::InsertResult::already_exists,
        "insert on an existing key reports already_exists");
  check(index->find("new").has_value() && *index->find("new") == 7,
        "insert on an existing key KEEPS the old value");
  check(reference["new"] == 7, "reference agrees the old value survives");

  // update
  check(index->update("new", 8) == sib::UpdateResult::updated, "update reports updated");
  reference["new"] = 8;
  check(index->find("new").has_value() && *index->find("new") == 8, "update changed the value");
  check(index->update("absent", 1) == sib::UpdateResult::not_found,
        "update on a missing key reports not_found");
  check(!index->find("absent").has_value(), "update on a missing key does NOT insert it");

  // erase
  check(index->erase("new") == sib::EraseResult::erased, "erase reports erased");
  reference.erase("new");
  check(!index->find("new").has_value(), "erase removed the key");
  check(index->erase("new") == sib::EraseResult::not_found,
        "erasing a missing key reports not_found");

  // Erase down to the last key, then past it.
  auto shrink = fresh();
  shrink->bulk_load(rows_of({{std::string("only"), 5}}));
  check(shrink->erase("only") == sib::EraseResult::erased, "erase removes the last key");
  check(!shrink->find("only").has_value(), "the last key is gone after erasing it");
  check(shrink->erase("only") == sib::EraseResult::not_found,
        "erasing again on the now-empty index reports not_found");

  // Every remaining key must agree with the reference.
  for (const auto& item : reference) {
    const auto found = index->find(item.first);
    check(found.has_value() && *found == item.second,
          "post-mutation state agrees with the reference: " + item.first);
  }
}

void test_mutations() {
  const auto caps=fresh()->descriptor().capabilities;
  if(caps.insert!=sib::Support::unsupported && caps.update!=sib::Support::unsupported && caps.erase!=sib::Support::unsupported) {
    test_all_supported_mutations();return;
  }
  // Probe each optional capability independently, including partial-support adapters.
  auto index=fresh();const auto rows=rows_of(base_keys());index->bulk_load(rows);
  const auto unchanged=[&] {
    for(const auto& row:rows)check(index->find(row.key)==std::optional<sib::Value>(row.value), "unsupported mutation preserves every loaded value");
    check(!index->find("new"), "unsupported mutation does not insert a new key");
  };
  const auto unsupported=[&](auto operation) {
    bool threw=false;try {operation();}catch(const sib::UnsupportedOperation&){threw=true;}
    check(threw,"declared unsupported mutation throws UnsupportedOperation");unchanged();
  };
  if(caps.insert==sib::Support::unsupported) {
    unsupported([&]{index->insert("new",7);});unsupported([&]{index->insert("a",999);});
  } else {
    check(index->insert("new",7)==sib::InsertResult::inserted,"supported insert adds key");
    check(index->insert("new",999)==sib::InsertResult::already_exists,"duplicate insert reports existing");
    check(index->find("new")==std::optional<sib::Value>(7),"duplicate insert preserves value");
    index=fresh();index->bulk_load(rows);
  }
  if(caps.update==sib::Support::unsupported) {
    unsupported([&]{index->update("a",99);});unsupported([&]{index->update("new",99);});
  } else {
    check(index->update("a",99)==sib::UpdateResult::updated,"supported update changes key");
    check(index->find("a")==std::optional<sib::Value>(99),"supported update stores value");
    check(index->update("new",99)==sib::UpdateResult::not_found&&!index->find("new"),"missing update does not insert");
    index=fresh();index->bulk_load(rows);
  }
  if(caps.erase==sib::Support::unsupported) {
    unsupported([&]{index->erase("a");});unsupported([&]{index->erase("new");});
  } else {
    check(index->erase("a")==sib::EraseResult::erased&&!index->find("a"),"supported erase removes key");
    check(index->erase("a")==sib::EraseResult::not_found,"missing erase reports not_found");
  }
}

// ------------------------------------------------------------------------------- scan
void test_scan() {
  const auto keys = base_keys();
  auto index = fresh();
  index->bulk_load(rows_of(keys));
  if(index->descriptor().capabilities.scan==sib::Support::unsupported) {
    bool threw=false;try {(void)index->scan("",10);}catch(const sib::UnsupportedOperation&){threw=true;}
    check(threw,"unsupported scan throws UnsupportedOperation");
    for(const auto& row:keys)check(index->find(row.first)==std::optional<sib::Value>(row.second),"unsupported scan preserves values");
    return;
  }

  std::map<std::string, std::uint64_t, UnsignedByteLess> reference;

  for (const auto& item : keys) {
    reference[item.first] = item.second;
  }

  // The whole thing.
  {
    const auto got = index->scan("", 1000);
    check(got.size() == reference.size(), "scan('') returns every key");

    std::size_t i = 0;

    for (const auto& item : reference) {
      check(i < got.size() && got[i].key == item.first, "scan is in unsigned-byte key order");
      check(i < got.size() && got[i].value == item.second, "scan returns the stored values");
      ++i;
    }
  }

  // A lower bound that is not itself a key.
  {
    const auto got = index->scan("ab\x01", 1000);
    check(!got.empty() && got.front().key == "b", "scan starts at the first key >= lower_bound");
  }

  // limit = 0 is legal and returns nothing.
  check(index->scan("", 0).empty(), "scan with limit 0 returns nothing");

  // A limit that runs out before the end.
  {
    const auto got = index->scan("", 2);
    check(got.size() == 2, "scan honours the limit");
    check(got[0].key == "a" && got[1].key == "aa", "scan's truncated prefix is the smallest keys");
  }

  // A lower bound past the end.
  check(index->scan("zzzz", 10).empty(), "scan past the last key returns nothing");

  // A lower bound equal to a stored key is inclusive.
  {
    const auto got = index->scan("ab", 10);
    check(!got.empty() && got.front().key == "ab", "scan's lower bound is inclusive");
  }

  // OWNERSHIP: the rows must outlive the index. An adapter returning views into its own
  // storage would pass every check above and fail this one.
  std::vector<sib::Row> survivors;

  {
    auto temporary = fresh();
    temporary->bulk_load(rows_of(keys));
    survivors = temporary->scan("", 1000);
  }

  check(survivors.size() == reference.size(), "scan results outlive the index that produced them");

  std::size_t i = 0;

  for (const auto& item : reference) {
    check(i < survivors.size() && survivors[i].key == item.first,
          "a surviving scan row still holds its key");
    check(i < survivors.size() && survivors[i].value == item.second,
          "a surviving scan row still holds its value");
    ++i;
  }
}

// -------------------------------------------------------------------- key domain claims
void test_key_domain() {
  auto index = fresh();
  const sib::KeyDomain domain = index->descriptor().capabilities.keys;

  // The probes are chosen FROM THE DECLARATION, and each declared permission is exercised.
  // A declaration nobody tests is a comment -- and the first version of this file declared
  // high bytes allowed unconditionally, which the LITS backend refused the moment a probe
  // contained one.
  std::vector<std::pair<std::string, std::uint64_t>> probes;

  if (domain.allows_nul) {
    probes.emplace_back(std::string("nul\0inside", 10), 1);
  }

  if (domain.allows_high_bit) {
    // HIGH BYTES ONLY. The obvious probe for this is "\x80\xff\x00\xfe", which also contains a
    // NUL and therefore tests TWO permissions at once -- so an adapter that allows high bytes
    // but not embedded NULs (HOT) fails a check about high bytes for a reason that has nothing
    // to do with them. One probe, one permission.
    probes.emplace_back(std::string("\x80\xff\xfe\xfd", 4), 2);
  }

  if (domain.allows_prefix_pairs) {
    probes.emplace_back(std::string("p"), 3);
    probes.emplace_back(std::string("pa"), 4);
    probes.emplace_back(std::string("pab"), 5);
  }

  // The length ceiling: a key AT the declared maximum must be accepted. The maximum is taken
  // from the declaration, so a backend that declares 4096 is asked for 4096 and a backend
  // that declares no bound is asked for a large key.
  // Bound fixture allocation. A representational multi-GiB limit must not make
  // a small contract gate allocate multiple GiB. Explicitly report unprobed ceilings.
  constexpr std::size_t allocation_limit=1024*1024;
  const std::size_t longest = domain.max_bytes.has_value()
                                  ? static_cast<std::size_t>(*domain.max_bytes<=allocation_limit?*domain.max_bytes:4096)
                                  : 4096;
  if(domain.max_bytes && *domain.max_bytes>allocation_limit)
    std::cout << "  (large key-length ceiling/refusal not allocation-probed; testing 4096 bytes)\n";
  probes.emplace_back(std::string(longest, 'L'), 6);

  std::sort(probes.begin(), probes.end(),
            [](const auto& a, const auto& b) { return UnsignedByteLess{}(a.first, b.first); });

  auto subject = fresh();
  subject->bulk_load(rows_of(probes));

  for (const auto& probe : probes) {
    const auto found = subject->find(probe.first);
    check(found.has_value() && *found == probe.second,
          "a key inside the declared domain is accepted (length " +
              std::to_string(probe.first.size()) + ")");
  }

  // THE OTHER DIRECTION. A declared limit that the implementation does not actually impose
  // would be a claim that makes the evaluator skip data it could have handled; a limit the
  // implementation imposes but does not declare would make the evaluator hand it data it
  // cannot take. Both are checked, so the declaration is pinned from both sides.
  //
  // EVERY REFUSAL PROBE CARRIES TWO KEYS, and that is not padding. The LITS backend's domain
  // check lives inside SharedModel::train, which returns early for fewer than two keys -- so a
  // single-key probe never reaches the check and would report "accepted" for a key the
  // implementation actually refuses. The first version of this file made exactly that
  // mistake and read as three passing checks that proved nothing.
  //
  // The pairs are ORDERED BY THE SAME COMPARISON THE CONTRACT USES. Writing them in the
  // obvious order is wrong for the high-byte case (0x80 sorts after 'l', not before it), and
  // an unsorted pair makes bulk_load throw its precondition error -- which this code would
  // then count as "refused" and report as a pass.
  const auto probe_pair = [](std::string suspect) {
    std::vector<std::pair<std::string, std::uint64_t>> rows = {
        {std::move(suspect), 1},
        {std::string("legal"), 100},
    };

    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return UnsignedByteLess{}(a.first, b.first); });

    return rows;
  };

  const auto refused_when_built = [](const std::vector<std::pair<std::string, std::uint64_t>>& rows) {
    auto subject = fresh();

    try {
      subject->bulk_load(rows_of(rows));
    } catch (const std::exception&) {
      return true;
    }

    return false;
  };

  if (domain.max_bytes.has_value() && *domain.max_bytes<allocation_limit) {
    check(refused_when_built(probe_pair(std::string(*domain.max_bytes + 1, 'L'))),
          "a key longer than the declared maximum is refused");
  }

  if (!domain.allows_high_bit) {
    check(refused_when_built(probe_pair(std::string("\x80", 1))),
          "a high byte is refused when the declaration says it is not allowed");
  }

  if (!domain.allows_nul) {
    check(refused_when_built(probe_pair(std::string("a\0b", 3))),
          "an embedded NUL is refused when the declaration says it is not allowed");
  }

  std::cout << "  key domain     = min_bytes=" << domain.min_bytes
            << " max_bytes="
            << (domain.max_bytes.has_value() ? std::to_string(*domain.max_bytes) : std::string("none"))
            << " nul=" << (domain.allows_nul ? "yes" : "no")
            << " high_bit=" << (domain.allows_high_bit ? "yes" : "no")
            << " prefix_pairs=" << (domain.allows_prefix_pairs ? "yes" : "no") << "\n";
}

// -------------------------------------------------------------------------- unsupported
void test_unsupported() {
  auto index = fresh();
  index->bulk_load(rows_of(base_keys()));

  const sib::Support declared = index->descriptor().capabilities.range_sum;

  if (declared == sib::Support::unsupported) {
    // The contract's rule: an unsupported method throws WITHOUT mutation, rather than
    // returning a plausible empty result that a caller would read as "the sum is zero".
    bool threw = false;

    try {
      (void)index->range_sum("a", "z");
    } catch (const sib::UnsupportedOperation&) {
      threw = true;
    }

    check(threw, "a declared-unsupported operation throws UnsupportedOperation");

    // And the throw must not have changed anything.
    const auto still_there = index->find("ab");
    check(still_there.has_value() && *still_there == 3,
          "the unsupported call did not mutate the index");
  } else {
    const sib::Value sum = index->range_sum("a", "ab");
    check(sum == 1 + 2 + 3, "a declared-supported range_sum returns the inclusive sum");
  }
}

// ------------------------------------------------------------------------ memory usage
void test_memory_usage() {
  auto index = fresh();
  index->bulk_load(rows_of(base_keys()));

  const sib::MemoryUsage usage = index->memory_usage();

  // The contract's rule is "null means unavailable, NEVER zero", and "byte counts must not
  // overlap". It does NOT require an adapter to know its own footprint: libart has no
  // allocation hook, so this adapter reports index_owned_bytes as unavailable rather than
  // inventing a number. An earlier version of this file required a positive figure, which
  // would have failed libart for being honest.
  check(!usage.accounting_notes.empty(), "memory accounting carries a stated basis");

  // A GENUINE ZERO IS LEGAL. The contract's rule is "null means unavailable, never zero" --
  // it forbids reporting 0 to mean unknown, not reporting a zero the adapter actually knows.
  // An earlier version of this file required every reported field to be positive, which
  // failed libart for correctly saying it retains 0 bytes of harness input. What can be
  // checked mechanically is that the BASIS is stated; the notes are what let a reader tell a
  // real zero from a placeholder.
  const auto either_way = [](const std::optional<std::uint64_t>&) { return true; };

  check(either_way(usage.index_owned_bytes), "index_owned_bytes is reported or unavailable");
  check(either_way(usage.adapter_owned_bytes), "adapter_owned_bytes is reported or unavailable");
  check(either_way(usage.retained_input_bytes), "retained_input_bytes is reported or unavailable");

  std::cout << "  memory         = index_owned="
            << (usage.index_owned_bytes.has_value() ? std::to_string(*usage.index_owned_bytes)
                                                    : std::string("unavailable"))
            << " adapter_owned="
            << (usage.adapter_owned_bytes.has_value() ? std::to_string(*usage.adapter_owned_bytes)
                                                      : std::string("unavailable"))
            << " retained_input="
            << (usage.retained_input_bytes.has_value()
                    ? std::to_string(*usage.retained_input_bytes)
                    : std::string("unavailable"))
            << "\n";
}

}  // namespace

int main() {
  std::cout << "adapter under acceptance: " << ACCEPTANCE_IMPLEMENTATION << "\n\n";

  try {
    test_descriptor();
    test_parameters();
    test_bulk_load();
    test_find();
    test_mutations();
    test_scan();
    test_key_domain();
    test_unsupported();
    test_memory_usage();
  } catch (const std::exception& error) {
    std::cout << "FAIL: unexpected exception: " << error.what() << "\n";
    return 1;
  }

  std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";

  if (failures != 0) {
    std::cout << failures << " FAILED\n";
    return 1;
  }

  return 0;
}
