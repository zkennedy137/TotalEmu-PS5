/*
 * PS Launcher - elevation client.
 * Ported from ps5-native-app-boilerplate by BlackBearReloaded.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sends /app0/lapy.elf to the ELF loader on port 9021, which unsandboxes
 * this process so the emulator core can open /data files directly.
 *
 * Key difference from PSSwanStation's original: the sceNet* calls are
 * replaced with BSD socket() / connect() / send() / recv(), which the
 * PS5 payload SDK provides and which work identically for this use.
 */
#if defined(__PROSPERO__)

#include "elevation.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Wire protocol (24-byte message, little-endian).
namespace wire {
enum class Kind : std::uint32_t { request = 1, prepare = 2, prepared = 3, response = 4 };
struct Message {
    std::uint32_t magic    = 0x31564c45;
    std::uint16_t version  = 1;
    std::uint16_t size     = 24;
    Kind          kind     = Kind::request;
    elevation::Capability capability = elevation::Capability::filesystem;
    std::uint32_t pid      = 0;
    elevation::Status     status     = elevation::Status::ok;
};
static_assert(sizeof(Message) == 24);
constexpr int io_timeout_us = 5'000'000;

static bool valid(const Message &m) {
    return m.magic == Message{}.magic && m.size == 24 && m.version == 1 &&
           m.kind >= Kind::request && m.kind <= Kind::response &&
           m.pid > 1 && m.pid <= INT32_MAX &&
           m.capability == elevation::Capability::filesystem;
}
static bool matches(const Message &m, const Message &req, Kind k) {
    return valid(m) && m.kind == k && m.pid == req.pid && m.capability == req.capability;
}
}  // namespace wire

namespace {

constexpr char kRequest[]  = "/download0/elevate_proc";
constexpr char kResult[]   = "/download0/lapy_owned_result";
constexpr unsigned kResidentPolls = 30;
constexpr unsigned kGracePolls    = 20;
constexpr useconds_t kPollUs      = 50000;
const char *g_path = "none";

// ---------- helpers ----------

static bool writeAll(int fd, const void *data, size_t n) noexcept {
    const char *p = static_cast<const char *>(data);
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w <= 0) return false;
        p += w; n -= (size_t)w;
    }
    return true;
}

static bool publishRequest(pid_t pid) noexcept {
    char tmp[80], body[64];
    int bn = std::snprintf(body, sizeof body, "{\"PID\":%ld}\n", (long)pid);
    int tn = std::snprintf(tmp,  sizeof tmp,  "/download0/.elevate_proc.%ld", (long)pid);
    if (bn <= 0 || (size_t)bn >= sizeof body || tn <= 0 || (size_t)tn >= sizeof tmp) return false;
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0 || !writeAll(fd, body, (size_t)bn)) { unlink(tmp); if (fd >= 0) close(fd); return false; }
    close(fd);
    return rename(tmp, kRequest) == 0;
}

static bool verifyData(pid_t pid, int &openErr) noexcept {
    char path[80];
    int n = std::snprintf(path, sizeof path, "/data/.lapy_probe_%ld", (long)pid);
    if (n <= 0 || (size_t)n >= sizeof path) { openErr = EOVERFLOW; return false; }
    unlink(path);
    const char token[] = "LAPYOWN\n";
    char actual[8] = {};
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    openErr = fd < 0 ? errno : 0;
    bool ok = fd >= 0 && writeAll(fd, token, 8) &&
              lseek(fd, 0, SEEK_SET) == 0 &&
              read(fd, actual, 8) == 8 &&
              std::memcmp(actual, token, 8) == 0;
    if (fd >= 0) close(fd);
    unlink(path);
    if (!ok) return false;
    DIR *d = opendir("/data");
    if (!d) { openErr = errno ? errno : EPERM; return false; }
    closedir(d);
    return true;
}

static bool waitForData(pid_t pid, unsigned polls, int &openErr, bool &proofFailed) noexcept {
    for (unsigned i = 0; i < polls; ++i) {
        if (verifyData(pid, openErr)) return true;
        if (openErr == 0) { proofFailed = true; return false; }
        usleep(kPollUs);
    }
    return false;
}

static void reportResult(int fd, bool ok, int openErr) noexcept {
    char buf[96];
    int n = std::snprintf(buf, sizeof buf, "DATA_OK=%d OPEN_ERRNO=%d\n", ok, openErr);
    if (n > 0) writeAll(fd, buf, (size_t)n);
}

// ---------- resident path ----------

struct ResidentResult { elevation::Status status; bool allowHelper; };

static ResidentResult tryResident(pid_t pid) noexcept {
    using elevation::Status;
    int rfd = open(kResult, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (rfd < 0) return {Status::unavailable, true};
    fchmod(rfd, 0644);
    if (seteuid(geteuid()) != 0) { close(rfd); return {Status::prepare_failed, false}; }
    if (!publishRequest(pid)) { close(rfd); return {Status::transport_error, true}; }

    int openErr = 0; bool proofFailed = false;
    if (waitForData(pid, kResidentPolls, openErr, proofFailed)) {
        reportResult(rfd, true, 0); close(rfd); return {Status::ok, false};
    }
    if (proofFailed) { reportResult(rfd, false, openErr); close(rfd); return {Status::apply_failed, false}; }
    close(rfd);

    errno = 0;
    if (unlink(kRequest) != 0) {
        if (errno != ENOENT) return {Status::transport_error, false};
        if (waitForData(pid, kGracePolls, openErr, proofFailed)) return {Status::ok, false};
        return {proofFailed ? Status::apply_failed : Status::timeout, false};
    }
    if (waitForData(pid, kGracePolls, openErr, proofFailed)) return {Status::ok, false};
    unlink(kResult);
    return {proofFailed ? Status::apply_failed : Status::timeout, !proofFailed};
}

// ---------- ELF-loader helper path (BSD sockets) ----------

static bool sendAll(int s, const void *data, size_t n) noexcept {
    const char *p = static_cast<const char *>(data);
    while (n > 0) {
        ssize_t w = send(s, p, n, 0);
        if (w <= 0) return false;
        p += w; n -= (size_t)w;
    }
    return true;
}

static bool recvMsg(int s, wire::Message &m) noexcept {
    char *p = reinterpret_cast<char *>(&m);
    size_t n = sizeof m;
    while (n > 0) {
        ssize_t r = recv(s, p, n, 0);
        if (r <= 0) return false;
        p += r; n -= (size_t)r;
    }
    return true;
}

static elevation::Status exchange(int s, const wire::Message &req) noexcept {
    using elevation::Status;
    if (!sendAll(s, &req, sizeof req)) return Status::transport_error;
    wire::Message reply{};
    if (!recvMsg(s, reply)) return Status::transport_error;
    if (wire::matches(reply, req, wire::Kind::response) && reply.status != Status::ok)
        return reply.status;
    if (!wire::matches(reply, req, wire::Kind::prepare) || reply.status != Status::ok)
        return Status::protocol_error;

    wire::Message prepared = req;
    prepared.kind = wire::Kind::prepared;
    if (seteuid(geteuid()) != 0) prepared.status = Status::prepare_failed;
    if (!sendAll(s, &prepared, sizeof prepared) || !recvMsg(s, reply))
        return Status::transport_error;
    if (!wire::matches(reply, req, wire::Kind::response)) return Status::protocol_error;
    if (prepared.status != Status::ok) return Status::prepare_failed;
    return reply.status;
}

static elevation::Status runHelper(const char *path, const wire::Message &req) noexcept {
    using elevation::Status;
    int elffd = open(path, O_RDONLY);
    if (elffd < 0) return Status::unavailable;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { close(elffd); return Status::transport_error; }

    struct timeval tv = {kPollUs / 1000000, kPollUs % 1000000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    sockaddr_in addr{};
#if defined(__PROSPERO__)
    addr.sin_len    = sizeof addr;
#endif
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(9021);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // Try OnionHEN's persistent internal loader (9020) first; if that fails,
    // fall back to the initial elfldr (9021), which may still be open.
    Status status = Status::transport_error;
    bool connected = false;
    for (uint16_t port : {(uint16_t)9020, (uint16_t)9021}) {
        addr.sin_port = htons(port);
        if (connect(s, (sockaddr *)&addr, sizeof addr) == 0) { connected = true; break; }
    }
    if (connected) {
        char buf[4096]; bool ok = true;
        for (;;) {
            ssize_t n = read(elffd, buf, sizeof buf);
            if (n == 0) break;
            if (n < 0 || !sendAll(s, buf, (size_t)n)) { ok = false; break; }
        }
        if (ok) status = exchange(s, req);
    }
    close(s); close(elffd);
    return status;
}

}  // namespace

// ---------- public API ----------

elevation::Status elevation::request(Capability cap, const char *helperPath) noexcept {
    g_path = "none";
    if (cap != Capability::filesystem) return Status::unsupported_capability;
    const pid_t pid = getpid();
    wire::Message msg{};
    msg.pid = (std::uint32_t)pid;
    msg.capability = cap;
    if (!wire::valid(msg)) return Status::invalid_request;

    int openErr = 0;
    if (verifyData(pid, openErr)) { g_path = "existing"; return Status::ok; }
    if (openErr == 0) return Status::apply_failed;

    const ResidentResult res = tryResident(pid);
    if (!res.allowHelper) { g_path = "resident"; return res.status; }

    g_path = "helper";
    Status s = runHelper(helperPath, msg);
    if (s != Status::ok) return s;

    bool proofFailed = false;
    return waitForData(pid, kGracePolls, openErr, proofFailed) ? Status::ok
           : proofFailed ? Status::apply_failed : Status::timeout;
}

const char *elevation::statusString(Status s) noexcept {
    switch (s) {
        case Status::ok:                    return "ok";
        case Status::invalid_request:       return "invalid_request";
        case Status::unsupported_version:   return "unsupported_version";
        case Status::unsupported_capability:return "unsupported_capability";
        case Status::target_mismatch:       return "target_mismatch";
        case Status::unavailable:           return "unavailable";
        case Status::prepare_failed:        return "prepare_failed";
        case Status::apply_failed:          return "apply_failed";
        case Status::rollback_failed:       return "rollback_failed";
        case Status::transport_error:       return "transport_error";
        case Status::protocol_error:        return "protocol_error";
        case Status::timeout:              return "timeout";
        default:                            return "unknown";
    }
}

const char *elevation::path() noexcept { return g_path; }

#endif  // __PROSPERO__
