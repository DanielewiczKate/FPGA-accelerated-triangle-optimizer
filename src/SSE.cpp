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
