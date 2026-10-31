// PS Launcher (experimental): one menu that starts separate emulator titles.
// Desktop: SDL2 window; emulators simulated or run through desktop_cmd.
// PS5: SDL reads the controller only. The menu is drawn by SDL's software renderer
// into a CPU buffer that ps5_video.c shows through VideoOut (PacBrew's SDL2 has no
// PS5 render driver), the same split SwanStationPS5 uses.

#include <SDL.h>
#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <string>
#include <set>
#include <vector>

#define STB_EASY_FONT_IMPLEMENTATION
#include "stb_easy_font.h"

#include "config.hpp"
#include "covers.hpp"
#include "helper_client.hpp"
#include "library.hpp"
#include "platform.hpp"
#include "play.hpp"

#include "vk_present.hpp"
#if defined(__PROSPERO__)
#include "disc/elevator/elevation.hpp"
#include "splash.hpp"
#include "disc/fe.h"
#endif
#if defined(__PROSPERO__)
extern "C" { int sceSystemServiceHideSplashScreen(void); }
#endif

namespace {

std::string num(long long n) {
    char b[24];
    std::snprintf(b, sizeof b, "%lld", n);
    return b;
}

struct Screen;
}  // namespace
static Screen* g_screen = nullptr;
static Platform* g_bootlog = nullptr;
static void bl(const char* m){ if (g_bootlog) g_bootlog->log(m); }
namespace {
constexpr int kW = 1920, kH = 1080;
constexpr int kRows = 15, kRowH = 44, kListY = 250;
constexpr const char* kVersion = "1.6.6";

// launcher.ini's flags in [launcher]: Debug_Menu (the Debug row in Settings) and http_probe (the network test at
// startup). Each is one key=value line, 1 or 0; a missing line is 0.
std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        const size_t nl = text.find('\n', at);
        if (nl == std::string::npos) {
            out.push_back(text.substr(at));
            break;
        }
        out.push_back(text.substr(at, nl - at));
        at = nl + 1;
    }
    return out;
}

// The value after key= in launcher.ini's [launcher] lines, or "".
std::string readValue(Platform& p, const std::string& dataDir, const std::string& key) {
    std::string text;
    if (!p.readFile(dataDir + "/launcher.ini", text)) return "";
    for (const std::string& line : linesOf(text))
        if (line.compare(0, key.size() + 1, key + "=") == 0) return line.substr(key.size() + 1);
    return "";
}

bool readFlag(Platform& p, const std::string& dataDir, const std::string& key) {
    std::string text;
    if (!p.readFile(dataDir + "/launcher.ini", text)) return false;
    for (const std::string& line : linesOf(text))
        if (line.compare(0, key.size() + 1, key + "=") == 0)
            return line.size() > key.size() + 1 && line[key.size() + 1] == '1';
    return false;
}

// Sets a flag in place. Its line goes under [launcher] when it is not there yet.
void writeValue(Platform& p, const std::string& dataDir, const std::string& key, const std::string& value) {
    const std::string path = dataDir + "/launcher.ini";
    std::string text;
    p.readFile(path, text);
    std::vector<std::string> lines = linesOf(text);
    const std::string flag = key + "=" + value;
    bool replaced = false;
    for (std::string& line : lines) {
        if (line.compare(0, key.size() + 1, key + "=") == 0) {
            line = flag;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        size_t at = 0;
        while (at < lines.size() && lines[at] != "[launcher]") ++at;
        if (at == lines.size()) {
            lines.insert(lines.begin(), "[launcher]");
            at = 0;
        }
        lines.insert(lines.begin() + at + 1, flag);
    }
    std::string out;
    for (size_t k = 0; k < lines.size(); ++k) {
        out += lines[k];
        if (k + 1 < lines.size()) out += '\n';
    }
    p.writeFile(path, out);
}

void writeFlag(Platform& p, const std::string& dataDir, const std::string& key, bool on) {
    writeValue(p, dataDir, key, on ? "1" : "0");
}
constexpr Uint32 kQuitHoldMs = 2000;

// ---------- drawing ----------

void fillRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

// stb_easy_font emits quads of {float x,y,z; uint8 rgba}; we scale them and
// draw them as triangles, so no font file or SDL_ttf port is needed.
void drawText(SDL_Renderer* r, float x, float y, float scale, const std::string& s, SDL_Color c) {
    static char buf[200000];
    unsigned char col[4] = {c.r, c.g, c.b, c.a};
    int quads = stb_easy_font_print(0, 0, const_cast<char*>(s.c_str()), col, buf, sizeof buf);
    if (quads <= 0) return;

    struct V { float x, y, z; unsigned char c[4]; };
    const V* src = reinterpret_cast<const V*>(buf);
    std::vector<SDL_Vertex> verts(quads * 4);
    std::vector<int> idx(quads * 6);
    for (int i = 0; i < quads * 4; ++i) {
        verts[i].position = {x + src[i].x * scale, y + src[i].y * scale};
        verts[i].color = {src[i].c[0], src[i].c[1], src[i].c[2], src[i].c[3]};
        verts[i].tex_coord = {0, 0};
    }
    for (int q = 0; q < quads; ++q) {
        int b = q * 4;
        int* o = &idx[q * 6];
        o[0] = b; o[1] = b + 1; o[2] = b + 2; o[3] = b; o[4] = b + 2; o[5] = b + 3;
    }
    SDL_RenderGeometry(r, nullptr, verts.data(), (int)verts.size(), idx.data(), (int)idx.size());
}

float textWidth(const std::string& s, float scale) {
    return stb_easy_font_width(const_cast<char*>(s.c_str())) * scale;
}

const SDL_Color kBg{14, 17, 26, 255}, kPanel{24, 29, 43, 255}, kSel{45, 85, 200, 255},
    kText{230, 233, 240, 255}, kDim{140, 148, 165, 255}, kOk{90, 200, 120, 255}, kOff{78, 84, 102, 255},
    kWarn{240, 170, 70, 255}, kBad{235, 95, 95, 255};

// ---------- app ----------

// Headings before game i of a list: 1 for "Disc:" (its first game), 2 for "Installed:"
// (the first installed game). Discs come first in the list (src/library.cpp), in PS1, PS2 and PS3.
int headingsBefore(const SystemState& st, int i) {
    int discs = 0;
#if defined(__PROSPERO__)
    for (const auto& g : st.games) discs += fe::discdrive::isDiscPath(g.path);
#endif
    int mask = 0;
    if (i == 0 && discs > 0) mask |= 1;
    if (i == discs) mask |= 2;
    return mask;
}

enum class Act { None, Up, Down, PrevSys, NextSys, Launch, OpenEmu, Rescan, Quit, Back, Settings, Covers };
enum class View { Library, Settings, DiscPrompt, CancelPrompt, CoverArt, Credits, Version, DebugMenu, Controls, DiscOptions };
enum class OnInsert { Ask, Play, Install, Memory, Nothing };  // what a disc that goes in does by default

// The cover sources the API to use setting cycles through; "all" is the last.
const char* const kApiNames[] = {"libretro", "TheGamesDB", "RAWG", "all"};
const int kApiCount = 4;

int apiIndexOf(const std::string& name) {
    for (int i = 0; i < kApiCount; ++i)
        if (name == kApiNames[i]) return i;
    return kApiCount - 1;  // "all" when the value is missing or unknown
}

// The rows of Settings, in order. The Debug row is there only while Debug_Menu is on.
enum class SetRow { Covers, DiscOptions, DriveCheck, Debug, Version, Credits, Back };
enum class CoverRow { Ps1, Ps2, Ps3, All, UseApi, ApiTo, Back };
enum class DiscRow { Insert, Back };

struct App {
    Platform& plat;
    LauncherConfig& cfg;
    std::vector<SystemState> lib;
    Session ses;
    int scroll = 0;
    std::string status;
    Uint32 statusSince = 0;
    std::string access = "Desktop";  // /data unlock result (PS5)
    bool dataListable = true;
    SDL_Color statusColor = kDim;
    View view = View::Library;
    OnInsert onInsert = OnInsert::Ask;
    int menuRow = 0;       // the highlighted row of the disc prompt or the settings
    bool debugMenu = false;        // Debug_Menu in launcher.ini: the Debug row is in Settings
    bool httpProbe = false;        // http_probe in launcher.ini: the network test runs at startup
    bool debugFlashing = false;    // the Debug row flashes twice when it has just been turned on
    Uint32 debugFlashStart = 0;
    bool skipDriveCheck = false;   // DC_onstartup in launcher.ini: the drive check at startup is skipped
    bool useApi = false;           // UseApi in launcher.ini: TheGamesDB and RAWG are used for covers
    int apiIndex = kApiCount - 1;  // ApiToUse in launcher.ini: an index into kApiNames
    int settingsScroll = 0;        // the first Settings row on screen (nine rows fit)
    std::vector<int> trigger;      // the last L2 (1) and R2 (2) presses on the Version page
    bool l2Held = false;           // L2 is past half-way
#if defined(__PROSPERO__)
    SDL_Texture* arrowsTex = nullptr;  // assets/images/arrows.png (left of the button bar)
    SDL_Texture* faceTex = nullptr;    // assets/images/cstc.png (right of the button bar)
    bool buttonsLoaded = false;

    SDL_Texture* loadButtonImage(SDL_Renderer* r, const std::string& name) {
        for (const std::string dir : {"/data/homebrew/PPSA97500/assets/images/", "/app0/assets/images/"}) {
            std::string png;
            if (!plat.readFile(dir + name, png)) continue;
            int w = 0, h = 0;
            const std::vector<uint32_t> pixels = decodeButtonImage(png, w, h);
            if (pixels.empty()) return nullptr;
            SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
            if (tex == nullptr) return nullptr;
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_UpdateTexture(tex, nullptr, pixels.data(), w * 4);
            return tex;
        }
        return nullptr;
    }
#endif
    bool resetArmed = false;       // Reset All Settings: a second Cross erases launcher.ini
    int biosShownFor = -1;         // the system whose BIOS was last checked for a notification
    bool helperWarned = false;     // the helper warning has been shown
    bool cancelRequested = false;  // the user chose to cancel an install or a memory read
    bool jobIsInstall = true;      // the running job copies to the games folder (not memory)
    std::string playPath;          // the game to launch instead of the selected entry (memory play)
    bool pendingPlay = false;  // a disc was chosen to play: the loop launches it
    std::string coverPath;           // the cover on screen (empty = none)
    SDL_Texture* coverTex = nullptr;
    int coverW = 1, coverH = 1;
    bool coverWasBusy = false;       // a cover download was running at the last frame
#if defined(__PROSPERO__)
    fe::discdrive::Disc promptDisc;
    std::set<std::string> lastDiscs;  // the discs in the drives at the last change
    unsigned discGen = ~0u;
#endif

    App(Platform& p, LauncherConfig& c) : plat(p), cfg(c) {}

    SystemState& sys() { return lib[ses.system]; }

    void rescan() {
        lib = scanLibrary(plat, cfg, dataListable);
        ses.system = std::clamp(ses.system, 0, (int)lib.size() - 1);
        clampGame();
    }

    void clampGame() {
        int n = (int)sys().games.size();
        ses.game = n ? std::clamp(ses.game, 0, n - 1) : 0;
        if (ses.game < scroll) scroll = ses.game;
        // The rows from the scroll to the selection, headings included, must fit.
        while (scroll < ses.game && rowsFromTo(sys(), scroll, ses.game) > kRows) ++scroll;
        scroll = std::max(0, scroll);
    }

    // The rows a game list takes from game a to game b, with the headings in between.
    static int rowsFromTo(const SystemState& st, int a, int b) {
        int rows = 0;
        for (int i = a; i <= b; ++i) rows += 1 + __builtin_popcount(headingsBefore(st, i));
        return rows;
    }

    void setStatus(const std::string& s, SDL_Color c) {
        status = s;
        statusColor = c;
        statusSince = SDL_GetTicks();
        plat.log(s);
    }

    // Messages go after 2 seconds; a copy's progress stays while it runs.
    void expireStatus(Uint32 now) {
        if (!status.empty() && now - statusSince > 2000 && !copying()) status.clear();
    }

    bool copying() const {
#if defined(__PROSPERO__)
        return fe::discdrive::jobState() == fe::discdrive::JobRunning;
#else
        return false;
#endif
    }

    std::vector<SetRow> settingsRows() const {
        std::vector<SetRow> rows = {SetRow::Covers, SetRow::DiscOptions, SetRow::DriveCheck};
        if (debugMenu) rows.push_back(SetRow::Debug);
        rows.push_back(SetRow::Version);
        rows.push_back(SetRow::Credits);
        rows.push_back(SetRow::Back);
        return rows;
    }

    std::vector<CoverRow> coverRows() const {
        std::vector<CoverRow> rows = {CoverRow::Ps1, CoverRow::Ps2, CoverRow::Ps3, CoverRow::All, CoverRow::UseApi};
        if (useApi) rows.push_back(CoverRow::ApiTo);
        rows.push_back(CoverRow::Back);
        return rows;
    }

    std::vector<DiscRow> discRows() const {
        return {DiscRow::Insert, DiscRow::Back};
    }

    int rowOf(SetRow row) const {
        const std::vector<SetRow> rows = settingsRows();
        for (int i = 0; i < (int)rows.size(); ++i)
            if (rows[i] == row) return i;
        return 0;
    }

    // Use API for Cover Art Download: UseApi=1 or 0 in launcher.ini.
    void toggleUseApi() {
        useApi = !useApi;
        writeFlag(plat, cfg.dataDir, "UseApi", useApi);
        setStatus(std::string("Cover art downloads ") + (useApi ? "use the API" : "use libretro only"), kOk);
    }

    // API to Use: steps through the cover databases, and "all" uses every one of them.
    void cycleApi(int step) {
        apiIndex = (apiIndex + step + kApiCount) % kApiCount;
        writeValue(plat, cfg.dataDir, "ApiToUse", kApiNames[apiIndex]);
        setStatus(std::string("API to use: ") + kApiNames[apiIndex], kOk);
    }

    // Disable Drive Check on Startup: DC_onstartup=1 skips the wait for the drives when the app starts.
    void toggleDriveCheck() {
        skipDriveCheck = !skipDriveCheck;
        writeFlag(plat, cfg.dataDir, "DC_onstartup", skipDriveCheck);
        setStatus(std::string("Drive check on startup is ") + (skipDriveCheck ? "disabled" : "on"), kOk);
    }

    // The Debug row blinks five times, 0.3 s each, in the selection colour, when it has just been turned on.
    SDL_Color debugRowColor(bool selected) const {
        if (debugFlashing) {
            const Uint32 t = SDL_GetTicks() - debugFlashStart;
            if (t < 3000) return (t / 300) % 2 == 0 ? kSel : kPanel;  // five blinks, 0.3 s each
        }
        return selected ? kSel : kPanel;
    }

    // A tap on the touchpad opens the controls, and another tap or Circle closes them.
    void toggleControls() {
        view = view == View::Controls ? View::Library : View::Controls;
    }

    void openSettings() {
        view = View::Settings;
        menuRow = 0;
        settingsScroll = 0;
    }

    // The Version page: L2 (1) and R2 (2). L2, R2, L2, R2 turns the Debug row on and takes the user to Settings.
    void triggerPress(int which) {
        trigger.push_back(which);
        if (trigger.size() > 4) trigger.erase(trigger.begin());
        if (trigger != std::vector<int>{1, 2, 1, 2}) return;
        trigger.clear();
        const bool first = !debugMenu;
        debugMenu = true;
        writeFlag(plat, cfg.dataDir, "Debug_Menu", true);
        view = View::Settings;
        menuRow = rowOf(SetRow::Debug);
        if (first) {
            debugFlashing = true;
            debugFlashStart = SDL_GetTicks();
        }
    }

    // Settings > Debug Menu: Reset All Settings, Enable http-probe, Disable Debug Mode, Back.
    void debugMenuConfirm() {
        switch (menuRow) {
            case 0:
                if (!resetArmed) {
                    resetArmed = true;
                    setStatus("Press Cross again to erase launcher.ini and restore the stock settings", kWarn);
                } else {
                    resetSettings();
                }
                break;
            case 1: toggleProbe(); break;
            case 2:
                debugMenu = false;
                writeFlag(plat, cfg.dataDir, "Debug_Menu", false);
                resetArmed = false;
                view = View::Library;
                setStatus("Debug mode is off", kOk);
                break;
            default:
                resetArmed = false;
                view = View::Settings;
                menuRow = rowOf(SetRow::Debug);
                break;
        }
    }

    void toggleProbe() {
        httpProbe = !httpProbe;
        writeFlag(plat, cfg.dataDir, "http_probe", httpProbe);
        setStatus(std::string("http-probe on startup: ") + (httpProbe ? "on" : "off"), kOk);
    }

    // Erases launcher.ini and writes the stock one from the app's bundled copy. Takes effect at the next start.
    void resetSettings() {
        resetArmed = false;
        std::string stock;
        if (!plat.readFile("/data/homebrew/PPSA97500/assets/launcher.ini", stock) &&
            !plat.readFile("/app0/assets/launcher.ini", stock)) {
            setStatus("Could not find the stock launcher.ini", kBad);
            return;
        }
        if (!plat.writeFile(cfg.dataDir + "/launcher.ini", stock)) {
            setStatus("Could not write launcher.ini", kBad);
            return;
        }
        debugMenu = false;
        httpProbe = false;
        view = View::Settings;
        menuRow = 0;
        setStatus("launcher.ini restored to the stock settings. Restart the launcher to use them.", kOk);
    }

    const char* onInsertName() const {
        static const char* names[] = {"ask", "play", "install", "memory", "nothing"};
        return names[(int)onInsert];
    }

    void changeOnInsert(int step) {
        onInsert = (OnInsert)(((int)onInsert + step + 5) % 5);
        plat.writeFile(cfg.dataDir + "/disc.ini", std::string("on_insert=") + onInsertName() + "\n");
    }

    void loadDiscSettings() {
        std::string text;
        if (!plat.readFile(cfg.dataDir + "/disc.ini", text)) return;
        if (text.find("on_insert=play") != std::string::npos) onInsert = OnInsert::Play;
        else if (text.find("on_insert=install") != std::string::npos) onInsert = OnInsert::Install;
        else if (text.find("on_insert=memory") != std::string::npos) onInsert = OnInsert::Memory;
        else if (text.find("on_insert=nothing") != std::string::npos) onInsert = OnInsert::Nothing;
    }

#if defined(__PROSPERO__)
    static std::string discKey(const fe::discdrive::Disc& d) { return d.device + "|" + d.name; }

    // A playable disc that was not in the drives at the last change: the library is
    // rescanned, and the prompt opens unless something else is on screen.
    void checkDiscs() {
        const unsigned gen = fe::discdrive::generation();
        if (gen == discGen) return;
        discGen = gen;
        std::set<std::string> present;
        bool fresh = false;
        for (const auto& d : fe::discdrive::discs()) {
            if (!d.playable) continue;
            present.insert(discKey(d));
            if (!lastDiscs.count(discKey(d)) && !d.ps2 && !d.ps3 && !d.other) {  // a PS2, PS3 or other disc is listed, not prompted
                promptDisc = d;
                fresh = true;
            }
        }
        lastDiscs = present;
        rescan();
        if (fresh && view == View::Library && !copying() && onInsert != OnInsert::Nothing) {
            openDiscPrompt();
        }
    }

    // A copy or a memory read that finished or failed: the library shows the new game,
    // or the reason. A memory read is played straight away.
    void checkCopy() {
        const int state = fe::discdrive::jobState();
        if (state == fe::discdrive::JobDone) {
            if (jobIsInstall) {
                const std::string made = fe::discdrive::copiedPath();
                fe::discdrive::finishJob();
                rescan();
                setStatus("Copied to the games folder: " + made, kOk);
            } else {
                fe::discdrive::finishJob();
                selectDisc();
                if (pendingPlay) playPath = fe::discdrive::ramPath(promptDisc);
            }
        } else if (state == fe::discdrive::JobFailed) {
            const std::string why = fe::discdrive::lastError();
            fe::discdrive::finishJob();
            if (cancelRequested) {
                cancelRequested = false;
                setStatus(jobIsInstall ? "Installation canceled by user" : "Reading canceled by user", kWarn);
                return;
            }
            setStatus((jobIsInstall ? "Copy failed: " : "Couldn't read the disc: ") + why, kBad);
        }
    }

    // Circle, then Cross: the install (or the memory read) is stopped.
    void cancelInstall() {
        cancelRequested = true;
        fe::discdrive::cancel();
        view = View::Library;
        setStatus(jobIsInstall ? "Canceling the install..." : "Canceling...", kWarn);
    }

    // Copy into memory and play: the disc is read into memory, then its game starts.
    void playFromMemory() {
        if (copying()) {
            setStatus("Wait for the install to finish or cancel it", kWarn);
            return;
        }
        cancelRequested = false;
        jobIsInstall = false;
        fe::discdrive::startRam(promptDisc);
        setStatus("Reading " + promptDisc.name + " into memory...", kDim);
    }

    // The disc menu for the disc entry that is selected (Triangle), or for a disc that went in.
    void openDiscPrompt() {
        view = View::DiscPrompt;
        menuRow = onInsert == OnInsert::Install ? 1 : onInsert == OnInsert::Memory ? 2 : 0;
    }

    // Eject: the drive's tray opens with the disc in it.
    void ejectDisc() {
        if (copying()) {
            setStatus("Wait for the install to finish or cancel it", kWarn);
            return;
        }
        std::string why;
        if (fe::discdrive::eject(promptDisc.device, why))
            setStatus("Ejected " + promptDisc.name, kOk);
        else
            setStatus("Couldn't eject: " + why, kBad);
    }

    // Holding Triangle on the library: the first disc in the drive is ejected.
    void ejectFirstDisc() {
        for (const auto& d : fe::discdrive::discs())
            if (d.playable) {
                promptDisc = d;
                ejectDisc();
                return;
            }
        setStatus("No disc in the drive.", kWarn);
    }

    // Play: select the disc's entry in the PS1 list, for the loop to launch.
    void selectDisc() {
        const std::string path = fe::discdrive::streamPath(promptDisc);
        for (int s = 0; s < (int)lib.size(); ++s)
            for (int g = 0; g < (int)lib[s].games.size(); ++g)
                if (lib[s].games[g].path == path || lib[s].games[g].path == promptDisc.device) {
                    ses.system = s;
                    ses.game = g;
                    clampGame();
                    pendingPlay = true;
                    return;
                }
        setStatus("That disc is no longer in the drive.", kWarn);
    }

    // Install: copy the disc to the internal games folder, as a game in the library.
    void install() {
        if (copying()) {
            setStatus("A copy is already running.", kWarn);
            return;
        }
        if (promptDisc.ps2 || promptDisc.ps3) {  // ISO copies: PS2 as is, PS3 decrypted
            cancelRequested = false;
            jobIsInstall = true;
            fe::discdrive::startIso(promptDisc);
            setStatus("Copying " + promptDisc.name + " to the games folder...", kDim);
            return;
        }
        std::string why;
        if (fe::discdrive::destinationFolder(fe::discdrive::ToGames, &why).empty()) {
            setStatus("Can't copy: " + why, kBad);
            return;
        }
        cancelRequested = false;
        jobIsInstall = true;
        fe::discdrive::startCopy(promptDisc, fe::discdrive::ToGames);
        setStatus("Copying " + promptDisc.name + " to the internal disk...", kDim);
    }
#endif

    void confirm() {
        if (view == View::Controls) {
            view = View::Library;
            return;
        }
        if (view == View::CancelPrompt) {
            cancelInstall();
            return;
        }
        if (view == View::Settings) {
            switch (settingsRows()[menuRow]) {
                case SetRow::Covers: view = View::CoverArt; menuRow = 0; break;
                case SetRow::DiscOptions: view = View::DiscOptions; menuRow = 0; break;
                case SetRow::DriveCheck: toggleDriveCheck(); break;
                case SetRow::Debug:
                    debugFlashing = false;  // opening it stops the blinking
                    view = View::DebugMenu;
                    menuRow = 0;
                    resetArmed = false;
                    break;
                case SetRow::Version: view = View::Version; break;
                case SetRow::Credits: view = View::Credits; break;
                case SetRow::Back: view = View::Library; break;
            }
            return;
        }
        if (view == View::DiscOptions) {
            if (discRows()[menuRow] == DiscRow::Insert) changeOnInsert(1);
            else backFromModal();
            return;
        }
        if (view == View::CoverArt) {
            switch (coverRows()[menuRow]) {
                case CoverRow::Ps1: downloadCovers(2); break;
                case CoverRow::Ps2: downloadCovers(3); break;
                case CoverRow::Ps3: downloadCovers(4); break;
                case CoverRow::All: downloadCovers(5); break;
                case CoverRow::UseApi: toggleUseApi(); break;
                case CoverRow::ApiTo: cycleApi(1); break;
                case CoverRow::Back: backFromModal(); break;
            }
            return;
        }
        if (view == View::DebugMenu) {
            debugMenuConfirm();
            return;
        }
        if (view == View::CoverArt) {
            if (menuRow < 4) downloadCovers(menuRow + 2);  // 2 PS1, 3 PS2, 4 PS3, 5 all systems
            else backFromModal();
            return;
        }
        if (view == View::Credits || view == View::Version) {
            backFromModal();
            return;
        }
#if defined(__PROSPERO__)
        view = View::Library;
        if (promptDisc.ps3) {  // a PS3 disc: play from the drive, install it, or not now
            if (menuRow == 0) selectDisc();
            else if (menuRow == 1) install();
            return;
        }
        if (menuRow == 0) selectDisc();
        else if (menuRow == 1) install();
        else if (menuRow == 2 && !promptDisc.ps2) playFromMemory();
#endif
    }

    // Keys and buttons while the prompt or the settings is open.
    void modal(Act a) {
        const int rows = view == View::DiscPrompt ? (promptDisc.ps3 || promptDisc.ps2 ? 3 : 4) : view == View::CoverArt ? (int)coverRows().size() : view == View::DebugMenu ? 4 : view == View::Settings ? (int)settingsRows().size() : view == View::DiscOptions ? 2 : 1;
        switch (a) {
            case Act::Up: menuRow = (menuRow + rows - 1) % rows; break;
            case Act::Down: menuRow = (menuRow + 1) % rows; break;
            case Act::PrevSys:
            case Act::NextSys:
                if (view == View::DiscOptions && discRows()[menuRow] == DiscRow::Insert) changeOnInsert(a == Act::NextSys ? 1 : -1);
                if (view == View::Settings && settingsRows()[menuRow] == SetRow::DriveCheck) toggleDriveCheck();
                if (view == View::CoverArt && coverRows()[menuRow] == CoverRow::UseApi) toggleUseApi();
                if (view == View::CoverArt && coverRows()[menuRow] == CoverRow::ApiTo) cycleApi(a == Act::NextSys ? 1 : -1);
                if (view == View::DebugMenu && menuRow == 1) toggleProbe();
                break;
            case Act::Launch: confirm(); break;
            case Act::Back:
            case Act::OpenEmu:
            case Act::Quit: backFromModal(); break;
            default: break;
        }
    }

    covers::Keys coverKeys() const { return {cfg.thegamesdbKey, cfg.rawgKey, useApi, kApiNames[apiIndex]}; }

    // Share: downloads the cover of the selected game.
    void coverSelected() {
        const SystemState& st = sys();
        if (st.games.empty()) { setStatus("No game selected", kWarn); return; }
        const std::string title = st.games[ses.game].title;
        if (!covers::startJob(cfg.dataDir, {covers::Item{st.cfg->id, title}}, coverKeys()))
            setStatus("Covers are already downloading", kWarn);
        else
            setStatus("Downloading the cover of " + title, kDim);
    }

    // Settings: downloads the covers of PS1 (row 2), PS2 (row 3), PS3 (row 4), or of every system (row 5).
    void downloadCovers(int row) {
        view = View::Library;
        std::vector<covers::Item> items;
        for (const SystemState& s : lib) {
            if (row != 5 && s.cfg->id != "ps" + std::to_string(row - 1)) continue;
            for (const auto& g : s.games) items.push_back(covers::Item{s.cfg->id, g.title});
        }
        if (items.empty()) { setStatus("No games to download covers for", kWarn); return; }
        if (!covers::startJob(cfg.dataDir, items, coverKeys()))
            setStatus("Covers are already downloading", kWarn);
        else
            setStatus("Downloading covers for " + std::to_string(items.size()) + " games", kDim);
    }

    // The selected game's cover in the right field, as large as fits without distortion. With `backdrop` (PS1 and PS2),
    // the field from the library panel's edge to the screen's right edge is first filled with the cover, dimmed.
    void drawCover(SDL_Renderer* r, int px, int py, int pw, int ph, bool backdrop) {
#if defined(__PROSPERO__)
        const SystemState& st = sys();
        const int fx = 1180, fw = kW - fx;  // the library panel ends at 1180
        const SDL_Color dim{14, 17, 26, 160};
        const std::string path = st.games.empty() ? std::string()
                                                  : covers::existing(cfg.dataDir, st.cfg->id, st.games[ses.game].title);
        if (path != coverPath) {
            coverPath = path;
            if (coverTex) SDL_DestroyTexture(coverTex);
            coverTex = nullptr;
            std::string png;
            int w = 0, h = 0;
            if (!path.empty() && plat.readFile(path, png)) {
                const std::vector<uint32_t> pixels = decodeIcon(png, w, h);
                if (!pixels.empty()) {
                    coverTex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
                    if (coverTex) SDL_UpdateTexture(coverTex, nullptr, pixels.data(), w * 4);
                    coverW = w;
                    coverH = h;
                }
            }
        }
        if (coverTex == nullptr) {
            if (backdrop) fillRect(r, fx, py, fw, ph, kPanel);
            return;
        }
        if (backdrop) {
            SDL_SetTextureBlendMode(coverTex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(coverTex, 255);
            SDL_Rect back{fx, py, fw, ph};
            SDL_RenderCopy(r, coverTex, nullptr, &back);
            fillRect(r, fx, py, fw, ph, dim);
        }
        const float scale = std::min((float)pw / coverW, (float)ph / coverH);
        SDL_Rect dst{px + ((int)pw - (int)(coverW * scale)) / 2, py + ((int)ph - (int)(coverH * scale)) / 2,
                     (int)(coverW * scale), (int)(coverH * scale)};
        SDL_RenderCopy(r, coverTex, nullptr, &dst);
#else
        (void)r;
        (void)px;
        (void)py;
        (void)pw;
        (void)ph;
#endif
    }

    // The controls, opened by a tap on the touchpad: the pictures with what they do, and the other controls.
    void drawControls(SDL_Renderer* r) {
        const int px = 260, py = 150, pw = 1400, ph = 730;
        fillRect(r, px, py, pw, ph, kPanel);
        drawText(r, px + 40, py + 30, 4, "CONTROLS", kText);
#if defined(__PROSPERO__)
        // The arrows, then the face buttons to their right, at the same height.
        const int picH = 340;  // twice the height the pictures had
        int arrowsW = 0;
        if (arrowsTex) {
            int w = 0, h = 0;
            SDL_QueryTexture(arrowsTex, nullptr, nullptr, &w, &h);
            arrowsW = h > 0 ? w * picH / h : picH;
            SDL_Rect dst{px + 40, py + 100, arrowsW, picH};
            SDL_RenderCopy(r, arrowsTex, nullptr, &dst);
        }
        if (faceTex) {
            int w = 0, h = 0;
            SDL_QueryTexture(faceTex, nullptr, nullptr, &w, &h);
            const int faceW = h > 0 ? w * picH / h : picH;
            SDL_Rect dst{px + 40 + arrowsW + 40, py + 100, faceW, picH};
            SDL_RenderCopy(r, faceTex, nullptr, &dst);
        }
#endif
        drawText(r, px + 40, py + 520, 3, "L1/R1: switch system", kText);
        drawText(r, px + 40, py + 560, 3, "R2: download the cover art of the selected game", kText);
        drawText(r, px + 40, py + 600, 3, "Options: settings", kText);
        drawText(r, px + 40, py + 640, 3, "Hold Touchpad: quit", kText);
        drawText(r, px + 40, py + ph - 60, 2.4f, "Press Circle to close", kDim);
    }

    // Settings > Debug Menu.
    void drawDebugMenu(SDL_Renderer* r) {
        drawText(r, 80, 50, 5, "DEBUG MENU", kText);
        const char* labels[] = {"Reset All Settings", "", "Disable Debug Mode", "Back"};
        const int y0 = 200;
        for (int i = 0; i < 4; ++i) {
            fillRect(r, 80, y0 + i * 90, 1760, 70, menuRow == i ? kSel : kPanel);
            const std::string label = i == 1 ? std::string("Enable http-probe:   < ") + (httpProbe ? "On" : "Off") + " >"
                                             : labels[i];
            drawText(r, 110, y0 + i * 90 + 18, 3, label, kText);
        }
        drawText(r, 80, 1030, 2.2f, "Up/Down: choose   Left/Right: change   Cross: select   Triangle: back", kDim);
    }

    void drawDiscPrompt(SDL_Renderer* r) {
#if defined(__PROSPERO__)
        const int x = 460, y = 280, w = 1000, h = 500;
        fillRect(r, x, y, w, h, kPanel);
        drawText(r, x + 40, y + 40, 3.5f, "A disc is in the drive", kText);
        std::string name = promptDisc.name.empty() ? promptDisc.serial : promptDisc.name;
        if (name.size() > 60) name = name.substr(0, 57) + "...";
        drawText(r, x + 40, y + 110, 2.8f, name, kDim);
        const char* labels[] = {"Play from the disc", "Install to the internal disk", "Copy into memory and play",
                                "Not now"};
        const int count = promptDisc.ps3 || promptDisc.ps2 ? 3 : 4;
        if (promptDisc.ps3 || promptDisc.ps2) labels[2] = "Not now";
        for (int i = 0; i < count; ++i) {
            const int ry = y + 170 + i * 60;
            if (i == menuRow) fillRect(r, x + 30, ry, w - 60, 50, kSel);
            std::string label = labels[i];
            if ((i == 0 && onInsert == OnInsert::Play) || (i == 1 && onInsert == OnInsert::Install) ||
                (i == 2 && onInsert == OnInsert::Memory))
                label += "   (default)";
            drawText(r, x + 50, ry + 10, 3, label, kText);
        }
        drawText(r, x + 40, y + h - 50, 2.2f, "Up/Down: choose   Cross: confirm   Triangle: not now", kDim);
#else
        (void)r;
#endif
    }

    void drawCancelPrompt(SDL_Renderer* r) {
#if defined(__PROSPERO__)
        const int x = 560, y = 380, w = 800, h = 260;
        fillRect(r, x, y, w, h, kPanel);
        drawText(r, x + 40, y + 50, 3.5f, jobIsInstall ? "Cancel the install?" : "Cancel reading the disc?", kText);
        drawText(r, x + 40, y + 130, 2.8f, "Cross: yes, cancel     Circle: keep going", kDim);
#else
        (void)r;
#endif
    }

    void drawSettings(SDL_Renderer* r) {
        drawText(r, 80, 50, 5, "SETTINGS", kText);
        const int y0 = 200, shown = 9;
        const std::vector<SetRow> rows = settingsRows();
        // Keep the selected row on screen: nine rows at a time.
        if (menuRow < settingsScroll) settingsScroll = menuRow;
        if (menuRow >= settingsScroll + shown) settingsScroll = menuRow - shown + 1;
        for (int i = settingsScroll; i < (int)rows.size() && i < settingsScroll + shown; ++i) {
            const int slot = i - settingsScroll;
            std::string label;
            SDL_Color fill = menuRow == i ? kSel : kPanel;
            switch (rows[i]) {
                case SetRow::Covers: label = "Cover Art"; break;
                case SetRow::DiscOptions: label = "Disc Options"; break;
                case SetRow::DriveCheck:
                    label = std::string("Disable Drive Check on Startup:   < ") + (skipDriveCheck ? "On" : "Off") + " >";
                    break;
                case SetRow::Debug:
                    label = "Debug";
                    fill = debugRowColor(menuRow == i);
                    break;
                case SetRow::Version: label = "Version"; break;
                case SetRow::Credits: label = "Credits"; break;
                case SetRow::Back: label = "Back"; break;
            }
            fillRect(r, 80, y0 + slot * 90, 1760, 70, fill);
            drawText(r, 110, y0 + slot * 90 + 18, 3, label, kText);
        }
        drawText(r, 80, 1030, 2.2f,
                 "Up/Down: choose   Left/Right: change   Cross: select   Triangle: back", kDim);
    }

    // Settings > Cover Art: one system, or all of them, and the cover database settings.
    void drawCoverArt(SDL_Renderer* r) {
        drawText(r, 80, 50, 5, "COVER ART", kText);
        const char* labels[] = {"Download PS1 Cover Art", "Download PS2 Cover Art", "Download PS3 Cover Art",
                                "Download Cover Art for All Systems"};
        const std::vector<CoverRow> rows = coverRows();
        const int y0 = 200;
        for (int i = 0; i < (int)rows.size(); ++i) {
            std::string label;
            switch (rows[i]) {
                case CoverRow::Ps1: label = labels[0]; break;
                case CoverRow::Ps2: label = labels[1]; break;
                case CoverRow::Ps3: label = labels[2]; break;
                case CoverRow::All: label = labels[3]; break;
                case CoverRow::UseApi:
                    label = std::string("Use API for Cover Art Download:   < ") + (useApi ? "On" : "Off") + " >";
                    break;
                case CoverRow::ApiTo: label = std::string("API to Use:   < ") + kApiNames[apiIndex] + " >"; break;
                case CoverRow::Back: label = "Back"; break;
            }
            fillRect(r, 80, y0 + i * 90, 1760, 70, menuRow == i ? kSel : kPanel);
            drawText(r, 110, y0 + i * 90 + 18, 3, label, kText);
        }
        drawText(r, 80, 1030, 2.2f,
                 "Up/Down: choose   Left/Right: change   Cross: select   Triangle: back", kDim);
    }

    // Settings > Disc Options: what happens when a disc goes in.
    void drawDiscOptions(SDL_Renderer* r) {
        drawText(r, 80, 50, 5, "DISC OPTIONS", kText);
        const char* values[] = {"Ask each time", "Play from the disc", "Install to the internal disk",
                                "Copy into memory and play", "Do nothing"};
        const std::vector<DiscRow> rows = discRows();
        const int y0 = 200;
        for (int i = 0; i < (int)rows.size(); ++i) {
            const std::string label = rows[i] == DiscRow::Insert
                                          ? std::string("When a disc is inserted:   < ") + values[(int)onInsert] + " >"
                                          : std::string("Back");
            fillRect(r, 80, y0 + i * 90, 1760, 70, menuRow == i ? kSel : kPanel);
            drawText(r, 110, y0 + i * 90 + 18, 3, label, kText);
        }
        drawText(r, 80, 1030, 2.2f,
                 "Up/Down: choose   Left/Right: change   Cross: select   Triangle: back", kDim);
    }

    // Settings > Credits: the emulators, the authors, the data and the APIs this launcher uses.
    void drawCredits(SDL_Renderer* r) {
        drawText(r, 80, 50, 5, "CREDITS", kText);
        struct Line { const char* text; bool heading; };
        const Line lines[] = {
            {"Emulators", true},
            {"PSSwanStation by Press5elect: the PS1 emulator built into this launcher (GPL-3.0-or-later)", false},
            {"SwanStation, libretro's fork of DuckStation, on which PSSwanStation is based (GPL-3.0)", false},
            {"PS5SX2 by Swordpdf, the PS2 emulator this launcher starts: a port of PCSX2 by the PCSX2 Dev Team (GPL-3.0-or-later)", false},
            {"RPCS3-5 (KB-RPCS3) by kamb205, the PS3 emulator this launcher starts: a port of RPCS3 by the RPCS3 team (GPL-2.0-only core)", false},
            {"Build tooling and runtime", true},
            {"ps5-native-app-boilerplate by BlackBearReloaded, via SwanStationPS5 by darkxex (GPL-3.0-or-later)", false},
            {"PS5 Payload SDK by John Tornblom (ps5-payload-dev), with LLVM libc++ (Apache-2.0)", false},
            {"SDL2 (PacBrew port), zlib; libiconv (LGPL-2.1); libsamplerate (BSD-2-Clause)", false},
            {"mbedTLS 3.6.7 by the Mbed TLS project (Apache-2.0)", false},
            {"HTTPS client and JSON reader from PSSwanStation by Press5elect (GPL-3.0-or-later)", false},
            {"stb_easy_font and stb_image by Sean Barrett (public domain or MIT)", false},
            {"lapy.elf by BlackBearReloaded and ArkSama (Team PHU), built on libhijacker (MIT)", false},
            {"Data and APIs", true},
            {"TheGamesDB (thegamesdb.net): box art, with your own key", false},
            {"RAWG (rawg.io): pictures, with your own key", false},
            {"GameTDB (gametdb.com): PS3 game titles, used with GameTDB's permission", false},
            {"libretro thumbnails (thumbnails.libretro.com): covers", false},
            {"libretro-database: PS1 and PS2 game names (CC BY-SA 4.0)", false},
            {"GameDB-PS2 by niemasd: PS2 game titles (GPL-3.0), from GameFAQs, MiSTer Addons, PlayStation Datacenter, Redump and VGCollect", false},
            {"GameDB-PSX by niemasd: PS1 game titles (GPL-3.0), from several public sources (see CREDITS.md)", false},
            {"Sony", true},
            {"PlayStation and PS5 are trademarks of Sony Interactive Entertainment Inc. TotalEmu is not affiliated with or endorsed by Sony.", false},
        };
        int y = 150;
        for (const Line& l : lines) {
            if (l.heading) {
                y += 8;
                drawText(r, 80, y, 2.6f, l.text, kText);
                y += 36;
            } else {
                drawText(r, 80, y, 2.0f, l.text, kDim);
                y += 30;
            }
        }
        drawText(r, 80, 1030, 2.2f, "Cross or Triangle: back", kDim);
    }

    // Settings > Version: the version of this build.
    void drawVersion(SDL_Renderer* r) {
        drawText(r, 80, 50, 5, "VERSION", kText);
        drawText(r, 110, 220, 3.5f, std::string("TotalEmu ") + kVersion, kText);
        drawText(r, 80, 1030, 2.2f, "Cross or Triangle: back", kDim);
    }

    // Back from a page: a page under Settings returns to Settings, Settings returns to the library.
    void backFromModal() {
        if (view == View::CoverArt) { view = View::Settings; menuRow = rowOf(SetRow::Covers); }
        else if (view == View::DiscOptions) { view = View::Settings; menuRow = rowOf(SetRow::DiscOptions); }
        else if (view == View::Credits) { view = View::Settings; menuRow = rowOf(SetRow::Credits); }
        else if (view == View::Version) { view = View::Settings; menuRow = rowOf(SetRow::Version); }
        else if (view == View::DebugMenu) { view = View::Settings; menuRow = rowOf(SetRow::Debug); resetArmed = false; }
        else view = View::Library;
    }

    // Returns true when the launcher should exit (PS5: the emulator takes over).
    bool launch(SDL_Window* win, bool withGame) {
        SystemState& st = sys();
        const SystemConfig& sc = *st.cfg;
        // BIOS problems are shown, never enforced: emulators handle their own BIOS
        // (SwanStation even has a built-in one), and the sandbox can hide real files.

        std::string game;
        if (withGame) {
            if (st.games.empty()) { setStatus("No games found for " + sc.name, kWarn); return false; }
            game = st.games[ses.game].path;
        }
        saveSession(plat, cfg.dataDir, ses);
        if (sc.passGame && !game.empty()) writeHandoff(plat, cfg.dataDir, sc.id, game);
        // An emulator that reads a launch file starts this game itself (PS5SX2's disc-launch.txt):
        // line 1 the game's path, line 2 its serial. The emulator only takes a fresh file.
        bool filed = false;
        if (!sc.launchFile.empty() && !game.empty()) {
#if defined(__PROSPERO__)
            const std::string serial = serialIn(st.games[ses.game].title);
#else
            const std::string serial;
#endif
            const std::string dir = sc.launchFile.substr(0, sc.launchFile.find_last_of('/'));
            plat.makeDirs(dir);
            if (!plat.writeFile(sc.launchFile, game + "\n" + serial + "\n")) {
                setStatus("Couldn't write " + sc.launchFile, kBad);
                return false;
            }
            filed = true;
        }

        if (plat.launchBlocks() && win) SDL_HideWindow(win);
        std::string err;
        bool ok = plat.launch(sc, (sc.passGame || !sc.launchArg.empty()) ? game : "", err);
        if (plat.launchBlocks() && win) { SDL_ShowWindow(win); SDL_RaiseWindow(win); }

        if (!ok) { setStatus("Launch failed: " + err, kBad); return false; }
        if (filed)
            setStatus("Starting " + st.games[ses.game].title + " in " + sc.name, kOk);
        else if (withGame && !sc.passGame && sc.id != "ps5")
            setStatus(sc.name + " opened. It can't take a game yet: pick it in its own menu.", kWarn);
        else
            setStatus("Started " + sc.name, kOk);
        return !plat.launchBlocks() && cfg.exitAfterLaunch;
    }

    void render(SDL_Renderer* r) {
        fillRect(r, 0, 0, kW, kH, kBg);
        // A cover download that has ended says what it did.
        if (covers::busy()) {
            coverWasBusy = true;
        } else if (coverWasBusy) {
            coverWasBusy = false;
            setStatus(covers::status(), kOk);
        }
        if (view == View::Settings) {
            drawSettings(r);
            return;
        }
        if (view == View::CoverArt) {
            drawCoverArt(r);
            return;
        }
        if (view == View::DiscOptions) {
            drawDiscOptions(r);
            return;
        }
        if (view == View::Credits) {
            drawCredits(r);
            return;
        }
        if (view == View::Version) {
            drawVersion(r);
            return;
        }
        if (view == View::DebugMenu) {
            drawDebugMenu(r);
            return;
        }
        drawText(r, 80, 50, 5, "TotalEmu", kText);
        drawText(r, 80 + textWidth("TotalEmu", 5) + 30, 72, 2.5f,
                 std::string("experimental - ") + plat.name(), kDim);

        // System tabs
        float x = 80;
        for (int i = 0; i < (int)lib.size(); ++i) {
            const std::string& n = lib[i].cfg->name;
            float w = textWidth(n, 3.5f) + 50;
            fillRect(r, (int)x, 150, (int)w, 60, i == ses.system ? kSel : kPanel);
            drawText(r, x + 25, 165, 3.5f, n, kText);
            x += w + 16;
        }

        // Game list
        SystemState& st = sys();
        // The notifications: a missing BIOS when a system is shown, and a helper that is not running (once).
        if (biosShownFor != ses.system) {
            biosShownFor = ses.system;
            if (st.bios == SystemState::Bios::Missing) setStatus(st.biosMsg, kWarn);
        }
        if (!helperWarned && (st.sandboxed || !dataListable)) {
            helperWarned = true;
            setStatus("Helper not running: load pslauncher-helper.elf via Payload Manager, then restart this app.", kWarn);
        }
        fillRect(r, 80, kListY - 10, 1100, kRows * kRowH + 20 + kRowH, kPanel);
        if (st.games.empty()) {
            drawText(r, 110, kListY + 20, 3, "No games found.", kDim);
            int y = kListY + 80;
            for (const auto& d : st.cfg->gameDirs) {
                drawText(r, 110, y, 2.5f, "Put games in: " + d, kDim);
                y += 40;
            }
            drawText(r, 110, y + 20, 2.5f, "Triangle still opens the emulator's own menu.", kDim);
        }
        const int bottom = kListY + kRows * kRowH;
        int gy = kListY;
        for (int i = scroll; i < (int)st.games.size(); ++i) {
            const int heads = headingsBefore(st, i);
            if (gy + (1 + __builtin_popcount(heads)) * kRowH > bottom) break;
            if (heads & 1) { drawText(r, 110, gy + 10, 2.5f, "Disc:", kDim); gy += kRowH; }
            if (heads & 2) { drawText(r, 110, gy + 10, 2.5f, "Installed:", kDim); gy += kRowH; }
            if (i == ses.game) fillRect(r, 90, gy, 1080, kRowH - 6, kSel);
            std::string t = st.games[i].title;
            if (t.size() > 52) t = t.substr(0, 49) + "...";
            drawText(r, 110, gy + 10, 3, t, kText);
            gy += kRowH;
        }
        // The count of games under the list, and the position in it when the list scrolls.
        const int countY = kListY + kRows * kRowH + 10;
        drawText(r, 110, countY, 2.5f, num((long long)st.games.size()) + " games", kDim);
        if ((int)st.games.size() > kRows)
            drawText(r, 1000, countY, 2.5f, num(ses.game + 1) + " / " + num((long long)st.games.size()), kDim);

        // The right field is the game's cover, as large as fits in it without distortion. On PS1 and PS2 the space
        // around it is filled with the cover itself, dimmed.
        const int px = 1200, py = kListY - 10, pw = 680, ph = kRows * kRowH + 20 + kRowH;
        const bool backdrop = st.cfg->id == "ps1" || st.cfg->id == "ps2";
#if defined(__PROSPERO__)
        drawCover(r, px, py, pw, ph, backdrop);
#endif

        // Status + help
#if defined(__PROSPERO__)
        if (copying()) {
            const auto job = fe::discdrive::status();
            std::string line = job.text;
            if (job.progress >= 0) line += "   " + num((long long)(job.progress * 100)) + "%";
            drawText(r, 220, 985, 2.5f, line, kDim);
            if (job.progress >= 0) fillRect(r, 220, 1020, (int)(600 * job.progress), 6, kOk);
        } else
#endif
        drawText(r, 220, 985, 2.5f, covers::busy() ? covers::status() : status,
                 covers::busy() ? kWarn : statusColor);
        // The built-in PS1 has no emulator to open, so Triangle only means the disc menu there.
#if defined(__PROSPERO__)
        if (!buttonsLoaded) {
            buttonsLoaded = true;
            arrowsTex = loadButtonImage(r, "arrows.png");
            faceTex = loadButtonImage(r, "cstc.png");
        }
#endif
        drawText(r, 80, 1030, 2.2f, "Tap Touchpad to See Controls", kText);
        if (view == View::Controls) drawControls(r);
        if (view == View::DiscPrompt) drawDiscPrompt(r);
        if (view == View::CancelPrompt) drawCancelPrompt(r);
    }

    // PS4 and PS5 tabs list discs and games only: nothing on them can be started or installed yet.
    bool notStartable() {
        const SystemConfig& cfgSys = *sys().cfg;
        if (cfgSys.id != "ps4" && cfgSys.id != "ps5") return false;
        // The PS5 title_id entry starts its installed title.
        if (cfgSys.id == "ps5" && !sys().games.empty() && sys().games[ses.game].path == cfgSys.titleId) return false;
        setStatus("PS4 and PS5 games and discs can't be started or installed here yet", kWarn);
        return true;
    }

    // A disc entry is selected in the library: Circle can install it.
    bool discSelected() {
#if defined(__PROSPERO__)
        if (sys().games.empty()) return false;
        const std::string& path = sys().games[ses.game].path;
        return fe::discdrive::isDrivePath(path) || path.rfind("/dev/cd", 0) == 0;
#else
        return false;
#endif
    }

    void installSelected() {
#if defined(__PROSPERO__)
        if (copying()) {  // Circle during an install asks before it cancels
            view = View::CancelPrompt;
            return;
        }
        if (notStartable()) return;
        if (!discSelected()) return;
        const std::string path = sys().games[ses.game].path;
        for (const auto& d : fe::discdrive::discs())
            if (fe::discdrive::streamPath(d) == path || d.device == path) {
                promptDisc = d;
                install();
                return;
            }
        setStatus("That disc is no longer in the drive.", kWarn);
#endif
    }

    void apply(Act a) {
        switch (a) {
            case Act::Up: ses.game--; clampGame(); break;
            case Act::Down: ses.game++; clampGame(); break;
            case Act::PrevSys:
                ses.system = (ses.system + (int)lib.size() - 1) % (int)lib.size();
                ses.game = 0; scroll = 0; clampGame(); break;
            case Act::NextSys:
                ses.system = (ses.system + 1) % (int)lib.size();
                ses.game = 0; scroll = 0; clampGame(); break;
            case Act::Rescan: fe::discdrive::recheck(); rescan(); setStatus("Library rescanned", kDim); break;
            case Act::Back: installSelected(); break;
            case Act::Covers: coverSelected(); break;
            default: break;
        }
    }
};

Act fromButton(Uint8 b) {
    switch (b) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return Act::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return Act::Down;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return Act::PrevSys;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Act::NextSys;
        case SDL_CONTROLLER_BUTTON_A: return Act::Launch;   // Cross
        case SDL_CONTROLLER_BUTTON_Y: return Act::OpenEmu;  // Triangle
        case SDL_CONTROLLER_BUTTON_X: return Act::Rescan;   // Square
        case SDL_CONTROLLER_BUTTON_B: return Act::Back;     // Circle
        case SDL_CONTROLLER_BUTTON_BACK: return Act::Covers;      // Share: the selected game's cover
        case SDL_CONTROLLER_BUTTON_START: return Act::Settings;   // Options
        default: return Act::None;
    }
}

Act fromKey(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP: return Act::Up;
        case SDLK_DOWN: return Act::Down;
        case SDLK_LEFT: case SDLK_q: return Act::PrevSys;
        case SDLK_RIGHT: case SDLK_e: return Act::NextSys;
        case SDLK_RETURN: return Act::Launch;
        case SDLK_t: return Act::OpenEmu;
        case SDLK_r: return Act::Rescan;
        case SDLK_s: return Act::Settings;
        case SDLK_ESCAPE: return Act::Quit;
        default: return Act::None;
    }
}

// Copies the bundled default config to the data folder on first run, so it can
// be edited over FTP.
// Copies the bundled default config to the data folder on first run, so it can
// be edited over FTP. The first readable source wins; each step is logged.
void seedConfig(Platform& p, const std::vector<std::string>& from, const std::string& to) {
    if (p.exists(to)) return;
    for (const auto& src : from) {
        std::string text;
        if (!p.readFile(src, text)) continue;
        if (p.writeFile(to, text)) p.log("config: copied " + src + " to " + to);
        else p.log("config: could not write " + to);
        return;
    }
    p.log("config: no default config found to copy to " + to);
}

struct Screen {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    uint32_t* pixels = nullptr;  // PS5 canvas (ARGB8888, scrW x scrH)
    int scrW = 1920, scrH = 1080;
    std::string error;

    bool open(bool headless) {
#if defined(__PROSPERO__)
        (void)headless;
        sceSystemServiceHideSplashScreen();
        if (SDL_Init(SDL_INIT_EVENTS) != 0) return fail("SDL_Init");
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) error = SDL_GetError();  // keep going
        SDL_InitSubSystem(SDL_INIT_AUDIO);  // for in-game sound; ok if it fails
        char e[160];
        int dispW = 1920, dispH = 1080;
        bl("Screen: calling vkp::open");
        bool vkok = vkp::open(e, sizeof e, &dispW, &dispH);
        bl("Screen: vkp::open returned");
        if (!vkok) { error = std::string("Vulkan: ") + e; return false; }
        // The menu is software-rendered into a fixed 1920x1080 CPU buffer; vkp::present
        // scales it up to the real display (dispW x dispH) with a GPU blit. Keeping the
        // canvas at a fixed size decouples the menu from 4K/1080p display differences.
        scrW = kW;
        scrH = kH;
        size_t bytes = (size_t)scrW * scrH * 4;
        void* mem = std::calloc(1, bytes);  // raw mmap faults once RADV is resident; heap is fine
        if (!mem) { error = "no memory for the canvas"; return false; }
        pixels = static_cast<uint32_t*>(mem);
        bl("Screen: canvas allocated");
        SDL_Surface* canvas = SDL_CreateRGBSurfaceWithFormatFrom(pixels, scrW, scrH, 32,
                                                                 scrW * 4, SDL_PIXELFORMAT_ARGB8888);
        if (!canvas) return fail("canvas");
        ren = SDL_CreateSoftwareRenderer(canvas);
        if (!ren) return fail("software renderer");
#else
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) return fail("SDL_Init");
        win = SDL_CreateWindow("TotalEmu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               headless ? kW : 1280, headless ? kH : 720, SDL_WINDOW_RESIZABLE);
        if (!win) return fail("window");
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
        if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
        if (!ren) return fail("renderer");
        SDL_RenderSetLogicalSize(ren, kW, kH);
#endif
        SDL_RenderSetLogicalSize(ren, kW, kH);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        return true;
    }

    void present() {
        SDL_RenderPresent(ren);
#if defined(__PROSPERO__)
        vkp::present(pixels, scrW, scrH, scrW * 4);  // FIFO vsync
#endif
    }

    void presentOnly() {
#if defined(__PROSPERO__)
        vkp::present(pixels, scrW, scrH, scrW * 4);
#endif
    }

    bool fail(const char* what) {
        error = std::string(what) + ": " + SDL_GetError();
        return false;
    }
};

}  // namespace

#if defined(__PROSPERO__)
Platform* g_discLog = nullptr;  // the app's log, for the disc code (fe_compat.cpp)
void pslDiscLog(const char* line) { if (g_discLog) g_discLog->log(line); }
void pslPresentPS5() { if (g_screen) g_screen->presentOnly(); }
// The launcher's canvas, for the in-game picture (play.cpp).
uint32_t* pslCanvas(int& w, int& h) {
    if (!g_screen || !g_screen->pixels) return nullptr;
    w = g_screen->scrW;
    h = g_screen->scrH;
    return g_screen->pixels;
}
#endif

void installFaultHandler();
std::string httpsProbe(const std::string& url);  // src/http_probe.cpp

int main(int argc, char** argv) {
    std::string cfgPath, shotPath;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shotPath = argv[++i];
        else cfgPath = argv[i];
    }

    std::string helperVersion;
    bool useHelper = helper::ping(helperVersion);
    const bool helperUp = useHelper;  // kept: a launch needs the helper even when /data is read directly
#if !defined(__PROSPERO__)
    if (!std::getenv("PSL_USE_HELPER")) useHelper = false;  // desktop: opt in for testing
#endif
    std::string accessStatus;
    bool dataListable = true;

#if defined(__PROSPERO__)
    // Set PS5_VIDEOOUT_PARAM_JSON BEFORE the jailbreak: after jailbreak fd_rdir
    // changes to rootvnode, /app0 resolves differently and RADV can't find param.json.
    setenv("PS5_VIDEOOUT_PARAM_JSON", "/app0/sce_sys/param.json", 1);
    // Ask the helper to jailbreak this process using the kernel APIs it already
    // has (kernel_set_ucred_authid, caps, fd_jdir/rdir -> rootvnode). The helper
    // is already elevated; this avoids needing a separate ELF loader or daemon.
    bool directFiles = false;
    std::string jbStatus = "no helper";
    if (!helperVersion.empty()) {
        char pidBuf[32];
        std::snprintf(pidBuf, sizeof pidBuf, "JAILBREAK %d", (int)getpid());
        std::string jbReply;
        if (helper::request(pidBuf, jbReply, nullptr, 3000) && jbReply.compare(0, 2, "OK") == 0) {
            directFiles = true;
            jbStatus = "jailbreak ok";
        } else {
            jbStatus = "jailbreak failed (" + jbReply + ")";
        }
    }
    const bool appCanReadData = directFiles;
    accessStatus = (helperVersion.empty() ? std::string("no helper; ") : "helper " + helperVersion + "; ") +
                   jbStatus;
    dataListable = appCanReadData || useHelper;
    useHelper = useHelper && !directFiles;
#else
    // Desktop: no elevation needed
    bool directFiles = false;
    (void)directFiles;
    accessStatus = useHelper ? "helper " + helperVersion + " connected" : "Desktop";
    dataListable = true;
    (void)appCanReadData;
#endif

    auto plat = makePlatform(useHelper, helperUp);
#if defined(__PROSPERO__)
    g_discLog = plat.get();
    plat->makeDirs("/data/PSLauncher");
    // One log for the whole run: main.log starts empty each launch, and a crash
    // is appended to it, so it can be pulled after a crash as a single file.
    plat->writeFile("/data/PSLauncher/main.log", "");
    plat->setLogPath("/data/PSLauncher/main.log");
    seedConfig(*plat, {"/data/homebrew/PPSA97500/assets/launcher.ini", "/app0/assets/launcher.ini"},
               "/data/PSLauncher/launcher.ini");
#endif

    // Config: argument, the console's data folder, the bundled default, the working folder.
    LauncherConfig cfg;
    std::string err;
    std::vector<std::string> candidates;
    if (!cfgPath.empty()) candidates.push_back(cfgPath);
    candidates.push_back("/data/PSLauncher/launcher.ini");
    candidates.push_back("/app0/assets/launcher.ini");
    candidates.push_back("launcher.ini");
    bool loaded = false;
    std::string cfgFrom;
    for (const auto& c : candidates) {
        std::string text, e;
        if (!plat->readFile(c, text)) { err += "cannot read " + c + "; "; continue; }
        if (loadConfigText(text, c, cfg, e)) { loaded = true; cfgFrom = c; break; }
        err += e + "; ";
    }
    if (!loaded) {
        plat->log("No usable config: " + err);
        plat->closeApp();
        return 1;
    }

    plat->makeDirs(cfg.dataDir);
    plat->setLogPath(cfg.dataDir + "/main.log");
    plat->log(std::string("PS Launcher starting on ") + plat->name());
#if defined(__PROSPERO__)
    installFaultHandler();
#endif
    plat->log("config: " + cfgFrom);
    plat->log("storage: " + accessStatus);
    {
        // Covers that v1.4.8 saved in the wrong folder are moved to the right one (once; nothing is deleted).
        std::string report;
        if (covers::moveOldCovers(cfg.dataDir, report) > 0) plat->log(report);
    }
#if defined(__PROSPERO__)
    // One HTTPS request to test the network, when http_probe is on in launcher.ini (see src/http_probe.cpp).
    if (readFlag(*plat, cfg.dataDir, "http_probe")) {
        plat->log("http-probe on startup: on");
        plat->log(httpsProbe("https://api.thegamesdb.net/"));
    }
#endif
#if defined(__PROSPERO__)
    // The storage status goes to helper.log (once), not the info panel.
    {
        const std::string logLine = "storage: " + accessStatus + "\n";
        std::string log;
        plat->readFile(cfg.dataDir + "/helper.log", log);
        if (log.find(logLine) == std::string::npos) plat->writeFile(cfg.dataDir + "/helper.log", log + logLine);
    }
#endif

#if defined(__PROSPERO__)
    static Platform* s_logPlat = plat.get();
    g_bootlog = plat.get();
    vkp::setLogSink([](const char* m){ s_logPlat->log(m); });
    plat->log("screen: opening (Vulkan)");
#endif
    Screen screen;
    if (!screen.open(!shotPath.empty())) {
        plat->log("screen: " + screen.error);
        plat->closeApp();
        return 1;
    }
    plat->log("boot: open() returned");
    if (!screen.error.empty()) plat->log("controllers: " + screen.error);
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(screen.ren, &info) == 0) plat->log(std::string("renderer: ") + info.name);
#if defined(__PROSPERO__)
    plat->log(std::string("display: ") + vkp::describe());
    plat->log("boot: screen ready, building app");
#endif
    SDL_Renderer* ren = screen.ren;
    SDL_Window* win = screen.win;
    g_screen = &screen;
#if defined(__PROSPERO__)
    // The loading splash: the app icon on the canvas and a line under it, shown from here
    // until the menu, so the console shows the app is loading rather than stalled.
    std::vector<uint32_t> icon;
    int iconW = 0, iconH = 0;
    for (const char* path : {"/data/homebrew/PPSA97500/sce_sys/icon0.png", "/app0/sce_sys/icon0.png"}) {
        std::string png;
        if (plat->readFile(path, png)) icon = decodeIcon(png, iconW, iconH);
        if (!icon.empty()) break;
    }
    plat->log(icon.empty() ? "boot: no icon for the splash" : "boot: splash icon ready");
    auto splash = [&](const std::string& text) {
        fillRect(ren, 0, 0, kW, kH, kBg);
        if (!icon.empty()) {
            const int size = 320, x0 = (kW - size) / 2, y0 = 250;
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    screen.pixels[(size_t)(y0 + y) * kW + x0 + x] =
                        icon[(size_t)(y * iconH / size) * iconW + x * iconW / size];
        }
        drawText(ren, (kW - textWidth(text, 3.5f)) / 2, 640, 3.5f, text, kDim);
        screen.present();
    };
    splash("Starting...");
#endif

    App app(*plat, cfg);
    app.debugMenu = readFlag(*plat, cfg.dataDir, "Debug_Menu");
    app.httpProbe = readFlag(*plat, cfg.dataDir, "http_probe");
    app.skipDriveCheck = readFlag(*plat, cfg.dataDir, "DC_onstartup");
    app.useApi = readFlag(*plat, cfg.dataDir, "UseApi");
    app.apiIndex = apiIndexOf(readValue(*plat, cfg.dataDir, "ApiToUse"));
    app.access = accessStatus;
    app.dataListable = dataListable;
    app.loadDiscSettings();
    // RPCS3-5 starts a game in its drive by itself when it opens, unless its config says not to:
    // turn that off when PS Launcher starts (the helper turns it back on after a game closes).
    for (const auto& sys : cfg.systems) {
        if (sys.configFile.empty()) continue;
        std::string text;
        const std::string key = "Automatically start games after boot: true";
        if (!plat->readFile(sys.configFile, text))
            plat->log("config: " + sys.configFile + " not found; " + sys.name + " starts its own games");
        else if (text.find(key) != std::string::npos) {
            text.replace(text.find(key), key.size(), "Automatically start games after boot: false");
            plat->log(plat->writeFile(sys.configFile, text)
                          ? "config: " + sys.name + " set to not start games after boot"
                          : "config: could not write " + sys.configFile);
        }
    }
    app.ses = loadSession(*plat, cfg.dataDir);
#if defined(__PROSPERO__)
    // USB disc drives: the emulator opens drive:// paths through the drive reader,
    // and the watcher lists the discs that go in.
    // The sandbox elevator gives the app the console's root, where /dev holds the drives.
    // The copy under /data/homebrew is the one that is installed (/app0 can hold an older one).
#if defined(__PROSPERO__)
    splash("Starting...");
#endif
    const char *elevator = access("/data/homebrew/PPSA97500/assets/sandbox-elevator.elf", R_OK) == 0
                               ? "/data/homebrew/PPSA97500/assets/sandbox-elevator.elf"
                               : "/app0/assets/sandbox-elevator.elf";
    const auto elevated = sandboxelevation::requestHelper(sandboxelevation::Capability::filesystem, elevator);
    plat->log("disc: sandbox elevator status " + std::to_string((int)elevated));
    fe::gamedb::init();  // game names by serial (assets/gamedb)
    fe::discdrive::installOpener();
    fe::discdrive::start();
    plat->log("boot: disc drive watcher started");
    plat->log("boot: scanning library");
#endif
#if defined(__PROSPERO__)
    // The menu waits for the drives' first look (the watcher counts its first pass in
    // the generation), so a disc already in a drive is there when the menu appears, and
    // its prompt opens with the first frame. Capped, so a stuck drive can't hold the boot.
    if (readFlag(*plat, cfg.dataDir, "DC_onstartup")) {
        plat->log("boot: drive check skipped (DC_onstartup=1)");
    } else {
        const Uint32 begin = SDL_GetTicks();
        while (fe::discdrive::generation() <= 1 && SDL_GetTicks() - begin < 15000) {
            SDL_PumpEvents();
            splash("Checking the drives...");
            SDL_Delay(50);
        }
        plat->log(fe::discdrive::generation() <= 1 ? "boot: drive check timed out" : "boot: drives checked");
    }
#endif
    app.rescan();
#if defined(__PROSPERO__)
    plat->log("boot: first render");
#endif
    if (!shotPath.empty()) {  // headless check: render one frame to a BMP and exit
        app.render(ren);
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
        SDL_SaveBMP(s, shotPath.c_str());
        SDL_FreeSurface(s);
        SDL_Quit();
        return 0;
    }

    SDL_GameController* pad = nullptr;
    Uint32 quitHoldStart = 0;
    int heldDir = SDL_CONTROLLER_BUTTON_INVALID;  // int: Uint8 can never equal -1
    Uint32 heldSince = 0, lastRepeat = 0;
    bool running = true;
    // After a game closes, Touchpad must be let go and pressed again before a hold quits.
    bool touchReleased = true;
    bool touchWas = false;         // the touchpad was down at the last frame
    Uint32 touchDownAt = 0;
    // Triangle on the library: a quick press opens the disc menu; holding it 0.5 s ejects.
    bool triangleHeld = false, ejectDone = false, r2Held = false;
    Uint32 triangleDownAt = 0;

    // One action, whichever screen takes it.
    auto handle = [&](Act act) {
        if (app.view != View::Library) app.modal(act);
        else if (act == Act::Quit) running = false;
        else if (act == Act::Settings) app.openSettings();
        else if ((act == Act::Launch || act == Act::OpenEmu) && app.notStartable()) {}
        else if (act == Act::Launch && app.sys().cfg->builtinCore && !app.sys().games.empty()) {
            // Run the game inside this app with the built-in core.
            const SystemState& st = app.sys();
            if (app.copying()) {
                app.setStatus("Wait for the install to finish or cancel it", kWarn);
                return;
            }
            std::string game = app.playPath.empty() ? st.games[app.ses.game].path : app.playPath;
            app.playPath.clear();
            app.setStatus("Loading " + st.games[app.ses.game].title + "...", kDim);
            app.render(ren); screen.present();
#if defined(__PROSPERO__)
            fe::discdrive::setPlaying(fe::discdrive::isDrivePath(game));  // the watcher leaves the drive alone
#endif
            const std::string playErr = playGame(ren, *plat, *st.cfg, game, pad);
#if defined(__PROSPERO__)
            fe::discdrive::setPlaying(false);
            if (fe::discdrive::isRamPath(game)) fe::discdrive::releaseRam();
#endif
            touchReleased = false;
            if (playErr.empty()) app.setStatus("Back from " + st.cfg->name, kDim);
            else app.setStatus("Couldn't start: " + playErr, kWarn);
        }
        else if (act == Act::OpenEmu && app.discSelected()) {
#if defined(__PROSPERO__)
            // Triangle on a disc entry opens the disc menu.
            const std::string path = app.sys().games[app.ses.game].path;
            for (const auto& d : fe::discdrive::discs())
                if (fe::discdrive::streamPath(d) == path || d.device == path) {
                    app.promptDisc = d;
                    app.openDiscPrompt();
                    break;
                }
#endif
        }
        // Triangle opens an external emulator only for systems that have one (not the built-in PS1).
        else if (act == Act::Launch || (act == Act::OpenEmu && !app.sys().cfg->builtinCore)) {
            if (app.launch(win, act == Act::Launch)) {
                app.render(ren);  // show "Started ..." while the system switches titles
                screen.present();
                running = false;
            }
        }
        else app.apply(act);
    };

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            Act act = Act::None;
            switch (ev.type) {
                case SDL_QUIT: running = false; break;
                case SDL_CONTROLLERDEVICEADDED:
                    if (!pad) pad = SDL_GameControllerOpen(ev.cdevice.which);
                    break;
                case SDL_CONTROLLERDEVICEREMOVED:
                    if (pad && ev.cdevice.which ==
                                   SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                        SDL_GameControllerClose(pad);
                        pad = nullptr;
                    }
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_Y && app.view == View::Library) {
                        triangleHeld = true;
                        ejectDone = false;
                        triangleDownAt = SDL_GetTicks();
                        break;
                    }
                    act = fromButton(ev.cbutton.button);
                    if (act == Act::Up || act == Act::Down) {
                        heldDir = ev.cbutton.button;
                        heldSince = lastRepeat = SDL_GetTicks();
                    }
                    break;
                case SDL_CONTROLLERBUTTONUP:
                    if (ev.cbutton.button == heldDir) heldDir = SDL_CONTROLLER_BUTTON_INVALID;
                    if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_Y && triangleHeld) {
                        triangleHeld = false;
                        if (!ejectDone) act = Act::OpenEmu;
                    }
                    break;
                case SDL_CONTROLLERAXISMOTION:
                    if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
                        const bool pressed = ev.caxis.value > 16000;
                        if (pressed && !app.l2Held && app.view == View::Version) app.triggerPress(1);
                        app.l2Held = pressed;
                    } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                        const bool pressed = ev.caxis.value > 16000;
                        if (pressed && !r2Held) {
                            if (app.view == View::Version) app.triggerPress(2);
                            else act = Act::Covers;  // R2: the selected game's cover
                        }
                        r2Held = pressed;
                    }
                    break;
                case SDL_KEYDOWN: act = fromKey(ev.key.keysym.sym); break;
            }
            handle(act);
        }
        if (app.pendingPlay) {
            app.pendingPlay = false;
            handle(Act::Launch);
        }

        Uint32 now = SDL_GetTicks();

        // Hold-to-scroll on the D-pad.
        if (heldDir != SDL_CONTROLLER_BUTTON_INVALID && now - heldSince > 400 && now - lastRepeat > 80) {
            handle(fromButton((Uint8)heldDir));
            lastRepeat = now;
        }

        // Quit combo: hold the touchpad. The PS button belongs to the system. After a game
        // closes, the touchpad has to be released and pressed again first.
        const bool touch = pad && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_TOUCHPAD);
        if (!touch) touchReleased = true;
        if (touch && touchReleased) {
            if (!quitHoldStart) quitHoldStart = now;
            if (now - quitHoldStart >= kQuitHoldMs) running = false;
        } else {
            quitHoldStart = 0;
        }
        // A tap (pressed and let go within 400 ms) opens or closes the controls.
        if (touch && !touchWas) touchDownAt = now;
        if (!touch && touchWas && now - touchDownAt < 400 &&
            (app.view == View::Library || app.view == View::Controls))
            app.toggleControls();
        touchWas = touch;

#if defined(__PROSPERO__)
        app.checkDiscs();
        app.checkCopy();
        if (triangleHeld && !ejectDone && now - triangleDownAt >= 500) {
            ejectDone = true;
            app.ejectFirstDisc();
        }
#endif
        app.expireStatus(now);
        app.render(ren);
        if (quitHoldStart) {
            float p = std::min(1.0f, (now - quitHoldStart) / (float)kQuitHoldMs);
            fillRect(ren, 0, kH - 8, (int)(kW * p), 8, kBad);
        }
        screen.present();
#if !defined(__PROSPERO__)
        SDL_Delay(1);  // vsync paces the desktop window
#endif
    }

    saveSession(*plat, cfg.dataDir, app.ses);
    plat->log("PS Launcher exiting");
    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
#if defined(__PROSPERO__)
    vkp::close();
#endif
#if defined(__PROSPERO__)
    fe::discdrive::stop();
#endif
    SDL_Quit();
    plat->closeApp();  // PS5: never returns
    return 0;
}
