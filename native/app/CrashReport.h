#pragma once
// A native crash cannot be reported while it happens: the signal handler writes the backtrace to a file,
// and the next start sends it (and the last lines of the previous run) to the server log.
#include <string>

namespace mc {
void installCrashReport(const std::string& writableDir);   // call before rlogSetFile(); sends the previous run's report
}
