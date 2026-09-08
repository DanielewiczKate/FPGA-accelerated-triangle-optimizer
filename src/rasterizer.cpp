#include "rasterizer.hpp"
#include <cstdlib>
#ifdef DUMP
#include <iostream>
#include <string>
#include <format>
#include <fstream>
#ifndef TRIOPT_DUMP_PATH          // set by CMake -DTRIOPT_DUMP=ON to an absolute path
#define TRIOPT_DUMP_PATH "render_dump.txt"
#endif
#endif

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

void RasterizeTriangleV2(ImageData& image, const Triangle& triangle) {
    pcrd_t x_size = image.x_size();
    pcrd_t y_size = image.y_size();
    const PixelBounds tri_bounds = triangle.bounds();
    if (tri_bounds.x_max >= x_size) std::abort();
    if (tri_bounds.y_max >= y_size) std::abort();
    const auto& vx = triangle.verts_x;
    const auto& vy = triangle.verts_y;
    const Color col = triangle.color;
    Color* data = image.data();

    // Per-edge step deltas (constant across the whole triangle).
    const int64_t A0 = (int64_t)vy[1] - vy[0], B0 = (int64_t)vx[0] - vx[1];
    const int64_t A1 = (int64_t)vy[2] - vy[1], B1 = (int64_t)vx[1] - vx[2];
    const int64_t A2 = (int64_t)vy[0] - vy[2], B2 = (int64_t)vx[2] - vx[0];

    // Edge function values at (x_min, y_min) — the only multiplies needed.
    int64_t d0_row = (int64_t)(tri_bounds.x_min - vx[0]) * A0
                    - (int64_t)(tri_bounds.y_min - vy[0]) * (vx[1] - vx[0]);
    int64_t d1_row = (int64_t)(tri_bounds.x_min - vx[1]) * A1
                    - (int64_t)(tri_bounds.y_min - vy[1]) * (vx[2] - vx[1]);
    int64_t d2_row = (int64_t)(tri_bounds.x_min - vx[2]) * A2
                    - (int64_t)(tri_bounds.y_min - vy[2]) * (vx[0] - vx[2]);

#ifdef DUMP
    std::string dump_output = "[";
#endif

    for (pcrd_t y = tri_bounds.y_min; y <= tri_bounds.y_max; y++) {
        int64_t d0 = d0_row, d1 = d1_row, d2 = d2_row;
        size_t row_i = (size_t)y * x_size;

        for (pcrd_t x = tri_bounds.x_min; x <= tri_bounds.x_max; x++) {
            if ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0)) {
                size_t i = row_i + x;
                Color new_col;
                uint16_t alpha = col.a;
                new_col.r = (data[i].r * (255 - alpha) + col.r * alpha) / 255;
                new_col.g = (data[i].g * (255 - alpha) + col.g * alpha) / 255;
                new_col.b = (data[i].b * (255 - alpha) + col.b * alpha) / 255;
                new_col.a = data[i].a;
                data[i] = new_col;

#ifdef DUMP
                dump_output += std::format("({}, {}, {}, {}),",
                        i, d0, d1, d2);
#endif
            }
            d0 += A0; d1 += A1; d2 += A2;
        }
        d0_row += B0; d1_row += B1; d2_row += B2;
    }
#ifdef DUMP
    dump_output += "]";
    {
        std::ofstream f(TRIOPT_DUMP_PATH);
        if (!f) std::abort();  // dir missing or path unwritable
        f << dump_output << '\n';
    }
#endif
}
