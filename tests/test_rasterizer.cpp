// Unit tests for the rasterizer module (src/rasterizer.{hpp,cpp}).
//
// See test_SSE.cpp for the doctest cheatsheet.

#include "doctest.h"
#include "rasterizer.hpp"
#include "SSE.hpp"
#include <vector>


const std::vector<Triangle> TEST_TRIANGLES_N32 = {
    // verts_x        verts_y        color (r,g,b,a)
    { {10, 12, 12}, {0, 12, 30}, {255, 0, 0, 255} },
    { {0, 0, 0},    {0, 0, 0},    {0, 255, 0, 255} },
};

void assert_equal_image_data(ImageData& a, ImageData& b) {
    size_t size = a.size();
    Color* data_a = a.data();
    Color* data_b = b.data();

    int acc = 0;
    for (size_t i = 0; i < size; i++) {
        acc += data_a[i].r != data_b[i].r;
        acc += data_a[i].g != data_b[i].g;
        acc += data_a[i].b != data_b[i].b;
        acc += data_a[i].a != data_b[i].a;
    }
    REQUIRE(acc == 0);
}
void compare_rasterizer_versions(
        void (*v1)(ImageData&, const Triangle&),
        void (*v2)(ImageData&, const Triangle&),
        ImageData& image,
        Triangle tri) {

    ImageData image_cpy = image;
    v1(image, tri);
    v2(image_cpy, tri);
    assert_equal_image_data(image, image_cpy);

}
TEST_CASE("RasterizeTriangle: assert equal image data helper") {
    ImageData image(2, 2);
    Triangle tri;
    tri.verts_x = {0, 1, 1};
    tri.verts_y = {0, 0, 1};
    tri.color = {255, 255, 255, 255 - 100}; // SSE counts pixels in this case

    assert_equal_image_data(image, image);
}
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


TEST_CASE("RasterizeTriangleV2: V2 matches V1") {
    const int N = 32;
    ImageData image(N, N);
    for(auto t : TEST_TRIANGLES_N32) {
        compare_rasterizer_versions(
                RasterizeTriangle,
                RasterizeTriangleV2,
                image,
                t);
    }
}

TEST_CASE("Dump Triangles") {
    const int N = 5;
    ImageData image(N, N);
    Triangle tri;

    tri.verts_x = {0, N - 1, N - 1};
    tri.verts_y = {0, 0, N - 1};
    tri.color = {255, 255, 255, 128}; // SSE counts pixels in this case
    RasterizeTriangleV2(image, tri);
}



