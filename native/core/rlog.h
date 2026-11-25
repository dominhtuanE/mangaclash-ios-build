#pragma once
// Remote log: messages are printed locally and POSTed (form field "m") to <logUrl>clientlog by a background thread,
// so the server's request log shows what the app did. Messages logged before rlogSetUrl() are queued.
#include <string>
namespace mc {
void rlogSetUrl(const std::string& baseUrl);      // e.g. "http://192.168.1.5:8890/"
void rlog(const char* fmt, ...);
}  // namespace mc
