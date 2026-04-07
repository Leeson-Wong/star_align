cmake_minimum_required(VERSION 3.16)
project(star_align LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#add_library(dss STATIC star_align.cpp star_align.h)
#target_include_directories(dss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(libraw)

# Test executable
add_executable(main main.cpp)
target_link_libraries(main PRIVATE raw)
