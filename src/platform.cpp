#include "platform.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "helper_client.hpp"

#if defined(__PROSPERO__)
extern "C" int sceSystemServiceLoadExec(const char* path, const char** argv);
#else
#include <sys/wait.h>
#endif

namespace {

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

class PosixPlatform : public Platform {
public:
    PosixPlatform(bool useHelper, bool helperUp) : helper_(useHelper), helperUp_(helperUp) {}

    const char* name() const override {
#if defined(__PROSPERO__)
        return "PS5";
#else
        return "Desktop";
#endif
    }

    // The app's own files (/app0) are always read directly.
    bool viaHelper(const std::string& path) const { return helper_ && !startsWith(path, "/app0"); }

    bool listDir(const std::string& path, std::vector<DirEntry>& out) override {
        if (viaHelper(path)) {
            std::string data;
            if (!helper::requestData("LIST " + path, data)) return false;
            size_t pos = 0;
            while (pos < data.size()) {
                size_t nl = data.find('\n', pos);
                if (nl == std::string::npos) nl = data.size();
                std::string row = data.substr(pos, nl - pos);
                pos = nl + 1;
                if (row.size() < 3 || row[1] != ' ') continue;
                out.push_back({row.substr(2), row[0] == 'D'});
            }
            return true;
        }
        DIR* d = opendir(path.c_str());
        if (!d) return false;
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            DirEntry de;
            de.name = e->d_name;
            struct stat st{};
            de.isDir = (stat((path + "/" + de.name).c_str(), &st) == 0) && S_ISDIR(st.st_mode);
            out.push_back(de);
        }
        closedir(d);
        return true;
    }

    bool exists(const std::string& path) override {
        if (viaHelper(path)) {
            std::string r;
            return helper::request("EXISTS " + path, r) && r.compare(0, 1, "1") == 0;
        }
        struct stat st{};
        if (stat(path.c_str(), &st) == 0) return true;
        // A sandboxed title may fail stat() on files it can still open.
        if (FILE* f = std::fopen(path.c_str(), "rb")) { std::fclose(f); return true; }
        return false;
    }

    void makeDirs(const std::string& path) override {
        if (viaHelper(path)) {
            std::string r;
            helper::request("MKDIR " + path, r);
            return;
        }
        std::string cur;
        for (size_t i = 0; i < path.size(); ++i) {
            cur += path[i];
            if ((path[i] == '/' && i > 0) || i + 1 == path.size()) mkdir(cur.c_str(), 0777);
        }
    }

    bool readFile(const std::string& path, std::string& out) override {
        out.clear();
        if (viaHelper(path)) return helper::requestData("READ " + path, out);
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        char buf[8192];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
        return true;
    }

    bool writeFile(const std::string& path, const std::string& data) override {
        if (viaHelper(path)) {
            std::string r;
            return helper::request("WRITE " + path, r, &data) && r.compare(0, 2, "OK") == 0;
        }
        std::string tmp = path + ".tmp";
        FILE* f = std::fopen(tmp.c_str(), "wb");
        if (!f) return false;
        bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
        ok = std::fclose(f) == 0 && ok;
        return ok && std::rename(tmp.c_str(), path.c_str()) == 0;
    }

    bool launch(const SystemConfig& sys, const std::string& gamePath, std::string& err) override {
        if (helper_ || helperUp_) {
            std::string cmd = "LAUNCH " + sys.titleId;
            if (sys.passGame && !gamePath.empty()) cmd += " " + gamePath;
            if (!sys.launchArg.empty() && !gamePath.empty()) cmd += " " + sys.launchArg + " " + gamePath;
            std::string r;
            if (!helper::request(cmd, r)) { err = "the helper stopped answering"; return false; }
            if (r.compare(0, 2, "OK") != 0) { err = "the helper refused " + sys.titleId + " (check title_id)"; return false; }
            log("helper: " + cmd);
#if defined(__PROSPERO__)
            return true;  // the helper starts it once this app has closed
#endif
        }
#if defined(__PROSPERO__)
        (void)gamePath;
        // Starting a title from inside a sandboxed app crashed it (0.2 on hardware).
        err = "Helper not running: load pslauncher-helper.elf via Payload Manager and reboot";
        return false;
#else
        return launchDesktop(sys, gamePath, err);
#endif
    }

    bool launchBlocks() const override {
#if defined(__PROSPERO__)
        return false;
#else
        return true;
#endif
    }

    void setLogPath(const std::string& path) override { logPath_ = path; }

    void log(const std::string& msg) override {
        char ts[32];
        time_t t = time(nullptr);
        strftime(ts, sizeof ts, "%H:%M:%S", localtime(&t));
        std::string line = std::string(ts) + " " + msg + "\n";
        std::fputs(line.c_str(), stderr);
        if (logPath_.empty()) return;
        if (viaHelper(logPath_)) {
            std::string r;
            helper::request("APPEND " + logPath_, r, &line);
            return;
        }
        if (FILE* f = std::fopen(logPath_.c_str(), "a")) {
            std::fputs(line.c_str(), f);
            std::fclose(f);  // closed every line, so the log survives a crash
        }
    }

    void closeApp() override {
#if defined(__PROSPERO__)
        // As SwanStationPS5 and ProsperoStore do it: LoadExec "exit" ends the title.
        std::fflush(nullptr);
        for (;;) {
            int rc = sceSystemServiceLoadExec("exit", nullptr);
            char b[64];
            std::snprintf(b, sizeof b, "closeApp: LoadExec exit refused 0x%08X", (unsigned)rc);
            log(b);
            sleep(1);
        }
#endif
    }

private:
    bool helper_;
    bool helperUp_;  // the helper answers: launching a title goes through it
    std::string logPath_;

#if !defined(__PROSPERO__)
    static std::string shellQuote(const std::string& s) {
        std::string q = "'";
        for (char c : s) q += (c == '\'') ? std::string("'\\''") : std::string(1, c);
        return q + "'";
    }

    // Desktop stand-in: runs desktop_cmd (e.g. "pcsx2-qt -batch {game}") and waits.
    bool launchDesktop(const SystemConfig& sys, const std::string& gamePath, std::string& err) {
        if (sys.desktopCmd.empty()) {
            log("[sim] would launch title " + sys.titleId + (gamePath.empty() ? "" : " with game " + gamePath));
            return true;
        }
        std::string cmd = sys.desktopCmd;
        size_t p = cmd.find("{game}");
        if (p != std::string::npos) cmd.replace(p, 6, gamePath.empty() ? "" : shellQuote(gamePath));
        log("desktop launch: " + cmd);
        pid_t pid = fork();
        if (pid < 0) { err = std::string("fork: ") + std::strerror(errno); return false; }
        if (pid == 0) {
            execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
            _exit(127);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        char b[48];
        std::snprintf(b, sizeof b, "emulator exited, status %d", WEXITSTATUS(status));
        log(b);
        return true;
    }
#endif
};

}  // namespace

std::unique_ptr<Platform> makePlatform(bool useHelper, bool helperUp) {
    return std::make_unique<PosixPlatform>(useHelper, helperUp);
}
