#include <cstdio>
#include <cstdlib>

#include "net.h"

namespace mc {
namespace {

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

std::string decodeChunked(const std::string& b) {
    std::string out;
    size_t p = 0;
    while (p < b.size()) {
        size_t e = b.find("\r\n", p);
        if (e == std::string::npos) break;
        size_t n = std::strtoul(b.substr(p, e - p).c_str(), nullptr, 16);
        if (n == 0) break;
        out.append(b, e + 2, n);
        p = e + 2 + n + 2;
    }
    return out;
}

}  // namespace

HttpResult httpRequest(const std::string& url, const std::vector<std::pair<std::string, std::string>>& form, int timeoutMs) {
    HttpResult res;
    if (url.compare(0, 7, "http://") != 0) { res.error = "only http:// is supported"; return res; }
    size_t hostStart = 7, slash = url.find('/', hostStart);
    std::string hostPort = url.substr(hostStart, slash == std::string::npos ? std::string::npos : slash - hostStart);
    std::string path = slash == std::string::npos ? "/" : url.substr(slash);
    std::string host = hostPort;
    int port = 80;
    size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos) { host = hostPort.substr(0, colon); port = std::atoi(hostPort.c_str() + colon + 1); }

    std::string body;
    for (auto& kv : form) { if (!body.empty()) body += '&'; body += urlEncode(kv.first) + "=" + urlEncode(kv.second); }
    std::string req = std::string(form.empty() ? "GET " : "POST ") + path + " HTTP/1.1\r\nHost: " + hostPort + "\r\nConnection: close\r\nUser-Agent: MangaClash\r\n";
    if (!form.empty()) req += "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n" + body;

    Tcp t;
    if (!t.connect(host, port, timeoutMs)) { res.error = "connect failed"; return res; }
    if (!t.send(req)) { res.error = "send failed"; return res; }
    std::string raw;
    for (;;) {
        int n = t.readSome(raw, timeoutMs);
        if (n == 0) break;
        if (n < 0) { res.error = "read timeout"; return res; }
        size_t he = raw.find("\r\n\r\n");
        if (he != std::string::npos) {
            size_t cl = raw.find("Content-Length:");
            if (cl != std::string::npos && cl < he && raw.size() >= he + 4 + static_cast<size_t>(std::atoi(raw.c_str() + cl + 15))) break;
            if (raw.find("0\r\n\r\n", he) != std::string::npos && raw.find("chunked") < he) break;
        }
    }
    size_t he = raw.find("\r\n\r\n");
    if (he == std::string::npos || raw.compare(0, 5, "HTTP/") != 0) { res.error = "bad response"; return res; }
    res.code = std::atoi(raw.c_str() + raw.find(' ') + 1);
    std::string head = raw.substr(0, he), b = raw.substr(he + 4);
    res.body = head.find("chunked") != std::string::npos ? decodeChunked(b) : b;
    return res;
}

}  // namespace mc
