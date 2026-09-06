#include "rasterizer.hpp"
#include <cstdlib>
#include <algorithm>
void RasterizeTriangle(ImageData& image, const Triangle& triangle) {
    pcrd_t x_size = image.x_size();
    pcrd_t y_size = image.y_size();
    const PixelBounds tri_bounds = triangle.bounds();
    if (tri_bounds.x_max >= x_size) std::abort();
    if (tri_bounds.y_max >= y_size) std::abort();

    const auto& vx = triangle.verts_x;
    const auto& vy = triangle.verts_y;
    const Color col = triangle.color;

    // Edge function coefficients (integer)
    int64_t A0 = vy[1] - vy[0], B0 = vx[0] - vx[1];
    int64_t A1 = vy[2] - vy[1], B1 = vx[1] - vx[2];
    int64_t A2 = vy[0] - vy[2], B2 = vx[2] - vx[0];

    // Evaluate at pixel centers using doubled coordinates to stay integer
    // (x + 0.5, y + 0.5) -> (2x + 1, 2y + 1), edge sign unaffected by the 2x scale
    int64_t x0 = tri_bounds.x_min, y0 = tri_bounds.y_min;

    int64_t C0 = A0 * (2 * x0 + 1) + B0 * (2 * y0 + 1)
               - 2 * (vx[0] * (vy[1] - vy[0]) - vy[0] * (vx[1] - vx[0]));
    int64_t C1 = A1 * (2 * x0 + 1) + B1 * (2 * y0 + 1)
               - 2 * (vx[1] * (vy[2] - vy[1]) - vy[1] * (vx[2] - vx[1]));
    int64_t C2 = A2 * (2 * x0 + 1) + B2 * (2 * y0 + 1)
               - 2 * (vx[2] * (vy[0] - vy[2]) - vy[2] * (vx[0] - vx[2]));

    int64_t A0_2 = 2 * A0, A1_2 = 2 * A1, A2_2 = 2 * A2;
    int64_t B0_2 = 2 * B0, B1_2 = 2 * B1, B2_2 = 2 * B2;

    Color* data = image.data();

    for (pcrd_t y = tri_bounds.y_min; y <= tri_bounds.y_max; y++) {
        int64_t d0 = C0, d1 = C1, d2 = C2;
        Color* row = data + y * x_size;
        for (pcrd_t x = tri_bounds.x_min; x <= tri_bounds.x_max; x++) {
            if ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0)) {
                row[x] = col;
            }
            d0 += A0_2; d1 += A1_2; d2 += A2_2;
        }
        C0 += B0_2; C1 += B1_2; C2 += B2_2;
    }
}
