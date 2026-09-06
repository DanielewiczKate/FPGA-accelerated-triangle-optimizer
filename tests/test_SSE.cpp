// Unit tests for the image module (src/image.{hpp,cpp}).
//
// ---------------------------------------------------------------------------
// doctest cheatsheet
// ---------------------------------------------------------------------------
//   TEST_CASE("name") { ... }   A test. Each runs with its own fresh locals.
//   SUBCASE("name") { ... }     Inside a TEST_CASE: the code from the top of
//                               the TEST_CASE down to here re-runs once per
//                               sibling SUBCASE. Use for "same setup, a few
//                               variations" without repeating the setup.
//   CHECK(expr)                 Non-fatal assertion; test keeps running.
//   REQUIRE(expr)               Fatal assertion; aborts this TEST_CASE.
//   CHECK(a == b)               On failure, prints the two operand values.
//   CHECK_FALSE / REQUIRE_FALSE
//   CHECK_THROWS / CHECK_NOTHROW / CHECK_THROWS_AS(expr, Type)
//   doctest::Approx(x)          Float compare with tolerance: CHECK(f == Approx(0.1));
//   TEST_CASE("name" * doctest::skip())   Registered but not run.
//   FAIL("msg")                 Unconditional failure (used below as a stub).
//
// Run:  cmake --build <builddir> && ctest --test-dir <builddir> --output-on-failure
// Or run the binary directly for filtering:  ./tests/triopt_tests -tc="ImageData*"

#include "doctest.h"

#include "common.hpp"
#include "SSE.hpp"


TEST_CASE("compute_SSE: candidate is identical to target") {
    // SSE should be 0
    ImageData target(10, 10, Color{0, 0, 0, 255});
    ImageData candidate(10, 10, Color{0, 0, 0, 255});

    REQUIRE(compute_SSE(target, candidate) == 0);
}

TEST_CASE("compute_SSE: candidate is identical to target with differnt alpha") {
    // SSE should be 0
    ImageData target(10, 10, Color{0, 0, 0, 0});
    ImageData candidate(10, 10, Color{0, 0, 0, 255});

    REQUIRE(compute_SSE(target, candidate) == 0);
}

TEST_CASE("compute_SSE: full black target vs full white candidate") {
    // SSE should be x_size * y_size * (255 ^ 2) * 3

    ImageData target(10, 10, Color{0, 0, 0, 255});
    ImageData candidate(10, 10, Color{255, 255, 255, 255});

    REQUIRE(compute_SSE(target, candidate) == 10 * 10 * 255 * 255 * 3);
}


