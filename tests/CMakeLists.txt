# One test binary for the whole project.
#
# test_main.cpp compiles doctest's implementation + entry point once. Every
# other test_*.cpp just includes the header and registers TEST_CASEs, so
# editing a test does not recompile the ~7k-line doctest header.
add_executable(triopt_tests
    test_main.cpp
    test_image.cpp
)
target_link_libraries(triopt_tests PRIVATE triopt_core doctest)
target_compile_options(triopt_tests PRIVATE ${TRIOPT_WARNINGS})

# The test binary needs to find fixture files (e.g. tests/test.png) regardless
# of where ctest is invoked from. Hand it this directory as a compile-time string.
target_compile_definitions(triopt_tests PRIVATE
    TRIOPT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# Turn every doctest TEST_CASE into its own CTest entry, so `ctest` reports and
# filters them individually instead of showing the whole binary as one test.
# (CMAKE_MODULE_PATH gained third_party/doctest via that subdirectory.)
include(doctest)
doctest_discover_tests(triopt_tests)
