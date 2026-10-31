// PS Launcher - game covers. Sources, in order: libretro's thumbnails (no key), then TheGamesDB and RAWG, each
// with the user's own key from launcher.ini. A cover is kept as it was downloaded (PNG or JPEG).
#include "covers.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <thread>

#include "json.h"
#if defined(__PROSPERO__)
#include "tls/tls_client.h"
#endif

namespace fe::gamedb
{
// From disc/gamedb.cpp: the serial of a game's title, and the disc's name as Redump (and libretro) spell it.
std::string serialByName(const std::string& fileTitle);
std::string redumpName(const std::string& serial);
std::string officialTitle(const std::string& serial);
}

namespace covers
{
namespace
{
std::mutex statusLock;
std::string jobStatus;
std::atomic<bool> jobBusy{false};

// The ids of each platform in the three databases: libretro's folder, TheGamesDB's platform and RAWG's platform.
struct Platform
{
    const char *id;
    const char *libretro;
    int gamesDb;
    int rawg;
};
const Platform platforms[] = {
    {"ps1", "Sony - PlayStation", 10, 27},
    {"ps2", "Sony - PlayStation 2", 11, 15},
    {"ps3", "Sony - PlayStation 3", 12, 16},
};

const Platform *platformFor(const std::string& id)
{
    for (const Platform& p : platforms)
        if (id == p.id) return &p;
    return nullptr;
}

void setStatus(const std::string& s)
{
    std::lock_guard<std::mutex> lock(statusLock);
    jobStatus = s;
}

// Every byte but letters, digits and -_.~ as %XX.
std::string percentEncode(const std::string& s)
{
    std::string out;
    char hex[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            std::snprintf(hex, sizeof hex, "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

// The title as a file name: the characters a file system refuses become '_'.
std::string fileName(const std::string& title)
{
    std::string out = title;
    for (char& c : out)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    return out;
}

// The title without its region: "Guitar Hero 5 (USA)" is searched as "Guitar Hero 5".
std::string searchName(const std::string& title)
{
    const size_t at = title.find(" (");
    return at == std::string::npos ? title : title.substr(0, at);
}

// Appends one line to <dataDir>/covers.log: what each cover lookup found, or why each source failed.
void logLine(const std::string& dataDir, const std::string& text)
{
    FILE *f = std::fopen((dataDir + "/covers.log").c_str(), "a");
    if (f == nullptr) return;
    std::fprintf(f, "%s\n", text.c_str());
    std::fclose(f);
}

bool exists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

// The GET, through the TLS client (the console's own client refuses public certificates after the sandbox
// elevator). The status code, or -1 with `why` set.
int download(const std::string& url, std::vector<uint8_t>& out, std::string& why)
{
#if defined(__PROSPERO__)
    return fe::tls::get(url, out, 30, 8u << 20, &why);
#else
    (void)url;
    (void)out;
    why = "this build has no network client";
    return -1;
#endif
}

// Parses a JSON answer. False when it is not JSON.
bool parseJson(const std::vector<uint8_t>& body, fe::Json& root)
{
    fe::JsonReader reader((const char *)body.data(), body.size());
    return reader.read(root);
}

const fe::Json *member(const fe::Json *j, const char *key)
{
    return j != nullptr ? j->find(key) : nullptr;
}

bool isPicture(const std::vector<uint8_t>& b)
{
    const bool png = b.size() > 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G';
    const bool jpeg = b.size() > 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff;
    return png || jpeg;
}

// Downloads the picture at `url` and keeps it as base + .png or .jpg. The path, or empty with `why` set.
std::string saveImage(const std::string& url, const std::string& base, std::string& why)
{
    std::vector<uint8_t> bytes;
    const int code = download(url, bytes, why);
    if (code != 200) {
        if (code >= 0) why = "HTTP " + std::to_string(code);
        return "";
    }
    if (!isPicture(bytes)) {
        why = "the answer is not a picture";
        return "";
    }
    const std::string path = base + (bytes[0] == 0x89 ? ".png" : ".jpg");
    FILE *f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        why = "could not write " + path;
        return "";
    }
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    return path;
}

// TheGamesDB's front box art for a game: the picture's address, or empty with `why` set.
std::string gamesDbCover(const Platform& p, const std::string& name, const std::string& key, std::string& why)
{
    const std::string url = "https://api.thegamesdb.net/v1.1/Games/ByGameName?apikey=" + key + "&name=" +
                            percentEncode(name) + "&filter%5Bplatform%5D=" + std::to_string(p.gamesDb) +
                            "&include=boxart";
    std::vector<uint8_t> body;
    const int code = download(url, body, why);
    if (code != 200) {
        if (code >= 0) why = "HTTP " + std::to_string(code);
        return "";
    }
    fe::Json root;
    if (!parseJson(body, root)) {
        why = "the answer is not JSON";
        return "";
    }
    const fe::Json *games = member(member(&root, "data"), "games");
    if (games == nullptr || games->type != fe::Json::Array || games->items.empty()) {
        why = "not in TheGamesDB";
        return "";
    }
    const fe::Json *gameId = member(&games->items[0], "id");
    if (gameId == nullptr) {
        why = "no id in the answer";
        return "";
    }
    const std::string id = std::to_string((long long)gameId->number);
    const fe::Json *boxart = member(member(&root, "include"), "boxart");
    const std::string base = member(boxart, "base_url") != nullptr ? member(boxart, "base_url")->string("large") : "";
    const fe::Json *pictures = member(member(boxart, "data"), id.c_str());
    if (base.empty() || pictures == nullptr || pictures->type != fe::Json::Array) {
        why = "no box art in TheGamesDB";
        return "";
    }
    for (const fe::Json& picture : pictures->items) {
        if (picture.string("type") == "boxart" && picture.string("side") == "front") {
            const std::string file = picture.string("filename");
            if (!file.empty()) return base + (base.back() == '/' ? "" : "/") + file;
        }
    }
    why = "no front box art in TheGamesDB";
    return "";
}

// RAWG's picture for a game: the address, or empty with `why` set.
std::string rawgCover(const Platform& p, const std::string& name, const std::string& key, std::string& why)
{
    const std::string url = "https://api.rawg.io/api/games?key=" + key + "&search=" + percentEncode(name) +
                            "&platforms=" + std::to_string(p.rawg) + "&page_size=1";
    std::vector<uint8_t> body;
    const int code = download(url, body, why);
    if (code != 200) {
        if (code >= 0) why = "HTTP " + std::to_string(code);
        return "";
    }
    fe::Json root;
    if (!parseJson(body, root)) {
        why = "the answer is not JSON";
        return "";
    }
    const fe::Json *results = member(&root, "results");
    if (results == nullptr || results->type != fe::Json::Array || results->items.empty()) {
        why = "not in RAWG";
        return "";
    }
    const std::string picture = results->items[0].string("background_image");
    if (picture.empty()) why = "no picture in RAWG";
    return picture;
}

// The cover of one game from the first source that has one: the path written, or empty with `why` set to what
// each source said.
std::string fetch(const std::string& dataDir, const Platform& p, const std::string& title, const Keys& keys,
                  std::string& why)
{
    const std::string dir = dataDir + "/covers";
    mkdir(dir.c_str(), 0777);
    mkdir((dir + "/" + p.id).c_str(), 0777);
    const std::string base = dir + "/" + p.id + "/" + fileName(title);
    const std::string serial = fe::gamedb::serialByName(title);
    // The title the databases search by: GameDB's title for a PS2 game, else the title without its region.
    const std::string official = fe::gamedb::officialTitle(serial);
    const std::string search = official.empty() ? searchName(title) : official;
    std::string reasons;

    // libretro names its files as the disc is named in Redump, with its language tags. The serial gives that name;
    // the title is the fallback.
    const std::string redump = serial.empty() ? std::string() : fe::gamedb::redumpName(serial);
    std::string path;
    for (const std::string& name : {redump, title}) {
        if (name.empty()) continue;
        const std::string url = std::string("https://thumbnails.libretro.com/") + percentEncode(p.libretro) +
                                "/Named_Boxarts/" + percentEncode(name) + ".png";
        path = saveImage(url, base, why);
        if (!path.empty()) return path;
        reasons += "libretro (" + name + "): " + why + "; ";
    }
    std::string url;

    if (keys.useApi && (keys.source == "all" || keys.source == "TheGamesDB") && !keys.thegamesdb.empty()) {
        url = gamesDbCover(p, search, keys.thegamesdb, why);
        if (!url.empty()) path = saveImage(url, base, why);
        if (!path.empty()) return path;
        reasons += "; TheGamesDB: " + why;
    }
    if (keys.useApi && (keys.source == "all" || keys.source == "RAWG") && !keys.rawg.empty()) {
        url = rawgCover(p, search, keys.rawg, why);
        if (!url.empty()) path = saveImage(url, base, why);
        if (!path.empty()) return path;
        reasons += "; RAWG: " + why;
    }
    why = reasons;
    return "";
}
}  // namespace

std::string existing(const std::string& dataDir, const std::string& platform, const std::string& title)
{
    const std::string base = dataDir + "/covers/" + platform + "/" + fileName(title);
    for (const char *ext : {".png", ".jpg"})
        if (exists(base + ext)) return base + ext;
    return "";
}

int moveOldCovers(const std::string& dataDir, std::string& report)
{
    const std::string oldDir = dataDir + "covers";
    const std::string newDir = dataDir + "/covers";
    DIR *top = opendir(oldDir.c_str());
    if (top == nullptr) return 0;
    closedir(top);
    int moved = 0, kept = 0;
    for (const char *platform : {"ps1", "ps2", "ps3"}) {
        const std::string from = oldDir + "/" + platform;
        DIR *dir = opendir(from.c_str());
        if (dir == nullptr) continue;
        mkdir(newDir.c_str(), 0777);
        mkdir((newDir + "/" + platform).c_str(), 0777);
        while (dirent *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            const std::string to = newDir + "/" + platform + "/" + name;
            if (exists(to)) {
                ++kept;
                continue;
            }
            if (rename((from + "/" + name).c_str(), to.c_str()) == 0) ++moved;
        }
        closedir(dir);
    }
    report = "covers: moved " + std::to_string(moved) + " from " + oldDir + " to " + newDir;
    if (kept > 0) report += " (" + std::to_string(kept) + " were already in " + newDir + " and were left in " + oldDir + ")";
    return moved;
}

bool startJob(const std::string& dataDir, const std::vector<Item>& items, const Keys& keys)
{
    if (jobBusy.exchange(true)) return false;
    std::thread([dataDir, items, keys]() {
        int found = 0, have = 0, missing = 0, done = 0;
        std::string lastTitle, lastWhy;
        for (const Item& item : items) {
            ++done;
            const Platform *p = platformFor(item.platform);
            if (p == nullptr) {
                ++missing;
                continue;
            }
            if (!existing(dataDir, p->id, item.title).empty()) {
                ++have;
                continue;
            }
            setStatus("Covers: " + std::to_string(done) + " of " + std::to_string(items.size()) + ", " + item.title);
            std::string why;
            if (fetch(dataDir, *p, item.title, keys, why).empty()) {
                ++missing;
                lastTitle = item.title;
                lastWhy = why;
                logLine(dataDir, std::string(p->id) + " " + item.title + ": not found. " + why);
            } else {
                ++found;
            }
        }
        std::string text = "Covers: " + std::to_string(found) + " downloaded, " + std::to_string(have) +
                           " already there, " + std::to_string(missing) + " not found";
        if (missing > 0) text += " (" + lastTitle + ": " + lastWhy + ")";
        setStatus(text);
        jobBusy = false;
    }).detach();
    return true;
}

bool busy()
{
    return jobBusy;
}

std::string status()
{
    std::lock_guard<std::mutex> lock(statusLock);
    return jobStatus;
}
}  // namespace covers
