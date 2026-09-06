#pragma once
/// Functions for computing the SSE of two ImageData classes

#include "common.hpp"


/// take two ImageDatas and compute the SSE
///
/// @param target is the Image which the candidate ideally would look like
///
/// Both target and candidate must be initialized and must have equal x_size
/// and y_size. This is checked in the function and will cause an std::abort()
///
/// Alpha is ignored, only the RGB components used.
///
/// This is the most simple implementation of SSE and is intended to be a
/// function to iteratively improve upon.
///
/// The return is uint64_t, with a max of 18,446,744,073,709,551,615.
/// Since the SSE is the sum of the squares of each channel the max possible SSE
/// is x_size() * y_size() * 3*(255^2). This means that x_size() * y_size() must
/// be less than the uint64_t max. Assuming a square image, x_size() and
/// y_size() must both be less than 9,724,315. This is less than the pcrd_t max
/// so, this is not an issue.
uint64_t compute_SSE(const ImageData& target, const ImageData& candidate);


/// Take a target image, the previous best, and a candidate image.
///
/// This retunts the change is MSE that is observerd by moving from prev_best
/// to the candidate. The following equality shows how these are related:
/// compute_SSE(target, candidate) = compute_SSE(target, prev_best) +
///     compute_delta_SSE(target, prev_best, candidate, bounding_box)
/// Where bounding box is the regeion of candidate that may be different from
/// prev_best.
///
/// All of the restrictions applicable to compuse_SSE apply to this.
uint64_t compute_delta_SSE(const ImageData& target, const ImageData& prev_best,
    const ImageData& candidate, const PixelBounds bounds);
