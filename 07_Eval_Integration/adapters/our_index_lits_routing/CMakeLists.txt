# Adapter package: the same index on the LITS model backend, with the compact region-routing
# candidate ENABLED. Mirrors ../our_index_lits (which delegates to ../our_index with
# OUR_INDEX_BACKEND=lits) and adds the one variable that selects the routing arm.
cmake_minimum_required(VERSION 3.16)
project(string_index_our_index_lits_routing_adapter LANGUAGES CXX)

set(OUR_INDEX_BACKEND "lits" CACHE STRING "" FORCE)
set(OUR_INDEX_ROUTING "on" CACHE STRING "" FORCE)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../our_index" our_index_impl)

target_include_directories(our_index_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../our_index")