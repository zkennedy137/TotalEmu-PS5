#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Runs the built-in PSSwanStation (SwanStation) PS1 core through its libretro API,
// which is linked in with every symbol renamed swanstation_retro_*. One game at a
// time. Starts with the core's software renderer, so frames arrive as a CPU pixel
// buffer the launcher blits like its menu; the Vulkan renderer comes later.
namespace core {

// Pixel formats the core produces, as libretro reports them.
enum class PixelFormat { RGB565, XRGB8888, RGB1555 };

// A frame from the core. The pixels stay in the core's buffer and are valid only
// until the next runFrame; convertFrame copies them out.
struct Frame {
    const void* pixels = nullptr;
    int width = 0;
    int height = 0;
    int pitch = 0;  // bytes per row
    PixelFormat format = PixelFormat::RGB565;
    bool updated = false;  // false when the core repeated the previous frame
};

// Writes the frame as XRGB8888 into dst, with dstPitch bytes per row.
void convertFrame(const Frame& f, void* dst, int dstPitch);

// systemDir holds the BIOS (scph5500/5501/5502.bin); the core also writes its
// caches there. biosOptional lets a game boot on OpenBIOS when no BIOS is present.
bool init(const std::string& systemDir, std::string& err);

bool loadGame(const std::string& path, std::string& err);  // .cue/.bin/.chd/.pbp/.m3u...
bool gameLoaded();

// One emulated frame. Call at the game's rate (~60 fps NTSC, 50 PAL; see avInfo()).
void runFrame(Frame& frameOut);

// PS1 digital pad, one bit per button (see Button). Set before runFrame.
void setButtons(uint32_t pad0Bits);
// Analog stick position, -32768..32767: stick 0 left, 1 right; axis 0 X, 1 Y.
void setAnalog(int stick, int axis, int16_t value);

// Audio produced during the last runFrame: interleaved stereo int16 at avInfo().sampleRate.
const int16_t* audioData(size_t& frames);

struct AvInfo {
    double fps = 60.0;
    double sampleRate = 44100.0;
    int baseWidth = 320, baseHeight = 240, maxWidth = 640, maxHeight = 480;
};
AvInfo avInfo();

void unloadGame();
void shutdown();

// PS1 pad bit positions (RETRO_DEVICE_ID_JOYPAD_*).
enum Button {
    B = 0, Y = 1, SELECT = 2, START = 3, UP = 4, DOWN = 5, LEFT = 6, RIGHT = 7,
    A = 8, X = 9, L1 = 10, R1 = 11, L2 = 12, R2 = 13, L3 = 14, R3 = 15
};

}  // namespace core
