// PS Launcher - game covers: picture files in <data>/covers/<platform>/, named after the game's title.
#pragma once
#include <string>
#include <vector>

namespace covers
{
// The keys each user entered in launcher.ini's [api] section. Empty = that source is off.
struct Keys
{
    std::string thegamesdb;
    std::string rawg;
    bool useApi = false;      // UseApi: TheGamesDB and RAWG are tried (libretro always is)
    std::string source = "all";  // ApiToUse: "libretro", "TheGamesDB", "RAWG" or "all"
};

// Moves covers that v1.4.8 saved in <dataDir>covers/ (no slash) to <dataDir>/covers/. Files already in the new
// folder are kept where they are, and nothing is deleted. Returns how many were moved; `report` says what happened.
int moveOldCovers(const std::string& dataDir, std::string& report);

// The cover file a game has, or empty.
std::string existing(const std::string& dataDir, const std::string& platform, const std::string& title);

// A game to find a cover for: its platform ("ps1", "ps2" or "ps3") and its title.
struct Item
{
    std::string platform;
    std::string title;
};

// Starts a download on a thread of its own for every game in the list that has no cover yet. One job runs at a
// time: false when another is still going.
bool startJob(const std::string& dataDir, const std::vector<Item>& items, const Keys& keys);
bool busy();
// What the job is doing, or what it last did, for the status line.
std::string status();
}
