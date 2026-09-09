cmake_minimum_required(VERSION 3.16)
project(triopt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# triopt_core is a static lib that gets linked into the shared Python bridge
# (bindings/triopt_c.cpp), so every first-party object needs to be PIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# clang-tidy / clangd read this.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# This project lives or dies on benchmark numbers, so an unoptimized default
# build would be actively misleading. Default to Release; override with
# -DCMAKE_BUILD_TYPE=RelWithDebInfo when profiling, or Debug when stepping.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

enable_testing()

add_subdirectory(third_party/stb)
add_subdirectory(third_party/doctest)

# Warning flags we want on all first-party code (not third_party/).
set(TRIOPT_WARNINGS -Wall -Wextra -Wpedantic)

# Everything except main() builds into a library, so the executable and the
# test binary link the exact same objects instead of recompiling sources.
add_library(triopt_core STATIC
    src/image.cpp
    src/SSE.cpp
    src/rasterizer.cpp
)
target_include_directories(triopt_core PUBLIC src)
target_link_libraries(triopt_core PRIVATE stb)
target_compile_options(triopt_core PRIVATE ${TRIOPT_WARNINGS})

# Render-trace dump. Compiles the DUMP instrumentation in src/rasterizer.cpp and
# points it at a fixed, CWD-independent file. OFF by default so benchmark builds
# are never affected; enable with `cmake -S . -B build -DTRIOPT_DUMP=ON`.
option(TRIOPT_DUMP "compile the render-trace dump into the rasterizer" OFF)
if(TRIOPT_DUMP)
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/tests/data")
    target_compile_definitions(triopt_core PRIVATE
        DUMP
        TRIOPT_DUMP_PATH="${CMAKE_SOURCE_DIR}/tests/data/render_dump.txt"
    )
endif()

add_executable(triopt
    src/main.cpp
)
target_link_libraries(triopt PRIVATE triopt_core)
target_compile_options(triopt PRIVATE ${TRIOPT_WARNINGS})

# Python (ctypes) bridge. A thin extern "C" shim over triopt_core so the cocotb
# / pytest reference models can call the real C++ (golden rasterizer and
# delta-SSE) instead of a Python re-implementation. Builds to
# build/libtriopt_c.so; FPGA/triopt_ref.py loads it from there.
#     cmake --build build --target triopt_c
add_library(triopt_c SHARED bindings/triopt_c.cpp)
target_link_libraries(triopt_c PRIVATE triopt_core)
target_compile_options(triopt_c PRIVATE ${TRIOPT_WARNINGS})

add_subdirectory(tests)
