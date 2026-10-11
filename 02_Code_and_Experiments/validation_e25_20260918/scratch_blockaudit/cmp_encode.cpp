// Cross-check: the teammate's own encode() (upstream commit 60837a88) against ours.
// Prints one line per input so the numbers can be pinned in check_block_rank.cpp.
#include "../../Learned-Index-Model/src/key_bytes.h"   // blocklearn::encode
#include "model/block_linear_rank.hpp"                   // blockrank::encode

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

int main() {
    std::vector<std::pair<const char*, std::string>> cases;
    std::string longkey(4095, 'x');
    std::string nul1(1, '\0');
    std::string nul2; nul2.push_back('\0'); nul2.push_back('a');
    std::string hi80(1, static_cast<char>(0x80));
    std::string hiff(1, static_cast<char>(0xff));
    std::string hi_mix; hi_mix.push_back(static_cast<char>(0x80)); hi_mix.push_back('a');
    std::string ab_pref = "abcdefgh";  // 8 bytes: 7 encoded + 1 beyond

    cases.emplace_back("empty", "");
    cases.emplace_back("a", "a");
    cases.emplace_back("ab", "ab");
    cases.emplace_back("b", "b");
    cases.emplace_back("nul", nul1);
    cases.emplace_back("nul_a", nul2);
    cases.emplace_back("hi80", hi80);
    cases.emplace_back("hiff", hiff);
    cases.emplace_back("hi80_a", hi_mix);
    cases.emplace_back("abcdefgh", ab_pref);
    cases.emplace_back("long_4095x", longkey);

    std::uint64_t mism = 0;
    std::printf("%-12s %-20s %-20s %s\n", "case", "upstream", "ours", "same");
    for (const auto& c : cases) {
        const std::uint64_t up = blocklearn::encode(c.second, 0, 7);
        const std::uint64_t us = blockrank::encode(c.second, 0, 7);
        mism += (up != us);
        std::printf("%-12s 0x%016llx 0x%016llx %s\n", c.first,
                    static_cast<unsigned long long>(up),
                    static_cast<unsigned long long>(us), up == us ? "yes" : "NO");
    }
    // Prefix variant: skip 2 bytes.
    const std::string pk = "zzabcdefgh";
    const std::uint64_t up2 = blocklearn::encode(pk, 2, 7);
    const std::uint64_t us2 = blockrank::encode(pk, 2, 7);
    std::printf("prefix2     0x%016llx 0x%016llx %s\n",
                static_cast<unsigned long long>(up2), static_cast<unsigned long long>(us2),
                up2 == us2 ? "yes" : "NO");
    mism += (up2 != us2);
    std::printf("mismatches=%llu\n", static_cast<unsigned long long>(mism));
    return mism ? 1 : 0;
}
