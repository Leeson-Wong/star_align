cmake_minimum_required(VERSION 3.16)

file(GLOB_RECURSE LIBRAW_SOURCES src/*.cpp)

add_library(raw STATIC ${LIBRAW_SOURCES})

target_include_directories(raw PUBLIC include/)

target_compile_definitions(raw PUBLIC LIBRAW_NODLL)

target_compile_definitions(raw PRIVATE
    NO_JPEG
    NO_LCMS
    LIBRAW_LIBRARY_BUILD
)

if(WIN32)
    target_compile_definitions(raw PRIVATE _USE_MATH_DEFINES)
endif()

if(MSVC)
    target_compile_options(raw PRIVATE /W0)
endif()
