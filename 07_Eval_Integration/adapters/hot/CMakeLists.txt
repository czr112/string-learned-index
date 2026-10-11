# Adapter package for HOT (string-index/v1).
#
# HOT IS THE REASON THIS ROUND HAS AN ISA FLOOR AT ALL. Its own CMakeLists compiles with
# -march=haswell (or -march=native) and it makes heavy use of AVX2 and BMI2; it cannot be
# built for x86-64-v2, which is what this project's earlier rounds used. Rather than give HOT
# a different compiler environment from the other two arms, EVERY arm of this round is built
# at x86-64-v3 -- and x86-64-v3 is exactly AVX2 + BMI2 + LZCNT, so it is the lowest level that
# can carry HOT. The host CPU (Intel Core Ultra 9 275HX) has avx2/bmi1/bmi2, checked before
# this decision rather than assumed.
#
# -march=native IS DELIBERATELY NOT USED even though HOT's own build prefers it: it makes the
# binary depend on the machine that built it, which a result file cannot record and a reader
# cannot reproduce.
#
# THE SOURCE IS READ FROM THE UPSTREAM CLONE, not copied in, so the bytes built are the bytes
# the commit names.

cmake_minimum_required(VERSION 3.16)
project(string_index_hot_adapter LANGUAGES CXX)

set(HOT_ISA "x86-64-v3" CACHE STRING
    "ISA level; v3 is the round's floor because HOT needs AVX2+BMI2")

set(HOT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../hot-src" CACHE PATH
    "Upstream HOT checkout")

set(CMAKE_CXX_FLAGS_RELEASE "-O3")

if(NOT EXISTS "${HOT_SOURCE_DIR}/libs/hot/single-threaded/include/hot/singlethreaded/HOTSingleThreaded.hpp")
  message(FATAL_ERROR "HOT headers not found under ${HOT_SOURCE_DIR}")
endif()

add_library(hot_adapter STATIC "${CMAKE_CURRENT_SOURCE_DIR}/hot_adapter.cpp")

target_compile_features(hot_adapter PUBLIC cxx_std_17)

target_include_directories(hot_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# HOT is header-only; these are the include roots its own build uses.
target_include_directories(hot_adapter PRIVATE
  "${HOT_SOURCE_DIR}/libs/hot/single-threaded/include"
  "${HOT_SOURCE_DIR}/libs/hot/commons/include"
  "${HOT_SOURCE_DIR}/libs/hot/rowex/include"
  "${HOT_SOURCE_DIR}/libs/idx/content-helpers/include"
  "${HOT_SOURCE_DIR}/libs/idx/map-helpers/include")

if(DEFINED STRING_INDEX_CONTRACT_INCLUDE_DIR)
  target_include_directories(hot_adapter PUBLIC "${STRING_INDEX_CONTRACT_INCLUDE_DIR}")
else()
  target_include_directories(hot_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../learned-index/src")
endif()

# The four -fno-builtin-malloc and friends are HOT's own requirement: it routes allocation
# through its own allocator and the compiler must not assume the libc calls are pure.
# -Wno-deprecated-declarations is for std::iterator and friends, which HOT uses and C++17
# deprecates -- the same source builds clean as C++14, and warning about it here would say
# nothing about this adapter.
# THE ISA IS PUBLIC AND THE REST IS PRIVATE, and the split is deliberate. The evaluator's
# runner links this target and contains the query loop and the timing, so the ISA has to
# propagate or the measured loop runs at a different level from the code it measures. The
# allocator flags below are about HOT's own allocator and must NOT reach the runner.
target_compile_options(hot_adapter PUBLIC "-march=${HOT_ISA}")

target_compile_options(hot_adapter PRIVATE
  -mbmi2
  -mlzcnt
  -faligned-new
  -fno-builtin-malloc
  -fno-builtin-calloc
  -fno-builtin-realloc
  -fno-builtin-free
  -Wno-deprecated-declarations)

option(HOT_BUILD_ACCEPTANCE "Build the adapter interface acceptance program" OFF)

if(HOT_BUILD_ACCEPTANCE)
  # The SAME acceptance program the other arms use, so all three are held to one bar.
  add_executable(hot_adapter_acceptance
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/contract_acceptance.cpp")

  target_link_libraries(hot_adapter_acceptance PRIVATE hot_adapter)

  target_compile_definitions(hot_adapter_acceptance PRIVATE
    ACCEPTANCE_FACTORY_HEADER="hot_adapter.h"
    ACCEPTANCE_FACTORY_SYMBOL=make_hot_index)
endif()
