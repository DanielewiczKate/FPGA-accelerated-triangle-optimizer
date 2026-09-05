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
#include "image.hpp"

#include <string>

static std::string data_path(const char* name) {
    return std::string(TRIOPT_TEST_DATA_DIR) + "/" + name;
}

TEST_CASE("Color defaults to opaque black") {
    Color c;
    CHECK(c.r == 0);
    CHECK(c.g == 0);
    CHECK(c.b == 0);
    CHECK(c.a == 255);
}

TEST_CASE("ImageData stores x_size * y_size pixels") {
    ImageData img(4, 3);

    CHECK(img.x_size() == 4);
    CHECK(img.y_size() == 3);
    CHECK(img.size() == 12);

    SUBCASE("default fill is opaque black") {
        for (const Color& px : img) {
            CHECK(px.r == 0);
            CHECK(px.a == 255);
        }
    }

    SUBCASE("explicit fill reaches every pixel") {
        ImageData red(4, 3, Color{255, 0, 0, 255});
        for (const Color& px : red) {
            CHECK(px.r == 255);
            CHECK(px.g == 0);
        }
    }
}

TEST_CASE("LoadImageDataFromPNG loads a known PNG") {
    auto image_data = LoadImageDataFromPNG(data_path("test.png"));
    REQUIRE(image_data.has_value());
    Color* data = image_data->data();
    int x_size = image_data->x_size();
    int y_size = image_data->y_size();

    REQUIRE(x_size == 10);
    REQUIRE(y_size == 10);

    for(int x = 0; x < x_size; x++) {
        for(int y = 0; y < y_size; y++) {
            int idx = y * x_size + x;
            REQUIRE(data[idx].r == x);
            REQUIRE(data[idx].g == 128 - x + y);
            REQUIRE(data[idx].b == y);
            REQUIRE(data[idx].a == 255);
        }
    }
}

TEST_CASE("LoadImageDataFromPNG failes when loading non existant PNG") {
    auto image_data = LoadImageDataFromPNG("./tests/does_not_exist.png");
    if (image_data) FAIL("Load from png returned data for a nonexistant file");
}


// ===========================================================================
// TODO (you write these): SaveImageDataToPNG + round-trip
//
// Cases worth covering:
//   - save returns true and the file exists afterwards
//   - round-trip: save an ImageData, load it back, assert pixel-for-pixel
//     equality (this is the property that actually matters)
//   - save to an unwritable path (e.g. "/nonexistent-dir/x.png") -> false
//
// Put temp outputs somewhere disposable and clean them up, or use a path
// under the build directory.
// ===========================================================================
TEST_CASE("SaveImageDataToPNG round-trips") {
    {
        auto image_data = LoadImageDataFromPNG(data_path("test.png"));
        REQUIRE(image_data.has_value());

        REQUIRE(SaveImageDataToPNG(data_path("test_cpy.png"), *image_data)
                == true);
    }

    auto image_data = LoadImageDataFromPNG(data_path("test_cpy.png"));
    REQUIRE(image_data.has_value());
    Color* data = image_data->data();
    int x_size = image_data->x_size();
    int y_size = image_data->y_size();

    REQUIRE(x_size == 10);
    REQUIRE(y_size == 10);

    for(int x = 0; x < x_size; x++) {
        for(int y = 0; y < y_size; y++) {
            int idx = y * x_size + x;
            REQUIRE(data[idx].r == x);
            REQUIRE(data[idx].g == 128 - x + y);
            REQUIRE(data[idx].b == y);
            REQUIRE(data[idx].a == 255);
        }
    }
}
