import hashlib
IDX = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\index.hpp"
src = open(IDX, 'r', encoding='utf-8', newline='').read()
assert '\r\n' not in src
a = '''#ifdef E25_DIAG
namespace validation {
namespace e25 {
struct Counts {'''
r = '''#ifdef E25_DIAG
// The rank model's fit timing needs a clock. E14 and E30 carry their own includes, but both of
// those blocks are compiled out unless their own switch is on, so this one does not borrow
// theirs -- and does not make <chrono> part of a timing build either.
#include <chrono>
namespace validation {
namespace e25 {
struct Counts {'''
n = src.count(a)
if n != 1:
    raise SystemExit('ANCHOR FAIL: %d' % n)
src = src.replace(a, r)
open(IDX, 'w', encoding='utf-8', newline='').write(src)
print('index.hpp diag include added, sha256', hashlib.sha256(open(IDX,'rb').read()).hexdigest())
