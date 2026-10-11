#include "../../model/equal_depth_dir.hpp"
#include <iostream>
#include <limits>
#include <string>

// Planning audit only: compare the production mapping with an independent
// unsigned-byte count. No timers, no changes to the index or its models.
int main() {
    std::vector<std::string> owned;
    for (unsigned b : {1u, 127u, 128u, 255u})
        owned.emplace_back(1, static_cast<char>(b));
    std::vector<std::string_view> keys(owned.begin(), owned.end());
    std::cout << "char_signed=" << std::numeric_limits<char>::is_signed << '\n';
    for (bool trunc : {false, true}) {
        const auto model = equivdir::RegionModel::fit(keys.data(), keys.size(), 0, 4, trunc);
        unsigned mismatches = 0, inversions = 0, previous = 0;
        for (unsigned b = 0; b < 256; ++b) {
            const std::string q(1, static_cast<char>(b));
            unsigned expected = 0;
            // Each threshold is one byte, so integer byte comparison is the
            // independent definition of the required order for this fixture.
            for (unsigned threshold : {127u, 128u, 255u})
                expected += threshold <= b;
            const auto actual = model.predict_group(q);
            mismatches += actual != expected;
            inversions += b && actual < previous;
            previous = actual;
            if (b == 1 || b == 127 || b == 128 || b == 255)
                std::cout << "trunc=" << trunc << " byte=" << b
                          << " expected=" << expected << " actual=" << actual << '\n';
        }
        std::cout << "trunc=" << trunc << " queries=256 mismatches=" << mismatches
                  << " inversions=" << inversions << '\n';
    }
}
