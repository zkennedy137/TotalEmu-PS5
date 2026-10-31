#include "core_host.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "libretro.h"

// The core's exports, renamed by tools/build-psswanstation.sh. We declare the
// handful we call with the swanstation_ prefix and drive them through libretro's
// own types from libretro.h.
extern "C" {
void swanstation_retro_set_environment(retro_environment_t);
void swanstation_retro_set_video_refresh(retro_video_refresh_t);
void swanstation_retro_set_audio_sample(retro_audio_sample_t);
void swanstation_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void swanstation_retro_set_input_poll(retro_input_poll_t);
void swanstation_retro_set_input_state(retro_input_state_t);
void swanstation_retro_set_controller_port_device(unsigned port, unsigned device);
void swanstation_retro_init(void);
void swanstation_retro_deinit(void);
bool swanstation_retro_load_game(const struct retro_game_info*);
void swanstation_retro_unload_game(void);
void swanstation_retro_run(void);
void swanstation_retro_get_system_av_info(struct retro_system_av_info*);
size_t swanstation_retro_serialize_size(void);
}

namespace core {
namespace {

std::string g_systemDir;
bool g_biosOptional = true;
bool g_loaded = false;
retro_system_av_info g_av{};
retro_pixel_format g_pixfmt = RETRO_PIXEL_FORMAT_RGB565;

// Latest frame from the core.
Frame g_frame;
std::vector<int16_t> g_audio;     // this frame's samples
size_t g_audioFrames = 0;

uint32_t g_pad0 = 0;
// Analog sticks: [0] left, [1] right; [][0] X, [][1] Y, as libretro reports them.
int16_t g_analog[2][2] = {};
void (*g_logSink)(const char*) = nullptr;
std::string g_lastCoreError;  // last error-level message from the core

// Core options we answer to GET_VARIABLE. Software renderer first.
struct Var { const char* key; const char* value; };
const Var kVars[] = {
    {"swanstation_GPU_Renderer", "Software"},
    {"swanstation_Console_Region", "Auto"},
    {"swanstation_GPU_ResolutionScale", "1"},
    {"swanstation_CDROMThread", "false"},  // threads after jailbreak may be unstable
    {"swanstation_BIOS_PatchFastBoot", "false"},
    // The recompiler needs executable memory, which this title isn't given:
    // allocating its code buffer threw std::bad_alloc. PSSwanStation falls
    // back the same way when the console refuses executable memory.
    {"swanstation_CPU_ExecutionMode", "CachedInterpreter"},
    {"swanstation_CPU_FastmemMode", "Disabled"},
};

void logMsg(const char* fmt, ...) {
    if (!g_logSink) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_logSink(buf);
}

void coreLog(retro_log_level level, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    size_t n = std::strlen(buf);
    if (n && buf[n - 1] == '\n') buf[n - 1] = '\0';
    if (level >= RETRO_LOG_ERROR) g_lastCoreError = buf;
    if (g_logSink) g_logSink(buf);
}

bool onEnvironment(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            *(const char**)data = g_systemDir.c_str();
            return true;
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *(const char**)data = g_systemDir.c_str();
            return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            g_pixfmt = *(const retro_pixel_format*)data;
            return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *(bool*)data = true;
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            auto* var = (retro_variable*)data;
            for (const auto& v : kVars)
                if (std::strcmp(var->key, v.key) == 0) { var->value = v.value; return true; }
            var->value = nullptr;
            return false;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *(bool*)data = false;
            return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            ((retro_log_callback*)data)->log = coreLog;
            return true;
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
            return true;
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
            return true;
        default:
            return false;
    }
}

PixelFormat frameFormat() {
    switch (g_pixfmt) {
        case RETRO_PIXEL_FORMAT_XRGB8888: return PixelFormat::XRGB8888;
        case RETRO_PIXEL_FORMAT_RGB565: return PixelFormat::RGB565;
        default: return PixelFormat::RGB1555;
    }
}

// Keep the core's pixels where they are; play.cpp converts them into the screen
// texture with convertFrame, so no per-frame buffer is needed here.
void onVideo(const void* data, unsigned w, unsigned h, size_t pitch) {
    if (!data) { g_frame.updated = false; return; }  // duped frame
    // Log size changes so a new picture size shows up in the log.
    static unsigned lastW = 0, lastH = 0;
    if (w != lastW || h != lastH) {
        logMsg("core: frame size %ux%u", w, h);
        lastW = w;
        lastH = h;
    }
    g_frame = {data, (int)w, (int)h, (int)pitch, frameFormat(), true};
}

void onAudioBatchBegin() { g_audio.clear(); g_audioFrames = 0; }

size_t onAudioBatch(const int16_t* data, size_t frames) {
    g_audio.insert(g_audio.end(), data, data + frames * 2);
    g_audioFrames += frames;
    return frames;
}

void onAudioSample(int16_t l, int16_t r) {
    g_audio.push_back(l);
    g_audio.push_back(r);
    ++g_audioFrames;
}

void onInputPoll() {}

int16_t onInputState(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port == 0 && device == RETRO_DEVICE_ANALOG && index < 2 && id < 2) return g_analog[index][id];
    if (port != 0 || device != RETRO_DEVICE_JOYPAD) return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)g_pad0;
    return (g_pad0 >> id) & 1;
}

}  // namespace

void setLogSink(void (*sink)(const char*)) { g_logSink = sink; }

void convertFrame(const Frame& f, void* dst, int dstPitch) {
    const uint8_t* src = (const uint8_t*)f.pixels;
    for (int y = 0; y < f.height; ++y) {
        const uint8_t* s8 = src + (size_t)y * f.pitch;
        uint32_t* d = (uint32_t*)((uint8_t*)dst + (size_t)y * dstPitch);
        switch (f.format) {
            case PixelFormat::XRGB8888:
                for (int x = 0; x < f.width; ++x)
                    d[x] = ((const uint32_t*)s8)[x] | 0xff000000u;
                break;
            case PixelFormat::RGB565:
                for (int x = 0; x < f.width; ++x) {
                    uint16_t p = ((const uint16_t*)s8)[x];
                    uint32_t r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
                    d[x] = 0xff000000u | (((r * 255 + 15) / 31) << 16) | (((g * 255 + 31) / 63) << 8) | ((b * 255 + 15) / 31);
                }
                break;
            case PixelFormat::RGB1555:
                for (int x = 0; x < f.width; ++x) {
                    uint16_t p = ((const uint16_t*)s8)[x];
                    uint32_t r = (p >> 10) & 0x1f, g = (p >> 5) & 0x1f, b = p & 0x1f;
                    d[x] = 0xff000000u | (((r * 255 + 15) / 31) << 16) | (((g * 255 + 15) / 31) << 8) | ((b * 255 + 15) / 31);
                }
                break;
        }
    }
}

bool init(const std::string& systemDir, std::string& err) {
    g_systemDir = systemDir;
    swanstation_retro_set_environment(onEnvironment);
    swanstation_retro_set_video_refresh(onVideo);
    swanstation_retro_set_audio_sample(onAudioSample);
    swanstation_retro_set_audio_sample_batch(onAudioBatch);
    swanstation_retro_set_input_poll(onInputPoll);
    swanstation_retro_set_input_state(onInputState);
    swanstation_retro_init();
    // Port 1 as a DualShock (analog) pad: the core reads the sticks only from an analog device.
    swanstation_retro_set_controller_port_device(0, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0));
    logMsg("core: init, system dir %s", systemDir.c_str());
    (void)err;
    return true;
}

bool loadGame(const std::string& path, std::string& err) {
    if (g_loaded) unloadGame();
    retro_game_info info{};
    info.path = path.c_str();
    info.data = nullptr;   // need_fullpath: the core opens the file itself
    info.size = 0;
    g_lastCoreError.clear();
    logMsg("core: calling retro_load_game");
    if (!swanstation_retro_load_game(&info)) {
        logMsg("core: retro_load_game returned false");
        err = g_lastCoreError.empty() ? "the core could not load the game" : g_lastCoreError;
        logMsg("core: load failed: %s", path.c_str());
        return false;
    }
    logMsg("core: retro_load_game returned true");
    swanstation_retro_get_system_av_info(&g_av);
    g_loaded = true;
    logMsg("core: loaded %s (%dx%d, %.2f fps, %.0f Hz)", path.c_str(), g_av.geometry.base_width,
           g_av.geometry.base_height, g_av.timing.fps, g_av.timing.sample_rate);
    return true;
}

bool gameLoaded() { return g_loaded; }

void runFrame(Frame& frameOut) {
    g_frame.updated = false;
    onAudioBatchBegin();
    swanstation_retro_run();
    frameOut = g_frame;
}

void setButtons(uint32_t pad0Bits) { g_pad0 = pad0Bits; }
void setAnalog(int stick, int axis, int16_t value) {
    if (stick >= 0 && stick < 2 && axis >= 0 && axis < 2) g_analog[stick][axis] = value;
}

const int16_t* audioData(size_t& frames) {
    frames = g_audioFrames;
    return g_audio.data();
}

AvInfo avInfo() {
    AvInfo a;
    a.fps = g_av.timing.fps > 0 ? g_av.timing.fps : 60.0;
    a.sampleRate = g_av.timing.sample_rate > 0 ? g_av.timing.sample_rate : 44100.0;
    a.baseWidth = g_av.geometry.base_width;
    a.baseHeight = g_av.geometry.base_height;
    a.maxWidth = g_av.geometry.max_width;
    a.maxHeight = g_av.geometry.max_height;
    return a;
}

void unloadGame() {
    if (!g_loaded) return;
    swanstation_retro_unload_game();
    g_loaded = false;
    logMsg("core: game unloaded");
}

void shutdown() {
    unloadGame();
    swanstation_retro_deinit();
}

}  // namespace core

// Exported for src/platform/fault.cpp (logMsg itself is file-private).
void pslFaultLog(const char* line) { core::logMsg("%s", line); }
