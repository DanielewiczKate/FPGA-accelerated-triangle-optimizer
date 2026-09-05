# doctest is a single header. This INTERFACE target just puts it on the include
# path for whatever links it; there is nothing to compile here.
#
# SYSTEM keeps doctest's own code out of our -Wall -Wextra -Wpedantic output.
add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Make doctest_discover_tests() available to the rest of the project. The two
# vendored scripts (doctest.cmake + doctestAddTests.cmake) are the upstream
# CTest-integration helpers; doctest.cmake expects its companion to sit next to
# it under this directory, which it does.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} PARENT_SCOPE)
