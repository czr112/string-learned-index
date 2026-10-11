import re
p = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\scratch_blockaudit\cmp_fit.cpp'
s = open(p, 'r', encoding='utf-8', newline='').read()

a = '''void compare(const char* name, const std::vector<std::string>& keys) {
    std::vector<std::string_view> views;'''
r = '''void compare(const char* name, const std::vector<std::string>& keys_in) {
    // Sorted input is a precondition of both fits (and of the index's regions), so the
    // battery sorts here instead of trusting each case to be written in order.
    std::vector<std::string> keys = keys_in;
    std::sort(keys.begin(), keys.end());
    std::vector<std::string_view> views;'''
assert s.count(a) == 1
s = s.replace(a, r)

a = '''    {   // coordinate collisions: same first 7 suffix bytes, differing after
        std::vector<std::string> keys;
        for (int i = 0; i < 20; ++i) keys.push_back(std::string("ABCDEFG") + char('a' + i));
        compare("coord_collisions", keys);
    }'''
r = '''    {   // TRUE coordinate collisions: the common prefix of front/back is 1 byte, so the
        // encoder reads bytes 1..7 -- and these keys differ only at byte 8.
        std::vector<std::string> keys;
        keys.push_back("a");
        for (int i = 0; i < 19; ++i)
            keys.push_back(std::string("a") + "0123456" + char('a' + i));
        // sanity: the two long keys really encode to the same coordinate
        compare("coord_collisions", keys);
    }'''
assert s.count(a) == 1
s = s.replace(a, r)

a = '''#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>'''
r = '''#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>'''
assert s.count(a) == 1
s = s.replace(a, r)

open(p, 'w', encoding='utf-8', newline='').write(s)
print('patched scratch cmp_fit.cpp')
