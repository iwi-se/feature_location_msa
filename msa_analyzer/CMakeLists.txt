cmake_minimum_required(VERSION 3.16)

project(msa_analyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(TREE_SITTER_LIB tree-sitter)
find_library(TREE_SITTER_CPP_LIB tree-sitter-cpp)
find_library(TREE_SITTER_JAVA_LIB tree-sitter-java)


file(GLOB SRC_FILES
    src/*.cpp
)

add_executable(msa_analyzer ${SRC_FILES})

target_link_libraries(msa_analyzer PRIVATE ${TREE_SITTER_LIB} ${TREE_SITTER_CPP_LIB} ${TREE_SITTER_JAVA_LIB})

target_include_directories(msa_analyzer PRIVATE src)
