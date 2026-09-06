// Unit tests for the rasterizer module (src/rasterizer.{hpp,cpp}).
//
// See test_SSE.cpp for the doctest cheatsheet.

#include "doctest.h"
#include "rasterizer.hpp"
#include "SSE.hpp"
TEST_CASE("RasterizeTriangle: alpha blending") {
    ImageData image(2, 2);
    Triangle tri;
    tri.verts_x = {0, 1, 1};
    tri.verts_y = {0, 0, 1};
    tri.color = {255, 255, 255, 255 - 100}; // SSE counts pixels in this case

    RasterizeTriangle(image, tri);

    REQUIRE(image.data()[0].r == 255 - 100);
    REQUIRE(image.data()[0].g == 255 - 100);
    REQUIRE(image.data()[0].b == 255 - 100);
}

TEST_CASE("RasterizeTriangle: invisible triangle") {
    ImageData target(2, 2);
    ImageData image(2, 2);
    Triangle tri;
    tri.verts_x = {0, 1, 1};
    tri.verts_y = {0, 0, 1};
    tri.color = {255, 255, 255, 0}; // SSE counts pixels in this case

    RasterizeTriangle(image, tri);

    REQUIRE(compute_SSE(target, image) == 0);
}
TEST_CASE("RasterizeTriangle: pixel bounds, upper left tri") {
    const int N = 32;
    ImageData target(N, N);
    ImageData image(N, N);
    Triangle tri;
    tri.verts_x = {0, N - 1, N - 1};
    tri.verts_y = {0, 0, N - 1};
    tri.color = {1, 0, 0, 255}; // SSE counts pixels in this case

    RasterizeTriangle(image, tri);

    REQUIRE(compute_SSE(target, image) == N*(N+1)/2);
}
TEST_CASE("RasterizeTriangle: pixel bounds, lower right tri") {
    const int N = 32;
    ImageData target(N, N);
    ImageData image(N, N);
    Triangle tri;

    tri.verts_x = {N - 1, N - 1, 0};
    tri.verts_y = {N - 1, 0, N - 1};
    tri.color = {1, 0, 0, 255}; // SSE counts pixels in this case

    RasterizeTriangle(image, tri);

    REQUIRE(compute_SSE(target, image) == N*(N+1)/2);
}

TEST_CASE("RasterizeTriangle: triangle shape") {
    const int N = 32;
    ImageData target(N, N);
    ImageData image(N, N);
    Triangle tri;

    tri.verts_x = {0, N - 1, N - 1};
    tri.verts_y = {0, 0, N - 1};
    tri.color = {1, 0, 0, 255}; // SSE counts pixels in this case

    RasterizeTriangle(image, tri);

    Color* data = target.data();

    pcrd_t x_size = image.x_size();
    pcrd_t y_size = image.y_size();

    for (int y = 0; y < y_size; y++) {
        for (int x = 0; x < x_size; x++) {
            if(!(y <= x)) continue;
            data[y * x_size + x] = Color{1, 0, 0, 255};
        }
    }

    REQUIRE(compute_SSE(target, image) == 0);
}

TEST_CASE("RasterizeTriangle: alpha preservation") {
    const int N = 32;
    ImageData image(N, N, Color{0, 0, 0, 128});
    Triangle tri;

    tri.verts_x = {0, N - 1, N - 1};
    tri.verts_y = {0, 0, N - 1};
    tri.color = {255, 255, 255, 255}; // SSE counts pixels in this case

    RasterizeTriangle(image, tri);

    REQUIRE(image.data()[0].a == 128);
}
