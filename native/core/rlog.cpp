#include "rlog.h"

#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

#include "net.h"

namespace mc {
namespace {
std::mutex g_mu;
std::condition_variable g_cv;
std::deque<std::string> g_q;
std::string g_url;
bool g_started = false;
std::string g_file;
std::deque<std::string> g_tail;

void worker() {
    for (;;) {
        std::string m, url;
        {
            std::unique_lock<std::mutex> lk(g_mu);
            g_cv.wait(lk, [] { return !g_q.empty() && !g_url.empty(); });
            m = g_q.front(); g_q.pop_front(); url = g_url;
        }
        httpRequest(url + "clientlog", {{"m", m}}, 2000);
    }
}
}  // namespace

void rlogSetFile(const std::string& path) { std::lock_guard<std::mutex> lk(g_mu); g_file = path; }

std::string rlogTail(int lines) {
    std::lock_guard<std::mutex> lk(g_mu);
    std::string out;
    size_t from = g_tail.size() > static_cast<size_t>(lines) ? g_tail.size() - lines : 0;
    for (size_t i = from; i < g_tail.size(); ++i) { out += g_tail[i]; out += '
'; }
    return out;
}

void rlogSetUrl(const std::string& baseUrl) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_url = baseUrl;
    if (!g_started) { g_started = true; std::thread(worker).detach(); }
    g_cv.notify_all();
}

void rlog(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    std::printf("%s\n", buf);
    std::lock_guard<std::mutex> lk(g_mu);
    g_tail.emplace_back(buf);
    if (g_tail.size() > 60) g_tail.pop_front();
    if (!g_file.empty()) { if (FILE* f = std::fopen(g_file.c_str(), "a")) { std::fprintf(f, "%s
", buf); std::fclose(f); } }
    if (g_q.size() < 500) g_q.emplace_back(buf);
    g_cv.notify_all();
}
}  // namespace mc
