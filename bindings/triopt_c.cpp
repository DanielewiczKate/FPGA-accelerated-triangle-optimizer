// =============================================================================
// AI-GENERATED (Claude Code) — C-ABI bridge boilerplate, not algorithm code.
// This file only marshals flat buffers and scalars across the C ABI; every
// rasterizer / SSE implementation it forwards to is hand-written in src/.
// Reviewed before commit; review again before relying on it.
// =============================================================================
//
// extern "C" shim over triopt_core for the Python (ctypes) bridge.
//
// This file contains no algorithm -- it only marshals flat RGBA8 buffers and
// plain scalars across the C ABI and forwards to the real implementations in
// src/. Keep it that way: anything with actual rasterizer / SSE logic belongs
// in src/, not here.
//
// Buffer contract (matches ImageData in src/common.hpp):
//   - pixels are row-major RGBA8, 4 bytes each, byte order r,g,b,a
//   - pixel [x, y] starts at byte (x + y * x_size) * 4
//   - caller owns the buffer; rasterize_* mutate it in place

#include <cstdint>
#include <cstring>

#include "common.hpp"
#include "rasterizer.hpp"
#include "SSE.hpp"

namespace {

std::size_t byte_count(uint16_t x_size, uint16_t y_size) {
    return static_cast<std::size_t>(x_size) * static_cast<std::size_t>(y_size) *
           sizeof(Color);
}

ImageData image_from_rgba(const uint8_t* px, uint16_t x_size, uint16_t y_size) {
    ImageData img(x_size, y_size);
    std::memcpy(img.data(), px, byte_count(x_size, y_size));
    return img;
}

Triangle make_triangle(const uint16_t* verts_x, const uint16_t* verts_y,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Triangle tri;
    tri.verts_x = {verts_x[0], verts_x[1], verts_x[2]};
    tri.verts_y = {verts_y[0], verts_y[1], verts_y[2]};
    tri.color = Color{r, g, b, a};
    return tri;
}

}  // namespace

extern "C" {

// RasterizeTriangle (golden V1), in place on `px`.
void triopt_rasterize_v1(uint8_t* px, uint16_t x_size, uint16_t y_size,
                         const uint16_t* verts_x, const uint16_t* verts_y,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    ImageData img = image_from_rgba(px, x_size, y_size);
    RasterizeTriangle(img, make_triangle(verts_x, verts_y, r, g, b, a));
    std::memcpy(px, img.data(), byte_count(x_size, y_size));
}

// RasterizeTriangleV2 (incremental edge functions), in place on `px`.
void triopt_rasterize_v2(uint8_t* px, uint16_t x_size, uint16_t y_size,
                         const uint16_t* verts_x, const uint16_t* verts_y,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    ImageData img = image_from_rgba(px, x_size, y_size);
    RasterizeTriangleV2(img, make_triangle(verts_x, verts_y, r, g, b, a));
    std::memcpy(px, img.data(), byte_count(x_size, y_size));
}

// compute_SSE(target, candidate).
uint64_t triopt_sse(const uint8_t* target, const uint8_t* candidate,
                    uint16_t x_size, uint16_t y_size) {
    ImageData t = image_from_rgba(target, x_size, y_size);
    ImageData c = image_from_rgba(candidate, x_size, y_size);
    return compute_SSE(t, c);
}

// compute_delta_SSE(target, prev_best, candidate, {x_min, x_max, y_min, y_max}).
// Returns the raw uint64_t bit pattern; the delta is signed, so the Python
// side reinterprets it as two's-complement 64-bit.
uint64_t triopt_delta_sse(const uint8_t* target, const uint8_t* prev_best,
                          const uint8_t* candidate, uint16_t x_size,
                          uint16_t y_size, uint16_t x_min, uint16_t x_max,
                          uint16_t y_min, uint16_t y_max) {
    ImageData t = image_from_rgba(target, x_size, y_size);
    ImageData p = image_from_rgba(prev_best, x_size, y_size);
    ImageData c = image_from_rgba(candidate, x_size, y_size);
    return compute_delta_SSE(t, p, c, PixelBounds{x_min, x_max, y_min, y_max});
}

}  // extern "C"
