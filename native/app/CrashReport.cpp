#include "CrashReport.h"

#include <execinfo.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>

#include "../core/rlog.h"

namespace mc {
namespace {
char g_path[1024];
unsigned long g_base = 0;
char g_stack[1 << 16];

void onCrash(int sig) {
    int fd = open(g_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char head[96];
        int n = std::snprintf(head, sizeof head, "signal %d base 0x%lx\n", sig, g_base);
        write(fd, head, n);
        void* frames[48];
        int count = backtrace(frames, 48);
        backtrace_symbols_fd(frames, count, fd);
        close(fd);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

void sendFile(const std::string& path, const char* tag, size_t lastLines) {
    std::ifstream in(path.c_str());
    if (!in) return;
    std::deque<std::string> lines;
    std::string line;
    while (std::getline(in, line)) { lines.push_back(line); if (lines.size() > lastLines) lines.pop_front(); }
    for (const std::string& l : lines) rlog("[%s] %s", tag, l.c_str());
}
}  // namespace

void installCrashReport(const std::string& dir) {
    std::string crash = dir + "crash.log", prev = dir + "mc.log";
    if (std::ifstream(crash.c_str())) {                    // the previous run crashed: report it
        sendFile(crash, "crash", 48);
        sendFile(prev, "before-crash", 40);
        std::remove(crash.c_str());
    }
    std::remove(prev.c_str());
    std::snprintf(g_path, sizeof g_path, "%s", crash.c_str());
    g_base = reinterpret_cast<unsigned long>(_dyld_get_image_header(0));
    stack_t ss;
    std::memset(&ss, 0, sizeof ss);
    ss.ss_sp = g_stack;
    ss.ss_size = sizeof g_stack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = onCrash;
    sa.sa_flags = SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int s : {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE, SIGTRAP}) sigaction(s, &sa, nullptr);
}
}  // namespace mc
