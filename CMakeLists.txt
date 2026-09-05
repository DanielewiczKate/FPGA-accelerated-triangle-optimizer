cmake_minimum_required(VERSION 3.16)
project(triopt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# clang-tidy / clangd read this.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# This project lives or dies on benchmark numbers, so an unoptimized default
# build would be actively misleading. Default to Release; override with
# -DCMAKE_BUILD_TYPE=RelWithDebInfo when profiling, or Debug when stepping.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_subdirectory(third_party/stb)

add_executable(triopt
    src/main.cpp
)
target_link_libraries(triopt PRIVATE stb)
target_compile_options(triopt PRIVATE -Wall -Wextra -Wpedantic)
