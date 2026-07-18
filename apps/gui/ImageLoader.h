#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Loads assets/ image files (ALG connection diagrams, WS waveform curves -
// see D-016/D-021) as RGBA pixel buffers ready for glTexImage2D(). Backed
// by stb_image (vcpkg port "stb", D-022 - replaced the project's own
// minimal 24bit-BMP-only decoder so assets can be shipped as PNG instead
// of BMP). Any format stb_image supports would decode here, but PNG is
// the only one this project's assets actually use.
struct ImageRGBA {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // width*height*4 bytes, top-to-bottom row order
                               // (matches ImGui::Image()'s default UV convention).
};

bool loadImageRgba(const std::string& path, ImageRGBA& out);
