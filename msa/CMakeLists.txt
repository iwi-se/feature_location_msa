cmake_minimum_required(VERSION 3.16)

project(feature_location_msa LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(TBB tbb)
find_library(TREE_SITTER_LIB tree-sitter)
find_library(TREE_SITTER_CPP_LIB NAMES tree-sitter-cpp)
find_library(TREE_SITTER_JAVA_LIB NAMES tree-sitter-java)


file(GLOB SRC_FILES
    src/*.cpp
)

add_executable(feature_location_msa ${SRC_FILES})

target_link_libraries(feature_location_msa PRIVATE ${TBB} ${TREE_SITTER_LIB} ${TREE_SITTER_CPP_LIB} ${TREE_SITTER_JAVA_LIB})

target_include_directories(feature_location_msa PRIVATE src)
