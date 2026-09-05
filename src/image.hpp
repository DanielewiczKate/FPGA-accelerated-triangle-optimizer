#pragma once
/// Image loading and saving PNG functionality.

#include <optional>
#include <string>
#include "common.hpp"

/// Loads PNG to ImageData
///
/// On failure, returns nullopt.
/// Pixels are decoded to RGBA.
///
/// If an image is larger than what ImageData can store, the load fails. This is
/// a limitation of pcrd_t: image dimensions are limited by the pcrd_t maximum
/// value.
/// Alpha is set to 255.
/// Failure modes are all collapsed into the option.
[[nodiscard]]
std::optional<ImageData> LoadImageDataFromPNG(const std::string& path);


/// Saves file to RGBA PNG
///
/// Returns true on successful save.
[[nodiscard]]
bool SaveImageDataToPNG(const std::string& path, const ImageData& image_data);
