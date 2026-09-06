// Unit tests for the SSE module (src/SSE.{hpp,cpp}).
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
#include "rasterizer.hpp"
#include "SSE.hpp"


TEST_CASE("compute_SSE: candidate is identical to target") {
    // SSE should be 0
    ImageData target(10, 10, Color{0, 0, 0, 255});
    ImageData candidate(10, 10, Color{0, 0, 0, 255});

    REQUIRE(compute_SSE(target, candidate) == 0);
}

TEST_CASE("compute_SSE: candidate is identical to target with different alpha") {
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

TEST_CASE("compute_SSE: indexing check on 2x2 image") {
    ImageData target(2, 2);
    ImageData candidate(2, 2);

    Color* t = target.data();
    Color* c = candidate.data();

    t[0] = Color{128, 118,  52,  85};  c[0] = Color{128, 118,  52,  85};
    t[1] = Color{ 10, 200, 255, 255};  c[1] = Color{ 40, 200, 100,   0};
    t[2] = Color{  0,   0,   0, 255};  c[2] = Color{  5,   9,   1, 255};
    t[3] = Color{255, 255, 255, 255};  c[3] = Color{  0, 255, 128, 255};

    uint64_t px_0 =
        (t[0].r - c[0].r) * (t[0].r - c[0].r) +
        (t[0].g - c[0].g) * (t[0].g - c[0].g) +
        (t[0].b - c[0].b) * (t[0].b - c[0].b);
    uint64_t px_1 =
        (t[1].r - c[1].r) * (t[1].r - c[1].r) +
        (t[1].g - c[1].g) * (t[1].g - c[1].g) +
        (t[1].b - c[1].b) * (t[1].b - c[1].b);
    uint64_t px_2 =
        (t[2].r - c[2].r) * (t[2].r - c[2].r) +
        (t[2].g - c[2].g) * (t[2].g - c[2].g) +
        (t[2].b - c[2].b) * (t[2].b - c[2].b);
    uint64_t px_3 =
        (t[3].r - c[3].r) * (t[3].r - c[3].r) +
        (t[3].g - c[3].g) * (t[3].g - c[3].g) +
        (t[3].b - c[3].b) * (t[3].b - c[3].b);

    const uint64_t expected = px_0 + px_1 + px_2 + px_3;
    REQUIRE(compute_SSE(target, candidate) == expected);
}

TEST_CASE("compute_delta_SSE: delta inequality works") {
    // compute_SSE(target, candidate) = compute_SSE(target, prev_best) +
    //     compute_delta_SSE(target, prev_best, candidate, bounding_box)

    const int N = 32;
    ImageData target(N, N);
    {
        Triangle tri;

        tri.verts_x = {N - 1, N - 1, 0};
        tri.verts_y = {N - 1, 0, N - 1};
        tri.color = {255, 0, 0, 255};

        RasterizeTriangle(target, tri);
    }

    ImageData prev_best(N, N);
    {
        Triangle tri;

        tri.verts_x = {N - 1, N - 1, 0};
        tri.verts_y = {N - 1, 0, N - 1};
        tri.color = {128, 0, 0, 255}; // right shape wrong shade

        RasterizeTriangle(prev_best, tri);
    }

    ImageData candidate(N, N);
    Triangle tri;

    tri.verts_x = {N - 1, N - 1, 0};
    tri.verts_y = {N - 1, 0, N - 1};
    tri.color = {255, 10, 0, 128};

    RasterizeTriangle(candidate, tri);

    REQUIRE(compute_SSE(target, candidate) ==
            compute_SSE(target, prev_best) +
            compute_delta_SSE(target, prev_best, candidate, tri.bounds()));
}
