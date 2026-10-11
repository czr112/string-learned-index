# Adapter package for this project's string learned index (string-index/v1).
#
# HOW THIS IS CONSUMED. The evaluator generates a top-level CMake project that does
#   add_subdirectory(<this directory> adapter-build)
# and then links its own runner against the target named in benchmark-adapter.json. So this
# file must define that target, and the include directories it publishes reach the runner
# through target_link_libraries.
#
# THE ISA IS SET HERE, PER ADAPTER, AND THAT IS NOT A STYLE CHOICE. The three arms of this
# round cannot share one -march: HOT requires AVX2 and BMI2 and cannot be compiled for
# x86-64-v2 at all. Round-wide comparability therefore comes from every arm using the SAME
# level, not from every arm inheriting the project's old level. OUR_INDEX_ISA exists so the
# diagnostic v2 build can be produced without editing this file, but the default is what
# the matrix uses.
#
# ASSERTIONS STAY ON. The project's own timing builds have never defined NDEBUG
# (tools_build.py uses -O3 with no -DNDEBUG), and several structural preconditions in the
# implementation are checked with assert. CMake's Release flags would turn them off, which
# would make this round's build silently different from every previous round's, so
# CMAKE_CXX_FLAGS_RELEASE is overridden in this directory only.

cmake_minimum_required(VERSION 3.16)
project(string_index_our_index_adapter LANGUAGES CXX)

set(OUR_INDEX_ISA "x86-64-v3" CACHE STRING
    "ISA level for this adapter; x86-64-v3 is the round's floor because HOT needs AVX2+BMI2")
set(OUR_INDEX_BACKEND "context16" CACHE STRING
    "Model backend: context16 (diagnostic control) or lits (temporary reference backend)")

set(CMAKE_CXX_FLAGS_RELEASE "-O3")

add_library(our_index_adapter STATIC "${CMAKE_CURRENT_SOURCE_DIR}/our_index_adapter.cpp")

target_compile_features(our_index_adapter PUBLIC cxx_std_17)

# The factory header the runner includes BY NAME. Without this line the adapter itself
# compiles (it includes the header relatively) and the runner does not -- which is exactly
# how this failed the first time: the library target built cleanly while the runner died on
# "our_index_adapter.h: No such file or directory".
target_include_directories(our_index_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# The contract header. The generated project sets STRING_INDEX_CONTRACT_INCLUDE_DIR to the
# evaluator's own src/ directory; the fallback keeps this package buildable on its own.
if(DEFINED STRING_INDEX_CONTRACT_INCLUDE_DIR)
  target_include_directories(our_index_adapter PUBLIC
    "${STRING_INDEX_CONTRACT_INCLUDE_DIR}")
else()
  target_include_directories(our_index_adapter PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../learned-index/src")
endif()

# The implementation itself lives in the upstream round directory, which is NOT part of the
# evaluator's repository. It is added as an external source tree: the evaluator does not
# hash it recursively, which is why the adapter manifest carries its digest explicitly.
target_include_directories(our_index_adapter PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../02_Code_and_Experiments/validation_e25_20260918")

if(OUR_INDEX_BACKEND STREQUAL "lits")
  target_compile_definitions(our_index_adapter PUBLIC OUR_INDEX_BACKEND=1)
elseif(OUR_INDEX_BACKEND STREQUAL "context16")
  target_compile_definitions(our_index_adapter PUBLIC OUR_INDEX_BACKEND=0)
else()
  message(FATAL_ERROR "OUR_INDEX_BACKEND must be context16 or lits, got '${OUR_INDEX_BACKEND}'")
endif()

# THE ROUTING CANDIDATE, PER PACKAGE. off is the shipped configuration and the default, so
# every existing target keeps compiling exactly the arm it always did. on is what the
# *_routing packages set; it flips compact_region_routing at the adapter's single Config
# site. The switch is NOT a runtime parameter because the evaluator passes none -- a
# switch selected by a parameter nobody sets would silently measure the baseline while the
# label said otherwise.
set(OUR_INDEX_ROUTING "off" CACHE STRING
  "Enable the compact region-routing candidate for this package: off (baseline) or on")

if(OUR_INDEX_ROUTING STREQUAL "on")
  target_compile_definitions(our_index_adapter PUBLIC OUR_INDEX_ROUTING_ENABLED=1)
elseif(NOT OUR_INDEX_ROUTING STREQUAL "off")
  message(FATAL_ERROR "OUR_INDEX_ROUTING must be off or on, got '${OUR_INDEX_ROUTING}'")
endif()

target_compile_options(our_index_adapter PUBLIC
  "-march=${OUR_INDEX_ISA}"
  -Wall
  -Wextra
  -Wno-unused-parameter)

# The interface acceptance program. OFF by default: the evaluator adds this directory with
# add_subdirectory and would otherwise build an executable it never runs. Turn it on to run
# the contract's optional operations -- which the evaluator itself does not check, because its
# first version validates bulk_load and find only.
#
# The program itself is SHARED with the ART and HOT packages, so all three arms are held to
# one bar rather than three hand-written ones.
option(OUR_INDEX_BUILD_ACCEPTANCE "Build the adapter interface acceptance program" OFF)

if(OUR_INDEX_BUILD_ACCEPTANCE)
  add_executable(our_index_adapter_acceptance
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/contract_acceptance.cpp")

  target_link_libraries(our_index_adapter_acceptance PRIVATE our_index_adapter)

  target_compile_definitions(our_index_adapter_acceptance PRIVATE
    ACCEPTANCE_FACTORY_HEADER="our_index_adapter.h"
    ACCEPTANCE_FACTORY_SYMBOL=make_our_index_index
    # This adapter DOES accept a named parameter, so the illegal-value rule is probed here.
    # The ART and HOT packages do not define this, and the shared program reports that rather
    # than quietly lowering the bar for them.
    # A bare identifier, stringified by the program. Quoting it here would have to survive
    # CMake AND the shell, and when the quotes are stripped the failure is a compile error
    # about an undeclared name rather than a wrong probe.
    ACCEPTANCE_SAMPLE_PARAMETER=ov_threshold)
endif()
