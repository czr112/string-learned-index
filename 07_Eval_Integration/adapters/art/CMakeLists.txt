# Adapter package for libart (string-index/v1).
#
# THE LIBRARY SOURCE IS NOT COPIED INTO THIS PACKAGE. It is read from the upstream round's
# vendored copy, whose bytes are pinned by fetch_libart.py against a specific commit. Reading
# it in place means the evaluator builds exactly the bytes that script verified, and the
# manifest's source_identity records the digests so the result file can be traced back to them.
#
# -lmarch IS SET HERE AND MATCHES THE OTHER ARMS. The three arms of this round must be built
# at the same ISA level for their timings to be comparable at all; libart itself has no ISA
# requirement, so it simply inherits the round's level rather than imposing one.

cmake_minimum_required(VERSION 3.16)
project(string_index_art_adapter LANGUAGES C CXX)

set(ART_ISA "x86-64-v3" CACHE STRING "ISA level; must match the other arms of the round")

set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_C_FLAGS_RELEASE "-O3")

set(LIBART_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src")

if(NOT EXISTS "${LIBART_DIR}/art.c")
  message(FATAL_ERROR "vendored libart not found at ${LIBART_DIR}")
endif()

add_library(art_adapter STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/art_adapter.cpp"
  "${LIBART_DIR}/art.c")

target_compile_features(art_adapter PUBLIC cxx_std_17)

# The factory header the runner includes by name.
target_include_directories(art_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# libart's own headers, for the adapter's `extern "C" { #include "art.h" }`.
target_include_directories(art_adapter PRIVATE "${LIBART_DIR}")

if(DEFINED STRING_INDEX_CONTRACT_INCLUDE_DIR)
  target_include_directories(art_adapter PUBLIC "${STRING_INDEX_CONTRACT_INCLUDE_DIR}")
else()
  target_include_directories(art_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../learned-index/src")
endif()

# PUBLIC, NOT PRIVATE. The evaluator's runner links this target, and the runner contains the
# query loop and the timing. With PRIVATE, libart got -march=x86-64-v3 while the runner was
# built at CMake's default -- so two arms of the same round ran their measured loop at
# different ISA levels. The ISA is a property of the ROUND, so it propagates.
target_compile_options(art_adapter PUBLIC "-march=${ART_ISA}")

# libart is C99 and its own build uses warnings this adapter does not need to inherit.
set_source_files_properties("${LIBART_DIR}/art.c" PROPERTIES COMPILE_OPTIONS "-w")

option(ART_BUILD_ACCEPTANCE "Build the adapter interface acceptance program" OFF)

if(ART_BUILD_ACCEPTANCE)
  # The SAME acceptance program the other arms use, so all three are held to one bar.
  add_executable(art_adapter_acceptance
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/contract_acceptance.cpp")

  target_link_libraries(art_adapter_acceptance PRIVATE art_adapter)

  target_compile_definitions(art_adapter_acceptance PRIVATE
    ACCEPTANCE_FACTORY_HEADER="art_adapter.h"
    ACCEPTANCE_FACTORY_SYMBOL=make_art_index)
endif()
