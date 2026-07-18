#include "ImageLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <fstream>
#include <iterator>

bool loadImageRgba(const std::string& path, ImageRGBA& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (buf.empty()) return false;

    int width = 0, height = 0, channelsInFile = 0;
    stbi_uc* pixels = stbi_load_from_memory(buf.data(), static_cast<int>(buf.size()), &width, &height,
                                             &channelsInFile, 4);
    if (!pixels) return false;

    out.width = width;
    out.height = height;
    out.rgba.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    stbi_image_free(pixels);
    return true;
}
