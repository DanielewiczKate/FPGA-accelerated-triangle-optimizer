#pragma once

#include "common.hpp"
#include "stb_image_impl.cpp"
#include <string>

void LoadImageData(std::string path, ImageData* image_data);

void SaveImageData(std::string path, const ImageData* const image_data);
