#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <algorithm>

/// Defines the width of the vertex integers. These are pixel coordinates, with
/// the top and left being 0.
using pcrd_t = uint16_t;

/// Defines the width of the color integers. Standard 0-255 colors
using col_t = uint8_t;

/// RGBA color. Each channel is stored as a col_t.
struct Color {
    col_t r = 0;
    col_t g = 0;
    col_t b = 0;
    col_t a = 255;
};
static_assert(sizeof(Color) == 4, "Color must be tightly packed for stb");

// Bounding box in pixel coordinates
struct PixelBounds {
    pcrd_t x_min = 0;
    pcrd_t x_max = 0;
    pcrd_t y_min = 0;
    pcrd_t y_max = 0;
};

/// Singular triangle, including color and vertex position
///
/// Triangles are stored as pixel coordinates. Triangle vertices must not
/// fall outside of the range of an image. This must be asserted by user.
class Triangle {
public:
    PixelBounds bounds() const {
        auto [x_min_it, x_max_it] = std::minmax_element(verts_x.begin(), verts_x.end());
        auto [y_min_it, y_max_it] = std::minmax_element(verts_y.begin(), verts_y.end());

        return {*x_min_it, *x_max_it, *y_min_it, *y_max_it};
    }
    std::array<pcrd_t, 3> verts_x = {};
    std::array<pcrd_t, 3> verts_y = {};
    Color color;
};



/// Class for raw pixel data of images
///
/// Invariant: x_size() * y_size() == size(). This is established at
/// construction. Once an ImageData class is constructed, the size of the
/// image is not mutable
///
/// Reading and writing of pixel data is to be done through direct array
/// manipulation using the data() functions. No checks are done to assert that
/// modifying data will be within the bounds of the array.
///
/// Data is unpadded pixel data. Pixel [0,0] is the top left corner, and
/// pixel [x_size() - 1, y_size() - 1] is the bottom right corner. To index into
/// the array: `[x + y * x_size()]` where the pixel of interest is [x, y]
class ImageData {
public:
    /// Constructs ImageData to a specified size and constructs the color_data
    /// array to the correct size. The data in the array is initialized to the
    /// fill value.
    ImageData(pcrd_t x_size, pcrd_t y_size, Color fill = Color {} ) :
        x_size_(x_size), y_size_(y_size),
        color_data_(
                static_cast<std::size_t>(x_size) *
                static_cast<std::size_t>(y_size),
                fill) {}

    pcrd_t x_size() const noexcept { return x_size_; }
    pcrd_t y_size() const noexcept { return y_size_; }
    std::size_t size() const noexcept { return color_data_.size(); }

    /// Mutable getter for color_data
    Color* data() noexcept { return color_data_.data(); }
    /// Constant getter for color_data
    const Color* data() const noexcept { return color_data_.data(); }

    auto begin() noexcept { return color_data_.begin(); }
    auto end()   noexcept { return color_data_.end(); }
    auto begin() const noexcept { return color_data_.begin(); }
    auto end()   const noexcept { return color_data_.end(); }
private:
    pcrd_t x_size_ = 0;
    pcrd_t y_size_ = 0;
    std::vector<Color> color_data_;
};
