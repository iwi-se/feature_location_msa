cmake_minimum_required(VERSION 3.16)

find_library(TREE_SITTER_LIB      tree-sitter)
find_library(TREE_SITTER_CPP_LIB  tree-sitter-cpp)
find_library(TREE_SITTER_JAVA_LIB tree-sitter-java)

add_library(ts_shared STATIC tree.cpp parser.cpp)

target_include_directories(ts_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ts_shared PUBLIC
    ${TREE_SITTER_LIB}
    ${TREE_SITTER_CPP_LIB}
    ${TREE_SITTER_JAVA_LIB}
)

set_target_properties(ts_shared PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
