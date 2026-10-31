#pragma once
#include <string>
#include <vector>

// One emulator section of the launcher ("[system.ps1]" in launcher.ini).
struct SystemConfig {
    std::string id;                       // "ps1", "ps2", "ps3"
    std::string name;                     // shown in the menu
    std::string titleId;                  // the emulator's home-screen title, e.g. PPSA98510
    std::vector<std::string> gameDirs;    // folders to scan
    std::vector<std::string> extensions;  // lowercase with dot, in priority order (.m3u before .cue)
    std::string marker;                   // a folder containing this entry is a game (PS3_GAME)
    std::vector<std::string> biosAny;     // at least one of these must exist
    std::vector<std::string> biosAll;     // every one of these must exist
    std::string biosDir;                  // folder searched for a BIOS by name (bios_name)
    std::string biosName;                 // a BIOS is any file "<bios_name>[-]<5 digits>.bin" in bios_dir
    bool passGame = false;                // emulator understands the game handoff
    std::string launchFile;               // emulator starts a game named here: line 1 its path, line 2 its serial
    std::string launchArg;                // the game's path follows this argument (PS3: --boot)
    std::string configFile;               // a config file edited before the title starts (PS3: RPCS3-5's config.yml)
    bool builtinCore = false;             // run the in-app core instead of launching a title
    std::string systemDir;                // BIOS/cache dir for the built-in core
    std::string desktopCmd;               // desktop testing only: command run instead of a title
};

struct LauncherConfig {
    std::string dataDir = "/data/PSLauncher";
    bool exitAfterLaunch = true;          // PS5: free the launcher's memory for the emulator
    std::string thegamesdbKey;            // [api] thegamesdb_key: each user's own key (empty = not set)
    std::string rawgKey;                  // [api] rawg_key: each user's own key (empty = not set)
    std::vector<SystemConfig> systems;
};

// Parses launcher.ini text; `name` is only used in error messages.
bool loadConfigText(const std::string& text, const std::string& name, LauncherConfig& out, std::string& err);
