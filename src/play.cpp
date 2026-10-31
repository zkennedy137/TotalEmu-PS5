#include "play.hpp"

#include <algorithm>
#include <cstdint>

#include "core_host.hpp"

namespace core { void setLogSink(void (*)(const char*)); }

#if defined(__PROSPERO__)
extern "C" int32_t sceKernelAvailableFlexibleMemorySize(size_t* available);
#endif

namespace {
// Free flexible memory in MiB, the pool the title's heap is drawn from (0 where unknown).
size_t freeFlexibleMiB() {
#if defined(__PROSPERO__)
    size_t avail = 0;
    if (sceKernelAvailableFlexibleMemorySize(&avail) == 0) return avail >> 20;
#endif
    return 0;
}
}  // namespace
void pslSetStep(const char* step);
void pslPresentPS5();  // main.cpp: shows the software canvas on the display
#if defined(__PROSPERO__)
uint32_t* pslCanvas(int& w, int& h);  // main.cpp: the launcher's ARGB8888 canvas
#endif

namespace {
Platform* g_plat = nullptr;
void sink(const char* m) { if (g_plat) g_plat->log(m); }

// DualSense (SDL) -> PS1 pad bit. Cross=B? No: on PS1 pads, Cross is the bottom
// face button. libretro maps PS layouts so Cross->RETRO B, Circle->A, Square->Y,
// Triangle->X, matching SwanStation's default.
uint32_t readPad(SDL_GameController* c) {
    if (!c) return 0;
    auto b = [&](SDL_GameControllerButton sb) { return SDL_GameControllerGetButton(c, sb); };
    uint32_t p = 0;
    p |= b(SDL_CONTROLLER_BUTTON_A) << core::B;
    p |= b(SDL_CONTROLLER_BUTTON_B) << core::A;
    p |= b(SDL_CONTROLLER_BUTTON_X) << core::Y;
    p |= b(SDL_CONTROLLER_BUTTON_Y) << core::X;
    p |= b(SDL_CONTROLLER_BUTTON_BACK) << core::SELECT;
    p |= b(SDL_CONTROLLER_BUTTON_START) << core::START;
    p |= b(SDL_CONTROLLER_BUTTON_DPAD_UP) << core::UP;
    p |= b(SDL_CONTROLLER_BUTTON_DPAD_DOWN) << core::DOWN;
    p |= b(SDL_CONTROLLER_BUTTON_DPAD_LEFT) << core::LEFT;
    p |= b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) << core::RIGHT;
    p |= b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) << core::L1;
    p |= b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) << core::R1;
    p |= (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000) << core::L2;
    p |= (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000) << core::R2;
    p |= b(SDL_CONTROLLER_BUTTON_LEFTSTICK) << core::L3;
    p |= b(SDL_CONTROLLER_BUTTON_RIGHTSTICK) << core::R3;
    return p;
}

#if defined(__PROSPERO__)
// One pixel of the core's frame as ARGB8888, converted per pixel so no buffer is
// needed: the console's heap is too small for a second frame-sized copy.
uint32_t corePixel(const core::Frame& f, int x, int y) {
    const uint8_t* row = (const uint8_t*)f.pixels + (size_t)y * f.pitch;
    switch (f.format) {
        case core::PixelFormat::XRGB8888:
            return ((const uint32_t*)row)[x] | 0xff000000u;
        case core::PixelFormat::RGB565: {
            uint16_t p = ((const uint16_t*)row)[x];
            uint32_t r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
            return 0xff000000u | (((r * 255 + 15) / 31) << 16) | (((g * 255 + 31) / 63) << 8) | ((b * 255 + 15) / 31);
        }
        case core::PixelFormat::RGB1555: {
            uint16_t p = ((const uint16_t*)row)[x];
            uint32_t r = (p >> 10) & 0x1f, g = (p >> 5) & 0x1f, b = p & 0x1f;
            return 0xff000000u | (((r * 255 + 15) / 31) << 16) | (((g * 255 + 15) / 31) << 8) | ((b * 255 + 15) / 31);
        }
    }
    return 0xff000000u;
}

// Scales the core's frame into the launcher canvas: nearest neighbour, 4:3 and
// pillarboxed, as the desktop texture path does.
void drawToCanvas(const core::Frame& f, uint32_t* canvas, int cw, int ch) {
    std::fill(canvas, canvas + (size_t)cw * ch, 0xff000000u);
    int dh = ch, dw = (int)(ch * 4.0 / 3.0);
    if (dw > cw) { dw = cw; dh = (int)(cw * 3.0 / 4.0); }
    const int x0 = (cw - dw) / 2, y0 = (ch - dh) / 2;
    for (int y = 0; y < dh; ++y) {
        const int sy = y * f.height / dh;
        uint32_t* d = canvas + (size_t)(y0 + y) * cw + x0;
        for (int x = 0; x < dw; ++x) d[x] = corePixel(f, x * f.width / dw, sy);
    }
}
#endif

// DualShock sticks -> the core's analog axes, with a small dead zone.
void readSticks(SDL_GameController* c) {
    auto axis = [&](SDL_GameControllerAxis a) -> int16_t {
        int16_t v = c ? SDL_GameControllerGetAxis(c, a) : 0;
        return (v > -8000 && v < 8000) ? 0 : v;
    };
    core::setAnalog(0, 0, axis(SDL_CONTROLLER_AXIS_LEFTX));
    core::setAnalog(0, 1, axis(SDL_CONTROLLER_AXIS_LEFTY));
    core::setAnalog(1, 0, axis(SDL_CONTROLLER_AXIS_RIGHTX));
    core::setAnalog(1, 1, axis(SDL_CONTROLLER_AXIS_RIGHTY));
}

// Pixels with any colour in a locked texture region, to tell a drawn frame from a black one.
size_t countLit(const void* pix, int pitch, int w, int h) {
    size_t n = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            n += (((const uint32_t*)((const uint8_t*)pix + (size_t)y * pitch))[x] & 0xffffff) != 0;
    return n;
}
}  // namespace

std::string playGame(SDL_Renderer* ren, Platform& plat, const SystemConfig& sys,
                     const std::string& gamePath, SDL_GameController* pad) {
    g_plat = &plat;
    core::setLogSink(sink);
    std::string err;
    if (!core::init(sys.systemDir, err) || !core::loadGame(gamePath, err)) {
        plat.log("play: " + err);
        core::shutdown();
        return err.empty() ? std::string("the game could not be started") : err;
    }
    plat.log("play: core loaded");

    // Audio: open at the core's rate once we know it.
    core::AvInfo av = core::avInfo();
    SDL_AudioSpec want{}, have{};
    want.freq = (int)av.sampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);
    plat.log(audio ? "play: audio open" : "play: audio unavailable");

#if defined(__PROSPERO__)
    // The console's SDL software renderer cannot make streaming textures, so the
    // frame is scaled straight into the launcher's canvas and shown with it.
    int canvasW = 0, canvasH = 0;
    uint32_t* canvas = pslCanvas(canvasW, canvasH);
    if (!canvas) {
        plat.log("play: no canvas for the game picture");
        core::shutdown();
        return "the game picture could not be created";
    }
    SDL_Texture* tex = nullptr;
    plat.log("play: canvas ready, entering loop");
#else
    // One streaming texture sized to the core's max; we update its top-left each frame.
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                         std::max(av.maxWidth, 640), std::max(av.maxHeight, 480));
    if (!tex) {
        plat.log("play: texture failed: " + std::string(SDL_GetError()));
        core::shutdown();
        return "the game picture could not be created";
    }
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    plat.log("play: texture ready, entering loop");
#endif

    const double frameMs = 1000.0 / av.fps;
    Uint32 quitHold = 0;
    bool running = true;
    double next = SDL_GetTicks();
    uint64_t frameNo = 0;
    // Size of the last frame the core produced; the copy below shows this area.
    int shownW = av.baseWidth ? av.baseWidth : 320;
    int shownH = av.baseHeight ? av.baseHeight : 240;

    while (running) {
        ++frameNo;
        // The first frames and then every 600th are logged, so a crash shows
        // the last frame and the step it reached.
        const bool trace = frameNo <= 5 || frameNo % 600 == 0;
        // Heap headroom every 10 frames, to tell a steady climb from a single jump.
        if (frameNo % 10 == 0)
            plat.log("play: frame " + std::to_string(frameNo) + " free flexible " +
                     std::to_string(freeFlexibleMiB()) + " MiB");
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad)
                pad = SDL_GameControllerOpen(ev.cdevice.which);
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        core::setButtons(readPad(pad));
        readSticks(pad);

        core::Frame f;
        pslSetStep(("frame " + std::to_string(frameNo) + " runFrame").c_str());
        if (trace) plat.log("play: frame " + std::to_string(frameNo) + " runFrame");
        core::runFrame(f);
        pslSetStep(("frame " + std::to_string(frameNo) + " after runFrame").c_str());
        if (trace) plat.log("play: frame " + std::to_string(frameNo) + " ran, updated=" +
                            std::to_string((int)f.updated));

        if (audio) {
            size_t frames = 0;
            const int16_t* a = core::audioData(frames);
            if (frames) SDL_QueueAudio(audio, a, (Uint32)(frames * 2 * sizeof(int16_t)));
        }

        if (f.updated && f.pixels) {
#if defined(__PROSPERO__)
            drawToCanvas(f, canvas, canvasW, canvasH);
#else
            SDL_Rect src{0, 0, f.width, f.height};
            void* pix = nullptr;
            int pitch = 0;
            if (SDL_LockTexture(tex, &src, &pix, &pitch) == 0) {
                core::convertFrame(f, pix, pitch);
                if (frameNo % 60 == 0)
                    plat.log("play: frame " + std::to_string(frameNo) + " lit pixels " +
                             std::to_string(countLit(pix, pitch, f.width, f.height)) + " of " +
                             std::to_string(f.width * f.height));
                SDL_UnlockTexture(tex);
            } else {
                plat.log("play: texture lock failed: " + std::string(SDL_GetError()));
            }
#endif
            shownW = f.width;
            shownH = f.height;
        }
        if (frameNo % 60 == 0)
            plat.log("play: frame " + std::to_string(frameNo) + " updated=" + std::to_string((int)f.updated) +
                     " size " + std::to_string(f.width) + "x" + std::to_string(f.height));

#if !defined(__PROSPERO__)
        // Keep the PS1's 4:3 inside the 16:9 screen (pillarboxed).
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        int dh = oh, dw = (int)(oh * 4.0 / 3.0);
        if (dw > ow) { dw = ow; dh = (int)(ow * 3.0 / 4.0); }
        SDL_Rect src{0, 0, shownW, shownH};
        SDL_Rect dst{(ow - dw) / 2, (oh - dh) / 2, dw, dh};
        SDL_RenderCopy(ren, tex, &src, &dst);
        pslSetStep(("frame " + std::to_string(frameNo) + " present").c_str());
        SDL_RenderPresent(ren);
#else
        pslSetStep(("frame " + std::to_string(frameNo) + " present").c_str());
#endif
        // On PS5 the game frame lives in the launcher's canvas, which vkp::present shows.
        pslPresentPS5();
        if (trace) plat.log("play: frame " + std::to_string(frameNo) + " presented");

        // Quit: hold the touchpad for 2 s, returns to the menu.
        if (pad && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_TOUCHPAD)) {
            if (!quitHold) quitHold = SDL_GetTicks();
            if (SDL_GetTicks() - quitHold >= 2000) running = false;
        } else {
            quitHold = 0;
        }

        next += frameMs;
        double now = SDL_GetTicks();
        if (next > now) SDL_Delay((Uint32)(next - now));
        else next = now;
    }

    if (audio) SDL_CloseAudioDevice(audio);
    SDL_DestroyTexture(tex);
    core::shutdown();
    plat.log("play: returned to menu");
    return {};
}
