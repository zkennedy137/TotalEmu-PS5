#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Decodes a PNG (the app icon) into 0xAARRGGBB pixels, w x h. Empty when it is not a PNG.
std::vector<uint32_t> decodeIcon(const std::string& png, int& w, int& h);
// A button picture (white background made transparent), 0xAARRGGBB, w x h. Empty when it is not a picture.
std::vector<uint32_t> decodeButtonImage(const std::string& png, int& w, int& h);
