// PS Launcher - one HTTPS request, to test whether the launcher can reach the internet over TLS.
// Run only when /data/PSLauncher/http-probe exists; the result goes to main.log. The Sony functions are called
// directly, as PSSwanStation's HTTP client does: the build loads a Sony module only when the code uses one of its
// functions, so dlopen finds nothing in this app. When the console's client refuses the certificate (0x8095f00c,
// after the sandbox elevator), the request is made again with the TLS client in src/tls.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "tls/tls_client.h"

#if defined(__PROSPERO__)
extern "C"
{
int sceNetInit(void);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceSslInit(size_t poolSize);
int sceHttp2Init(int netPool, int sslContext, size_t poolSize, int maxRequests);
int sceHttp2CreateTemplate(int context, const char *userAgent, int httpVersion, int autoProxy);
int sceHttp2CreateRequestWithURL(int templateId, const char *method, const char *url, uint64_t contentLength);
int sceHttp2SendRequest(int request, const void *data, size_t size);
int sceHttp2GetStatusCode(int request, int *status);
int sceHttp2ReadData(int request, void *data, size_t size);
int sceHttp2SetTimeOut(int id, uint32_t usec);
int sceNetCtlInit(void);
}

namespace {
constexpr int CertificateRefused = (int)0x8095f00c;

// The console's own client: the status and the size of the body, or the error it stopped with.
std::string consoleRequest(const std::string& url, int& status, size_t& total, int& error) {
    char line[256];
    sceNetCtlInit();
    sceNetInit();  // an error only means the network was already up
    const int pool = sceNetPoolCreate("pslauncher-probe", 64 * 1024, 0);
    const int ssl = pool >= 0 ? sceSslInit(256 * 1024) : -1;
    const int context = ssl >= 0 ? sceHttp2Init(pool, ssl, 256 * 1024, 1) : -1;
    const int templ = context >= 0 ? sceHttp2CreateTemplate(context, "PS-Launcher-probe", 3, 1) : -1;
    if (templ < 0) {
        std::snprintf(line, sizeof line, "http probe: no HTTP template (pool %#x, ssl %#x, http2 %#x, template %#x)",
                      (unsigned)pool, (unsigned)ssl, (unsigned)context, (unsigned)templ);
        error = 0;
        return line;
    }
    const int request = sceHttp2CreateRequestWithURL(templ, "GET", url.c_str(), 0);
    if (request < 0) {
        std::snprintf(line, sizeof line, "http probe: request not created (%#x)", (unsigned)request);
        error = 0;
        return line;
    }
    sceHttp2SetTimeOut(request, 15 * 1000 * 1000);
    const int sent = sceHttp2SendRequest(request, nullptr, 0);
    if (sent < 0 || sceHttp2GetStatusCode(request, &status) < 0) {
        error = sent;
        std::snprintf(line, sizeof line, "http probe: request failed (send %#x)", (unsigned)sent);
        return line;
    }
    total = 0;
    char buf[4096];
    for (int n = sceHttp2ReadData(request, buf, sizeof buf); n > 0; n = sceHttp2ReadData(request, buf, sizeof buf))
        total += (size_t)n;
    std::snprintf(line, sizeof line, "http probe: HTTP %d, %zu bytes from %s", status, total, url.c_str());
    error = 0;
    return line;
}
}

// The result as one line for main.log.
std::string httpsProbe(const std::string& url) {
    int status = 0;
    size_t total = 0;
    int error = 0;
    const std::string line = consoleRequest(url, status, total, error);
    if (error != CertificateRefused)
        return line;

    std::vector<uint8_t> body;
    std::string why;
    char text[256];
    const int code = fe::tls::get(url, body, 15, 8u << 20, &why);
    if (code < 0) {
        std::snprintf(text, sizeof text, "http probe: console client refused the certificate; TLS fallback failed (%s)",
                      why.c_str());
    } else {
        std::snprintf(text, sizeof text, "http probe: console client refused the certificate; TLS fallback HTTP %d, %zu bytes from %s",
                      code, body.size(), url.c_str());
    }
    return text;
}
#else
std::string httpsProbe(const std::string& url) {
    (void)url;
    return "http probe: this build has no network client";
}
#endif
