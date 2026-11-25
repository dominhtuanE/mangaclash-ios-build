"""Copy ../native -> ./native (sources only) before committing; strips the script password and server IP for the public repo."""
import os, re, shutil
here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, "..", "native")
dst = os.path.join(here, "native")
shutil.rmtree(dst, ignore_errors=True)
shutil.copytree(src, dst, ignore=shutil.ignore_patterns("out", "tests", "*.o", "*.exe", "build_host.sh", "BUILD_IOS.md", "secret.h"))
h = os.path.join(dst, "core", "host.cpp")
t = open(h, encoding="utf-8").read()
t = re.sub(r'const char\* kPassword = "[^"]*";', 'const char* kPassword = MC_SCRIPT_PASSWORD;', t)
t = t.replace('#include <cstring>\n', '#include <cstring>\n#include "secret.h"   // CI writes this from the GitHub secret MC_SCRIPT_PASSWORD\n', 1)
open(h, "w", encoding="utf-8", newline="").write(t)
print("synced + scrubbed", dst)
