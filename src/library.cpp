#include "library.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#if defined(__PROSPERO__)
#include "disc/fe.h"
#include <fcntl.h>
#include <unistd.h>
#endif

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static std::string num(size_t n) {
    char b[24];
    std::snprintf(b, sizeof b, "%zu", n);
    return b;
}

// Calls fn for each line of a text file read through the platform (helper or local).
template <typename F>
static void eachLine(Platform& p, const std::string& path, F fn) {
    std::string text;
    if (!p.readFile(path, text)) return;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        fn(line);
        pos = nl + 1;
    }
}

static std::string extOf(const std::string& name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos ? "" : lower(name.substr(dot));
}

static std::string stem(const std::string& name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// Lower is better; -1 means "not a game file for this system".
static int extRank(const SystemConfig& sys, const std::string& name) {
    std::string e = extOf(name);
    for (size_t i = 0; i < sys.extensions.size(); ++i)
        if (sys.extensions[i] == e) return (int)i;
    return -1;
}

// Sandboxed fallback: <games_dir>/library.txt, one game per line,
// either "path" or "Title|path".
static void readLibraryTxt(Platform& p, const std::string& dir, std::vector<Game>& out) {
    eachLine(p, dir + "/library.txt", [&](const std::string& line) {
        if (line.empty() || line[0] == '#') return;
        size_t bar = line.find('|');
        Game g;
        if (bar == std::string::npos) {
            g.path = line;
            size_t slash = line.find_last_of('/');
            g.title = stem(slash == std::string::npos ? line : line.substr(slash + 1));
        } else {
            g.title = line.substr(0, bar);
            g.path = line.substr(bar + 1);
        }
        out.push_back(g);
    });
}

static void scanDir(Platform& p, const SystemConfig& sys, const std::string& dir,
                    std::vector<Game>& out, bool& sandboxed) {
    std::vector<DirEntry> entries;
    if (!p.listDir(dir, entries)) {
        if (p.exists(dir + "/library.txt")) {
            readLibraryTxt(p, dir, out);
            sandboxed = true;
        } else if (p.exists(dir)) {
            sandboxed = true;  // the folder is there but we may not list it
        }
        return;
    }

    for (const auto& e : entries) {
        std::string full = dir + "/" + e.name;
        if (!e.isDir) {
            if (extRank(sys, e.name) >= 0) out.push_back({stem(e.name), full});
            continue;
        }
        // A folder per game: either it holds a marker (PS3_GAME) ...
        if (!sys.marker.empty() && p.exists(full + "/" + sys.marker)) {
            out.push_back({e.name, full});
            continue;
        }
        // ... or the best-ranked game file inside it (.m3u beats .cue, etc.).
        std::vector<DirEntry> inner;
        if (!p.listDir(full, inner)) continue;
        int best = -1;
        std::string bestName;
        for (const auto& f : inner) {
            if (f.isDir) continue;
            int r = extRank(sys, f.name);
            if (r >= 0 && (best < 0 || r < best)) { best = r; bestName = f.name; }
        }
        if (best >= 0) out.push_back({e.name, full + "/" + bestName});
    }
}

// A BIOS file: any name that starts with the prefix (scph) and ends in .bin, or .rom0 for the
// ROM0 part of a split dump, in any case (scph10000.bin, SCPH-39001.bin, scph70012.bin).
static bool isNamedBios(const std::string& name, const std::string& prefix) {
    std::string n;
    for (char c : name) n += (char)std::tolower((unsigned char)c);
    std::string want;
    for (char c : prefix) want += (char)std::tolower((unsigned char)c);
    if (n.size() <= want.size() || n.compare(0, want.size(), want) != 0) return false;
    return (n.size() >= 4 && n.compare(n.size() - 4, 4, ".bin") == 0) ||
           (n.size() >= 5 && n.compare(n.size() - 5, 5, ".rom0") == 0);
}

static void checkBios(Platform& p, SystemState& st, bool dataListable) {
    const SystemConfig& sys = *st.cfg;
    if (sys.biosAll.empty() && sys.biosAny.empty() && sys.biosName.empty()) {
        st.bios = SystemState::Bios::Ok;
        st.biosMsg = "No BIOS needed";
        return;
    }
    std::vector<std::string> missing;
    for (const auto& f : sys.biosAll)
        if (!p.exists(f)) missing.push_back(f);
    bool anyOk = sys.biosAny.empty() && sys.biosName.empty();
    for (const auto& f : sys.biosAny)
        if (p.exists(f)) { anyOk = true; break; }
    if (!anyOk && !sys.biosName.empty()) {
        std::vector<DirEntry> files;
        if (p.listDir(sys.biosDir, files))
            for (const auto& e : files)
                if (!e.isDir && isNamedBios(e.name, sys.biosName)) { anyOk = true; break; }
    }

    if (missing.empty() && anyOk) {
        st.bios = SystemState::Bios::Ok;
        st.biosMsg = "BIOS OK";
        return;
    }
    std::string what;
    if (!anyOk) {
        if (!sys.biosName.empty())
            what += "one " + sys.biosName + " BIOS (" + sys.biosName + "-xxxxx.bin) in " + sys.biosDir + "  ";
        else {
            size_t slash = sys.biosAny.front().find_last_of('/');
            what += "one BIOS in " + sys.biosAny.front().substr(0, slash) + "  ";
        }
    }
    for (const auto& m : missing) what += m + "  ";
    if (dataListable) {
        st.bios = SystemState::Bios::Missing;
        st.biosMsg = "Missing: " + what;
    } else {
        st.bios = SystemState::Bios::Unknown;
        st.biosMsg = "BIOS not verified (sandboxed): " + what;
    }
}

#if defined(__PROSPERO__)
// The PS1 discs in USB drives that can be played, as games under the drive's path.
static void addDiscs(std::vector<Game>& games) {
    for (const auto& disc : fe::discdrive::discs()) {
        if (!disc.playable || disc.ps2 || disc.ps3 || disc.other) continue;
        games.push_back({disc.name.empty() ? disc.serial : disc.name, fe::discdrive::streamPath(disc)});
    }
}

// The discs that are neither PS1, PS2 nor PS3, listed on the PS4 and PS5 tabs. They cannot be played or installed yet.
static void addOtherDiscs(std::vector<Game>& games) {
    for (const auto& disc : fe::discdrive::discs())
        if (disc.playable && disc.other) games.push_back({disc.name.empty() ? disc.serial : disc.name, disc.device});
}

// The PS2 discs in the drives: played from the drive by PS5SX2, whose path is the drive ("/dev/cd1").
static void addPs2Discs(std::vector<Game>& games) {
    for (const auto& disc : fe::discdrive::discs())
        if (disc.playable && disc.ps2) games.push_back({disc.name.empty() ? disc.serial : disc.name, disc.device});
}

// The PS3 discs in the drives: played from the drive by RPCS3-5 (--boot <drive>).
static void addPs3Discs(std::vector<Game>& games) {
    for (const auto& disc : fe::discdrive::discs())
        if (disc.playable && disc.ps3) games.push_back({disc.name.empty() ? disc.serial : disc.name, disc.device});
}
#endif

#if defined(__PROSPERO__)
// A PS1 serial in a game's name ("Crash Bandicoot [SCUS-94900]" gives SCUS-94900), or "".
std::string serialIn(const std::string& title) {
    for (size_t i = 0; i + 10 <= title.size(); ++i) {
        bool match = true;
        for (size_t k = 0; k < 4 && match; ++k) match = std::isalpha((unsigned char)title[i + k]);
        if (!match || (title[i + 4] != '-' && title[i + 4] != '_')) continue;
        for (size_t k = 5; k < 10 && match; ++k) match = std::isdigit((unsigned char)title[i + k]);
        if (match) {
            std::string serial = title.substr(i, 10);
            serial[4] = '-';
            for (char& c : serial) c = (char)std::toupper((unsigned char)c);
            return serial;
        }
    }
    return "";
}

// The name without its brackets and parentheses, in lower case ("Crash (USA)" gives "crash").
static std::string plainName(const std::string& title) {
    std::string out;
    int depth = 0;
    for (char c : title) {
        if (c == '[' || c == '(') ++depth;
        else if ((c == ']' || c == ')') && depth > 0) --depth;
        else if (depth == 0) out += (char)std::tolower((unsigned char)c);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// The same game: the same serial, or, when neither name has one, the same name. Two
// discs of one game have different serials, so they stay apart.
static bool sameGame(const Game& a, const Game& b) {
    const std::string sa = serialIn(a.title), sb = serialIn(b.title);
    if (!sa.empty() || !sb.empty()) return sa == sb;
    return plainName(a.title) == plainName(b.title);
}
#endif

#if defined(__PROSPERO__)
// One entry per game in the library folders: a game in an ISO and in a game folder shows once, as the ISO.
// The discs in the drives are added after this, so they still show beside an installed copy.
static void keepOneCopy(std::vector<Game>& games) {
    auto isIso = [](const Game& g) { return lower(g.path).ends_with(".iso"); };
    std::vector<Game> kept;
    for (const auto& g : games) {
        auto same = std::find_if(kept.begin(), kept.end(), [&](const Game& k) { return sameGame(k, g); });
        if (same == kept.end())
            kept.push_back(g);
        else if (isIso(g) && !isIso(*same))
            *same = g;
    }
    games = std::move(kept);
}
#endif

#if defined(__PROSPERO__)
// PS3 titles from PS3_GAME/PARAM.SFO. The file is plain text on a PS3 disc image and in a game folder, so no key is
// needed. Only the bytes needed are read: the ISO's volume descriptor, its directories, and the PARAM.SFO itself.
static std::string readUpTo(const std::string& path, uint64_t off, size_t n) {
    std::string out(n, '\0');
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return "";
    const ssize_t got = ::pread(fd, out.data(), n, static_cast<off_t>(off));
    ::close(fd);
    if (got <= 0) return "";
    out.resize(static_cast<size_t>(got));
    return out;
}

static uint32_t le32(const std::string& s, size_t at) {
    return static_cast<uint32_t>(static_cast<uint8_t>(s[at])) | (static_cast<uint32_t>(static_cast<uint8_t>(s[at + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[at + 2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(s[at + 3])) << 24);
}

// The record called name in an ISO 9660 directory (dirSize bytes at sector lba). records never cross a sector.
// A disc image to read: its sector size (2048, or 2352 for a raw disc) and where the user data starts in a sector.
struct IsoSource {
    std::string path;
    uint32_t sector = 2048;
    uint32_t userOff = 0;
};

// The user data of bytes at lba, from a 2048-byte or a raw 2352-byte sector image.
static std::string isoRead(const IsoSource& s, uint32_t lba, uint32_t bytes) {
    std::string out;
    for (uint32_t done = 0; done < bytes; done += 2048) {
        const uint32_t want = std::min<uint32_t>(2048, bytes - done);
        out += readUpTo(s.path, static_cast<uint64_t>(lba + done / 2048) * s.sector + s.userOff, want);
    }
    return out;
}

static bool isoFind(const IsoSource& s, uint32_t lba, uint32_t dirSize, const std::string& name, uint32_t& outLba, uint32_t& outSize, bool& outDir) {
    const std::string dir = isoRead(s, lba, dirSize);
    size_t at = 0;
    while (at < dir.size()) {
        const uint8_t len = static_cast<uint8_t>(dir[at]);
        if (len == 0) {
            at = (at / 2048 + 1) * 2048;
            continue;
        }
        if (at + len > dir.size() || at + 33 > dir.size()) break;
        const uint8_t nameLen = static_cast<uint8_t>(dir[at + 32]);
        std::string n = dir.substr(at + 33, nameLen);
        if (const size_t semi = n.find(';'); semi != std::string::npos) n.resize(semi);
        if (n == name) {
            outLba = le32(dir, at + 2);
            outSize = le32(dir, at + 10);
            outDir = (static_cast<uint8_t>(dir[at + 25]) & 2) != 0;
            return true;
        }
        at += len;
    }
    return false;
}

// The TITLE entry of a PARAM.SFO, or "" when it cannot be read.
static std::string sfoValue(const std::string& s, const std::string& key) {
    if (s.size() < 20 || s.compare(0, 4, "\0PSF", 4) != 0) return "";
    const uint32_t keyTable = le32(s, 8), dataTable = le32(s, 12), count = le32(s, 16);
    for (uint32_t i = 0; i < count && 20 + 16 * static_cast<size_t>(i) + 16 <= s.size(); ++i) {
        const size_t e = 20 + 16 * static_cast<size_t>(i);
        const uint32_t keyOff = static_cast<uint8_t>(s[e]) | (static_cast<uint32_t>(static_cast<uint8_t>(s[e + 1])) << 8);
        const uint32_t dataLen = le32(s, e + 4), dataOff = le32(s, e + 12);
        if (keyTable + keyOff >= s.size())
            continue;
        if (std::string(s.c_str() + keyTable + keyOff) != key)
            continue;
        const size_t start = static_cast<size_t>(dataTable) + dataOff;
        if (start >= s.size())
            return "";
        std::string t = s.substr(start, dataLen);
        t.resize(std::char_traits<char>::length(t.c_str()));
        // The launcher's font draws plain ASCII only: other bytes (UTF-8 marks, control codes) come out as noise
        std::string clean;
        for (const char c : t)
            if (c >= 0x20 && c <= 0x7e) clean += c;
        const size_t first = clean.find_first_not_of(' ');
        if (first == std::string::npos) return "";
        return clean.substr(first, clean.find_last_not_of(' ') - first + 1);
    }
    return "";
}

// A PS3 game's name from its PARAM.SFO: the title, with its region from the serial ("Guitar Hero 5 (USA)").
static std::string ps3Named(const std::string& sfo) {
    const std::string id = sfoValue(sfo, "TITLE_ID");
    std::string title = fe::gamedb::ps3Title(id);
    if (title.empty())
        title = sfoValue(sfo, "TITLE");
    const std::string region = fe::discdrive::ps3RegionTag(id);
    return title.empty() || region.empty() ? title : title + " (" + region + ")";
}

// The title of a PS3 game: an ISO file, or a game folder with PS3_GAME inside. "" when it has none.
static std::string ps3TitleOf(const std::string& path) {
    if (lower(path).ends_with(".iso")) {
        const std::string pvd = readUpTo(path, 16 * 2048, 2048);
        if (pvd.size() < 190 || pvd.compare(1, 5, "CD001") != 0)
            return "";
        uint32_t rootLba = le32(pvd, 156 + 2), rootSize = le32(pvd, 156 + 10);
        uint32_t gameLba = 0, gameSize = 0;
        bool isDir = false;
        if (!isoFind({path}, rootLba, rootSize, "PS3_GAME", gameLba, gameSize, isDir) || !isDir)
            return "";
        uint32_t sfoLba = 0, sfoSize = 0;
        if (!isoFind({path}, gameLba, gameSize, "PARAM.SFO", sfoLba, sfoSize, isDir) || isDir || sfoSize == 0 || sfoSize > 8192)
            return "";
        return ps3Named(readUpTo(path, static_cast<uint64_t>(sfoLba) * 2048, sfoSize));
    }
    return ps3Named(readUpTo(path + "/PS3_GAME/PARAM.SFO", 0, 8192));
}

// The serial of a PS2 game from SYSTEM.CNF's BOOT2 line: "cdrom0:\SLUS_200.01;1" becomes "SLUS-20001", the form the
// PS2 list uses. Returns "" when the line is not there.
static std::string ps2SerialFromCnf(const std::string& cnf) {
    const size_t boot = cnf.find("BOOT2");
    if (boot == std::string::npos)
        return "";
    const size_t slash = cnf.find('\\', boot);
    if (slash == std::string::npos)
        return "";
    std::string letters, digits;
    for (size_t i = slash + 1; i < cnf.size() && cnf[i] != ';' && cnf[i] != '\r' && cnf[i] != '\n'; ++i) {
        const char c = cnf[i];
        if (std::isalpha(static_cast<unsigned char>(c))) letters += c;
        else if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
    }
    if (letters.size() != 4 || digits.size() != 5)
        return "";
    return letters + "-" + digits;
}

// The name of a PS2 game in an ISO: its serial from SYSTEM.CNF, looked up in the PS2 list. "" when it has none.
static std::string ps2TitleOf(const std::string& path) {
    if (!lower(path).ends_with(".iso"))
        return "";
    const std::string pvd = readUpTo(path, 16 * 2048, 2048);
    if (pvd.size() < 190 || pvd.compare(1, 5, "CD001") != 0)
        return "";
    const uint32_t rootLba = le32(pvd, 156 + 2), rootSize = le32(pvd, 156 + 10);
    uint32_t cnfLba = 0, cnfSize = 0;
    bool isDir = false;
    if (!isoFind({path}, rootLba, rootSize, "SYSTEM.CNF", cnfLba, cnfSize, isDir) || isDir || cnfSize == 0 || cnfSize > 4096)
        return "";
    const std::string serial = ps2SerialFromCnf(isoRead({path}, cnfLba, cnfSize));
    fe::gamedb::Info info;
    if (serial.empty() || !fe::gamedb::find(serial, info))
        return "";
    return info.name;
}

// The serial of a PS1 game from SYSTEM.CNF's BOOT line: "cdrom:\SLUS_011.83;1" becomes "SLUS-01183".
static std::string ps1SerialFromCnf(const std::string& cnf) {
    const size_t boot = cnf.find("BOOT =");
    if (boot == std::string::npos)
        return "";
    const size_t slash = cnf.find('\\', boot);
    if (slash == std::string::npos)
        return "";
    std::string letters, digits;
    for (size_t i = slash + 1; i < cnf.size() && cnf[i] != ';' && cnf[i] != '\r' && cnf[i] != '\n'; ++i) {
        const char c = cnf[i];
        if (std::isalpha(static_cast<unsigned char>(c))) letters += c;
        else if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
    }
    if (letters.size() != 4 || digits.size() != 5)
        return "";
    return letters + "-" + digits;
}

// The name of a PS1 game from its .cue: the serial in SYSTEM.CNF on the data track of the .cue's .bin. "" when
// it has none. A raw .bin (2352-byte sectors) has a header before each sector's user data.
static std::string ps1TitleOf(const std::string& cuePath) {
    if (!lower(cuePath).ends_with(".cue"))
        return "";
    const std::string cue = readUpTo(cuePath, 0, 4096);
    const size_t file = cue.find("FILE ");
    if (file == std::string::npos)
        return "";
    std::string binName;
    const size_t at = file + 5;
    if (at < cue.size() && cue[at] == '"') {
        const size_t close = cue.find('"', at + 1);
        if (close == std::string::npos)
            return "";
        binName = cue.substr(at + 1, close - at - 1);
    } else {
        const size_t end = cue.find(' ', at);
        binName = cue.substr(at, end == std::string::npos ? std::string::npos : end - at);
    }
    if (binName.empty())
        return "";
    IsoSource src {cuePath.substr(0, cuePath.find_last_of('/')) + "/" + binName, 2048, 0};
    if (cue.find("/2352") != std::string::npos) {
        src.sector = 2352;
        const std::string header = readUpTo(src.path, 16ull * 2352, 24);
        if (header.size() < 24)
            return "";
        src.userOff = static_cast<uint8_t>(header[15]) == 2 ? 24 : 16; // the mode byte: 2 has an 8-byte subheader
    }
    const std::string pvd = isoRead(src, 16, 2048);
    if (pvd.size() < 190 || pvd.compare(1, 5, "CD001") != 0)
        return "";
    const uint32_t rootLba = le32(pvd, 156 + 2), rootSize = le32(pvd, 156 + 10);
    uint32_t cnfLba = 0, cnfSize = 0;
    bool isDir = false;
    if (!isoFind(src, rootLba, rootSize, "SYSTEM.CNF", cnfLba, cnfSize, isDir) || isDir || cnfSize == 0 || cnfSize > 4096)
        return "";
    const std::string serial = ps1SerialFromCnf(isoRead(src, cnfLba, cnfSize));
    fe::gamedb::Info info;
    if (serial.empty() || !fe::gamedb::find(serial, info))
        return "";
    return info.name;
}
#endif

std::vector<SystemState> scanLibrary(Platform& p, const LauncherConfig& cfg, bool dataListable) {
    std::vector<SystemState> out;
    for (const auto& sys : cfg.systems) {
        SystemState st;
        st.cfg = &sys;
        std::vector<Game> found;
        for (const auto& d : sys.gameDirs) scanDir(p, sys, d, found, st.sandboxed);
#if defined(__PROSPERO__)
        // Discs installed from a USB drive or the disc drive go here (src/disc). A game
        // in the other folders that is also installed here is hidden: the installed one stays.
        if (sys.id == "ps1" || sys.id == "ps3") {
            // PS3: the copy in games/ps3 is the one kept, over RPCS3-5's own folder.
            std::vector<Game> installed;
            scanDir(p, sys, sys.id == "ps1" ? "/data/PSLauncher/games/ps1" : "/data/PSLauncher/games/ps3", installed, st.sandboxed);
            found.erase(std::remove_if(found.begin(), found.end(), [&](const Game& g) {
                return std::any_of(installed.begin(), installed.end(),
                                   [&](const Game& i) { return sameGame(i, g); });
            }), found.end());
            st.games = installed;
        }
#endif
        st.games.insert(st.games.end(), found.begin(), found.end());
#if defined(__PROSPERO__)
        if (sys.id == "ps1" || sys.id == "ps3") keepOneCopy(st.games);
        // PS3: the name in PARAM.SFO, which is the game's own title. Done after the duplicates are dropped, so the
        // file-name matching still sees the names the files have.
#if defined(__PROSPERO__)
        if (sys.id == "ps3")
            for (auto& g : st.games)
                if (const std::string t = ps3TitleOf(g.path); !t.empty()) g.title = t;
        // PS2: the name in the PS2 list for the serial in the ISO's SYSTEM.CNF
        if (sys.id == "ps2")
            for (auto& g : st.games)
                if (const std::string t = ps2TitleOf(g.path); !t.empty()) g.title = t;
        // PS1: the name in the PS1 list for the serial in the SYSTEM.CNF on the .cue's data track
        if (sys.id == "ps1")
            for (auto& g : st.games)
                if (const std::string t = ps1TitleOf(g.path); !t.empty()) g.title = t;
#endif
        if (sys.id == "ps1") addDiscs(st.games);
        if (sys.id == "ps2") addPs2Discs(st.games);
        if (sys.id == "ps3") addPs3Discs(st.games);
        if (sys.id == "ps4" || sys.id == "ps5") addOtherDiscs(st.games);
        // PS5: the title_id set in launcher.ini is listed as one game, which starts that installed title.
        if (sys.id == "ps5" && !sys.titleId.empty()) st.games.push_back({"PS5 game " + sys.titleId, sys.titleId});
#endif
        std::sort(st.games.begin(), st.games.end(), [](const Game& a, const Game& b) {
            return lower(a.title) < lower(b.title);
        });
#if defined(__PROSPERO__)
        // The discs in the drives come first, under their own heading (src/main.cpp).
        std::stable_partition(st.games.begin(), st.games.end(),
                              [](const Game& g) { return fe::discdrive::isDiscPath(g.path); });
#endif
        st.games.erase(std::unique(st.games.begin(), st.games.end(),
                                   [](const Game& a, const Game& b) { return a.path == b.path; }),
                       st.games.end());
        checkBios(p, st, dataListable);
        p.log("[" + sys.id + "] " + num(st.games.size()) + " games, " + st.biosMsg +
              (st.sandboxed ? " (sandboxed)" : ""));
        out.push_back(std::move(st));
    }
    return out;
}

Session loadSession(Platform& p, const std::string& dataDir) {
    Session s;
    eachLine(p, dataDir + "/session.ini", [&](const std::string& line) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) return;
        std::string k = line.substr(0, eq);
        int v = std::atoi(line.c_str() + eq + 1);
        if (k == "system") s.system = v;
        else if (k == "game") s.game = v;
    });
    return s;
}

void saveSession(Platform& p, const std::string& dataDir, const Session& s) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "system=%d\ngame=%d\n", s.system, s.game);
    p.writeFile(dataDir + "/session.ini", buf);  // written to a temp file, then renamed
}

void writeHandoff(Platform& p, const std::string& dataDir, const std::string& systemId,
                  const std::string& gamePath) {
    p.makeDirs(dataDir + "/handoff");
    p.writeFile(dataDir + "/handoff/" + systemId + ".txt", gamePath + "\n");
}
