// PS Launcher - asking the HEN to let this title list /data.
// Ported from SwanStationPS5's src/platform/ps5_unlock.c. SPDX-License-Identifier: GPL-3.0-or-later
//
// A sandboxed title can open files under /data but can't list folders (and stat()
// may fail). Services that lift this:
//  - LegacyJB, OnionHEN (and PS5SX2 Helper, for whitelisted title IDs) watch for
//    {"PID":<pid>} in /download0/etahen_jailbreak;
//  - etaHEN answers a command on TCP 127.0.0.1:9028 when its "Legacy CMD server"
//    setting is on.
// Ask first thing in main(), before SDL starts threads. A marker file prevents a
// crash loop: if the previous request killed the app, this launch skips it.
#if defined(__PROSPERO__)
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

extern "C" int sceKernelUsleep(unsigned microseconds);

namespace {

const char* const kRequests[] = {"/download0/etahen_jailbreak", "/download0/onionhen_jailbreak"};
const char* const kMarker = "/download0/pslauncher_unlock_attempt";

bool listable() {
    DIR* d = opendir("/data");
    if (!d) return false;
    closedir(d);
    return true;
}

bool publish(const char* path, int pid) {
    char staged[64];
    std::snprintf(staged, sizeof staged, "%s.tmp", path);
    unlink(staged);
    unlink(path);
    int fd = open(staged, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) return false;
    fchmod(fd, 0666);
    char body[32];
    int n = std::snprintf(body, sizeof body, "{\"PID\":%d}\n", pid);
    bool ok = write(fd, body, (size_t)n) == n && fsync(fd) == 0;
    close(fd);
    if (!ok || rename(staged, path) != 0) { unlink(staged); return false; }
    return true;
}

struct HijackerCommand {
    int magic;  // 0xDEADBEEF
    int cmd;    // 5 = jailbreak
    int pid;
    int ret;    // -1337 until answered
    char msg1[0x500];
    char msg2[0x500];
};

bool requestOverTcp(int pid) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    timeval tv = {2, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_len = sizeof a;
    a.sin_family = AF_INET;
    a.sin_port = htons(9028);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool ok = false;
    if (connect(s, (sockaddr*)&a, sizeof a) == 0) {
        static HijackerCommand c;
        std::memset(&c, 0, sizeof c);
        c.magic = (int)0xDEADBEEF;
        c.cmd = 5;
        c.pid = pid;
        c.ret = -1337;
        if (send(s, &c, sizeof c, 0) == (ssize_t)sizeof c) {
            size_t got = 0;
            while (got < sizeof c) {
                ssize_t n = recv(s, (char*)&c + got, sizeof c - got, 0);
                if (n <= 0) break;
                got += (size_t)n;
            }
            ok = got >= 16 && (c.ret == 0 || c.ret == -1337);
        }
    }
    close(s);
    return ok;
}

bool waitListable(int polls) {
    for (int i = 0; i < polls; ++i) {
        sceKernelUsleep(16667);
        if (listable()) return true;
    }
    return false;
}

}  // namespace

// Returns a short status for the log and the screen.
const char* pslUnlockData() {
    if (listable()) return "/data open";
    if (access(kMarker, F_OK) == 0) {
        unlink(kMarker);
        return "sandboxed (last unlock request crashed; skipped once)";
    }
    int m = open(kMarker, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (m >= 0) { fsync(m); close(m); }

    seteuid(geteuid());  // own credential before the HEN changes it
    const int pid = (int)getpid();
    const char* result = "sandboxed (no service answered)";
    int published = 0;
    for (const char* r : kRequests) published += publish(r, pid);
    if (published && waitListable(90)) result = "/data unlocked (request file)";
    for (const char* r : kRequests) unlink(r);
    if (!listable() && requestOverTcp(pid) && waitListable(60)) result = "/data unlocked (etaHEN 9028)";
    if (!published && !listable()) result = "sandboxed (can't write request)";
    unlink(kMarker);
    return result;
}
#endif
