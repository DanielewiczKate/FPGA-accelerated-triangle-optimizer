#include "common.hpp"
#include "SSE.hpp"
#include <cstdlib>

uint64_t compute_SSE(const ImageData& target, const ImageData& candidate) {
    int x_size = target.x_size();
    int y_size = target.y_size();

    size_t size = target.size();

    if (x_size != candidate.x_size() || y_size != candidate.y_size()) {
        std::abort();
    }

    const Color* t_data = target.data();
    const Color* c_data = candidate.data();


    uint64_t acc = 0;
    for(size_t i = 0; i < size; i++) {
        int r = (t_data[i].r - c_data[i].r);
        int g = (t_data[i].g - c_data[i].g);
        int b = (t_data[i].b - c_data[i].b);

        // 3 * 255^2 ~= 200k, much less than the 2B that a signed int can store
        // It is safe to accumulate like this
        acc += r * r + b * b + g * g;
    }

    return acc;
}


uint64_t compute_delta_SSE(const ImageData& target, const ImageData& prev_best,
    const ImageData& candidate, const PixelBounds bounds) {

    int x_size = target.x_size();
    int y_size = target.y_size();

    if (x_size != prev_best.x_size() || y_size != prev_best.y_size()) {
        std::abort();
    }
    if (x_size != candidate.x_size() || y_size != candidate.y_size()) {
        std::abort();
    }

    const Color* t_data = target.data();
    const Color* b_data = prev_best.data();
    const Color* c_data = candidate.data();

    uint64_t acc = 0;
    for(int y = bounds.y_min; y <= bounds.y_max; y++) {
        for(int x = bounds.x_min; x <= bounds.x_max; x++) {
            size_t i = y * x_size + x;

            int b_r = (t_data[i].r - b_data[i].r);
            int b_g = (t_data[i].g - b_data[i].g);
            int b_b = (t_data[i].b - b_data[i].b);

            acc -= b_r * b_r + b_b * b_b + b_g * b_g;

            int c_r = (t_data[i].r - c_data[i].r);
            int c_g = (t_data[i].g - c_data[i].g);
            int c_b = (t_data[i].b - c_data[i].b);

            acc += c_r * c_r + c_b * c_b + c_g * c_g;
        }
    }
    return acc;
}
