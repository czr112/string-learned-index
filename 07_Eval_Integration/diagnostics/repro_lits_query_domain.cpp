// Repro (parametrised): Index::scan with one start key, hex-encoded on argv.
// usage: repro_long_scan <skip|keep> <hexbytes like 61 80 ff>
#include "index.hpp"
#include "switches.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
using namespace validation;

static std::string from_hex(int argc, char** argv) {
    std::string out;
    for (int i = 2; i < argc; ++i) {
        unsigned v = 0;
        if (std::sscanf(argv[i], "%x", &v) != 1) { std::cerr << "bad hex\n"; std::exit(3); }
        out.push_back(static_cast<char>(static_cast<unsigned char>(v)));
    }
    return out;
}

int main(int argc, char** argv) {
    const bool skip = argc > 1 && std::string(argv[1]) == "skip";
    const std::string start = from_hex(argc, argv);
    Config cfg = e25sw::baseline_config(128);
    e25sw::pin_frozen_switches(cfg);
    cfg.model_backend = ModelBackend::LitsHpt;
    cfg.compact_region_routing = true;
    cfg.lits_skip_reference_table = skip;
    Index idx(cfg);
    std::vector<Pair> rows;
    rows.emplace_back("a", 1);
    rows.emplace_back(std::string(4096, 'x'), 2);
    idx.bulk_load(rows);
    std::cerr << "len=" << start.size() << " lits=" << idx.region(0).uses_lits() << " ";
    try {
        const auto out = idx.scan(start, 8);
        std::cerr << "ok rows=" << out.size() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "threw: " << e.what() << "\n";
    }
    return 0;
}