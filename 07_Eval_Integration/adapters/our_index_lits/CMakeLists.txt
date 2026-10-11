# Adapter package: the same index implementation on the LITS model backend.
#
# WHY THIS IS A SEPARATE PACKAGE RATHER THAN A PARAMETER. The evaluator's runner passes an
# empty parameter list today, so a backend selected by a parameter would be a backend that is
# never selected -- and it would fail silently, by measuring Context16 when the label says
# LITS. The backend is therefore a compile definition, and the two packages exist so that
# neither can be reached by accident.
#
# The implementation is NOT duplicated. This package sets the backend and delegates to the
# sibling package's CMakeLists, so the two packages cannot drift apart: there is one
# our_index_adapter.cpp and one set of build rules.

cmake_minimum_required(VERSION 3.16)
project(string_index_our_index_lits_adapter LANGUAGES CXX)

set(OUR_INDEX_BACKEND "lits" CACHE STRING "" FORCE)

# The sibling directory defaults OUR_INDEX_ISA to x86-64-v3 and that default is what the
# matrix uses; it is not overridden here, so both packages are built at the same ISA level.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../our_index" our_index_impl)

# The runner includes the factory header by name, and that header lives in the sibling
# package. Re-publishing the directory here keeps the manifest's factory_header honest: it
# names a header the runner can actually reach from THIS package.
target_include_directories(our_index_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../our_index")
