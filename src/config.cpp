#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::vector<std::string> splitList(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(sep, start);
        if (end == std::string::npos) end = s.size();
        std::string item = trim(s.substr(start, end - start));
        if (!item.empty()) out.push_back(item);
        start = end + 1;
    }
    return out;
}

static bool toBool(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

bool loadConfigText(const std::string& text, const std::string& path, LauncherConfig& out, std::string& err) {
    out = LauncherConfig{};
    bool inLauncher = false, inSystem = false, inApi = false;
    int lineNo = 0;
    size_t pos = 0;

    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        ++lineNo;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            std::string sec = trim(line.substr(1, line.size() - 2));
            inLauncher = (sec == "launcher");
            inApi = (sec == "api");
            inSystem = sec.rfind("system.", 0) == 0;
            if (inSystem) {
                out.systems.push_back(SystemConfig{});
                out.systems.back().id = sec.substr(7);
                out.systems.back().name = out.systems.back().id;
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            char where[32];
            std::snprintf(where, sizeof where, ":%d", lineNo);
            err = path + where + ": expected key=value";
            return false;
        }
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (inLauncher) {
            if (key == "data_dir") out.dataDir = val;
            else if (key == "exit_after_launch") out.exitAfterLaunch = toBool(val);
        } else if (inApi) {
            if (key == "thegamesdb_key") out.thegamesdbKey = val;
            else if (key == "rawg_key") out.rawgKey = val;
        } else if (inSystem) {
            SystemConfig& cur = out.systems.back();
            if (key == "name") cur.name = val;
            else if (key == "title_id") cur.titleId = val;
            else if (key == "games_dir") cur.gameDirs = splitList(val, '|');
            else if (key == "extensions") {
                cur.extensions = splitList(val, ',');
                for (auto& e : cur.extensions) {
                    std::transform(e.begin(), e.end(), e.begin(),
                                   [](unsigned char c) { return (char)std::tolower(c); });
                    if (e[0] != '.') e = "." + e;
                }
            }
            else if (key == "marker") cur.marker = val;
            else if (key == "bios_any") cur.biosAny = splitList(val, '|');
            else if (key == "bios_all") cur.biosAll = splitList(val, '|');
            else if (key == "bios_dir") cur.biosDir = val;
            else if (key == "bios_name") cur.biosName = val;
            else if (key == "pass_game") cur.passGame = toBool(val);
            else if (key == "launch_file") cur.launchFile = val;
            else if (key == "launch_arg") cur.launchArg = val;
            else if (key == "config_file") cur.configFile = val;
            else if (key == "builtin_core") cur.builtinCore = toBool(val);
            else if (key == "system_dir") cur.systemDir = val;
            else if (key == "desktop_cmd") cur.desktopCmd = val;
        }
    }

    if (out.systems.empty()) { err = path + ": no [system.*] sections"; return false; }
    return true;
}
