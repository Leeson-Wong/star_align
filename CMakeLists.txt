cmake_minimum_required(VERSION 3.16)

# Auto-inject vcpkg toolchain if not already set
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "E:/environment/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain")
endif()

project(star_align LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(star_align STATIC star_align.cpp star_align.h)
target_include_directories(star_align PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(libraw)

# Test executable
add_executable(test_star_align test_star_align.cpp)
target_link_libraries(test_star_align PRIVATE star_align raw)

# vcpkg DLL path
set(VCPKG_BIN_DIR "E:/environment/vcpkg/installed/x64-windows/bin")

# White balance test executable
set(OpenCV_ROOT "E:/environment/vcpkg/installed/x64-windows/share/opencv4")
find_package(OpenCV REQUIRED)
add_executable(test_white_balance test_white_balance.cpp)
target_include_directories(test_white_balance PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(test_white_balance PRIVATE raw ${OpenCV_LIBS})

# Aggressive WB sweep executable
add_executable(test_wb_aggressive test_wb_aggressive.cpp)
target_include_directories(test_wb_aggressive PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(test_wb_aggressive PRIVATE raw ${OpenCV_LIBS})

# PNG encode WB verification test
add_executable(test_png_encode_wb test_png_encode_wb.cpp)
target_include_directories(test_png_encode_wb PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(test_png_encode_wb PRIVATE raw ${OpenCV_LIBS})

# Copy DLLs to build output directory at build time
# Use generator expression to pick debug/release bin dir automatically
set(VCPKG_BIN_DIR_DEBUG "E:/environment/vcpkg/installed/x64-windows/debug/bin")
foreach(tgt test_white_balance test_wb_aggressive test_png_encode_wb)
add_custom_command(TARGET ${tgt} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<IF:$<CONFIG:Debug>,${VCPKG_BIN_DIR_DEBUG},${VCPKG_BIN_DIR}>"
        "$<TARGET_FILE_DIR:${tgt}>"
    COMMENT "Copying vcpkg DLLs to output directory"
)
endforeach()
