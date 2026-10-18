#pragma once
#include "unified/runner.h"
#include <fstream>
#include <iterator>
#include <sstream>

namespace static_blocks {
inline std::istringstream buffered(const char* path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("cannot open static experiment input");
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(in.bad())throw std::runtime_error("failed to read static experiment input");
    return std::istringstream(std::move(bytes),std::ios::binary);
}
} // namespace static_blocks
