#include <optional>
#include <string>
#include "common.hpp"
#include <limits>
#include <array>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#define STBI_ONLY_PNG
#include "stb_image.h"
#include "stb_image_write.h"


[[nodiscard]]
std::optional<ImageData> LoadImageDataFromPNG(const std::string& path) {
    const char* path_c = path.c_str();
    int x,y,n,ok;
    ok = stbi_info(path_c, &x, &y, &n);
    pcrd_t max_dim = std::numeric_limits<pcrd_t>::max();
    if (x > max_dim || y > max_dim || !ok || n < 3) {
        return std::nullopt;
    }
    ImageData image_data = ImageData(x, y);
    stbi_uc* data_stream = stbi_load(path_c, &x, &y, &n, 3);

    Color* data = image_data.data();

    for (std::size_t i = 0; i < image_data.size(); i++) {
        col_t r, g, b;
        r = data_stream[3 * i + 0];
        g = data_stream[3 * i + 1];
        b = data_stream[3 * i + 2];

        Color col = {r, g, b, 255};

        data[i] = col;
    }

    return image_data;
}


[[nodiscard]]
bool SaveImageDataToPNG(const std::string& path, const ImageData& image_data) {
    const char* path_c = path.c_str();

    int x_size = image_data.x_size();
    int y_size = image_data.y_size();

    stbi_write_png(path_c, x_size, y_size, 4, image_data.data(), 4 * x_size);
    return true;
}
