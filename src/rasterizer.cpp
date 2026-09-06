#include "rasterizer.hpp"
#include <cstdlib>

void RasterizeTriangle(ImageData& image, const Triangle& triangle) {
    pcrd_t x_size = image.x_size();
    pcrd_t y_size = image.y_size();
    const PixelBounds tri_bounds = triangle.bounds();
    if (tri_bounds.x_max >= x_size) std::abort();
    if (tri_bounds.y_max >= y_size) std::abort();

    const auto& vx = triangle.verts_x;
    const auto& vy = triangle.verts_y;
    const Color col = triangle.color;

    Color* data = image.data();

    for (pcrd_t y = tri_bounds.y_min; y <= tri_bounds.y_max; y++) {
        for (pcrd_t x = tri_bounds.x_min; x <= tri_bounds.x_max; x++) {
            // Edge v0 -> v1
            int64_t d0 = (int64_t)(x - vx[0]) * (vy[1] - vy[0])
                       - (int64_t)(y - vy[0]) * (vx[1] - vx[0]);
            // Edge v1 -> v2
            int64_t d1 = (int64_t)(x - vx[1]) * (vy[2] - vy[1])
                       - (int64_t)(y - vy[1]) * (vx[2] - vx[1]);
            // Edge v2 -> v0
            int64_t d2 = (int64_t)(x - vx[2]) * (vy[0] - vy[2])
                       - (int64_t)(y - vy[2]) * (vx[0] - vx[2]);

            if ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0)) {
                Color new_col;
                uint16_t alpha = col.a;

                size_t i = (size_t)y * x_size + x;

                new_col.r = (data[i].r * (255 - alpha) + col.r * alpha) / 255;
                new_col.g = (data[i].g * (255 - alpha) + col.g * alpha) / 255;
                new_col.b = (data[i].b * (255 - alpha) + col.b * alpha) / 255;
                new_col.a = data[i].a;

                data[i] = new_col;
            }
        }
    }
}
