#pragma once
#include <string>
#include <vector>

#include "config.hpp"
#include "platform.hpp"

struct Game {
    std::string title;
    std::string path;
};

struct SystemState {
    const SystemConfig* cfg = nullptr;
    std::vector<Game> games;
    enum class Bios { Ok, Missing, Unknown } bios = Bios::Ok;
    std::string biosMsg;    // shown in the info panel; never blocks a launch
    bool sandboxed = false; // a games folder could not be listed
};

// dataListable: false when the sandbox hides /data (a missing BIOS can't be confirmed).
std::vector<SystemState> scanLibrary(Platform& p, const LauncherConfig& cfg, bool dataListable);

#if defined(__PROSPERO__)
// A serial in a game's name ("Title [SLUS-20469]" gives SLUS-20469), or "".
std::string serialIn(const std::string& title);
#endif

// Where the user was, so the menu reopens on the same system and game.
struct Session {
    int system = 0;
    int game = 0;
};
Session loadSession(Platform& p, const std::string& dataDir);
void saveSession(Platform& p, const std::string& dataDir, const Session& s);

// handoff/<system>.txt holds the chosen game's path. Emulators patched to
// read it boot straight into the game; others ignore it.
void writeHandoff(Platform& p, const std::string& dataDir, const std::string& systemId,
                  const std::string& gamePath);
