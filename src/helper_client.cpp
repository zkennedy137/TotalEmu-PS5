#include "helper_client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>

namespace helper {

static bool sendAll(int s, const char* p, size_t n) {
    while (n > 0) {
        ssize_t w = send(s, p, n, 0);
        if (w <= 0) return false;
        p += w;
        n -= (size_t)w;
    }
    return true;
}

bool request(const std::string& command, std::string& reply, const std::string* body, int timeoutMs) {
    reply.clear();
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    timeval tv = {timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#if !defined(__linux__)
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
#if defined(__PROSPERO__)
    a.sin_len = sizeof a;
#endif
    a.sin_family = AF_INET;
    a.sin_port = htons(kPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, (sockaddr*)&a, sizeof a) != 0) { close(s); return false; }

    std::string head = command + "\n";
    bool ok = sendAll(s, head.data(), head.size());
    if (ok && body) ok = sendAll(s, body->data(), body->size());
    shutdown(s, SHUT_WR);  // tells the helper the body is complete
    char buf[16384];
    ssize_t n;
    while (ok && (n = recv(s, buf, sizeof buf, 0)) > 0) reply.append(buf, (size_t)n);
    close(s);
    return ok && !reply.empty();
}

bool requestData(const std::string& command, std::string& data, const std::string* body) {
    std::string r;
    if (!request(command, r, body) || r.compare(0, 3, "OK\n") != 0) return false;
    data = r.substr(3);
    return true;
}

bool ping(std::string& version) {
    std::string r;
    if (!request("PING", r, nullptr, 500) || r.compare(0, 3, "OK ") != 0) return false;
    version = r.substr(3);
    while (!version.empty() && (version.back() == '\n' || version.back() == '\r')) version.pop_back();
    return true;
}

}  // namespace helper
