cmake_minimum_required(VERSION 3.16)
project(star_align LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Collect all .cpp files from dss/ then selectively include
file(GLOB_RECURSE DSS_ALL_SOURCES "dss/*.cpp")

# Exclude files that depend on external libraries orcomplex types not yet integrated
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*FITSUtil\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*TIFFUtil\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*avx_luminance\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*avx_simd_check\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*avx_bitmap_util\\.cpp$")
# Exclude files with deep dependencies not yet stubbed
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*BitmapExt\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*DarkFrame\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*FlatFrame\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*MasterFrames\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*Multitask\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*StackingTasks\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*TaskInfo\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*Workspace\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*FrameInfo\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*Filters\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*DSSProgress\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*ColorHelpers\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*BackgroundCalibration\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*ColorBitmap\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*GrayBitmap\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*RegisterEngine\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*MatchingStars\\.cpp$")

add_library(dss STATIC ${DSS_ALL_SOURCES})
target_include_directories(dss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dss)

add_subdirectory(libraw)

add_executable(test_main main.cpp)
target_link_libraries(test_main PRIVATE dss raw)
