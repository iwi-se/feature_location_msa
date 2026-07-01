cmake_minimum_required(VERSION 3.16)

project(msa_analyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(TBB REQUIRED)

add_subdirectory(../shared shared)

file(GLOB SRC_FILES
    src/*.cpp
)

add_executable(msa_analyzer ${SRC_FILES})

target_link_libraries(msa_analyzer PRIVATE TBB::tbb ts_shared)

target_include_directories(msa_analyzer PRIVATE src)
