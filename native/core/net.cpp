#include "net.h"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define MC_INVALID INVALID_SOCKET
static void sockInit() { static bool done = false; if (!done) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); done = true; } }
static void sockClose(sock_t s) { closesocket(s); }
static void setNonBlocking(sock_t s, bool on) { u_long m = on ? 1 : 0; ioctlsocket(s, FIONBIO, &m); }
static bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
typedef int sock_t;
#define MC_INVALID (-1)
static void sockInit() {}
static void sockClose(sock_t s) { ::close(s); }
static void setNonBlocking(sock_t s, bool on) { int f = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, on ? (f | O_NONBLOCK) : (f & ~O_NONBLOCK)); }
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
#endif

namespace mc {
namespace {

// A name cut to its fixed-size field can end inside a UTF-8 character; iOS text APIs reject the whole string then.
void trimUtf8(std::string& s) {
    size_t n = s.size(), back = 0;
    while (back < 4 && back < n && (static_cast<unsigned char>(s[n - 1 - back]) & 0xC0) == 0x80) ++back;
    if (back >= n) return;
    unsigned char lead = static_cast<unsigned char>(s[n - 1 - back]);
    size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (need > 1 && back + 1 < need) s.resize(n - 1 - back);
}

template <typename T> void put(std::string& b, T v) { b.append(reinterpret_cast<const char*>(&v), sizeof(T)); }   // little-endian hosts only (iOS arm64, x86)

}  // namespace

bool PacketWriter::header(uint16_t code, uint16_t gs) { put(buf_, code); put(buf_, gs); return true; }
bool PacketWriter::u32(uint32_t v) { put(buf_, v); return true; }
bool PacketWriter::bytes(const std::string& s, size_t n) {
    std::string t = s.substr(0, n);                 // char[n] holds up to n bytes (a 32-hex login ticket fills it; no NUL reserved)
    t.resize(n, '\0');
    buf_ += t;
    return true;
}
bool PacketWriter::fmt(const std::string& f, const std::vector<double>& nums, const std::vector<std::string>& strs) {
    size_t ni = 0, si = 0;
    for (size_t i = 0; i < f.size();) {
        char k = f[i++];
        size_t n = 0;
        while (i < f.size() && f[i] >= '0' && f[i] <= '9') n = n * 10 + (f[i++] - '0');
        if (k == 's') { bytes(si < strs.size() ? strs[si++] : "", n); continue; }
        double v = ni < nums.size() ? nums[ni++] : 0;
        switch (k) {
            case 'i': put(buf_, static_cast<int32_t>(v)); break;
            case 'I': put(buf_, static_cast<uint32_t>(static_cast<int64_t>(v))); break;
            case 'h': put(buf_, static_cast<int16_t>(v)); break;
            case 'H': put(buf_, static_cast<uint16_t>(v)); break;
            case 'c': put(buf_, static_cast<int8_t>(v)); break;
            case 'C': put(buf_, static_cast<uint8_t>(v)); break;
            case 'f': put(buf_, static_cast<float>(v)); break;
            default: return false;
        }
    }
    return true;
}
std::string PacketWriter::frame() const {
    std::string out;
    put(out, static_cast<uint32_t>(buf_.size()));
    return out + buf_;
}

bool PacketReader::header(uint16_t& code, uint16_t& gs) {
    if (pos_ + 4 > buf_.size()) return ok_ = false;
    std::memcpy(&code, &buf_[pos_], 2);
    std::memcpy(&gs, &buf_[pos_ + 2], 2);
    pos_ += 4;
    return true;
}
bool PacketReader::u32(uint32_t& v) {
    if (pos_ + 4 > buf_.size()) return ok_ = false;
    std::memcpy(&v, &buf_[pos_], 4);
    pos_ += 4;
    return true;
}
bool PacketReader::str(size_t n, std::string& out) {
    if (pos_ + n > buf_.size()) { out.clear(); return ok_ = false; }
    out.assign(buf_, pos_, n);
    pos_ += n;
    size_t z = out.find('\0');
    if (z != std::string::npos) out.resize(z);
    trimUtf8(out);
    return true;
}
bool PacketReader::num(char kind, double& out) {
    auto take = [&](void* dst, size_t n) {
        if (pos_ + n > buf_.size()) { ok_ = false; return false; }
        std::memcpy(dst, &buf_[pos_], n);
        pos_ += n;
        return true;
    };
    switch (kind) {
        case 'i': { int32_t v; if (!take(&v, 4)) return false; out = v; return true; }
        case 'I': { uint32_t v; if (!take(&v, 4)) return false; out = v; return true; }
        case 'h': { int16_t v; if (!take(&v, 2)) return false; out = v; return true; }
        case 'H': { uint16_t v; if (!take(&v, 2)) return false; out = v; return true; }
        case 'c': { int8_t v; if (!take(&v, 1)) return false; out = v; return true; }
        case 'C': { uint8_t v; if (!take(&v, 1)) return false; out = v; return true; }
        case 'f': { float v; if (!take(&v, 4)) return false; out = v; return true; }
    }
    return ok_ = false;
}

bool Tcp::connect(const std::string& host, int port, int timeoutMs) {
    close();
    sockInit();
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return false;
    sock_t s = MC_INVALID;
    for (addrinfo* a = res; a; a = a->ai_next) {
        s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == MC_INVALID) continue;
        setNonBlocking(s, true);
        int r = ::connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen));
        if (r != 0 && !wouldBlock()) { sockClose(s); s = MC_INVALID; continue; }
        if (r != 0) {
            fd_set w;
            FD_ZERO(&w);
            FD_SET(s, &w);
            timeval tv;
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            if (select(static_cast<int>(s) + 1, nullptr, &w, nullptr, &tv) <= 0) { sockClose(s); s = MC_INVALID; continue; }
        }
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
        if (err != 0) { sockClose(s); s = MC_INVALID; continue; }
        break;
    }
    freeaddrinfo(res);
    if (s == MC_INVALID) return false;
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    sock_ = static_cast<long long>(s);
    in_.clear();
    return true;
}

bool Tcp::send(const std::string& data) {
    if (sock_ == -1) return false;
    size_t off = 0;
    while (off < data.size()) {
        int n = static_cast<int>(::send(static_cast<sock_t>(sock_), data.data() + off, static_cast<int>(data.size() - off), 0));
        if (n > 0) { off += static_cast<size_t>(n); continue; }
        if (n < 0 && wouldBlock()) {
            fd_set w;
            FD_ZERO(&w);
            FD_SET(static_cast<sock_t>(sock_), &w);
            timeval tv;
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            if (select(static_cast<int>(sock_) + 1, nullptr, &w, nullptr, &tv) <= 0) return false;
            continue;
        }
        return false;
    }
    return true;
}

bool Tcp::poll(std::vector<std::string>& frames, bool& closed) {
    closed = false;
    if (sock_ == -1) { closed = true; return false; }
    char tmp[16384];
    for (;;) {
        int n = static_cast<int>(::recv(static_cast<sock_t>(sock_), tmp, sizeof(tmp), 0));
        if (n > 0) { in_.append(tmp, static_cast<size_t>(n)); continue; }
        if (n == 0) closed = true;
        else if (!wouldBlock()) closed = true;
        break;
    }
    for (;;) {
        if (in_.size() < 4) break;
        uint32_t len;
        std::memcpy(&len, in_.data(), 4);
        if (len > (64u << 20)) { closed = true; break; }
        if (in_.size() < 4 + static_cast<size_t>(len)) break;
        frames.emplace_back(in_, 4, len);
        in_.erase(0, 4 + static_cast<size_t>(len));
    }
    if (closed) close();
    return !frames.empty();
}

int Tcp::readSome(std::string& out, int timeoutMs) {
    if (sock_ == -1) return -1;
    fd_set r;
    FD_ZERO(&r);
    FD_SET(static_cast<sock_t>(sock_), &r);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    if (select(static_cast<int>(sock_) + 1, &r, nullptr, nullptr, &tv) <= 0) return -1;
    char tmp[16384];
    int n = static_cast<int>(::recv(static_cast<sock_t>(sock_), tmp, sizeof(tmp), 0));
    if (n > 0) out.append(tmp, static_cast<size_t>(n));
    return n;
}

void Tcp::close() {
    if (sock_ != -1) sockClose(static_cast<sock_t>(sock_));
    sock_ = -1;
}

}  // namespace mc
