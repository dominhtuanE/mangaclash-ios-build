#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mc {

// Packet codec for the game's TCP protocol. Frame (little-endian): u32 bodyLen | u16 code | u16 gs | u32 msgId | payload.
class PacketWriter {
public:
    void begin() { buf_.clear(); }
    bool header(uint16_t code, uint16_t gs);
    bool u32(uint32_t v);
    bool fmt(const std::string& fmt, const std::vector<double>& nums, const std::vector<std::string>& strs);
    bool bytes(const std::string& s, size_t n);          // fixed n bytes, zero padded / truncated (incl. terminator)
    std::string frame() const;                           // prepends the length
private:
    std::string buf_;
};

class PacketReader {
public:
    void load(const std::string& body) { buf_ = body; pos_ = 0; ok_ = true; }
    bool header(uint16_t& code, uint16_t& gs);
    bool u32(uint32_t& v);
    bool str(size_t n, std::string& out);                // fixed n bytes, cut at the first zero
    bool num(char kind, double& out);                    // one numeric format token
    bool ok() const { return ok_; }
private:
    std::string buf_;
    size_t pos_ = 0;
    bool ok_ = true;
};

// TCP client: connect() waits up to timeoutMs, then the socket is non-blocking and poll() drains complete frames.
class Tcp {
public:
    ~Tcp() { close(); }
    bool connect(const std::string& host, int port, int timeoutMs);
    bool send(const std::string& data);
    bool poll(std::vector<std::string>& frames, bool& closed);   // appends complete frame bodies
    int readSome(std::string& out, int timeoutMs);               // raw bytes: >0 got, 0 peer closed, <0 timeout/error
    void close();
    bool open() const { return sock_ != -1; }
private:
    long long sock_ = -1;
    std::string in_;
};

struct HttpResult { int code = -1; std::string body; std::string error; };
// Synchronous HTTP/1.1 form POST (or GET when form is empty). Plain http only; run it off the UI thread.
HttpResult httpRequest(const std::string& url, const std::vector<std::pair<std::string, std::string>>& form, int timeoutMs);

}  // namespace mc
