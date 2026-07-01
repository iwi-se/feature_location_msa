cmake_minimum_required(VERSION 3.16)

project(feature_location_msa LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(TBB tbb)

add_subdirectory(../shared shared)

file(GLOB SRC_FILES
    src/*.cpp
)

add_executable(feature_location_msa ${SRC_FILES})

target_link_libraries(feature_location_msa PRIVATE ${TBB} ts_shared)

target_include_directories(feature_location_msa PRIVATE src)
