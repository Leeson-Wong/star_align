cmake_minimum_required(VERSION 3.16)
project(star_align LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Collect all .cpp files from dss/ then selectively include
file(GLOB_RECURSE DSS_ALL_SOURCES "dss/*.cpp")

add_library(dss STATIC ${DSS_ALL_SOURCES})
target_include_directories(dss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dss)

target_precompile_headers(dss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dss/pch.h)

add_subdirectory(libraw)

add_executable(test_main main.cpp)
target_link_libraries(test_main PRIVATE dss raw)
