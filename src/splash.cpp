/*
 * PS Launcher - the loading splash: the app icon, decoded from its PNG.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * stb_image (public domain or MIT, Sean Barrett) decodes the PNG from memory.
 */
#include "splash.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"
#include <cstdlib>

std::vector<uint32_t> decodeIcon(const std::string& png, int& w, int& h) {
    std::vector<uint32_t> out;
    int channels = 0;
    unsigned char* px = stbi_load_from_memory((const unsigned char*)png.data(), (int)png.size(), &w, &h,
                                              &channels, 4);
    if (!px) return out;
    out.resize((size_t)w * h);
    for (size_t i = 0; i < out.size(); ++i) {
        const unsigned char* p = px + i * 4;
        out[i] = 0xff000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    }
    stbi_image_free(px);
    return out;
}

// A button picture: its background becomes transparent, whatever colour it is (white or black). The background is the
// colour of the picture's top-left corner or fully transparent pixels, and only what is joined to the edge of the
// picture is cleared, so the symbols inside the buttons stay. The result is 0xAARRGGBB, w x h. Empty when it is not a
// picture.
std::vector<uint32_t> decodeButtonImage(const std::string& png, int& w, int& h) {
    std::vector<uint32_t> out;
    int channels = 0;
    unsigned char* px = stbi_load_from_memory((const unsigned char*)png.data(), (int)png.size(), &w, &h,
                                              &channels, 4);
    if (!px) return out;
    const size_t count = (size_t)w * h;
    const unsigned char bg[3] = {px[0], px[1], px[2]};
    auto isBackground = [&](size_t i) {
        const unsigned char* q = px + i * 4;
        if (q[3] == 0) return true;
        return std::abs((int)q[0] - bg[0]) <= 24 && std::abs((int)q[1] - bg[1]) <= 24 &&
               std::abs((int)q[2] - bg[2]) <= 24;
    };
    // Flood fill from the edges through background pixels.
    std::vector<uint8_t> clear(count, 0);
    std::vector<size_t> stack;
    auto seed = [&](size_t i) {
        if (!clear[i] && isBackground(i)) {
            clear[i] = 1;
            stack.push_back(i);
        }
    };
    for (int x = 0; x < w; ++x) { seed((size_t)x); seed((size_t)(h - 1) * w + x); }
    for (int y = 0; y < h; ++y) { seed((size_t)y * w); seed((size_t)y * w + (w - 1)); }
    while (!stack.empty()) {
        const size_t i = stack.back();
        stack.pop_back();
        const int x = (int)(i % w), y = (int)(i / w);
        if (x > 0) seed(i - 1);
        if (x < w - 1) seed(i + 1);
        if (y > 0) seed(i - w);
        if (y < h - 1) seed(i + w);
    }
    out.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const unsigned char* q = px + i * 4;
        const uint32_t alpha = clear[i] ? 0u : (uint32_t)q[3];
        out[i] = (alpha << 24) | ((uint32_t)q[0] << 16) | ((uint32_t)q[1] << 8) | q[2];
    }
    stbi_image_free(px);
    return out;
}
