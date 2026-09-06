#pragma once

#include "common.hpp"

/// Rasterize a triangle onto an ImageData
///
/// Triangle's vertices must be within the image, meaning:
/// x in [0, x_size() - 1]
/// y in [0, y_size() - 1]
/// Failure to be within this range will cause an std::abort().
///
/// ImageData is mutated by this call.
///
/// The ImageData is assumed to have alpha = 255 for all pixels,
/// the triangle is rendered on top of this opaque image, with
/// alpha blending:
/// data[i] = (data[i] * (255 - alpha) + triangle.color * alpha) / 255
/// Where alpha = triangle.color.a
/// Note: this must be done with uint16_t to ensure that it will not
/// overflow.
/// ImageData's alpha values will not be mutated.
///
/// All triangles are rasterized, zero area triangles will
/// affect the resulting image. While deterministic,
/// it may have unintended visual effects. It is recommended to ensure
/// triangles have non-zero area.
///
/// Inside-of-triangle is calculated using pixel corners (x, y), not centers.
void RasterizeTriangle(ImageData& image, const Triangle& triangle);
