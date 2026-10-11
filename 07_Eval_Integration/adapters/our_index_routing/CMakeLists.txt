# Adapter package: the SAME implementation, the SAME adapter source and the SAME build rules
# as ../our_index, with the compact region-routing candidate ENABLED.
#
# WHY A SEPARATE PACKAGE. The evaluator passes an empty parameter list, so a candidate
# selected by a runtime parameter would never be selected -- it would measure the baseline
# while the label claimed otherwise. The switch is therefore a compile definition of THIS
# package, and the baseline package stays the arm that produced the earlier matrices
# (it compiles without the definition and asserts the switch is off).
cmake_minimum_required(VERSION 3.16)
project(string_index_our_index_routing_adapter LANGUAGES CXX)

set(OUR_INDEX_BACKEND "context16" CACHE STRING "" FORCE)
set(OUR_INDEX_ROUTING "on" CACHE STRING "" FORCE)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../our_index" our_index_impl)

# The runner includes the factory header by name; republishing the sibling directory keeps
# the manifest's factory_header honest for THIS package too.
target_include_directories(our_index_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../our_index")