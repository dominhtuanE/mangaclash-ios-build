#pragma once
// Remote log: messages are printed locally and POSTed (form field "m") to <logUrl>clientlog by a background thread,
// so the server's request log shows what the app did. Messages logged before rlogSetUrl() are queued.
#include <string>
namespace mc {
void rlogSetUrl(const std::string& baseUrl);      // e.g. "http://192.168.1.5:8890/"
void rlog(const char* fmt, ...);
void rlogSetFile(const std::string& path);        // also append every line to this file (iOS: Documents/mc.log, visible in the Files app)
void rlogTrace(const char* fmt, ...);            // append to the file only (breadcrumbs)
std::string rlogTail(int lines);                  // last N lines, for the on-screen debug overlay
}  // namespace mc
