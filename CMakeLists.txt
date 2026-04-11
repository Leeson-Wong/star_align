cmake_minimum_required(VERSION 3.16)
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
set(VCPKG_BIN_DIR "D:/environment/vcpkg/installed/x64-windows/bin")

# White balance test executable
set(OpenCV_ROOT "D:/environment/vcpkg/installed/x64-windows/share/opencv4")
find_package(OpenCV REQUIRED)
add_executable(test_white_balance test_white_balance.cpp)
target_include_directories(test_white_balance PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(test_white_balance PRIVATE raw ${OpenCV_LIBS})

# Copy DLLs to build output directory at build time
# Use generator expression to pick debug/release bin dir automatically
set(VCPKG_BIN_DIR_DEBUG "D:/environment/vcpkg/installed/x64-windows/debug/bin")
add_custom_command(TARGET test_white_balance POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<IF:$<CONFIG:Debug>,${VCPKG_BIN_DIR_DEBUG},${VCPKG_BIN_DIR}>"
        "$<TARGET_FILE_DIR:test_white_balance>"
    COMMENT "Copying vcpkg DLLs to output directory"
)
