// Dump a corpus to a raw-lines key file for the unified evaluator.
//
// WHY A C++ DUMPER AND NOT A PYTHON ONE. make_data() (data.hpp) is the generator every
// round of this project has used, and it is seeded and deterministic. Reimplementing it in
// Python would produce a corpus that is only ALMOST the same one, and every cross-round
// comparison would then rest on a difference nobody would notice. So the keys are written
// by the same code that generated them.
//
// Ordering: make_data returns keys sorted by ORDER, which is not unsigned-byte order.
// The unified evaluator sorts and deduplicates whatever it reads, so the file order does
// not matter -- but the file must contain exactly the key SET, one key per line, with only
// the line terminator stripped. A key containing a newline byte would be read back wrong,
// so this refuses to write one rather than emitting a file that silently differs.
//
// Usage:  dump_keys KIND N OUT [TRANCOCSV]

#include "data.hpp"
#include "selected.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace validation;

int main(int argc, char** argv) {
    try {
        if (argc < 4) throw std::invalid_argument("dump_keys KIND N OUT [TRANCOCSV]");

        const std::string kind = argv[1];
        const std::size_t n = std::stoull(argv[2]);
        const std::string out_path = argv[3];
        const std::string trcsv = argc > 4 ? argv[4] : "";

        const auto keys = make_data(kind, n, trcsv);

        std::ofstream out(out_path.c_str(), std::ios::binary | std::ios::trunc);
        out.exceptions(std::ios::badbit | std::ios::failbit);

        std::uint64_t bytes = 0;
        std::size_t min_len = SIZE_MAX, max_len = 0, nul_keys = 0, nl_keys = 0, empty = 0;

        for (const auto& kv : keys) {
            const std::string& k = kv.first;

            if (k.empty()) ++empty;
            if (k.find('\0') != std::string::npos) ++nul_keys;
            if (k.find('\n') != std::string::npos || k.find('\r') != std::string::npos) ++nl_keys;

            if (k.size() < min_len) min_len = k.size();
            if (k.size() > max_len) max_len = k.size();
            bytes += k.size();

            out.write(k.data(), static_cast<std::streamsize>(k.size()));
            out.put('\n');
        }

        out.close();

        // The length distribution is printed because the round must report real key counts
        // and length statistics, not just the corpus name.
        std::cout << "{\"kind\":\"" << kind << "\",\"keys\":" << keys.size()
                  << ",\"key_bytes\":" << bytes
                  << ",\"min_len\":" << (keys.empty() ? 0 : min_len)
                  << ",\"max_len\":" << max_len
                  << ",\"mean_len\":" << (keys.empty() ? 0.0 : double(bytes) / double(keys.size()))
                  << ",\"empty_keys\":" << empty
                  << ",\"keys_with_nul\":" << nul_keys
                  << ",\"keys_with_newline\":" << nl_keys
                  << ",\"out\":\"" << out_path << "\"}" << std::endl;

        // A NUL inside a key, or an embedded newline, would make the raw-lines file
        // ambiguous on read-back. Both are refused HERE, where the cause is visible, rather
        // than discovered later as a corpus that quietly has fewer keys than it should.
        if (empty || nul_keys || nl_keys) {
            std::cerr << "FAILED: corpus contains keys the raw-lines reader cannot round-trip"
                      << std::endl;
            return 2;
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}
