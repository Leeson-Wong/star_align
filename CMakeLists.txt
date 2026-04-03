cmake_minimum_required(VERSION 3.16)
project(star_align LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#add_library(star_align STATIC star_align.cpp star_align.h)
#target_include_directories(star_align PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE DSS_SOURCES "dss/*.cpp")
add_library(dss STATIC ${DSS_SOURCES})
target_include_directories(dss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dss)

add_subdirectory(libraw)

# Test executable
#add_executable(test_star_align test_star_align.cpp)
#target_link_libraries(test_star_align PRIVATE star_align raw)

add_executable(test_main main.cpp)
target_link_libraries(test_main PRIVATE dss raw)
