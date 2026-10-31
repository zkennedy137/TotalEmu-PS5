#pragma once
#include <cstdint>

// A minimal Vulkan presenter for PS Launcher, on RADV's display-plane surface.
// Replaces the direct sceVideoOut path (ps5_video.c), which crashes once RADV is
// linked because the driver wants to own the console's one display plane.
//
// The launcher draws its menu and the emulator's frames into a CPU ARGB8888
// buffer as before; this uploads that buffer to a GPU image and blits it to the
// swapchain each frame. No ImGui, no emulator Vulkan context: just present.
//
// Mirrors PSSwanStation's display.cpp instance/surface/swapchain setup (the path
// confirmed working on the console), with a plain device and a blit of our own.
namespace vkp {

// Optional: receives a line at each init step, so a crash in open() is pinpointed.
void setLogSink(void (*sink)(const char*));

// Opens the driver, instance, device and swapchain. error holds a message on
// failure. Returns the screen size actually in use.
bool open(char* error, int errorSize, int* widthOut, int* heightOut);

// Uploads a width*height ARGB8888 image (pitch bytes per row) and presents it,
// scaled to fill the screen. Returns false if the swapchain needs reopening.
bool present(const uint32_t* pixels, int width, int height, int pitch);

void close();

// For logging.
const char* describe();

}  // namespace vkp
